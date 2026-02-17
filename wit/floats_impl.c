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
 * WIT floats interface implementation.
 * Bridges the wit-bindgen-generated C types to the ryu64 C API.
 *
 * The three exported functions are declared in the generated floats_impl.h
 * and connected to ryu64_to_shortest, ryu64_to_printf, and
 * ryu64_from_decimal_full respectively.
 */

#include "floats_impl.h"
#include "ryu64.h"

/* --- to-string ---------------------------------------------------------- */

void exports_lambkin_runtime_floats_to_string(double value,
                                              floats_impl_string_t *ret) {
  char buf[32];
  size_t len;
  ryu_status st = ryu64_to_shortest(buf, sizeof(buf), value, &len);
  if (st == RYU_OK) {
    floats_impl_string_dup_n(ret, buf, len);
  } else {
    /* Should never happen — 32 bytes is always enough for shortest. */
    floats_impl_string_set(ret, "");
  }
}

/* --- format-f64 --------------------------------------------------------- */

/*
 * Apply field-width padding to a formatted result in `buf` of length `len`.
 * Writes the padded result into `out` (which must have room for `width` bytes)
 * and returns the final length.
 *
 * Printf semantics:
 *  - left_justify ('-'): pad right with spaces; overrides zero_pad.
 *  - zero_pad ('0'): pad left with '0' between sign prefix and digits.
 *    Not applied to inf/nan tokens.
 *  - Otherwise: pad left with spaces.
 */
static size_t apply_width(const char *buf, size_t len, uint32_t width,
                          bool zero_pad, bool left_justify, char *out) {
  if (len >= width) {
    /* Already meets or exceeds width — copy as-is. */
    for (size_t i = 0; i < len; i++)
      out[i] = buf[i];
    return len;
  }

  size_t pad = width - len;

  if (left_justify) {
    /* Content then spaces on the right. */
    for (size_t i = 0; i < len; i++)
      out[i] = buf[i];
    for (size_t i = 0; i < pad; i++)
      out[len + i] = ' ';
    return width;
  }

  if (zero_pad) {
    /* Check for inf/nan — zero-pad is not applied to these. */
    size_t start = 0;
    if (len > 0 && (buf[0] == '-' || buf[0] == '+' || buf[0] == ' '))
      start = 1;
    bool is_special = false;
    if (start < len) {
      char c = buf[start];
      if (c == 'i' || c == 'I' || c == 'n' || c == 'N')
        is_special = true;
    }
    if (!is_special) {
      /* Sign prefix, then zeros, then remaining digits. */
      size_t pos = 0;
      for (size_t i = 0; i < start; i++)
        out[pos++] = buf[i];
      for (size_t i = 0; i < pad; i++)
        out[pos++] = '0';
      for (size_t i = start; i < len; i++)
        out[pos++] = buf[i];
      return width;
    }
    /* Fall through to space-pad for inf/nan. */
  }

  /* Default: spaces on the left. */
  for (size_t i = 0; i < pad; i++)
    out[i] = ' ';
  for (size_t i = 0; i < len; i++)
    out[pad + i] = buf[i];
  return width;
}

/* --- locale helpers ----------------------------------------------------- */

/*
 * Resolve a decimal-point variant to its UTF-8 byte sequence.
 */
static const char *resolve_dp(
    const exports_lambkin_runtime_floats_decimal_point_t *dp, size_t *len) {
  switch (dp->tag) {
  case EXPORTS_LAMBKIN_RUNTIME_FLOATS_DECIMAL_POINT_DOT:
    *len = 1; return ".";
  case EXPORTS_LAMBKIN_RUNTIME_FLOATS_DECIMAL_POINT_COMMA:
    *len = 1; return ",";
  case EXPORTS_LAMBKIN_RUNTIME_FLOATS_DECIMAL_POINT_MIDDLE_DOT:
    *len = 2; return "\xc2\xb7";
  case EXPORTS_LAMBKIN_RUNTIME_FLOATS_DECIMAL_POINT_ARABIC_DECIMAL:
    *len = 2; return "\xd9\xab";
  case EXPORTS_LAMBKIN_RUNTIME_FLOATS_DECIMAL_POINT_OTHER:
    *len = dp->val.other.len;
    return (const char *)dp->val.other.ptr;
  default:
    *len = 1; return ".";
  }
}

