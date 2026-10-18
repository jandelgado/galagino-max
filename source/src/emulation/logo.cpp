#include <cstring>
#include "logo.h"

// Inflate into the tail of dst, then expand front to back in place, so no
// extra buffer. Writes (2 bytes per pixel) never overtake reads (0.5 bytes
// per pixel, starting at 3/4): byte i is read before pixels 2i and 2i+1
// overwrite bytes 4i..4i+3. They meet only after the last byte.
//
//   dst  |<---------- RGB565 output, 43008 bytes ------------>|
//        |                           |pal|  packed pixels     |
//                                   32224 32256           43008
void Logo::decodeInto(uint16_t *dst) const {
    uint8_t *packed = (uint8_t *)dst + PIXELS * sizeof(uint16_t) - PACKED_BYTES;

    // count from the format, not the blob: a stream of any other size aborts
    Asset<uint8_t, COMPRESSED>(blob.packed, blob.packedLen, PACKED_BYTES).decodeInto(packed);

    // expansion overwrites the palette before it ends
    uint16_t palette[COLORS];
    memcpy(palette, packed, sizeof(palette));

    const uint8_t *src = packed + sizeof(palette);
    for (uint32_t i = 0; i < PIXELS / 2; i++) {
        const uint8_t pair = src[i];
        dst[2 * i] = palette[pair >> 4];
        dst[2 * i + 1] = palette[pair & 0x0f];
    }
}
