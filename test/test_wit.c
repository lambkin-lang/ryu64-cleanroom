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
 * Tests for the WIT floats interface implementation.
 * Exercises exports_lambkin_runtime_floats_* functions
 * to verify they correctly bridge to the ryu64 C API.
 */

#include "floats_impl.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

static int g_pass = 0;
static int g_fail = 0;

static void check(int ok, const char *name) {
  if (ok) {
    g_pass++;
  } else {
    g_fail++;
    fprintf(stderr, "FAIL: %s\n", name);
  }
}

static int str_eq(const floats_impl_string_t *s, const char *expected) {
  size_t elen = strlen(expected);
  return s->len == elen && memcmp(s->ptr, expected, elen) == 0;
}

static uint64_t dbl_bits(double x) {
  uint64_t b;
  memcpy(&b, &x, sizeof(x));
  return b;
}

/* --- to-string tests ---------------------------------------------------- */

static void test_to_string(void) {
  floats_impl_string_t ret;

  exports_lambkin_runtime_floats_to_string(0.0, &ret);
  check(str_eq(&ret, "0"), "to_string(0.0)");
  floats_impl_string_free(&ret);

  exports_lambkin_runtime_floats_to_string(-0.0, &ret);
  check(str_eq(&ret, "-0"), "to_string(-0.0)");
  floats_impl_string_free(&ret);

  exports_lambkin_runtime_floats_to_string(3.14, &ret);
  check(str_eq(&ret, "3.14"), "to_string(3.14)");
  floats_impl_string_free(&ret);

  exports_lambkin_runtime_floats_to_string(1e20, &ret);
  check(str_eq(&ret, "1e20"), "to_string(1e20)");
  floats_impl_string_free(&ret);

  exports_lambkin_runtime_floats_to_string(1.0 / 0.0, &ret);
  check(str_eq(&ret, "inf"), "to_string(inf)");
  floats_impl_string_free(&ret);

  exports_lambkin_runtime_floats_to_string(-1.0 / 0.0, &ret);
  check(str_eq(&ret, "-inf"), "to_string(-inf)");
  floats_impl_string_free(&ret);

  exports_lambkin_runtime_floats_to_string(0.0 / 0.0, &ret);
  check(str_eq(&ret, "nan"), "to_string(nan)");
  floats_impl_string_free(&ret);
}

/* --- format-f64 tests --------------------------------------------------- */

