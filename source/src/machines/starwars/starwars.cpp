#include "starwars.h"
#include "starwars_rom.h"
#include "starwars_vrom.h"
#include "starwars_srom.h"
#include "starwars_mproms.h"
#include "../../emulation/arena.h"
#include "../../emulation/pokey.h"
#include <new>


// main CPU memory map (MAME main_map)
enum : uint16_t {
  VROM = 0x3000, // 0x3000-0x3fff vector ROM
  VROM_SIZE = 0x1000,
  IN0 = 0x4300,
  IN1 = 0x4320,
  DSW0 = 0x4340,
  DSW1 = 0x4360,
  ADC = 0x4380,
  LATCH = 0x4400,       // read: main latch, write: sound latch
  LATCH_FLAGS = 0x4401,
  NVRAM = 0x4500,       // 0x4500-0x45ff
  AVG_GO = 0x4600,
  IRQ_ACK = 0x4660,
  OUTLATCH = 0x4680,    // 0x4680-0x469f, Q = a & 7, D = data bit 7
  ADC_START = 0x46c0,   // 0x46c0-0x46c3, a & 3 = channel
  SOUND_RESET = 0x46e0,
  MATH = 0x4700,        // 0x4700-0x4707 write, 0x4700/1/3 read
  CPU_RAM = 0x4800,     // 0x4800-0x4fff
  MATH_RAM = 0x5000,    // 0x5000-0x5fff
  BANK = 0x6000,        // 0x6000-0x7fff
  ROM = 0x8000,
};
// 0x4620 AVG reset, 0x4640 watchdog, 0x46a0 NVRAM store: ignored

// sound CPU memory map (MAME sound_map)
enum : uint16_t {
  S_LATCH_W = 0x0000, // 0x0000-0x07ff write: main latch
  S_LATCH_R = 0x0800, // 0x0800-0x0fff read: sound latch
  S_RIOT_RAM = 0x1000,
  S_RIOT_IO = 0x1080, // 0x1080-0x109f
  S_POKEY = 0x1800,   // 0x1800-0x183f
  S_RAM = 0x2000,     // 0x2000-0x27ff
  S_ROM = 0x4000,     // 0x4000-0x7fff, mirrored at 0xc000-0xffff
  S_ROM_HI = 0xc000,
  S_ROM_SIZE = 0x4000,
};

// RAM inside machineBase::memory
enum : uint16_t {
  MAIN_RAM_OFS = 0x0000, // 0x0000-0x2fff, vector RAM
  CPU_RAM_OFS = 0x3000,  // 0x4800-0x4fff
  SOUND_RAM_OFS = 0x3800, // sound 0x2000-0x27ff
  SOUND_RAM_SIZE = 0x0800,
};

// main ROM asset: 0x6000-0xffff (bank page 0 first), then bank page 1
static const uint16_t BANK_SIZE = 0x2000;
static const uint16_t BANK1_OFS = 0xa000;

// IN0 / IN1, active low unless noted
enum : uint8_t {
  IN0_COIN1 = 0x02,
  IN0_BUTTON1 = 0x80, // right trigger
  IN0_IDLE = 0xdf,    // bit 5 unused, active high
  IN1_IDLE = 0x34,    // diag step, buttons 2 + 3 off
  IN1_VG_HALT = 0x40, // active high: AVG done, always (it draws instantly)
};
// main_read() 0x4401: latch pending flags
enum : uint8_t { SOUND_PENDING = 0x80, MAIN_PENDING = 0x40 };

// outlatch Q bits (MAME 9L/M)
enum : uint8_t { OUT_LED3 = 2, OUT_LED2 = 3, OUT_BANK = 4, OUT_LED1 = 6 };

// ADC channels (MAME adc0809 inputs)
enum : uint8_t { ADC_PITCH = 0, ADC_YAW = 1 };

// RIOT port A; PA0, PA1 are outputs
enum : uint8_t {
  PA_TMS_WSQ = 0x01,
  PA_TMS_RSQ = 0x02,
  PA_TMS_READYQ = 0x04, // active low: ready
  PA_NO_SELFTEST = 0x10,
  PA_MAIN_PENDING = 0x40,
  PA_SOUND_PENDING = 0x80,
};

// POKEY: register offset bits 0-2, chip bits 3-4, control (AUDCTL..) bit 5
enum : uint8_t { POKEY_AUDCTL = 8, POKEY_SKCTL = 15 };

