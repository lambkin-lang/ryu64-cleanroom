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

#ifndef RYU64_H
#define RYU64_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Build-tier macros (set by the build system):
 *   RYU_TIER_TINY: shortest + 9sig, tiny/no-libc usage.
 *   RYU_TIER_TEST: enables test-only helpers/builds.
 *   RYU_TIER_FULL: enables printf-style formatting.
 */

typedef enum {
  RYU_OK = 0,
  RYU_BUFFER_TOO_SMALL = 1,
  RYU_UNSUPPORTED = 2,
  RYU_INVALID = 3
} ryu_status;

typedef enum {
  RYU_PARSE_OK = 0,
  RYU_PARSE_INVALID = 1,
  RYU_PARSE_OUT_OF_RANGE = 2,
  RYU_PARSE_INEXACT = 3,
  RYU_PARSE_UNSUPPORTED = 4,
  RYU_PARSE_OVERFLOW = 5,
  RYU_PARSE_UNDERFLOW = 6
} ryu_parse_status;

typedef struct {
  ryu_parse_status status;
  double value;
  size_t parsed_len;
} ryu64_parse_result;

/*
 * 9sig mode policy (Option 2: shortest-but-capped):
 * - If shortest-roundtrip uses <= 9 significant digits, return shortest output.
 * - Otherwise emit a 9-significant-digit scientific form with nearest-even
 *   rounding.
 * - Not all values in capped form are guaranteed to round-trip.
 */
ryu_status ryu64_to_shortest(char* out, size_t out_cap, double x, size_t* out_len);
ryu_status ryu64_to_9sig(char* out, size_t out_cap, double x, size_t* out_len);

typedef enum {
  RYU_FMT_F = 0,
  RYU_FMT_E = 1,
  RYU_FMT_G = 2
} ryu_fmt_kind;

typedef struct {
  ryu_fmt_kind kind;
  int precision;      /* -1 = default semantics per kind */
  int uppercase;      /* 0 => e/g/inf/nan, 1 => E/G/INF/NAN */
  int alternate_form; /* '#' */
  int always_sign;    /* '+' */
  int space_sign;     /* ' ' */
} ryu_printf_spec;

/*
 * Printf-style conversion for binary64.
 * Requires builds with RYU_TIER_FULL.
 */
ryu_status ryu64_to_printf(
    char* out,
    size_t out_cap,
    double x,
    const ryu_printf_spec* spec,
    size_t* out_len);

/*
 * Tiny decimal parser contract:
 * - C-locale ASCII syntax only.
 * - No heap, no locale, no libc parser calls.
 * - Nonzero values are accepted when they have <= 19 significant digits and
 *   effective base-10 exponent in [-19, 19].
 * - Inputs outside the tiny contract return RYU_PARSE_OUT_OF_RANGE.
 */
ryu64_parse_result ryu64_from_decimal_tiny(const char* s, size_t n);

/*
 * Full parser entry point (libc-free). Current implementation provides a tiny
 * parser fallback and reports RYU_PARSE_UNSUPPORTED for wider numeric ranges.
 */
ryu64_parse_result ryu64_from_decimal_full(const char* s, size_t n);

#ifdef __cplusplus
}
#endif

#endif
