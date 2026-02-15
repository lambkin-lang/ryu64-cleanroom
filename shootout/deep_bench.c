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

#define _POSIX_C_SOURCE 200809L

#include "ryu64.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(__APPLE__) && defined(__MACH__)
#include <mach/mach_time.h>
#endif

#define SIGN_MASK UINT64_C(0x8000000000000000)
#define EXP_MASK UINT64_C(0x7ff0000000000000)
#define MANT_MASK UINT64_C(0x000fffffffffffff)
#define QUIET_NAN_BIT UINT64_C(0x0008000000000000)

#if defined(RYU_ENABLE_BIGINT_PROFILE)
#define RYU_BIGINT_PROFILE_SCOPE_FORMAT 0u
#define RYU_BIGINT_PROFILE_SCOPE_PARSE 1u
void ryu_bigint_profile_begin(unsigned scope);
void ryu_bigint_profile_end(unsigned scope);
size_t ryu_bigint_profile_last_peak(unsigned scope);
#endif

typedef struct {
  uint64_t* data;
  size_t len;
  size_t cap;
} u64_vec;

typedef struct {
  uint64_t* keys;
  unsigned char* used;
  size_t cap;
  size_t len;
} u64_set;

typedef struct {
  u64_vec values;
  u64_set seen;
} corpus;

typedef struct {
  size_t random_count;
  uint64_t seed;
  size_t warmup_count;
  size_t sample_limit;
  int bigint_stats;
} bench_options;

typedef struct {
  uint64_t in_bits;
  uint64_t parsed_bits;
  char text[128];
  int parsed_ok;
} fail_sample;

typedef struct {
  const char* id;
  size_t conversions;
  uint64_t elapsed_ns;
  double ns_per_conv;
  double avg_len;
  uint64_t checksum;
  size_t numeric_fail;
  size_t bit_fail;
  size_t parse_fail;
  size_t format_fail;
  fail_sample* numeric_samples;
  fail_sample* bit_samples;
  size_t numeric_sample_count;
  size_t bit_sample_count;
  size_t bigint_format_calls;
  uint64_t bigint_format_peak_sum_words;
  size_t bigint_format_peak_max_words;
  uint64_t bigint_format_peak_max_bits;
  size_t bigint_parse_calls;
  uint64_t bigint_parse_peak_sum_words;
  size_t bigint_parse_peak_max_words;
  uint64_t bigint_parse_peak_max_bits;
} bench_result;

typedef int (*format_fn)(char* out, size_t out_cap, double x, size_t* out_len);

typedef struct {
  const char* id;
  format_fn fn;
  int timed_roundtrip;
  int (*parse)(const char* text, double* out_value);
  int uses_ryu_format;
  int uses_ryu_parse;
} candidate;

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

static uint64_t canonicalize_snan(uint64_t bits) {
  uint64_t exp = bits & EXP_MASK;
  uint64_t mant = bits & MANT_MASK;
  if (exp == EXP_MASK && mant != 0u && (mant & QUIET_NAN_BIT) == 0u) {
    bits |= QUIET_NAN_BIT;
  }
  return bits;
}

static uint64_t xorshift64(uint64_t* state) {
  uint64_t x = *state;
  x ^= x << 13u;
  x ^= x >> 7u;
  x ^= x << 17u;
  *state = x;
  return x;
}

static uint64_t mix64(uint64_t x) {
  x ^= x >> 33u;
  x *= UINT64_C(0xff51afd7ed558ccd);
  x ^= x >> 33u;
  x *= UINT64_C(0xc4ceb9fe1a85ec53);
  x ^= x >> 33u;
  return x;
}

