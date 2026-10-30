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
}

#endif // HAL_VIDEO_WIDTH_H
