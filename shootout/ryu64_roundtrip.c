#ifdef RYU64_WASI_BUILD
#include "wasi_io.h"
#else
#include <stdio.h>
#endif

#include <stdint.h>
#include <string.h>

#include "ryu64.h"
#include "values.h"

static uint64_t bits_from_double(double x) {
  uint64_t bits = 0u;
  memcpy(&bits, &x, sizeof(bits));
  return bits;
}

static int parse_ok(ryu_parse_status st) {
  return st == RYU_PARSE_OK || st == RYU_PARSE_INEXACT;
}

int main(void) {
  char buf[256];
  size_t len = 0u;
  ryu_printf_spec spec;
  uint64_t checksum = 0u;
  int i;

  spec.kind = RYU_FMT_G;
  spec.precision = 17;
  spec.uppercase = 0;
  spec.alternate_form = 0;
  spec.always_sign = 0;
  spec.space_sign = 0;

  for (i = 0; i < NVALUES; ++i) {
    ryu64_parse_result parsed;
    if (ryu64_to_printf(buf, sizeof(buf), kValues[i], &spec, &len) != RYU_OK ||
        len >= sizeof(buf)) {
      return 2;
    }
    buf[len] = '\0';
    parsed = ryu64_from_decimal_full(buf, len);
    if (parse_ok(parsed.status) && parsed.parsed_len == len) {
      checksum ^= bits_from_double(parsed.value);
    } else {
      checksum ^= UINT64_C(0x9e3779b97f4a7c15);
    }
    checksum ^= (uint64_t)len << 20u;
#ifdef RYU64_WASI_BUILD
    write_line(buf, len);
#else
    puts(buf);
#endif
  }

  if (checksum == UINT64_C(0x4d3c2b1a98765432)) {
    return 3;
  }
  return 0;
}