static size_t ceil_pow2(size_t x) {
  size_t p = 1u;
  while (p < x) {
    p <<= 1u;
  }
  return p;
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
    cap <<= 1u;
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

static int set_init(u64_set* s, size_t want_cap) {
  size_t cap = ceil_pow2(want_cap < 16u ? 16u : want_cap);
  s->keys = (uint64_t*)calloc(cap, sizeof(uint64_t));
  s->used = (unsigned char*)calloc(cap, sizeof(unsigned char));
  if (s->keys == NULL || s->used == NULL) {
    free(s->keys);
    free(s->used);
    s->keys = NULL;
    s->used = NULL;
    s->cap = 0u;
    s->len = 0u;
    return 0;
  }
  s->cap = cap;
  s->len = 0u;
  return 1;
}

static void set_free(u64_set* s) {
  free(s->keys);
  free(s->used);
  s->keys = NULL;
  s->used = NULL;
  s->cap = 0u;
  s->len = 0u;
}

static int set_insert_existing(u64_set* s, uint64_t key) {
  size_t mask = s->cap - 1u;
  size_t idx = (size_t)(mix64(key) & (uint64_t)mask);
  for (;;) {
    if (!s->used[idx]) {
      s->used[idx] = 1u;
      s->keys[idx] = key;
      s->len += 1u;
      return 1;
    }
    idx = (idx + 1u) & mask;
  }
}

static int set_grow(u64_set* s) {
  size_t old_cap = s->cap;
  uint64_t* old_keys = s->keys;
  unsigned char* old_used = s->used;
  size_t i;
  if (!set_init(s, old_cap << 1u)) {
    s->keys = old_keys;
    s->used = old_used;
    s->cap = old_cap;
    return 0;
  }
  for (i = 0u; i < old_cap; ++i) {
    if (old_used[i]) {
      set_insert_existing(s, old_keys[i]);
    }
  }
  free(old_keys);
  free(old_used);
  return 1;
}

static int set_insert(u64_set* s, uint64_t key, int* inserted) {
  size_t mask;
  size_t idx;
  if ((s->len + 1u) * 10u >= s->cap * 7u) {
    if (!set_grow(s)) {
      return 0;
    }
  }
  mask = s->cap - 1u;
  idx = (size_t)(mix64(key) & (uint64_t)mask);
  for (;;) {
    if (!s->used[idx]) {
      s->used[idx] = 1u;
      s->keys[idx] = key;
      s->len += 1u;
      *inserted = 1;
      return 1;
    }
    if (s->keys[idx] == key) {
      *inserted = 0;
      return 1;
    }
    idx = (idx + 1u) & mask;
  }
}

static int corpus_init(corpus* c, size_t expected_values) {
  memset(c, 0, sizeof(*c));
  if (!vec_reserve(&c->values, expected_values)) {
    return 0;
  }
  if (!set_init(&c->seen, expected_values * 2u + 1024u)) {
    free(c->values.data);
    c->values.data = NULL;
    c->values.cap = 0u;
    return 0;
  }
  return 1;
}

static void corpus_free(corpus* c) {
  free(c->values.data);
  c->values.data = NULL;
  c->values.len = 0u;
  c->values.cap = 0u;
  set_free(&c->seen);
}

static int corpus_add_bits(corpus* c, uint64_t bits) {
  int inserted = 0;
  bits = canonicalize_snan(bits);
  if (!set_insert(&c->seen, bits, &inserted)) {
    return 0;
  }
  if (!inserted) {
    return 1;
  }
  return vec_push(&c->values, bits);
}

static int corpus_add_double(corpus* c, double x) {
  return corpus_add_bits(c, bits_from_double(x));
}

static int add_signed(corpus* c, uint64_t bits) {
  if (!corpus_add_bits(c, bits & ~SIGN_MASK)) {
    return 0;
  }
  return corpus_add_bits(c, bits | SIGN_MASK);
}

static int add_value_neighbors_both_signs(corpus* c, double x) {
  double lo;
  double hi;
  double vals[3];
  size_t i;
  if (!isfinite(x)) {
    return 1;
  }
  if (x == 0.0) {
    return add_signed(c, bits_from_double(0.0));
  }
  if (x < 0.0) {
    x = -x;
  }
  lo = nextafter(x, 0.0);
  hi = nextafter(x, INFINITY);
  vals[0] = lo;
  vals[1] = x;
  vals[2] = hi;
  for (i = 0u; i < 3u; ++i) {
    uint64_t bits;
    if (!isfinite(vals[i])) {
      continue;
    }
    bits = bits_from_double(vals[i]);
    if (!add_signed(c, bits)) {
      return 0;
    }
  }
  return 1;
}

static int add_canonical_cases(corpus* c) {
  static const uint64_t kCases[] = {
      UINT64_C(0x0000000000000000), /* +0 */
      UINT64_C(0x8000000000000000), /* -0 */
      UINT64_C(0x3ff0000000000000), /* +1 */
      UINT64_C(0xbff0000000000000), /* -1 */
      UINT64_C(0x0010000000000000), /* +min normal */
      UINT64_C(0x8010000000000000), /* -min normal */
      UINT64_C(0x0000000000000001), /* +min subnormal */
      UINT64_C(0x8000000000000001), /* -min subnormal */
      UINT64_C(0x000fffffffffffff), /* +max subnormal */
      UINT64_C(0x800fffffffffffff), /* -max subnormal */
      UINT64_C(0x7fefffffffffffff), /* +max finite */
      UINT64_C(0xffefffffffffffff), /* -max finite */
      UINT64_C(0x7ff0000000000000), /* +inf */
      UINT64_C(0xfff0000000000000), /* -inf */
      UINT64_C(0x7ff8000000000000), /* +qnan */
      UINT64_C(0xfff8000000000000), /* -qnan */
      UINT64_C(0x7ff8000000000001), /* +qnan payload */
      UINT64_C(0xfff8000000000001), /* -qnan payload */
      UINT64_C(0x7ff9000000000000), /* +qnan payload */
      UINT64_C(0xfff9000000000000), /* -qnan payload */
      UINT64_C(0x7ff0000000000001), /* +snan payload (canonicalized) */
      UINT64_C(0xfff0000000000001), /* -snan payload (canonicalized) */
  };
  size_t i;
  for (i = 0u; i < sizeof(kCases) / sizeof(kCases[0]); ++i) {
    if (!corpus_add_bits(c, kCases[i])) {
      return 0;
    }
  }
  return 1;
}

static int add_pow2_neighbors(corpus* c) {
  int e;
  for (e = -1074; e <= 1023; ++e) {
    double x = ldexp(1.0, e);
    if (!add_value_neighbors_both_signs(c, x)) {
      return 0;
    }
  }
  return 1;
}

static int add_pow10_neighbors(corpus* c) {
  int k;
  for (k = -323; k <= 308; ++k) {
    double x = pow(10.0, (double)k);
    if (!isfinite(x) || x == 0.0) {
      continue;
    }
    if (!add_value_neighbors_both_signs(c, x)) {
      return 0;
    }
  }
  return 1;
}

static int add_decimal_literal_cases(corpus* c) {
  static const char* kLiterals[] = {
      "0.1",
      "0.2",
      "0.3",
      "-0.1",
      "-0.2",
      "-0.3",
      "9007199254740991",
      "9007199254740992",
      "9007199254740993",
      "-9007199254740991",
      "-9007199254740992",
      "-9007199254740993",
      "2.2250738585072014e-308",
      "2.2250738585072013e-308",
      "2.2250738585072012e-308",
      "4.9406564584124654e-324",
      "5e-324",
      "1e-323",
      "1.7976931348623157e308",
      "1.7976931348623155e308",
      "1e308",
      "1e-308",
      "1e-320",
      "1e-324",
      "1e309",
      "1e-400",
      "2.71828182845904523536028747135266249775724709369995",
      "3.14159265358979323846264338327950288419716939937510",
      "1.00000000000000011102230246251565404236316680908203125",
      "0.1000000000000000055511151231257827021181583404541015625",
      "2.22507385850720113605740979670913197593481954635164564e-308",
      "2.22507385850720125958213282370967131787874611351433091e-308",
  };
  size_t i;
  for (i = 0u; i < sizeof(kLiterals) / sizeof(kLiterals[0]); ++i) {
    char* end = NULL;
    double x = strtod(kLiterals[i], &end);
    if (end == NULL || *end != '\0') {
      continue;
    }
    if (!corpus_add_double(c, x)) {
      return 0;
    }
  }
  return 1;
}

static int add_crafted_bit_patterns(corpus* c) {
  static const uint16_t kExpFields[] = {
      0u, 1u, 2u, 3u, 32u, 511u, 1022u, 1023u, 1024u, 1536u, 2045u, 2046u};
  static const uint64_t kMantPatterns[] = {
      UINT64_C(0x0000000000000000),
      UINT64_C(0x0000000000000001),
      UINT64_C(0x0000000000000002),
      UINT64_C(0x0000000000000003),
      UINT64_C(0x000000000000000f),
      UINT64_C(0x00000000000000ff),
      UINT64_C(0x000000000000ffff),
      UINT64_C(0x0008000000000000),
      UINT64_C(0x0004000000000000),
      UINT64_C(0x000c000000000000),
      UINT64_C(0x0005555555555555),
      UINT64_C(0x000aaaaaaaaaaaaa),
      UINT64_C(0x000ffffffffffffe),
      UINT64_C(0x000fffffffffffff),
  };
  static const uint16_t kWalkExp[] = {0u, 1u, 1023u, 2046u};
  size_t i;
  size_t j;

  for (i = 0u; i < sizeof(kExpFields) / sizeof(kExpFields[0]); ++i) {
    uint64_t exp_bits = ((uint64_t)kExpFields[i]) << 52u;
    for (j = 0u; j < sizeof(kMantPatterns) / sizeof(kMantPatterns[0]); ++j) {
      uint64_t mant = kMantPatterns[j] & MANT_MASK;
      uint64_t bits = exp_bits | mant;
      if (!add_signed(c, bits)) {
        return 0;
      }
    }
  }

  for (i = 0u; i < 52u; ++i) {
    uint64_t mant = UINT64_C(1) << i;
    for (j = 0u; j < sizeof(kWalkExp) / sizeof(kWalkExp[0]); ++j) {
      uint64_t bits = (((uint64_t)kWalkExp[j]) << 52u) | mant;
      if (!add_signed(c, bits)) {
        return 0;
      }
    }
  }

  return 1;
}

static int add_random_patterns(corpus* c, uint64_t seed, size_t count) {
  size_t i;
  uint64_t state = (seed == 0u) ? UINT64_C(0x9e3779b97f4a7c15) : seed;
  for (i = 0u; i < count; ++i) {
    uint64_t bits = xorshift64(&state);
    if (!corpus_add_bits(c, bits)) {
      return 0;
    }
  }
  return 1;
}

static int build_corpus(corpus* c, const bench_options* opt) {
  size_t expected = opt->random_count + 250000u;
  if (!corpus_init(c, expected)) {
    return 0;
  }
  if (!add_canonical_cases(c)) {
    return 0;
  }
  if (!add_pow2_neighbors(c)) {
    return 0;
  }
  if (!add_pow10_neighbors(c)) {
    return 0;
  }
  if (!add_decimal_literal_cases(c)) {
    return 0;
  }
  if (!add_crafted_bit_patterns(c)) {
    return 0;
  }
  if (!add_random_patterns(c, opt->seed, opt->random_count)) {
    return 0;
  }
  return 1;
}

static uint64_t now_ns(void) {
#if defined(__APPLE__) && defined(__MACH__)
  static mach_timebase_info_data_t tb = {0u, 0u};
  uint64_t t;
  if (tb.denom == 0u) {
    mach_timebase_info(&tb);
  }
  t = mach_absolute_time();
  return (t * (uint64_t)tb.numer) / (uint64_t)tb.denom;
#elif defined(CLOCK_MONOTONIC)
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    return 0u;
  }
  return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
#else
  struct timespec ts;
  timespec_get(&ts, TIME_UTC);
  return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
#endif
}

