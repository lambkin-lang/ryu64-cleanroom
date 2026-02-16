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
  ok = exports_lambkin_runtime_floats_parse_f64(&text, NULL, &ret, &err);
  check(ok, "parse_f64 '3.14' succeeds");
  check(ret.value == 3.14, "parse_f64 '3.14' value");
  check(ret.parsed_length == 4, "parse_f64 '3.14' length");

  /* Parse "inf" */
  floats_impl_string_set(&text, "inf");
  ok = exports_lambkin_runtime_floats_parse_f64(&text, NULL, &ret, &err);
  check(ok, "parse_f64 'inf' succeeds");
  check(ret.value == 1.0 / 0.0, "parse_f64 'inf' value");

  /* Parse "-0" */
  floats_impl_string_set(&text, "-0");
  ok = exports_lambkin_runtime_floats_parse_f64(&text, NULL, &ret, &err);
  check(ok, "parse_f64 '-0' succeeds");
  check(dbl_bits(ret.value) == dbl_bits(-0.0), "parse_f64 '-0' is negative zero");

  /* Parse invalid */
  floats_impl_string_set(&text, "not_a_number");
  ok = exports_lambkin_runtime_floats_parse_f64(&text, NULL, &ret, &err);
  check(!ok, "parse_f64 'not_a_number' fails");
  check(err.tag == EXPORTS_LAMBKIN_RUNTIME_FLOATS_PARSE_ERROR_INVALID,
        "parse_f64 'not_a_number' is INVALID");

  /* Roundtrip: to-string then parse */
  floats_impl_string_t str;
  exports_lambkin_runtime_floats_to_string(2.2250738585072014e-308, &str);
  ok = exports_lambkin_runtime_floats_parse_f64(&str, NULL, &ret, &err);
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

/* --- locale-format tests ------------------------------------------------ */

/*
 * Helper: build a numeric-locale with repeat-N grouping.
 */
static exports_lambkin_runtime_floats_numeric_locale_t
make_locale(uint8_t dp_tag, uint8_t sep_tag, uint8_t group_every) {
  exports_lambkin_runtime_floats_numeric_locale_t loc;
  loc.decimal_point.tag = dp_tag;
  loc.thousands_sep.tag = sep_tag;
  loc.grouping.rule.tag = EXPORTS_LAMBKIN_RUNTIME_FLOATS_GROUPING_RULE_REPEAT;
  loc.grouping.rule.val.repeat = group_every;
  loc.grouping.require_separator = false;
  return loc;
}

