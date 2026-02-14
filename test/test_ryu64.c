/*
 * MIT License
 *
 * Copyright (c) 2026 lambkin-lang
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "ryu64.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t bits_from_double(double x) {
  uint64_t b;
  memcpy(&b, &x, sizeof(b));
  return b;
}

static double double_from_bits(uint64_t b) {
  double x;
  memcpy(&x, &b, sizeof(x));
  return x;
}

static uint64_t xorshift64(uint64_t* state) {
  uint64_t x = *state;
  x ^= x << 13;
  x ^= x >> 7;
  x ^= x << 17;
  *state = x;
  return x;
}

static int is_nan_bits(uint64_t bits) {
  uint64_t exp = (bits >> 52) & 0x7ffu;
  uint64_t frac = bits & ((UINT64_C(1) << 52) - UINT64_C(1));
  return exp == 0x7ffu && frac != 0u;
}

static int run_roundtrip_edges(void) {
  const uint64_t edge_bits[] = {
      UINT64_C(0x0000000000000000),
      UINT64_C(0x8000000000000000),
      UINT64_C(0x0000000000000001),
      UINT64_C(0x8000000000000001),
      UINT64_C(0x000fffffffffffff),
      UINT64_C(0x0010000000000000),
      UINT64_C(0x7fefffffffffffff),
      UINT64_C(0x3ff0000000000000),
      UINT64_C(0x3fefffffffffffff),
      UINT64_C(0x3ff0000000000001),
      UINT64_C(0x4340000000000000),
      UINT64_C(0x44b52d02c7e14af6),
      UINT64_C(0x7ff0000000000000),
      UINT64_C(0xfff0000000000000),
      UINT64_C(0x7ff8000000000000),
  };
  size_t i;

  for (i = 0u; i < sizeof(edge_bits) / sizeof(edge_bits[0]); ++i) {
    uint64_t in_bits = edge_bits[i];
    double x = double_from_bits(in_bits);
    char out[512];
    size_t out_len = 0u;
    ryu_status st = ryu64_to_shortest(out, sizeof(out), x, &out_len);
    if (st != RYU_OK) {
      fprintf(stderr, "shortest edge failed status=%d bits=0x%016llx\n", (int)st, (unsigned long long)in_bits);
      return 0;
    }
    if (is_nan_bits(in_bits)) {
      continue;
    }
    if (((in_bits >> 52) & 0x7ffu) == 0x7ffu) {
      continue;
    }
    {
      char* end = NULL;
      double y = strtod(out, &end);
      uint64_t out_bits = bits_from_double(y);
      if (end == NULL || *end != '\0') {
        fprintf(stderr, "strtod parse failed for '%s' bits=0x%016llx\n", out, (unsigned long long)in_bits);
        return 0;
      }
      if (out_bits != in_bits) {
        fprintf(stderr,
                "roundtrip edge mismatch in=0x%016llx out=0x%016llx str='%s'\n",
                (unsigned long long)in_bits,
                (unsigned long long)out_bits,
                out);
        return 0;
      }
    }
  }

  return 1;
}

static int run_roundtrip_random(unsigned iters) {
  uint64_t state = UINT64_C(0x0123456789abcdef);
  unsigned i;

  for (i = 0u; i < iters; ++i) {
    uint64_t bits = xorshift64(&state);
    double x;
    char out[512];
    size_t out_len = 0u;
    char* end = NULL;
    double y;
    uint64_t y_bits;

    if (is_nan_bits(bits)) {
      continue;
    }

    x = double_from_bits(bits);
    if (ryu64_to_shortest(out, sizeof(out), x, &out_len) != RYU_OK) {
      fprintf(stderr, "shortest random failed bits=0x%016llx\n", (unsigned long long)bits);
      return 0;
    }

    y = strtod(out, &end);
    if (end == NULL || *end != '\0') {
      fprintf(stderr, "strtod random parse failed str='%s' bits=0x%016llx\n", out, (unsigned long long)bits);
      return 0;
    }

    y_bits = bits_from_double(y);
    if (y_bits != bits) {
      fprintf(stderr,
              "roundtrip random mismatch in=0x%016llx out=0x%016llx str='%s'\n",
              (unsigned long long)bits,
              (unsigned long long)y_bits,
              out);
      return 0;
    }
  }

  return 1;
}

static int run_printf_diff(unsigned iters) {
#if defined(RYU_TIER_FULL)
  static const int precisions[] = {0, 1, 2, 6, 9, 15};
  static const ryu_fmt_kind kinds[] = {RYU_FMT_F, RYU_FMT_E, RYU_FMT_G};
  uint64_t state = UINT64_C(0xfedcba9876543210);
  unsigned i;

  for (i = 0u; i < iters; ++i) {
    uint64_t bits = xorshift64(&state);
    double x = double_from_bits(bits);
    size_t k;

    if (isnan(x) || isinf(x)) {
      continue;
    }

    for (k = 0u; k < sizeof(kinds) / sizeof(kinds[0]); ++k) {
      size_t p;
      for (p = 0u; p < sizeof(precisions) / sizeof(precisions[0]); ++p) {
        int upper;
        for (upper = 0; upper <= 1; ++upper) {
          ryu_printf_spec spec;
          char out_ryu[1024];
          char out_libc[1024];
          char fmt[16];
          size_t out_len = 0u;
          int n_libc;
          char conv;

          spec.kind = kinds[k];
          spec.precision = precisions[p];
          spec.uppercase = upper;
          spec.alternate_form = 0;
          spec.always_sign = 0;
          spec.space_sign = 0;

          if (ryu64_to_printf(out_ryu, sizeof(out_ryu), x, &spec, &out_len) != RYU_OK) {
            fprintf(stderr, "ryu printf failed bits=0x%016llx\n", (unsigned long long)bits);
            return 0;
          }

          conv = (spec.kind == RYU_FMT_F) ? 'f' : (spec.kind == RYU_FMT_E ? 'e' : 'g');
          if (upper) {
            conv = (char)(conv - 'a' + 'A');
          }
          snprintf(fmt, sizeof(fmt), "%%.%d%c", spec.precision, conv);
          n_libc = snprintf(out_libc, sizeof(out_libc), fmt, x);
          if (n_libc < 0 || (size_t)n_libc >= sizeof(out_libc)) {
            fprintf(stderr, "libc snprintf failed\n");
            return 0;
          }

          if (strcmp(out_ryu, out_libc) != 0) {
            fprintf(stderr,
                    "printf mismatch bits=0x%016llx fmt='%s' ryu='%s' libc='%s'\n",
                    (unsigned long long)bits,
                    fmt,
                    out_ryu,
                    out_libc);
            return 0;
          }
        }
      }
    }
  }
#else
  (void)iters;
#endif
  return 1;
}

static int run_9sig_smoke(unsigned iters) {
  uint64_t state = UINT64_C(0x1122334455667788);
  unsigned i;
  for (i = 0u; i < iters; ++i) {
    double x = double_from_bits(xorshift64(&state));
    char out[256];
    size_t out_len = 0u;
    if (ryu64_to_9sig(out, sizeof(out), x, &out_len) != RYU_OK) {
      fprintf(stderr, "9sig failed at iter=%u\n", i);
      return 0;
    }
    if (out_len == 0u || out_len >= sizeof(out)) {
      fprintf(stderr, "9sig bad length at iter=%u\n", i);
      return 0;
    }
  }
  return 1;
}

int main(void) {
  if (!run_roundtrip_edges()) {
    return 1;
  }
  if (!run_roundtrip_random(20000u)) {
    return 1;
  }
  if (!run_9sig_smoke(10000u)) {
    return 1;
  }
  if (!run_printf_diff(3000u)) {
    return 1;
  }
  printf("all tests passed\n");
  return 0;
}