static int numeric_equal(double a, double b) {
  if (isnan(a) && isnan(b)) {
    return 1;
  }
  return a == b;
}

static void add_sample(
    fail_sample* samples,
    size_t* sample_count,
    size_t sample_cap,
    uint64_t in_bits,
    int parsed_ok,
    uint64_t parsed_bits,
    const char* text) {
  fail_sample* slot;
  if (*sample_count >= sample_cap) {
    return;
  }
  slot = &samples[*sample_count];
  slot->in_bits = in_bits;
  slot->parsed_ok = parsed_ok;
  slot->parsed_bits = parsed_bits;
  strncpy(slot->text, text, sizeof(slot->text) - 1u);
  slot->text[sizeof(slot->text) - 1u] = '\0';
  *sample_count += 1u;
}

static int parse_decimal_text(const char* text, double* out_value) {
  char* end = NULL;
  *out_value = strtod(text, &end);
  return end != NULL && *end == '\0' && end != text;
}

static int parse_status_success(ryu_parse_status st) {
  return st == RYU_PARSE_OK || st == RYU_PARSE_INEXACT;
}

static int parse_decimal_text_ryu_full(const char* text, double* out_value) {
  size_t n = strlen(text);
  ryu64_parse_result r = ryu64_from_decimal_full(text, n);
  if (!parse_status_success(r.status) || r.parsed_len != n) {
    return 0;
  }
  *out_value = r.value;
  return 1;
}

