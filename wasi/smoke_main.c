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

/*
 * WASI smoke test: exercises ryu64 formatting and parsing APIs, then
 * verifies roundtrip correctness.  Runs under wasmtime; uses WASI
 * fd_write for all output (no libc printf).
 */

#include "wasi_io.h"
#include <string.h>
#include "ryu64.h"

static int ryu_parse_success(ryu_parse_status st) {
  return st == RYU_PARSE_OK || st == RYU_PARSE_INEXACT;
}

static int g_total = 0;
static int g_pass = 0;

static void check(int ok, const char* detail, size_t dlen) {
  g_total++;
  if (ok) {
    g_pass++;
    write_stdout("PASS ", 5);
  } else {
    write_stdout("FAIL ", 5);
  }
  write_line(detail, dlen);
}

static uint64_t dbl_bits(double x) {
  uint64_t b;
  memcpy(&b, &x, sizeof(x));
  return b;
}

static void emit_int(int v) {
  char buf[12];
  int neg = 0;
  if (v < 0) {
    neg = 1;
    v = -v;
  }
  int n = 0;
  if (v == 0) {
    buf[n++] = '0';
  } else {
    while (v > 0) {
      buf[n++] = (char)('0' + v % 10);
      v /= 10;
    }
  }
  if (neg) write_stdout("-", 1);
  for (int i = n - 1; i >= 0; --i) write_stdout(&buf[i], 1);
}

/* Numeric test values (no NaN/Inf here; tested separately). */
static const double kValues[] = {
    0.0,
    -0.0,
    1.0,
    -1.0,
    0.1,
    3.14159265358979,
    1e100,
    -1e-100,
    5e-324,                  /* smallest subnormal */
    2.2250738585072014e-308, /* DBL_MIN (smallest normal) */
    1.7976931348623157e308,  /* DBL_MAX */
};
#define NVALS ((int)(sizeof(kValues) / sizeof(kValues[0])))

int main(void) {
  char buf[512];
  size_t len;
  ryu_status st;
  int i;

  write_line("=== ryu64 WASI smoke test ===", 29);

  /* --- shortest formatting --- */
  write_line("--- shortest ---", 16);
  for (i = 0; i < NVALS; i++) {
    st = ryu64_to_shortest(buf, sizeof(buf), kValues[i], &len);
    check(st == RYU_OK, buf, len);
  }
  /* specials */
  st = ryu64_to_shortest(buf, sizeof(buf), __builtin_inf(), &len);
  check(st == RYU_OK, buf, len);
  st = ryu64_to_shortest(buf, sizeof(buf), -__builtin_inf(), &len);
  check(st == RYU_OK, buf, len);
  st = ryu64_to_shortest(buf, sizeof(buf), __builtin_nan(""), &len);
  check(st == RYU_OK, buf, len);

  /* --- printf %g (default precision) --- */
  write_line("--- printf %g ---", 17);
  {
    ryu_printf_spec spec;
    spec.kind = RYU_FMT_G;
    spec.precision = -1;
    spec.uppercase = 0;
    spec.alternate_form = 0;
    spec.always_sign = 0;
    spec.space_sign = 0;
    for (i = 0; i < NVALS; i++) {
      st = ryu64_to_printf(buf, sizeof(buf), kValues[i], &spec, &len);
      check(st == RYU_OK, buf, len);
    }
  }

  /* --- printf %.17e --- */
  write_line("--- printf %.17e ---", 20);
  {
    ryu_printf_spec spec;
    spec.kind = RYU_FMT_E;
    spec.precision = 17;
    spec.uppercase = 0;
    spec.alternate_form = 0;
    spec.always_sign = 0;
    spec.space_sign = 0;
    for (i = 0; i < NVALS; i++) {
      st = ryu64_to_printf(buf, sizeof(buf), kValues[i], &spec, &len);
      check(st == RYU_OK, buf, len);
    }
  }

  /* --- printf %f --- */
  write_line("--- printf %f ---", 17);
  {
    ryu_printf_spec spec;
    spec.kind = RYU_FMT_F;
    spec.precision = -1;
    spec.uppercase = 0;
    spec.alternate_form = 0;
    spec.always_sign = 0;
    spec.space_sign = 0;
    for (i = 0; i < 7; i++) { /* skip extreme values for %f */
      st = ryu64_to_printf(buf, sizeof(buf), kValues[i], &spec, &len);
      check(st == RYU_OK, buf, len);
    }
  }

  /* --- roundtrip: shortest -> parse full -> compare bits --- */
  write_line("--- roundtrip ---", 17);
  for (i = 0; i < NVALS; i++) {
    st = ryu64_to_shortest(buf, sizeof(buf), kValues[i], &len);
    if (st != RYU_OK) {
      check(0, "format failed", 13);
      continue;
    }
    ryu64_parse_result pr = ryu64_from_decimal_full(buf, len);
    int ok = ryu_parse_success(pr.status) &&
             (dbl_bits(kValues[i]) == dbl_bits(pr.value));
    check(ok, buf, len);
  }
  /* roundtrip inf */
  {
    double inf = __builtin_inf();
    st = ryu64_to_shortest(buf, sizeof(buf), inf, &len);
    if (st == RYU_OK) {
      ryu64_parse_result pr = ryu64_from_decimal_full(buf, len);
      check(ryu_parse_success(pr.status) && dbl_bits(inf) == dbl_bits(pr.value),
            buf, len);
    } else {
      check(0, "inf format failed", 17);
    }
  }
  /* roundtrip -inf */
  {
    double ninf = -__builtin_inf();
    st = ryu64_to_shortest(buf, sizeof(buf), ninf, &len);
    if (st == RYU_OK) {
      ryu64_parse_result pr = ryu64_from_decimal_full(buf, len);
      check(ryu_parse_success(pr.status) && dbl_bits(ninf) == dbl_bits(pr.value),
            buf, len);
    } else {
      check(0, "-inf format failed", 18);
    }
  }

  /* --- parse known strings --- */
  write_line("--- parse ---", 13);
  {
    struct {
      const char* s;
      int expect_ok;
    } cases[] = {
        {"0", 1},
        {"1", 1},
        {"-1", 1},
        {"3.14", 1},
        {"1e100", 1},
        {"5e-324", 1},
        {"1.7976931348623157e308", 1},
        {"2.2250738585072014e-308", 1},
        {"inf", 1},
        {"-inf", 1},
        {"nan", 1},
        {"not_a_number", 0},
        {"", 0},
    };
    int ncases = (int)(sizeof(cases) / sizeof(cases[0]));
    for (i = 0; i < ncases; i++) {
      size_t slen = strlen(cases[i].s);
      ryu64_parse_result pr = ryu64_from_decimal_full(cases[i].s, slen);
      int ok;
      if (cases[i].expect_ok) {
        ok = ryu_parse_success(pr.status);
      } else {
        ok = !ryu_parse_success(pr.status);
      }
      check(ok, cases[i].s, slen);
    }
  }

  /* --- summary --- */
  write_stdout("\n", 1);
  emit_int(g_pass);
  write_stdout("/", 1);
  emit_int(g_total);
  write_line(" tests passed", 13);

  return (g_pass == g_total) ? 0 : 1;
}
