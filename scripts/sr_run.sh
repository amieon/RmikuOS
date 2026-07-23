#!/usr/bin/env bash
# sr_run.sh —— GBN vs SR 对照采集:保留每次 run 的原始耗时(不做中位数)
#
# 前提:内核按目标版本(GBN=无重组缓存 / SR=有重组缓存)和 LOSS_EVERY 编译,
#       QEMU 已 tee 到 logs/console.log,httpd 已启动。
#
# 用法:
#   ./scripts/sr_run.sh gbn 50 20 1M    # 对照组:CUBIC 版(无重组缓存),LOSS=50
#   ./scripts/sr_run.sh sr  50 20 1M    # 实验组:CUBIC+SR,LOSS=50
#
# 产物:
#   logs/sr/runs.csv                          每次 run 一行(两组共用,追加)
#   logs/sr/<ver>-l<loss>-s<size>.events.txt  该区间的 tcp-stat 切片

set -euo pipefail

VER="${1:?用法: sr_run.sh 版本 LOSS标签 [RUNS] [尺寸]}"
LOSS="${2:?缺 LOSS 标签(与内核编译的 LOSS_EVERY 一致)}"
RUNS="${3:-20}"
SIZE="${4:-1M}"
URL="http://localhost:8080/random/${SIZE}.bin"
CONSOLE_LOG="${CONSOLE_LOG:-logs/console.log}"
OUTDIR="logs/sr"
RUNS_CSV="$OUTDIR/runs.csv"
EV="$OUTDIR/${VER}-l${LOSS}-s${SIZE}.events.txt"

expect=$(case "$SIZE" in
    64K)  echo 65536 ;;
    256K) echo 262144 ;;
    1M)   echo 1048576 ;;
esac)

mkdir -p "$OUTDIR"
[ -f "$RUNS_CSV" ] || echo "version,loss,size,run,time_s" > "$RUNS_CSV"

echo "=== 预热 10x 64K + 静置 5s ==="
for i in $(seq 1 10); do
    curl -o /dev/null -s "http://localhost:8080/random/64K.bin" || true
done
sleep 5

if [ -f "$CONSOLE_LOG" ]; then mark=$(wc -l < "$CONSOLE_LOG"); else mark=0; fi

echo "=== ${VER}-l${LOSS}-s${SIZE}: ${RUNS}x ==="
for i in $(seq 1 "$RUNS"); do
    out=$(curl -o /dev/null -s -w "%{time_total} %{http_code} %{size_download}" "$URL") \
        || { echo "curl 失败,QEMU/httpd 还在吗?"; exit 1; }
    read -r t code got <<< "$out"
    if [ "$code" != "200" ] || [ "$got" != "$expect" ]; then
        echo "  !! HTTP $code size=$got(期望 $expect),终止"; exit 1
    fi
    echo "  run $i: ${t}s"
    echo "$VER,$LOSS,$SIZE,$i,$t" >> "$RUNS_CSV"
    sleep 2
done

conn=0
if [ -f "$CONSOLE_LOG" ]; then
    tail -n +$((mark + 1)) "$CONSOLE_LOG" \
        | grep -E "tcp-stat|drop!|\[cubic\]" > "$EV" || true
    conn=$(grep -c "tcp-stat" "$EV" || true)
fi
if [ "$conn" != "$RUNS" ]; then
    echo "  !! 警告:tcp-stat 行数=$conn ≠ runs=$RUNS(预热越界或连接未终态)"
fi
echo "完成: $RUNS_CSV + $EV"