static void record_parse_failure(
    bench_result* out,
    const bench_options* opt,
    uint64_t in_bits,
    const char* text) {
  out->parse_fail += 1u;
  out->numeric_fail += 1u;
  out->bit_fail += 1u;
  add_sample(
      out->numeric_samples,
      &out->numeric_sample_count,
      opt->sample_limit,
      in_bits,
      0,
      0u,
      text);
  add_sample(
      out->bit_samples,
      &out->bit_sample_count,
      opt->sample_limit,
      in_bits,
      0,
      0u,
      text);
}

static void record_format_failure(
    bench_result* out,
    const bench_options* opt,
    uint64_t in_bits) {
  out->format_fail += 1u;
  out->numeric_fail += 1u;
  out->bit_fail += 1u;
  add_sample(
      out->numeric_samples,
      &out->numeric_sample_count,
      opt->sample_limit,
      in_bits,
      0,
      0u,
      "<format-fail>");
  add_sample(
      out->bit_samples,
      &out->bit_sample_count,
      opt->sample_limit,
      in_bits,
      0,
      0u,
      "<format-fail>");
}

static void record_parse_compare(
    bench_result* out,
    const bench_options* opt,
    uint64_t in_bits,
    double in_value,
    double parsed_value,
    const char* text) {
  uint64_t parsed_bits = bits_from_double(parsed_value);
  int numeric_ok = numeric_equal(in_value, parsed_value);
  int bit_ok = (in_bits == parsed_bits);

  if (!numeric_ok) {
    out->numeric_fail += 1u;
    add_sample(
        out->numeric_samples,
        &out->numeric_sample_count,
        opt->sample_limit,
        in_bits,
        1,
        parsed_bits,
        text);
  }
  if (!bit_ok) {
    out->bit_fail += 1u;
    add_sample(
        out->bit_samples,
        &out->bit_sample_count,
        opt->sample_limit,
        in_bits,
        1,
        parsed_bits,
        text);
  }
}

