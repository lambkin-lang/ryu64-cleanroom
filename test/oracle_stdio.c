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
#include <time.h>

typedef struct {
  uint64_t* data;
  size_t len;
  size_t cap;
} u64_vec;

typedef struct {
  size_t cases;
  size_t failures;
} oracle_stats;

static uint64_t bits_from_double(double x) {
  uint64_t bits;
  memcpy(&bits, &x, sizeof(bits));
  return bits;
}

static double double_from_bits(uint64_t bits) {
  double x;
  memcpy(&x, &bits, sizeof(x));
  return x;
}

static uint64_t xorshift64(uint64_t* state) {
  uint64_t x = *state;
  x ^= x << 13u;
  x ^= x >> 7u;
  x ^= x << 17u;
  *state = x;
  return x;
}

static double now_seconds(void) {
  return (double)clock() / (double)CLOCKS_PER_SEC;
}

static int vec_reserve(u64_vec* v, size_t need) {
  uint64_t* next;
  size_t cap = v->cap;
  if (need <= cap) {
    return 1;
  }
  if (cap == 0u) {
    cap = 1024u;
  }
  while (cap < need) {
    cap *= 2u;
  }
  next = (uint64_t*)realloc(v->data, cap * sizeof(uint64_t));
  if (next == NULL) {
    return 0;
  }
  v->data = next;
  v->cap = cap;
  return 1;
}

static int vec_push(u64_vec* v, uint64_t bits) {
  if (!vec_reserve(v, v->len + 1u)) {
    return 0;
  }
  v->data[v->len++] = bits;
  return 1;
}

static int vec_push_if_finite(u64_vec* v, double x) {
  if (!isfinite(x)) {
    return 1;
  }
  return vec_push(v, bits_from_double(x));
}

static int add_boundary_neighbors(u64_vec* v, double x) {
  double prev;
  double next;
  if (!isfinite(x) || x == 0.0) {
    return 1;
  }
  prev = nextafter(x, 0.0);
  next = nextafter(x, INFINITY);
  if (!vec_push_if_finite(v, prev)) {
    return 0;
  }
  if (!vec_push_if_finite(v, x)) {
    return 0;
  }
  if (!vec_push_if_finite(v, next)) {
    return 0;
  }
  if (!vec_push_if_finite(v, -prev)) {
    return 0;
  }
  if (!vec_push_if_finite(v, -x)) {
    return 0;
  }
  if (!vec_push_if_finite(v, -next)) {
    return 0;
  }
  return 1;
}

static int add_pow2_neighbors(u64_vec* v) {
  int e;
  for (e = -1074; e <= 1023; ++e) {
    double x = ldexp(1.0, e);
    if (!add_boundary_neighbors(v, x)) {
      return 0;
    }
  }
  return 1;
}

static int add_pow10_neighbors(u64_vec* v) {
  int k;
  for (k = -323; k <= 308; ++k) {
    double x = pow(10.0, (double)k);
    if (!add_boundary_neighbors(v, x)) {
      return 0;
    }
  }
  return 1;
}

static int add_literal_bucket(u64_vec* v) {
  static const char* literals[] = {
      "0",
      "-0",
      "1",
      "-1",
      "9007199254740991",
      "9007199254740992",
      "9007199254740993",
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
      "1.00000000000000011102230246251565404236316680908203125",
      "0.1000000000000000055511151231257827021181583404541015625",
      "3.14159265358979323846264338327950288419716939937510",
      "2.71828182845904523536028747135266249775724709369995",
  };
  size_t i;
  for (i = 0u; i < sizeof(literals) / sizeof(literals[0]); ++i) {
    char* end = NULL;
    double x = strtod(literals[i], &end);
    if (end == NULL || *end != '\0') {
      continue;
    }
    if (isfinite(x)) {
      if (!vec_push(v, bits_from_double(x))) {
        return 0;
      }
    }
  }
  return 1;
}

static int add_special_bucket(u64_vec* v) {
  static const uint64_t specials[] = {
      UINT64_C(0x0000000000000000), /* +0 */
      UINT64_C(0x8000000000000000), /* -0 */
      UINT64_C(0x7ff0000000000000), /* +inf */
      UINT64_C(0xfff0000000000000), /* -inf */
      UINT64_C(0x7ff8000000000000), /* +qnan */
      UINT64_C(0xfff8000000000000), /* -qnan */
      UINT64_C(0x7ff8000000000001), /* +qnan payload */
      UINT64_C(0xfff8000000000001), /* -qnan payload */
  };
  size_t i;

  for (i = 0u; i < sizeof(specials) / sizeof(specials[0]); ++i) {
    if (!vec_push(v, specials[i])) {
      return 0;
    }
  }
  return 1;
}

