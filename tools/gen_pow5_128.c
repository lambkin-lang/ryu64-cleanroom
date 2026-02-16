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
 * Table generator for the Eisel-Lemire fast-path parser.
 *
 * Generates a 651-entry table of 128-bit approximations of 5^q for
 * q in [-342, 308].  Each entry stores the most significant 128 bits,
 * normalized so the MSB is bit 127.
 *
 * For q >= 0:  T[q] = floor(5^q / 2^(bitlen(5^q) - 128))
 *              (exact when bitlen(5^q) <= 128)
 * For q < 0:   T[q] = ceil(2^(127 + floor(|q| * log2(5))) / 5^|q|)
 *
 * Reference: Daniel Lemire, "Number Parsing at a Gigabyte per Second",
 *            Software: Practice and Experience, 2021.
 *            (Table structure derived from paper's algorithm description.)
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Simple big integer: array of 32-bit limbs, little-endian (limb[0] = LSB). */
#define BIGINT_MAX_LIMBS 64  /* enough for 5^342 (~795 bits) + headroom */

typedef struct {
  uint32_t limb[BIGINT_MAX_LIMBS];
  int len;  /* number of active limbs */
} bigint;

static void bigint_from_u32(bigint* b, uint32_t v) {
  memset(b, 0, sizeof(*b));
  b->limb[0] = v;
  b->len = (v != 0) ? 1 : 0;
}

static void bigint_mul_u32(bigint* b, uint32_t m) {
  uint64_t carry = 0;
  int i;
  for (i = 0; i < b->len; ++i) {
    uint64_t p = (uint64_t)b->limb[i] * m + carry;
    b->limb[i] = (uint32_t)p;
    carry = p >> 32;
  }
  if (carry != 0) {
    if (b->len >= BIGINT_MAX_LIMBS) {
      fprintf(stderr, "bigint overflow in mul\n");
      return;
    }
    b->limb[b->len] = (uint32_t)carry;
    b->len += 1;
  }
}

/* Return the bit length of the bigint (0 for zero). */
static int bigint_bitlen(const bigint* b) {
  int i;
  if (b->len == 0) return 0;
  i = b->len - 1;
  /* bit position of MSB in top limb */
  {
    uint32_t top = b->limb[i];
    int bits = 0;
    while (top != 0) {
      top >>= 1;
      bits++;
    }
    return i * 32 + bits;
  }
}

/* Extract bits [bit_pos-63 .. bit_pos] as a uint64_t (bit_pos is 0-indexed). */
static uint64_t bigint_extract_u64(const bigint* b, int bit_pos) {
  uint64_t result = 0;
  int i;
  if (bit_pos < 0) return 0;
  for (i = 63; i >= 0; --i) {
    int bp = bit_pos - (63 - i);
    if (bp < 0) continue;
    int limb_idx = bp / 32;
    int bit_idx = bp % 32;
    if (limb_idx < b->len) {
      result |= (uint64_t)((b->limb[limb_idx] >> bit_idx) & 1) << i;
    }
  }
  return result;
}

/*
 * Compute the 128-bit division: ceil(2^B / divisor), where B = 127 + (bitlen(divisor) - 1).
 * This gives a result in [2^127, 2^128), i.e., MSB at bit 127.
 *
 * We compute floor(2^B / divisor) using long division on our bigint,
 * then add 1 if there is a nonzero remainder (to get ceiling).
 *
 * We only need the top 128 bits of the quotient.
 */
static void compute_reciprocal_128(const bigint* divisor, uint64_t* out_hi, uint64_t* out_lo) {
  /*
   * We want: ceil(2^B / divisor) where B = 127 + (bitlen(divisor) - 1).
   *
   * Since divisor has its MSB at position (bitlen-1), the quotient 2^B / divisor
   * has its MSB at position B - (bitlen-1) = 127, giving a 128-bit result.
   *
   * Strategy: build the dividend 2^B as a bigint, perform bigint division.
   */
  int div_bits = bigint_bitlen(divisor);
  int B = 127 + (div_bits - 1);
  bigint dividend;
  int i;

  /* Build 2^B */
  memset(&dividend, 0, sizeof(dividend));
  {
    int limb_idx = B / 32;
    int bit_idx = B % 32;
    if (limb_idx >= BIGINT_MAX_LIMBS) {
      fprintf(stderr, "dividend too large: B=%d\n", B);
      *out_hi = 0; *out_lo = 0;
      return;
    }
    dividend.limb[limb_idx] = (uint32_t)1 << bit_idx;
    dividend.len = limb_idx + 1;
  }

  /* Binary long division: shift-and-subtract, MSB first */
  {
    bigint rem2;
    bigint quot2;
    int has_nonzero_remainder = 0;

    memset(&rem2, 0, sizeof(rem2));
    memset(&quot2, 0, sizeof(quot2));

    /* Process bits of dividend from MSB to LSB */
    for (i = B; i >= 0; --i) {
      int d_limb = i / 32;
      int d_bit = i % 32;
      uint32_t d_val = 0;
      int j;
      uint32_t carry;
      int ge;

      if (d_limb < dividend.len) {
        d_val = (dividend.limb[d_limb] >> d_bit) & 1;
      }

      /* Shift rem2 left by 1, insert d_val at bit 0 */
      carry = 0;
      for (j = 0; j < rem2.len; ++j) {
        uint32_t new_carry = rem2.limb[j] >> 31;
        rem2.limb[j] = (rem2.limb[j] << 1) | carry;
        carry = new_carry;
      }
      if (carry) {
        if (rem2.len < BIGINT_MAX_LIMBS) {
          rem2.limb[rem2.len] = carry;
          rem2.len += 1;
        }
      }
      rem2.limb[0] |= d_val;
      if (rem2.len == 0 && d_val) rem2.len = 1;

      /* Compare rem2 >= divisor */
      ge = 0;
      if (rem2.len > divisor->len) {
        ge = 1;
      } else if (rem2.len == divisor->len) {
        for (j = rem2.len - 1; j >= 0; --j) {
          if (rem2.limb[j] > divisor->limb[j]) { ge = 1; break; }
          if (rem2.limb[j] < divisor->limb[j]) { ge = 0; break; }
          if (j == 0) ge = 1; /* equal */
        }
      }

      if (ge) {
        /* Subtract divisor from rem2 */
        uint64_t borrow = 0;
        for (j = 0; j < divisor->len; ++j) {
          uint64_t sub = (uint64_t)rem2.limb[j] - (uint64_t)divisor->limb[j] - borrow;
          rem2.limb[j] = (uint32_t)sub;
          borrow = (sub >> 32) & 1;
        }
        for (; borrow && j < rem2.len; ++j) {
          uint64_t sub = (uint64_t)rem2.limb[j] - borrow;
          rem2.limb[j] = (uint32_t)sub;
          borrow = (sub >> 32) & 1;
        }
        /* Trim leading zeros */
        while (rem2.len > 0 && rem2.limb[rem2.len - 1] == 0) {
          rem2.len -= 1;
        }

        /* Set quotient bit i */
        {
          int q_limb = i / 32;
          int q_bit = i % 32;
          if (q_limb < BIGINT_MAX_LIMBS) {
            quot2.limb[q_limb] |= (uint32_t)1 << q_bit;
            if (q_limb >= quot2.len) quot2.len = q_limb + 1;
          }
        }
      }
    }

    /* Check if remainder is nonzero (for ceiling) */
    has_nonzero_remainder = (rem2.len > 0);

    /* Extract top 128 bits from quotient */
    {
      int qbits = bigint_bitlen(&quot2);
      /* The quotient should be ~128 bits (MSB at bit 127) */
      *out_hi = bigint_extract_u64(&quot2, qbits - 1);
      *out_lo = bigint_extract_u64(&quot2, qbits - 65);
    }

    /* Ceiling: add 1 if remainder was nonzero */
    if (has_nonzero_remainder) {
      uint64_t lo_old = *out_lo;
      *out_lo += 1;
      if (*out_lo < lo_old) {
        *out_hi += 1;
      }
    }
  }
}

int main(void) {
  bigint pow5;
  int q;

  printf("#ifdef RYU64_ENABLE_PARSE_BIGINT\n\n");
  printf("const ryu_u128 ryu64_pow5_128[651] = {\n");

  /* First, generate entries for q in [-342, -1] (reciprocals).
   * For each |q|, compute 5^|q|, then the 128-bit reciprocal approximation. */

  /* Compute 5^1, 5^2, ..., 5^342 iteratively for negative q entries */
  bigint_from_u32(&pow5, 1);

  /* We'll store all pow5 values we need. Since we iterate from 1 to 342,
   * we can compute reciprocals as we go. But we need them in order
   * q = -342 to q = -1, which is |q| = 342 down to 1.
   * So first compute all 5^k for k=1..342, store the reciprocal results,
   * then output in order. */

  /* Strategy: compute 5^k for k=1..342, store 128-bit reciprocals in arrays */
  {
    uint64_t recip_hi[343];
    uint64_t recip_lo[343];
    int k;

    memset(recip_hi, 0, sizeof(recip_hi));
    memset(recip_lo, 0, sizeof(recip_lo));

    bigint_from_u32(&pow5, 1);
    for (k = 1; k <= 342; ++k) {
      bigint_mul_u32(&pow5, 5);
      compute_reciprocal_128(&pow5, &recip_hi[k], &recip_lo[k]);
    }

    /* Output q = -342 to q = -1 (indices 0 to 341 in the table) */
    for (q = -342; q <= -1; ++q) {
      k = -q;
      printf("    {UINT64_C(0x%016" PRIx64 "), UINT64_C(0x%016" PRIx64 ")}, /* 5^(%d) */\n",
             recip_hi[k], recip_lo[k], q);
    }
  }

  /* q = 0: 5^0 = 1, normalized to 128 bits = {0x8000000000000000, 0} */
  /* Wait: 5^0 = 1. bitlen = 1. We store floor(1 / 2^(1-128)) = 1 << 127.
   * So hi = 0x8000000000000000, lo = 0. */
  printf("    {UINT64_C(0x8000000000000000), UINT64_C(0x0000000000000000)}, /* 5^0 */\n");

  /* q = 1 to 308: compute 5^q, extract top 128 bits */
  bigint_from_u32(&pow5, 1);
  for (q = 1; q <= 308; ++q) {
    int bits;
    uint64_t hi, lo;

    bigint_mul_u32(&pow5, 5);
    bits = bigint_bitlen(&pow5);

    if (bits <= 64) {
      /* Value fits in 64 bits; extraction normalizes MSB to bit 63 of hi */
      hi = bigint_extract_u64(&pow5, bits - 1);
      lo = 0;
    } else if (bits <= 128) {
      /* Value fits in 128 bits; extraction normalizes MSB to bit 63 of hi */
      hi = bigint_extract_u64(&pow5, bits - 1);
      lo = bigint_extract_u64(&pow5, bits - 65);
    } else {
      /* Extract top 128 bits: bits [bits-1 .. bits-128] */
      hi = bigint_extract_u64(&pow5, bits - 1);
      lo = bigint_extract_u64(&pow5, bits - 65);
    }

    printf("    {UINT64_C(0x%016" PRIx64 "), UINT64_C(0x%016" PRIx64 ")}, /* 5^%d */\n",
           hi, lo, q);
  }

  printf("};\n\n");
  printf("#endif /* RYU64_ENABLE_PARSE_BIGINT */\n");

  return 0;
}
