#ifndef HAL_RP2350_AUDIO_DVI_H
#define HAL_RP2350_AUDIO_DVI_H

// HAL_AUDIO_DVI: AudioOut (audio_dvi.cpp) fills this ring at 24 kHz, the
// DVI scanout IRQ (video_dvi_hstx.cpp) sends it as HDMI audio packets.

#include "sample_ring.h"

namespace hal {
namespace hdmi {

extern SampleRing audioRing;

} // namespace hdmi
} // namespace hal

#endif // HAL_RP2350_AUDIO_DVI_H