/*
 * Resolve a thousands-sep variant to its UTF-8 byte sequence.
 */
static const char *resolve_sep(
    const exports_lambkin_runtime_floats_thousands_sep_t *sep, size_t *len) {
  switch (sep->tag) {
  case EXPORTS_LAMBKIN_RUNTIME_FLOATS_THOUSANDS_SEP_NONE:
    *len = 0; return "";
  case EXPORTS_LAMBKIN_RUNTIME_FLOATS_THOUSANDS_SEP_COMMA:
    *len = 1; return ",";
  case EXPORTS_LAMBKIN_RUNTIME_FLOATS_THOUSANDS_SEP_DOT:
    *len = 1; return ".";
  case EXPORTS_LAMBKIN_RUNTIME_FLOATS_THOUSANDS_SEP_SPACE:
    *len = 1; return " ";
  case EXPORTS_LAMBKIN_RUNTIME_FLOATS_THOUSANDS_SEP_NARROW_NBSP:
    *len = 3; return "\xe2\x80\xaf";
  case EXPORTS_LAMBKIN_RUNTIME_FLOATS_THOUSANDS_SEP_NBSP:
    *len = 2; return "\xc2\xa0";
  case EXPORTS_LAMBKIN_RUNTIME_FLOATS_THOUSANDS_SEP_APOSTROPHE:
    *len = 1; return "'";
  case EXPORTS_LAMBKIN_RUNTIME_FLOATS_THOUSANDS_SEP_UNDERSCORE:
    *len = 1; return "_";
  case EXPORTS_LAMBKIN_RUNTIME_FLOATS_THOUSANDS_SEP_OTHER:
    *len = sep->val.other.len;
    return (const char *)sep->val.other.ptr;
  default:
    *len = 0; return "";
  }
}

/*
 * Get the next group size from a grouping rule.
 * `group_index` counts groups from the right (0 = first/rightmost group).
 * Returns 0 to indicate "no more grouping".
 */
static uint8_t next_group_size(
    const exports_lambkin_runtime_floats_grouping_rule_t *rule,
    size_t group_index) {
  switch (rule->tag) {
  case EXPORTS_LAMBKIN_RUNTIME_FLOATS_GROUPING_RULE_REPEAT:
    return rule->val.repeat;
  case EXPORTS_LAMBKIN_RUNTIME_FLOATS_GROUPING_RULE_FIRST_THEN_REPEAT: {
    const exports_lambkin_runtime_floats_first_then_repeat_rule_t *ftr =
        &rule->val.first_then_repeat;
    return (group_index == 0) ? ftr->first : ftr->next;
  }
  case EXPORTS_LAMBKIN_RUNTIME_FLOATS_GROUPING_RULE_EXPLICIT: {
    const exports_lambkin_runtime_floats_explicit_grouping_rule_t *ex =
        &rule->val.explicit_;
    if (group_index < ex->groups.len) {
      return ex->groups.ptr[group_index];
    }
    /* Past the end of explicit list. */
    if (ex->tail == EXPORTS_LAMBKIN_RUNTIME_FLOATS_GROUPING_TAIL_REPEAT_LAST &&
        ex->groups.len > 0) {
      return ex->groups.ptr[ex->groups.len - 1];
    }
    return 0; /* stop */
  }
  default:
    return 0;
  }
}

/*
 * Apply locale transformations to a formatted ASCII string.
 *
 * Replaces '.' with the locale decimal point and inserts thousands
 * separators into the integer-digit region according to the grouping rule.
 * Special values (inf/nan) are not transformed.
 *
 * Returns the number of bytes written to `out`.
 */