// 1.512 MHz CPUs
static const uint32_t CYCLES_PER_FRAME = 25200; // 60 Hz
static const uint32_t CYCLES_PER_IRQ = 6144;    // 3 kHz / 12 = 246 Hz

// TMS5220 at 12.096 MHz / 2 / 9 = 672 kHz, a sample per 80 clocks
static const uint32_t SPEECH_HZ = 8400, AUDIO_HZ = 24000;
static const uint16_t CYCLES_PER_SPEECH_SAMPLE = 180;

// Loops that spin until an IRQ handler changes a RAM flag (MAME speedup
// hack): only an IRQ ends them, so the CPU clock skips ahead.
//   main  0x6005 LDX;LDD;LDA;TFR A,DP;LSR <$3D;BCC  until 0x483d bit 0 set
//   main  0x6032 LDA <$3F;BMI                       until 0x483f bit 7 clear
//   sound 0x7d4c LDA <$00;CMPA #$3F;BNE;TFR DP,A;CMPA #$10;BNE;LEAX -1,X;BEQ;
//                LSR <$0B;BCC                       until 0x100b bit 0 set,
//                X counts down to a timeout
enum : uint16_t { MAIN_IDLE_LSR_PC = 0x6005, MAIN_IDLE_BMI_PC = 0x6032, SOUND_IDLE_PC = 0x7d4c };
enum : uint8_t {
  MAIN_IDLE_DP = 0x48,     // CPU RAM page
  MAIN_IDLE_LSR_FLAG = 0x3d,
  MAIN_IDLE_BMI_FLAG = 0x3f,
  SOUND_IDLE_DP = 0x10,    // RIOT RAM page
  SOUND_IDLE_CHECK = 0x00, // must hold SOUND_IDLE_MAGIC
  SOUND_IDLE_MAGIC = 0x3f,
  SOUND_IDLE_FLAG = 0x0b,
  SOUND_IDLE_CYCLES = 37,  // per iteration
};

// AVG screen (MAME visarea 0..250 x 0..280), beam center
static const int32_t XCENTER = 125 << 16, YCENTER = 140 << 16;
static const vec2d::Rect VISIBLE = {0, 0, 250, 280};
// 251 columns -> 240, rows centered in 288
static const int SCALE_MUL = 245, SCALE_SHIFT = 8;
static const int16_t TOP_BORDER = 4;

// NVRAM block 2 (nibbles 0x34-0x8f, ROM table 0xc7b6) starts with the
// yoke calibration (min, gain per axis) the game self-tunes from yoke
// extremes. Erased NVRAM loads the defaults, off center by ~48 px until
// both extremes are held. A zeroed block, like a played cabinet, centers
// within ~6 px. Checksum: 1 + nibble sum (end-around carry).
enum : uint8_t { NV_CAL_BLOCK = 0x34, NV_CAL_SIZE = 0x5c, NV_CAL_SUM_LO = 0x8f };

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  const uint16_t rgb = ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
  return (rgb >> 8) | (rgb << 8);
}

starwars::starwars()
  : starwars_rom(starwars_rom_blob),
    starwars_vrom(starwars_vrom_blob),
    starwars_srom(starwars_srom_blob),
    starwars_mproms(starwars_mproms_blob) {
  static_assert(SOUND_RAM_OFS + SOUND_RAM_SIZE <= RAMSIZE, "Star Wars RAM does not fit");
  rom = starwars_rom.data();
  vrom = starwars_vrom.data();
  srom = starwars_srom.data();
  // reset() clears it, and reset() runs before start()
  math_ram = Arena::alloc(StarWarsMath::RAM_SIZE);
  math.init(starwars_mproms.data(), math_ram);
  memset(&main_cpu, 0, sizeof(main_cpu));
  memset(&sound_cpu, 0, sizeof(sound_cpu));

  // color index (RGB << 4 | level 1..14); level 14 is MAME's brightest
  for (uint8_t i = 0; i < 128; i++) {
    const uint8_t rgb = i >> avg::SW_COLOR_SHIFT, level = i & 0xf;
    const uint16_t l = 60 + level * 14;
    const uint8_t v = level ? (l > 255 ? 255 : l) : 0;
    palette[i] = rgb565(rgb & 4 ? v : 0, rgb & 2 ? v : 0, rgb & 1 ? v : 0);
  }
}

