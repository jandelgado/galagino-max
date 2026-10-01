#ifndef ROMDATA_H
#define ROMDATA_H

#include <cstdio>
#include <cstdlib>
#include "uzlib.h"

// Plain flash array or zlib blob unpacked to heap on first access. Same
// interface either way, so converters can switch formats without touching
// call sites.
template<typename T>
class RomData {
public:
    // Plain: points into flash, no copy.
    constexpr RomData(const T *flashData, unsigned int count)
      : packed(nullptr), packedLen(0), count(count), plain(flashData), owned(nullptr) { }

    // Compressed: unpacked on first access, kept until release().
    constexpr RomData(const unsigned char *packed, unsigned int packedLen, unsigned int count)
      : packed(packed), packedLen(packedLen), count(count), plain(nullptr), owned(nullptr) { }

    // Must stay trivial. A non-trivial destructor registers every global
    // instance for atexit, which keeps its arrays past --gc-sections and
    // duplicates their flash use. Free with release().
    ~RomData() = default;

    RomData(const RomData &) = delete;
    RomData &operator=(const RomData &) = delete;

    const T *data() const {
      if (plain) return plain;
      if (!owned) {
        owned = new T[count];

        // No dict: dest holds the whole output, so back-references read
        // from it. Saves a window buffer.
        struct uzlib_uncomp d;
        uzlib_uncompress_init(&d, nullptr, 0);
        d.source = packed;
        d.source_limit = packed + packedLen;
        d.source_read_cb = nullptr;

        // Returns window size on success, not a TINF_* status.
        int hdr = uzlib_zlib_parse_header(&d);
        d.dest_start = d.dest = (unsigned char *)owned;
        d.dest_limit = (unsigned char *)owned + count * sizeof(T);

        int status = hdr;
        if (hdr >= 0) {
          // With dest full, TINF_OK repeats until the end-of-block marker
          // and checksum are consumed.
          do {
            status = uzlib_uncompress_chksum(&d);
          } while (status == TINF_OK);
        }

        if (status != TINF_DONE || d.dest != d.dest_limit) {
          printf("RomData: decompress failed (status=%d, got %lu/%lu bytes)\n",
                 status, (unsigned long)(d.dest - d.dest_start), (unsigned long)(count * sizeof(T)));
          abort();
        }
      }
      return owned;
    }

    const T &operator[](unsigned int idx) const { return data()[idx]; }

    void release() { delete[] owned; owned = nullptr; }

private:
    const unsigned char *packed;
    unsigned int packedLen;
    unsigned int count;
    const T *plain;
    mutable T *owned;
};

#endif
