#!/usr/bin/env bash
# shootout_track.sh — Append timestamped shootout results to history TSV.
# Called by: make shootout-track
set -euo pipefail

SHOOTOUT_REPORT_DIR="${SHOOTOUT_REPORT_DIR:-build/reports}"
SHOOTOUT_SIZE_REPORT="${SHOOTOUT_SIZE_REPORT:-$SHOOTOUT_REPORT_DIR/shootout_size.tsv}"
SHOOTOUT_SIZE_ROUNDTRIP_REPORT="${SHOOTOUT_SIZE_ROUNDTRIP_REPORT:-$SHOOTOUT_REPORT_DIR/shootout_size_roundtrip.tsv}"
SHOOTOUT_PERF_REPORT="${SHOOTOUT_PERF_REPORT:-$SHOOTOUT_REPORT_DIR/shootout_perf.tsv}"
SHOOTOUT_HISTORY_REPORT="${SHOOTOUT_HISTORY_REPORT:-$SHOOTOUT_REPORT_DIR/shootout_history.tsv}"

mkdir -p "$SHOOTOUT_REPORT_DIR"

tab="$(printf '\t')"
ts="$(date -u '+%Y-%m-%d %H:%M:%S UTC')"
commit="$(git rev-parse --short=12 HEAD 2>/dev/null || printf '%s' 'unknown')"
if git rev-parse --git-dir >/dev/null 2>&1 && ! git diff --quiet --ignore-submodules HEAD -- 2>/dev/null; then
    commit="${commit}-dirty"
fi

if [ ! -f "$SHOOTOUT_HISTORY_REPORT" ]; then
    printf "timestamp_utc\tcommit\tid\tlabel\tsize_bytes\tns_per_conv\tnumeric_fail\tbit_fail\tconversions\tavg_len\tcorpus_size\twarmup\trandom_count\tseed\n" > "$SHOOTOUT_HISTORY_REPORT"
fi

for size_file in "$SHOOTOUT_SIZE_REPORT" "$SHOOTOUT_SIZE_ROUNDTRIP_REPORT"; do
    while IFS="$tab" read -r sid slabel sbytes; do
        [ "$sid" = "id" ] && continue
        found="0"
        while IFS="$tab" read -r pid plabel pns pelapsed pconv pavg pnum pbit pparse pfmt pcorpus pwarm prandom pseed; do
            [ "$pid" = "id" ] && continue
            if [ "$pid" = "$sid" ]; then
                printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" \
                    "$ts" "$commit" "$sid" "$slabel" "$sbytes" "$pns" "$pnum" "$pbit" "$pconv" "$pavg" "$pcorpus" "$pwarm" "$prandom" "$pseed" >> "$SHOOTOUT_HISTORY_REPORT"
                found="1"
                break
            fi
        done < "$SHOOTOUT_PERF_REPORT"
        if [ "$found" != "1" ]; then
            echo "shootout track warning: missing perf row for $sid"
        fi
    done < "$size_file"
done

printf "  history log: %s\n" "$SHOOTOUT_HISTORY_REPORT"
