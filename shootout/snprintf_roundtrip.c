#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "values.h"

static uint64_t bits_from_double(double x) {
  uint64_t bits = 0u;
  memcpy(&bits, &x, sizeof(bits));
  return bits;
}

int main(void) {
  char buf[256];
  uint64_t checksum = 0u;
  int i;

  for (i = 0; i < NVALUES; ++i) {
    char* end = NULL;
    double parsed = 0.0;
    int n = snprintf(buf, sizeof(buf), "%.17g", kValues[i]);
    if (n < 0 || (size_t)n >= sizeof(buf)) {
      return 2;
    }
    parsed = strtod(buf, &end);
    if (end == NULL || *end != '\0' || end == buf) {
      checksum ^= UINT64_C(0x9e3779b97f4a7c15);
    } else {
      checksum ^= bits_from_double(parsed);
    }
    checksum ^= (uint64_t)(unsigned)n << 20u;
    puts(buf);
  }

  if (checksum == UINT64_C(0x4d3c2b1a98765432)) {
    return 3;
  }
  return 0;
}