static void test_locale_format(void) {
  floats_impl_string_t ret;
  exports_lambkin_runtime_floats_format_error_t err;
  exports_lambkin_runtime_floats_format_options_t opts;
  bool ok;

  /* Base options: %.2f */
  opts.notation = EXPORTS_LAMBKIN_RUNTIME_FLOATS_NOTATION_DECIMAL;
  opts.precision.is_some = true;
  opts.precision.val = 2;
  opts.width.is_some = false;
  opts.uppercase = false;
  opts.alternate_form = false;
  opts.always_sign = false;
  opts.space_sign = false;
  opts.zero_pad = false;
  opts.left_justify = false;
  opts.locale.is_some = false;

  /* --- Decimal point replacement --- */

  /* German style: comma decimal point, dot thousands sep */
  opts.locale.is_some = true;
  opts.locale.val = make_locale(
      EXPORTS_LAMBKIN_RUNTIME_FLOATS_DECIMAL_POINT_COMMA,
      EXPORTS_LAMBKIN_RUNTIME_FLOATS_THOUSANDS_SEP_DOT, 3);

  ok = exports_lambkin_runtime_floats_format_f64(3.14, &opts, &ret, &err);
  check(ok, "locale de 3.14 succeeds");
  check(str_eq(&ret, "3,14"), "locale de 3.14 value");
  floats_impl_string_free(&ret);

  /* Comma decimal with large number: 1234567.89 → 1.234.567,89 */
  ok = exports_lambkin_runtime_floats_format_f64(1234567.89, &opts, &ret, &err);
  check(ok, "locale de 1234567.89 succeeds");
  check(str_eq(&ret, "1.234.567,89"), "locale de 1234567.89 value");
  floats_impl_string_free(&ret);

  /* --- US-style: dot decimal, comma thousands --- */
  opts.locale.val = make_locale(
      EXPORTS_LAMBKIN_RUNTIME_FLOATS_DECIMAL_POINT_DOT,
      EXPORTS_LAMBKIN_RUNTIME_FLOATS_THOUSANDS_SEP_COMMA, 3);

  ok = exports_lambkin_runtime_floats_format_f64(1234567.89, &opts, &ret, &err);
  check(ok, "locale us 1234567.89 succeeds");
  check(str_eq(&ret, "1,234,567.89"), "locale us 1234567.89 value");
  floats_impl_string_free(&ret);

  /* Small number (< 1000): no separator with require-separator=false */
  ok = exports_lambkin_runtime_floats_format_f64(123.45, &opts, &ret, &err);
  check(ok, "locale us 123.45 succeeds");
  check(str_eq(&ret, "123.45"), "locale us 123.45 no grouping");
  floats_impl_string_free(&ret);

  /* require-separator=true: force separator even with one group */
  opts.locale.val.grouping.require_separator = true;
  ok = exports_lambkin_runtime_floats_format_f64(123.45, &opts, &ret, &err);
  check(ok, "locale us require-sep 123.45 succeeds");
  /* 123 is one full group of 3, no second group → still no sep inserted
   * (grouping fires only when there are digits beyond the first group). */
  check(str_eq(&ret, "123.45"), "locale us require-sep 123.45 value");
  floats_impl_string_free(&ret);

  /* 1234 with require-separator=true → "1,234" (1 separator) */
  opts.precision.val = 0;
  ok = exports_lambkin_runtime_floats_format_f64(1234.0, &opts, &ret, &err);
  check(ok, "locale us 1234 succeeds");
  check(str_eq(&ret, "1,234"), "locale us 1234 grouped");
  floats_impl_string_free(&ret);
  opts.precision.val = 2;
  opts.locale.val.grouping.require_separator = false;

  /* --- Indian grouping: first-then-repeat(3, 2) --- */
  opts.locale.val.decimal_point.tag =
      EXPORTS_LAMBKIN_RUNTIME_FLOATS_DECIMAL_POINT_DOT;
  opts.locale.val.thousands_sep.tag =
      EXPORTS_LAMBKIN_RUNTIME_FLOATS_THOUSANDS_SEP_COMMA;
  opts.locale.val.grouping.rule.tag =
      EXPORTS_LAMBKIN_RUNTIME_FLOATS_GROUPING_RULE_FIRST_THEN_REPEAT;
  opts.locale.val.grouping.rule.val.first_then_repeat.first = 3;
  opts.locale.val.grouping.rule.val.first_then_repeat.next = 2;

  /* 12345678.00 → 1,23,45,678.00 */
  ok = exports_lambkin_runtime_floats_format_f64(12345678.0, &opts, &ret, &err);
  check(ok, "locale indian 12345678 succeeds");
  check(str_eq(&ret, "1,23,45,678.00"), "locale indian 12345678 value");
  floats_impl_string_free(&ret);

  /* --- Explicit grouping: [3] with repeat-last --- */
  uint8_t groups3[] = {3};
  opts.locale.val.grouping.rule.tag =
      EXPORTS_LAMBKIN_RUNTIME_FLOATS_GROUPING_RULE_EXPLICIT;
  opts.locale.val.grouping.rule.val.explicit_.groups.ptr = groups3;
  opts.locale.val.grouping.rule.val.explicit_.groups.len = 1;
  opts.locale.val.grouping.rule.val.explicit_.tail =
      EXPORTS_LAMBKIN_RUNTIME_FLOATS_GROUPING_TAIL_REPEAT_LAST;

  ok = exports_lambkin_runtime_floats_format_f64(1234567.0, &opts, &ret, &err);
  check(ok, "locale explicit repeat-last 1234567 succeeds");
  check(str_eq(&ret, "1,234,567.00"), "locale explicit repeat-last 1234567 value");
  floats_impl_string_free(&ret);

  /* --- Explicit grouping: [3] with stop --- */
  opts.locale.val.grouping.rule.val.explicit_.tail =
      EXPORTS_LAMBKIN_RUNTIME_FLOATS_GROUPING_TAIL_STOP;

  /* 1234567 with stop → only one separator: "1234,567.00" */
  ok = exports_lambkin_runtime_floats_format_f64(1234567.0, &opts, &ret, &err);
  check(ok, "locale explicit stop 1234567 succeeds");
  check(str_eq(&ret, "1234,567.00"), "locale explicit stop 1234567 value");
  floats_impl_string_free(&ret);

  /* --- Negative number with grouping --- */
  opts.locale.val = make_locale(
      EXPORTS_LAMBKIN_RUNTIME_FLOATS_DECIMAL_POINT_DOT,
      EXPORTS_LAMBKIN_RUNTIME_FLOATS_THOUSANDS_SEP_COMMA, 3);

  ok = exports_lambkin_runtime_floats_format_f64(-1234567.89, &opts, &ret, &err);
  check(ok, "locale negative grouped succeeds");
  check(str_eq(&ret, "-1,234,567.89"), "locale negative grouped value");
  floats_impl_string_free(&ret);

  /* --- Scientific notation: no grouping expected (single integer digit) --- */
  opts.notation = EXPORTS_LAMBKIN_RUNTIME_FLOATS_NOTATION_SCIENTIFIC;
  ok = exports_lambkin_runtime_floats_format_f64(1234567.89, &opts, &ret, &err);
  check(ok, "locale scientific succeeds");
  check(str_eq(&ret, "1.23e+06"), "locale scientific value");
  floats_impl_string_free(&ret);

  /* --- Special values with locale: inf/nan unchanged --- */
  ok = exports_lambkin_runtime_floats_format_f64(1.0 / 0.0, &opts, &ret, &err);
  check(ok, "locale inf succeeds");
  check(str_eq(&ret, "inf"), "locale inf unchanged");
  floats_impl_string_free(&ret);

  ok = exports_lambkin_runtime_floats_format_f64(0.0 / 0.0, &opts, &ret, &err);
  check(ok, "locale nan succeeds");
  check(str_eq(&ret, "nan"), "locale nan unchanged");
  floats_impl_string_free(&ret);

  /* --- Underscore separator (Python-style) --- */
  opts.notation = EXPORTS_LAMBKIN_RUNTIME_FLOATS_NOTATION_DECIMAL;
  opts.locale.val = make_locale(
      EXPORTS_LAMBKIN_RUNTIME_FLOATS_DECIMAL_POINT_DOT,
      EXPORTS_LAMBKIN_RUNTIME_FLOATS_THOUSANDS_SEP_UNDERSCORE, 3);

  ok = exports_lambkin_runtime_floats_format_f64(1234567.89, &opts, &ret, &err);
  check(ok, "locale underscore 1234567.89 succeeds");
  check(str_eq(&ret, "1_234_567.89"), "locale underscore 1234567.89 value");
  floats_impl_string_free(&ret);

  /* --- Width + locale: width applies after locale transformation --- */
  opts.locale.val = make_locale(
      EXPORTS_LAMBKIN_RUNTIME_FLOATS_DECIMAL_POINT_DOT,
      EXPORTS_LAMBKIN_RUNTIME_FLOATS_THOUSANDS_SEP_COMMA, 3);
  opts.width.is_some = true;
  opts.width.val = 20;

  /* "1,234,567.89" is 12 chars → padded to 20 with spaces */
  ok = exports_lambkin_runtime_floats_format_f64(1234567.89, &opts, &ret, &err);
  check(ok, "locale + width succeeds");
  check(ret.len == 20, "locale + width length is 20");
  check(str_eq(&ret, "        1,234,567.89"), "locale + width value");
  floats_impl_string_free(&ret);

  /* --- Zero: locale with grouping, no integer digits to group --- */
  opts.width.is_some = false;
  ok = exports_lambkin_runtime_floats_format_f64(0.0, &opts, &ret, &err);
  check(ok, "locale zero succeeds");
  check(str_eq(&ret, "0.00"), "locale zero value");
  floats_impl_string_free(&ret);
}

