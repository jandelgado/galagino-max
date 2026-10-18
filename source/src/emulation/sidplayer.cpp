#include "sidplayer.h"
#include <string.h>

// PSID header, big endian fields
static const uint32_t PSID_MIN_HEADER = 0x76;
static const uint8_t OFS_VERSION = 0x04, OFS_DATA = 0x06, OFS_LOAD = 0x08;
static const uint8_t OFS_INIT = 0x0a, OFS_PLAY = 0x0c, OFS_SONGS = 0x0e;
static const uint8_t OFS_START = 0x10, OFS_SPEED = 0x12, OFS_FLAGS = 0x76;
static const uint16_t MAX_VERSION = 4;
static const uint16_t FLAGS_VERSION = 2;           // v1 headers end before the flags
static const uint8_t CLOCK_SHIFT = 2, CLOCK_MASK = 3, CLOCK_NTSC = 2; // 1 PAL, 3 both

static uint16_t be16(const uint8_t *p) { return (p[0] << 8) | p[1]; }
static uint32_t be32(const uint8_t *p) { return ((uint32_t)be16(p) << 16) | be16(p + 2); }

bool psidParse(const uint8_t *file, uint32_t len, PsidInfo *info) {
  if (len < PSID_MIN_HEADER || memcmp(file, "PSID", 4) != 0) {
    return false;
  }
  const uint16_t version = be16(file + OFS_VERSION);
  const uint16_t data = be16(file + OFS_DATA);
  if (version < 1 || version > MAX_VERSION || data < PSID_MIN_HEADER || len < data + 2u) {
    return false;
  }

  info->load = be16(file + OFS_LOAD);
  info->init = be16(file + OFS_INIT);
  info->play = be16(file + OFS_PLAY);
  info->songs = be16(file + OFS_SONGS);
  info->start_song = be16(file + OFS_START);
  info->clock = SID_PAL;
  if (version >= FLAGS_VERSION && ((be16(file + OFS_FLAGS) >> CLOCK_SHIFT) & CLOCK_MASK) == CLOCK_NTSC) {
    info->clock = SID_NTSC;
  }
  info->payload = file + data;
  info->payload_len = len - data;

  // load address 0: first two data bytes, little endian (C64 PRG style)
  if (info->load == 0) {
    info->load = info->payload[0] | (info->payload[1] << 8);
    info->payload += 2;
    info->payload_len -= 2;
  }

  // play 0 = player installs its own IRQ; speed bit set = CIA timed
  if (info->play == 0 || info->songs == 0 || be32(file + OFS_SPEED) != 0) {
    return false;
  }

  // PSID defaults: init 0 = load address, start song 0 or out of range = 1
  if (info->init == 0) {
    info->init = info->load;
  }
  if (info->start_song == 0 || info->start_song > info->songs) {
    info->start_song = 1;
  }
  return true;
}

bool SidPlayer::load(const PsidInfo &tune, uint8_t *mem, uint16_t mem_size) {
  if (tune.payload_len > mem_size) {
    return false;
  }
  info = tune;
  ram = mem;
  ram_size = mem_size;
  samples_per_play = Sid::SAMPLE_RATE / (tune.clock == SID_NTSC ? NTSC_PLAY_HZ : PAL_PLAY_HZ);
  playing = false;
  return true;
}

void SidPlayer::start(uint8_t song) {
  // tunes modify their own data: every start begins from the file
  memcpy(ram, info.payload, info.payload_len);
  memset(ram + info.payload_len, 0, ram_size - info.payload_len);
  memset(low, 0, sizeof(low));
  sid.reset(info.clock);

  memset(&cpu, 0, sizeof(cpu));
  cpu.read = read;
  cpu.write = write;
  cpu.user = this;

  stray_count = 0;
  samples_to_play = 0; // first play with the first sample
  playing = call(info.init, song - 1, INIT_MAX_CYCLES);
}

void SidPlayer::jsr(uint16_t addr, uint8_t a) {
  if (!playing) {
    return;
  }
  playing = call(addr, a, PLAY_MAX_CYCLES);
}

void SidPlayer::render(int16_t *out, int n) {
  while (n > 0) {
    if (!playing) {
      sid.render(out, n); // keep the last register state ringing out
      return;
    }
    if (samples_to_play == 0) {
      playing = call(info.play, 0, PLAY_MAX_CYCLES);
      samples_to_play = samples_per_play;
    }

    const int chunk = n < (int)samples_to_play ? n : (int)samples_to_play;
    sid.render(out, chunk);
    out += chunk;
    n -= chunk;
    samples_to_play -= chunk;
  }
}

// JSR from nowhere: the routine's final RTS lands on RETURN_PC. A runaway
// routine is cut off after max_cycles so the audio core never hangs.
bool SidPlayer::call(uint16_t addr, uint8_t a, uint32_t max_cycles) {
  const uint16_t ret = RETURN_PC - 1; // RTS adds 1
  cpu.sp = 0xff;
  low[STACK + cpu.sp--] = ret >> 8;
  low[STACK + cpu.sp--] = ret & 0xff;
  cpu.pc = addr;
  cpu.a = a;
  cpu.x = cpu.y = 0;

  uint32_t cycles = 0;
  while (cpu.pc != RETURN_PC) {
    if (cycles > max_cycles) {
      return false;
    }
    cycles += m6502_step(&cpu);
  }
  return true;
}

uint8_t SidPlayer::read(m6502_t *cpu, uint16_t addr) {
  SidPlayer *p = static_cast<SidPlayer *>(cpu->user);
  if (addr < LOW_SIZE) {
    return p->low[addr];
  }
  const uint16_t ofs = addr - p->info.load; // wraps below load
  if (ofs < p->ram_size) {
    return p->ram[ofs];
  }

  // SID reads (OSC3/ENV3) are not emulated: count them too
  p->stray_count++;
  return 0;
}

void SidPlayer::write(m6502_t *cpu, uint16_t addr, uint8_t val) {
  SidPlayer *p = static_cast<SidPlayer *>(cpu->user);
  if (addr < LOW_SIZE) {
    p->low[addr] = val;
    return;
  }
  const uint16_t ofs = addr - p->info.load;
  if (ofs < p->ram_size) {
    p->ram[ofs] = val;
    return;
  }
  if (addr >= SID_BASE && addr < SID_END) {
    p->sid.write(addr - SID_BASE, val);
    return;
  }
  p->stray_count++;
}
