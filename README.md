# ryu64-cleanroom
`ryu64-cleanroom` is a clean-room, MIT-licensed implementation of the Ryu family of float-to-string conversion algorithms for IEEE-754 binary64 (`double`), written in portable C with WebAssembly translation in mind.

The library is organized into build tiers to support different consumers:

- Tiny / no-libc environments (including WebAssembly MVP and embedded targets) where `snprintf` and even `strtod` may not exist.
- Host environments that can run extensive correctness and compatibility tests using libc as an oracle.
- Full-featured targets that want printf-compatible formatting (`%f`, `%e`/`%E`, `%g`/`%G`) without depending on libc at runtime.

Key goals:

- Clean-room provenance: implemented from the peer-reviewed Ryu papers and formatting specifications, with reference implementations used only for post-freeze cross-validation.
- Deterministic output under C-locale assumptions (decimal point `.`; no thousands grouping).
- No heap allocation; caller-provided buffers; explicit status codes.
- Wasm-friendly portability: fixed-width integers, careful avoidance of undefined behavior, and optional fallbacks when 128-bit arithmetic is unavailable.

Planned APIs include:

- Shortest / round-trip safe formatting for `double`.
- A small, fast mode capped at 9 significant digits (policy documented in the API).
- Optional printf-style formatting compatible with common `snprintf` behavior for `%f`/`%e`/`%g`.

This project is intended as a practical, embeddable formatting core for runtimes that need predictable float64 text output across native and WebAssembly targets under a permissive MIT license.