/* --- locale-aware parse tests ------------------------------------------- */

static void test_locale_parse(void) {
  exports_lambkin_runtime_floats_parsed_f64_t ret;
  exports_lambkin_runtime_floats_parse_error_t err;
  floats_impl_string_t text;
  bool ok;

  /* --- No locale (NULL): same as C-locale --- */
  floats_impl_string_set(&text, "3.14");
  ok = exports_lambkin_runtime_floats_parse_f64(&text, NULL, &ret, &err);
  check(ok, "locale-parse NULL succeeds");
  check(ret.value == 3.14, "locale-parse NULL value");
  check(ret.parsed_length == 4, "locale-parse NULL length");

  /* --- German locale: comma decimal, dot separator --- */
  exports_lambkin_runtime_floats_numeric_locale_t de_loc = make_locale(
      EXPORTS_LAMBKIN_RUNTIME_FLOATS_DECIMAL_POINT_COMMA,
      EXPORTS_LAMBKIN_RUNTIME_FLOATS_THOUSANDS_SEP_DOT, 3);

  /* Simple decimal point replacement: "3,14" → 3.14 */
  floats_impl_string_set(&text, "3,14");
  ok = exports_lambkin_runtime_floats_parse_f64(&text, &de_loc, &ret, &err);
  check(ok, "locale-parse de '3,14' succeeds");
  check(ret.value == 3.14, "locale-parse de '3,14' value");
  check(ret.parsed_length == 4, "locale-parse de '3,14' length");

  /* Thousands separators + decimal: "1.234.567,89" → 1234567.89 */
  floats_impl_string_set(&text, "1.234.567,89");
  ok = exports_lambkin_runtime_floats_parse_f64(&text, &de_loc, &ret, &err);
  check(ok, "locale-parse de '1.234.567,89' succeeds");
  check(ret.value == 1234567.89, "locale-parse de '1.234.567,89' value");
  check(ret.parsed_length == 12, "locale-parse de '1.234.567,89' length");

  /* Negative with German locale: "-1.234,56" → -1234.56 */
  floats_impl_string_set(&text, "-1.234,56");
  ok = exports_lambkin_runtime_floats_parse_f64(&text, &de_loc, &ret, &err);
  check(ok, "locale-parse de negative succeeds");
  check(ret.value == -1234.56, "locale-parse de negative value");
  check(ret.parsed_length == 9, "locale-parse de negative length");

  /* --- US locale: dot decimal, comma separator --- */
  exports_lambkin_runtime_floats_numeric_locale_t us_loc = make_locale(
      EXPORTS_LAMBKIN_RUNTIME_FLOATS_DECIMAL_POINT_DOT,
      EXPORTS_LAMBKIN_RUNTIME_FLOATS_THOUSANDS_SEP_COMMA, 3);

  /* "1,234,567.89" → 1234567.89 */
  floats_impl_string_set(&text, "1,234,567.89");
  ok = exports_lambkin_runtime_floats_parse_f64(&text, &us_loc, &ret, &err);
  check(ok, "locale-parse us '1,234,567.89' succeeds");
  check(ret.value == 1234567.89, "locale-parse us '1,234,567.89' value");
  check(ret.parsed_length == 12, "locale-parse us '1,234,567.89' length");

  /* "1,234" (integer only) → 1234 */
  floats_impl_string_set(&text, "1,234");
  ok = exports_lambkin_runtime_floats_parse_f64(&text, &us_loc, &ret, &err);
  check(ok, "locale-parse us '1,234' succeeds");
  check(ret.value == 1234.0, "locale-parse us '1,234' value");
  check(ret.parsed_length == 5, "locale-parse us '1,234' length");

  /* Exponent with separator: "1,234e10" → 1234e10 */
  floats_impl_string_set(&text, "1,234e10");
  ok = exports_lambkin_runtime_floats_parse_f64(&text, &us_loc, &ret, &err);
  check(ok, "locale-parse us exponent succeeds");
  check(ret.value == 1234e10, "locale-parse us exponent value");
  check(ret.parsed_length == 8, "locale-parse us exponent length");

  /* --- Underscore separator (Python/Rust style) --- */
  exports_lambkin_runtime_floats_numeric_locale_t us_loc2 = make_locale(
      EXPORTS_LAMBKIN_RUNTIME_FLOATS_DECIMAL_POINT_DOT,
      EXPORTS_LAMBKIN_RUNTIME_FLOATS_THOUSANDS_SEP_UNDERSCORE, 3);

  floats_impl_string_set(&text, "1_234_567.89");
  ok = exports_lambkin_runtime_floats_parse_f64(&text, &us_loc2, &ret, &err);
  check(ok, "locale-parse underscore succeeds");
  check(ret.value == 1234567.89, "locale-parse underscore value");
  check(ret.parsed_length == 12, "locale-parse underscore length");

  /* --- Multi-byte separator: narrow no-break space U+202F (3 bytes) --- */
  exports_lambkin_runtime_floats_numeric_locale_t nbsp_loc = make_locale(
      EXPORTS_LAMBKIN_RUNTIME_FLOATS_DECIMAL_POINT_DOT,
      EXPORTS_LAMBKIN_RUNTIME_FLOATS_THOUSANDS_SEP_NARROW_NBSP, 3);

  /* "1\xe2\x80\xaf234.89" = "1<NNBSP>234.89" → 1234.89 */
  floats_impl_string_set(&text, "1\xe2\x80\xaf" "234.89");
  ok = exports_lambkin_runtime_floats_parse_f64(&text, &nbsp_loc, &ret, &err);
  check(ok, "locale-parse narrow-nbsp succeeds");
  check(ret.value == 1234.89, "locale-parse narrow-nbsp value");
  /* 1 digit + 3 sep bytes + 3 digits + 1 dot + 2 frac = 10 */
  check(ret.parsed_length == 10, "locale-parse narrow-nbsp length");

  /* --- Multi-byte decimal point: middle dot U+00B7 (2 bytes) --- */
  exports_lambkin_runtime_floats_numeric_locale_t mdot_loc = make_locale(
      EXPORTS_LAMBKIN_RUNTIME_FLOATS_DECIMAL_POINT_MIDDLE_DOT,
      EXPORTS_LAMBKIN_RUNTIME_FLOATS_THOUSANDS_SEP_NONE, 3);

  /* "3\xc2\xb714" = "3·14" → 3.14 */
  floats_impl_string_set(&text, "3\xc2\xb7" "14");
  ok = exports_lambkin_runtime_floats_parse_f64(&text, &mdot_loc, &ret, &err);
  check(ok, "locale-parse middle-dot succeeds");
  check(ret.value == 3.14, "locale-parse middle-dot value");
  /* 1 digit + 2 dp bytes + 2 frac = 5 */
  check(ret.parsed_length == 5, "locale-parse middle-dot length");

  /* --- Special values pass through with any locale --- */
  floats_impl_string_set(&text, "inf");
  ok = exports_lambkin_runtime_floats_parse_f64(&text, &de_loc, &ret, &err);
  check(ok, "locale-parse inf succeeds");
  check(ret.value == 1.0 / 0.0, "locale-parse inf value");
  check(ret.parsed_length == 3, "locale-parse inf length");

  floats_impl_string_set(&text, "nan");
  ok = exports_lambkin_runtime_floats_parse_f64(&text, &de_loc, &ret, &err);
  check(ok, "locale-parse nan succeeds");
  check(ret.parsed_length == 3, "locale-parse nan length");

  floats_impl_string_set(&text, "-infinity");
  ok = exports_lambkin_runtime_floats_parse_f64(&text, &de_loc, &ret, &err);
  check(ok, "locale-parse -infinity succeeds");
  check(ret.value == -1.0 / 0.0, "locale-parse -infinity value");
  check(ret.parsed_length == 9, "locale-parse -infinity length");

  /* --- Error: consecutive separators --- */
  floats_impl_string_set(&text, "1,,234");
  ok = exports_lambkin_runtime_floats_parse_f64(&text, &us_loc, &ret, &err);
  check(!ok, "locale-parse consecutive seps fails");
  check(err.tag == EXPORTS_LAMBKIN_RUNTIME_FLOATS_PARSE_ERROR_INVALID,
        "locale-parse consecutive seps is INVALID");

  /* --- Error: leading separator --- */
  floats_impl_string_set(&text, ",1234");
  ok = exports_lambkin_runtime_floats_parse_f64(&text, &us_loc, &ret, &err);
  check(!ok, "locale-parse leading sep fails");

  /* --- Roundtrip: format with locale then parse with same locale --- */
  {
    floats_impl_string_t formatted;
    exports_lambkin_runtime_floats_format_error_t ferr;
    exports_lambkin_runtime_floats_format_options_t opts;
    opts.notation = EXPORTS_LAMBKIN_RUNTIME_FLOATS_NOTATION_DECIMAL;
    opts.precision.is_some = true;
    opts.precision.val = 2;
    opts.width.is_some = false;
    opts.uppercase = false;
    opts.alternate_form = false;
    opts.always_sign = false;
    opts.space_sign = false;
    opts.zero_pad = false;
    opts.left_justify = false;
    opts.locale.is_some = true;
    opts.locale.val = de_loc;

    ok = exports_lambkin_runtime_floats_format_f64(1234567.89, &opts,
                                                    &formatted, &ferr);
    check(ok, "roundtrip-locale format succeeds");
    check(str_eq(&formatted, "1.234.567,89"), "roundtrip-locale formatted");

    /* Now parse it back with the same German locale. */
    ok = exports_lambkin_runtime_floats_parse_f64(&formatted, &de_loc,
                                                   &ret, &err);
    check(ok, "roundtrip-locale parse succeeds");
    check(ret.value == 1234567.89, "roundtrip-locale value");
    floats_impl_string_free(&formatted);
  }

  /* --- C-locale fast path: dot decimal + no separator → skip normalization --- */
  exports_lambkin_runtime_floats_numeric_locale_t c_loc = make_locale(
      EXPORTS_LAMBKIN_RUNTIME_FLOATS_DECIMAL_POINT_DOT,
      EXPORTS_LAMBKIN_RUNTIME_FLOATS_THOUSANDS_SEP_NONE, 3);
  floats_impl_string_set(&text, "42.5");
  ok = exports_lambkin_runtime_floats_parse_f64(&text, &c_loc, &ret, &err);
  check(ok, "locale-parse c-locale fast path succeeds");
  check(ret.value == 42.5, "locale-parse c-locale fast path value");
  check(ret.parsed_length == 4, "locale-parse c-locale fast path length");
}

/* --- main --------------------------------------------------------------- */

int main(void) {
  test_to_string();
  test_format_f64();
  test_parse_f64();
  test_hex_float();
  test_locale_format();
  test_locale_parse();

  fprintf(stderr, "wit-test: %d passed, %d failed\n", g_pass, g_fail);
  return g_fail > 0 ? 1 : 0;
}