static int add_crafted_patterns(u64_vec* v) {
  static const uint64_t mant_patterns[] = {
      UINT64_C(0),
      UINT64_C(1),
      UINT64_C(1) << 51u,
      (UINT64_C(1) << 52u) - UINT64_C(2),
      (UINT64_C(1) << 52u) - UINT64_C(1),
  };
  uint64_t exp_raw;
  size_t i;

  for (exp_raw = 1u; exp_raw <= 2046u; ++exp_raw) {
    for (i = 0u; i < sizeof(mant_patterns) / sizeof(mant_patterns[0]); ++i) {
      uint64_t bits = (exp_raw << 52u) | mant_patterns[i];
      if (!vec_push(v, bits)) {
        return 0;
      }
      if (!vec_push(v, bits | (UINT64_C(1) << 63u))) {
        return 0;
      }
    }
  }

  for (i = 0u; i < 52u; ++i) {
    uint64_t mant = UINT64_C(1) << i;
    uint64_t b1 = (UINT64_C(1) << 52u) | mant;
    uint64_t b2 = (UINT64_C(1023) << 52u) | mant;
    uint64_t b3 = (UINT64_C(2046) << 52u) | mant;
    if (!vec_push(v, b1) || !vec_push(v, b2) || !vec_push(v, b3)) {
      return 0;
    }
    if (!vec_push(v, b1 | (UINT64_C(1) << 63u)) ||
        !vec_push(v, b2 | (UINT64_C(1) << 63u)) ||
        !vec_push(v, b3 | (UINT64_C(1) << 63u))) {
      return 0;
    }
  }

  return 1;
}

static int add_subnormal_stress(u64_vec* v) {
  const uint64_t kSubMaxMant = (UINT64_C(1) << 52u) - UINT64_C(1);
  const uint64_t kDenseWindow = UINT64_C(8192);
  const uint64_t kScatterIters = UINT64_C(65536);
  uint64_t i;
  uint64_t m = UINT64_C(1);

  for (i = UINT64_C(1); i <= kDenseWindow; ++i) {
    uint64_t lo = i;
    uint64_t hi = kSubMaxMant - (i - UINT64_C(1));
    if (!vec_push(v, lo) || !vec_push(v, lo | (UINT64_C(1) << 63u))) {
      return 0;
    }
    if (hi != lo) {
      if (!vec_push(v, hi) || !vec_push(v, hi | (UINT64_C(1) << 63u))) {
        return 0;
      }
    }
  }

  for (i = 0u; i < kScatterIters; ++i) {
    m = (m * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407)) & kSubMaxMant;
    if (m == 0u) {
      m = UINT64_C(1);
    }
    if (!vec_push(v, m) || !vec_push(v, m | (UINT64_C(1) << 63u))) {
      return 0;
    }
  }

  return 1;
}

static int build_deterministic_dataset(u64_vec* out) {
  out->data = NULL;
  out->len = 0u;
  out->cap = 0u;
  if (!add_pow2_neighbors(out)) {
    return 0;
  }
  if (!add_pow10_neighbors(out)) {
    return 0;
  }
  if (!add_literal_bucket(out)) {
    return 0;
  }
  if (!add_crafted_patterns(out)) {
    return 0;
  }
  if (!add_subnormal_stress(out)) {
    return 0;
  }
  if (!add_special_bucket(out)) {
    return 0;
  }
  return 1;
}

static int oracle_scan_text(const char* s, size_t n, double* out_value, size_t* out_len) {
  char* buf = (char*)malloc(n + 1u);
  int consumed = 0;
  double value = 0.0;
  int rc;
  if (buf == NULL) {
    return 0;
  }
  memcpy(buf, s, n);
  buf[n] = '\0';
  rc = sscanf(buf, " %lf%n", &value, &consumed);
  free(buf);
  if (rc == 1 && consumed >= 0) {
    *out_value = value;
    *out_len = (size_t)consumed;
    return 1;
  }
  *out_len = 0u;
  return 0;
}