static int format_ryu64(char* out, size_t out_cap, double x, size_t* out_len) {
  static const ryu_printf_spec kSpec = {
      RYU_FMT_G,
      17,
      0,
      0,
      0,
      0,
  };
  ryu_status st = ryu64_to_printf(out, out_cap, x, &kSpec, out_len);
  if (st != RYU_OK || out_len == NULL || *out_len >= out_cap) {
    return 0;
  }
  out[*out_len] = '\0';
  return 1;
}

static int format_snprintf17g(char* out, size_t out_cap, double x, size_t* out_len) {
  int n = snprintf(out, out_cap, "%.17g", x);
  if (n < 0 || (size_t)n >= out_cap) {
    return 0;
  }
  if (out_len != NULL) {
    *out_len = (size_t)n;
  }
  return 1;
}

static int run_candidate(
    const candidate* cand,
    const u64_vec* corpus_values,
    const bench_options* opt,
    bench_result* out) {
  char buf[256];
  size_t i;
  size_t warmup = opt->warmup_count;
  size_t len_sum = 0u;
  size_t len_count = 0u;
  uint64_t t0;
  uint64_t t1;

  memset(out, 0, sizeof(*out));
  out->id = cand->id;
  out->conversions = corpus_values->len;
  out->numeric_samples = (fail_sample*)calloc(opt->sample_limit, sizeof(fail_sample));
  out->bit_samples = (fail_sample*)calloc(opt->sample_limit, sizeof(fail_sample));
  if (out->numeric_samples == NULL || out->bit_samples == NULL) {
    free(out->numeric_samples);
    free(out->bit_samples);
    out->numeric_samples = NULL;
    out->bit_samples = NULL;
    return 0;
  }

  if (warmup > corpus_values->len) {
    warmup = corpus_values->len;
  }

  for (i = 0u; i < warmup; ++i) {
    size_t out_len = 0u;
    double x = double_from_bits(corpus_values->data[i]);
    if (cand->fn(buf, sizeof(buf), x, &out_len)) {
      if (cand->timed_roundtrip) {
        double parsed = 0.0;
        if (cand->parse == NULL || !cand->parse(buf, &parsed)) {
          out->checksum ^= UINT64_C(0x9e3779b97f4a7c15);
        } else {
          out->checksum ^= bits_from_double(parsed);
        }
      }
      out->checksum ^= (uint64_t)(unsigned char)buf[0];
      out->checksum ^= (uint64_t)out_len << 16u;
    }
  }

  t0 = now_ns();
  for (i = 0u; i < corpus_values->len; ++i) {
    uint64_t in_bits = corpus_values->data[i];
    double x = double_from_bits(in_bits);
    size_t out_len = 0u;
    int format_ok = 0;
#if defined(RYU_ENABLE_BIGINT_PROFILE)
    if (opt->bigint_stats && cand->uses_ryu_format) {
      size_t peak_words;
      ryu_bigint_profile_begin(RYU_BIGINT_PROFILE_SCOPE_FORMAT);
      format_ok = cand->fn(buf, sizeof(buf), x, &out_len);
      ryu_bigint_profile_end(RYU_BIGINT_PROFILE_SCOPE_FORMAT);
      peak_words = ryu_bigint_profile_last_peak(RYU_BIGINT_PROFILE_SCOPE_FORMAT);
      out->bigint_format_calls += 1u;
      out->bigint_format_peak_sum_words += (uint64_t)peak_words;
      if (peak_words > out->bigint_format_peak_max_words) {
        out->bigint_format_peak_max_words = peak_words;
        out->bigint_format_peak_max_bits = in_bits;
      }
    } else
#endif
    {
      format_ok = cand->fn(buf, sizeof(buf), x, &out_len);
    }
    if (!format_ok) {
      if (cand->timed_roundtrip) {
        record_format_failure(out, opt, in_bits);
      }
      continue;
    }

    out->checksum ^= (uint64_t)(unsigned char)buf[0];
    out->checksum ^= ((uint64_t)out_len << 8u);
    len_sum += out_len;
    len_count += 1u;

    if (cand->timed_roundtrip) {
      double parsed = 0.0;
      int parse_ok = 0;
#if defined(RYU_ENABLE_BIGINT_PROFILE)
      if (opt->bigint_stats && cand->uses_ryu_parse) {
        size_t peak_words;
        ryu_bigint_profile_begin(RYU_BIGINT_PROFILE_SCOPE_PARSE);
        parse_ok = (cand->parse != NULL && cand->parse(buf, &parsed));
        ryu_bigint_profile_end(RYU_BIGINT_PROFILE_SCOPE_PARSE);
        peak_words = ryu_bigint_profile_last_peak(RYU_BIGINT_PROFILE_SCOPE_PARSE);
        out->bigint_parse_calls += 1u;
        out->bigint_parse_peak_sum_words += (uint64_t)peak_words;
        if (peak_words > out->bigint_parse_peak_max_words) {
          out->bigint_parse_peak_max_words = peak_words;
          out->bigint_parse_peak_max_bits = in_bits;
        }
      } else
#endif
      {
        parse_ok = (cand->parse != NULL && cand->parse(buf, &parsed));
      }
      if (!parse_ok) {
        record_parse_failure(out, opt, in_bits, buf);
      } else {
        record_parse_compare(out, opt, in_bits, x, parsed, buf);
      }
    }
  }
  t1 = now_ns();

  out->elapsed_ns = (t1 >= t0) ? (t1 - t0) : 0u;
  if (out->conversions != 0u) {
    out->ns_per_conv = (double)out->elapsed_ns / (double)out->conversions;
  }
  if (len_count != 0u) {
    out->avg_len = (double)len_sum / (double)len_count;
  }

  if (!cand->timed_roundtrip) {
    /*
     * Validation is intentionally outside the timed formatting loop so
     * roundtrip parsing does not contaminate format-only ns/conv.
     */
    for (i = 0u; i < corpus_values->len; ++i) {
      uint64_t in_bits = corpus_values->data[i];
      double x = double_from_bits(in_bits);
      double parsed = 0.0;
      size_t out_len = 0u;
      int format_ok = cand->fn(buf, sizeof(buf), x, &out_len);
      if (!format_ok) {
        record_format_failure(out, opt, in_bits);
        continue;
      }
      if (!parse_decimal_text(buf, &parsed)) {
        record_parse_failure(out, opt, in_bits, buf);
        continue;
      }
      record_parse_compare(out, opt, in_bits, x, parsed, buf);
    }
  }
  return 1;
}

