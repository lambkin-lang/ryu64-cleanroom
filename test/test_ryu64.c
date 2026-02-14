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

#if defined(RYU_NO_LIBC_TEST)
#include <stdint.h>
#include <string.h>

static uint64_t bits_from_double(double x) {
  uint64_t b;
  memcpy(&b, &x, sizeof(b));
  return b;
}

static int str_eq(const char* a, const char* b) {
  size_t i = 0u;
  while (a[i] != '\0' || b[i] != '\0') {
    if (a[i] != b[i]) {
      return 0;
    }
    i += 1u;
  }
  return 1;
}

int main(void) {
  char out[128];
  size_t out_len = 0u;
  ryu64_parse_result p;
  ryu_printf_spec spec;

  if (ryu64_to_shortest(out, sizeof(out), 1.5, &out_len) != RYU_OK || !str_eq(out, "1.5")) {
    return 1;
  }
  if (ryu64_to_9sig(out, sizeof(out), 1.23456789, &out_len) != RYU_OK || out_len == 0u) {
    return 2;
  }

  p = ryu64_from_decimal_tiny("1.5", 3u);
  if ((p.status != RYU_PARSE_OK && p.status != RYU_PARSE_INEXACT) ||
      p.parsed_len != 3u ||
      bits_from_double(p.value) != bits_from_double(1.5)) {
    return 3;
  }

  p = ryu64_from_decimal_full("1e20", 4u);
  if (p.parsed_len == 0u || p.status == RYU_PARSE_INVALID) {
    return 4;
  }

  spec.kind = RYU_FMT_G;
  spec.precision = 6;
  spec.uppercase = 0;
  spec.alternate_form = 0;
  spec.always_sign = 0;
  spec.space_sign = 0;
  if (ryu64_to_printf(out, sizeof(out), 1.0, &spec, &out_len) != RYU_OK || out_len == 0u) {
    return 5;
  }
  return 0;
}

#else

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
  uint64_t exp = (bits >> 52u) & 0x7ffu;
  uint64_t frac = bits & ((UINT64_C(1) << 52u) - UINT64_C(1));
  return exp == 0x7ffu && frac != 0u;
}

static int parse_status_success(ryu_parse_status st) {
  return st == RYU_PARSE_OK || st == RYU_PARSE_INEXACT;
}

static int text_has_nonzero_digit(const char* s) {
  size_t i = 0u;
  while (s[i] != '\0') {
    if (s[i] >= '1' && s[i] <= '9') {
      return 1;
    }
    i += 1u;
  }
  return 0;
}

static int expect_parse_value(
    const char* text,
    ryu_parse_status expected_status,
    size_t expected_len,
    uint64_t expected_bits) {
  ryu64_parse_result r = ryu64_from_decimal_tiny(text, strlen(text));
  if (r.status != expected_status) {
    fprintf(stderr,
            "parse status mismatch text='%s' got=%d expected=%d\n",
            text,
            (int)r.status,
            (int)expected_status);
    return 0;
  }
  if (r.parsed_len != expected_len) {
    fprintf(stderr,
            "parse len mismatch text='%s' got=%lu expected=%lu\n",
            text,
            (unsigned long)r.parsed_len,
            (unsigned long)expected_len);
    return 0;
  }
  if (parse_status_success(r.status) && bits_from_double(r.value) != expected_bits) {
    fprintf(stderr,
            "parse value mismatch text='%s' got=0x%016llx expected=0x%016llx\n",
            text,
            (unsigned long long)bits_from_double(r.value),
            (unsigned long long)expected_bits);
    return 0;
  }
  return 1;
}

