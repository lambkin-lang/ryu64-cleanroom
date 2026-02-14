# ryu64-cleanroom

`ryu64-cleanroom` is a clean-room, MIT-licensed portable C11 library for IEEE-754 binary64 (`double`) decimal formatting, designed with WebAssembly MVP and no-heap integration in mind.

## What it provides

- Shortest round-trip conversion for `double`:
  - `ryu64_to_shortest(...)`
- 9-significant-digit compact mode (Option 2: shortest-but-capped):
  - `ryu64_to_9sig(...)`
- Printf-style conversion family (`%f`, `%e`/`%E`, `%g`/`%G`):
  - `ryu64_to_printf(...)` (FULL tier)
- Libc-free decimal text parser entry points:
  - `ryu64_from_decimal_tiny(...)`
  - `ryu64_from_decimal_full(...)` (bigint-backed when `RYU64_ENABLE_PARSE_BIGINT` is enabled)

Behavior includes finite values, signed zero, infinities, and NaN.

## Build tiers

The build system defines one tier macro at compile time:

- `RYU_TIER_TINY`
  - no-heap core, deterministic C-locale behavior, shortest + 9sig + tiny parser
- `RYU_TIER_TEST`
  - same implementation with test harness/oracle checks enabled in test binaries
- `RYU_TIER_FULL`
  - enables `ryu64_to_printf` and full parser entry point

Parser tier contract:

- Tiny parser (`ryu64_from_decimal_tiny`) accepts:
  - ASCII C-locale syntax with optional leading ASCII whitespace and sign
  - finite decimal numbers with up to 19 significant digits and effective decimal exponent in `[-19, 19]`
  - `inf`/`infinity` and bare `nan` (case-insensitive)
- Tiny parser returns `RYU_PARSE_OUT_OF_RANGE` for nonzero numeric inputs outside that bounded contract.
- Full parser (`ryu64_from_decimal_full`) with `RYU64_ENABLE_PARSE_BIGINT`:
  - accepts general decimal mantissas and large exponents with libc-free conversion
  - uses a fixed-width integer fast path for non-truncated mantissas (`<=19` significant digits) with decimal exponent in `[-38, 38]`
  - for mantissas that exceed bigint capacity, uses a conservative truncated-interval resolver:
    - computes lower/upper decimal bounds from kept digits
    - returns a value only when both bounds map to the same binary64 result
  - supports `nan(payload)` token consumption (ASCII alnum/underscore payload)
  - returns `RYU_PARSE_OVERFLOW`/`RYU_PARSE_UNDERFLOW` for numeric range overflow/underflow
  - may still return `RYU_PARSE_OUT_OF_RANGE` when truncated intervals remain ambiguous or internal intermediate sizes exceed fixed bigint capacity

## Build (macOS + GNU Make 3.81 compatible)

Requirements:

- C compiler available as `cc` (default macOS `clang` works)
- `ar`
- GNU Make 3.81
- GNU bash 3.2-compatible shell commands

Commands:

```bash
make tiny
make full
make test
make oracle-test
make benchmark-speed
make benchmark-size
make wasm-tiny
make nolibc-check-speed
make nolibc-check-size
make gen-parse-pow10
make clean
```

Artifacts:

- `build/libryu64_tiny.a`
- `build/libryu64_full.a`
- `build/test_ryu64`
- `build/oracle_ryu64_speed`
- `build/oracle_ryu64_size`
- `build/wasm-tiny/*.o` (compile-only wasm32 tiny objects)
- `build/nolibc-speed/*.o` and `build/nolibc-size/*.o` (compile-only no-libc checks)

Oracle/benchmark program:

- `test/oracle_stdio.c` is a dedicated libc-using test oracle that compares:
  - printing (`ryu64_to_printf`) vs `printf`/`snprintf`
  - scanning (`ryu64_from_decimal_tiny/full`) vs `scanf`/`sscanf`
  - shortest/9sig via `scanf` roundtrip checks
- It includes deterministic stress datasets and random fuzzing, and reports timing.
- Non-oracle compile checks are provided via `nolibc-check-speed` and `nolibc-check-size`.

## API summary

See `/include/ryu64.h`.

Core status:

- `RYU_OK`
- `RYU_BUFFER_TOO_SMALL`
- `RYU_UNSUPPORTED`
- `RYU_INVALID`

Parse status:

- `RYU_PARSE_OK`
- `RYU_PARSE_INVALID`
- `RYU_PARSE_OUT_OF_RANGE`
- `RYU_PARSE_INEXACT`
- `RYU_PARSE_UNSUPPORTED`
- `RYU_PARSE_OVERFLOW`
- `RYU_PARSE_UNDERFLOW`

All APIs use caller-provided buffers (`char* out`, `size_t out_cap`) and report output length via `size_t* out_len` when requested.

## Determinism and locale

- Decimal point is always `.`
- No locale/grouping behavior
- ASCII-only parsing/formatting behavior in parser grammar handling
- No dynamic allocation
- No libc dependency in library parse/format code paths

## Repository layout

- `/include/ryu64.h`
- `/src/*.c`
- `/test/test_ryu64.c`
- `/test/oracle_stdio.c`
- `/docs/IMPLEMENTATION_LOG.md`
- `/docs/CLEANROOM_PROTOCOL.md`
- `/tools/gen_pow10_u128.c`
- `/LICENSE`

## Clean-room provenance

See `/docs/IMPLEMENTATION_LOG.md` for consulted-paper/spec tracking and clean-room notes.
See `/docs/CLEANROOM_PROTOCOL.md` for the implementation/freeze/validation protocol and provenance guardrails.