static void test_format_f64(void) {
  floats_impl_string_t ret;
  exports_lambkin_runtime_floats_format_error_t err;
  exports_lambkin_runtime_floats_format_options_t opts;
  bool ok;

  /* %g default precision */
  opts.notation = EXPORTS_LAMBKIN_RUNTIME_FLOATS_NOTATION_GENERAL;
  opts.precision.is_some = false;
  opts.width.is_some = false;
  opts.uppercase = false;
  opts.alternate_form = false;
  opts.always_sign = false;
  opts.space_sign = false;
  opts.zero_pad = false;
  opts.left_justify = false;

  ok = exports_lambkin_runtime_floats_format_f64(3.14, &opts, &ret, &err);
  check(ok, "format_f64 %g 3.14 succeeds");
  check(str_eq(&ret, "3.14"), "format_f64 %g 3.14 value");
  floats_impl_string_free(&ret);

  /* %.2f */
  opts.notation = EXPORTS_LAMBKIN_RUNTIME_FLOATS_NOTATION_DECIMAL;
  opts.precision.is_some = true;
  opts.precision.val = 2;

  ok = exports_lambkin_runtime_floats_format_f64(3.14159, &opts, &ret, &err);
  check(ok, "format_f64 %.2f 3.14159 succeeds");
  check(str_eq(&ret, "3.14"), "format_f64 %.2f 3.14159 value");
  floats_impl_string_free(&ret);

  /* %.3e uppercase */
  opts.notation = EXPORTS_LAMBKIN_RUNTIME_FLOATS_NOTATION_SCIENTIFIC;
  opts.precision.is_some = true;
  opts.precision.val = 3;
  opts.uppercase = true;

  ok = exports_lambkin_runtime_floats_format_f64(12345.0, &opts, &ret, &err);
  check(ok, "format_f64 %.3E 12345 succeeds");
  check(str_eq(&ret, "1.234E+04") || str_eq(&ret, "1.235E+04"),
        "format_f64 %.3E 12345 value");
  floats_impl_string_free(&ret);

  /* +sign flag */
  opts.notation = EXPORTS_LAMBKIN_RUNTIME_FLOATS_NOTATION_GENERAL;
  opts.precision.is_some = false;
  opts.uppercase = false;
  opts.always_sign = true;

  ok = exports_lambkin_runtime_floats_format_f64(1.0, &opts, &ret, &err);
  check(ok, "format_f64 %+g 1.0 succeeds");
  check(ret.len > 0 && ret.ptr[0] == '+', "format_f64 %+g 1.0 has plus sign");
  floats_impl_string_free(&ret);

  /* Reset to defaults for width/padding tests. */
  opts.always_sign = false;

  /* --- width: space-padding (right-aligned) --- */
  /* %10.2f of 3.14 => "      3.14" (10 chars, right-aligned) */
  opts.notation = EXPORTS_LAMBKIN_RUNTIME_FLOATS_NOTATION_DECIMAL;
  opts.precision.is_some = true;
  opts.precision.val = 2;
  opts.width.is_some = true;
  opts.width.val = 10;

  ok = exports_lambkin_runtime_floats_format_f64(3.14, &opts, &ret, &err);
  check(ok, "width %10.2f 3.14 succeeds");
  check(ret.len == 10, "width %10.2f 3.14 length is 10");
  check(str_eq(&ret, "      3.14"), "width %10.2f 3.14 value");
  floats_impl_string_free(&ret);

  /* Width smaller than result — no padding. */
  /* %2.2f of 3.14 => "3.14" (no truncation) */
  opts.width.val = 2;

  ok = exports_lambkin_runtime_floats_format_f64(3.14, &opts, &ret, &err);
  check(ok, "width %2.2f 3.14 succeeds");
  check(str_eq(&ret, "3.14"), "width %2.2f 3.14 not truncated");
  floats_impl_string_free(&ret);

  /* --- zero-pad --- */
  /* %010.2f of 3.14 => "0000003.14" */
  opts.width.val = 10;
  opts.zero_pad = true;

  ok = exports_lambkin_runtime_floats_format_f64(3.14, &opts, &ret, &err);
  check(ok, "zero-pad %010.2f 3.14 succeeds");
  check(str_eq(&ret, "0000003.14"), "zero-pad %010.2f 3.14 value");
  floats_impl_string_free(&ret);

  /* Zero-pad with negative: sign then zeros. */
  /* %010.2f of -3.14 => "-000003.14" */
  ok = exports_lambkin_runtime_floats_format_f64(-3.14, &opts, &ret, &err);
  check(ok, "zero-pad %010.2f -3.14 succeeds");
  check(str_eq(&ret, "-000003.14"), "zero-pad %010.2f -3.14 value");
  floats_impl_string_free(&ret);

  /* Zero-pad with +sign: +sign then zeros. */
  opts.always_sign = true;
  ok = exports_lambkin_runtime_floats_format_f64(3.14, &opts, &ret, &err);
  check(ok, "zero-pad %+010.2f 3.14 succeeds");
  check(str_eq(&ret, "+000003.14"), "zero-pad %+010.2f 3.14 value");
  floats_impl_string_free(&ret);
  opts.always_sign = false;

  /* Zero-pad does NOT apply to inf/nan — space-padded instead. */
  ok = exports_lambkin_runtime_floats_format_f64(1.0 / 0.0, &opts, &ret, &err);
  check(ok, "zero-pad %010.2f inf succeeds");
  check(str_eq(&ret, "       inf"), "zero-pad %010.2f inf space-padded");
  floats_impl_string_free(&ret);

  /* --- left-justify --- */
  /* %-10.2f of 3.14 => "3.14      " */
  opts.zero_pad = false;
  opts.left_justify = true;

  ok = exports_lambkin_runtime_floats_format_f64(3.14, &opts, &ret, &err);
  check(ok, "left-justify %-10.2f 3.14 succeeds");
  check(ret.len == 10, "left-justify %-10.2f 3.14 length is 10");
  check(str_eq(&ret, "3.14      "), "left-justify %-10.2f 3.14 value");
  floats_impl_string_free(&ret);

  /* Left-justify overrides zero-pad. */
  opts.zero_pad = true;
  ok = exports_lambkin_runtime_floats_format_f64(3.14, &opts, &ret, &err);
  check(ok, "left-justify overrides zero-pad succeeds");
  check(str_eq(&ret, "3.14      "), "left-justify overrides zero-pad value");
  floats_impl_string_free(&ret);

  /* No width set — width fields are ignored. */
  opts.width.is_some = false;
  opts.zero_pad = true;
  opts.left_justify = true;
  ok = exports_lambkin_runtime_floats_format_f64(3.14, &opts, &ret, &err);
  check(ok, "no width ignores pad flags succeeds");
  check(str_eq(&ret, "3.14"), "no width ignores pad flags value");
  floats_impl_string_free(&ret);
}