void starwars::start() {
  if (lines) {
    return; // start() runs twice
  }

  work = Arena::alloc<vec2d::Line>(MAX_LINES);
  void *list = Arena::alloc(sizeof(Seqlock<GoList>), alignof(Seqlock<GoList>));
  go_list = new (list) Seqlock<GoList>();
  lines = Arena::alloc<vec2d::Line>(MAX_LINES);
  line_next = Arena::alloc<uint16_t>(MAX_LINES);
  line_active = Arena::alloc<uint16_t>(MAX_LINES);
  strip_head = Arena::alloc<uint16_t>(STRIPS);
  raster.init(line_next, line_active, strip_head, STRIPS);
  raster.begin(lines, 0);
}

void starwars::reset() {
  machineBase::reset();
  memset(math_ram, 0, StarWarsMath::RAM_SIZE);
  memset(nvram, 0xff, sizeof(nvram)); // X2212 default
  memset(nvram + NV_CAL_BLOCK, 0, NV_CAL_SIZE);
  nvram[NV_CAL_SUM_LO] = 1;
  math.reset();
  riot.reset();
  sound_latch = main_latch = 0;
  sound_pending = main_pending = false;
  outlatch = 0;
  bank = rom;
  adc_channel = 0;
  yoke_x = YokeAxis();
  yoke_y = YokeAxis();
  avg_state.reset(XCENTER, YCENTER);

  main_cycles = sound_cycles = 0;
  // emulation task not running, the audio core is this core
  speech.reset(SPEECH_HZ, AUDIO_HZ);
  tms.reset(&speech, CYCLES_PER_SPEECH_SAMPLE, sound_cycles);
  next_irq = CYCLES_PER_IRQ;
  main_irq = false;
  m6809_reset(&main_cpu);
  m6809_reset(&sound_cpu);
  install_rom_direct();
}

// Opcode fetch windows, m6809_reset() clears them. Main: with bank page
// 0 the asset is 0x6000-0xffff in one piece; page 1 runs most of the time
// it is selected, so its window is just the page. Sound code runs from
// the 0x4000 mirror.
void starwars::install_rom_direct() {
  const bool page1 = bank != rom;
  main_cpu.rom_direct = bank;
  main_cpu.rom_base = BANK;
  main_cpu.rom_size = page1 ? BANK_SIZE : 0x10000 - BANK;
  sound_cpu.rom_direct = srom;
  sound_cpu.rom_base = S_ROM;
  sound_cpu.rom_size = S_ROM_SIZE;
}

void starwars::reset_sound_cpu() {
  sound_pending = main_pending = false;
  m6809_reset(&sound_cpu);
  install_rom_direct();
  riot.edge_detect(riot_pa_in());
}

uint8_t starwars::in0() const {
  const unsigned int k = input->buttons_get();
  uint8_t v = IN0_IDLE;
  if (k & BUTTON_COIN) {
    v &= ~IN0_COIN1;
  }
  if (k & (BUTTON_FIRE | BUTTON_START)) {
    v &= ~IN0_BUTTON1;
  }
  return v;
}

uint8_t starwars::in1() const {
  return IN1_IDLE | IN1_VG_HALT; // MATH_RUN (bit 7) low: runs are instant
}

uint8_t starwars::riot_pa_in() {
  return PA_NO_SELFTEST | (tms.readyq_r(sound_cycles) ? PA_TMS_READYQ : 0) |
         (main_pending ? PA_MAIN_PENDING : 0) | (sound_pending ? PA_SOUND_PENDING : 0);
}

// RIOT pins -> TMS5220 (MAME pa_wr/pb_wr callbacks, /WS before /RS)
void starwars::speech_pins() {
  const uint8_t pa = riot.pa_output();
  tms.data_w(riot.pb_output());
  tms.wsq_w(pa & PA_TMS_WSQ, sound_cycles);
  tms.rsq_w(pa & PA_TMS_RSQ, sound_cycles);
}

void starwars::frame_input() {
  const unsigned int k = input->buttons_get();
  yoke_x.step(((k & BUTTON_RIGHT) ? 1 : 0) - ((k & BUTTON_LEFT) ? 1 : 0));
  // MAME IPT_AD_STICK_Y: up lowers the value
  int8_t dy = ((k & BUTTON_DOWN) ? 1 : 0) - ((k & BUTTON_UP) ? 1 : 0);
#ifdef STARWARS_INVERT_Y
  dy = -dy;
#endif
  yoke_y.step(dy);
}

