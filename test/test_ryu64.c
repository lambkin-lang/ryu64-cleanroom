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

#include "ryu64_internal.h"

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
  if (r.status != RYU_PARSE_OK || r.parsed_len != 3u || bits_from_double(r.value) != UINT64_C(0x7ff8000000000000)) {
    fprintf(stderr, "nan parsing mismatch\n");
    return 0;
  }
  r = ryu64_from_decimal_tiny("-nan(payload)", 13u);
  if (r.status != RYU_PARSE_OK || r.parsed_len != 4u || bits_from_double(r.value) != UINT64_C(0xfff8000000000000)) {
    fprintf(stderr, "tiny parser signed nan payload mismatch\n");
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

  r = ryu64_from_decimal_tiny("5e-324", 6u);
  if (r.status != RYU_PARSE_OUT_OF_RANGE || r.parsed_len != 6u) {
    fprintf(stderr, "tiny parser should reject subnormal-scale text 5e-324\n");
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

  r = ryu64_from_decimal_full("5e-324", 6u);
  if (!parse_status_success(r.status) || r.parsed_len != 6u || bits_from_double(r.value) != UINT64_C(0x0000000000000001)) {
    fprintf(stderr, "full parser subnormal mismatch for 5e-324\n");
    return 0;
  }

  r = ryu64_from_decimal_full("-inf", 4u);
  if (r.status != RYU_PARSE_OK || r.parsed_len != 4u || !isinf(r.value) || !signbit(r.value)) {
    fprintf(stderr, "full parser inf mismatch\n");
    return 0;
  }

  r = ryu64_from_decimal_full("nan(payload)_tail", 17u);
  if (r.status != RYU_PARSE_OK || r.parsed_len != 12u || bits_from_double(r.value) != UINT64_C(0x7ff8000000000000)) {
    fprintf(stderr, "full parser nan payload mismatch\n");
    return 0;
  }
  r = ryu64_from_decimal_full("-nan(payload)", 13u);
  if (r.status != RYU_PARSE_OK || r.parsed_len != 13u || bits_from_double(r.value) != UINT64_C(0xfff8000000000000)) {
    fprintf(stderr, "full parser signed nan payload mismatch\n");
    return 0;
  }
  r = ryu64_from_decimal_full("nan(pay-load)", 13u);
  if (r.status != RYU_PARSE_OK || r.parsed_len != 3u || bits_from_double(r.value) != UINT64_C(0x7ff8000000000000)) {
    fprintf(stderr, "full parser invalid nan payload should stop at bare nan\n");
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
static const uint64_t kPow10U64[20] = {
    UINT64_C(1),
    UINT64_C(10),
    UINT64_C(100),
    UINT64_C(1000),
    UINT64_C(10000),
    UINT64_C(100000),
    UINT64_C(1000000),
    UINT64_C(10000000),
    UINT64_C(100000000),
    UINT64_C(1000000000),
    UINT64_C(10000000000),
    UINT64_C(100000000000),
    UINT64_C(1000000000000),
    UINT64_C(10000000000000),
    UINT64_C(100000000000000),
    UINT64_C(1000000000000000),
    UINT64_C(10000000000000000),
    UINT64_C(100000000000000000),
    UINT64_C(1000000000000000000),
    UINT64_C(10000000000000000000),
};

static size_t append_u64_dec_local(char* out, uint64_t x) {
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

static int parse_shortest_components(const char* s, uint64_t* out_sig, int* out_exp10, unsigned* out_digits) {
  char digits[64];
  size_t i = 0u;
  unsigned dcount = 0u;
  unsigned frac_digits = 0u;
  int seen_dot = 0;
  int exp_sign = 1;
  int exp_part = 0;
  size_t first;
  size_t last;
  uint64_t sig = 0u;
  size_t j;

  if (s[i] == '+' || s[i] == '-') {
    i += 1u;
  }
  if (s[i] == '\0') {
    return 0;
  }

  while (s[i] != '\0' && s[i] != 'e' && s[i] != 'E') {
    char c = s[i];
    if (c == '.') {
      if (seen_dot) {
        return 0;
      }
      seen_dot = 1;
    } else if (c >= '0' && c <= '9') {
      if (dcount >= sizeof(digits)) {
        return 0;
      }
      digits[dcount++] = c;
      if (seen_dot) {
        frac_digits += 1u;
      }
    } else {
      return 0;
    }
    i += 1u;
  }

  if (dcount == 0u) {
    return 0;
  }

  if (s[i] == 'e' || s[i] == 'E') {
    i += 1u;
    if (s[i] == '+' || s[i] == '-') {
      exp_sign = (s[i] == '-') ? -1 : 1;
      i += 1u;
    }
    if (s[i] < '0' || s[i] > '9') {
      return 0;
    }
    while (s[i] >= '0' && s[i] <= '9') {
      if (exp_part < 100000000) {
        exp_part = exp_part * 10 + (int)(s[i] - '0');
      }
      i += 1u;
    }
  }

  if (s[i] != '\0') {
    return 0;
  }

  first = 0u;
  while (first < dcount && digits[first] == '0') {
    first += 1u;
  }
  if (first == dcount) {
    *out_sig = 0u;
    *out_exp10 = 0;
    *out_digits = 1u;
    return 1;
  }

  last = dcount;
  *out_exp10 = exp_sign * exp_part - (int)frac_digits;
  while (last > first + 1u && digits[last - 1u] == '0') {
    last -= 1u;
    *out_exp10 += 1;
  }

  for (j = first; j < last; ++j) {
    uint64_t prev = sig;
    sig = sig * UINT64_C(10) + (uint64_t)(digits[j] - '0');
    if (sig < prev) {
      return 0;
    }
  }

  *out_sig = sig;
  *out_digits = (unsigned)(last - first);
  return 1;
}

static int build_shorter_candidate(
    char* out,
    size_t out_cap,
    int negative,
    uint64_t sig,
    unsigned digits,
    int exp10) {
  char sig_text[32];
  size_t pos = 0u;
  size_t sig_len = append_u64_dec_local(sig_text, sig);
  int e = exp10;

  if (digits == 0u || sig_len != (size_t)digits) {
    return 0;
  }
  if (negative) {
    if (pos >= out_cap) {
      return 0;
    }
    out[pos++] = '-';
  }
  if (pos >= out_cap) {
    return 0;
  }
  out[pos++] = sig_text[0];
  if (digits > 1u) {
    if (pos + 1u + (size_t)(digits - 1u) >= out_cap) {
      return 0;
    }
    out[pos++] = '.';
    memcpy(out + pos, sig_text + 1, (size_t)(digits - 1u));
    pos += (size_t)(digits - 1u);
  }
  if (pos + 2u >= out_cap) {
    return 0;
  }
  out[pos++] = 'e';
  if (e < 0) {
    out[pos++] = '-';
    e = -e;
  } else {
    out[pos++] = '+';
  }
  if (pos >= out_cap) {
    return 0;
  }
  pos += append_u64_dec_local(out + pos, (uint64_t)e);
  if (pos >= out_cap) {
    return 0;
  }
  out[pos] = '\0';
  return 1;
}

static int verify_shortest_minimality(uint64_t bits, const char* shortest) {
  const int kDeltaRadius = 8;
  const uint64_t kAbsMask = UINT64_C(0x7fffffffffffffff);
  int negative = (bits >> 63u) != 0u;
  uint64_t abs_bits = bits & kAbsMask;
  uint64_t sig = 0u;
  int exp10 = 0;
  unsigned digits = 0u;
  unsigned q;

  if (abs_bits == 0u || ((abs_bits >> 52u) & 0x7ffu) == 0x7ffu) {
    return 1;
  }
  if (!parse_shortest_components(shortest, &sig, &exp10, &digits)) {
    fprintf(stderr, "shortest parse failure for minimality str='%s'\n", shortest);
    return 0;
  }
  if (sig == 0u || digits <= 1u || digits >= 20u) {
    return 1;
  }

  for (q = 1u; q < digits; ++q) {
    unsigned cut = digits - q;
    uint64_t cut_pow10 = kPow10U64[cut];
    uint64_t lo = (q == 1u) ? UINT64_C(1) : kPow10U64[q - 1u];
    uint64_t hi = kPow10U64[q] - UINT64_C(1);
    uint64_t base = sig / cut_pow10;
    int cand_exp10 = exp10 + (int)cut;
    int delta;

    for (delta = -kDeltaRadius; delta <= kDeltaRadius; ++delta) {
      int64_t cand_i = (int64_t)base + (int64_t)delta;
      uint64_t cand_sig;
      char cand_text[96];
      char* end = NULL;
      double parsed;
      if (cand_i < (int64_t)lo || cand_i > (int64_t)hi) {
        continue;
      }
      cand_sig = (uint64_t)cand_i;
      if (q > 1u && (cand_sig % UINT64_C(10)) == 0u) {
        continue;
      }
      if (!build_shorter_candidate(cand_text, sizeof(cand_text), negative, cand_sig, q, cand_exp10)) {
        continue;
      }
      parsed = strtod(cand_text, &end);
      if (end != NULL && *end == '\0' && bits_from_double(parsed) == bits) {
        fprintf(stderr,
                "shortest minimality violated bits=0x%016llx shortest='%s' shorter='%s'\n",
                (unsigned long long)bits,
                shortest,
                cand_text);
        return 0;
      }
    }
  }

  return 1;
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
      if (!verify_shortest_minimality(in_bits, out)) {
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

static int run_shortest_minimality_random(unsigned iters) {
  uint64_t state = UINT64_C(0x5bd1e9951234abcd);
  unsigned checked = 0u;

  while (checked < iters) {
    uint64_t bits = xorshift64(&state);
    uint64_t abs_bits = bits & UINT64_C(0x7fffffffffffffff);
    double x;
    char out[512];
    size_t out_len = 0u;

    if (abs_bits == 0u || ((abs_bits >> 52u) & 0x7ffu) == 0x7ffu) {
      continue;
    }

    x = double_from_bits(bits);
    if (ryu64_to_shortest(out, sizeof(out), x, &out_len) != RYU_OK) {
      fprintf(stderr, "shortest minimality generation failed bits=0x%016llx\n", (unsigned long long)bits);
      return 0;
    }
    if (!verify_shortest_minimality(bits, out)) {
      return 0;
    }
    checked += 1u;
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

static int run_printf_nan_sign_policy(void) {
#if defined(RYU_TIER_FULL)
  static const uint64_t nan_bits[] = {
      UINT64_C(0x7ff8000000000000),
      UINT64_C(0xfff8000000000000),
      UINT64_C(0x7ff8000000000001),
      UINT64_C(0xfff8000000000001),
  };
  static const int precisions[] = {-1, 0, 6};
  static const ryu_fmt_kind kinds[] = {RYU_FMT_F, RYU_FMT_E, RYU_FMT_G};
  size_t i;

  for (i = 0u; i < sizeof(nan_bits) / sizeof(nan_bits[0]); ++i) {
    double x = double_from_bits(nan_bits[i]);
    size_t k;
    for (k = 0u; k < sizeof(kinds) / sizeof(kinds[0]); ++k) {
      size_t p;
      for (p = 0u; p < sizeof(precisions) / sizeof(precisions[0]); ++p) {
        int upper;
        for (upper = 0; upper <= 1; ++upper) {
          int alt;
          for (alt = 0; alt <= 1; ++alt) {
            int sign_mode;
            for (sign_mode = 0; sign_mode < 3; ++sign_mode) {
              ryu_printf_spec spec;
              char out[64];
              const char* expected = upper ? "NAN" : "nan";
              size_t out_len = 0u;
              ryu_status st;

              spec.kind = kinds[k];
              spec.precision = precisions[p];
              spec.uppercase = upper;
              spec.alternate_form = alt;
              spec.always_sign = (sign_mode == 1);
              spec.space_sign = (sign_mode == 2);

              st = ryu64_to_printf(out, sizeof(out), x, &spec, &out_len);
              if (st != RYU_OK) {
                fprintf(stderr,
                        "printf nan status mismatch bits=0x%016llx status=%d\n",
                        (unsigned long long)nan_bits[i],
                        (int)st);
                return 0;
              }
              if (strcmp(out, expected) != 0 || out_len != 3u) {
                fprintf(stderr,
                        "printf nan text mismatch bits=0x%016llx kind=%d prec=%d upper=%d alt=%d sign_mode=%d out='%s'\n",
                        (unsigned long long)nan_bits[i],
                        (int)spec.kind,
                        spec.precision,
                        spec.uppercase,
                        spec.alternate_form,
                        sign_mode,
                        out);
                return 0;
              }
            }
          }
        }
      }
    }
  }
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

static int check_parse_full_vs_strtod(const char* text, size_t len, const char* tag) {
  ryu64_parse_result p = ryu64_from_decimal_full(text, len);
  char* end = NULL;
  double oracle = strtod(text, &end);

  if (end == NULL || (size_t)(end - text) != len) {
    fprintf(stderr, "strtod %s parse mismatch for '%s'\n", tag, text);
    return 0;
  }
  if (p.parsed_len != len) {
    fprintf(stderr, "full %s parsed_len mismatch for '%s'\n", tag, text);
    return 0;
  }
  if (isinf(oracle)) {
    if (p.status != RYU_PARSE_OVERFLOW || !isinf(p.value) || signbit(p.value) != signbit(oracle)) {
      fprintf(stderr, "full %s overflow mismatch for '%s' status=%d\n", tag, text, (int)p.status);
      return 0;
    }
    return 1;
  }
  if (oracle == 0.0 && text_has_nonzero_digit(text)) {
    if (p.status != RYU_PARSE_UNDERFLOW && bits_from_double(p.value) != bits_from_double(oracle)) {
      fprintf(stderr, "full %s underflow mismatch for '%s' status=%d\n", tag, text, (int)p.status);
      return 0;
    }
    return 1;
  }
  if (!parse_status_success(p.status)) {
    fprintf(stderr, "full %s unexpected status for '%s': %d\n", tag, text, (int)p.status);
    return 0;
  }
  if (bits_from_double(p.value) != bits_from_double(oracle)) {
    fprintf(stderr,
            "full %s value mismatch for '%s': full=0x%016llx oracle=0x%016llx\n",
            tag,
            text,
            (unsigned long long)bits_from_double(p.value),
            (unsigned long long)bits_from_double(oracle));
    return 0;
  }
  return 1;
}

static uint64_t parse_env_u64_or_zero(const char* name) {
  const char* s = getenv(name);
  char* end = NULL;
  unsigned long long v;

  if (s == NULL || s[0] == '\0') {
    return 0u;
  }
  v = strtoull(s, &end, 10);
  if (end == s || (end != NULL && *end != '\0')) {
    return 0u;
  }
  return (uint64_t)v;
}

static int check_subnormal_parse_case(uint64_t bits, const char* tag) {
  char text[128];
  int n;
  double x;

  x = double_from_bits(bits);
  n = snprintf(text, sizeof(text), "%.17e", x);
  if (n <= 0 || (size_t)n >= sizeof(text)) {
    fprintf(stderr, "snprintf subnormal failed for bits=0x%016llx\n", (unsigned long long)bits);
    return 0;
  }
  return check_parse_full_vs_strtod(text, (size_t)n, tag);
}

static int run_parse_full_subnormal_vs_strtod(void) {
  const uint64_t kSubMaxMant = (UINT64_C(1) << 52u) - UINT64_C(1);
  const uint64_t kDenseWindow = UINT64_C(8192);
  const uint64_t kScatterIters = UINT64_C(65536);
  uint64_t exhaustive_limit = parse_env_u64_or_zero("RYU_EXHAUSTIVE_SUBNORMAL_LIMIT");
  uint64_t i;
  uint64_t m = UINT64_C(1);

  for (i = UINT64_C(1); i <= kDenseWindow; ++i) {
    uint64_t lo = i;
    uint64_t hi = kSubMaxMant - (i - UINT64_C(1));
    if (!check_subnormal_parse_case(lo, "subnormal-dense-lo")) {
      return 0;
    }
    if (!check_subnormal_parse_case(lo | (UINT64_C(1) << 63u), "subnormal-dense-lo-neg")) {
      return 0;
    }
    if (hi != lo) {
      if (!check_subnormal_parse_case(hi, "subnormal-dense-hi")) {
        return 0;
      }
      if (!check_subnormal_parse_case(hi | (UINT64_C(1) << 63u), "subnormal-dense-hi-neg")) {
        return 0;
      }
    }
  }

  for (i = 0u; i < kScatterIters; ++i) {
    m = (m * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407)) & kSubMaxMant;
    if (m == 0u) {
      m = UINT64_C(1);
    }
    if (!check_subnormal_parse_case(m, "subnormal-scatter")) {
      return 0;
    }
    if (!check_subnormal_parse_case(m | (UINT64_C(1) << 63u), "subnormal-scatter-neg")) {
      return 0;
    }
  }

  if (exhaustive_limit > kSubMaxMant) {
    exhaustive_limit = kSubMaxMant;
  }
  for (i = UINT64_C(1); i <= exhaustive_limit; ++i) {
    if (!check_subnormal_parse_case(i, "subnormal-exhaustive")) {
      return 0;
    }
    if (!check_subnormal_parse_case(i | (UINT64_C(1) << 63u), "subnormal-exhaustive-neg")) {
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
    if (!check_parse_full_vs_strtod(cases[i], strlen(cases[i]), "boundary")) {
      return 0;
    }
  }
  return 1;
}

static int run_parse_full_long_truncated_vs_strtod(void) {
  char text_a[16384];
  char text_b[16384];
  char text_c[16384];
  char text_d[16384];
  char text_e[16384];
  size_t i = 0u;
  size_t len;

  text_a[i++] = '1';
  while (i < 3000u) {
    text_a[i++] = '0';
  }
  memcpy(text_a + i, "e-2999", 7u);
  len = i + 6u;
  if (!check_parse_full_vs_strtod(text_a, len, "long-trunc-a")) {
    return 0;
  }

  i = 0u;
  text_b[i++] = '1';
  while (i < 3000u) {
    text_b[i++] = '0';
  }
  text_b[i++] = '1';
  memcpy(text_b + i, "e-3000", 7u);
  len = i + 6u;
  if (!check_parse_full_vs_strtod(text_b, len, "long-trunc-b")) {
    return 0;
  }

  i = 0u;
  text_c[i++] = '-';
  text_c[i++] = '1';
  while (i < 3001u) {
    text_c[i++] = '0';
  }
  text_c[i++] = '1';
  memcpy(text_c + i, "e-3001", 7u);
  len = i + 6u;
  if (!check_parse_full_vs_strtod(text_c, len, "long-trunc-c")) {
    return 0;
  }

  i = 0u;
  text_d[i++] = '1';
  while (i < 12000u) {
    text_d[i++] = '0';
  }
  memcpy(text_d + i, "e-11999", 8u);
  len = i + 7u;
  if (!check_parse_full_vs_strtod(text_d, len, "long-trunc-d")) {
    return 0;
  }

  i = 0u;
  text_e[i++] = '1';
  while (i < 12000u) {
    text_e[i++] = '0';
  }
  text_e[i++] = '1';
  memcpy(text_e + i, "e-12000", 8u);
  len = i + 7u;
  if (!check_parse_full_vs_strtod(text_e, len, "long-trunc-e")) {
    return 0;
  }

  return 1;
}

static int run_bankers_rounding_via_printf(void) {
#if defined(RYU_TIER_FULL)
  char ryu_buf[128];
  char libc_buf[128];
  size_t ryu_len = 0u;
  ryu_printf_spec spec;
  size_t i;

  static const struct {
    double value;
    int kind;
    int precision;
  } cases[] = {
    { 1.5, RYU_FMT_E, 0 },
    { 2.5, RYU_FMT_E, 0 },
    { 3.5, RYU_FMT_E, 0 },
    { 4.5, RYU_FMT_E, 0 },
    { 0.5, RYU_FMT_F, 0 },
    { 1.5, RYU_FMT_F, 0 },
    { 2.5, RYU_FMT_F, 0 },
    { 3.5, RYU_FMT_F, 0 },
    { 4.5, RYU_FMT_F, 0 },
    { 1.25, RYU_FMT_F, 1 },
    { 1.35, RYU_FMT_F, 1 },
    { 2.75, RYU_FMT_F, 1 },
    { 0.125, RYU_FMT_F, 2 },
    { 0.375, RYU_FMT_F, 2 },
    { 0.625, RYU_FMT_F, 2 },
    { 0.875, RYU_FMT_F, 2 },
    { 9.5, RYU_FMT_E, 0 },
    { 99.5, RYU_FMT_F, 0 },
    { 999.5, RYU_FMT_F, 0 },
    { 9999.5, RYU_FMT_F, 0 },
  };
  size_t num_cases = sizeof(cases) / sizeof(cases[0]);

  for (i = 0u; i < num_cases; ++i) {
    const char* fmt_str;
    spec.kind = cases[i].kind;
    spec.precision = cases[i].precision;
    spec.uppercase = 0;
    spec.alternate_form = 0;
    spec.always_sign = 0;
    spec.space_sign = 0;

    if (ryu64_to_printf(ryu_buf, sizeof(ryu_buf), cases[i].value, &spec, &ryu_len) != RYU_OK) {
      fprintf(stderr, "bankers_rounding[%zu]: ryu64_to_printf failed for %.17g kind=%d prec=%d\n",
              i, cases[i].value, cases[i].kind, cases[i].precision);
      return 0;
    }

    if (cases[i].kind == RYU_FMT_E) {
      fmt_str = (cases[i].precision >= 0) ? "%.*e" : "%e";
    } else {
      fmt_str = (cases[i].precision >= 0) ? "%.*f" : "%f";
    }
    if (cases[i].precision >= 0) {
      snprintf(libc_buf, sizeof(libc_buf), fmt_str, cases[i].precision, cases[i].value);
    } else {
      snprintf(libc_buf, sizeof(libc_buf), fmt_str, cases[i].value);
    }

    if (strcmp(ryu_buf, libc_buf) != 0) {
      fprintf(stderr, "bankers_rounding[%zu]: mismatch for %.17g kind=%d prec=%d: ryu=\"%s\" libc=\"%s\"\n",
              i, cases[i].value, cases[i].kind, cases[i].precision, ryu_buf, libc_buf);
      return 0;
    }
  }
#endif
  return 1;
}

static int run_full_parser_sig_u64_boundary(void) {
  static const struct {
    const char* text;
    const char* label;
  } cases[] = {
    { "9999999999999999999", "19-nines" },
    { "18446744073709551615", "UINT64_MAX" },
    { "18446744073709551616", "UINT64_MAX+1" },
    { "99999999999999999999", "20-nines" },
    { "12345678901234567890", "20-digit-mixed" },
    { "18446744073709551614", "UINT64_MAX-1" },
    { "10000000000000000000", "10^19" },
    { "100000000000000000000", "10^20" },
    { "1844674407370955161", "UINT64_MAX/10" },
    { "184467440737095516150", "near-UINT64_MAX*10" },
  };
  size_t num_cases = sizeof(cases) / sizeof(cases[0]);
  size_t i;

  for (i = 0u; i < num_cases; ++i) {
    size_t len = strlen(cases[i].text);
    ryu64_parse_result pr = ryu64_from_decimal_full(cases[i].text, len);
    double libc_val;
    char* end = NULL;
    uint64_t ryu_bits, libc_bits;

    libc_val = strtod(cases[i].text, &end);

    if (pr.status != RYU_PARSE_OK && pr.status != RYU_PARSE_INEXACT) {
      fprintf(stderr, "sig_u64_boundary[%s]: full parser rejected \"%s\" status=%d\n",
              cases[i].label, cases[i].text, (int)pr.status);
      return 0;
    }
    if (pr.parsed_len != len) {
      fprintf(stderr, "sig_u64_boundary[%s]: parsed_len=%zu expected=%zu\n",
              cases[i].label, pr.parsed_len, len);
      return 0;
    }

    ryu_bits = bits_from_double(pr.value);
    libc_bits = bits_from_double(libc_val);
    if (ryu_bits != libc_bits) {
      fprintf(stderr, "sig_u64_boundary[%s]: bits mismatch ryu=0x%016llx libc=0x%016llx for \"%s\"\n",
              cases[i].label,
              (unsigned long long)ryu_bits,
              (unsigned long long)libc_bits,
              cases[i].text);
      return 0;
    }
  }
  return 1;
}

static int run_subnormal_formatting(void) {
  static const uint64_t subnormal_bits[] = {
    UINT64_C(0x0000000000000001),
    UINT64_C(0x0000000000000002),
    UINT64_C(0x0000000000000003),
    UINT64_C(0x0000000000000010),
    UINT64_C(0x00000000000000FF),
    UINT64_C(0x0000000000001000),
    UINT64_C(0x0008000000000000),
    UINT64_C(0x000FFFFFFFFFFFFF),
    UINT64_C(0x8000000000000001),
    UINT64_C(0x8000000000000002),
    UINT64_C(0x800FFFFFFFFFFFFF),
    UINT64_C(0x0010000000000000),
  };
  size_t num_bits = sizeof(subnormal_bits) / sizeof(subnormal_bits[0]);
  size_t i;

  for (i = 0u; i < num_bits; ++i) {
    double x = double_from_bits(subnormal_bits[i]);
    char buf[64];
    size_t out_len = 0u;
    double parsed;
    uint64_t parsed_bits;

    if (ryu64_to_shortest(buf, sizeof(buf), x, &out_len) != RYU_OK) {
      fprintf(stderr, "subnormal_fmt[%zu]: shortest failed for bits=0x%016llx\n",
              i, (unsigned long long)subnormal_bits[i]);
      return 0;
    }
    if (out_len == 0u) {
      fprintf(stderr, "subnormal_fmt[%zu]: shortest produced empty output for bits=0x%016llx\n",
              i, (unsigned long long)subnormal_bits[i]);
      return 0;
    }

    parsed = strtod(buf, NULL);
    parsed_bits = bits_from_double(parsed);
    if (parsed_bits != subnormal_bits[i]) {
      fprintf(stderr, "subnormal_fmt[%zu]: roundtrip mismatch bits=0x%016llx text=\"%s\" parsed=0x%016llx\n",
              i, (unsigned long long)subnormal_bits[i], buf, (unsigned long long)parsed_bits);
      return 0;
    }

    if (ryu64_to_9sig(buf, sizeof(buf), x, &out_len) != RYU_OK) {
      fprintf(stderr, "subnormal_fmt[%zu]: 9sig failed for bits=0x%016llx\n",
              i, (unsigned long long)subnormal_bits[i]);
      return 0;
    }
    if (out_len == 0u) {
      fprintf(stderr, "subnormal_fmt[%zu]: 9sig produced empty output for bits=0x%016llx\n",
              i, (unsigned long long)subnormal_bits[i]);
      return 0;
    }

#if defined(RYU_TIER_FULL)
    {
      char ryu_buf[128];
      char libc_buf[128];
      ryu_printf_spec spec;
      spec.kind = RYU_FMT_E;
      spec.precision = 6;
      spec.uppercase = 0;
      spec.alternate_form = 0;
      spec.always_sign = 0;
      spec.space_sign = 0;
      if (ryu64_to_printf(ryu_buf, sizeof(ryu_buf), x, &spec, &out_len) != RYU_OK) {
        fprintf(stderr, "subnormal_fmt[%zu]: printf %%e failed for bits=0x%016llx\n",
                i, (unsigned long long)subnormal_bits[i]);
        return 0;
      }
      snprintf(libc_buf, sizeof(libc_buf), "%.6e", x);
      if (strcmp(ryu_buf, libc_buf) != 0) {
        fprintf(stderr, "subnormal_fmt[%zu]: printf %%e mismatch bits=0x%016llx ryu=\"%s\" libc=\"%s\"\n",
                i, (unsigned long long)subnormal_bits[i], ryu_buf, libc_buf);
        return 0;
      }
    }
#endif
  }
  return 1;
}

static int run_printf_edge_cases(void) {
#if defined(RYU_TIER_FULL)
  char ryu_buf[512];
  char libc_buf[512];
  size_t ryu_len = 0u;
  ryu_printf_spec spec;

  /* %e precision=0 on various values */
  {
    static const double e0_values[] = { 0.0, 1.0, -1.0, 9.5, 1e10, 1e-10 };
    size_t num = sizeof(e0_values) / sizeof(e0_values[0]);
    size_t i;
    for (i = 0u; i < num; ++i) {
      spec.kind = RYU_FMT_E;
      spec.precision = 0;
      spec.uppercase = 0;
      spec.alternate_form = 0;
      spec.always_sign = 0;
      spec.space_sign = 0;
      if (ryu64_to_printf(ryu_buf, sizeof(ryu_buf), e0_values[i], &spec, &ryu_len) != RYU_OK) {
        fprintf(stderr, "printf_edge %%e prec=0: failed for %.17g\n", e0_values[i]);
        return 0;
      }
      snprintf(libc_buf, sizeof(libc_buf), "%.0e", e0_values[i]);
      if (strcmp(ryu_buf, libc_buf) != 0) {
        fprintf(stderr, "printf_edge %%e prec=0: mismatch for %.17g ryu=\"%s\" libc=\"%s\"\n",
                e0_values[i], ryu_buf, libc_buf);
        return 0;
      }
    }
  }

  /* %g trailing zero trimming */
  {
    spec.kind = RYU_FMT_G;
    spec.precision = 6;
    spec.uppercase = 0;
    spec.alternate_form = 0;
    spec.always_sign = 0;
    spec.space_sign = 0;

    if (ryu64_to_printf(ryu_buf, sizeof(ryu_buf), 1.0, &spec, &ryu_len) != RYU_OK) {
      fprintf(stderr, "printf_edge %%g: failed for 1.0\n");
      return 0;
    }
    snprintf(libc_buf, sizeof(libc_buf), "%g", 1.0);
    if (strcmp(ryu_buf, libc_buf) != 0) {
      fprintf(stderr, "printf_edge %%g trim: mismatch for 1.0 ryu=\"%s\" libc=\"%s\"\n", ryu_buf, libc_buf);
      return 0;
    }

    if (ryu64_to_printf(ryu_buf, sizeof(ryu_buf), 1.1, &spec, &ryu_len) != RYU_OK) {
      fprintf(stderr, "printf_edge %%g: failed for 1.1\n");
      return 0;
    }
    snprintf(libc_buf, sizeof(libc_buf), "%g", 1.1);
    if (strcmp(ryu_buf, libc_buf) != 0) {
      fprintf(stderr, "printf_edge %%g trim: mismatch for 1.1 ryu=\"%s\" libc=\"%s\"\n", ryu_buf, libc_buf);
      return 0;
    }
  }

  /* %g alternate form preserves trailing zeros */
  {
    spec.kind = RYU_FMT_G;
    spec.precision = 6;
    spec.uppercase = 0;
    spec.alternate_form = 1;
    spec.always_sign = 0;
    spec.space_sign = 0;

    if (ryu64_to_printf(ryu_buf, sizeof(ryu_buf), 1.0, &spec, &ryu_len) != RYU_OK) {
      fprintf(stderr, "printf_edge %%#g: failed for 1.0\n");
      return 0;
    }
    snprintf(libc_buf, sizeof(libc_buf), "%#g", 1.0);
    if (strcmp(ryu_buf, libc_buf) != 0) {
      fprintf(stderr, "printf_edge %%#g: mismatch for 1.0 ryu=\"%s\" libc=\"%s\"\n", ryu_buf, libc_buf);
      return 0;
    }
  }

  /* %f precision=0 alternate form: trailing dot */
  {
    spec.kind = RYU_FMT_F;
    spec.precision = 0;
    spec.uppercase = 0;
    spec.alternate_form = 1;
    spec.always_sign = 0;
    spec.space_sign = 0;

    if (ryu64_to_printf(ryu_buf, sizeof(ryu_buf), 1.0, &spec, &ryu_len) != RYU_OK) {
      fprintf(stderr, "printf_edge %%#.0f: failed for 1.0\n");
      return 0;
    }
    snprintf(libc_buf, sizeof(libc_buf), "%#.0f", 1.0);
    if (strcmp(ryu_buf, libc_buf) != 0) {
      fprintf(stderr, "printf_edge %%#.0f: mismatch for 1.0 ryu=\"%s\" libc=\"%s\"\n", ryu_buf, libc_buf);
      return 0;
    }
  }

  /* Negative zero formatting */
  {
    double neg_zero = double_from_bits(UINT64_C(0x8000000000000000));

    spec.kind = RYU_FMT_F;
    spec.precision = 0;
    spec.uppercase = 0;
    spec.alternate_form = 0;
    spec.always_sign = 0;
    spec.space_sign = 0;
    if (ryu64_to_printf(ryu_buf, sizeof(ryu_buf), neg_zero, &spec, &ryu_len) != RYU_OK) {
      fprintf(stderr, "printf_edge -0 %%f: failed\n");
      return 0;
    }
    snprintf(libc_buf, sizeof(libc_buf), "%.0f", neg_zero);
    if (strcmp(ryu_buf, libc_buf) != 0) {
      fprintf(stderr, "printf_edge -0 %%.0f: mismatch ryu=\"%s\" libc=\"%s\"\n", ryu_buf, libc_buf);
      return 0;
    }

    spec.kind = RYU_FMT_E;
    spec.precision = 0;
    if (ryu64_to_printf(ryu_buf, sizeof(ryu_buf), neg_zero, &spec, &ryu_len) != RYU_OK) {
      fprintf(stderr, "printf_edge -0 %%e: failed\n");
      return 0;
    }
    snprintf(libc_buf, sizeof(libc_buf), "%.0e", neg_zero);
    if (strcmp(ryu_buf, libc_buf) != 0) {
      fprintf(stderr, "printf_edge -0 %%.0e: mismatch ryu=\"%s\" libc=\"%s\"\n", ryu_buf, libc_buf);
      return 0;
    }
  }

  /* Sign flags: always_sign and space_sign */
  {
    spec.kind = RYU_FMT_F;
    spec.precision = 1;
    spec.uppercase = 0;
    spec.alternate_form = 0;
    spec.always_sign = 1;
    spec.space_sign = 0;
    if (ryu64_to_printf(ryu_buf, sizeof(ryu_buf), 1.0, &spec, &ryu_len) != RYU_OK) {
      fprintf(stderr, "printf_edge +sign: failed\n");
      return 0;
    }
    snprintf(libc_buf, sizeof(libc_buf), "%+.1f", 1.0);
    if (strcmp(ryu_buf, libc_buf) != 0) {
      fprintf(stderr, "printf_edge +sign: mismatch ryu=\"%s\" libc=\"%s\"\n", ryu_buf, libc_buf);
      return 0;
    }

    spec.always_sign = 0;
    spec.space_sign = 1;
    if (ryu64_to_printf(ryu_buf, sizeof(ryu_buf), 1.0, &spec, &ryu_len) != RYU_OK) {
      fprintf(stderr, "printf_edge space_sign: failed\n");
      return 0;
    }
    snprintf(libc_buf, sizeof(libc_buf), "% .1f", 1.0);
    if (strcmp(ryu_buf, libc_buf) != 0) {
      fprintf(stderr, "printf_edge space_sign: mismatch ryu=\"%s\" libc=\"%s\"\n", ryu_buf, libc_buf);
      return 0;
    }
  }
#endif
  return 1;
}

#endif

static int run_bigint_basic_ops(void) {
  static ryu_bigint a, b, c;
  uint64_t val = 0u;

  /* from_u64 round-trips */
  ryu_bigint_from_u64(&a, 0u);
  if (!ryu_bigint_is_zero(&a)) {
    fprintf(stderr, "bigint_basic: from_u64(0) not zero\n");
    return 0;
  }
  if (!ryu_bigint_to_u64(&a, &val) || val != 0u) {
    fprintf(stderr, "bigint_basic: from_u64(0) round-trip failed\n");
    return 0;
  }

  ryu_bigint_from_u64(&a, 1u);
  if (ryu_bigint_is_zero(&a)) {
    fprintf(stderr, "bigint_basic: from_u64(1) is zero\n");
    return 0;
  }
  if (!ryu_bigint_to_u64(&a, &val) || val != 1u) {
    fprintf(stderr, "bigint_basic: from_u64(1) round-trip failed\n");
    return 0;
  }

  ryu_bigint_from_u64(&a, 999999999u);
  if (!ryu_bigint_to_u64(&a, &val) || val != 999999999u) {
    fprintf(stderr, "bigint_basic: from_u64(999999999) round-trip failed\n");
    return 0;
  }

  ryu_bigint_from_u64(&a, UINT64_C(1000000000));
  if (!ryu_bigint_to_u64(&a, &val) || val != UINT64_C(1000000000)) {
    fprintf(stderr, "bigint_basic: from_u64(1000000000) round-trip failed\n");
    return 0;
  }

  ryu_bigint_from_u64(&a, UINT64_MAX);
  if (!ryu_bigint_to_u64(&a, &val) || val != UINT64_MAX) {
    fprintf(stderr, "bigint_basic: from_u64(UINT64_MAX) round-trip failed\n");
    return 0;
  }

  /* cmp tests */
  ryu_bigint_from_u64(&a, 100u);
  ryu_bigint_from_u64(&b, 200u);
  if (ryu_bigint_cmp(&a, &b) >= 0) {
    fprintf(stderr, "bigint_basic: cmp(100,200) should be < 0\n");
    return 0;
  }
  if (ryu_bigint_cmp(&b, &a) <= 0) {
    fprintf(stderr, "bigint_basic: cmp(200,100) should be > 0\n");
    return 0;
  }
  ryu_bigint_from_u64(&b, 100u);
  if (ryu_bigint_cmp(&a, &b) != 0) {
    fprintf(stderr, "bigint_basic: cmp(100,100) should be 0\n");
    return 0;
  }

  /* multi-limb cmp */
  ryu_bigint_from_u64(&a, UINT64_C(1000000000));
  ryu_bigint_from_u64(&b, UINT64_C(1000000001));
  if (ryu_bigint_cmp(&a, &b) >= 0) {
    fprintf(stderr, "bigint_basic: multi-limb cmp failed\n");
    return 0;
  }

  /* add: single-limb carry */
  ryu_bigint_from_u64(&a, 999999999u);
  ryu_bigint_add_small(&a, 1u);
  if (!ryu_bigint_to_u64(&a, &val) || val != UINT64_C(1000000000)) {
    fprintf(stderr, "bigint_basic: 999999999+1 != 1000000000 (got %llu)\n",
            (unsigned long long)val);
    return 0;
  }

  /* add: multi-limb cascade */
  ryu_bigint_from_u64(&a, UINT64_C(999999999999999999));
  ryu_bigint_add_small(&a, 1u);
  if (!ryu_bigint_to_u64(&a, &val) || val != UINT64_C(1000000000000000000)) {
    fprintf(stderr, "bigint_basic: multi-limb add cascade failed (got %llu)\n",
            (unsigned long long)val);
    return 0;
  }

  /* add: two bigints */
  ryu_bigint_from_u64(&a, UINT64_C(1000000000));
  ryu_bigint_from_u64(&b, UINT64_C(2000000000));
  ryu_bigint_add(&a, &b);
  if (!ryu_bigint_to_u64(&a, &val) || val != UINT64_C(3000000000)) {
    fprintf(stderr, "bigint_basic: 1e9+2e9 != 3e9\n");
    return 0;
  }

  /* sub: borrow across limbs */
  ryu_bigint_from_u64(&a, UINT64_C(1000000000));
  ryu_bigint_sub_small(&a, 1u);
  if (!ryu_bigint_to_u64(&a, &val) || val != 999999999u) {
    fprintf(stderr, "bigint_basic: 1e9-1 != 999999999\n");
    return 0;
  }

  /* sub: to zero */
  ryu_bigint_from_u64(&a, 42u);
  ryu_bigint_from_u64(&b, 42u);
  ryu_bigint_sub(&a, &b);
  if (!ryu_bigint_is_zero(&a)) {
    fprintf(stderr, "bigint_basic: 42-42 != 0\n");
    return 0;
  }

  /* mul_small: by 0 */
  ryu_bigint_from_u64(&a, 12345u);
  ryu_bigint_mul_small(&a, 0u);
  if (!ryu_bigint_is_zero(&a)) {
    fprintf(stderr, "bigint_basic: mul_small by 0 not zero\n");
    return 0;
  }

  /* mul_small: by 1 */
  ryu_bigint_from_u64(&a, 12345u);
  ryu_bigint_mul_small(&a, 1u);
  if (!ryu_bigint_to_u64(&a, &val) || val != 12345u) {
    fprintf(stderr, "bigint_basic: mul_small by 1 changed value\n");
    return 0;
  }

  /* mul_small: near-max */
  ryu_bigint_from_u64(&a, 2u);
  ryu_bigint_mul_small(&a, 999999999u);
  if (!ryu_bigint_to_u64(&a, &val) || val != UINT64_C(1999999998)) {
    fprintf(stderr, "bigint_basic: 2*999999999 failed\n");
    return 0;
  }

  /* copy independence */
  ryu_bigint_from_u64(&a, 42u);
  ryu_bigint_copy(&c, &a);
  ryu_bigint_from_u64(&a, 99u);
  if (!ryu_bigint_to_u64(&c, &val) || val != 42u) {
    fprintf(stderr, "bigint_basic: copy not independent\n");
    return 0;
  }

  /* shl_bits: double a value */
  ryu_bigint_from_u64(&a, 100u);
  ryu_bigint_shl_bits(&a, 1u);
  if (!ryu_bigint_to_u64(&a, &val) || val != 200u) {
    fprintf(stderr, "bigint_basic: shl_bits(100,1) != 200\n");
    return 0;
  }

  /* shl_bits: large shift */
  ryu_bigint_from_u64(&a, 1u);
  ryu_bigint_shl_bits(&a, 32u);
  if (!ryu_bigint_to_u64(&a, &val) || val != UINT64_C(4294967296)) {
    fprintf(stderr, "bigint_basic: shl_bits(1,32) != 2^32\n");
    return 0;
  }

  return 1;
}

static int run_bigint_decimal_len(void) {
  static ryu_bigint a;
  char dec_buf[256];
  size_t dec_len = 0u;
  unsigned dlen;

  static const struct {
    uint64_t value;
    unsigned expected_len;
  } cases[] = {
    { 0, 1 },
    { 1, 1 },
    { 9, 1 },
    { 10, 2 },
    { 99, 2 },
    { 100, 3 },
    { 999, 3 },
    { 1000, 4 },
    { 9999, 4 },
    { 10000, 5 },
    { 99999, 5 },
    { 100000, 6 },
    { 999999, 6 },
    { 1000000, 7 },
    { 9999999, 7 },
    { 10000000, 8 },
    { 99999999, 8 },
    { 100000000, 9 },
    { 999999999, 9 },
    { UINT64_C(1000000000), 10 },
    { UINT64_C(9999999999), 10 },
    { UINT64_C(10000000000), 11 },
    { UINT64_C(1000000000000000000), 19 },
    { UINT64_C(9999999999999999999), 19 },
    { UINT64_MAX, 20 },
  };
  size_t num_cases = sizeof(cases) / sizeof(cases[0]);
  size_t i;

  for (i = 0u; i < num_cases; ++i) {
    ryu_bigint_from_u64(&a, cases[i].value);
    dlen = ryu_bigint_decimal_len(&a);
    if (dlen != cases[i].expected_len) {
      fprintf(stderr, "bigint_decimal_len: value=%llu expected=%u got=%u\n",
              (unsigned long long)cases[i].value, cases[i].expected_len, dlen);
      return 0;
    }
    /* cross-check against to_decimal string length */
    if (!ryu_bigint_to_decimal(&a, dec_buf, sizeof(dec_buf), &dec_len)) {
      fprintf(stderr, "bigint_decimal_len: to_decimal failed for %llu\n",
              (unsigned long long)cases[i].value);
      return 0;
    }
    if (dec_len != (size_t)dlen) {
      fprintf(stderr, "bigint_decimal_len: to_decimal len=%zu != decimal_len=%u for %llu\n",
              dec_len, dlen, (unsigned long long)cases[i].value);
      return 0;
    }
  }

  /* Large multi-limb: 10^100 has 101 digits */
  ryu_bigint_from_u64(&a, 1u);
  if (!ryu_bigint_mul_pow10(&a, 100u)) {
    fprintf(stderr, "bigint_decimal_len: mul_pow10(1,100) failed\n");
    return 0;
  }
  dlen = ryu_bigint_decimal_len(&a);
  if (dlen != 101u) {
    fprintf(stderr, "bigint_decimal_len: 10^100 expected 101 digits got %u\n", dlen);
    return 0;
  }

  return 1;
}

static int run_bigint_div_pow10_floor(void) {
  static ryu_bigint n, q, ref;
  uint64_t qval = 0u;
  int rem_zero = 0;

  /* pow10=0: returns value unchanged */
  ryu_bigint_from_u64(&n, 12345u);
  if (!ryu_bigint_div_pow10_floor(&n, 0u, &q, &rem_zero)) {
    fprintf(stderr, "bigint_div_pow10: pow10=0 failed\n");
    return 0;
  }
  if (ryu_bigint_cmp(&q, &n) != 0 || !rem_zero) {
    fprintf(stderr, "bigint_div_pow10: pow10=0 should return value unchanged\n");
    return 0;
  }

  /* Limb-aligned exact: 123456789 * 10^9 / 10^9 = 123456789 */
  ryu_bigint_from_u64(&n, UINT64_C(123456789));
  ryu_bigint_mul_pow10(&n, 9u);
  if (!ryu_bigint_div_pow10_floor(&n, 9u, &q, &rem_zero)) {
    fprintf(stderr, "bigint_div_pow10: limb-aligned failed\n");
    return 0;
  }
  if (!ryu_bigint_to_u64(&q, &qval) || qval != UINT64_C(123456789)) {
    fprintf(stderr, "bigint_div_pow10: limb-aligned got %llu expected 123456789\n",
            (unsigned long long)qval);
    return 0;
  }
  if (!rem_zero) {
    fprintf(stderr, "bigint_div_pow10: limb-aligned remainder should be zero\n");
    return 0;
  }

  /* Non-aligned with nonzero remainder: 123456789 / 10^3 = 123456, remainder != 0 */
  ryu_bigint_from_u64(&n, UINT64_C(123456789));
  if (!ryu_bigint_div_pow10_floor(&n, 3u, &q, &rem_zero)) {
    fprintf(stderr, "bigint_div_pow10: non-aligned failed\n");
    return 0;
  }
  if (!ryu_bigint_to_u64(&q, &qval) || qval != UINT64_C(123456)) {
    fprintf(stderr, "bigint_div_pow10: non-aligned got %llu expected 123456\n",
            (unsigned long long)qval);
    return 0;
  }
  if (rem_zero) {
    fprintf(stderr, "bigint_div_pow10: non-aligned remainder should not be zero\n");
    return 0;
  }

  /* Exact non-aligned: 123000 / 10^3 = 123, remainder = 0 */
  ryu_bigint_from_u64(&n, UINT64_C(123000));
  if (!ryu_bigint_div_pow10_floor(&n, 3u, &q, &rem_zero)) {
    fprintf(stderr, "bigint_div_pow10: exact non-aligned failed\n");
    return 0;
  }
  if (!ryu_bigint_to_u64(&q, &qval) || qval != 123u) {
    fprintf(stderr, "bigint_div_pow10: exact non-aligned got %llu expected 123\n",
            (unsigned long long)qval);
    return 0;
  }
  if (!rem_zero) {
    fprintf(stderr, "bigint_div_pow10: exact non-aligned remainder should be zero\n");
    return 0;
  }

  /* Divisor > value: 99 / 10^3 = 0, remainder != 0 */
  ryu_bigint_from_u64(&n, 99u);
  if (!ryu_bigint_div_pow10_floor(&n, 3u, &q, &rem_zero)) {
    fprintf(stderr, "bigint_div_pow10: divisor>value failed\n");
    return 0;
  }
  if (!ryu_bigint_is_zero(&q)) {
    fprintf(stderr, "bigint_div_pow10: divisor>value quotient should be zero\n");
    return 0;
  }
  if (rem_zero) {
    fprintf(stderr, "bigint_div_pow10: divisor>value remainder should not be zero (99/1000)\n");
    return 0;
  }

  /* Divisor == value: 1000 / 10^3 = 1, remainder = 0 */
  ryu_bigint_from_u64(&n, 1000u);
  if (!ryu_bigint_div_pow10_floor(&n, 3u, &q, &rem_zero)) {
    fprintf(stderr, "bigint_div_pow10: divisor==value failed\n");
    return 0;
  }
  if (!ryu_bigint_to_u64(&q, &qval) || qval != 1u) {
    fprintf(stderr, "bigint_div_pow10: 1000/10^3 got %llu expected 1\n",
            (unsigned long long)qval);
    return 0;
  }
  if (!rem_zero) {
    fprintf(stderr, "bigint_div_pow10: 1000/10^3 remainder should be zero\n");
    return 0;
  }

  /* Consistency check: q * 10^p <= n for a large multi-limb value */
  ryu_bigint_from_u64(&n, UINT64_C(123456789012345));
  ryu_bigint_mul_pow10(&n, 20u);
  ryu_bigint_add_small(&n, 42u);
  if (!ryu_bigint_div_pow10_floor(&n, 18u, &q, &rem_zero)) {
    fprintf(stderr, "bigint_div_pow10: large consistency check failed\n");
    return 0;
  }
  /* Verify q * 10^18 <= n by reconstructing */
  ryu_bigint_copy(&ref, &q);
  ryu_bigint_mul_pow10(&ref, 18u);
  if (ryu_bigint_cmp(&ref, &n) > 0) {
    fprintf(stderr, "bigint_div_pow10: consistency q*10^p > n\n");
    return 0;
  }
  /* Verify (q+1) * 10^18 > n */
  ryu_bigint_add_small(&q, 1u);
  ryu_bigint_copy(&ref, &q);
  ryu_bigint_mul_pow10(&ref, 18u);
  if (ryu_bigint_cmp(&ref, &n) <= 0) {
    fprintf(stderr, "bigint_div_pow10: consistency (q+1)*10^p <= n\n");
    return 0;
  }

  return 1;
}

static int run_bigint_mul_pow5(void) {
  static ryu_bigint a, b;
  uint64_t val = 0u;

  /* 5^13 = 1220703125 — exercises kPow5Small[13] */
  ryu_bigint_from_u64(&a, 1u);
  if (!ryu_bigint_mul_pow5(&a, 13u)) {
    fprintf(stderr, "bigint_mul_pow5: pow5(13) failed\n");
    return 0;
  }
  if (!ryu_bigint_to_u64(&a, &val) || val != UINT64_C(1220703125)) {
    fprintf(stderr, "bigint_mul_pow5: 5^13 got %llu expected 1220703125\n",
            (unsigned long long)val);
    return 0;
  }

  /* 5^14 = 6103515625 — chunk 13 + chunk 1 */
  ryu_bigint_from_u64(&a, 1u);
  if (!ryu_bigint_mul_pow5(&a, 14u)) {
    fprintf(stderr, "bigint_mul_pow5: pow5(14) failed\n");
    return 0;
  }
  if (!ryu_bigint_to_u64(&a, &val) || val != UINT64_C(6103515625)) {
    fprintf(stderr, "bigint_mul_pow5: 5^14 got %llu expected 6103515625\n",
            (unsigned long long)val);
    return 0;
  }

  /* 5^26 = 1490116119384765625 — two chunks of 13 */
  ryu_bigint_from_u64(&a, 1u);
  if (!ryu_bigint_mul_pow5(&a, 26u)) {
    fprintf(stderr, "bigint_mul_pow5: pow5(26) failed\n");
    return 0;
  }
  if (!ryu_bigint_to_u64(&a, &val) || val != UINT64_C(1490116119384765625)) {
    fprintf(stderr, "bigint_mul_pow5: 5^26 got %llu expected 1490116119384765625\n",
            (unsigned long long)val);
    return 0;
  }

  /* 5^27 = 7450580596923828125 — chunks: 13+13+1 */
  ryu_bigint_from_u64(&a, 1u);
  if (!ryu_bigint_mul_pow5(&a, 27u)) {
    fprintf(stderr, "bigint_mul_pow5: pow5(27) failed\n");
    return 0;
  }
  if (!ryu_bigint_to_u64(&a, &val) || val != UINT64_C(7450580596923828125)) {
    fprintf(stderr, "bigint_mul_pow5: 5^27 got %llu expected 7450580596923828125\n",
            (unsigned long long)val);
    return 0;
  }

  /* Cross-check: compute 5^27 via repeated mul_small(5) */
  {
    unsigned j;
    ryu_bigint_from_u64(&b, 1u);
    for (j = 0u; j < 27u; ++j) {
      ryu_bigint_mul_small(&b, 5u);
    }
    if (ryu_bigint_cmp(&a, &b) != 0) {
      fprintf(stderr, "bigint_mul_pow5: cross-check 5^27 mismatch\n");
      return 0;
    }
  }

  /* Large exponent cross-check: 5^100 via mul_pow5 vs repeated mul_small */
  {
    unsigned j;
    ryu_bigint_from_u64(&a, 1u);
    ryu_bigint_mul_pow5(&a, 100u);
    ryu_bigint_from_u64(&b, 1u);
    for (j = 0u; j < 100u; ++j) {
      ryu_bigint_mul_small(&b, 5u);
    }
    if (ryu_bigint_cmp(&a, &b) != 0) {
      fprintf(stderr, "bigint_mul_pow5: cross-check 5^100 mismatch\n");
      return 0;
    }
  }

  return 1;
}

static int run_bigint_to_decimal_and_div_exact(void) {
  static ryu_bigint a;
  char buf[256];
  size_t len = 0u;
  uint64_t val = 0u;

  /* Zero */
  ryu_bigint_zero(&a);
  if (!ryu_bigint_to_decimal(&a, buf, sizeof(buf), &len) || len != 1u || buf[0] != '0') {
    fprintf(stderr, "bigint_to_dec: zero failed\n");
    return 0;
  }

  /* 1 */
  ryu_bigint_from_u64(&a, 1u);
  if (!ryu_bigint_to_decimal(&a, buf, sizeof(buf), &len) || len != 1u || buf[0] != '1') {
    fprintf(stderr, "bigint_to_dec: 1 failed\n");
    return 0;
  }

  /* 999999999 (max single limb) */
  ryu_bigint_from_u64(&a, 999999999u);
  if (!ryu_bigint_to_decimal(&a, buf, sizeof(buf), &len) ||
      strcmp(buf, "999999999") != 0) {
    fprintf(stderr, "bigint_to_dec: 999999999 got \"%s\"\n", buf);
    return 0;
  }

  /* UINT64_MAX */
  ryu_bigint_from_u64(&a, UINT64_MAX);
  if (!ryu_bigint_to_decimal(&a, buf, sizeof(buf), &len) ||
      strcmp(buf, "18446744073709551615") != 0) {
    fprintf(stderr, "bigint_to_dec: UINT64_MAX got \"%s\"\n", buf);
    return 0;
  }

  /* div_small_exact succeeds: 10^9 / 10 = 10^8 */
  ryu_bigint_from_u64(&a, UINT64_C(1000000000));
  if (!ryu_bigint_div_small_exact(&a, 10u)) {
    fprintf(stderr, "bigint_div_exact: 10^9/10 should succeed\n");
    return 0;
  }
  if (!ryu_bigint_to_u64(&a, &val) || val != UINT64_C(100000000)) {
    fprintf(stderr, "bigint_div_exact: 10^9/10 got %llu expected 100000000\n",
            (unsigned long long)val);
    return 0;
  }

  /* div_small_exact fails: (10^9 + 1) / 10 has remainder */
  ryu_bigint_from_u64(&a, UINT64_C(1000000001));
  if (ryu_bigint_div_small_exact(&a, 10u)) {
    fprintf(stderr, "bigint_div_exact: (10^9+1)/10 should fail\n");
    return 0;
  }

  /* Large value: 10^50 = "1" followed by 50 zeros */
  ryu_bigint_from_u64(&a, 1u);
  if (!ryu_bigint_mul_pow10(&a, 50u)) {
    fprintf(stderr, "bigint_to_dec: mul_pow10(1,50) failed\n");
    return 0;
  }
  if (!ryu_bigint_to_decimal(&a, buf, sizeof(buf), &len)) {
    fprintf(stderr, "bigint_to_dec: to_decimal(10^50) failed\n");
    return 0;
  }
  if (len != 51u) {
    fprintf(stderr, "bigint_to_dec: 10^50 len=%zu expected 51\n", len);
    return 0;
  }
  if (buf[0] != '1') {
    fprintf(stderr, "bigint_to_dec: 10^50 first char='%c' expected '1'\n", buf[0]);
    return 0;
  }
  {
    size_t j;
    for (j = 1u; j < 51u; ++j) {
      if (buf[j] != '0') {
        fprintf(stderr, "bigint_to_dec: 10^50 char[%zu]='%c' expected '0'\n", j, buf[j]);
        return 0;
      }
    }
  }

  return 1;
}

static int run_buffer_too_small(void) {
  char big[512];
  char small_buf[512];
  size_t out_len = 0u;
  ryu_status st;

  /* Test pattern: format into big buffer, then try exact-fit and off-by-one */
  {
    static const double values[] = { 1.0, -1.0, 1e100, 0.0 };
    size_t num = sizeof(values) / sizeof(values[0]);
    size_t i;

    for (i = 0u; i < num; ++i) {
      size_t needed;
      st = ryu64_to_shortest(big, sizeof(big), values[i], &out_len);
      if (st != RYU_OK) {
        fprintf(stderr, "buf_too_small: shortest(%.17g) failed in big buffer\n", values[i]);
        return 0;
      }
      needed = out_len + 1u; /* +1 for NUL */

      st = ryu64_to_shortest(small_buf, needed, values[i], &out_len);
      if (st != RYU_OK) {
        fprintf(stderr, "buf_too_small: shortest(%.17g) failed with exact size %zu\n",
                values[i], needed);
        return 0;
      }

      if (needed > 1u) {
        st = ryu64_to_shortest(small_buf, needed - 1u, values[i], &out_len);
        if (st != RYU_BUFFER_TOO_SMALL) {
          fprintf(stderr, "buf_too_small: shortest(%.17g) should fail with size %zu, got status %d\n",
                  values[i], needed - 1u, (int)st);
          return 0;
        }
      }
    }
  }

  /* NaN and Inf */
  {
    double nan_val = double_from_bits(UINT64_C(0x7FF8000000000000));
    double inf_val = double_from_bits(UINT64_C(0x7FF0000000000000));
    double neg_inf = double_from_bits(UINT64_C(0xFFF0000000000000));
    static const double specials[3] = { 0.0, 0.0, 0.0 };
    double spec_vals[3];
    size_t j;

    spec_vals[0] = nan_val;
    spec_vals[1] = inf_val;
    spec_vals[2] = neg_inf;
    (void)specials;

    for (j = 0u; j < 3u; ++j) {
      size_t needed;
      st = ryu64_to_shortest(big, sizeof(big), spec_vals[j], &out_len);
      if (st != RYU_OK) {
        fprintf(stderr, "buf_too_small: shortest(special[%zu]) failed\n", j);
        return 0;
      }
      needed = out_len + 1u;
      if (needed > 1u) {
        st = ryu64_to_shortest(small_buf, needed - 1u, spec_vals[j], &out_len);
        if (st != RYU_BUFFER_TOO_SMALL) {
          fprintf(stderr, "buf_too_small: shortest(special[%zu]) should fail with size %zu\n",
                  j, needed - 1u);
          return 0;
        }
      }
    }
  }

  /* 9sig buffer boundary */
  {
    st = ryu64_to_9sig(big, sizeof(big), 1.23456789e200, &out_len);
    if (st != RYU_OK) {
      fprintf(stderr, "buf_too_small: 9sig failed in big buffer\n");
      return 0;
    }
    if (out_len + 1u > 1u) {
      st = ryu64_to_9sig(small_buf, out_len, 1.23456789e200, &out_len);
      if (st != RYU_BUFFER_TOO_SMALL) {
        fprintf(stderr, "buf_too_small: 9sig should fail with size %zu\n", out_len);
        return 0;
      }
    }
  }

  /* printf buffer boundary */
#if defined(RYU_TIER_FULL)
  {
    ryu_printf_spec pspec;
    pspec.kind = RYU_FMT_E;
    pspec.precision = 6;
    pspec.uppercase = 0;
    pspec.alternate_form = 0;
    pspec.always_sign = 0;
    pspec.space_sign = 0;

    st = ryu64_to_printf(big, sizeof(big), 1.0, &pspec, &out_len);
    if (st != RYU_OK) {
      fprintf(stderr, "buf_too_small: printf %%e failed in big buffer\n");
      return 0;
    }
    if (out_len + 1u > 1u) {
      st = ryu64_to_printf(small_buf, out_len, 1.0, &pspec, &out_len);
      if (st != RYU_BUFFER_TOO_SMALL) {
        fprintf(stderr, "buf_too_small: printf %%e should fail with undersized buffer\n");
        return 0;
      }
    }
  }
#endif

  /* out_cap=0 should not crash */
  st = ryu64_to_shortest(NULL, 0u, 1.0, &out_len);
  if (st != RYU_BUFFER_TOO_SMALL) {
    fprintf(stderr, "buf_too_small: shortest(NULL,0) should return BUFFER_TOO_SMALL, got %d\n",
            (int)st);
    return 0;
  }

  st = ryu64_to_9sig(NULL, 0u, 1.0, &out_len);
  if (st != RYU_BUFFER_TOO_SMALL) {
    fprintf(stderr, "buf_too_small: 9sig(NULL,0) should return BUFFER_TOO_SMALL, got %d\n",
            (int)st);
    return 0;
  }

  return 1;
}

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
  if (!run_bigint_basic_ops()) {
    return 1;
  }
  if (!run_bigint_decimal_len()) {
    return 1;
  }
  if (!run_bigint_div_pow10_floor()) {
    return 1;
  }
  if (!run_bigint_mul_pow5()) {
    return 1;
  }
  if (!run_bigint_to_decimal_and_div_exact()) {
    return 1;
  }
  if (!run_buffer_too_small()) {
    return 1;
  }

#if defined(RYU_ENABLE_LIBC_ORACLE)
  if (!run_roundtrip_edges()) {
    return 1;
  }
  if (!run_roundtrip_random(20000u)) {
    return 1;
  }
  if (!run_shortest_minimality_random(5000u)) {
    return 1;
  }
  if (!run_printf_diff(3000u)) {
    return 1;
  }
  if (!run_printf_nan_sign_policy()) {
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
  if (!run_parse_full_subnormal_vs_strtod()) {
    return 1;
  }
  if (!run_parse_full_long_truncated_vs_strtod()) {
    return 1;
  }
  if (!run_bankers_rounding_via_printf()) {
    return 1;
  }
  if (!run_full_parser_sig_u64_boundary()) {
    return 1;
  }
  if (!run_subnormal_formatting()) {
    return 1;
  }
  if (!run_printf_edge_cases()) {
    return 1;
  }
#endif

  printf("all tests passed\n");
  return 0;
}

#endif