static int oracle_format_text(char* out, size_t out_cap, double x, const ryu_printf_spec* spec) {
  char fmt[64];
  char conv;
  char sign_flag = '\0';
  char alt_flag = '\0';
  int n;

  if (spec->kind == RYU_FMT_F) {
    conv = spec->uppercase ? 'F' : 'f';
  } else if (spec->kind == RYU_FMT_E) {
    conv = spec->uppercase ? 'E' : 'e';
  } else {
    conv = spec->uppercase ? 'G' : 'g';
  }

  if (spec->always_sign) {
    sign_flag = '+';
  } else if (spec->space_sign) {
    sign_flag = ' ';
  }
  if (spec->alternate_form) {
    alt_flag = '#';
  }

  if (spec->precision >= 0) {
    if (sign_flag != '\0' && alt_flag != '\0') {
      n = snprintf(fmt, sizeof(fmt), "%%%c%c.%d%c", sign_flag, alt_flag, spec->precision, conv);
    } else if (sign_flag != '\0') {
      n = snprintf(fmt, sizeof(fmt), "%%%c.%d%c", sign_flag, spec->precision, conv);
    } else if (alt_flag != '\0') {
      n = snprintf(fmt, sizeof(fmt), "%%%c.%d%c", alt_flag, spec->precision, conv);
    } else {
      n = snprintf(fmt, sizeof(fmt), "%%.%d%c", spec->precision, conv);
    }
  } else {
    if (sign_flag != '\0' && alt_flag != '\0') {
      n = snprintf(fmt, sizeof(fmt), "%%%c%c%c", sign_flag, alt_flag, conv);
    } else if (sign_flag != '\0') {
      n = snprintf(fmt, sizeof(fmt), "%%%c%c", sign_flag, conv);
    } else if (alt_flag != '\0') {
      n = snprintf(fmt, sizeof(fmt), "%%%c%c", alt_flag, conv);
    } else {
      n = snprintf(fmt, sizeof(fmt), "%%%c", conv);
    }
  }

  if (n <= 0 || (size_t)n >= sizeof(fmt)) {
    return 0;
  }
  n = snprintf(out, out_cap, fmt, x);
  return n >= 0 && (size_t)n < out_cap;
}

static unsigned count_significant_digits(const char* s) {
  unsigned count = 0u;
  int seen_nonzero = 0;
  size_t i = 0u;

  if (s[0] == '+' || s[0] == '-') {
    i += 1u;
  }

  while (s[i] != '\0' && s[i] != 'e' && s[i] != 'E') {
    char c = s[i];
    if (c >= '0' && c <= '9') {
      if (!seen_nonzero) {
        if (c != '0') {
          seen_nonzero = 1;
          count += 1u;
        }
      } else {
        count += 1u;
      }
    }
    i += 1u;
  }

  if (count == 0u) {
    return 1u;
  }
  return count;
}

static int text_has_nonzero_digit_prefix(const char* s, size_t n) {
  size_t i;
  for (i = 0u; i < n; ++i) {
    if (s[i] >= '1' && s[i] <= '9') {
      return 1;
    }
  }
  return 0;
}

static void log_mismatch(
    const char* tag,
    oracle_stats* stats,
    const char* detail,
    const char* a,
    const char* b) {
  stats->failures += 1u;
  if (stats->failures <= 20u) {
    fprintf(stderr, "[%s] %s | got='%s' oracle='%s'\n", tag, detail, a, b);
  }
}

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

static int shortest_has_shorter_candidate(uint64_t bits, const char* shortest, char* out_candidate, size_t out_cap) {
  const int kDeltaRadius = 8;
  const uint64_t kAbsMask = UINT64_C(0x7fffffffffffffff);
  int negative = (bits >> 63u) != 0u;
  uint64_t abs_bits = bits & kAbsMask;
  uint64_t sig = 0u;
  int exp10 = 0;
  unsigned digits = 0u;
  unsigned q;

  if (abs_bits == 0u || ((abs_bits >> 52u) & 0x7ffu) == 0x7ffu) {
    return 0;
  }
  if (!parse_shortest_components(shortest, &sig, &exp10, &digits)) {
    return -1;
  }
  if (sig == 0u || digits <= 1u || digits >= 20u) {
    return 0;
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
        if (out_candidate != NULL && out_cap > 0u) {
          strncpy(out_candidate, cand_text, out_cap - 1u);
          out_candidate[out_cap - 1u] = '\0';
        }
        return 1;
      }
    }
  }

  return 0;
}

