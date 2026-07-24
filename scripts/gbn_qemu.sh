#!/usr/bin/env bash
set -uo pipefail

VALUES=(5 10 20 50 100 200 500 0)
TARGET_FILE="./kernel/src/drivers/net/tcp.rs"
TEST_SCRIPT="./scripts/sr_run.sh"
LOG_DIR="logs"
mkdir -p "$LOG_DIR"

if ! command -v expect &>/dev/null; then
    echo "❌ 需要 expect: sudo apt install expect"
    exit 1
fi

for X in "${VALUES[@]}"; do
    echo ""
    echo "===== 实验: LOSS_EVERY = $X ====="

    # 保险：杀干净
    pkill -9 -f "qemu-system-riscv64" 2>/dev/null || true
    sleep 1

    # 1. 改代码
    sed -i "42s/const LOSS_EVERY: u32 = [0-9]\+;/const LOSS_EVERY: u32 = $X;/" "$TARGET_FILE"
    echo "代码: $(head -n 48 "$TARGET_FILE" | tail -n 1)"

    # 2. 准备标记文件
    DONE_FILE="/tmp/rmiku_done_${X}"
    rm -f "$DONE_FILE"

    # 3. expect 脚本：严格按你的命令启动，输入 httpd，后台跑测试脚本，死等完成，杀 QEMU
    EXP_FILE="/tmp/rmiku_${X}.exp"
    cat > "$EXP_FILE" << 'EXPECT_EOF'
set timeout 60

# 严格按你的命令：./run.sh riscv64 debug 2>&1 | tee logs/console.log
spawn bash -c "./run.sh riscv64 debug 2>&1 | tee logs/console.log"

# 等 shell
expect "RmikuOS shell"
sleep 1
expect "/ $"

# 输入 httpd
send "httpd\r"

# 等 httpd 就绪
expect "httpd listening on"
sleep 1
puts "httpd 已启动"

# 后台启动测试脚本（带 &，system 立即返回，不阻塞 expect）
# 输出同时显示在终端并保存到文件，跑完 touch done 文件
system "bash __TEST_SCRIPT__ gbn __X__ 20 1M 2>&1 | tee __LOG_DIR__/gbn_test___X__.log; touch __DONE_FILE__" &

puts "测试脚本已后台启动，等待完成中..."

# 死等标记文件（每秒检查，同时 expect 事件循环读取 QEMU 输出，防止管道阻塞）
set count 0
while {1} {
    if {[file exists "__DONE_FILE__"]} {
        puts "测试脚本已完成"
        break
    }
    if {$count > 86400} {
        puts "24小时超时，强制结束"
        break
    }
    sleep 1
    incr count
}

# 结束 QEMU
puts "正在结束 QEMU..."
catch { exec kill -15 [exp_pid -i $spawn_id] 2>/dev/null }
sleep 1
catch { exec pkill -9 -f "qemu-system-riscv64" 2>/dev/null }
EXPECT_EOF

    sed -i "s|__TEST_SCRIPT__|${TEST_SCRIPT}|" "$EXP_FILE"
    sed -i "s|__X__|${X}|" "$EXP_FILE"
    sed -i "s|__LOG_DIR__|${LOG_DIR}|" "$EXP_FILE"
    sed -i "s|__DONE_FILE__|${DONE_FILE}|" "$EXP_FILE"

    # 4. 执行 expect
    expect "$EXP_FILE" || echo "expect 异常（QEMU可能崩溃），继续..."

    # 5. 清理
    rm -f "$EXP_FILE" "$DONE_FILE"
    pkill -9 -f "qemu-system-riscv64" 2>/dev/null || true

    echo "✅ 实验 $X 完成"
    echo "   控制台日志: logs/console.log"
    echo "   测试日志:   ${LOG_DIR}/gbn_test_${X}.log"
    sleep 2
done

echo ""
echo "🎉 全部 8 组实验完成！"