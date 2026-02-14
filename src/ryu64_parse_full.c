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

#include "ryu64_parse_internal.h"

#if defined(RYU64_ENABLE_PARSE_BIGINT)
#include "ryu64_internal.h"

typedef struct {
  int negative;
  int special; /* 0=none, 1=inf, 2=nan */
  int saw_any_digit;
  int saw_nonzero;
  int truncated;
  long long sig_digits;
  long long exp10;
  size_t parsed_len;
  ryu_bigint sig;
} ryu_full_parsed;

static ryu64_parse_result ryu_parse_overflow_result(int negative, size_t parsed_len) {
  uint64_t bits = UINT64_C(0x7ff0000000000000);
  if (negative) {
    bits |= UINT64_C(1) << 63u;
  }
  return ryu_parse_result_make(RYU_PARSE_OVERFLOW, ryu_double_from_bits_local(bits), parsed_len);
}

static ryu64_parse_result ryu_parse_underflow_result(int negative, size_t parsed_len) {
  uint64_t bits = negative ? UINT64_C(0x8000000000000000) : UINT64_C(0);
  return ryu_parse_result_make(RYU_PARSE_UNDERFLOW, ryu_double_from_bits_local(bits), parsed_len);
}

static ryu64_parse_result ryu_parse_zero_result(int negative, size_t parsed_len) {
  uint64_t bits = negative ? UINT64_C(0x8000000000000000) : UINT64_C(0);
  return ryu_parse_result_make(RYU_PARSE_OK, ryu_double_from_bits_local(bits), parsed_len);
}

static int ryu_parse_append_digit(ryu_full_parsed* p, unsigned digit) {
  if (!p->truncated) {
    if (!ryu_bigint_mul_small(&p->sig, 10u)) {
      p->truncated = 1;
    } else if (!ryu_bigint_add_small(&p->sig, (uint32_t)digit)) {
      p->truncated = 1;
    }
  }
  return 1;
}

static ryu_parse_status ryu_parse_full_decimal_lex(const char* s, size_t n, ryu_full_parsed* out) {
  size_t i = 0u;
  size_t end_pos;

  out->negative = 0;
  out->special = 0;
  out->saw_any_digit = 0;
  out->saw_nonzero = 0;
  out->truncated = 0;
  out->sig_digits = 0;
  out->exp10 = 0;
  out->parsed_len = 0u;
  ryu_bigint_zero(&out->sig);

  while (i < n && ryu_ascii_isspace(s[i])) {
    i += 1u;
  }
  if (i == n) {
    return RYU_PARSE_INVALID;
  }

  if (s[i] == '+' || s[i] == '-') {
    out->negative = (s[i] == '-');
    i += 1u;
  }

  if (i < n && ryu_ascii_lower(s[i]) == 'i') {
    size_t p = 0u;
    if (ryu_ascii_match_ci(s, n, i, "infinity", &p) || ryu_ascii_match_ci(s, n, i, "inf", &p)) {
      out->special = 1;
      out->parsed_len = p;
      return RYU_PARSE_OK;
    }
  }

  if (i < n && ryu_ascii_lower(s[i]) == 'n') {
    size_t p = 0u;
    if (ryu_ascii_match_ci(s, n, i, "nan", &p)) {
      if (p < n && s[p] == '(') {
        size_t j = p + 1u;
        while (j < n) {
          char c = s[j];
          int ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
          if (!ok) {
            break;
          }
          j += 1u;
        }
        if (j < n && s[j] == ')') {
          p = j + 1u;
        }
      }
      out->special = 2;
      out->parsed_len = p;
      return RYU_PARSE_OK;
    }
  }

  while (i < n && ryu_ascii_isdigit(s[i])) {
    unsigned d = (unsigned)(s[i] - '0');
    out->saw_any_digit = 1;
    if (d != 0u || out->saw_nonzero) {
      out->saw_nonzero = 1;
      out->sig_digits += 1;
      ryu_parse_append_digit(out, d);
    }
    i += 1u;
  }

  if (i < n && s[i] == '.') {
    i += 1u;
    while (i < n && ryu_ascii_isdigit(s[i])) {
      unsigned d = (unsigned)(s[i] - '0');
      out->saw_any_digit = 1;
      out->exp10 -= 1;
      if (d != 0u || out->saw_nonzero) {
        out->saw_nonzero = 1;
        out->sig_digits += 1;
        ryu_parse_append_digit(out, d);
      }
      i += 1u;
    }
  }

  if (!out->saw_any_digit) {
    return RYU_PARSE_INVALID;
  }

  end_pos = i;
  if (i < n && (s[i] == 'e' || s[i] == 'E')) {
    size_t j = i + 1u;
    int exp_neg = 0;
    int have_exp_digit = 0;
    int exp_part = 0;

    if (j < n && (s[j] == '+' || s[j] == '-')) {
      exp_neg = (s[j] == '-');
      j += 1u;
    }

    while (j < n && ryu_ascii_isdigit(s[j])) {
      have_exp_digit = 1;
      if (exp_part < 100000000) {
        exp_part = exp_part * 10 + (int)(s[j] - '0');
      }
      j += 1u;
    }

    if (have_exp_digit) {
      if (exp_neg) {
        out->exp10 -= (long long)exp_part;
      } else {
        out->exp10 += (long long)exp_part;
      }
      end_pos = j;
    }
  }

  out->parsed_len = end_pos;
  if (!out->saw_nonzero) {
    ryu_bigint_zero(&out->sig);
  }
  return RYU_PARSE_OK;
}

