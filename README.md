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
  - for `nan(payload)`, consumes only `nan` (or signed `nan`) and leaves `(payload)` unparsed
- Tiny parser returns `RYU_PARSE_OUT_OF_RANGE` for nonzero numeric inputs outside that bounded contract.
  - This includes subnormal-scale decimal texts such as `5e-324`; use `ryu64_from_decimal_full` for those.
- Full parser (`ryu64_from_decimal_full`) with `RYU64_ENABLE_PARSE_BIGINT`:
  - accepts general decimal mantissas and large exponents with libc-free conversion
  - uses a fixed-width integer fast path for non-truncated mantissas (`<=19` significant digits) with decimal exponent in `[-38, 38]`
  - bigint conversion decomposes `10^k` as `5^k * 2^k` (tracked with a binary exponent adjustment) to reduce intermediate growth and avoid avoidable range fallthrough
  - uses a shared bigint workspace to cap nested-call stack growth in the fallback path
  - lexical significand accumulation stores up to `RYU_BIGINT_MAX_LIMBS * 9` decimal digits (`9216` with current defaults) before entering interval truncation logic
  - for mantissas that exceed bigint capacity, uses a conservative truncated-interval resolver:
    - computes lower/upper decimal bounds from kept digits
    - returns a value only when both bounds map to the same binary64 result
  - supports `nan(payload)` token consumption (ASCII alnum/underscore payload)
    - payload text is consumed but payload bits are not encoded; output is canonical quiet NaN (sign preserved)
  - returns `RYU_PARSE_OVERFLOW`/`RYU_PARSE_UNDERFLOW` for numeric range overflow/underflow
  - returns `RYU_PARSE_OUT_OF_RANGE` only when truncated intervals remain ambiguous or when fixed bigint capacity is exceeded during exact rational conversion

Bigint capacity and stack tuning:

- `RYU_BIGINT_MAX_LIMBS` defaults to `1024` (compile-time macro in internal headers).
- Full parser stack use is approximately:
  - `(7 * sizeof(ryu_bigint))` for parser state + shared workspace (`~29 KiB` at 1024 limbs, before normal call-frame overhead).
  - measured with `clang -O2 -fstack-usage` on macOS: `ryu64_from_decimal_full` uses `28944` bytes at default limb count.
- Lowering `RYU_BIGINT_MAX_LIMBS` reduces stack/object footprint, but also lowers long-mantissa coverage (more `RYU_PARSE_OUT_OF_RANGE` for very long inputs).
- Override example:
  - `make full CFLAGS_BASE='-std=c11 -O2 -Wall -Wextra -Wpedantic -Iinclude -DRYU_BIGINT_MAX_LIMBS=512u'`

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

CI note:

- GitHub Actions workflow (`.github/workflows/ci.yml`) runs `make test`, `make wasm-tiny`, `make nolibc-check-speed`, and `make nolibc-check-size` on both macOS and Linux.

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
  - deterministic stress includes dense low/high subnormal windows and a broad deterministic subnormal scatter set
- Non-oracle compile checks are provided via `nolibc-check-speed` and `nolibc-check-size`.
- Optional exhaustive subnormal parser sweep:
  - `RYU_EXHAUSTIVE_SUBNORMAL_LIMIT=<N> make test`
  - runs additional sequential checks for mantissas `1..N` in both subnormal signs (capped at `2^52-1`)
- Freestanding wasm note:
  - `wasm_compat/string.h` declares libc-like string/memory routines used by library code.
  - This repository provides `wasm_compat/string.c` with portable definitions (`memcpy`, `memmove`, `memcmp`, `memset`, `strlen`) for no-libc linkage.
  - the shim is intentionally minimal/correctness-first (byte-wise loops), not tuned for peak throughput.
  - `memcmp` returns sign-only (-1/0/1), which is C-conforming (`<0`, `==0`, `>0` contract).
  - If your runtime already provides these symbols, you can use that instead.

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

## Performance Tradeoff Note

- The current shortest/printf formatting core is implemented with bigint interval and rounding logic derived from paper/spec behavior, rather than a fully table-driven constant-time Ryū layout.
- This is an intentional clean-room/provenance tradeoff:
  - prioritizes straightforward, auditable derivations from allowed specs/papers,
  - accepts higher per-value cost (bigint operations and trial-style digit reduction) versus classic O(1)-style lookup-heavy Ryū implementations.
- Differential oracles and fuzzing are used to validate correctness despite this performance-oriented compromise.

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
