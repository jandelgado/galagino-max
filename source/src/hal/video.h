#ifndef HAL_VIDEO_H
#define HAL_VIDEO_H

// hal::Video: the video backend of this build, a sink for rendered strips
// (240 x 8 RGB565 pixels). Not part of hal.h: only the frame loop and the
// loader screen include it. Backend by define, HAL_VIDEO_SPI_TFT,
// HAL_VIDEO_DVI_HSTX or HAL_VIDEO_NULL, chosen in config.h (board profile or
// platform default).

#include "../config.h"

#if defined(HAL_VIDEO_SPI_TFT)
#include "esp/video_spi_tft.h"
namespace hal { typedef SpiTftVideo Video; }
#elif defined(HAL_VIDEO_DVI_HSTX)
#include "rp2350/video_dvi_hstx.h"
namespace hal { typedef DviHstxVideo Video; }
#elif defined(HAL_VIDEO_NULL)
#include "host/video_null.h"
namespace hal { typedef NullVideo Video; }
#endif

#endif // HAL_VIDEO_H
