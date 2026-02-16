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

  /* %f of DBL_MAX needs ~800 bytes; 1024 covers all cases with margin. */
  char buf[1024];
  size_t len;
  ryu_status st = ryu64_to_printf(buf, sizeof(buf), value, &spec, &len);

  switch (st) {
  case RYU_OK:
    floats_impl_string_dup_n(ret, buf, len);
    return true;
  case RYU_UNSUPPORTED:
    err->tag = EXPORTS_LAMBKIN_RUNTIME_FLOATS_FORMAT_ERROR_UNSUPPORTED;
    return false;
  default:
    err->tag = EXPORTS_LAMBKIN_RUNTIME_FLOATS_FORMAT_ERROR_INVALID;
    return false;
  }
}

/* --- parse-f64 ---------------------------------------------------------- */

bool exports_lambkin_runtime_floats_parse_f64(
    floats_impl_string_t *text,
    exports_lambkin_runtime_floats_parsed_f64_t *ret,
    exports_lambkin_runtime_floats_parse_error_t *err) {
  ryu64_parse_result pr =
      ryu64_from_decimal_full((const char *)text->ptr, text->len);

  switch (pr.status) {
  case RYU_PARSE_OK:
  case RYU_PARSE_INEXACT:
    ret->value = pr.value;
    ret->parsed_length = (uint32_t)pr.parsed_len;
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