void starwars::outlatch_write(uint8_t bit, bool v) {
  outlatch = (outlatch & ~(1 << bit)) | (v << bit);
  if (bit == OUT_BANK) {
    bank = rom + (v ? BANK1_OFS : 0);
    install_rom_direct();
  }
}

// The AVG draws the list right away; the CPU builds the next one meanwhile.
void starwars::go() {
  const avg::Memory m = {memory + MAIN_RAM_OFS, VRAM_SIZE, vrom, VROM, VROM_SIZE, 0, 0, avg::STARWARS};
  const uint16_t count = map_lines(avg::decode(m, avg_state, work, MAX_LINES));

  // packed after mapping: the video core spins while a publish runs
  go_list->publish([this, count](GoList &g) {
    g.count = count;
    for (uint16_t i = 0; i < count; i++) {
      const vec2d::Line &l = work[i];
      g.lines[i] = {(uint16_t)l.y0, (uint16_t)l.y1, (uint8_t)l.x0, (uint8_t)l.x1, l.color};
    }
  });
  game_started = 1;
}

uint8_t starwars::main_read(uint16_t a) {
  if (a >= ROM) {
    return rom[a - BANK];
  }
  if (a >= BANK) {
    return bank[a - BANK];
  }
  if (a < VROM) {
    return memory[MAIN_RAM_OFS + a];
  }
  if (a < VROM + VROM_SIZE) {
    return vrom[a - VROM];
  }
  if (a >= MATH_RAM) {
    return math_ram[a - MATH_RAM];
  }
  if (a >= CPU_RAM) {
    return memory[CPU_RAM_OFS + a - CPU_RAM];
  }
  if ((a & 0xff00) == NVRAM) {
    return nvram[a & 0xff] & 0x0f;
  }

  switch (a & 0xffe0) {
    case IN0: return in0();
    case IN1: return in1();
    case DSW0: return STARWARS_DSW0;
    case DSW1: return STARWARS_DSW1;
    case ADC: return adc_channel == ADC_PITCH ? yoke_y.pos : (adc_channel == ADC_YAW ? yoke_x.pos : 0);
  }

  switch (a) {
    case LATCH:
      run_sound(main_cycles);
      main_pending = false;
      return main_latch;
    case LATCH_FLAGS:
      run_sound(main_cycles);
      return (sound_pending ? SOUND_PENDING : 0) | (main_pending ? MAIN_PENDING : 0);
    case MATH: return math.quotient_hi();
    case MATH + 1: return math.quotient_lo();
    case MATH + 3: return math.random();
  }
  return 0;
}

void starwars::main_write(uint16_t a, uint8_t v) {
  if (a < VROM) {
    memory[MAIN_RAM_OFS + a] = v;
    return;
  }
  if (a >= BANK) {
    return;
  }
  if (a >= MATH_RAM) {
    math_ram[a - MATH_RAM] = v;
    return;
  }
  if (a >= CPU_RAM) {
    memory[CPU_RAM_OFS + a - CPU_RAM] = v;
    return;
  }
  if ((a & 0xff00) == NVRAM) {
    nvram[a & 0xff] = v & 0x0f;
    return;
  }
  if ((a & 0xfff8) == MATH) {
    math.write(a & 7, v);
    return;
  }
  if ((a & 0xfffc) == ADC_START) {
    adc_channel = a & 3;
    return;
  }

  switch (a & 0xffe0) {
    case AVG_GO:
      go();
      return;
    case IRQ_ACK:
      main_irq = false;
      main_cpu.irq_pending = 0;
      return;
    case OUTLATCH:
      outlatch_write(a & 7, v & 0x80);
      return;
  }

  switch (a) {
    case LATCH:
      run_sound(main_cycles);
      sound_latch = v;
      sound_pending = true;
      riot.edge_detect(riot_pa_in());
      return;
    case SOUND_RESET:
      run_sound(main_cycles);
      reset_sound_cpu();
      return;
  }
}

uint8_t starwars::sound_read(uint16_t a) {
  if (a >= S_ROM_HI) {
    return srom[a - S_ROM_HI];
  }
  if (a >= S_ROM && a < S_ROM + S_ROM_SIZE) {
    return srom[a - S_ROM];
  }
  if (a >= S_RAM && a < S_RAM + SOUND_RAM_SIZE) {
    return memory[SOUND_RAM_OFS + a - S_RAM];
  }
  if (a >= S_LATCH_R && a < S_RIOT_RAM) {
    sound_pending = false;
    riot.edge_detect(riot_pa_in());
    return sound_latch;
  }
  if (a >= S_RIOT_RAM && a < S_RIOT_IO) {
    return riot.ram_read(a);
  }
  if ((a & 0xffe0) == S_RIOT_IO) {
    return riot.io_read(a, riot_pa_in(), tms.status_r(sound_cycles), sound_cycles);
  }
  return 0;
}