static int ryu_cmp_ratio_pow2(const ryu_bigint* num, const ryu_bigint* den, int q) {
  ryu_bigint tmp;
  if (q >= 0) {
    ryu_bigint_copy(&tmp, den);
    if (!ryu_bigint_shl_bits(&tmp, (unsigned)q)) {
      return -1;
    }
    return ryu_bigint_cmp(num, &tmp);
  }
  ryu_bigint_copy(&tmp, num);
  if (!ryu_bigint_shl_bits(&tmp, (unsigned)(-q))) {
    return 1;
  }
  return ryu_bigint_cmp(&tmp, den);
}

static int ryu_floor_log2_ratio(const ryu_bigint* num, const ryu_bigint* den, int* out_q) {
  const int kMaxQ = 32768;
  int cmp0 = ryu_bigint_cmp(num, den);
  if (cmp0 >= 0) {
    int low = 0;
    int high = 1;
    while (high < kMaxQ && ryu_cmp_ratio_pow2(num, den, high) >= 0) {
      low = high;
      high <<= 1;
    }
    while (low + 1 < high) {
      int mid = low + (high - low) / 2;
      if (ryu_cmp_ratio_pow2(num, den, mid) >= 0) {
        low = mid;
      } else {
        high = mid;
      }
    }
    *out_q = low;
    return 1;
  }

  {
    int prev = 0;
    int high = 1;
    while (high < kMaxQ && ryu_cmp_ratio_pow2(num, den, -high) < 0) {
      prev = high;
      high <<= 1;
    }
    if (high >= kMaxQ && ryu_cmp_ratio_pow2(num, den, -high) < 0) {
      *out_q = -high;
      return 1;
    }
    {
      int left = prev + 1;
      int right = high;
      while (left < right) {
        int mid = left + (right - left) / 2;
        if (ryu_cmp_ratio_pow2(num, den, -mid) >= 0) {
          right = mid;
        } else {
          left = mid + 1;
        }
      }
      *out_q = -left;
    }
  }
  return 1;
}

static int ryu_round_rational_shift_to_u64(
    const ryu_bigint* num,
    const ryu_bigint* den,
    unsigned shift,
    uint64_t* out,
    int* out_inexact) {
  ryu_bigint scaled;
  ryu_bigint rem;
  ryu_bigint tmp;
  ryu_bigint rem2;
  uint64_t value = 0u;
  int q;
  int cmp_half;
  int exact;

  ryu_bigint_copy(&scaled, num);
  if (!ryu_bigint_shl_bits(&scaled, shift)) {
    return 0;
  }

  if (!ryu_floor_log2_ratio(&scaled, den, &q)) {
    return 0;
  }

  if (q >= 64) {
    return 0;
  }

  ryu_bigint_copy(&rem, &scaled);
  if (q >= 0) {
    int bit;
    for (bit = q; bit >= 0; --bit) {
      ryu_bigint_copy(&tmp, den);
      if (!ryu_bigint_shl_bits(&tmp, (unsigned)bit)) {
        continue;
      }
      if (ryu_bigint_cmp(&rem, &tmp) >= 0) {
        if (!ryu_bigint_sub(&rem, &tmp)) {
          return 0;
        }
        value |= (UINT64_C(1) << (unsigned)bit);
      }
    }
  }

  exact = ryu_bigint_is_zero(&rem);
  ryu_bigint_copy(&rem2, &rem);
  if (!ryu_bigint_mul_small(&rem2, 2u)) {
    return 0;
  }
  cmp_half = ryu_bigint_cmp(&rem2, den);
  if (cmp_half > 0 || (cmp_half == 0 && (value & UINT64_C(1)) != 0u)) {
    if (value == UINT64_MAX) {
      return 0;
    }
    value += UINT64_C(1);
  }

  *out = value;
  *out_inexact = (!exact) || (cmp_half == 0);
  return 1;
}