static void free_result(bench_result* r) {
  free(r->numeric_samples);
  free(r->bit_samples);
  r->numeric_samples = NULL;
  r->bit_samples = NULL;
}

static int parse_u64_arg(const char* s, uint64_t* out) {
  char* end = NULL;
  unsigned long long v = strtoull(s, &end, 0);
  if (s == NULL || *s == '\0' || end == NULL || *end != '\0') {
    return 0;
  }
  *out = (uint64_t)v;
  return 1;
}

static void usage(const char* argv0) {
  printf("usage: %s [--random N] [--seed N] [--warmup N] [--samples N] [--bigint-stats]\n", argv0);
}

static int parse_args(int argc, char** argv, bench_options* opt) {
  int i;
  opt->random_count = 200000u;
  opt->seed = UINT64_C(0x9e3779b97f4a7c15);
  opt->warmup_count = 1000u;
  opt->sample_limit = 8u;
  opt->bigint_stats = 0;

  for (i = 1; i < argc; ++i) {
    uint64_t v = 0u;
    if (strcmp(argv[i], "--") == 0) {
      continue;
    } else if (strcmp(argv[i], "--random") == 0) {
      if (i + 1 >= argc || !parse_u64_arg(argv[i + 1], &v)) {
        return 0;
      }
      opt->random_count = (size_t)v;
      i += 1;
    } else if (strcmp(argv[i], "--seed") == 0) {
      if (i + 1 >= argc || !parse_u64_arg(argv[i + 1], &v)) {
        return 0;
      }
      opt->seed = v;
      i += 1;
    } else if (strcmp(argv[i], "--warmup") == 0) {
      if (i + 1 >= argc || !parse_u64_arg(argv[i + 1], &v)) {
        return 0;
      }
      opt->warmup_count = (size_t)v;
      i += 1;
    } else if (strcmp(argv[i], "--samples") == 0) {
      if (i + 1 >= argc || !parse_u64_arg(argv[i + 1], &v)) {
        return 0;
      }
      opt->sample_limit = (size_t)v;
      i += 1;
    } else if (strcmp(argv[i], "--bigint-stats") == 0) {
      opt->bigint_stats = 1;
    } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      usage(argv[0]);
      exit(0);
    } else {
      return 0;
    }
  }

  if (opt->sample_limit == 0u) {
    opt->sample_limit = 1u;
  }
  return 1;
}

