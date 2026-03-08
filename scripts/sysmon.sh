#!/bin/bash
# sysmon.sh — Feed macOS process snapshots to Viewpoints via pipe
# Usage: ./scripts/sysmon.sh | ./build/vp.app/Contents/MacOS/vp --stdin
#
# Uses `top -l` for per-process stats and gpustat for GPU utilization.
# Protocol: header line first, then data rows, then "---" to end each snapshot.
# Viewpoints replaces its dataset on each "---".

INTERVAL=${1:-2}  # seconds between snapshots (default: 2)
MAXPROCS=${2:-9999}  # max processes per snapshot

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
GPUSTAT="$SCRIPT_DIR/gpustat"
GPU_TMP=$(mktemp /tmp/sysmon_gpu.XXXXXX)
trap "rm -f $GPU_TMP" EXIT

STATS="pid,cpu,csw,threads,ports,mem,purg,faults,cow,msgsent,msgrecv,sysbsd,sysmach,pageins,pgrp,ppid,uid,wq,instrs,cycles"

# Print column header once
echo "PID CPU CSW Threads Ports Mem Purgeable Faults COW MsgSent MsgRecv SysBSD SysMach PageIns PGRP PPID UID WQ Instrs Cycles GPU"

# Check if gpustat is available
if [ ! -x "$GPUSTAT" ]; then
    echo "Warning: gpustat not found at $GPUSTAT — GPU column will be 0" >&2
    echo "Compile with: clang -framework IOKit -framework CoreFoundation -o $GPUSTAT $SCRIPT_DIR/gpustat.m" >&2
fi

while true; do
    # Collect GPU stats into temp file
    if [ -x "$GPUSTAT" ]; then
        "$GPUSTAT" 500 > "$GPU_TMP" 2>/dev/null
    else
        : > "$GPU_TMP"
    fi

    # Grab one top snapshot, convert units, merge GPU column via awk
    top -l 1 -n "$MAXPROCS" -stats "$STATS" 2>/dev/null \
    | sed -n '/^PID /,$ { /^PID /d; /^$/d; p; }' \
    | sed -E '
        s|([0-9]+)/[0-9]+|\1|g;
        s/ N\/A / 0 /g;
        s/ - / 0 /g;
    ' \
    | awk -v gpufile="$GPU_TMP" '
    BEGIN {
        while ((getline line < gpufile) > 0) {
            split(line, a, " ")
            gpu[a[1]] = a[2]
        }
        close(gpufile)
    }
    {
        for (i = 1; i <= NF; i++) {
            v = $i
            n = length(v)
            if (n > 1) {
                suffix = substr(v, n, 1)
                num = substr(v, 1, n - 1)
                if (suffix == "B") v = num + 0
                else if (suffix == "K") v = num * 1024
                else if (suffix == "M") v = num * 1048576
                else if (suffix == "G") v = num * 1073741824
            }
            printf "%s%s", (i > 1 ? " " : ""), v
        }
        pid = $1 + 0
        g = (pid in gpu) ? gpu[pid] : "0.0"
        printf " %s\n", g
    }'
    echo "---"
    sleep "$INTERVAL"
done
