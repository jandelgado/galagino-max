#ifndef HAL_VIDEO_WIDTH_H
#define HAL_VIDEO_WIDTH_H

// Widest strip a machine may render (renderWidth()), a property of the video
// backend: hal::Video::MAX_WIDTH. Machines read it here, without the backend
// headers (SDK); main.cpp asserts both agree.

#include <stdint.h>
#include "../config.h"

namespace hal {
#if defined(HAL_VIDEO_DVI_HSTX)
constexpr uint16_t VIDEO_MAX_WIDTH = 256;
#else
constexpr uint16_t VIDEO_MAX_WIDTH = 240;
#endif

// Output width of a game `native` columns wide: all of them where they fit,
// else the game's own crop or scale to `fit` columns.
constexpr uint16_t fitWidth(uint16_t native, uint16_t fit, uint16_t max = VIDEO_MAX_WIDTH) {
  return max >= native ? native : fit;
}
}

#endif // HAL_VIDEO_WIDTH_H
