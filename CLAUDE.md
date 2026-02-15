# ryu64-cleanroom

Clean-room, MIT-licensed C11 library for IEEE-754 binary64 (`double`) decimal formatting and parsing. Part of the **lambkin-lang** project.

## Primary objective

Provide a *smaller* `snprintf`-like formatter and `strtod`-like parser for `double` values than including libc/stdlib directly — while matching or outperforming `snprintf`/`strtod` on the supported cases. The parser is less mature than the formatter and needs more work.

## WebAssembly is the primary target

**Universal MVP Wasm** is the vital target for embedded contexts. The library must always compile and validate against the MVP instruction set (`wasm-tools validate --features=mvp`). **WasmGC-plus** is a secondary target that benefits from extended features (bulk-memory, etc.) in non-embedded Wasm runtimes.

Native builds are useful for development, testing, and benchmarking, but Wasm correctness and code size are the metrics that matter most.

## Strict allocation rules

- **No heap allocation** anywhere in library code. Ever.
- All buffers are caller-provided. All temporaries are stack-allocated or fixed-size.
- Stack budgets are measured with `-fstack-usage` and documented per tier. Keep them tight — Wasm runtimes have limited stack space.
- `RYU_BIGINT_MAX_LIMBS` is 256 on wasm32 (1 KB per bigint) vs 512 on native (2 KB). Lowering it reduces stack footprint but also long-mantissa coverage.
- Minimum supported `RYU_BIGINT_MAX_LIMBS` is 96 (compile-time guard).

## Clean-room protocol

This is a **clean-room implementation**. The full protocol is in `docs/CLEANROOM_PROTOCOL.md`. The key rules:

### Allowed references
- Ulf Adams, "Ryū: fast float-to-string conversion" (PLDI 2018)
- "Ryū revisited: printf floating point conversion" (ACM)
- Daniel Lemire et al., "Number Parsing at a Gigabyte per Second"
- POSIX/C specifications for printf/strtod behavior
- IEEE-754 binary64 format references

### Forbidden inputs
- **No** existing Ryū codebases, forks, or variants
- **No** libc parser/formatter source code (musl, glibc, Apple libc, etc.)
- **No** language runtime implementations (V8, Go, Rust std, etc.)
- **No** blog posts, gist code samples, or external table dumps

### Process
1. All consulted references must be recorded in `docs/IMPLEMENTATION_LOG.md`
2. Code is derived from papers and first-principles reasoning, not from existing implementations
3. Differential testing against libc (`snprintf`, `strtod`) is allowed **only in test binaries** — never in library code
4. If a mismatch is found, fix by re-deriving from specs/papers, not by copying libc behavior

### When making changes
- Do not introduce libc dependencies into `src/` or `include/` code paths
- Verify freestanding builds still work: `make wasm-tiny`, `make nolibc-check-speed`, `make nolibc-check-size`
- Update `docs/IMPLEMENTATION_LOG.md` if consulting any new reference
- New `.c`/`.h` files must include the MIT license header

## Build tiers

| Tier | Macro | Capabilities | Bigint limbs |
|------|-------|-------------|--------------|
| **TINY** | `RYU_TIER_TINY` | shortest + 9sig formatters, tiny parser | 256 (wasm) |
| **FULL** | `RYU_TIER_FULL` | + printf-style formatter, full bigint parser | 512 (native) / 256 (wasm) |
| **TEST** | `RYU_TIER_TEST` | FULL + libc oracle harnesses | same as FULL |

## Key commands

```
make test              # Build + run unit tests + quick oracle checks
make oracle-test       # Run libc differential oracle suite
make wasm-tiny         # Compile-only wasm32 tiny check
make wasm-mvp          # Build MVP WASI executable
make wasm-gcplus       # Build GC+ WASI executable
make nolibc-check-speed  # Verify freestanding compilation (-O3)
make nolibc-check-size   # Verify freestanding compilation (-Oz)
make shootout-report   # Generate HTML/TSV benchmark reports
```

## Project layout

```
include/ryu64.h          — Public API (the only header consumers include)
src/ryu64_internal.h     — Internal types and bigint API
src/ryu64_shortest.c     — Shortest round-trip formatter
src/ryu64_9sig.c         — 9-significant-digit compact formatter
src/ryu64_printf.c       — Printf-style formatter (FULL tier only)
src/ryu64_bigint.c       — Arbitrary-precision integer arithmetic
src/ryu64_parse_tiny.c   — Bounded decimal parser (no heap, ≤19 digits)
src/ryu64_parse_full.c   — Full-range bigint-backed parser
src/ryu64_parse_tables.c — Precomputed powers-of-ten constants
test/test_ryu64.c        — Unit tests (bigint, formatter, parser, buffer edge cases)
test/oracle_stdio.c      — Differential oracle tests vs libc
shootout/deep_bench.c    — Performance benchmarking harness
wasm_compat/             — Freestanding memcpy/memmove/memcmp/memset/strlen
wasi/                    — WASI entry point and I/O primitives
tools/gen_pow10_u128.c   — Reproducible table generator
docs/CLEANROOM_PROTOCOL.md
docs/IMPLEMENTATION_LOG.md
```

## Coding conventions

- C11 standard. Portable across native and wasm32.
- Public API functions use `ryu64_*` prefix. Internal functions use `ryu_*` prefix.
- No dynamic allocation. No locale dependency. ASCII C-locale only.
- The `wasm_compat` layer provides `memcpy`/`memmove`/`memcmp`/`memset`/`strlen` for freestanding builds — these are intentionally simple byte-wise implementations, not speed-optimized.
- Avoid adding libc headers to library source files. `<stdint.h>` and `<stddef.h>` (or wasm_compat equivalents) are acceptable.
- Test functions return 1 on success, 0 on failure, with `fprintf(stderr, ...)` diagnostics.

## Performance notes

- The formatter uses bigint interval arithmetic (not constant-time O(1) table lookup). This is an intentional clean-room tradeoff for auditability.
- The optional `RYU_ENABLE_POW5_STRIDE_CACHE` accelerates formatting in benchmarks by precomputing pow5 anchors. This uses static memory and atomics — appropriate for native benchmarks, not for the minimal wasm library.
- Benchmark with `make shootout-report`. Reports go to `build/reports/`.
