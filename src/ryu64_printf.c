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

#include "ryu64_internal.h"

#include <string.h>

#if !defined(RYU_TIER_FULL)
ryu_status ryu64_to_printf(
    char* out,
    size_t out_cap,
    double x,
    const ryu_printf_spec* spec,
    size_t* out_len) {
  (void)out;
  (void)out_cap;
  (void)x;
  (void)spec;
  (void)out_len;
  return RYU_UNSUPPORTED;
}
#else

static size_t ryu_u32_to_dec(uint32_t v, char* out) {
  char rev[16];
  size_t n = 0u;
  if (v == 0u) {
    out[0] = '0';
    return 1u;
  }
  while (v != 0u) {
    rev[n++] = (char)('0' + (v % 10u));
    v /= 10u;
  }
  {
    size_t i;
    for (i = 0u; i < n; ++i) {
      out[i] = rev[n - 1u - i];
    }
  }
  return n;
}

static int ryu_append_exponent(
    char* out,
    size_t out_cap,
    size_t* pos,
    int exp10,
    int uppercase) {
  char tmp[16];
  size_t n;
  uint32_t abs_exp;
  if (*pos + 4u >= out_cap) {
    return 0;
  }
  out[(*pos)++] = uppercase ? 'E' : 'e';
  if (exp10 < 0) {
    out[(*pos)++] = '-';
    abs_exp = (uint32_t)(-exp10);
  } else {
    out[(*pos)++] = '+';
    abs_exp = (uint32_t)exp10;
  }

  n = ryu_u32_to_dec(abs_exp, tmp);
  if (n == 1u) {
    out[(*pos)++] = '0';
  }
  if (*pos + n >= out_cap) {
    return 0;
  }
  memcpy(out + *pos, tmp, n);
  *pos += n;
  return 1;
}

static int ryu_emit_scientific_from_rounded(
    char* out,
    size_t out_cap,
    int negative,
    const ryu_bigint* rounded,
    unsigned precision,
    int exp10,
    int uppercase,
    int alternate_form,
    int always_sign,
    int space_sign,
    size_t* out_len) {
  char dec[RYU_LOCAL_BUF_CAP];
  size_t len = 0u;
  size_t pos = 0u;
  unsigned frac = precision;
  size_t copy_tail;

  if (!ryu_bigint_to_decimal(rounded, dec, sizeof(dec), &len)) {
    return 0;
  }
  if (len == 0u) {
    return 0;
  }

  if (!ryu_write_sign(out, out_cap, negative, always_sign, space_sign, &pos)) {
    return 0;
  }

  if (pos + 2u >= out_cap) {
    return 0;
  }

  out[pos++] = dec[0];
  if (frac > 0u || alternate_form) {
    out[pos++] = '.';
    copy_tail = (len > 1u) ? (len - 1u) : 0u;
    if (copy_tail > (size_t)frac) {
      copy_tail = (size_t)frac;
    }
    if (pos + copy_tail + (size_t)frac + 8u >= out_cap) {
      return 0;
    }
    if (copy_tail != 0u) {
      memcpy(out + pos, dec + 1u, copy_tail);
      pos += copy_tail;
      frac -= (unsigned)copy_tail;
    }
    while (frac != 0u) {
      out[pos++] = '0';
      frac -= 1u;
    }
  }

  if (!ryu_append_exponent(out, out_cap, &pos, exp10, uppercase)) {
    return 0;
  }

  if (pos >= out_cap) {
    return 0;
  }
  out[pos] = '\0';
  if (out_len != NULL) {
    *out_len = pos;
  }
  return 1;
}

static int ryu_trim_for_g(char* out, size_t* out_len) {
  size_t len = *out_len;
  size_t e_pos = len;
  size_t dot_pos = len;
  size_t i;

  for (i = 0u; i < len; ++i) {
    if (out[i] == 'e' || out[i] == 'E') {
      e_pos = i;
      break;
    }
  }
  for (i = 0u; i < e_pos; ++i) {
    if (out[i] == '.') {
      dot_pos = i;
      break;
    }
  }
  if (dot_pos == len) {
    return 1;
  }

  i = e_pos;
  while (i > dot_pos + 1u && out[i - 1u] == '0') {
    i -= 1u;
  }
  if (i == dot_pos + 1u) {
    i -= 1u;
  }

  if (e_pos < len) {
    size_t tail = len - e_pos;
    memmove(out + i, out + e_pos, tail + 1u);
    len = i + tail;
  } else {
    out[i] = '\0';
    len = i;
  }

  *out_len = len;
  return 1;
}

static ryu_status ryu_printf_e(
    char* out,
    size_t out_cap,
    const ryu_decimal_exact* exact,
    int negative,
    int precision,
    int uppercase,
    int alternate_form,
    int always_sign,
    int space_sign,
    size_t* out_len) {
  ryu_bigint rounded;
  int exp10 = 0;

  if (precision < 0) {
    precision = 6;
  }

  if (ryu_bigint_is_zero(&exact->digits)) {
    ryu_bigint_from_u64(&rounded, 0u);
    if (!ryu_emit_scientific_from_rounded(
            out,
            out_cap,
            negative,
            &rounded,
            (unsigned)precision,
            0,
            uppercase,
            alternate_form,
            always_sign,
            space_sign,
            out_len)) {
      return RYU_BUFFER_TOO_SMALL;
    }
    return RYU_OK;
  }

  if (!ryu_round_exact_to_significant(exact, (unsigned)(precision + 1), &rounded, &exp10)) {
    return RYU_UNSUPPORTED;
  }

  if (!ryu_emit_scientific_from_rounded(
          out,
          out_cap,
          negative,
          &rounded,
          (unsigned)precision,
          exp10,
          uppercase,
          alternate_form,
          always_sign,
          space_sign,
          out_len)) {
    return RYU_BUFFER_TOO_SMALL;
  }

  return RYU_OK;
}