static ryu_parse_status ryu_convert_ratio_to_double(
    int negative,
    const ryu_bigint* num,
    const ryu_bigint* den,
    double* out_value) {
  int q;

  if (!ryu_floor_log2_ratio(num, den, &q)) {
    return RYU_PARSE_OUT_OF_RANGE;
  }

  if (q > 1023) {
    return RYU_PARSE_OVERFLOW;
  }

  if (q < -1022) {
    uint64_t sub = 0u;
    int inexact = 0;
    uint64_t bits;
    if (!ryu_round_rational_shift_to_u64(num, den, 1074u, &sub, &inexact)) {
      return RYU_PARSE_OUT_OF_RANGE;
    }
    if (sub == 0u) {
      bits = negative ? UINT64_C(0x8000000000000000) : UINT64_C(0);
      *out_value = ryu_double_from_bits_local(bits);
      return RYU_PARSE_UNDERFLOW;
    }
    if (sub >= (UINT64_C(1) << 52u)) {
      bits = (UINT64_C(1) << 52u) | (sub - (UINT64_C(1) << 52u));
    } else {
      bits = sub;
    }
    if (negative) {
      bits |= UINT64_C(1) << 63u;
    }
    *out_value = ryu_double_from_bits_local(bits);
    return inexact ? RYU_PARSE_INEXACT : RYU_PARSE_OK;
  }

  {
    ryu_bigint num_norm;
    ryu_bigint den_norm;
    ryu_bigint rem;
    uint64_t frac = 0u;
    uint64_t mant;
    int guard = 0;
    int round_bit = 0;
    int sticky;
    uint64_t bits;
    int i;

    if (q >= 0) {
      ryu_bigint_copy(&num_norm, num);
      ryu_bigint_copy(&den_norm, den);
      if (!ryu_bigint_shl_bits(&den_norm, (unsigned)q)) {
        return RYU_PARSE_OUT_OF_RANGE;
      }
    } else {
      ryu_bigint_copy(&num_norm, num);
      if (!ryu_bigint_shl_bits(&num_norm, (unsigned)(-q))) {
        return RYU_PARSE_OUT_OF_RANGE;
      }
      ryu_bigint_copy(&den_norm, den);
    }

    if (ryu_bigint_cmp(&num_norm, &den_norm) < 0) {
      return RYU_PARSE_OUT_OF_RANGE;
    }

    ryu_bigint_copy(&rem, &num_norm);
    if (!ryu_bigint_sub(&rem, &den_norm)) {
      return RYU_PARSE_OUT_OF_RANGE;
    }

    for (i = 0; i < 52; ++i) {
      if (!ryu_bigint_mul_small(&rem, 2u)) {
        return RYU_PARSE_OUT_OF_RANGE;
      }
      frac <<= 1u;
      if (ryu_bigint_cmp(&rem, &den_norm) >= 0) {
        if (!ryu_bigint_sub(&rem, &den_norm)) {
          return RYU_PARSE_OUT_OF_RANGE;
        }
        frac |= UINT64_C(1);
      }
    }

    if (!ryu_bigint_mul_small(&rem, 2u)) {
      return RYU_PARSE_OUT_OF_RANGE;
    }
    if (ryu_bigint_cmp(&rem, &den_norm) >= 0) {
      if (!ryu_bigint_sub(&rem, &den_norm)) {
        return RYU_PARSE_OUT_OF_RANGE;
      }
      guard = 1;
    }

    if (!ryu_bigint_mul_small(&rem, 2u)) {
      return RYU_PARSE_OUT_OF_RANGE;
    }
    if (ryu_bigint_cmp(&rem, &den_norm) >= 0) {
      if (!ryu_bigint_sub(&rem, &den_norm)) {
        return RYU_PARSE_OUT_OF_RANGE;
      }
      round_bit = 1;
    }

    sticky = !ryu_bigint_is_zero(&rem);

    mant = (UINT64_C(1) << 52u) | frac;
    if (guard && (round_bit || sticky || ((mant & UINT64_C(1)) != 0u))) {
      mant += UINT64_C(1);
    }
    if (mant == (UINT64_C(1) << 53u)) {
      mant >>= 1u;
      q += 1;
    }

    if (q > 1023) {
      return RYU_PARSE_OVERFLOW;
    }

    bits = ((uint64_t)(q + 1023) << 52u) | (mant & ((UINT64_C(1) << 52u) - UINT64_C(1)));
    if (negative) {
      bits |= UINT64_C(1) << 63u;
    }
    *out_value = ryu_double_from_bits_local(bits);

    return (guard || round_bit || sticky) ? RYU_PARSE_INEXACT : RYU_PARSE_OK;
  }
}

