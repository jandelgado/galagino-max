#ifndef BUNDLE_UNPACK_H
#define BUNDLE_UNPACK_H

#include <cstdint>
#include "../emulation/lzma/LzmaDec.h"

// Turns a downloaded (wire) bundle into the cache entry the device maps,
// streaming: bytes in as recv() delivers them, entry bytes out in order.
// Layout: romconv/romconv/remote/bundle.py.
//
//   wire entry               cache entry
//   LZMA (64 KB dict)  -->   RAW, decoded through dict, read in place
//   PACKED             -->   PACKED, copied, unpacked to Arena on use
//
// Offsets are recomputed (4-byte aligned, same order), so the table of the
// cache entry differs from the wire table. Only a few header checks: the
// SHA-256 of the whole download decides whether the entry is kept.
class BundleUnpacker {
public:
  static constexpr uint32_t PLAIN_DICT_BYTES = 65536;
  static constexpr uint16_t MAX_ENTRIES = 64;
  typedef bool (*Sink)(void *ctx, const uint8_t *data, uint32_t length);

  // dict: PLAIN_DICT_BYTES; alloc: LZMA probabilities (Arena in the loader)
  BundleUnpacker(Sink sink, void *ctx, uint8_t *dict, ISzAllocPtr alloc);

  // false: malformed header, LZMA error, sink refused, bytes past the end
  bool feed(const uint8_t *data, uint32_t length);
  // whole bundle consumed, every entry complete
  bool finished() const { return state == DONE; }
  // cache entry size, known once the header is in; 0 before
  uint32_t entryLength() const { return entryBytes; }

private:
  static constexpr uint32_t HEADER_BYTES = 8;
  static constexpr uint32_t ENTRY_BYTES = 20;
  static constexpr uint32_t TABLE_MAX = HEADER_BYTES + ENTRY_BYTES * MAX_ENTRIES;
  static constexpr uint32_t ALIGN = 4;

  enum State { HEADER, PAD, BODY, DONE, FAILED };

  struct Entry {
    uint32_t id;
    uint32_t offset;
    uint32_t stored;
    uint32_t raw;
    uint32_t flags;
  };

  static uint32_t read32(const uint8_t *p);
  static void put32(uint8_t *p, uint32_t v);
  static uint32_t align4(uint32_t n);
  Entry entry(uint16_t i) const;
  uint32_t cacheBytes(const Entry &e) const;
  uint32_t cacheOffset(uint16_t i) const;
  bool fail();
  bool header(const uint8_t *&data, uint32_t &length);
  bool validHeader() const;
  bool writeTable();
  bool pad(const uint8_t *&data, uint32_t &length);
  bool body(const uint8_t *&data, uint32_t &length);
  bool startLzma(uint8_t props);
  bool decode(const uint8_t *data, uint32_t length, uint32_t &used);
  bool out(const uint8_t *data, uint32_t length);
  bool nextEntry();

  Sink sink;
  void *ctx;
  uint8_t *dict;
  ISzAllocPtr alloc;

  State state = HEADER;
  uint8_t table[TABLE_MAX];
  uint32_t tableLen = 0;
  uint16_t count = 0;
  uint16_t current = 0;
  uint32_t wirePos = 0;
  uint32_t outPos = 0;
  uint32_t entryBytes = 0;

  // current entry
  uint32_t storedLeft = 0;
  uint32_t rawLeft = 0;
  bool lzmaStarted = false;
  bool lzmaDone = false;
  CLzmaDec lzma;
  SizeT flushed = 0;
};

#endif