static int run_printf_oracle(const u64_vec* ds, int quick, oracle_stats* stats) {
  static const int precisions_full[] = {-1, 0, 1, 2, 3, 6, 9, 15, 17, 24, 50, 100, 200};
  static const int precisions_quick[] = {-1, 0, 1, 2, 6, 9, 17, 50};
  const int* precisions = quick ? precisions_quick : precisions_full;
  size_t n_precisions = quick ? (sizeof(precisions_quick) / sizeof(precisions_quick[0]))
                              : (sizeof(precisions_full) / sizeof(precisions_full[0]));
  size_t i;
  size_t stride = quick ? ((ds->len / 1500u) + 1u) : 1u;

  for (i = 0u; i < ds->len; i += stride) {
    double x = double_from_bits(ds->data[i]);
    size_t k;
    int kind;
    int upper;
    int alt;
    int sign_mode;

    /*
     * Cross-libc NaN text/sign output is implementation-defined.
     * Keep this differential focused on stable finite/inf behavior and enforce
     * project NaN policy in run_printf_nan_policy().
     */
    if (isnan(x)) {
      continue;
    }

    for (kind = 0; kind < 3; ++kind) {
      for (k = 0u; k < n_precisions; ++k) {
        for (upper = 0; upper <= 1; ++upper) {
          for (alt = 0; alt <= 1; ++alt) {
            for (sign_mode = 0; sign_mode < 3; ++sign_mode) {
              ryu_printf_spec spec;
              char got[4096];
              char oracle[4096];
              size_t got_len = 0u;
              ryu_status st;

              spec.kind = (ryu_fmt_kind)kind;
              spec.precision = precisions[k];
              spec.uppercase = upper;
              spec.alternate_form = alt;
              spec.always_sign = (sign_mode == 1);
              spec.space_sign = (sign_mode == 2);

              st = ryu64_to_printf(got, sizeof(got), x, &spec, &got_len);
              stats->cases += 1u;
              if (st != RYU_OK) {
                stats->failures += 1u;
                if (stats->failures <= 20u) {
                  fprintf(stderr,
                          "[printf] status mismatch bits=0x%016llx status=%d\n",
                          (unsigned long long)ds->data[i],
                          (int)st);
                }
                continue;
              }
              if (!oracle_format_text(oracle, sizeof(oracle), x, &spec)) {
                stats->failures += 1u;
                continue;
              }
              if (strcmp(got, oracle) != 0) {
                log_mismatch("printf", stats, "text", got, oracle);
              }
              if (got_len != strlen(got)) {
                stats->failures += 1u;
              }
            }
          }
        }
      }
    }
  }
  return stats->failures == 0u;
}

static int run_printf_nan_policy(oracle_stats* stats) {
  static const uint64_t nan_bits[] = {
      UINT64_C(0x7ff8000000000000),
      UINT64_C(0xfff8000000000000),
      UINT64_C(0x7ff8000000000001),
      UINT64_C(0xfff8000000000001),
  };
  static const int precisions[] = {-1, 0, 6};
  size_t i;

  for (i = 0u; i < sizeof(nan_bits) / sizeof(nan_bits[0]); ++i) {
    double x = double_from_bits(nan_bits[i]);
    int kind;
    int upper;
    int alt;
    int sign_mode;
    size_t p;

    for (kind = 0; kind < 3; ++kind) {
      for (p = 0u; p < sizeof(precisions) / sizeof(precisions[0]); ++p) {
        for (upper = 0; upper <= 1; ++upper) {
          for (alt = 0; alt <= 1; ++alt) {
            for (sign_mode = 0; sign_mode < 3; ++sign_mode) {
              ryu_printf_spec spec;
              char got[64];
              const char* expected = upper ? "NAN" : "nan";
              size_t got_len = 0u;
              ryu_status st;

              spec.kind = (ryu_fmt_kind)kind;
              spec.precision = precisions[p];
              spec.uppercase = upper;
              spec.alternate_form = alt;
              spec.always_sign = (sign_mode == 1);
              spec.space_sign = (sign_mode == 2);

              st = ryu64_to_printf(got, sizeof(got), x, &spec, &got_len);
              stats->cases += 1u;
              if (st != RYU_OK) {
                stats->failures += 1u;
                continue;
              }
              if (strcmp(got, expected) != 0 || got_len != 3u) {
                log_mismatch("printf-nan", stats, "text", got, expected);
              }
            }
          }
        }
      }
    }
  }

  return stats->failures == 0u;
}