void starwars::sound_write(uint16_t a, uint8_t v) {
  if (a >= S_RAM && a < S_RAM + SOUND_RAM_SIZE) {
    memory[SOUND_RAM_OFS + a - S_RAM] = v;
    return;
  }
  if (a < S_LATCH_R) {
    main_latch = v;
    main_pending = true;
    return;
  }
  if (a >= S_RIOT_RAM && a < S_RIOT_IO) {
    riot.ram_write(a, v);
    return;
  }
  if ((a & 0xffe0) == S_RIOT_IO) {
    riot.io_write(a, v, riot_pa_in(), sound_cycles);
    speech_pins();
    return;
  }
  if ((a & 0xffc0) != S_POKEY) {
    return;
  }

  // POKEY n registers at soundregs[n * Pokey::REG_COUNT], see renderAudio()
  uint8_t *regs = soundregs + ((a >> 3) & 3) * Pokey::REG_COUNT;
  const uint8_t reg = (a & 7) | ((a & 0x20) >> 2);
  if (reg <= POKEY_AUDCTL) {
    regs[reg] = v;
  } else if (reg == POKEY_SKCTL) {
    regs[Pokey::REG_SKCTL] = v;
  }
}

unsigned char IRAM_ATTR starwars::m6809_read(m6809_state *s, uint16_t addr) {
  return s == &main_cpu ? main_read(addr) : sound_read(addr);
}

void IRAM_ATTR starwars::m6809_write(m6809_state *s, uint16_t addr, uint8_t val) {
  if (s == &main_cpu) {
    main_write(addr, val);
  } else {
    sound_write(addr, val);
  }
}

unsigned char IRAM_ATTR starwars::m6809_read_opcode(m6809_state *s, uint16_t addr) {
  return m6809_read(s, addr);
}

// main CPU in an idle loop, waiting for the next IRQ; both loops are in
// bank page 0
bool starwars::main_idle() const {
  if (bank != rom) {
    return false;
  }

  const uint8_t *ram = memory + CPU_RAM_OFS;
  switch (main_cpu.PC) {
    case MAIN_IDLE_LSR_PC: return ram[MAIN_IDLE_LSR_FLAG] == 0; // loop sets DP
    case MAIN_IDLE_BMI_PC: return main_cpu.DP == MAIN_IDLE_DP && (ram[MAIN_IDLE_BMI_FLAG] & 0x80);
  }
  return false;
}

// Sound CPU at SOUND_IDLE_PC: skips whole idle loop iterations up to
// cycle until or the RIOT timer IRQ, keeping the timeout counter X above
// 0. Returns false if nothing was skipped.
bool starwars::sound_idle(uint32_t until) {
  if (sound_cpu.DP != SOUND_IDLE_DP ||
      riot.ram_read(SOUND_IDLE_CHECK) != SOUND_IDLE_MAGIC || riot.ram_read(SOUND_IDLE_FLAG)) {
    return false;
  }

  uint32_t span = until - sound_cycles;
  const uint32_t timer = riot.until_timer_irq(sound_cycles);
  span = timer < span ? timer : span;
  uint32_t n = span / SOUND_IDLE_CYCLES;
  if (n >= sound_cpu.X) {
    n = sound_cpu.X ? sound_cpu.X - 1 : 0;
  }
  if (!n) {
    return false;
  }

  sound_cpu.X -= n;
  sound_cycles += n * SOUND_IDLE_CYCLES;
  return true;
}

// Sound CPU catches up to cycle until. The IRQ line is a level, asserted
// while the RIOT raises it.
void starwars::run_sound(uint32_t until) {
  while ((int32_t)(sound_cycles - until) < 0) {
    if (riot.irq(sound_cycles)) {
      m6809_irq(&sound_cpu);
    } else {
      sound_cpu.irq_pending = 0;
      if (sound_cpu.PC == SOUND_IDLE_PC && sound_idle(until)) {
        continue;
      }
    }
    const int c = m6809_step(&sound_cpu, 1);
    sound_cycles += c > 0 ? c : 1;
  }
}

