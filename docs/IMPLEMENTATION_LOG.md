# IMPLEMENTATION_LOG

This document tracks clean-room provenance for `ryu64-cleanroom`.

## Rules Followed

- Only paper/spec references listed below were consulted before implementation.
- No upstream Ryu implementation code was consulted during implementation.
- No libc dtoa/printf float-format source code was consulted during implementation.

## Consulted Documents (Pre-Implementation)

1. Ulf Adams, "Ryu: fast float-to-string conversion" (PLDI 2018).
   - DOI: https://doi.org/10.1145/3192366.3192369
   - Purpose: shortest/round-trip interval model and correctness goals.

2. "Ryu revisited: printf floating point conversion".
   - ACM page: https://dl.acm.org/doi/10.1145/3360595
   - Purpose: printf-family behavior goals and rounding/format family mapping.

3. POSIX printf utility description.
   - URL: https://pubs.opengroup.org/onlinepubs/9699919799/utilities/printf.html
   - Purpose: `%e/%f/%g` behavior semantics and defaults.

4. C formatting reference (`fprintf`).
   - URL: https://en.cppreference.com/w/c/io/fprintf.html
   - Purpose: precision defaults, `%g` style switching, and flag semantics checklist.

5. `snprintf(3)` behavior summary.
   - URL: https://man.archlinux.org/man/snprintf.3.en
   - Purpose: implementation compatibility checklist for tests.

## Decimal -> Binary64 Parser

Allowed/spec references consulted for parser design:

1. Daniel Lemire et al., "Number Parsing at a Gigabyte per Second"
   - arXiv: https://arxiv.org/abs/2101.11408
   - Purpose: fast decimal-to-binary algorithm family reference and rounding-oriented
     design constraints for fixed-width arithmetic fast paths.

2. IEEE 754 binary64 layout references.
   - C floating-point representation notes and binary64 field conventions.
   - Purpose: sign/exponent/fraction packing behavior and nearest-even rounding targets.

3. C/POSIX syntax references for `strtod`-style input behavior.
   - POSIX/C descriptions of decimal floating input grammar (`sign`, decimal, exponent,
     `inf`, `nan`) used as behavior targets for parsing.

Parser clean-room statement:

- No existing parser implementation code (libc/fast_float/dtoa/musl/glibc/bionic,
  PostgreSQL, language runtimes, or code snippets/tables) was read before writing
  `ryu64_parse_*` modules.

Implementation note:

- `ryu64_from_decimal_full` uses a clean-room bigint-backed rational rounding path when
  `RYU64_ENABLE_PARSE_BIGINT` is defined; otherwise it falls back to tiny-parser coverage.
- Full parser includes a bounded fast path delegation to the tiny parser for common numeric
  inputs, then uses bigint conversion for out-of-bound numerics.

## Notes

- Post-freeze differential comparison against external Ryu implementations is allowed,
  but must not be used as a source for code structure or direct fixes.
- When discrepancies are found, fixes must be derived from paper/spec reasoning.