static int run_parser_unit_tests(void) {
  ryu64_parse_result r;

  if (!expect_parse_value("0", RYU_PARSE_OK, 1u, UINT64_C(0x0000000000000000))) {
    return 0;
  }
  if (!expect_parse_value("-0", RYU_PARSE_OK, 2u, UINT64_C(0x8000000000000000))) {
    return 0;
  }
  if (!expect_parse_value(".5", RYU_PARSE_OK, 2u, bits_from_double(0.5))) {
    return 0;
  }
  if (!expect_parse_value("1e", RYU_PARSE_OK, 1u, bits_from_double(1.0))) {
    return 0;
  }
  if (!expect_parse_value("  +12.5xyz", RYU_PARSE_OK, 7u, bits_from_double(12.5))) {
    return 0;
  }
  if (!expect_parse_value("inf", RYU_PARSE_OK, 3u, UINT64_C(0x7ff0000000000000))) {
    return 0;
  }
  if (!expect_parse_value("-Infinity!", RYU_PARSE_OK, 9u, UINT64_C(0xfff0000000000000))) {
    return 0;
  }

  r = ryu64_from_decimal_tiny("nan(payload)", 12u);
  if (r.status != RYU_PARSE_OK || r.parsed_len != 3u || !isnan(r.value)) {
    fprintf(stderr, "nan parsing mismatch\n");
    return 0;
  }

  if (!expect_parse_value("", RYU_PARSE_INVALID, 0u, 0u)) {
    return 0;
  }
  if (!expect_parse_value("   ", RYU_PARSE_INVALID, 0u, 0u)) {
    return 0;
  }
  if (!expect_parse_value("+", RYU_PARSE_INVALID, 0u, 0u)) {
    return 0;
  }
  if (!expect_parse_value(".", RYU_PARSE_INVALID, 0u, 0u)) {
    return 0;
  }

  r = ryu64_from_decimal_tiny("1e20", 4u);
  if (r.status != RYU_PARSE_OUT_OF_RANGE || r.parsed_len != 4u) {
    fprintf(stderr, "range parsing mismatch for 1e20\n");
    return 0;
  }

  r = ryu64_from_decimal_tiny("12345678901234567890", 20u);
  if (r.status != RYU_PARSE_OUT_OF_RANGE || r.parsed_len != 20u) {
    fprintf(stderr, "sig-digit overflow mismatch\n");
    return 0;
  }

  if (!expect_parse_value("0e9999", RYU_PARSE_OK, 6u, UINT64_C(0x0000000000000000))) {
    return 0;
  }

  r = ryu64_from_decimal_full("1e20", 4u);
  if (!parse_status_success(r.status) || r.parsed_len != 4u) {
    fprintf(stderr, "full parser failed on 1e20 status=%d\n", (int)r.status);
    return 0;
  }
#if defined(RYU_ENABLE_LIBC_ORACLE)
  {
    char* end = NULL;
    double oracle = strtod("1e20", &end);
    if (end == NULL || *end != '\0' || bits_from_double(r.value) != bits_from_double(oracle)) {
      fprintf(stderr, "full parser mismatch on 1e20\n");
      return 0;
    }
  }
#endif

  r = ryu64_from_decimal_full("1.25", 4u);
  if (!parse_status_success(r.status) || r.parsed_len != 4u || bits_from_double(r.value) != bits_from_double(1.25)) {
    fprintf(stderr, "full parser tiny-path mismatch\n");
    return 0;
  }

  r = ryu64_from_decimal_full("-inf", 4u);
  if (r.status != RYU_PARSE_OK || r.parsed_len != 4u || !isinf(r.value) || !signbit(r.value)) {
    fprintf(stderr, "full parser inf mismatch\n");
    return 0;
  }

  r = ryu64_from_decimal_full("nan(payload)_tail", 17u);
  if (r.status != RYU_PARSE_OK || r.parsed_len != 12u || !isnan(r.value)) {
    fprintf(stderr, "full parser nan payload mismatch\n");
    return 0;
  }

  r = ryu64_from_decimal_full("1e4000", 6u);
  if (r.status != RYU_PARSE_OVERFLOW || !isinf(r.value)) {
    fprintf(stderr, "full parser overflow classification mismatch\n");
    return 0;
  }

  r = ryu64_from_decimal_full("1e-4000", 7u);
  if (r.status != RYU_PARSE_UNDERFLOW || bits_from_double(r.value) != UINT64_C(0x0000000000000000)) {
    fprintf(stderr, "full parser underflow classification mismatch\n");
    return 0;
  }

  return 1;
}

