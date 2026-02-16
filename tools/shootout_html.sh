#!/usr/bin/env bash
# shootout_html.sh — Generate HTML shootout report from size + perf TSV data.
# Called by: make shootout-report-html
set -euo pipefail

SHOOTOUT_REPORT_DIR="${SHOOTOUT_REPORT_DIR:-build/reports}"
SHOOTOUT_SIZE_REPORT="${SHOOTOUT_SIZE_REPORT:-$SHOOTOUT_REPORT_DIR/shootout_size.tsv}"
SHOOTOUT_SIZE_ROUNDTRIP_REPORT="${SHOOTOUT_SIZE_ROUNDTRIP_REPORT:-$SHOOTOUT_REPORT_DIR/shootout_size_roundtrip.tsv}"
SHOOTOUT_PERF_REPORT="${SHOOTOUT_PERF_REPORT:-$SHOOTOUT_REPORT_DIR/shootout_perf.tsv}"
SHOOTOUT_HTML_REPORT="${SHOOTOUT_HTML_REPORT:-$SHOOTOUT_REPORT_DIR/shootout_report.html}"
# SHOOTOUT_FLAGS_MAP: newline-separated "id\tflags_description" lines (set by Makefile)
# SHOOTOUT_COMMON_NOTE: note about common flags across all programs (set by Makefile)
# SHOOTOUT_WASM_OPT_NOTE: note about wasm-opt post-processing (set by Makefile)

mkdir -p "$SHOOTOUT_REPORT_DIR"

tab="$(printf '\t')"

lookup_flags() {
    local target_id="$1"
    local line=""
    printf '%s\n' "$SHOOTOUT_FLAGS_MAP" | while IFS="$tab" read -r fid fdesc; do
        if [ "$fid" = "$target_id" ]; then
            printf '%s' "$fdesc"
            return
        fi
    done
}

# emit_table_section SIZE_TSV_PATH SECTION_TITLE
# Reads size TSV, joins with perf data, looks up flags, emits HTML table rows.
emit_table_section() {
    local size_tsv="$1"
    local section_title="$2"

    printf '<h2>%s</h2>\n' "$section_title"
    printf '%s\n' \
        '<div class="panel">' \
        '<table><thead><tr><th>Program</th><th class="num">Size (bytes)</th><th class="num">ns/conv</th><th>Flags (compiler/linker flags)</th><th class="num">numeric failures</th><th class="num">bit-exact failures</th><th class="num">average characters</th></tr></thead><tbody>'

    while IFS="$tab" read -r id label bytes; do
        [ "$id" = "id" ] && continue
        ns_per_conv="" conversions="" avg_len="" numeric_fail="" bit_fail=""
        while IFS="$tab" read -r perf_id perf_label ns elapsed conv avg num bit parse fmt corpus warm rand seed; do
            [ "$perf_id" = "id" ] && continue
            if [ "$perf_id" = "$id" ]; then
                ns_per_conv="$ns"; conversions="$conv"; avg_len="$avg"; numeric_fail="$num"; bit_fail="$bit"
                break
            fi
        done < "$SHOOTOUT_PERF_REPORT"
        [ -z "$ns_per_conv" ] && continue
        flags="$(lookup_flags "$id")"
        [ -z "$flags" ] && flags="n/a"
        ncls="warn"; [ "$numeric_fail" = "0" ] && ncls="ok"
        bcls="warn"; [ "$bit_fail" = "0" ] && bcls="ok"
        printf '<tr><td>%s</td><td class="num js-int" data-int="%s">%s</td><td class="num js-fixed1" data-float="%s">%s</td><td class="flags">%s</td><td class="num %s js-ratio" data-num="%s" data-den="%s">%s/%s</td><td class="num %s js-ratio" data-num="%s" data-den="%s">%s/%s</td><td class="num js-fixed3" data-float="%s">%s</td></tr>\n' \
            "$label" "$bytes" "$bytes" "$ns_per_conv" "$ns_per_conv" "$flags" "$ncls" "$numeric_fail" "$conversions" "$numeric_fail" "$conversions" "$bcls" "$bit_fail" "$conversions" "$bit_fail" "$conversions" "$avg_len" "$avg_len"
    done < "$size_tsv"

    printf '%s\n' '</tbody></table>' '</div>'
}

