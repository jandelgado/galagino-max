#include <string.h>
#include <unity.h>

#include "../../src/machines/starwars/starwars_math.cpp"

static uint16_t prom[StarWarsMath::PROM_SIZE];
static uint8_t ram[StarWarsMath::RAM_SIZE];
static StarWarsMath math;

void setUp(void) {
  memset(prom, 0, sizeof(prom));
  memset(ram, 0, sizeof(ram));
  math.init(prom, ram);
  math.reset();
}
void tearDown(void) {}

static const uint16_t ABS = 0x80; // AM: absolute word address

static void put_word(uint16_t word, uint16_t v) {
  ram[word * 2] = v >> 8;
  ram[word * 2 + 1] = v & 0xff;
}
static uint16_t get_word(uint16_t word) { return (ram[word * 2] << 8) | ram[word * 2 + 1]; }

// quotient = dividend / divisor in 1.14 fixed point
static void test_divide(void) {
  const uint8_t args[][4] = {{0x10, 0x00, 0x40, 0x00}, {0x03, 0x00, 0x09, 0x00}};
  const uint16_t expect[] = {0x1000, 0x1555};
  for (int i = 0; i < 2; i++) {
    math.write(6, args[i][0]);
    math.write(7, args[i][1]);
    math.write(4, args[i][2]);
    math.write(5, args[i][3]);
    TEST_ASSERT_EQUAL_HEX16(expect[i], (math.quotient_hi() << 8) | math.quotient_lo());
  }
}

// program at PROM 4 (MW0 = 1): word 3 = (word 0 - word 1) * word 2
static void test_multiply(void) {
  prom[4] = (0x80 << 8) | ABS | 0;        // LDA
  prom[5] = (0x40 << 8) | ABS | 1;        // LDB
  prom[6] = ((0x10 | 0x20) << 8) | ABS | 2; // CLEAR_ACC, LDC
  prom[7] = ((0x02 | 0x04) << 8) | ABS | 3; // READ_ACC, M_HALT
  put_word(0, 0x0000);
  put_word(1, 0x2000);
  put_word(2, 0x4000);
  math.write(0, 1);
  TEST_ASSERT_EQUAL_HEX16(0xe000, get_word(3));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_divide);
  RUN_TEST(test_multiply);
  return UNITY_END();
}