static int run_tiny_roundtrip_from_shortest(unsigned iters) {
  uint64_t state = UINT64_C(0x2456a8ce13579bdf);
  unsigned i;
  unsigned accepted = 0u;

  for (i = 0u; i < iters; ++i) {
    uint64_t in_bits = xorshift64(&state);
    double x = double_from_bits(in_bits);
    char out[256];
    size_t out_len = 0u;
    ryu64_parse_result p;

    if (is_nan_bits(in_bits)) {
      continue;
    }
    if (((in_bits >> 52u) & 0x7ffu) == 0x7ffu) {
      continue;
    }

    if (ryu64_to_shortest(out, sizeof(out), x, &out_len) != RYU_OK) {
      fprintf(stderr, "shortest generation failed in tiny roundtrip\n");
      return 0;
    }

    p = ryu64_from_decimal_tiny(out, out_len);
    if (parse_status_success(p.status)) {
      accepted += 1u;
      if (bits_from_double(p.value) != in_bits) {
        fprintf(stderr,
                "tiny roundtrip mismatch in=0x%016llx out=0x%016llx str='%s'\n",
                (unsigned long long)in_bits,
                (unsigned long long)bits_from_double(p.value),
                out);
        return 0;
      }
    } else if (p.status != RYU_PARSE_OUT_OF_RANGE) {
      fprintf(stderr,
              "unexpected tiny parse status=%d str='%s'\n",
              (int)p.status,
              out);
      return 0;
    }
  }

  if (accepted < 100u) {
    fprintf(stderr, "accepted too few tiny roundtrip cases: %u\n", accepted);
    return 0;
  }
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

#if defined(RYU_ENABLE_LIBC_ORACLE)
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
    if (((in_bits >> 52u) & 0x7ffu) == 0x7ffu) {
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

static size_t append_u64_dec(char* out, uint64_t x) {
  char rev[32];
  size_t n = 0u;
  size_t i;
  if (x == 0u) {
    out[0] = '0';
    return 1u;
  }
  while (x != 0u) {
    rev[n++] = (char)('0' + (x % UINT64_C(10)));
    x /= UINT64_C(10);
  }
  for (i = 0u; i < n; ++i) {
    out[i] = rev[n - 1u - i];
  }
  return n;
}

static int run_parse_diff_vs_strtod(unsigned iters) {
  uint64_t state = UINT64_C(0xa1b2c3d4e5f60718);
  unsigned i;

  for (i = 0u; i < iters; ++i) {
    char digits[32];
    char text[128];
    size_t len;
    size_t out = 0u;
    unsigned dcount = (unsigned)((xorshift64(&state) % 19u) + 1u);
    unsigned j;
    int q = (int)(xorshift64(&state) % 39u) - 19;
    unsigned split;
    int exp_part;
    ryu64_parse_result p;
    char* end = NULL;
    double libc_v;

    digits[0] = (char)('1' + (xorshift64(&state) % 9u));
    for (j = 1u; j < dcount; ++j) {
      digits[j] = (char)('0' + (xorshift64(&state) % 10u));
    }
    digits[dcount] = '\0';

    if ((xorshift64(&state) & UINT64_C(1)) != 0u) {
      text[out++] = '-';
    }

    split = (unsigned)(xorshift64(&state) % (uint64_t)(dcount + 1u));
    if (split == 0u) {
      text[out++] = '.';
      memcpy(text + out, digits, dcount);
      out += dcount;
    } else if (split == dcount) {
      memcpy(text + out, digits, dcount);
      out += dcount;
    } else {
      memcpy(text + out, digits, split);
      out += split;
      text[out++] = '.';
      memcpy(text + out, digits + split, dcount - split);
      out += dcount - split;
    }

    exp_part = q + (int)dcount - (int)split;
    text[out++] = (xorshift64(&state) & UINT64_C(1)) != 0u ? 'E' : 'e';
    if (exp_part >= 0) {
      text[out++] = '+';
    } else {
      text[out++] = '-';
      exp_part = -exp_part;
    }
    out += append_u64_dec(text + out, (uint64_t)exp_part);
    text[out] = '\0';

    len = out;
    p = ryu64_from_decimal_tiny(text, len);
    if (!parse_status_success(p.status) || p.parsed_len != len) {
      fprintf(stderr, "tiny parser rejected bounded input '%s' status=%d\n", text, (int)p.status);
      return 0;
    }

    libc_v = strtod(text, &end);
    if (end == NULL || (size_t)(end - text) != len) {
      fprintf(stderr, "strtod parse mismatch for '%s'\n", text);
      return 0;
    }

    if (bits_from_double(p.value) != bits_from_double(libc_v)) {
      fprintf(stderr,
              "parse oracle mismatch str='%s' tiny=0x%016llx libc=0x%016llx\n",
              text,
              (unsigned long long)bits_from_double(p.value),
              (unsigned long long)bits_from_double(libc_v));
      return 0;
    }
  }

  return 1;
}

static int run_parse_full_diff_vs_strtod(unsigned iters) {
  uint64_t state = UINT64_C(0x7766554433221100);
  unsigned i;

  for (i = 0u; i < iters; ++i) {
    char digits[256];
    char text[512];
    size_t out = 0u;
    unsigned dcount = (unsigned)((xorshift64(&state) % 120u) + 1u);
    unsigned j;
    unsigned split;
    int q = (int)(xorshift64(&state) % 641u) - 320;
    int exp_part;
    ryu64_parse_result p;
    char* end = NULL;
    double oracle;

    digits[0] = (char)('1' + (xorshift64(&state) % 9u));
    for (j = 1u; j < dcount; ++j) {
      digits[j] = (char)('0' + (xorshift64(&state) % 10u));
    }
    digits[dcount] = '\0';

    if ((xorshift64(&state) & UINT64_C(1)) != 0u) {
      text[out++] = '-';
    }

    split = (unsigned)(xorshift64(&state) % (uint64_t)(dcount + 1u));
    if (split == 0u) {
      text[out++] = '.';
      memcpy(text + out, digits, dcount);
      out += dcount;
    } else if (split == dcount) {
      memcpy(text + out, digits, dcount);
      out += dcount;
    } else {
      memcpy(text + out, digits, split);
      out += split;
      text[out++] = '.';
      memcpy(text + out, digits + split, dcount - split);
      out += dcount - split;
    }

    exp_part = q + (int)dcount - (int)split;
    text[out++] = (xorshift64(&state) & UINT64_C(1)) != 0u ? 'E' : 'e';
    if (exp_part >= 0) {
      text[out++] = '+';
    } else {
      text[out++] = '-';
      exp_part = -exp_part;
    }
    out += append_u64_dec(text + out, (uint64_t)exp_part);
    text[out] = '\0';

    p = ryu64_from_decimal_full(text, out);
    oracle = strtod(text, &end);
    if (end == NULL || (size_t)(end - text) != out) {
      fprintf(stderr, "strtod full parse mismatch for '%s'\n", text);
      return 0;
    }

    if (isinf(oracle)) {
      if (p.status != RYU_PARSE_OVERFLOW || !isinf(p.value) || signbit(p.value) != signbit(oracle)) {
        fprintf(stderr, "full overflow mismatch str='%s' status=%d\n", text, (int)p.status);
        return 0;
      }
      continue;
    }

    if (oracle == 0.0 && digits[0] != '0') {
      if (p.status != RYU_PARSE_UNDERFLOW && bits_from_double(p.value) != bits_from_double(oracle)) {
        fprintf(stderr, "full underflow mismatch str='%s' status=%d\n", text, (int)p.status);
        return 0;
      }
      continue;
    }

    if (!parse_status_success(p.status) || p.parsed_len != out) {
      fprintf(stderr, "full parser rejected finite input '%s' status=%d\n", text, (int)p.status);
      return 0;
    }
    if (bits_from_double(p.value) != bits_from_double(oracle)) {
      fprintf(stderr,
              "full parser oracle mismatch str='%s' full=0x%016llx libc=0x%016llx status=%d\n",
              text,
              (unsigned long long)bits_from_double(p.value),
              (unsigned long long)bits_from_double(oracle),
              (int)p.status);
      return 0;
    }
  }
  return 1;
}

static int run_parse_full_boundary_vs_strtod(void) {
  static const char* cases[] = {
      "1e20",
      "-1e20",
      "1e38",
      "-1e38",
      "9.999999999999999999e38",
      "-9.999999999999999999e38",
      "1234567890123456789e24",
      "-1234567890123456789e24",
      "9999999999999999999e38",
      "-9999999999999999999e38",
      "2.2250738585072014e-308",
      "2.2250738585072013e-308",
      "1.7976931348623157e308",
      "1.7976931348623158e308",
      "4.9406564584124654e-324",
      "2.4703282292062327e-324",
      "7.4109846876186982e-324",
      "5e-324",
      "2e-324",
      "1e-323",
      "1.0000000000000001e-308",
      "9.999999999999999e307",
  };
  size_t i;
  for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    const char* text = cases[i];
    size_t len = strlen(text);
    ryu64_parse_result p = ryu64_from_decimal_full(text, len);
    char* end = NULL;
    double oracle = strtod(text, &end);
    if (end == NULL || (size_t)(end - text) != len) {
      fprintf(stderr, "strtod boundary parse mismatch for '%s'\n", text);
      return 0;
    }
    if (p.parsed_len != len) {
      fprintf(stderr, "full boundary parsed_len mismatch for '%s'\n", text);
      return 0;
    }
    if (isinf(oracle)) {
      if (p.status != RYU_PARSE_OVERFLOW || !isinf(p.value) || signbit(p.value) != signbit(oracle)) {
        fprintf(stderr, "full boundary overflow mismatch for '%s' status=%d\n", text, (int)p.status);
        return 0;
      }
      continue;
    }
    if (oracle == 0.0 && text_has_nonzero_digit(text)) {
      if (p.status != RYU_PARSE_UNDERFLOW && bits_from_double(p.value) != bits_from_double(oracle)) {
        fprintf(stderr, "full boundary underflow mismatch for '%s' status=%d\n", text, (int)p.status);
        return 0;
      }
      continue;
    }
    if (!parse_status_success(p.status)) {
      fprintf(stderr, "full boundary unexpected status for '%s': %d\n", text, (int)p.status);
      return 0;
    }
    if (bits_from_double(p.value) != bits_from_double(oracle)) {
      fprintf(stderr,
              "full boundary value mismatch for '%s': full=0x%016llx oracle=0x%016llx\n",
              text,
              (unsigned long long)bits_from_double(p.value),
              (unsigned long long)bits_from_double(oracle));
      return 0;
    }
  }
  return 1;
}
#endif

int main(void) {
  if (!run_parser_unit_tests()) {
    return 1;
  }
  if (!run_tiny_roundtrip_from_shortest(30000u)) {
    return 1;
  }
  if (!run_9sig_smoke(10000u)) {
    return 1;
  }

#if defined(RYU_ENABLE_LIBC_ORACLE)
  if (!run_roundtrip_edges()) {
    return 1;
  }
  if (!run_roundtrip_random(20000u)) {
    return 1;
  }
  if (!run_printf_diff(3000u)) {
    return 1;
  }
  if (!run_parse_diff_vs_strtod(20000u)) {
    return 1;
  }
  if (!run_parse_full_diff_vs_strtod(2000u)) {
    return 1;
  }
  if (!run_parse_full_boundary_vs_strtod()) {
    return 1;
  }
#endif

  printf("all tests passed\n");
  return 0;
}

#endif
