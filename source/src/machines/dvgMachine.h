#ifndef DVGMACHINE_H
#define DVGMACHINE_H

#include "machineBase.h"
#include "../cpus/m6502/m6502.h"
#include "../emulation/seqlock.h"
#include "../emulation/vector2d.h"
#include "../emulation/dvg.h"

// Shared base of the Atari vector machines on the Asteroids CPU board
// (Asteroids, Lunar Lander: DVG; Battle Zone: AVG), Tempest and Space Duel:
// one M6502 at 12.096 MHz / 8 with a 3 kHz / 12 NMI (Tempest, Space Duel:
// IRQ, held until acknowledged), display list decoded at GO and rasterized
// strip by strip.
//
//   emulation core                   video core
//   CPU writes VRAM, GO: go()        prepare_frame(): go_list ->
//     decode_frame(), map_lines()      lines, raster.begin()
//     publishes go_list -----------> render_row() x 36
//
// Decoding at GO like the real DVG keeps the vector count off the video
// core, which has no time left beside SPI (240 columns) and audio.
class dvgMachine : public machineBase {
public:
  void start() override;
  void reset() override;
  void run_frame() override;
  void prepare_frame() override;
  void render_row(short row) override;
  const int renderWidth() override { return 240; }
  const int renderBuffer() override { return 240 * 2 * 8; }

  // keep in sync with LINE_BUFFER_BYTES in the romconv scripts
  static const uint16_t MAX_LINES = 1024;

protected:
  static const uint16_t ADDR_MASK = 0x7fff; // 15 bit address bus
  static const uint16_t VRAM = 0x4000, VRAM_SIZE = 0x0800;
  static const uint16_t VRAM_OFS = 0x0400; // VRAM inside machineBase::memory
  static const uint16_t MAX_VRAM_SIZE = 0x1000; // largest VRAM (Battle Zone)

  // 3 kHz / 12 interrupt line
  enum Interrupt : uint8_t { NMI, IRQ };

  dvgMachine(uint8_t (*read)(m6502_t *, uint16_t),
             void (*write)(m6502_t *, uint16_t, uint8_t),
             Interrupt interrupt = NMI);

  // emulation core, at GO: decodes vram into work, returns the count
  virtual uint16_t decode_frame(const uint8_t *vram);
  // clips work[0, count) and maps it to panel coordinates in place,
  // returns the visible count
  virtual uint16_t map_lines(uint16_t count) = 0;
  // once per run_frame(), before the CPU runs
  virtual void frame_input() {}
  // intensity -> RGB565 for one strip (overlays tint parts of the screen)
  virtual const uint16_t *strip_palette(uint8_t strip) const { return palette; }

  void go(); // DVG GO write: decode VRAM, publish for the video core
  void irq_ack() { m_cpu.irq = 0; }
  bool clock_3k() const;
  static uint8_t input_bit(bool set);
  static uint8_t dsw_read(uint8_t dsw, uint16_t a);
  uint32_t cpu_cycles() const { return cycles; } // CPU clock since reset

  // main CPU and its clock since reset; a subclass with its own
  // run_frame() (Major Havoc: two CPUs) steps them itself
  m6502_t m_cpu;
  uint32_t cycles = 0;

  dvg::Memory dvg_mem = {}; // ROM window and glyphs, set by the subclass ctor
  vec2d::Line *work = nullptr; // emulation core decode buffer, Arena
  uint16_t palette[16]; // intensity -> RGB565, byte swapped

private:
  static const uint32_t CYCLES_PER_FRAME = 25200; // 1.512 MHz / 60 Hz
  static const uint32_t CYCLES_PER_INT = 6144;    // 3 kHz / 12 = 246 Hz
  static const uint8_t STRIPS = 36;

  const Interrupt interrupt;
  uint32_t next_int = 0; // cycles value of the next interrupt

  // mapped line, half a vec2d::Line
  struct PackedLine {
    uint16_t y0, y1;
    uint8_t x0, x1, color;
  };
  static_assert(sizeof(PackedLine) == 8, "PackedLine size is part of the Arena budget");

  // display list of the last GO, see seqlock.h
  struct GoList {
    uint16_t count;
    PackedLine lines[MAX_LINES];
  };
  Seqlock<GoList> *go_list = nullptr; // Arena

  vec2d::Line *lines = nullptr; // video core line list, Arena
  uint16_t *line_next = nullptr, *line_active = nullptr, *strip_head = nullptr;
  vec2d::StripRasterizer raster;
  uint16_t *glow_carry = nullptr; // one per column, Arena, see DVG_GLOW
  uint16_t glow[16];              // intensity -> glow RGB565, byte swapped
};

#endif
