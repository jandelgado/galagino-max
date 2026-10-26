#ifndef SHA256_H
#define SHA256_H

#include <cstdint>

// SHA-256 (FIPS 180-4) of a download, fed as it arrives. Portable: no SDK
// crypto library needed for one hash per game download.
class Sha256 {
public:
  static constexpr uint32_t DIGEST_BYTES = 32;

  Sha256();
  void update(const uint8_t *data, uint32_t length);
  // ends the hash; start a new Sha256 for the next one
  void finish(uint8_t digest[DIGEST_BYTES]);

private:
  void compress(const uint8_t *block);

  uint32_t state[8];
  uint8_t buffer[64];
  uint32_t buffered = 0;
  uint64_t total = 0;
};

#endif
