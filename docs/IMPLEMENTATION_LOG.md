# IMPLEMENTATION_LOG

This document tracks clean-room provenance and implementation checkpoints for `ryu64-cleanroom`.

## Clean-Room Commitments

- Only paper/spec references listed below are used for pre-freeze implementation.
- No third-party implementation code is used as an input for library design or coding.
- Differential oracles (`snprintf`, `strtod`, `scanf`) are used only in tests.
- Any post-freeze discrepancy resolution is done by returning to paper/spec reasoning.

See `/docs/CLEANROOM_PROTOCOL.md` for the operational process.

## Consulted Documents (Pre-Implementation)

1. Ulf Adams, "Ryū: fast float-to-string conversion" (PLDI 2018).
- DOI: https://doi.org/10.1145/3192366.3192369
- Used for: shortest conversion invariants, interval reasoning, nearest-even goals.
- Sections used: algorithm overview and correctness discussion for shortest round-trip conversion.

2. "Ryū revisited: printf floating point conversion".
- ACM page: https://dl.acm.org/doi/10.1145/3360595
- Used for: `%f/%e/%g` conversion family behavior and rounding model.

3. POSIX printf utility description.
- URL: https://pubs.opengroup.org/onlinepubs/9699919799/utilities/printf.html
- Used for: formatting behavior targets and default precision expectations.

4. C formatting reference (`fprintf`).
- URL: https://en.cppreference.com/w/c/io/fprintf.html
- Used for: `%g` switching semantics, precision defaults, and flag behavior checklist.

5. `snprintf(3)` behavior summary.
- URL: https://man.archlinux.org/man/snprintf.3.en
- Used for: compatibility checklist for test oracles.

## Decimal -> Binary64 Parser References

1. Daniel Lemire et al., "Number Parsing at a Gigabyte per Second".
- arXiv: https://arxiv.org/abs/2101.11408
- Used for: fast decimal parsing strategy concepts, fixed-width arithmetic fast-path framing, and fallback design constraints.
- Sections used: fast-path structure and correctness-oriented discussion around ambiguous cases requiring fallback.

2. IEEE-754 binary64 representation references.
- Used for: sign/exponent/fraction layout, exponent bias, normal/subnormal boundaries, and round-to-nearest-ties-to-even target behavior.

3. C/POSIX `strtod`-style grammar references.
- Used for: lexical grammar target (`sign`, decimal form, exponent form, `inf`, `nan` token handling), and end-pointer style consumption behavior.

## Prohibited Inputs Confirmed (Pre-Freeze)

The following were not used before implementation:

- Any upstream Ryū implementation code or forks.
- libc parsing/formatting sources (musl/glibc/bionic/Apple libc dtoa/strtod internals).
- fast_float source code and related parser implementations.
- language-runtime parser implementations (V8, Go, Rust std/crates, etc.).
- blog/gist snippets containing parser/formatter code or copied constant tables.

## Implementation Record

### Formatting modules (`/src/ryu64_shortest.c`, `/src/ryu64_9sig.c`, `/src/ryu64_printf.c`)

- Implemented directly from paper/spec behavior targets.
- No imported code structure from existing Ryū repositories.
- Oracle validation uses libc behavior only in test binaries.

### Parsing modules (`/src/ryu64_parse_tiny.c`, `/src/ryu64_parse_full.c`)

- Tiny parser:
- bounded contract (`<=19` significant digits, effective exponent bounded),
- no heap allocation, no libc parse dependency.

- Full parser:
- bigint-backed rational conversion path when `RYU64_ENABLE_PARSE_BIGINT` is enabled,
- fixed-width fast path for non-truncated significands (`<=19` digits) in exponent range `[-38, 38]`,
- positive exponent fixed-width path uses local 192-bit integer arithmetic + nearest-even rounding,
- negative exponent fixed-width path uses ratio arithmetic with bounded `10^k` denominators in `uint128`,
- bigint conversion path decomposes `10^k` as `5^k * 2^k` and carries a separate binary exponent adjustment to reduce intermediate growth,
- bigint conversion cancels available powers of 2/5 from the parsed significand before denominator construction,
- when lexical significand accumulation truncates (capacity exceeded), parser now forms a decimal interval from kept digits:
  - lower bound: kept prefix scaled by dropped-digit count,
  - upper bound: `(kept + 1)` scaled by dropped-digit count,
  - returns a rounded result only when both bounds map to the same binary64 output,
- falls back to bigint conversion for wider inputs.

### Parse table provenance (`/src/ryu64_parse_tables.c`)

- `10^k` tables are committed as static constants.
- Reproducibility tool added: `/tools/gen_pow10_u128.c`.
- Generation method: local arithmetic progression (`10^0` then repeated `*10`) only.
- No external table dumps were imported.
- Verification command used during implementation:
  - `./build/gen_pow10_u128` output compared to the committed table text (`TABLE_MATCH`).

## Test/Validation Record

### Libc-free compile checks

- `make wasm-tiny`
- `make nolibc-check-speed`
- `make nolibc-check-size`

These verify non-oracle sources compile in freestanding/no-libc configurations.

### Native tests and oracles

- `make test`
- Includes deterministic unit tests and differential checks vs libc behavior.
- Includes long-mantissa parse checks that exceed bigint lexical capacity and validate truncated-interval behavior against `strtod`.

- `make oracle-test` / `make benchmark-speed` / `make benchmark-size`
- Oracle program: `/test/oracle_stdio.c`
- Uses `printf`/`scanf` as behavior oracles for:
- shortest/9sig output acceptance and roundtrip behavior,
- `%f/%e/%g` formatting matrix checks,
- tiny/full scanning comparisons,
- deterministic stress sets:
- nextafter neighbors around `2^e` (`e=-1074..1023`),
- nextafter neighbors around `10^k` (`k=-323..308`),
- crafted literal and bit-pattern buckets,
- randomized bounded and wide fuzzing (wide mode includes very long decimal texts that cross bigint accumulation capacity).

## Freeze and Post-Freeze Policy

- Freeze means implementation logic is paused; only then additional external validation references may be consulted.
- Post-freeze comparisons against external implementations are allowed for differential checks only.
- Any code fix must still be re-derived from paper/spec constraints.
