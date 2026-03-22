/* Reference C for WASM / native: MSB-first pack of five ternary weights. */
#include <stdint.h>

static int to_digit(int w) {
  if (w < 0) return 0;
  if (w == 0) return 1;
  return 2;
}

uint8_t pack5_msb(int w0, int w1, int w2, int w3, int w4) {
  int d0 = to_digit(w0), d1 = to_digit(w1), d2 = to_digit(w2), d3 = to_digit(w3),
      d4 = to_digit(w4);
  return (uint8_t)(d0 * 81 + d1 * 27 + d2 * 9 + d3 * 3 + d4);
}
