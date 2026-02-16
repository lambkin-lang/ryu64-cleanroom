#!/usr/bin/env bash
# stride_report.sh — Parametric pow5 stride investigation: build, benchmark, report.
# Called by: make stride-investigation-data (data), make stride-investigation-html (html)
# Usage: tools/stride_report.sh [data|html|all]
set -euo pipefail

mode="${1:-all}"

# --- common env vars ---
SHOOTOUT_REPORT_DIR="${SHOOTOUT_REPORT_DIR:-build/reports}"
STRIDE_INVESTIGATION_TSV="${STRIDE_INVESTIGATION_TSV:-$SHOOTOUT_REPORT_DIR/stride_investigation.tsv}"
STRIDE_INVESTIGATION_HTML="${STRIDE_INVESTIGATION_HTML:-$SHOOTOUT_REPORT_DIR/stride_investigation.html}"

# --- data collection ---
collect_data() {
    STRIDE_INVESTIGATION_DIR="${STRIDE_INVESTIGATION_DIR:-build/stride-investigation}"
    STRIDE_INVESTIGATION_VALUES="${STRIDE_INVESTIGATION_VALUES:-0 4 8 16 32 64 128}"
    WASMTIME="${WASMTIME:-wasmtime}"
    CC="${CC:-cc}"
    # WASI_CC_CMD, WASM_OPT, WASM_OPT_POST_FLAGS, SHOOTOUT_WASI_MVP_LDFLAGS,
    # SIZE_LDFLAGS, LIBS_MATH, WASI_BUILTINS, STRIDE_NATIVE_BASE_CFLAGS,
    # STRIDE_WASI_FMT_BASE_CFLAGS, STRIDE_WASI_DEEP_BASE_CFLAGS,
    # SRC_FMT, SRC, SHOOTOUT_DEEP_ARGS are expected from the environment.

    mkdir -p "$STRIDE_INVESTIGATION_DIR" "$SHOOTOUT_REPORT_DIR"
    printf "stride\tplatform\tfmt_code_bytes\tns_per_conv\tns_per_conv_rt\tsnprintf_ns\tsnprintf_rt_ns\tanchor_count\testimated_static_bytes\n" > "$STRIDE_INVESTIGATION_TSV"

    snp_ns_n="" snp_ns_w="" snp_rt_n="" snp_rt_w=""

    # shellcheck disable=SC2086
    for stride in $STRIDE_INVESTIGATION_VALUES; do
        printf "\n=== stride-investigation: stride=%s ===\n" "$stride"
        vdir="$STRIDE_INVESTIGATION_DIR/s$stride"
        mkdir -p "$vdir"

        if [ "$stride" = "0" ]; then
            pow5="" anchors=0 mem_n=0 mem_w=0
        else
            pow5="-DRYU_ENABLE_POW5_STRIDE_CACHE=1 -DRYU_POW5_STRIDE=$stride"
            anchors=$(( (1074 / stride) + 1 ))
            mem_n=$(( anchors * 2056 ))
            mem_w=$(( anchors * 1028 ))
        fi

        printf "  building format-only native...\n"
        # shellcheck disable=SC2086
        $CC $STRIDE_NATIVE_BASE_CFLAGS $pow5 \
            -Iinclude -Ishootout \
            $SRC_FMT shootout/ryu64_print.c \
            -o "$vdir/ryu64_native" $SIZE_LDFLAGS
        strip "$vdir/ryu64_native"

        printf "  building format-only wasm mvp...\n"
        # shellcheck disable=SC2086
        $WASI_CC_CMD $STRIDE_WASI_FMT_BASE_CFLAGS $pow5 \
            -DRYU64_WASI_BUILD \
            -Iwasm_compat -Iinclude -Iwasi -Ishootout \
            $SRC_FMT shootout/ryu64_print.c wasi/wasi_start.c wasm_compat/string.c \
            $WASI_BUILTINS -o "$vdir/ryu64_mvp_raw.wasm" \
            $SHOOTOUT_WASI_MVP_LDFLAGS
        # shellcheck disable=SC2086
        $WASM_OPT $WASM_OPT_POST_FLAGS --strip-target-features \
            "$vdir/ryu64_mvp_raw.wasm" -o "$vdir/ryu64_mvp.wasm"
        wasm-tools validate --features=mvp "$vdir/ryu64_mvp.wasm"

        printf "  building deep bench native...\n"
        # shellcheck disable=SC2086
        $CC $STRIDE_NATIVE_BASE_CFLAGS $pow5 \
            -DRYU64_ENABLE_PARSE_BIGINT -Iinclude -Ishootout \
            $SRC shootout/deep_bench.c \
            -o "$vdir/deep_native" $SIZE_LDFLAGS $LIBS_MATH
        strip "$vdir/deep_native"

        printf "  building deep bench wasm mvp...\n"
        # shellcheck disable=SC2086
        $WASI_CC_CMD $STRIDE_WASI_DEEP_BASE_CFLAGS $pow5 \
            -DRYU64_ENABLE_PARSE_BIGINT -Iinclude -Ishootout \
            $SRC shootout/deep_bench.c \
            -o "$vdir/deep_mvp_raw.wasm" \
            -Wl,--gc-sections -Wl,--strip-all -flto
        # shellcheck disable=SC2086
        $WASM_OPT $WASM_OPT_POST_FLAGS --strip-target-features \
            --disable-bulk-memory --disable-bulk-memory-opt \
            --llvm-memory-copy-fill-lowering \
            "$vdir/deep_mvp_raw.wasm" -o "$vdir/deep_mvp.wasm"
        wasm-tools validate --features=mvp "$vdir/deep_mvp.wasm"

        native_size=$(wc -c < "$vdir/ryu64_native" | tr -d ' ')
        wasm_size=$(wc -c < "$vdir/ryu64_mvp.wasm" | tr -d ' ')

        printf "  running deep bench native...\n"
        n_out=$(mktemp)
        # shellcheck disable=SC2086
        "$vdir/deep_native" $SHOOTOUT_DEEP_ARGS > "$n_out"

        printf "  running deep bench wasm mvp...\n"
        w_out=$(mktemp)
        # shellcheck disable=SC2086
        "$WASMTIME" run "$vdir/deep_mvp.wasm" -- $SHOOTOUT_DEEP_ARGS > "$w_out"

        ryu_n=$(awk -F'\t' '$1=="RESULT" && $2=="ryu64" {print $5}' "$n_out")
        ryu_rt_n=$(awk -F'\t' '$1=="RESULT" && $2=="ryu64_rt" {print $5}' "$n_out")
        ryu_w=$(awk -F'\t' '$1=="RESULT" && $2=="ryu64" {print $5}' "$w_out")
        ryu_rt_w=$(awk -F'\t' '$1=="RESULT" && $2=="ryu64_rt" {print $5}' "$w_out")

        if [ "$stride" = "0" ]; then
            snp_ns_n=$(awk -F'\t' '$1=="RESULT" && $2=="snprintf" {print $5}' "$n_out")
            snp_ns_w=$(awk -F'\t' '$1=="RESULT" && $2=="snprintf" {print $5}' "$w_out")
            snp_rt_n=$(awk -F'\t' '$1=="RESULT" && $2=="snprintf_rt" {print $5}' "$n_out")
            snp_rt_w=$(awk -F'\t' '$1=="RESULT" && $2=="snprintf_rt" {print $5}' "$w_out")
        fi

        rm -f "$n_out" "$w_out"

        printf "%s\tnative\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" \
            "$stride" "$native_size" "$ryu_n" "$ryu_rt_n" "$snp_ns_n" "$snp_rt_n" "$anchors" "$mem_n" \
            >> "$STRIDE_INVESTIGATION_TSV"
        printf "%s\twasm_mvp\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" \
            "$stride" "$wasm_size" "$ryu_w" "$ryu_rt_w" "$snp_ns_w" "$snp_rt_w" "$anchors" "$mem_w" \
            >> "$STRIDE_INVESTIGATION_TSV"
        printf "  stride=%-3s  native: %6s bytes, %s ns/conv  wasm: %6s bytes, %s ns/conv\n" \
            "$stride" "$native_size" "$ryu_n" "$wasm_size" "$ryu_w"
    done

    echo ""
    echo "=== stride investigation data complete ==="
    printf "  tsv report: %s\n" "$STRIDE_INVESTIGATION_TSV"
}