static int run_shortest_oracle(const u64_vec* ds, int quick, oracle_stats* stats) {
  size_t i;
  size_t stride = quick ? ((ds->len / 3000u) + 1u) : 1u;
  for (i = 0u; i < ds->len; i += stride) {
    double x = double_from_bits(ds->data[i]);
    char text[256];
    size_t text_len = 0u;
    ryu_status st;
    double parsed;
    size_t parsed_len;
    stats->cases += 1u;

    if (!isfinite(x)) {
      continue;
    }

    st = ryu64_to_shortest(text, sizeof(text), x, &text_len);
    if (st != RYU_OK) {
      stats->failures += 1u;
      continue;
    }

    if (!oracle_scan_text(text, text_len, &parsed, &parsed_len)) {
      stats->failures += 1u;
      continue;
    }
    if (parsed_len != text_len) {
      stats->failures += 1u;
      continue;
    }
    if (bits_from_double(parsed) != bits_from_double(x)) {
      stats->failures += 1u;
      if (stats->failures <= 20u) {
        fprintf(stderr,
                "[shortest] roundtrip mismatch bits=0x%016llx parsed=0x%016llx str='%s'\n",
                (unsigned long long)bits_from_double(x),
                (unsigned long long)bits_from_double(parsed),
                text);
      }
    }
  }
  return stats->failures == 0u;
}

static int run_shortest_minimality_oracle(const u64_vec* ds, int quick, oracle_stats* stats) {
  size_t i;
  size_t stride = quick ? ((ds->len / 3000u) + 1u) : ((ds->len / 12000u) + 1u);

  for (i = 0u; i < ds->len; i += stride) {
    uint64_t bits = ds->data[i];
    uint64_t abs_bits = bits & UINT64_C(0x7fffffffffffffff);
    double x = double_from_bits(bits);
    char text[256];
    char shorter[96];
    size_t text_len = 0u;
    ryu_status st;
    int has_shorter;

    if (abs_bits == 0u || ((abs_bits >> 52u) & 0x7ffu) == 0x7ffu) {
      continue;
    }

    stats->cases += 1u;
    st = ryu64_to_shortest(text, sizeof(text), x, &text_len);
    if (st != RYU_OK) {
      stats->failures += 1u;
      continue;
    }

    has_shorter = shortest_has_shorter_candidate(bits, text, shorter, sizeof(shorter));
    if (has_shorter < 0) {
      stats->failures += 1u;
      if (stats->failures <= 20u) {
        fprintf(stderr,
                "[shortest-min] parse failure bits=0x%016llx text='%s'\n",
                (unsigned long long)bits,
                text);
      }
      continue;
    }
    if (has_shorter > 0) {
      stats->failures += 1u;
      if (stats->failures <= 20u) {
        fprintf(stderr,
                "[shortest-min] shorter candidate found bits=0x%016llx shortest='%s' shorter='%s'\n",
                (unsigned long long)bits,
                text,
                shorter);
      }
    }
  }
  return stats->failures == 0u;
}

static int run_9sig_oracle(const u64_vec* ds, int quick, oracle_stats* stats) {
  size_t i;
  size_t stride = quick ? ((ds->len / 3000u) + 1u) : 1u;
  for (i = 0u; i < ds->len; i += stride) {
    double x = double_from_bits(ds->data[i]);
    char text[256];
    size_t text_len = 0u;
    ryu_status st;
    double parsed;
    size_t parsed_len;
    unsigned sig;
    stats->cases += 1u;

    st = ryu64_to_9sig(text, sizeof(text), x, &text_len);
    if (st != RYU_OK) {
      stats->failures += 1u;
      continue;
    }
    sig = count_significant_digits(text);
    if (sig > 9u) {
      stats->failures += 1u;
      if (stats->failures <= 20u) {
        fprintf(stderr, "[9sig] significant digits exceeded: %u str='%s'\n", sig, text);
      }
      continue;
    }
    if (!oracle_scan_text(text, text_len, &parsed, &parsed_len) || parsed_len != text_len) {
      stats->failures += 1u;
      continue;
    }
    (void)parsed;
  }
  return stats->failures == 0u;
}

