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
- `ryu64_to_printf` emits unsigned NaN text (`nan`/`NAN`): NaN sign and `+`/space sign flags are ignored.
- Design choice:
  - `ryu64_to_shortest` and `ryu64_to_9sig` preserve sign (including signed NaN) as information-preserving conversions.
  - `ryu64_to_printf` suppresses NaN sign as a formatting portability policy because C `printf` NaN sign behavior is implementation-defined.

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
  - lexical significand accumulation uses in-place `*10 + digit` updates (no per-digit bigint snapshot copy)
  - lexical significand accumulation stores up to `RYU_BIGINT_MAX_LIMBS * 9` decimal digits before entering interval truncation logic
  - for mantissas that exceed bigint capacity, uses a conservative truncated-interval resolver:
    - computes lower/upper decimal bounds from kept digits
    - returns a value only when both bounds map to the same binary64 result
  - supports `nan(payload)` token consumption (ASCII alnum/underscore payload)
    - payload text is consumed but payload bits are not encoded; output is canonical quiet NaN (sign preserved)
  - returns `RYU_PARSE_OVERFLOW`/`RYU_PARSE_UNDERFLOW` for numeric range overflow/underflow
  - returns `RYU_PARSE_OUT_OF_RANGE` only when truncated intervals remain ambiguous or when fixed bigint capacity is exceeded during exact rational conversion

Bigint capacity and stack tuning:

- `RYU_BIGINT_MAX_LIMBS` defaults are tier/target-aware:
  - `512` for native non-tiny builds.
  - `256` for `RYU_TIER_TINY` and `wasm32` builds.
- Minimum supported value is `96` (compile-time guard).
- Per-tier stack budget (measured with `-O2 -fstack-usage`):
  - TINY / wasm32 (`RYU_TIER_TINY`, limbs=256, no parse-bigint workspace):
    - `ryu_choose_shortest_digits`: `4144` bytes
    - `ryu_decimal_interval_from_bits`: `2064` bytes
    - `ryu64_from_decimal_full` (tiny fallback build): `0` bytes
  - FULL / wasm32 (`RYU_TIER_FULL`, limbs=256, parse-bigint enabled):
    - `ryu64_from_decimal_full`: `7280` bytes
    - `ryu_convert_decimal_bigint_to_double`: `32` bytes
    - `ryu_choose_shortest_digits`: `4144` bytes
  - FULL / native (`RYU_TIER_FULL`, limbs=512, parse-bigint enabled):
    - `ryu64_from_decimal_full`: `14560` bytes
    - `ryu_convert_decimal_bigint_to_double`: `112` bytes
    - `ryu_choose_shortest_digits`: `8384` bytes
    - `ryu_decimal_interval_from_bits`: `4192` bytes
- Lowering `RYU_BIGINT_MAX_LIMBS` reduces stack/object footprint, but also lowers long-mantissa coverage (more `RYU_PARSE_OUT_OF_RANGE` for very long inputs).
- Override example:
  - `make full CFLAGS_BASE='-std=c11 -O2 -Wall -Wextra -Wpedantic -Iinclude -DRYU_BIGINT_MAX_LIMBS=768u'`

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
make shootout
make shootout-bench
make shootout-report
make wasm-compare
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
  - shortest minimality checks (no shorter-significand candidate around each shortest result may round-trip to the same bits)
- Oracle NaN strategy:
  - generic printf differential skips NaN because libc NaN sign/spelling behavior is implementation-defined across platforms
  - dedicated `printf-nan-policy` checks enforce this project's explicit NaN policy
- It includes deterministic stress datasets and random fuzzing, and reports timing.
  - deterministic stress includes dense low/high subnormal windows and a broad deterministic subnormal scatter set
- Shootout reporting:
  - `make shootout-report` emits automatic all-pairs size and performance breakdowns for:
    - `ryu64 native`
    - `ryu64 wasm mvp`
    - `ryu64 wasm gcplus`
    - `snprintf native`
    - `snprintf wasm mvp`
    - `snprintf wasm gcplus`
  - performance methodology (`shootout/deep_bench.c`):
    - single in-process harness with a narrow interface (`double` in, decimal text out) that runs every candidate in one executable
    - deterministic corpus built once per run from:
      - canonical IEEE-754 edge set (signed zeros, min/max finite, normal/subnormal boundaries, infinities, NaN patterns)
      - neighbors around powers of two across binary64 exponent range
      - neighbors around powers of ten across decimal exponent range
      - decimal-literal-origin hard cases
      - crafted raw bit-pattern buckets (exponent/mantissa patterns and walking mantissa bits)
      - fixed-seed random raw bit patterns (with signaling NaNs canonicalized to quiet NaNs)
    - short warm-up pass before timing, then one timed format-only pass per candidate over identical corpus order
    - roundtrip validation runs as a separate untimed pass, with separate counters for numeric vs bit-exact failures
  - it also generates one self-contained HTML page with all report sections:
    - `build/reports/shootout_report.html`
  - machine-readable report files are written to:
    - `build/reports/shootout_size.tsv`
    - `build/reports/shootout_perf.tsv`
    - `build/reports/shootout_perf_failures.tsv`
  - longitudinal tracking:
    - `make shootout-report` automatically appends a snapshot row-set to:
      - `build/reports/shootout_history.tsv`
    - each snapshot records UTC timestamp, commit id (with `-dirty` suffix when applicable), program id/label, size bytes, ns/conv, numeric failures, bit-exact failures, conversion count, average output length, corpus size, warmup, random count, and seed
    - you can append without regenerating HTML via:
      - `make shootout-track`
  - deep perf knobs (override on make command line):
    - `SHOOTOUT_DEEP_RANDOM` (default `224624`)
    - `SHOOTOUT_DEEP_SEED` (default `0x9e3779b97f4a7c15`)
    - `SHOOTOUT_DEEP_WARMUP` (default `1000`)
    - `SHOOTOUT_DEEP_SAMPLES` (default `8`)
  - native speed-profile knobs:
    - `SHOOTOUT_NATIVE_ENABLE_POW5_CACHE` (default `1`) enables a clean-room pow5 stride cache in native shootout/deep builds
    - `SHOOTOUT_NATIVE_POW5_STRIDE` (default `16`) controls anchor spacing for that cache
  - wasm speed-profile knobs:
    - `SHOOTOUT_WASM_ENABLE_POW5_CACHE` (default `1`) enables the same clean-room pow5 stride cache for wasm shootout/deep builds
    - `SHOOTOUT_WASM_POW5_STRIDE` (default `16`) controls anchor spacing for that cache
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
  - current dual wasm targets:
    - `make wasm-mvp` (MVP-only validation target)
    - `make wasm-gcplus` (feature-enabled target for current wasmtime support)
  - performance caveat for GC+ tuning:
    - enabling bulk-memory features alone is not enough to get `memory.copy` / `memory.fill`.
    - with `-fno-builtin`, clang generally will not lower memory ops to bulk-memory instructions.
    - to test bulk-memory speedups, enable builtins for GC+ builds while keeping MVP builds strict (`wasm-tools validate --features=mvp`).
  - mvp libc benchmark note:
    - the mvp shootout binaries that use wasi libc apply `wasm-opt --llvm-memory-copy-fill-lowering` plus bulk-memory disable flags before MVP validation.

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