# --- HTML generation ---
generate_html() {
    mkdir -p "$SHOOTOUT_REPORT_DIR"

    tab=$(printf '\t')
    generated=$(date -u '+%Y-%m-%d %H:%M:%S UTC')
    base_n="" base_w="" base_rt_n="" base_rt_w=""

    while IFS="$tab" read -r stride platform bytes ns ns_rt snp snp_rt anchors mem; do
        [ "$stride" = "stride" ] && continue
        if [ "$stride" = "0" ]; then
            case "$platform" in
                native)   base_n="$ns"; base_rt_n="$ns_rt" ;;
                wasm_mvp) base_w="$ns"; base_rt_w="$ns_rt" ;;
            esac
        fi
    done < "$STRIDE_INVESTIGATION_TSV"

    emit_table() {
        local plat_filter="$1"
        local plat_label="$2"
        local bns="$3"
        local brt="$4"

        printf '<h2>%s</h2>\n' "$plat_label"
        printf '%s\n' \
            '<div class="panel"><table><thead><tr>' \
            '<th>Stride</th>' \
            '<th class="num">Code Size (bytes)</th>' \
            '<th class="num">ns/conv (fmt)</th>' \
            '<th class="num">Speedup</th>' \
            '<th class="num">ns/conv (rt)</th>' \
            '<th class="num">RT Speedup</th>' \
            '<th class="num">snprintf ref</th>' \
            '<th class="num">Anchors</th>' \
            '<th class="num">Static Memory (bytes)</th>' \
            '</tr></thead><tbody>'

        while IFS="$tab" read -r stride platform bytes ns ns_rt snp snp_rt anchors mem; do
            [ "$stride" = "stride" ] && continue
            [ "$platform" != "$plat_filter" ] && continue
            if [ "$stride" = "0" ]; then
                slabel="disabled"; speedup="1.00x"; rt_speedup="1.00x"
            else
                slabel="$stride"
                speedup=$(awk "BEGIN{printf \"%.2fx\",$bns/$ns}")
                rt_speedup=$(awk "BEGIN{printf \"%.2fx\",$brt/$ns_rt}")
            fi
            printf '<tr><td>%s</td><td class="num js-int" data-int="%s">%s</td><td class="num js-fixed1" data-float="%s">%s</td><td class="num">%s</td><td class="num js-fixed1" data-float="%s">%s</td><td class="num">%s</td><td class="num js-fixed1" data-float="%s">%s</td><td class="num">%s</td><td class="num js-int" data-int="%s">%s</td></tr>\n' \
                "$slabel" "$bytes" "$bytes" "$ns" "$ns" "$speedup" "$ns_rt" "$ns_rt" "$rt_speedup" "$snp" "$snp" "$anchors" "$mem" "$mem"
        done < "$STRIDE_INVESTIGATION_TSV"

        printf '</tbody></table></div>\n'
    }

    {
        printf '%s\n' \
            '<!doctype html>' \
            '<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">' \
            '<title>Stride Investigation Report</title>' \
            '<style>' \
            'body{font-family:"Avenir Next","Segoe UI","Helvetica Neue",Arial,sans-serif;max-width:1220px;margin:28px auto;padding:0 18px;line-height:1.45;color:#18212a;background:#fafbfc}' \
            'h1{margin:0 0 10px;font-size:1.6rem;letter-spacing:0.01em}' \
            'h2{margin:20px 0 8px;font-size:1.14rem;letter-spacing:0.01em;color:#1e2b38}' \
            '.meta{color:#4a5a6a;margin-bottom:14px}' \
            '.panel{background:#fff;border:1px solid #dce3ea;border-radius:10px;box-shadow:0 6px 18px rgba(30,42,56,0.06);overflow:hidden}' \
            'table{border-collapse:collapse;width:100%}' \
            'thead th{background:linear-gradient(180deg,#f5f8fb,#eef3f8);font-weight:600;color:#1a2733}' \
            'th,td{border-bottom:1px solid #e6edf3;padding:10px 11px;text-align:left;vertical-align:top}' \
            'tbody tr:nth-child(even){background:#fcfdff}' \
            '.num{white-space:nowrap;font-variant-numeric:tabular-nums;text-align:right}' \
            '.note{font-size:0.95rem;color:#405468;margin-top:10px}' \
            '.highlight{background:#e8f5e9}' \
            '.muted{color:#617487}' \
            '</style></head><body>' \
            '<h1>Stride Investigation</h1>'
        printf '<div class="meta">pow5 cache size/speed tradeoff &mdash; Generated: %s</div>\n' "$generated"

        emit_table "native" "Native" "$base_n" "$base_rt_n"
        emit_table "wasm_mvp" "Wasm MVP" "$base_w" "$base_rt_w"

        printf '%s\n' \
            '<div class="note"><strong>Stride</strong>: the pow5 cache precomputes 5^(k&times;stride) for k=0,1,&hellip;,(1074/stride). Smaller stride = more anchors = faster lookup but more static memory. &ldquo;disabled&rdquo; means no cache (baseline).</div>' \
            '<div class="note"><strong>Code Size</strong>: format-only binary (ryu64_print). The cache adds three internal functions (~115 lines) plus a static anchor array.</div>' \
            '<div class="note"><strong>Static Memory</strong>: the anchor array lives in .bss (zero-init). On wasm32 each ryu_bigint is 1,028 bytes (256 limbs); on native-64 it is 2,056 bytes (512 limbs).</div>' \
            '<div class="note"><strong>Speedup</strong>: ratio of no-cache ns/conv to cached ns/conv. Higher is better.</div>' \
            '<div class="note muted">snprintf reference is captured once from the no-cache run; it is unaffected by the stride cache.</div>' \
            '<script>(function(){' \
            'var f=new Intl.NumberFormat("en-US"),f1=new Intl.NumberFormat("en-US",{minimumFractionDigits:1,maximumFractionDigits:1});' \
            'document.querySelectorAll(".js-int").forEach(function(e){var v=Number(e.dataset.int);if(isFinite(v))e.textContent=f.format(v)});' \
            'document.querySelectorAll(".js-fixed1").forEach(function(e){var v=Number(e.dataset.float);if(isFinite(v))e.textContent=f1.format(v)});' \
            '})();</script>' \
            '</body></html>'
    } > "$STRIDE_INVESTIGATION_HTML"

    printf "  html report: %s\n" "$STRIDE_INVESTIGATION_HTML"
}

case "$mode" in
    data) collect_data ;;
    html) generate_html ;;
    all)  collect_data; generate_html ;;
    *)    echo "usage: $0 [data|html|all]" >&2; exit 1 ;;
esac