static int compare_scan_full_with_oracle(
    const char* text,
    size_t text_len,
    double oracle_value,
    size_t oracle_len,
    oracle_stats* stats,
    const char* tag) {
  ryu64_parse_result r = ryu64_from_decimal_full(text, text_len);

  if (r.parsed_len != oracle_len) {
    stats->failures += 1u;
    if (stats->failures <= 20u) {
      fprintf(stderr,
              "[%s] parsed_len mismatch text='%s' got=%lu oracle=%lu\n",
              tag,
              text,
              (unsigned long)r.parsed_len,
              (unsigned long)oracle_len);
    }
    return 0;
  }

  if (isinf(oracle_value)) {
    if (!(r.status == RYU_PARSE_OVERFLOW || r.status == RYU_PARSE_OK) || !isinf(r.value)) {
      stats->failures += 1u;
      if (stats->failures <= 20u) {
        fprintf(stderr,
                "[%s] inf mismatch text='%s' status=%d val=0x%016llx oracle=0x%016llx\n",
                tag,
                text,
                (int)r.status,
                (unsigned long long)bits_from_double(r.value),
                (unsigned long long)bits_from_double(oracle_value));
      }
      return 0;
    }
    if (signbit(r.value) != signbit(oracle_value)) {
      stats->failures += 1u;
      if (stats->failures <= 20u) {
        fprintf(stderr,
                "[%s] inf sign mismatch text='%s' val=0x%016llx oracle=0x%016llx\n",
                tag,
                text,
                (unsigned long long)bits_from_double(r.value),
                (unsigned long long)bits_from_double(oracle_value));
      }
      return 0;
    }
    return 1;
  }

  if (isnan(oracle_value)) {
    if (!(r.status == RYU_PARSE_OK || r.status == RYU_PARSE_INEXACT) || !isnan(r.value)) {
      stats->failures += 1u;
      if (stats->failures <= 20u) {
        fprintf(stderr,
                "[%s] nan mismatch text='%s' status=%d val=0x%016llx oracle=0x%016llx\n",
                tag,
                text,
                (int)r.status,
                (unsigned long long)bits_from_double(r.value),
                (unsigned long long)bits_from_double(oracle_value));
      }
      return 0;
    }
    return 1;
  }

  if (oracle_value == 0.0 && text_has_nonzero_digit_prefix(text, oracle_len)) {
    if (r.status != RYU_PARSE_UNDERFLOW && bits_from_double(r.value) != bits_from_double(oracle_value)) {
      stats->failures += 1u;
      if (stats->failures <= 20u) {
        fprintf(stderr,
                "[%s] underflow mismatch text='%s' status=%d val=0x%016llx oracle=0x%016llx\n",
                tag,
                text,
                (int)r.status,
                (unsigned long long)bits_from_double(r.value),
                (unsigned long long)bits_from_double(oracle_value));
      }
      return 0;
    }
    return 1;
  }

  if (!(r.status == RYU_PARSE_OK || r.status == RYU_PARSE_INEXACT)) {
    stats->failures += 1u;
    if (stats->failures <= 20u) {
      fprintf(stderr,
              "[%s] status mismatch text='%s' status=%d oracle=0x%016llx\n",
              tag,
              text,
              (int)r.status,
              (unsigned long long)bits_from_double(oracle_value));
    }
    return 0;
  }

  if (bits_from_double(r.value) != bits_from_double(oracle_value)) {
    stats->failures += 1u;
    if (stats->failures <= 20u) {
      fprintf(stderr,
              "[%s] value mismatch text='%s' full=0x%016llx oracle=0x%016llx\n",
              tag,
              text,
              (unsigned long long)bits_from_double(r.value),
              (unsigned long long)bits_from_double(oracle_value));
    }
    return 0;
  }

  return 1;
}