# --- extract corpus metadata from first perf row ---
corpus_size="0" warmup="0" random_count="0" seed="n/a"
while IFS="$tab" read -r id label ns elapsed conv avg num bit parse fmt corpus warm rand seed_value; do
    [ "$id" = "id" ] && continue
    corpus_size="$corpus"; warmup="$warm"; random_count="$rand"; seed="$seed_value"
    break
done < "$SHOOTOUT_PERF_REPORT"

generated="$(date -u '+%Y-%m-%d %H:%M:%S UTC')"

# --- write HTML report ---
{
    printf '%s\n' \
        '<!doctype html>' \
        '<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">' \
        '<title>Shootout Report</title>' \
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
        '.flags{max-width:620px;color:#243445}' \
        '.note{font-size:0.95rem;color:#405468;margin-top:10px}' \
        '.ok{color:#1f6f43;font-weight:600}' \
        '.warn{color:#8a4b00;font-weight:600}' \
        '.muted{color:#617487}' \
        '</style></head><body>' \
        '<h1>Shootout Report</h1>'
    printf '<div class="meta">Generated: %s</div>\n' "$generated"

    emit_table_section "$SHOOTOUT_SIZE_REPORT" "Format-only"
    emit_table_section "$SHOOTOUT_SIZE_ROUNDTRIP_REPORT" "Roundtrip (format + parse)"

    printf '<div class="note">%s</div>\n' "$SHOOTOUT_COMMON_NOTE"
    printf '<div class="note">%s</div>\n' "$SHOOTOUT_WASM_OPT_NOTE"
    printf '%s\n' \
        '<div class="note">Format-only table: ns/conv reports the timed formatter pass; roundtrip checks run in a separate untimed validation pass.</div>' \
        '<div class="note">Roundtrip table: ns/conv reports one timed pass that includes formatting and parsing for each candidate pair.</div>'
    printf '<div class="note">Deep benchmark corpus: <span class="js-int" data-int="%s">%s</span> values; warmup: <span class="js-int" data-int="%s">%s</span>; random subset: <span class="js-int" data-int="%s">%s</span>; seed: %s.</div>\n' \
        "$corpus_size" "$corpus_size" "$warmup" "$warmup" "$random_count" "$random_count" "$seed"
    printf '%s\n' \
        '<div class="note muted">`bit-exact failures` are expected for NaN sign/payload normalization differences across format/parse paths.</div>' \
        '<script>' \
        '(function() {' \
        '  const intFmt = new Intl.NumberFormat("en-US");' \
        '  const fixed1Fmt = new Intl.NumberFormat("en-US", { minimumFractionDigits: 1, maximumFractionDigits: 1 });' \
        '  const fixed3Fmt = new Intl.NumberFormat("en-US", { minimumFractionDigits: 3, maximumFractionDigits: 3 });' \
        '  document.querySelectorAll(".js-int").forEach((el) => {' \
        '    const value = Number(el.dataset.int);' \
        '    if (Number.isFinite(value)) { el.textContent = intFmt.format(value); }' \
        '  });' \
        '  document.querySelectorAll(".js-fixed1").forEach((el) => {' \
        '    const value = Number(el.dataset.float);' \
        '    if (Number.isFinite(value)) { el.textContent = fixed1Fmt.format(value); }' \
        '  });' \
        '  document.querySelectorAll(".js-fixed3").forEach((el) => {' \
        '    const value = Number(el.dataset.float);' \
        '    if (Number.isFinite(value)) { el.textContent = fixed3Fmt.format(value); }' \
        '  });' \
        '  document.querySelectorAll(".js-ratio").forEach((el) => {' \
        '    const num = Number(el.dataset.num);' \
        '    const den = Number(el.dataset.den);' \
        '    if (Number.isFinite(num) && Number.isFinite(den)) {' \
        '      el.textContent = intFmt.format(num) + "/" + intFmt.format(den);' \
        '    }' \
        '  });' \
        '})();' \
        '</script>' \
        '</body></html>'
} > "$SHOOTOUT_HTML_REPORT"

printf "  html report: %s\n" "$SHOOTOUT_HTML_REPORT"
