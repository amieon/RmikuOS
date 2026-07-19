#!/usr/bin/env bash
# tcp_exp.sh —— TCP RTO 实验数据采集
#
# 每组实验的流程:
#   1. 改 tcp.rs(切换新旧版本 / LOSS_EVERY),重新编译
#   2. 终端1: ./run.sh riscv64 debug 2>&1 | tee logs/console.log
#   3. 等 shell 出来、httpd 可用
#   4. 终端2: ./tcp_exp.sh <实验名> [次数=5] [URL]
#   5. pkill -f qemu-system,进入下一组
#
# 命名约定(画图时统一): {old|new}-{丢包率}pct
#   ./tcp_exp.sh old-5pct
#   ./tcp_exp.sh new-10pct 10
#
# 产物(都在 logs/tcp/ 下):
#   summary.csv              追加一行:名字/中位数/均值/极值/原始耗时
#   <名字>.events.txt        内核日志里的 drop!/rtx/rtt sample 事件
#   <名字>.recovery.txt      每次丢包的恢复耗时(drop! -> 同 seq 的 rtx 时间差)

set -euo pipefail

NAME="${1:?用法: ./scripts/tcp_exp.sh <实验名> [次数=5] [URL]}"
RUNS="${2:-5}"
URL="${3:-http://localhost:8080/random/big.bin}"
CONSOLE_LOG="${CONSOLE_LOG:-./logs/console.log}"
OUTDIR="./logs/tcp"

mkdir -p "$OUTDIR"

echo "=== 实验 $NAME: ${RUNS}x curl $URL ==="

times=()
for i in $(seq 1 "$RUNS"); do
    if ! t=$(curl -o /dev/null -s -w "%{time_total}" "$URL"); then
        echo "curl 失败 —— QEMU 起来了没?httpd 起了没?big.bin 在镜像里没?"
        exit 1
    fi
    echo "  run $i: ${t}s"
    times+=("$t")
done

# 中位数 / 均值 / 极值(浮点)
stats=$(printf '%s\n' "${times[@]}" | sort -n | awk -v n="$RUNS" '
    { a[NR]=$1; s+=$1 }
    END {
        if (n%2) med=a[(n+1)/2]; else med=(a[n/2]+a[n/2+1])/2;
        printf "%.3f %.3f %.3f %.3f", med, s/n, a[1], a[n]
    }')
read -r median mean tmin tmax <<< "$stats"

if [ ! -f "$OUTDIR/summary.csv" ]; then
    echo "name,runs,median_s,mean_s,min_s,max_s,raw_times" > "$OUTDIR/summary.csv"
fi
raw=$(IFS=' '; echo "${times[*]}")
echo "$NAME,$RUNS,$median,$mean,$tmin,$tmax,\"$raw\"" >> "$OUTDIR/summary.csv"

echo "-> 中位数 ${median}s  均值 ${mean}s  (min ${tmin} / max ${tmax})"
echo "-> 已追加 $OUTDIR/summary.csv"

# ---- 内核日志事件提取 ----
if [ ! -f "$CONSOLE_LOG" ]; then
    echo "警告: 找不到 $CONSOLE_LOG,只记了 curl 耗时"
    echo "提示: 终端1 请用 ./run.sh riscv64 debug 2>&1 | tee $CONSOLE_LOG 启动"
    exit 0
fi

grep -E "drop!|rtx|rtt sample" "$CONSOLE_LOG" > "$OUTDIR/$NAME.events.txt" || true
echo "-> 事件 $(wc -l < "$OUTDIR/$NAME.events.txt") 条: $OUTDIR/$NAME.events.txt"

# drop! 与随后同 seq 的 rtx 配对,算单次恢复耗时
awk '
    /drop!/ {
        match($0, /t=[0-9]+/);  td = substr($0, RSTART+2, RLENGTH-2) + 0;
        match($0, /seq=[0-9]+/); s = substr($0, RSTART+4, RLENGTH-4);
        drop[s] = td; next
    }
    /rtx/ {
        match($0, /t=[0-9]+/);  tr = substr($0, RSTART+2, RLENGTH-2) + 0;
        match($0, /seq=[0-9]+/); s = substr($0, RSTART+4, RLENGTH-4);
        if ((s in drop) && !(s in done) && tr >= drop[s]) {
            printf "seq=%s drop_t=%d rtx_t=%d recovery=%dms\n", s, drop[s], tr, tr - drop[s];
            done[s] = 1
        }
    }
' "$OUTDIR/$NAME.events.txt" > "$OUTDIR/$NAME.recovery.txt" || true

if [ -s "$OUTDIR/$NAME.recovery.txt" ]; then
    echo "-> 单次恢复耗时:"
    cat "$OUTDIR/$NAME.recovery.txt"
    awk -F'recovery=|ms' '{s+=$2; n++} END {if (n) printf "-> 平均恢复 %.0fms(%d 次丢包)\n", s/n, n}' \
        "$OUTDIR/$NAME.recovery.txt"
fi