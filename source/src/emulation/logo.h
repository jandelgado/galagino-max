#ifndef LOGO_H
#define LOGO_H

#include <cstdint>
#include "asset.h"

// Menu logo, 224x96 pixels with a 16 color palette, zlib packed in flash.
// Packed layout (romconv/internal/logoconv.py):
//   16 x uint16_t  palette, byte swapped RGB565 as the display expects
//   10752 bytes    pixels, 2 per byte, high nibble first
// Lives in flash, decodes on demand into a menu cache slot.
class Logo {
public:
    static constexpr uint32_t WIDTH = 224;
    static constexpr uint32_t HEIGHT = 96;
    static constexpr uint32_t PIXELS = WIDTH * HEIGHT;
    static constexpr uint32_t COLORS = 16;
    static constexpr uint32_t PACKED_BYTES = COLORS * sizeof(uint16_t) + PIXELS / 2;

    constexpr Logo(const FlashAsset<uint8_t, COMPRESSED> &blob) : blob(blob) { }

    Logo(const Logo &) = delete;
    Logo &operator=(const Logo &) = delete;

    // dst: PIXELS + 1 elements, the extra one takes uzlib's overrun byte.
    void decodeInto(uint16_t *dst) const;

private:
    const FlashAsset<uint8_t, COMPRESSED> blob;
};

#endif
