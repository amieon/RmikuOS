#!/usr/bin/env bash
# tcp_loss_sweep.sh —— 实验二:尺寸扫描 @ 指定丢包率(对照 旧版 vs CUBIC)
#
# 前提:内核按指定 LOSS_EVERY 编译;LOSS 参数只是写进 CSV 的标签,
#       真实丢包率由内核常量决定,别填错。
#
# 用法:
#   ./tcp_loss_sweep.sh old   100 7     # A 组:旧版,LOSS_EVERY=100
#   ./tcp_loss_sweep.sh cubic 100 7     # B 组:CUBIC,LOSS_EVERY=100
#   ./tcp_loss_sweep.sh cubic 0   7     # 基线 E0
#
# 产物:
#   logs/tcp/loss_sweep.csv          version,loss,size,runs,median,mean,conn,bytes,rtx_rto,rtx_fast,loss_events,rtt_samples
#   logs/tcp/<名字>.events.txt       该尺寸区间事件日志切片(含 cwnd 轨迹)

set -euo pipefail

VER="${1:?用法: tcp_loss_sweep.sh 版本 LOSS标签 [每尺寸次数]}"
LOSS="${2:?缺 LOSS 标签(与内核编译的 LOSS_EVERY 一致)}"
RUNS="${3:-7}"
URL_BASE="${4:-http://localhost:8080/random}"
CONSOLE_LOG="${CONSOLE_LOG:-logs/console.log}"
OUTDIR="logs/tcp"
CSV="$OUTDIR/loss_sweep.csv"

SIZES=(64K 256K 1M)     # 丢包实验不需要九档,小尺寸一个窗口就发完了,看不出拥塞控制
WARMUP="${WARMUP:-20}"  # 两组必须用相同 WARMUP:确定性丢包计数器是全局的,预热会移相
SLEEP_S="${SLEEP_S:-2}"

size_bytes() {
    case "$1" in
        64K)  echo 65536 ;;
        256K) echo 262144 ;;
        1M)   echo 1048576 ;;
    esac
}

mkdir -p "$OUTDIR"
[ -f "$CSV" ] || echo "version,loss,size,runs,median_s,mean_s,conn,bytes,rtx_rto,rtx_fast,loss_events,rtt_samples" > "$CSV"

if [ "$WARMUP" -gt 0 ]; then
    echo "=== 预热: ${WARMUP}x 64K ==="
    for i in $(seq 1 "$WARMUP"); do
        curl -o /dev/null -s "$URL_BASE/64K.bin" || true
    done
    sleep 5 
    echo
fi

for sz in "${SIZES[@]}"; do
    expect=$(size_bytes "$sz")
    url="$URL_BASE/$sz.bin"
    name="${VER}-l${LOSS}-s${sz}"
    echo "=== $name: ${RUNS}x ==="

    if [ -f "$CONSOLE_LOG" ]; then mark=$(wc -l < "$CONSOLE_LOG"); else mark=0; fi

    times=()
    for i in $(seq 1 "$RUNS"); do
        out=$(curl -o /dev/null -s -w "%{time_total} %{http_code} %{size_download}" "$url") \
            || { echo "curl 失败,QEMU/httpd 还在吗?"; exit 1; }
        read -r t code got <<< "$out"
        if [ "$code" != "200" ] || [ "$got" != "$expect" ]; then
            echo "  !! HTTP $code size=$got(期望 $expect),终止"; exit 1
        fi
        echo "  run $i: ${t}s"
        times+=("$t")
        sleep "$SLEEP_S"
    done

    stats=$(printf '%s\n' "${times[@]}" | sort -n | awk -v n="$RUNS" '
        { a[NR]=$1; s+=$1 }
        END { if (n%2) med=a[(n+1)/2]; else med=(a[n/2]+a[n/2+1])/2;
              printf "%.3f %.3f", med, s/n }')
    read -r median mean <<< "$stats"

    ev="$OUTDIR/$name.events.txt"
    conn=0; bytes=0; rtx_rto=0; rtx_fast=0; loss_ev=0; samples=0
    if [ -f "$CONSOLE_LOG" ]; then
        tail -n +$((mark + 1)) "$CONSOLE_LOG" \
            | grep -E "drop!|rtx|rtt sample|tcp-stat|\[cubic\]|\[cwnd\]" > "$ev" || true
        read -r conn bytes rtx_rto rtx_fast loss_ev <<< "$(grep '\[tcp-stat\]' "$ev" | awk '
            { for (i=1; i<=NF; i++) { split($i, kv, "="); v[kv[1]]=kv[2] }
              n++; b+=v["bytes"]; r+=v["rtx_rto"]; f+=v["rtx_fast"]; l+=v["loss"] }
            END { printf "%d %d %d %d %d", n, b, r, f, l }')"
        samples=$(grep -c "rtt sample" "$ev" || true)
    fi

    if [ "$conn" != "$RUNS" ]; then
        echo "  !! 警告:tcp-stat 行数=$conn ≠ runs=$RUNS,有连接没走到终态(或跨区间串行)"
    fi

    echo "$VER,$LOSS,$sz,$RUNS,$median,$mean,$conn,$bytes,$rtx_rto,$rtx_fast,$loss_ev,$samples" >> "$CSV"
    echo "-> 中位 ${median}s  rtx_rto=$rtx_rto rtx_fast=$rtx_fast loss_events=$loss_ev"
    echo
done

echo "全部写入 $CSV"