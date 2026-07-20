#!/usr/bin/env bash
# tcp_size_sweep.sh —— 实验一:尺寸扫描 @ 0% 丢包(证明延迟抖动随尺寸累积)
#
# 前提:
#   1. tcp.rs 的 LOSS_EVERY = 0,内核已编译
#   2. guest 的 /random/ 下备好 4K..1M 九个文件,宿主机生成:
#        mkdir -p tftpboot
#        for s in 4 8 16 32 64 128 256 512; do
#          dd if=/dev/urandom of=tftpboot/${s}K.bin bs=1024 count=$s
#        done
#        dd if=/dev/urandom of=tftpboot/1M.bin bs=1024 count=1024
#      然后进 QEMU 逐个 tftp 拉取(tftp 4K.bin /random/4K.bin ...)
#   3. 终端1: ./run.sh riscv64 debug 2>&1 | tee logs/console.log,httpd 已启动
#
# 用法(终端2):
#   ./tcp_size_sweep.sh new
#   ./tcp_size_sweep.sh old 10
#
# 产物:
#   logs/tcp/size_sweep.csv        每尺寸一行:版本/尺寸/中位数/均值/rtx数/样本数
#   logs/tcp/<名字>.events.txt     该尺寸区间的事件日志切片

set -euo pipefail

VER="${1:?用法: tcp_size_sweep.sh 版本 [每尺寸次数]}"
RUNS="${2:-7}"
URL_BASE="${3:-http://localhost:8080/random}"
CONSOLE_LOG="${CONSOLE_LOG:-logs/console.log}"
OUTDIR="logs/tcp"
CSV="$OUTDIR/size_sweep.csv"

SIZES=(4K 8K 16K 32K 64K 128K 256K 512K 1M)
WARMUP="${WARMUP:-50}"   # 预热:正式测量前先发 WARMUP 个 4K 请求,数据丢弃
SLEEP_S="${SLEEP_S:-2}"  # 测量期每次 run 间隔:防 TIME_WAIT 槽位耗尽(18s 停摆)

size_bytes() {
    case "$1" in
        4K)   echo 4096 ;;    8K)   echo 8192 ;;
        16K)  echo 16384 ;;   32K)  echo 32768 ;;
        64K)  echo 65536 ;;   128K) echo 131072 ;;
        256K) echo 262144 ;;  512K) echo 524288 ;;
        1M)   echo 1048576 ;;
    esac
}

mkdir -p "$OUTDIR"
[ -f "$CSV" ] || echo "version,size,runs,median_s,mean_s,rtx_count,sample_count" > "$CSV"

# ---- 预热:4K 连发,吸收启动瞬态;数据不记录 ----
if [ "$WARMUP" -gt 0 ]; then
    echo "=== 预热: ${WARMUP}x 4K(数据丢弃) ==="
    for i in $(seq 1 "$WARMUP"); do
        curl -o /dev/null -s "$URL_BASE/4K.bin" || true
    done
    echo "=== 预热完成,开始测量 ==="
    echo
fi

for sz in "${SIZES[@]}"; do
    expect=$(size_bytes "$sz")
    url="$URL_BASE/$sz.bin"
    name="${VER}-s${sz}-l0"
    echo "=== $name: ${RUNS}x curl $url ==="

    # 书签:记下当前日志行数,本尺寸的事件只切新增部分
    if [ -f "$CONSOLE_LOG" ]; then
        mark=$(wc -l < "$CONSOLE_LOG")
    else
        mark=0
    fi

    times=()
    for i in $(seq 1 "$RUNS"); do
        out=$(curl -o /dev/null -s -w "%{time_total} %{http_code} %{size_download}" "$url") \
            || { echo "curl 失败,QEMU/httpd 还在吗?"; exit 1; }
        read -r t code got <<< "$out"
        if [ "$code" != "200" ] || [ "$got" != "$expect" ]; then
            echo "  !! HTTP $code size=$got(期望 $expect)—— $sz.bin 没就位,终止"
            exit 1
        fi
        echo "  run $i: ${t}s"
        times+=("$t")
        sleep "$SLEEP_S"   # 让 TIME_WAIT 槽位消化,避免 SYN 被丢的 18s 停摆
    done

    stats=$(printf '%s\n' "${times[@]}" | sort -n | awk -v n="$RUNS" '
        { a[NR]=$1; s+=$1 }
        END {
            if (n%2) med=a[(n+1)/2]; else med=(a[n/2]+a[n/2+1])/2;
            printf "%.3f %.3f", med, s/n
        }')
    read -r median mean <<< "$stats"

    # 切出本尺寸的事件日志
    rtx=0; samples=0
    if [ -f "$CONSOLE_LOG" ]; then
        tail -n +$((mark + 1)) "$CONSOLE_LOG" \
            | grep -E "drop!|rtx|rtt sample" > "$OUTDIR/$name.events.txt" || true
        rtx=$(grep -c "rtx" "$OUTDIR/$name.events.txt" || true)
        samples=$(grep -c "rtt sample" "$OUTDIR/$name.events.txt" || true)
    fi

    echo "$VER,$sz,$RUNS,$median,$mean,$rtx,$samples" >> "$CSV"
    echo "-> 中位 ${median}s  均值 ${mean}s  rtx=$rtx  samples=$samples"
    echo
done

echo "全部写入 $CSV"