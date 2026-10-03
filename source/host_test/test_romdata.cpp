#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <type_traits>
#include "../src/emulation/romdata.h"

static_assert(std::is_literal_type<RomData<unsigned char, COMPRESSED>>::value, "RomData<T, COMPRESSED> must be a literal type");
static_assert(std::is_literal_type<RomData<unsigned char, PLAIN>>::value, "RomData<T, PLAIN> must be a literal type");

// zlib.compress(bytes(range(64)), 9) — 64 sequential unsigned char values 0..63
static const unsigned char scalar_packed[] = {
  0x78, 0xDA, 0x63, 0x60, 0x64, 0x62, 0x66, 0x61, 0x65, 0x63, 0xE7, 0xE0, 0xE4, 0xE2, 0xE6, 0xE1,
  0xE5, 0xE3, 0x17, 0x10, 0x14, 0x12, 0x16, 0x11, 0x15, 0x13, 0x97, 0x90, 0x94, 0x92, 0x96, 0x91,
  0x95, 0x93, 0x57, 0x50, 0x54, 0x52, 0x56, 0x51, 0x55, 0x53, 0xD7, 0xD0, 0xD4, 0xD2, 0xD6, 0xD1,
  0xD5, 0xD3, 0x37, 0x30, 0x34, 0x32, 0x36, 0x31, 0x35, 0x33, 0xB7, 0xB0, 0xB4, 0xB2, 0xB6, 0xB1,
  0xB5, 0xB3, 0x07, 0x00, 0xAA, 0xE0, 0x07, 0xE1
};

// zlib.compress(struct.pack("<HHHHHH", 0x0102,0x0304, 0x0506,0x0708, 0x0900,0x0A0B), 9)
// three rows of two unsigned shorts each -> exercises RomData<unsigned short[2]>
static const unsigned char rows_packed[] = {
  0x78, 0xDA, 0x63, 0x62, 0x64, 0x61, 0x66, 0x63, 0xE5, 0x60, 0x67, 0xE0, 0xE4, 0xE6, 0x02, 0x00,
  0x01, 0x53, 0x00, 0x43
};

static const unsigned char plain_data[4] = { 10, 20, 30, 40 };

void test_compressed_scalar() {
  RomData<unsigned char, COMPRESSED> rom(scalar_packed, sizeof(scalar_packed), 64);
  for (unsigned int i = 0; i < 64; i++) assert(rom[i] == i);
  printf("1. compressed unsigned char round-trip via operator[]: OK\n");
}

void test_compressed_multidim() {
  RomData<unsigned short[2], COMPRESSED> rows(rows_packed, sizeof(rows_packed), 3);
  assert(rows[0][0] == 0x0102 && rows[0][1] == 0x0304);
  assert(rows[1][0] == 0x0506 && rows[1][1] == 0x0708);
  assert(rows[2][0] == 0x0900 && rows[2][1] == 0x0A0B);
  printf("2. compressed RomData<unsigned short[2]> chained [][]: OK\n");
}

void test_plain_zero_alloc_path() {
  RomData<unsigned char, PLAIN> plain(plain_data, 4);
  assert(plain[0] == 10 && plain[1] == 20 && plain[2] == 30 && plain[3] == 40);
  assert(plain.data() == plain_data);  // no decompression/allocation happened
  printf("3. plain constructor returns the flash pointer directly: OK\n");
}

void test_release_frees_and_is_idempotent() {
  RomData<unsigned char, COMPRESSED> rom(scalar_packed, sizeof(scalar_packed), 64);
  (void)rom[0];       // force decompression
  rom.release();
  rom.release();      // must not double-free or crash
  assert(rom[5] == 5); // re-decompresses correctly after release
  printf("4. release() frees, is idempotent, and re-decompresses on next use: OK\n");
}

void test_const_romdata() {
  static const RomData<unsigned char, PLAIN> const_check(plain_data, 4);
  assert(const_check[0] == 10);
  assert(const_check.data()[1] == 20);
  printf("5. const RomData indexing and data() access work: OK\n");
}

void test_literal_type() {
  static_assert(std::is_literal_type<RomData<unsigned char, COMPRESSED>>::value, "literal type check");
  printf("6. RomData<T> is a literal type (trivial destructor): OK\n");
}

void test_release_on_plain_is_safe() {
  RomData<unsigned char, PLAIN> plain(plain_data, 4);
  plain.release();  // must not attempt to delete[] the flash pointer
  assert(plain[0] == 10 && plain[1] == 20 && plain[2] == 30 && plain[3] == 40);
  plain.release();  // idempotent
  assert(plain.data() == plain_data);
  printf("7. release() on a plain-constructed RomData is a safe no-op: OK\n");
}

void test_unpack_prints_timing_and_ratio() {
  char buf[4096];
  const char *tmpdir = getenv("TMPDIR");
  if (!tmpdir) tmpdir = "/tmp";
  char tmpname[256];
  snprintf(tmpname, sizeof(tmpname), "%s/romdata_test_output_XXXXXX", tmpdir);
  int tmpfd = mkstemp(tmpname);
  assert(tmpfd >= 0);
  FILE *tmp = fdopen(tmpfd, "w+");
  assert(tmp);
  int saved_fd = dup(fileno(stdout));
  fflush(stdout);
  dup2(fileno(tmp), fileno(stdout));

  {
    RomData<unsigned char, COMPRESSED> rom(scalar_packed, sizeof(scalar_packed), 64);
    (void)rom[0]; // force decompression, triggers the print
  }

  fflush(stdout);
  dup2(saved_fd, fileno(stdout));
  close(saved_fd);

  rewind(tmp);
  size_t n = fread(buf, 1, sizeof(buf) - 1, tmp);
  buf[n] = '\0';
  fclose(tmp);

  assert(strstr(buf, "RomData:") != nullptr);
  assert(strstr(buf, "ms") != nullptr);
  assert(strstr(buf, "%") != nullptr);
  assert(strstr(buf, "64") != nullptr);   // decompressed byte count
  assert(strstr(buf, "72") != nullptr);   // packed byte count (sizeof(scalar_packed) == 72)
  printf("8. unpack() prints decompress timing and compression ratio: OK\n");
}

int main() {
  test_compressed_scalar();
  test_compressed_multidim();
  test_plain_zero_alloc_path();
  test_release_frees_and_is_idempotent();
  test_const_romdata();
  test_literal_type();
  test_release_on_plain_is_safe();
  test_unpack_prints_timing_and_ratio();
  printf("\nALL ROMDATA TESTS PASSED\n");
  return 0;
}