static size_t apply_locale(
    const char *buf, size_t len,
    const exports_lambkin_runtime_floats_numeric_locale_t *loc,
    char *out, size_t out_cap) {
  size_t dp_len, sep_len;
  const char *dp_str = resolve_dp(&loc->decimal_point, &dp_len);
  const char *sep_str = resolve_sep(&loc->thousands_sep, &sep_len);

  /* Identify regions: [sign][prefix][int_digits][.][frac+exp] */
  size_t sign_end = 0;
  if (len > 0 && (buf[0] == '-' || buf[0] == '+' || buf[0] == ' '))
    sign_end = 1;

  /* Check for special values (inf/nan) — no locale transformation. */
  if (sign_end < len) {
    char c = buf[sign_end];
    if (c == 'i' || c == 'I' || c == 'n' || c == 'N') {
      /* Copy as-is. */
      size_t n = (len < out_cap) ? len : out_cap;
      for (size_t i = 0; i < n; i++)
        out[i] = buf[i];
      return n;
    }
  }

  /* Hex prefix "0x" / "0X". */
  size_t prefix_end = sign_end;
  if (prefix_end + 1 < len && buf[prefix_end] == '0' &&
      (buf[prefix_end + 1] == 'x' || buf[prefix_end + 1] == 'X'))
    prefix_end += 2;

  /* Find the decimal point '.' and end of integer digits. */
  size_t dot_pos = len;
  for (size_t i = prefix_end; i < len; i++) {
    if (buf[i] == '.') { dot_pos = i; break; }
    /* Stop at exponent marker — integer region is before it. */
    if (buf[i] == 'e' || buf[i] == 'E' || buf[i] == 'p' || buf[i] == 'P') {
      dot_pos = len; /* no dot before exponent */
      break;
    }
  }

  size_t int_start = prefix_end;
  size_t int_end = (dot_pos < len) ? dot_pos : len;
  /* If there is an exponent but no dot, find where exponent starts. */
  for (size_t i = int_start; i < int_end; i++) {
    if (buf[i] == 'e' || buf[i] == 'E' || buf[i] == 'p' || buf[i] == 'P') {
      int_end = i;
      break;
    }
  }

  size_t num_int_digits = int_end - int_start;

  /* --- Compute grouping separator positions --- */
  /* Count how many separators will be inserted. */
  size_t num_seps = 0;
  if (sep_len > 0 && num_int_digits > 0) {
    size_t pos_from_right = 0;
    size_t gi = 0;
    uint8_t gs = next_group_size(&loc->grouping.rule, gi);
    while (gs > 0 && pos_from_right + gs < num_int_digits) {
      pos_from_right += gs;
      num_seps++;
      gi++;
      gs = next_group_size(&loc->grouping.rule, gi);
    }
    /* require-separator policy: skip if only 1 group. */
    if (!loc->grouping.require_separator && num_seps < 1)
      num_seps = 0;
  }

  /* --- Assemble output --- */
  size_t pos = 0;

  /* Copy sign + prefix. */
  for (size_t i = 0; i < prefix_end && pos < out_cap; i++)
    out[pos++] = buf[i];

  /* Copy integer digits with grouping separators. */
  if (num_seps > 0 && sep_len > 0) {
    /*
     * Build an array of "digits before next separator" by replaying
     * the grouping rule from the right.  Then emit digits left-to-right,
     * inserting separators at the right positions.
     */
    /* sep_positions[i] = number of digits from the right where separator i goes */
    size_t sep_positions[512]; /* generous upper bound */
    {
      size_t pfr = 0;
      size_t gi = 0;
      size_t si = 0;
      uint8_t gs = next_group_size(&loc->grouping.rule, gi);
      while (gs > 0 && pfr + gs < num_int_digits && si < 512) {
        pfr += gs;
        sep_positions[si++] = pfr;
        gi++;
        gs = next_group_size(&loc->grouping.rule, gi);
      }
    }
    /* sep_positions[] are in "digits from right" order.
     * Convert to "digits from left" for easier emission. */
    size_t next_sep_idx = num_seps; /* count down from end */
    size_t digit_from_left = 0;
    for (size_t i = int_start; i < int_end && pos < out_cap; i++) {
      out[pos++] = buf[i];
      digit_from_left++;
      /* Check if a separator goes after this digit. */
      size_t digits_from_right = num_int_digits - digit_from_left;
      if (next_sep_idx > 0 &&
          digits_from_right == sep_positions[next_sep_idx - 1]) {
        next_sep_idx--;
        for (size_t s = 0; s < sep_len && pos < out_cap; s++)
          out[pos++] = sep_str[s];
      }
    }
  } else {
    /* No grouping — copy integer digits as-is. */
    for (size_t i = int_start; i < int_end && pos < out_cap; i++)
      out[pos++] = buf[i];
  }

  /* Replace decimal point. */
  if (dot_pos < len) {
    for (size_t i = 0; i < dp_len && pos < out_cap; i++)
      out[pos++] = dp_str[i];
    /* Copy everything after the dot (fraction + exponent). */
    for (size_t i = dot_pos + 1; i < len && pos < out_cap; i++)
      out[pos++] = buf[i];
  } else {
    /* No dot — copy the rest (exponent, if any) starting from int_end. */
    for (size_t i = int_end; i < len && pos < out_cap; i++)
      out[pos++] = buf[i];
  }

  return pos;
}