static void print_result(const bench_result* r) {
  size_t i;
  const double bytes_per_word = (double)sizeof(uint32_t);
  printf(
      "RESULT\t%s\t%llu\t%llu\t%.3f\t%.6f\t%llu\t%llu\t%llu\t%llu\t%llu\n",
      r->id,
      (unsigned long long)r->conversions,
      (unsigned long long)r->elapsed_ns,
      r->ns_per_conv,
      r->avg_len,
      (unsigned long long)r->numeric_fail,
      (unsigned long long)r->bit_fail,
      (unsigned long long)r->parse_fail,
      (unsigned long long)r->format_fail,
      (unsigned long long)r->checksum);

  for (i = 0u; i < r->numeric_sample_count; ++i) {
    const fail_sample* s = &r->numeric_samples[i];
    if (s->parsed_ok) {
      printf(
          "FAIL\t%s\tnumeric\t0x%016llx\t0x%016llx\t%s\n",
          r->id,
          (unsigned long long)s->in_bits,
          (unsigned long long)s->parsed_bits,
          s->text);
    } else {
      printf(
          "FAIL\t%s\tnumeric\t0x%016llx\tparse_error\t%s\n",
          r->id,
          (unsigned long long)s->in_bits,
          s->text);
    }
  }

  for (i = 0u; i < r->bit_sample_count; ++i) {
    const fail_sample* s = &r->bit_samples[i];
    if (s->parsed_ok) {
      printf(
          "FAIL\t%s\tbit_exact\t0x%016llx\t0x%016llx\t%s\n",
          r->id,
          (unsigned long long)s->in_bits,
          (unsigned long long)s->parsed_bits,
          s->text);
    } else {
      printf(
          "FAIL\t%s\tbit_exact\t0x%016llx\tparse_error\t%s\n",
          r->id,
          (unsigned long long)s->in_bits,
          s->text);
    }
  }

  if (r->bigint_format_calls != 0u) {
    double avg_words = (double)r->bigint_format_peak_sum_words / (double)r->bigint_format_calls;
    printf(
        "BIGINT\t%s\tformat\t%llu\t%.6f\t%llu\t%.6f\t%llu\t0x%016llx\n",
        r->id,
        (unsigned long long)r->bigint_format_calls,
        avg_words,
        (unsigned long long)r->bigint_format_peak_max_words,
        avg_words * bytes_per_word,
        (unsigned long long)(r->bigint_format_peak_max_words * sizeof(uint32_t)),
        (unsigned long long)r->bigint_format_peak_max_bits);
  }

  if (r->bigint_parse_calls != 0u) {
    double avg_words = (double)r->bigint_parse_peak_sum_words / (double)r->bigint_parse_calls;
    printf(
        "BIGINT\t%s\tparse\t%llu\t%.6f\t%llu\t%.6f\t%llu\t0x%016llx\n",
        r->id,
        (unsigned long long)r->bigint_parse_calls,
        avg_words,
        (unsigned long long)r->bigint_parse_peak_max_words,
        avg_words * bytes_per_word,
        (unsigned long long)(r->bigint_parse_peak_max_words * sizeof(uint32_t)),
        (unsigned long long)r->bigint_parse_peak_max_bits);
  }
}