static int run_scan_oracle_from_formats(const u64_vec* ds, int quick, oracle_stats* stats) {
  static const int precisions[] = {-1, 0, 1, 2, 6, 9, 17, 24, 50};
  size_t i;
  size_t k;
  size_t stride = quick ? ((ds->len / 2000u) + 1u) : 1u;

  for (i = 0u; i < ds->len; i += stride) {
    double x = double_from_bits(ds->data[i]);
    int kind;
    int upper;
    for (kind = 0; kind < 3; ++kind) {
      for (k = 0u; k < sizeof(precisions) / sizeof(precisions[0]); ++k) {
        for (upper = 0; upper <= 1; ++upper) {
          ryu_printf_spec spec;
          char text[4096];
          size_t text_len;
          double oracle_value;
          size_t oracle_len;
          ryu64_parse_result tiny;

          spec.kind = (ryu_fmt_kind)kind;
          spec.precision = precisions[k];
          spec.uppercase = upper;
          spec.alternate_form = 0;
          spec.always_sign = 0;
          spec.space_sign = 0;

          if (!oracle_format_text(text, sizeof(text), x, &spec)) {
            continue;
          }
          text_len = strlen(text);
          stats->cases += 1u;

          if (!oracle_scan_text(text, text_len, &oracle_value, &oracle_len)) {
            stats->failures += 1u;
            continue;
          }

          if (!compare_scan_full_with_oracle(text, text_len, oracle_value, oracle_len, stats, "scan-format")) {
            continue;
          }

          tiny = ryu64_from_decimal_tiny(text, text_len);
          if (tiny.status == RYU_PARSE_OK || tiny.status == RYU_PARSE_INEXACT) {
            if (tiny.parsed_len != oracle_len) {
              stats->failures += 1u;
            } else if (isnan(oracle_value)) {
              if (!isnan(tiny.value)) {
                stats->failures += 1u;
              }
            } else if (bits_from_double(tiny.value) != bits_from_double(oracle_value)) {
              stats->failures += 1u;
            }
          } else if (tiny.status != RYU_PARSE_OUT_OF_RANGE) {
            stats->failures += 1u;
          }
        }
      }
    }
  }
  return stats->failures == 0u;
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

static size_t build_random_decimal(
    char* out,
    size_t cap,
    uint64_t* state,
    int huge_scale,
    int force_long_digits) {
  const unsigned kSmallDigitsMax = 18u;
  const unsigned kWideDigitsMax = 3600u;
  const unsigned kTruncDigitsMin = 10000u;
  const unsigned kTruncDigitsSpan = 12000u;
  size_t pos = 0u;
  unsigned lead_ws = (unsigned)(xorshift64(state) % 3u);
  unsigned digits = 0u;
  unsigned split = 0u;
  int exp = 0;
  unsigned i;

  if (cap < 16u) {
    return 0u;
  }

  if (!huge_scale) {
    digits = (unsigned)(xorshift64(state) % kSmallDigitsMax) + 1u;
  } else if (force_long_digits) {
    /* Force parser truncation path coverage beyond bigint lexical capacity. */
    digits = kTruncDigitsMin + (unsigned)(xorshift64(state) % kTruncDigitsSpan);
  } else {
    digits = (unsigned)(xorshift64(state) % kWideDigitsMax) + 1u;
  }
  if (force_long_digits) {
    split = (unsigned)(xorshift64(state) % (uint64_t)digits) + 1u;
    /* Keep numeric magnitude near 1e[-20..20] while preserving long mantissa text. */
    exp = (int)split - (int)digits + (int)(xorshift64(state) % 41u) - 20;
  } else {
    split = (unsigned)(xorshift64(state) % (uint64_t)(digits + 1u));
    exp = (int)(xorshift64(state) % (huge_scale ? 20001u : 39u)) - (huge_scale ? 10000 : 19);
  }

  for (i = 0u; i < lead_ws && pos < cap - 1u; ++i) {
    out[pos++] = ' ';
  }
  if ((xorshift64(state) & UINT64_C(1)) != 0u && pos < cap - 1u) {
    out[pos++] = '-';
  }

  if (split == 0u && pos < cap - 1u) {
    out[pos++] = '.';
  }
  for (i = 0u; i < digits && pos < cap - 1u; ++i) {
    unsigned d;
    if (force_long_digits) {
      d = (i == 0u) ? ((unsigned)(xorshift64(state) % 9u) + 1u) : 0u;
    } else {
      d = (unsigned)(xorshift64(state) % 10u);
      if (i == 0u && d == 0u) {
        d = 1u;
      }
    }
    out[pos++] = (char)('0' + d);
    if (i + 1u == split && split != digits && pos < cap - 1u) {
      out[pos++] = '.';
    }
  }

  if ((force_long_digits || (xorshift64(state) & UINT64_C(3)) != 0u) && pos < cap - 1u) {
    int e = exp;
    out[pos++] = (xorshift64(state) & UINT64_C(1)) != 0u ? 'e' : 'E';
    if (e < 0) {
      out[pos++] = '-';
      e = -e;
    } else {
      out[pos++] = '+';
    }
    if (pos < cap - 1u) {
      pos += append_u64_dec(out + pos, (uint64_t)e);
    }
  }

  if (!force_long_digits && (xorshift64(state) & UINT64_C(7)) == 0u && pos < cap - 2u) {
    out[pos++] = 'x';
    out[pos++] = 'y';
  }

  out[pos] = '\0';
  return pos;
}

static int run_scan_fuzz(size_t iters, int huge_scale, uint64_t seed, oracle_stats* stats) {
  const size_t kFuzzTextCap = 32768u;
  uint64_t state = seed;
  char* text = (char*)malloc(kFuzzTextCap);
  size_t forced_long_cases = 0u;
  size_t i;

  if (text == NULL) {
    return 0;
  }

  for (i = 0u; i < iters; ++i) {
    int force_long_digits = (huge_scale && ((i % 8u) == 0u)) ? 1 : 0;
    size_t text_len = build_random_decimal(text, kFuzzTextCap, &state, huge_scale, force_long_digits);
    double oracle_value;
    size_t oracle_len;
    if (force_long_digits) {
      forced_long_cases += 1u;
    }
    stats->cases += 1u;
    if (text_len == 0u) {
      stats->failures += 1u;
      continue;
    }

    if (!oracle_scan_text(text, text_len, &oracle_value, &oracle_len)) {
      ryu64_parse_result r = ryu64_from_decimal_full(text, text_len);
      if (r.status != RYU_PARSE_INVALID) {
        stats->failures += 1u;
        if (stats->failures <= 20u) {
          fprintf(stderr,
                  "[scan-fuzz] oracle rejected text='%s' parser-status=%d\n",
                  text,
                  (int)r.status);
        }
      }
      continue;
    }

    if (!compare_scan_full_with_oracle(text, text_len, oracle_value, oracle_len, stats, "scan-fuzz")) {
      continue;
    }
  }
  if (huge_scale && forced_long_cases == 0u) {
    stats->failures += 1u;
  }
  free(text);
  return stats->failures == 0u;
}

static void print_summary(const char* name, const oracle_stats* before, const oracle_stats* after, double secs) {
  size_t cases = after->cases - before->cases;
  size_t fails = after->failures - before->failures;
  double rate = (secs > 0.0) ? ((double)cases / secs) : 0.0;
  printf("%-24s cases=%10lu fail=%6lu time=%8.3fs rate=%10.0f/s\n",
         name,
         (unsigned long)cases,
         (unsigned long)fails,
         secs,
         rate);
}

int main(int argc, char** argv) {
  int quick = 0;
  size_t fuzz_iters = 120000u;
  uint64_t seed = UINT64_C(0x9e3779b97f4a7c15);
  u64_vec ds;
  oracle_stats stats;
  double t0;
  oracle_stats mark;
  int i;

  for (i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--quick") == 0) {
      quick = 1;
      fuzz_iters = 12000u;
    } else if (strcmp(argv[i], "--fuzz") == 0 && i + 1 < argc) {
      fuzz_iters = (size_t)strtoull(argv[++i], NULL, 10);
    } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
      seed = (uint64_t)strtoull(argv[++i], NULL, 10);
    }
  }

  if (!build_deterministic_dataset(&ds)) {
    fprintf(stderr, "failed to build deterministic dataset\n");
    return 1;
  }

  stats.cases = 0u;
  stats.failures = 0u;

  printf("oracle dataset size: %lu values\n", (unsigned long)ds.len);
  printf("mode: %s fuzz_iters=%lu seed=%llu\n",
         quick ? "quick" : "full",
         (unsigned long)fuzz_iters,
         (unsigned long long)seed);

  mark = stats;
  t0 = now_seconds();
  run_shortest_oracle(&ds, quick, &stats);
  print_summary("shortest-oracle", &mark, &stats, now_seconds() - t0);

  mark = stats;
  t0 = now_seconds();
  run_shortest_minimality_oracle(&ds, quick, &stats);
  print_summary("shortest-minimality", &mark, &stats, now_seconds() - t0);

  mark = stats;
  t0 = now_seconds();
  run_9sig_oracle(&ds, quick, &stats);
  print_summary("9sig-oracle", &mark, &stats, now_seconds() - t0);

  mark = stats;
  t0 = now_seconds();
  run_printf_nan_policy(&stats);
  print_summary("printf-nan-policy", &mark, &stats, now_seconds() - t0);

  mark = stats;
  t0 = now_seconds();
  run_printf_oracle(&ds, quick, &stats);
  print_summary("printf-oracle", &mark, &stats, now_seconds() - t0);

  mark = stats;
  t0 = now_seconds();
  run_scan_oracle_from_formats(&ds, quick, &stats);
  print_summary("scan-from-format", &mark, &stats, now_seconds() - t0);

  mark = stats;
  t0 = now_seconds();
  run_scan_fuzz(fuzz_iters, 0, seed, &stats);
  print_summary("scan-fuzz-bounded", &mark, &stats, now_seconds() - t0);

  mark = stats;
  t0 = now_seconds();
  run_scan_fuzz(fuzz_iters, 1, seed ^ UINT64_C(0xa5a5a5a5a5a5a5a5), &stats);
  print_summary("scan-fuzz-wide", &mark, &stats, now_seconds() - t0);

  free(ds.data);

  if (stats.failures != 0u) {
    fprintf(stderr,
            "oracle checks failed: failures=%lu over %lu cases\n",
            (unsigned long)stats.failures,
            (unsigned long)stats.cases);
    return 1;
  }

  printf("oracle checks passed: %lu cases\n", (unsigned long)stats.cases);
  return 0;
}