/* --- locale-aware parse helpers ----------------------------------------- */

/*
 * Compare n bytes.  Returns true if equal.  (Avoids pulling in <string.h>.)
 */
static bool bytes_eq(const char *a, const char *b, size_t n) {
  for (size_t i = 0; i < n; i++)
    if (a[i] != b[i]) return false;
  return true;
}

/*
 * Case-insensitive match of `src[0..n)` against a lowercase literal.
 */
static bool ci_prefix(const char *src, size_t src_len, const char *lit, size_t n) {
  if (src_len < n) return false;
  for (size_t i = 0; i < n; i++) {
    char c = src[i];
    if (c >= 'A' && c <= 'Z') c += 32;
    if (c != lit[i]) return false;
  }
  return true;
}

/*
 * Normalize a locale-formatted numeric string to C-locale ASCII.
 *
 * Strips thousands separators from the integer portion and replaces
 * the locale decimal point with ASCII '.'.  Builds a byte-offset map
 * so that parsed_length in the normalized domain can be translated
 * back to original input byte count.
 *
 * norm_buf and offset_map must have capacity >= norm_cap.
 * offset_map[i] = the index in `src` that produced norm_buf[i].
 * offset_map[ni] (the sentinel) = the source index past the last consumed byte.
 *
 * Returns the normalized length, or 0 on error (invalid separator
 * placement, buffer overflow).
 */
