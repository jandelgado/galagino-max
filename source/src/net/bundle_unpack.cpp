#include <cstring>
#include "bundle.h"
#include "bundle_unpack.h"

static constexpr uint32_t FLAG_RAW = Bundle::FLAG_RAW;
static constexpr uint32_t FLAG_PACKED = Bundle::FLAG_PACKED;
static constexpr uint32_t FLAG_LZMA = Bundle::FLAG_LZMA;

uint32_t BundleUnpacker::read32(const uint8_t *p) {
  return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

void BundleUnpacker::put32(uint8_t *p, uint32_t v) {
  for (uint32_t i = 0; i < 4; i++) {
    p[i] = (uint8_t)(v >> (8 * i));
  }
}

uint32_t BundleUnpacker::align4(uint32_t n) {
  return (n + ALIGN - 1) & ~(ALIGN - 1);
}

BundleUnpacker::BundleUnpacker(Sink sink, void *ctx, uint8_t *dict, ISzAllocPtr alloc)
    : sink(sink), ctx(ctx), dict(dict), alloc(alloc) {
  LzmaDec_Construct(&lzma);
}

BundleUnpacker::Entry BundleUnpacker::entry(uint16_t i) const {
  const uint8_t *p = table + HEADER_BYTES + i * ENTRY_BYTES;
  return {read32(p), read32(p + 4), read32(p + 8), read32(p + 12), read32(p + 16)};
}

// bytes the entry takes in the cache: LZMA decoded, PACKED as is
uint32_t BundleUnpacker::cacheBytes(const Entry &e) const {
  return e.flags == FLAG_LZMA ? e.raw : e.stored;
}

uint32_t BundleUnpacker::cacheOffset(uint16_t i) const {
  uint32_t at = HEADER_BYTES + count * ENTRY_BYTES;
  for (uint16_t k = 0; k < i; k++) {
    at = align4(at) + cacheBytes(entry(k));
  }
  return align4(at);
}

bool BundleUnpacker::fail() {
  state = FAILED;
  return false;
}

bool BundleUnpacker::feed(const uint8_t *data, uint32_t length) {
  while (length) {
    bool ok = false;
    switch (state) {
      case HEADER: ok = header(data, length); break;
      case PAD:    ok = pad(data, length); break;
      case BODY:   ok = body(data, length); break;
      case DONE:   // bytes past the last entry
      case FAILED: ok = false; break;
    }
    if (!ok) {
      return fail();
    }
  }
  return state != FAILED;
}

// buffers header and table, then writes the cache entry's table
bool BundleUnpacker::header(const uint8_t *&data, uint32_t &length) {
  uint32_t need = HEADER_BYTES;
  if (tableLen >= HEADER_BYTES) {
    count = table[6] | (table[7] << 8);
    if (count == 0 || count > MAX_ENTRIES) {
      return false;
    }
    need = HEADER_BYTES + count * ENTRY_BYTES;
  }

  const uint32_t n = need - tableLen < length ? need - tableLen : length;
  memcpy(table + tableLen, data, n);
  tableLen += n;
  wirePos += n;
  data += n;
  length -= n;
  if (tableLen < need || need == HEADER_BYTES) {
    return true;
  }

  if (!validHeader()) {
    return false;
  }
  const Entry last = entry(count - 1);
  entryBytes = cacheOffset(count - 1) + cacheBytes(last);
  if (!writeTable()) {
    return false;
  }
  state = PAD;
  return true;
}

// in order, aligned, not overlapping; flags the wire may carry
bool BundleUnpacker::validHeader() const {
  if (read32(table) != Bundle::MAGIC || (table[4] | (table[5] << 8)) != Bundle::VERSION) {
    return false;
  }

  uint32_t end = HEADER_BYTES + count * ENTRY_BYTES;
  for (uint16_t i = 0; i < count; i++) {
    const Entry e = entry(i);
    if (e.flags != FLAG_LZMA && e.flags != FLAG_PACKED) {
      return false;
    }
    if (e.offset % ALIGN || e.offset < end) {
      return false;
    }
    if (e.flags == FLAG_LZMA && e.stored == 0) {
      return false;  // props byte at least
    }
    end = e.offset + e.stored;
  }
  return true;
}

bool BundleUnpacker::writeTable() {
  uint8_t head[HEADER_BYTES];
  memcpy(head, table, HEADER_BYTES);
  if (!out(head, HEADER_BYTES)) {
    return false;
  }

  for (uint16_t i = 0; i < count; i++) {
    const Entry e = entry(i);
    uint8_t row[ENTRY_BYTES];
    put32(row, e.id);
    put32(row + 4, cacheOffset(i));
    put32(row + 8, cacheBytes(e));
    put32(row + 12, e.raw);
    put32(row + 16, e.flags == FLAG_LZMA ? FLAG_RAW : FLAG_PACKED);
    if (!out(row, ENTRY_BYTES)) {
      return false;
    }
  }
  return true;
}

// skips wire padding up to the entry, writes cache padding up to its slot
bool BundleUnpacker::pad(const uint8_t *&data, uint32_t &length) {
  const Entry e = entry(current);
  const uint32_t skip = e.offset - wirePos < length ? e.offset - wirePos : length;
  wirePos += skip;
  data += skip;
  length -= skip;
  if (wirePos < e.offset) {
    return true;
  }

  static const uint8_t zeros[ALIGN] = {};
  if (!out(zeros, cacheOffset(current) - outPos)) {
    return false;
  }
  storedLeft = e.stored;
  rawLeft = e.raw;
  lzmaStarted = false;
  lzmaDone = false;
  state = BODY;
  // an empty PACKED entry is already complete
  return storedLeft ? true : nextEntry();
}

bool BundleUnpacker::body(const uint8_t *&data, uint32_t &length) {
  const Entry e = entry(current);
  uint32_t n = storedLeft < length ? storedLeft : length;
  uint32_t used = n;

  if (e.flags == FLAG_PACKED) {
    if (!out(data, n)) {
      return false;
    }
  }
  else if (!lzmaStarted) {
    used = 1;
    if (!startLzma(data[0])) {
      return false;
    }
  }
  else if (!lzmaDone && (!decode(data, n, used) || !used)) {
    return false;  // no progress on available input: would loop forever
  }
  // after the end of the stream: rest of the entry skipped

  storedLeft -= used;
  wirePos += used;
  data += used;
  length -= used;
  if (storedLeft) {
    return true;
  }
  if (e.flags == FLAG_LZMA && !lzmaDone) {
    return false;  // stream shorter than raw length
  }
  return nextEntry();
}

bool BundleUnpacker::startLzma(uint8_t props) {
  const Byte p[LZMA_PROPS_SIZE] = {
    props, (Byte)PLAIN_DICT_BYTES, (Byte)(PLAIN_DICT_BYTES >> 8), (Byte)(PLAIN_DICT_BYTES >> 16),
    (Byte)(PLAIN_DICT_BYTES >> 24)
  };
  // same lc/lp every entry (romconv: 0/0), so no reallocation after the first
  if (LzmaDec_AllocateProbs(&lzma, p, LZMA_PROPS_SIZE, alloc) != SZ_OK) {
    return false;
  }
  lzma.dic = dict;
  lzma.dicBufSize = PLAIN_DICT_BYTES;
  LzmaDec_Init(&lzma);
  flushed = 0;
  lzmaStarted = true;
  return true;
}

// decodes into the ring dictionary, writes each new run of output bytes
bool BundleUnpacker::decode(const uint8_t *data, uint32_t length, uint32_t &used) {
  used = 0;
  for (;;) {
    ELzmaStatus status;
    SizeT inSize = length - used;

    if (rawLeft == 0) {
      // all bytes out: the end marker remains
      if (LzmaDec_DecodeToDic(&lzma, lzma.dicPos, data + used, &inSize, LZMA_FINISH_END, &status) != SZ_OK) {
        return false;
      }
      used += inSize;
      lzmaDone = status == LZMA_STATUS_FINISHED_WITH_MARK ||
                 status == LZMA_STATUS_MAYBE_FINISHED_WITHOUT_MARK;
      return true;
    }

    if (lzma.dicPos == PLAIN_DICT_BYTES) {
      lzma.dicPos = 0;
      flushed = 0;
    }
    const SizeT room = PLAIN_DICT_BYTES - lzma.dicPos;
    const SizeT limit = lzma.dicPos + (rawLeft < room ? rawLeft : room);
    if (LzmaDec_DecodeToDic(&lzma, limit, data + used, &inSize, LZMA_FINISH_ANY, &status) != SZ_OK) {
      return false;
    }
    used += inSize;

    const uint32_t produced = lzma.dicPos - flushed;
    if (produced && !out(dict + flushed, produced)) {
      return false;
    }
    flushed = lzma.dicPos;
    rawLeft -= produced;
    if (status == LZMA_STATUS_FINISHED_WITH_MARK && rawLeft) {
      return false;  // stream ended early
    }
    if (!produced && !inSize) {
      return true;  // needs more input
    }
  }
}

bool BundleUnpacker::out(const uint8_t *data, uint32_t length) {
  if (!length) {
    return true;
  }
  if (!sink(ctx, data, length)) {
    return false;
  }
  outPos += length;
  return true;
}

bool BundleUnpacker::nextEntry() {
  current++;
  state = current < count ? PAD : DONE;
  return true;
}
