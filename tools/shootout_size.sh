#!/usr/bin/env bash
# shootout_size.sh — Measure shootout binary sizes and write TSV reports.
# Called by: make shootout-report-size
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"
SHOOTOUT_REPORT_DIR="${SHOOTOUT_REPORT_DIR:-build/reports}"
SHOOTOUT_SIZE_REPORT="${SHOOTOUT_SIZE_REPORT:-$SHOOTOUT_REPORT_DIR/shootout_size.tsv}"
SHOOTOUT_SIZE_ROUNDTRIP_REPORT="${SHOOTOUT_SIZE_ROUNDTRIP_REPORT:-$SHOOTOUT_REPORT_DIR/shootout_size_roundtrip.tsv}"

mkdir -p "$SHOOTOUT_REPORT_DIR"

measure_size() { wc -c < "$1" | tr -d ' '; }

# --- format-only size report ---
{
    printf "id\tlabel\tbytes\n"
    printf "ryu64_native\tryu64 native\t%s\n"           "$(measure_size "$BUILD_DIR/shootout_ryu64_native")"
    printf "ryu64_wasm_mvp\tryu64 wasm mvp\t%s\n"       "$(measure_size "$BUILD_DIR/shootout_ryu64_mvp.wasm")"
    printf "ryu64_wasm_gcplus\tryu64 wasm gcplus\t%s\n"  "$(measure_size "$BUILD_DIR/shootout_ryu64_gcplus.wasm")"
    printf "snprintf_native\tsnprintf native\t%s\n"      "$(measure_size "$BUILD_DIR/shootout_snprintf_native")"
    printf "snprintf_wasm_mvp\tsnprintf wasm mvp\t%s\n"  "$(measure_size "$BUILD_DIR/shootout_snprintf_mvp.wasm")"
    printf "snprintf_wasm_gcplus\tsnprintf wasm gcplus\t%s\n" "$(measure_size "$BUILD_DIR/shootout_snprintf_gcplus.wasm")"
} > "$SHOOTOUT_SIZE_REPORT"

# --- roundtrip size report ---
{
    printf "id\tlabel\tbytes\n"
    printf "ryu64_rt_native\tryu64 roundtrip native\t%s\n"           "$(measure_size "$BUILD_DIR/shootout_ryu64_rt_native")"
    printf "ryu64_rt_wasm_mvp\tryu64 roundtrip wasm mvp\t%s\n"       "$(measure_size "$BUILD_DIR/shootout_ryu64_rt_mvp.wasm")"
    printf "ryu64_rt_wasm_gcplus\tryu64 roundtrip wasm gcplus\t%s\n"  "$(measure_size "$BUILD_DIR/shootout_ryu64_rt_gcplus.wasm")"
    printf "snprintf_rt_native\tsnprintf roundtrip native\t%s\n"      "$(measure_size "$BUILD_DIR/shootout_snprintf_rt_native")"
    printf "snprintf_rt_wasm_mvp\tsnprintf roundtrip wasm mvp\t%s\n"  "$(measure_size "$BUILD_DIR/shootout_snprintf_rt_mvp.wasm")"
    printf "snprintf_rt_wasm_gcplus\tsnprintf roundtrip wasm gcplus\t%s\n" "$(measure_size "$BUILD_DIR/shootout_snprintf_rt_gcplus.wasm")"
} > "$SHOOTOUT_SIZE_ROUNDTRIP_REPORT"

# --- print summary ---
tab="$(printf '\t')"

echo ""
echo "=== shootout sizes (format-only) ==="
while IFS="$tab" read -r id label bytes; do
    [ "$id" = "id" ] && continue
    printf "  %-24s %8s bytes\n" "${label}:" "$bytes"
done < "$SHOOTOUT_SIZE_REPORT"
printf "  report file: %s\n" "$SHOOTOUT_SIZE_REPORT"

echo "=== shootout sizes (roundtrip) ==="
while IFS="$tab" read -r id label bytes; do
    [ "$id" = "id" ] && continue
    printf "  %-24s %8s bytes\n" "${label}:" "$bytes"
done < "$SHOOTOUT_SIZE_ROUNDTRIP_REPORT"
printf "  report file: %s\n" "$SHOOTOUT_SIZE_ROUNDTRIP_REPORT"
