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

## Notes

- Post-freeze differential comparison against external Ryu implementations is allowed,
  but must not be used as a source for code structure or direct fixes.
- When discrepancies are found, fixes must be derived from paper/spec reasoning.