static ryu_status ryu_printf_f(
    char* out,
    size_t out_cap,
    const ryu_decimal_exact* exact,
    int negative,
    int precision,
    int alternate_form,
    int always_sign,
    int space_sign,
    size_t* out_len) {
  ryu_bigint rounded;

  if (precision < 0) {
    precision = 6;
  }

  if (!ryu_round_exact_to_fractional(exact, (unsigned)precision, &rounded)) {
    return RYU_UNSUPPORTED;
  }

  if (!ryu_emit_fixed_from_scaled(
          out,
          out_cap,
          negative,
          &rounded,
          (unsigned)precision,
          alternate_form,
          always_sign,
          space_sign,
          out_len)) {
    return RYU_BUFFER_TOO_SMALL;
  }
  return RYU_OK;
}

static ryu_status ryu_printf_g(
    char* out,
    size_t out_cap,
    const ryu_decimal_exact* exact,
    int negative,
    int precision,
    int uppercase,
    int alternate_form,
    int always_sign,
    int space_sign,
    size_t* out_len) {
  ryu_bigint rounded_sig;
  int exp10 = 0;
  int use_scientific;
  int f_precision;
  ryu_status st;

  if (precision < 0) {
    precision = 6;
  }
  if (precision == 0) {
    precision = 1;
  }

  if (ryu_bigint_is_zero(&exact->digits)) {
    exp10 = 0;
  } else {
    if (!ryu_round_exact_to_significant(exact, (unsigned)precision, &rounded_sig, &exp10)) {
      return RYU_UNSUPPORTED;
    }
  }

  use_scientific = (exp10 < -4 || exp10 >= precision);
  if (use_scientific) {
    st = ryu_printf_e(
        out,
        out_cap,
        exact,
        negative,
        precision - 1,
        uppercase,
        alternate_form,
        always_sign,
        space_sign,
        out_len);
  } else {
    f_precision = precision - (exp10 + 1);
    if (f_precision < 0) {
      f_precision = 0;
    }
    st = ryu_printf_f(
        out,
        out_cap,
        exact,
        negative,
        f_precision,
        alternate_form,
        always_sign,
        space_sign,
        out_len);
  }
  if (st != RYU_OK) {
    return st;
  }

  if (!alternate_form) {
    if (!ryu_trim_for_g(out, out_len)) {
      return RYU_UNSUPPORTED;
    }
  }
  return RYU_OK;
}

ryu_status ryu64_to_printf(
    char* out,
    size_t out_cap,
    double x,
    const ryu_printf_spec* spec,
    size_t* out_len) {
  ryu_fp64 fp;
  uint64_t abs_bits;
  ryu_decimal_exact exact;

  if (out == NULL || out_cap == 0u) {
    return RYU_BUFFER_TOO_SMALL;
  }
  if (spec == NULL) {
    return RYU_INVALID;
  }
  if (spec->precision < -1) {
    return RYU_INVALID;
  }
  if (spec->precision > RYU_MAX_PRINTF_PRECISION) {
    return RYU_UNSUPPORTED;
  }

  ryu_decode_fp64(x, &fp);
  abs_bits = fp.bits & UINT64_C(0x7fffffffffffffff);

  if (fp.is_nan) {
    if (!ryu_write_special(
            out,
            out_cap,
            fp.sign,
            0,
            spec->uppercase,
            spec->always_sign,
            spec->space_sign,
            out_len)) {
      return RYU_BUFFER_TOO_SMALL;
    }
    return RYU_OK;
  }

  if (fp.is_inf) {
    if (!ryu_write_special(
            out,
            out_cap,
            fp.sign,
            1,
            spec->uppercase,
            spec->always_sign,
            spec->space_sign,
            out_len)) {
      return RYU_BUFFER_TOO_SMALL;
    }
    return RYU_OK;
  }

  if (!ryu_exact_decimal_from_bits(abs_bits, &exact)) {
    return RYU_UNSUPPORTED;
  }

  switch (spec->kind) {
    case RYU_FMT_F:
      return ryu_printf_f(
          out,
          out_cap,
          &exact,
          fp.sign,
          spec->precision,
          spec->alternate_form,
          spec->always_sign,
          spec->space_sign,
          out_len);
    case RYU_FMT_E:
      return ryu_printf_e(
          out,
          out_cap,
          &exact,
          fp.sign,
          spec->precision,
          spec->uppercase,
          spec->alternate_form,
          spec->always_sign,
          spec->space_sign,
          out_len);
    case RYU_FMT_G:
      return ryu_printf_g(
          out,
          out_cap,
          &exact,
          fp.sign,
          spec->precision,
          spec->uppercase,
          spec->alternate_form,
          spec->always_sign,
          spec->space_sign,
          out_len);
    default:
      return RYU_INVALID;
  }
}
#endif