int main(int argc, char** argv) {
  bench_options opt;
  corpus c;
  bench_result results[4];
  size_t warmup_applied = 0u;
  candidate candidates[4];
  size_t i;

  if (!parse_args(argc, argv, &opt)) {
    usage(argv[0]);
    return 2;
  }
#if !defined(RYU_ENABLE_BIGINT_PROFILE)
  if (opt.bigint_stats) {
    fprintf(stderr, "--bigint-stats requires build flag -DRYU_ENABLE_BIGINT_PROFILE=1\n");
    return 2;
  }
#endif

  if (!build_corpus(&c, &opt)) {
    fprintf(stderr, "failed to build deterministic corpus\n");
    return 1;
  }
  if (opt.warmup_count < c.values.len) {
    warmup_applied = opt.warmup_count;
  } else {
    warmup_applied = c.values.len;
  }

  printf(
      "CONFIG\t%llu\t%llu\t%llu\t%llu\n",
      (unsigned long long)opt.seed,
      (unsigned long long)opt.random_count,
      (unsigned long long)warmup_applied,
      (unsigned long long)opt.sample_limit);
  printf(
      "CORPUS\t%llu\t%llu\t%llu\t%llu\n",
      (unsigned long long)c.values.len,
      (unsigned long long)warmup_applied,
      (unsigned long long)opt.random_count,
      (unsigned long long)opt.seed);

  candidates[0].id = "ryu64";
  candidates[0].fn = format_ryu64;
  candidates[0].timed_roundtrip = 0;
  candidates[0].parse = NULL;
  candidates[0].uses_ryu_format = 1;
  candidates[0].uses_ryu_parse = 0;
  candidates[1].id = "snprintf";
  candidates[1].fn = format_snprintf17g;
  candidates[1].timed_roundtrip = 0;
  candidates[1].parse = NULL;
  candidates[1].uses_ryu_format = 0;
  candidates[1].uses_ryu_parse = 0;
  candidates[2].id = "ryu64_rt";
  candidates[2].fn = format_ryu64;
  candidates[2].timed_roundtrip = 1;
  candidates[2].parse = parse_decimal_text_ryu_full;
  candidates[2].uses_ryu_format = 1;
  candidates[2].uses_ryu_parse = 1;
  candidates[3].id = "snprintf_rt";
  candidates[3].fn = format_snprintf17g;
  candidates[3].timed_roundtrip = 1;
  candidates[3].parse = parse_decimal_text;
  candidates[3].uses_ryu_format = 0;
  candidates[3].uses_ryu_parse = 0;

  for (i = 0u; i < 4u; ++i) {
    if (!run_candidate(&candidates[i], &c.values, &opt, &results[i])) {
      fprintf(stderr, "candidate run failed: %s\n", candidates[i].id);
      corpus_free(&c);
      return 1;
    }
    print_result(&results[i]);
  }

  for (i = 0u; i < 4u; ++i) {
    free_result(&results[i]);
  }
  corpus_free(&c);
  return 0;
}