/* --- parse-f64 tests ---------------------------------------------------- */

static void test_parse_f64(void) {
  exports_lambkin_runtime_floats_parsed_f64_t ret;
  exports_lambkin_runtime_floats_parse_error_t err;
  floats_impl_string_t text;
  bool ok;

  /* Parse "3.14" */
  floats_impl_string_set(&text, "3.14");
  ok = exports_lambkin_runtime_floats_parse_f64(&text, &ret, &err);
  check(ok, "parse_f64 '3.14' succeeds");
  check(ret.value == 3.14, "parse_f64 '3.14' value");
  check(ret.parsed_length == 4, "parse_f64 '3.14' length");

  /* Parse "inf" */
  floats_impl_string_set(&text, "inf");
  ok = exports_lambkin_runtime_floats_parse_f64(&text, &ret, &err);
  check(ok, "parse_f64 'inf' succeeds");
  check(ret.value == 1.0 / 0.0, "parse_f64 'inf' value");

  /* Parse "-0" */
  floats_impl_string_set(&text, "-0");
  ok = exports_lambkin_runtime_floats_parse_f64(&text, &ret, &err);
  check(ok, "parse_f64 '-0' succeeds");
  check(dbl_bits(ret.value) == dbl_bits(-0.0), "parse_f64 '-0' is negative zero");

  /* Parse invalid */
  floats_impl_string_set(&text, "not_a_number");
  ok = exports_lambkin_runtime_floats_parse_f64(&text, &ret, &err);
  check(!ok, "parse_f64 'not_a_number' fails");
  check(err.tag == EXPORTS_LAMBKIN_RUNTIME_FLOATS_PARSE_ERROR_INVALID,
        "parse_f64 'not_a_number' is INVALID");

  /* Roundtrip: to-string then parse */
  floats_impl_string_t str;
  exports_lambkin_runtime_floats_to_string(2.2250738585072014e-308, &str);
  ok = exports_lambkin_runtime_floats_parse_f64(&str, &ret, &err);
  check(ok, "roundtrip DBL_MIN succeeds");
  check(dbl_bits(ret.value) == dbl_bits(2.2250738585072014e-308),
        "roundtrip DBL_MIN bits match");
  floats_impl_string_free(&str);
}

/* --- hex-float tests ---------------------------------------------------- */