static size_t normalize_locale_input(
    const char *src, size_t src_len,
    const exports_lambkin_runtime_floats_numeric_locale_t *loc,
    char *norm_buf, uint16_t *offset_map, size_t norm_cap) {
  size_t dp_len, sep_len;
  const char *dp_str = resolve_dp(&loc->decimal_point, &dp_len);
  const char *sep_str = resolve_sep(&loc->thousands_sep, &sep_len);

  size_t si = 0; /* source index */
  size_t ni = 0; /* normalized index */
  bool in_integer = true;
  bool last_was_sep = false;
  bool saw_digit = false;

  /* Optional sign. */
  if (si < src_len && (src[si] == '+' || src[si] == '-')) {
    if (ni >= norm_cap) return 0;
    norm_buf[ni] = src[si];
    offset_map[ni] = (uint16_t)si;
    ni++; si++;
  }

  /* Check for special tokens: inf, infinity, nan (case-insensitive). */
  if (ci_prefix(src + si, src_len - si, "inf", 3) ||
      ci_prefix(src + si, src_len - si, "nan", 3)) {
    /* Copy the token verbatim. */
    while (si < src_len && ni < norm_cap) {
      char c = src[si];
      /* Keep copying alphanumeric + '(' + ')' + '_' for nan(...) payloads. */
      bool is_token = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '(' || c == ')' || c == '_';
      if (!is_token) break;
      norm_buf[ni] = c;
      offset_map[ni] = (uint16_t)si;
      ni++; si++;
    }
    offset_map[ni] = (uint16_t)si;
    return ni;
  }

  /* Main scanning loop. */
  while (si < src_len && ni < norm_cap) {
    /* Try to match thousands separator (only in integer portion). */
    if (in_integer && sep_len > 0 && saw_digit &&
        si + sep_len <= src_len &&
        bytes_eq(src + si, sep_str, sep_len)) {
      if (last_was_sep) return 0; /* consecutive separators */
      last_was_sep = true;
      si += sep_len;
      continue;
    }

    /* Try to match locale decimal point. */
    if (in_integer && dp_len > 0 &&
        si + dp_len <= src_len &&
        bytes_eq(src + si, dp_str, dp_len)) {
      if (last_was_sep) return 0; /* separator immediately before decimal */
      norm_buf[ni] = '.';
      offset_map[ni] = (uint16_t)si;
      ni++;
      si += dp_len;
      in_integer = false;
      continue;
    }

    /* Digit. */
    if (src[si] >= '0' && src[si] <= '9') {
      norm_buf[ni] = src[si];
      offset_map[ni] = (uint16_t)si;
      ni++; si++;
      last_was_sep = false;
      saw_digit = true;
      continue;
    }

    /* Exponent marker (only if digits were seen). */
    if (saw_digit && (src[si] == 'e' || src[si] == 'E')) {
      if (in_integer && last_was_sep) return 0; /* trailing separator */
      norm_buf[ni] = src[si];
      offset_map[ni] = (uint16_t)si;
      ni++; si++;
      in_integer = false;
      /* Optional exponent sign. */
      if (si < src_len && ni < norm_cap &&
          (src[si] == '+' || src[si] == '-')) {
        norm_buf[ni] = src[si];
        offset_map[ni] = (uint16_t)si;
        ni++; si++;
      }
      /* Exponent digits. */
      while (si < src_len && ni < norm_cap &&
             src[si] >= '0' && src[si] <= '9') {
        norm_buf[ni] = src[si];
        offset_map[ni] = (uint16_t)si;
        ni++; si++;
      }
      break;
    }

    /* Anything else: stop scanning. */
    break;
  }

  /* Trailing separator at end of integer portion is invalid. */
  if (in_integer && last_was_sep) return 0;

  offset_map[ni] = (uint16_t)si; /* sentinel */
  return ni;
}

/* --- format-f64 --------------------------------------------------------- */

bool exports_lambkin_runtime_floats_format_f64(
    double value,
    exports_lambkin_runtime_floats_format_options_t *options,
    floats_impl_string_t *ret,
    exports_lambkin_runtime_floats_format_error_t *err) {
  /* Map WIT notation enum to ryu_fmt_kind. */
  ryu_fmt_kind kind;
  switch (options->notation) {
  case EXPORTS_LAMBKIN_RUNTIME_FLOATS_NOTATION_DECIMAL:
    kind = RYU_FMT_F;
    break;
  case EXPORTS_LAMBKIN_RUNTIME_FLOATS_NOTATION_SCIENTIFIC:
    kind = RYU_FMT_E;
    break;
  case EXPORTS_LAMBKIN_RUNTIME_FLOATS_NOTATION_GENERAL:
    kind = RYU_FMT_G;
    break;
  case EXPORTS_LAMBKIN_RUNTIME_FLOATS_NOTATION_HEX:
    kind = RYU_FMT_A;
    break;
  default:
    err->tag = EXPORTS_LAMBKIN_RUNTIME_FLOATS_FORMAT_ERROR_INVALID;
    return false;
  }

  ryu_printf_spec spec;
  spec.kind = kind;
  spec.precision = options->precision.is_some ? (int)options->precision.val : -1;
  spec.uppercase = options->uppercase ? 1 : 0;
  spec.alternate_form = options->alternate_form ? 1 : 0;
  spec.always_sign = options->always_sign ? 1 : 0;
  spec.space_sign = options->space_sign ? 1 : 0;
  spec.rounding_mode = (int)options->rounding;

  /* %f of DBL_MAX needs ~800 bytes; 1024 covers the number itself. */
  char buf[1024];
  size_t len;
  ryu_status st = ryu64_to_printf(buf, sizeof(buf), value, &spec, &len);

  if (st != RYU_OK) {
    err->tag = (st == RYU_UNSUPPORTED)
                   ? EXPORTS_LAMBKIN_RUNTIME_FLOATS_FORMAT_ERROR_UNSUPPORTED
                   : EXPORTS_LAMBKIN_RUNTIME_FLOATS_FORMAT_ERROR_INVALID;
    return false;
  }

  /* Apply locale transformations (decimal point, grouping) if requested. */
  const char *src = buf;
  size_t src_len = len;
  char localized[2048];
  if (options->locale.is_some) {
    src_len = apply_locale(buf, len, &options->locale.val,
                           localized, sizeof(localized));
    src = localized;
  }

  /* Apply field-width padding if requested. */
  if (options->width.is_some && (uint32_t)src_len < options->width.val) {
    char padded[2048];
    size_t plen = apply_width(src, src_len, options->width.val,
                              options->zero_pad, options->left_justify, padded);
    floats_impl_string_dup_n(ret, padded, plen);
  } else {
    floats_impl_string_dup_n(ret, src, src_len);
  }
  return true;
}

