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

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
  uint64_t hi;
  uint64_t lo;
} u128;

static u128 mul10(u128 v) {
#if defined(__SIZEOF_INT128__)
  __uint128_t x = ((__uint128_t)v.hi << 64u) | (__uint128_t)v.lo;
  __uint128_t y = x * (__uint128_t)10u;
  u128 out;
  out.lo = (uint64_t)y;
  out.hi = (uint64_t)(y >> 64u);
  return out;
#else
  uint64_t lo_lo = (v.lo & UINT64_C(0xffffffff)) * 10u;
  uint64_t lo_hi = (v.lo >> 32u) * 10u;
  uint64_t hi_lo = (v.hi & UINT64_C(0xffffffff)) * 10u;
  uint64_t hi_hi = (v.hi >> 32u) * 10u;
  uint64_t carry = (lo_lo >> 32u) + (lo_hi & UINT64_C(0xffffffff));
  u128 out;
  out.lo = (lo_lo & UINT64_C(0xffffffff)) | (carry << 32u);
  carry = (carry >> 32u) + (lo_hi >> 32u);
  carry += hi_lo;
  out.hi = (carry & UINT64_C(0xffffffff)) | ((hi_hi + (carry >> 32u)) << 32u);
  return out;
#endif
}

int main(void) {
  u128 v;
  int k;

  v.hi = 0u;
  v.lo = 1u;

  puts("const ryu_u128 ryu64_pow10_u128[39] = {");
  for (k = 0; k <= 38; ++k) {
    printf("    {UINT64_C(0x%016" PRIx64 "), UINT64_C(0x%016" PRIx64 ")}, /* 10^%d */\n",
           v.hi,
           v.lo,
           k);
    v = mul10(v);
  }
  puts("};");
  return 0;
}