static void test_hex_float(void) {
  floats_impl_string_t ret;
  exports_lambkin_runtime_floats_format_error_t err;
  exports_lambkin_runtime_floats_format_options_t opts;
  bool ok;

  /* Reset opts to defaults, then set notation to hex. */
  opts.notation = EXPORTS_LAMBKIN_RUNTIME_FLOATS_NOTATION_HEX;
  opts.precision.is_some = false;
  opts.width.is_some = false;
  opts.uppercase = false;
  opts.alternate_form = false;
  opts.always_sign = false;
  opts.space_sign = false;
  opts.zero_pad = false;
  opts.left_justify = false;

  /* %a of 0.0 → "0x0p+0" */
  ok = exports_lambkin_runtime_floats_format_f64(0.0, &opts, &ret, &err);
  check(ok, "hex 0.0 succeeds");
  check(str_eq(&ret, "0x0p+0"), "hex 0.0 value");
  floats_impl_string_free(&ret);

  /* %a of -0.0 → "-0x0p+0" */
  ok = exports_lambkin_runtime_floats_format_f64(-0.0, &opts, &ret, &err);
  check(ok, "hex -0.0 succeeds");
  check(str_eq(&ret, "-0x0p+0"), "hex -0.0 value");
  floats_impl_string_free(&ret);

  /* %a of 1.0 → "0x1p+0" */
  ok = exports_lambkin_runtime_floats_format_f64(1.0, &opts, &ret, &err);
  check(ok, "hex 1.0 succeeds");
  check(str_eq(&ret, "0x1p+0"), "hex 1.0 value");
  floats_impl_string_free(&ret);

  /* %a of 1.5 → "0x1.8p+0" */
  ok = exports_lambkin_runtime_floats_format_f64(1.5, &opts, &ret, &err);
  check(ok, "hex 1.5 succeeds");
  check(str_eq(&ret, "0x1.8p+0"), "hex 1.5 value");
  floats_impl_string_free(&ret);

  /* %a of 3.14 → "0x1.91eb851eb851fp+1" */
  ok = exports_lambkin_runtime_floats_format_f64(3.14, &opts, &ret, &err);
  check(ok, "hex 3.14 succeeds");
  check(str_eq(&ret, "0x1.91eb851eb851fp+1"), "hex 3.14 value");
  floats_impl_string_free(&ret);

  /* %a of -2.0 → "-0x1p+1" */
  ok = exports_lambkin_runtime_floats_format_f64(-2.0, &opts, &ret, &err);
  check(ok, "hex -2.0 succeeds");
  check(str_eq(&ret, "-0x1p+1"), "hex -2.0 value");
  floats_impl_string_free(&ret);

  /* %A uppercase → "0X1.8P+0" */
  opts.uppercase = true;
  ok = exports_lambkin_runtime_floats_format_f64(1.5, &opts, &ret, &err);
  check(ok, "hex uppercase 1.5 succeeds");
  check(str_eq(&ret, "0X1.8P+0"), "hex uppercase 1.5 value");
  floats_impl_string_free(&ret);
  opts.uppercase = false;

  /* Explicit precision %.3a of 3.14 → "0x1.91fp+1" (rounded) */
  opts.precision.is_some = true;
  opts.precision.val = 3;
  ok = exports_lambkin_runtime_floats_format_f64(3.14, &opts, &ret, &err);
  check(ok, "hex %.3a 3.14 succeeds");
  check(str_eq(&ret, "0x1.91fp+1"), "hex %.3a 3.14 value");
  floats_impl_string_free(&ret);

  /* %.0a of 1.5 → "0x2p+0" (round up from 0x1.8, tie-break odd) */
  opts.precision.val = 0;
  ok = exports_lambkin_runtime_floats_format_f64(1.5, &opts, &ret, &err);
  check(ok, "hex %.0a 1.5 succeeds");
  check(str_eq(&ret, "0x2p+0"), "hex %.0a 1.5 value");
  floats_impl_string_free(&ret);

  /* %.0a of 1.0 → "0x1p+0" (no fraction, no rounding) */
  ok = exports_lambkin_runtime_floats_format_f64(1.0, &opts, &ret, &err);
  check(ok, "hex %.0a 1.0 succeeds");
  check(str_eq(&ret, "0x1p+0"), "hex %.0a 1.0 value");
  floats_impl_string_free(&ret);

  /* %.0a# alternate form → "0x1.p+0" */
  opts.alternate_form = true;
  ok = exports_lambkin_runtime_floats_format_f64(1.0, &opts, &ret, &err);
  check(ok, "hex %.0a# 1.0 succeeds");
  check(str_eq(&ret, "0x1.p+0"), "hex %.0a# 1.0 value");
  floats_impl_string_free(&ret);
  opts.alternate_form = false;

  /* %a of inf → "inf" */
  opts.precision.is_some = false;
  ok = exports_lambkin_runtime_floats_format_f64(1.0 / 0.0, &opts, &ret, &err);
  check(ok, "hex inf succeeds");
  check(str_eq(&ret, "inf"), "hex inf value");
  floats_impl_string_free(&ret);

  /* %a of nan → "nan" */
  ok = exports_lambkin_runtime_floats_format_f64(0.0 / 0.0, &opts, &ret, &err);
  check(ok, "hex nan succeeds");
  check(str_eq(&ret, "nan"), "hex nan value");
  floats_impl_string_free(&ret);

  /* %+a of 1.0 → "+0x1p+0" */
  opts.always_sign = true;
  ok = exports_lambkin_runtime_floats_format_f64(1.0, &opts, &ret, &err);
  check(ok, "hex +sign 1.0 succeeds");
  check(str_eq(&ret, "+0x1p+0"), "hex +sign 1.0 value");
  floats_impl_string_free(&ret);
  opts.always_sign = false;

  /* Smallest subnormal: 5e-324 = 0x0.0000000000001p-1022 */
  ok = exports_lambkin_runtime_floats_format_f64(5e-324, &opts, &ret, &err);
  check(ok, "hex subnormal succeeds");
  check(str_eq(&ret, "0x0.0000000000001p-1022"), "hex subnormal value");
  floats_impl_string_free(&ret);

  /* Width + hex: %20a of 1.0 → "            0x1p+0" (20 chars, right-padded) */
  opts.width.is_some = true;
  opts.width.val = 20;
  ok = exports_lambkin_runtime_floats_format_f64(1.0, &opts, &ret, &err);
  check(ok, "hex width 20 1.0 succeeds");
  check(ret.len == 20, "hex width 20 1.0 length");
  check(str_eq(&ret, "              0x1p+0"), "hex width 20 1.0 value");
  floats_impl_string_free(&ret);
}

/* --- main --------------------------------------------------------------- */

int main(void) {
  test_to_string();
  test_format_f64();
  test_parse_f64();
  test_hex_float();

  fprintf(stderr, "wit-test: %d passed, %d failed\n", g_pass, g_fail);
  return g_fail > 0 ? 1 : 0;
}
