#!/usr/bin/env bash
# shootout_perf.sh — Run deep benchmarks and write perf TSV reports.
# Called by: make shootout-report-perf
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"
SHOOTOUT_REPORT_DIR="${SHOOTOUT_REPORT_DIR:-build/reports}"
SHOOTOUT_PERF_REPORT="${SHOOTOUT_PERF_REPORT:-$SHOOTOUT_REPORT_DIR/shootout_perf.tsv}"
SHOOTOUT_PERF_FAILURE_REPORT="${SHOOTOUT_PERF_FAILURE_REPORT:-$SHOOTOUT_REPORT_DIR/shootout_perf_failures.tsv}"
WASMTIME="${WASMTIME:-wasmtime}"
# SHOOTOUT_DEEP_ARGS is intentionally word-split when passed to benchmarks

mkdir -p "$SHOOTOUT_REPORT_DIR"

tab="$(printf '\t')"
native_out="$(mktemp)"
mvp_out="$(mktemp)"
gcplus_out="$(mktemp)"
trap 'rm -f "$native_out" "$mvp_out" "$gcplus_out"' EXIT INT TERM

# shellcheck disable=SC2086
"./$BUILD_DIR/shootout_deep_native" $SHOOTOUT_DEEP_ARGS > "$native_out"
# shellcheck disable=SC2086
"$WASMTIME" run "$BUILD_DIR/shootout_deep_mvp.wasm" -- $SHOOTOUT_DEEP_ARGS > "$mvp_out"
# shellcheck disable=SC2086
"$WASMTIME" run "$BUILD_DIR/shootout_deep_gcplus.wasm" -- $SHOOTOUT_DEEP_ARGS > "$gcplus_out"

extract_corpus() {
    local file="$1"
    local corpus=""
    while IFS="$tab" read -r tag c1 _; do
        if [ "$tag" = "CORPUS" ]; then corpus="$c1"; break; fi
    done < "$file"
    printf "%s" "$corpus"
}

corpus_native="$(extract_corpus "$native_out")"
corpus_mvp="$(extract_corpus "$mvp_out")"
corpus_gcplus="$(extract_corpus "$gcplus_out")"

if [ -z "$corpus_native" ] || [ "$corpus_native" != "$corpus_mvp" ] || [ "$corpus_native" != "$corpus_gcplus" ]; then
    echo "shootout deep perf error: corpus mismatch across profiles"
    exit 1
fi

printf "id\tlabel\tns_per_conv\telapsed_ns\tconversions\tavg_len\tnumeric_fail\tbit_fail\tparse_fail\tformat_fail\tcorpus_size\twarmup\trandom_count\tseed\n" > "$SHOOTOUT_PERF_REPORT"
printf "profile\tcandidate\tfailure_type\tinput_bits\tparsed_bits\ttext\n" > "$SHOOTOUT_PERF_FAILURE_REPORT"

append_profile() {
    local profile="$1"
    local file="$2"
    local corpus="" warmup="" random_count="" seed=""
    while IFS="$tab" read -r tag f1 f2 f3 f4 f5 f6 f7 f8 f9 f10; do
        case "$tag" in
            CORPUS)
                corpus="$f1"; warmup="$f2"; random_count="$f3"; seed="$f4"
                ;;
            RESULT)
                local candidate="$f1" conversions="$f2" elapsed_ns="$f3" ns_per_conv="$f4" avg_len="$f5"
                local numeric_fail="$f6" bit_fail="$f7" parse_fail="$f8" format_fail="$f9"
                local id="" label=""
                case "$profile:$candidate" in
                    native:ryu64)        id="ryu64_native";           label="ryu64 native" ;;
                    native:snprintf)     id="snprintf_native";        label="snprintf native" ;;
                    native:ryu64_rt)     id="ryu64_rt_native";        label="ryu64 roundtrip native" ;;
                    native:snprintf_rt)  id="snprintf_rt_native";     label="snprintf roundtrip native" ;;
                    wasm_mvp:ryu64)      id="ryu64_wasm_mvp";         label="ryu64 wasm mvp" ;;
                    wasm_mvp:snprintf)   id="snprintf_wasm_mvp";      label="snprintf wasm mvp" ;;
                    wasm_mvp:ryu64_rt)   id="ryu64_rt_wasm_mvp";      label="ryu64 roundtrip wasm mvp" ;;
                    wasm_mvp:snprintf_rt) id="snprintf_rt_wasm_mvp";  label="snprintf roundtrip wasm mvp" ;;
                    wasm_gcplus:ryu64)   id="ryu64_wasm_gcplus";      label="ryu64 wasm gcplus" ;;
                    wasm_gcplus:snprintf) id="snprintf_wasm_gcplus";  label="snprintf wasm gcplus" ;;
                    wasm_gcplus:ryu64_rt) id="ryu64_rt_wasm_gcplus";  label="ryu64 roundtrip wasm gcplus" ;;
                    wasm_gcplus:snprintf_rt) id="snprintf_rt_wasm_gcplus"; label="snprintf roundtrip wasm gcplus" ;;
                    *) id=""; label="" ;;
                esac
                [ -z "$id" ] && continue
                printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" \
                    "$id" "$label" "$ns_per_conv" "$elapsed_ns" "$conversions" "$avg_len" "$numeric_fail" "$bit_fail" "$parse_fail" "$format_fail" "$corpus" "$warmup" "$random_count" "$seed" >> "$SHOOTOUT_PERF_REPORT"
                ;;
            FAIL)
                printf "%s\t%s\t%s\t%s\t%s\t%s\n" "$profile" "$f1" "$f2" "$f3" "$f4" "$f5" >> "$SHOOTOUT_PERF_FAILURE_REPORT"
                ;;
        esac
    done < "$file"
}

append_profile native "$native_out"
append_profile wasm_mvp "$mvp_out"
append_profile wasm_gcplus "$gcplus_out"

# --- print summary ---
echo ""
echo "=== shootout deep perf (single in-process pass per profile) ==="
while IFS="$tab" read -r id label ns elapsed conv avg num bit parse fmt corpus warmup random_count seed; do
    [ "$id" = "id" ] && continue
    printf "  %-24s %10s ns/conv avg_len=%s fail(num/bit/parse/fmt)=%s/%s/%s/%s\n" "${label}:" "$ns" "$avg" "$num" "$bit" "$parse" "$fmt"
done < "$SHOOTOUT_PERF_REPORT"
printf "  report file: %s\n" "$SHOOTOUT_PERF_REPORT"
printf "  failure samples: %s\n" "$SHOOTOUT_PERF_FAILURE_REPORT"
