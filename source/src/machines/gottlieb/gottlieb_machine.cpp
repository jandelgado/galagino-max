#include "../../catalog.h"
#if defined(GAME_QBERT) || defined(GAME_KRULL) || defined(GAME_TYLZ)

#include "gottlieb_machine.h"

// I/O 5800-5fff (mirror 0x07f8), gottlieb_base_map
static constexpr uint16_t IO_BASE = 0x5800;
static constexpr uint8_t IO_DSW = 0, IO_IN1 = 1, IO_IN2 = 2, IO_IN3 = 3, IO_IN4 = 4;
static constexpr uint8_t IO_SOUND = 2, IO_OUTPUT = 3;
static constexpr uint16_t VRAM_BASE = 0x3800, CHAR_BASE = 0x4000, PALETTE_BASE = 0x5000;

// one video frame (318 x 256 pixels at 5 MHz) in sound CPU cycles:
// 894886 * 81408 / 5000000 = 14570.0
static constexpr uint32_t PIXEL_HZ = 5000000, FRAME_PIXELS = 318 * 256;

static constexpr uint16_t NVRAM_SIZE = 0x1000;   // battery RAM 0000-0fff
static constexpr uint32_t CHAR_TILE_BYTES = 128 * 8 * sizeof(uint32_t);

void gottliebMachine::reset() {
  machineBase::reset();   // clears memory: video RAM, char RAM
  ram = memory;
  memset(ram, 0xff, NVRAM_SIZE);   // MAME NVRAM default all 1
  if (charTiles) {
    memset(charTiles, 0, CHAR_TILE_BYTES);
  }
  const i8086::Bus bus = {this, busRead, busWrite, ram, board.ramEnd, rom, board.codeBase, board.romBase, 0xffff};
  i8086::setBus(bus);
  i8086::reset();
  booted = false;
  outputLatch = 0;
  sound.reset();
  cmdCount = 0;
  soundPhase = 0;
  dc = DcBlock();
  lastSample = 0;
}

// ---- 8088 I/O ----

uint8_t gottliebMachine::rdio(uint16_t a) {
  if (a >= IO_BASE) {
    switch (a & 7) {
      case IO_DSW:
        return readDsw();
      case IO_IN1:
        return readIn1(input->buttons_get());
      case IO_IN2:   // trackball, not fitted
      case IO_IN3:
        return 0xff;
      case IO_IN4:
        return readIn4(input->buttons_get());
    }
    return 0;
  }
  if (charTiles && a >= CHAR_BASE && a < PALETTE_BASE) {
    return ram[board.ramEnd + CHARRAM_OFS + (a & 0xfff)];
  }
  if (a >= VRAM_BASE && a < CHAR_BASE) {
    return ram[board.ramEnd + VRAM_OFS + (a & 0x3ff)];
  }
  return 0;   // sprite RAM and palette are write only
}

void gottliebMachine::wrio(uint16_t a, uint8_t v) {
  if (a < IO_START || a >= IO_END) {
    return;   // ROM and empty sockets
  }
  if (a >= IO_BASE) {
    switch (a & 7) {
      case IO_SOUND:
        if (cmdCount < CMD_QUEUE) {
          cmdQueue[cmdCount++] = v;
        }
        break;
      case IO_OUTPUT:
        outputLatch = v;
        break;
    }
    return;   // watchdog, trackball reset: nothing to do
  }

  uint8_t *video = ram + board.ramEnd;
  if (a >= PALETTE_BASE) {
    video[PALETTE_OFS + (a & 0x1f)] = v;
  } else if (a >= CHAR_BASE) {
    if (!charTiles) {
      return;   // ROM tiles: char RAM not fitted
    }
    const uint16_t offset = a & 0xfff;
    video[CHARRAM_OFS + offset] = v;
    gottlieb_video::charWrite(charTiles, offset, v);
  } else if (a >= VRAM_BASE) {
    video[VRAM_OFS + (a & 0x3ff)] = v;
  } else {
    video[SPRITES_OFS + (a & 0xff)] = v;
  }
}

// ---- Frame ----

void gottliebMachine::enterNmi(void) {
  i8086::nmi();
}

bool gottliebMachine::frameWait(uint16_t ip) const {
  if (ip != board.waitIp && ip != board.waitIp2) {
    return false;
  }
  if (board.waitCounter) {
    return i8086::AX() == (ram[board.waitFlag] | ram[board.waitFlag + 1] << 8);
  }
  return ram[board.waitFlag] == 0;
}

// steps the 8088 until it waits for the next frame
gottliebMachine::CpuExit gottliebMachine::cpuRun(int32_t budget) {
  for (budgetLeft = budget;; budgetLeft--) {
    const uint16_t ip = i8086::IP();
    if (frameWait(ip)) {
      return CPU_WAIT;
    }
    if (ip == board.slowIp) {
      budgetLeft -= board.slowCost;
    }
    if (budgetLeft <= 0) {
      return CPU_BUDGET;
    }
    i8086::step();
    if (i8086::fault()) {
      return CPU_TRAP;
    }
  }
}

void gottliebMachine::runCpu(void) {
  const CpuExit e = cpuRun(BUDGET);
  if (e == CPU_BUDGET) {
    budgetCuts++;
  } else if (e == CPU_TRAP) {
    trapCount++;
    printf("gottlieb: 8088 trap at %04x:%04x, reset\n", i8086::CS(), i8086::IP());
    reset();
  }
}

void gottliebMachine::run_frame(void) {
  pollInputs();
  if (booted) {
    publishVideo();
    enterNmi();
  }
  // from the first frame on: attract with demo sounds off sends no sound
  // command, the usual "first command" start signal
  booted = true;
  game_started = 1;
  runCpu();
  runSound();
}

// at the vblank NMI, before its handler copies the next sprite list and
// palette in: what the board shows for the frame that just ended
void gottliebMachine::latchVideo(GottliebVideoInput &v) const {
  const uint8_t *video = ram + board.ramEnd;
  memcpy(v.vram, video + VRAM_OFS, sizeof(v.vram));
  memcpy(v.spr, video + SPRITES_OFS, sizeof(v.spr));
  memcpy(v.pal, video + PALETTE_OFS, sizeof(v.pal));
  v.ctrl = outputLatch;
}

// ---- Sound ----

void gottliebMachine::runSound(void) {
  const uint64_t ticks = (uint64_t)GottliebSound::SOUND_HZ * FRAME_PIXELS + soundPhase;
  soundPhase = (uint32_t)(ticks % PIXEL_HZ);
  soundCmdSeen = cmdCount != 0;
  sound.runFrame(cmdQueue, cmdCount, (uint32_t)(ticks / PIXEL_HZ));
  cmdCount = 0;
}

// audio core: drain the board's 24 kHz ring, hold the last value on underrun
void gottliebMachine::renderAudio(int16_t *out, int n) {
  for (int i = 0; i < n; i++) {
    const int s = sound.sample();
    if (s != GottliebSound::EMPTY) {
      lastSample = (int16_t)s;
    }
    out[i] = audioClamp(dc.filter(lastSample), AUDIO_MAX);
  }
}

#endif
