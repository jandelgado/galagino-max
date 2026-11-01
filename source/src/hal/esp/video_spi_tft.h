#ifndef HAL_ESP_VIDEO_SPI_TFT_H
#define HAL_ESP_VIDEO_SPI_TFT_H

#include <stdint.h>
#include <driver/spi_master.h>
#include "../../config.h"
#include "../frame_pacer.h"

namespace hal {

// ILI9341 / ST7789 panel on SPI. Owns the panel protocol (init, MADCTL,
// address window), the DMA strip queue and the frame pacing: full rate is the
// panel's 60 Hz deadline, half rate (SPI below 80 MHz) two screen parts of
// 33 ms.
class SpiTftVideo {
public:
  // game pixels available on the 240 x 320 panel
  enum : uint16_t { CANVAS_W = 240, CANVAS_H = 320 };
  // widest strip (video_width.h): the panel width
  enum : uint16_t { MAX_WIDTH = 240 };

  SpiTftVideo();

  void begin(void);
  void setViewport(uint16_t width);
  void flip(char flipY, char flipX);
  void flipReset(char flipY, char flipX);

  // The machine's preference; VIDEO_HALF_RATE builds are always half rate.
  void setHalfRate(bool half);
  // Parts a screen is sent in per frame: 1 (full rate) or 2 (half rate).
  uint8_t screenParts() const;

  // One strip of `pixels` RGB565 pixels, in order. Blocks while all DMA
  // buffers are in flight.
  void submit(const uint16_t *strip, uint32_t pixels);
  // After the last strip of a screen part: sleeps until the next part may
  // start. frameStartUs is hal::micros() at the start of the frame.
  void endFrame(uint8_t part, uint32_t frameStartUs);
  // Strip to render as the i-th of a screen: the panel flips by MADCTL, so
  // render order is screen order.
  uint8_t stripIndex(uint8_t i) const { return i; }

private:
  void setAddrWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
  void sendCommand(uint8_t commandByte, uint8_t *dataBytes, uint8_t numDataBytes);
  void writeCommand(uint8_t cmd);
  void write8(uint8_t u8);
  void write16(uint16_t u16);
  void dmaWait();

  // two strips in flight: rendering the next strip, or a burst of audio,
  // overlaps the SPI sending the previous two
  static const uint8_t DMA_BUFFERS = 2;
  uint8_t dma_pending = 0; // queued, result not fetched yet
  uint8_t dma_next = 0;    // buffer of the next write, the oldest in flight
  spi_device_handle_t handle;
  spi_transaction_t* r_trans;
  spi_transaction_t transaction[DMA_BUFFERS];
  unsigned char *dma_buffer[DMA_BUFFERS];
  uint8_t madctl_last;
  uint16_t viewport_width = 224;

  bool half_rate = false;
  FramePacer pacer;            // full rate: 60 Hz deadlines
  uint32_t last_yield_ms = 0;  // full rate: last time the idle task got CPU
};

} // namespace hal

#endif // HAL_ESP_VIDEO_SPI_TFT_H