/* --- parse-f64 ---------------------------------------------------------- */

bool exports_lambkin_runtime_floats_parse_f64(
    floats_impl_string_t *text,
    exports_lambkin_runtime_floats_numeric_locale_t *maybe_locale,
    exports_lambkin_runtime_floats_parsed_f64_t *ret,
    exports_lambkin_runtime_floats_parse_error_t *err) {

  const char *parse_buf = (const char *)text->ptr;
  size_t parse_len = text->len;
  char norm_buf[1024];
  uint16_t offset_map[1025]; /* +1 for sentinel at [ni] */
  bool have_map = false;

  if (maybe_locale != NULL) {
    /* Check for C-locale fast path: dot decimal + no separator. */
    bool is_c_locale =
        maybe_locale->decimal_point.tag ==
            EXPORTS_LAMBKIN_RUNTIME_FLOATS_DECIMAL_POINT_DOT &&
        maybe_locale->thousands_sep.tag ==
            EXPORTS_LAMBKIN_RUNTIME_FLOATS_THOUSANDS_SEP_NONE;

    if (!is_c_locale) {
      size_t norm_len = normalize_locale_input(
          (const char *)text->ptr, text->len,
          maybe_locale, norm_buf, offset_map, 1024);
      if (norm_len == 0 && text->len > 0) {
        err->tag = EXPORTS_LAMBKIN_RUNTIME_FLOATS_PARSE_ERROR_INVALID;
        return false;
      }
      parse_buf = norm_buf;
      parse_len = norm_len;
      have_map = true;
    }
  }

  ryu64_parse_result pr = ryu64_from_decimal_full(parse_buf, parse_len);

  switch (pr.status) {
  case RYU_PARSE_OK:
  case RYU_PARSE_INEXACT:
    ret->value = pr.value;
    ret->parsed_length = have_map ? (uint32_t)offset_map[pr.parsed_len]
                                  : (uint32_t)pr.parsed_len;
    return true;

  case RYU_PARSE_INVALID:
    err->tag = EXPORTS_LAMBKIN_RUNTIME_FLOATS_PARSE_ERROR_INVALID;
    return false;

  case RYU_PARSE_OUT_OF_RANGE:
  case RYU_PARSE_OVERFLOW:
  case RYU_PARSE_UNDERFLOW:
    err->tag = EXPORTS_LAMBKIN_RUNTIME_FLOATS_PARSE_ERROR_OUT_OF_RANGE;
    return false;

  case RYU_PARSE_UNSUPPORTED:
    err->tag = EXPORTS_LAMBKIN_RUNTIME_FLOATS_PARSE_ERROR_UNSUPPORTED;
    return false;

  default:
    err->tag = EXPORTS_LAMBKIN_RUNTIME_FLOATS_PARSE_ERROR_INVALID;
    return false;
  }
}

/*
 * Stub for the component-type force-link symbol that wit-bindgen emits.
 * The real definition lives in the generated component_type.o, which is
 * only meaningful in a wasm component build.  For native builds we
 * satisfy the reference with this no-op.
 */
void __component_type_object_force_link_floats_impl(void) {}