// Main CPU up to its next IRQ, then the sound CPU catches up; the main
// CPU syncs it before each latch access (MAME: perfect quantum after a
// latch write). The main IRQ holds until IRQ_ACK. In its idle loop the
// main CPU skips ahead to the next IRQ.
void starwars::run_frame() {
  frame_input();

  const uint32_t end = main_cycles + CYCLES_PER_FRAME;
  while ((int32_t)(main_cycles - end) < 0) {
    if ((int32_t)(main_cycles - next_irq) >= 0) {
      main_irq = true;
      next_irq += CYCLES_PER_IRQ;
    }

    const uint32_t until = (int32_t)(next_irq - end) < 0 ? next_irq : end;
    while ((int32_t)(main_cycles - until) < 0) {
      if (main_irq) {
        m6809_irq(&main_cpu);
      } else if (main_idle()) {
        main_cycles = until;
        break;
      }
      const int c = m6809_step(&main_cpu, 1);
      main_cycles += c > 0 ? c : 1;
    }

    run_sound(main_cycles);
  }
  tms.update(sound_cycles);
}

uint16_t starwars::map_lines(uint16_t count) {
  uint16_t kept = 0;
  for (uint16_t i = 0; i < count; i++) {
    vec2d::Line l = work[i];
    // most lines are on screen, skip the clip call for them
    const bool visible = (uint16_t)l.x0 <= VISIBLE.x1 && (uint16_t)l.x1 <= VISIBLE.x1 &&
                         (uint16_t)l.y0 <= VISIBLE.y1 && (uint16_t)l.y1 <= VISIBLE.y1;
    if (!visible && !vec2d::clip(l, VISIBLE)) {
      continue;
    }

    l.x0 = (l.x0 * SCALE_MUL) >> SCALE_SHIFT;
    l.x1 = (l.x1 * SCALE_MUL) >> SCALE_SHIFT;
    l.y0 += TOP_BORDER;
    l.y1 += TOP_BORDER;
    work[kept++] = l;
  }
  return kept;
}

// empty list before the first GO
void starwars::prepare_frame() {
  uint16_t count = 0;
  go_list->visit([this, &count](const GoList &g) {
    count = g.count < MAX_LINES ? g.count : MAX_LINES; // torn read
    for (uint16_t i = 0; i < count; i++) {
      const PackedLine &p = g.lines[i];
      vec2d::Line &l = lines[i];
      l.x0 = p.x0;
      l.y0 = p.y0;
      l.x1 = p.x1;
      l.y1 = p.y1;
      l.color = p.color;
    }
  });
  raster.begin(lines, count);
}

// audio core: TMS5220 output, 24 kHz
void starwars::renderSpeech(int16_t *out, int n) {
  speech.render(out, n);
}

// frame_buffer is cleared by the caller
void starwars::render_row(short row) {
  raster.render(row, frame_buffer, renderWidth(), palette);
}

#ifdef LED_PIN
// logo yellows; the cabinet button lamps light the middle LEDs
static const CRGB STARWARS_LED_DARK(0x7d670f), STARWARS_LED_MID(0xd8b31a),
    STARWARS_LED_BRIGHT(0xfedd20);
static const uint8_t LED_MIDDLE = NUM_LEDS / 2;

void starwars::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      STARWARS_LED_DARK, STARWARS_LED_MID, STARWARS_LED_BRIGHT, STARWARS_LED_BRIGHT,
      STARWARS_LED_BRIGHT, STARWARS_LED_MID, STARWARS_LED_DARK};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// The trigger lamp (LED 1, active low, blinks in attract) lights the
// middle LED white.
void starwars::gameLeds(CRGB *leds) {
  menuLeds(leds);
  if (!(outlatch & (1 << OUT_LED1))) {
    leds[LED_MIDDLE] = CRGB::White;
  }
}
#endif

// four POKEYs at half level plus the TMS5220 at 2.5 times a POKEY
// (MAME 0.50 vs 0.20)
void starwars::renderAudio(int16_t *out, int n) {
  int16_t voice[AUDIO_CHUNK];
  Pokey::renderBank(pokey, soundregs, out, n);
  renderSpeech(voice, n);
  for (int i = 0; i < n; i++) {
    out[i] = audioClamp((out[i] >> POKEY_SHIFT) + (voice[i] >> SPEECH_SHIFT), AUDIO_MAX);
  }
}
