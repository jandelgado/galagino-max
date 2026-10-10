#include "dvgMachine.h"
#include "../emulation/arena.h"
#include <new>

// Experimental: dim halo around each line, like a vector monitor's beam
// bloom. Costs 480 bytes of Arena and some render time.
//#define DVG_GLOW

static const uint32_t CLOCK_3K_BIT = 0x100; // MAME clock_r: total cycles bit 8

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  const uint16_t rgb = ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
  return (rgb >> 8) | (rgb << 8);
}

static uint16_t grey565(uint8_t level) {
  return rgb565(level, level, level);
}

// half the line's level, tinted blue like a vector monitor's phosphor bloom
static uint16_t glow565(uint8_t level) {
  const uint8_t l = level / 2;
  return rgb565(l * 3 / 4, l * 7 / 8, l * 3 / 2 > 255 ? 255 : l * 3 / 2);
}

dvgMachine::dvgMachine(uint8_t (*read)(m6502_t *, uint16_t),
                       void (*write)(m6502_t *, uint16_t, uint8_t)) {
  static_assert(VRAM_OFS + MAX_VRAM_SIZE <= RAMSIZE, "vector machine RAM does not fit");
  memset(&m_cpu, 0, sizeof(m_cpu));
  m_cpu.read = read;
  m_cpu.write = write;
  m_cpu.user = this;

  // DVG intensity 0-15 as grey; Asteroids uses mostly 7 (objects) and 15
  // (bullets, text), both must stand out on the small LCD
  palette[0] = glow[0] = 0;
  for (uint8_t i = 1; i < 16; i++) {
    palette[i] = grey565(80 + i * 11);
    glow[i] = glow565(80 + i * 11);
  }
}

void dvgMachine::start() {
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
#ifdef DVG_GLOW
  glow_carry = Arena::alloc<uint16_t>(renderWidth());
#endif
  raster.init(line_next, line_active, strip_head, STRIPS, glow_carry);
  raster.begin(lines, 0);
}

void dvgMachine::reset() {
  machineBase::reset();
  cycles = 0;
  next_nmi = CYCLES_PER_NMI;
  m6502_reset(&m_cpu);
}

void dvgMachine::run_frame() {
  frame_input();

  // NMI every 6144 cycles; the self test switch that masks it is never on
  const uint32_t end = cycles + CYCLES_PER_FRAME;
  while ((int32_t)(cycles - end) < 0) {
    if ((int32_t)(cycles - next_nmi) >= 0) {
      m_cpu.nmi = 1;
      next_nmi += CYCLES_PER_NMI;
    }
    cycles += m6502_step(&m_cpu);
  }
}

uint16_t dvgMachine::decode_frame(const uint8_t *vram) {
  dvg::Memory m = dvg_mem;
  m.ram = vram;
  return dvg::decode(m, work, MAX_LINES);
}

// empty list before the first GO
void dvgMachine::prepare_frame() {
  uint16_t count = 0;
  go_list->visit([this, &count](const GoList &g) {
    count = g.count < MAX_LINES ? g.count : uint16_t(MAX_LINES); // torn read
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

// frame_buffer is cleared by the caller
void dvgMachine::render_row(short row) {
#ifdef DVG_GLOW
  raster.render(row, frame_buffer, renderWidth(), strip_palette(row), glow);
#else
  raster.render(row, frame_buffer, renderWidth(), strip_palette(row));
#endif
}

// The DVG draws the list right away; the CPU builds the next one meanwhile.
// Several GOs per frame: the last complete list wins.
void dvgMachine::go() {
  const uint16_t count = map_lines(decode_frame(memory + VRAM_OFS));

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

bool dvgMachine::clock_3k() const {
  return cycles & CLOCK_3K_BIT;
}

// IN0/IN1 style ports: one bit per address in data bit 7
uint8_t dvgMachine::input_bit(bool set) {
  return set ? 0x80 : 0x7f;
}

// LS153: address n selects DIP bits 7-2n (-> D1) and 6-2n (-> D0)
uint8_t dvgMachine::dsw_read(uint8_t dsw, uint16_t a) {
  const uint8_t shift = 6 - 2 * (a & 3);
  return 0xfc | ((dsw >> shift) & 3);
}
