#include "logo.h"

void Logo::decodeInto(uint8_t *slot) const {
    // count from the format, not the blob: a stream of any other size aborts
    Asset<uint8_t, COMPRESSED>(blob.packed, blob.packedLen, PACKED_BYTES).decodeInto(slot);
}