ryu64_parse_result ryu64_from_decimal_full(const char* s, size_t n) {
  ryu64_parse_result tiny_fast;
  ryu_full_parsed p;
  ryu_parse_status st;
  long long dec_exp10;
  ryu_bigint num;
  ryu_bigint den;
  double value = 0.0;

  if (s == NULL) {
    return ryu_parse_result_make(RYU_PARSE_INVALID, 0.0, 0u);
  }

  tiny_fast = ryu64_from_decimal_tiny(s, n);
  if (tiny_fast.status == RYU_PARSE_OK || tiny_fast.status == RYU_PARSE_INEXACT) {
    size_t i = 0u;
    while (i < n && ryu_ascii_isspace(s[i])) {
      i += 1u;
    }
    if (i < n && (s[i] == '+' || s[i] == '-')) {
      i += 1u;
    }
    if (i < n) {
      char c = ryu_ascii_lower(s[i]);
      if (c != 'n' && c != 'i') {
        return tiny_fast;
      }
    }
  }

  st = ryu_parse_full_decimal_lex(s, n, &p);
  if (st != RYU_PARSE_OK) {
    return ryu_parse_result_make(RYU_PARSE_INVALID, 0.0, 0u);
  }

  if (p.special == 1) {
    uint64_t bits = UINT64_C(0x7ff0000000000000);
    if (p.negative) {
      bits |= UINT64_C(1) << 63u;
    }
    return ryu_parse_result_make(RYU_PARSE_OK, ryu_double_from_bits_local(bits), p.parsed_len);
  }
  if (p.special == 2) {
    {
      uint64_t bits = UINT64_C(0x7ff8000000000000);
      if (p.negative) {
        bits |= UINT64_C(1) << 63u;
      }
      return ryu_parse_result_make(RYU_PARSE_OK, ryu_double_from_bits_local(bits), p.parsed_len);
    }
  }

  if (!p.saw_nonzero) {
    return ryu_parse_zero_result(p.negative, p.parsed_len);
  }

  dec_exp10 = (p.sig_digits - 1) + p.exp10;
  if (dec_exp10 > 309) {
    return ryu_parse_overflow_result(p.negative, p.parsed_len);
  }
  if (dec_exp10 < -400) {
    return ryu_parse_underflow_result(p.negative, p.parsed_len);
  }

  if (p.truncated) {
    return ryu_parse_result_make(RYU_PARSE_OUT_OF_RANGE, 0.0, p.parsed_len);
  }

  ryu_bigint_copy(&num, &p.sig);
  ryu_bigint_from_u64(&den, UINT64_C(1));

  if (p.exp10 >= 0) {
    if (p.exp10 > 1000000) {
      return ryu_parse_overflow_result(p.negative, p.parsed_len);
    }
    if (!ryu_bigint_mul_pow10(&num, (unsigned)p.exp10)) {
      return ryu_parse_overflow_result(p.negative, p.parsed_len);
    }
  } else {
    long long abs_exp10 = -p.exp10;
    if (abs_exp10 > 1000000) {
      return ryu_parse_underflow_result(p.negative, p.parsed_len);
    }
    if (!ryu_bigint_mul_pow10(&den, (unsigned)abs_exp10)) {
      if (dec_exp10 < -350) {
        return ryu_parse_underflow_result(p.negative, p.parsed_len);
      }
      return ryu_parse_result_make(RYU_PARSE_OUT_OF_RANGE, 0.0, p.parsed_len);
    }
  }

  st = ryu_convert_ratio_to_double(p.negative, &num, &den, &value);
  if (st == RYU_PARSE_OVERFLOW) {
    return ryu_parse_overflow_result(p.negative, p.parsed_len);
  }
  if (st == RYU_PARSE_UNDERFLOW) {
    return ryu_parse_underflow_result(p.negative, p.parsed_len);
  }
  if (st != RYU_PARSE_OK && st != RYU_PARSE_INEXACT) {
    return ryu_parse_result_make(st, 0.0, p.parsed_len);
  }

  return ryu_parse_result_make(st, value, p.parsed_len);
}

#else

ryu64_parse_result ryu64_from_decimal_full(const char* s, size_t n) {
  ryu64_parse_result tiny = ryu64_from_decimal_tiny(s, n);
  if (tiny.status == RYU_PARSE_OUT_OF_RANGE) {
    tiny.status = RYU_PARSE_UNSUPPORTED;
  }
  return tiny;
}

#endif
