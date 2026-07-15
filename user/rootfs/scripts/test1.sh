# test.sh —— RmikuOS Shell 功能测试脚本

echo "========== 1. 基本命令 =========="
pwd
cd /bin
ls

echo "========== 2. 通配符 =========="
# 假设 /bin 下有若干程序
ls /bin/*.c 2>/dev/null || echo "no .c in /bin"
ls /t?st* 2>/dev/null || echo "no test files"

echo "========== 3. 大括号展开 =========="
echo {hello,world}
echo file{1,2,3}.txt
echo num{10..13}

echo "========== 4. 逻辑链 && || =========="
# 成功 && 执行
echo "first" && echo "second"
# 失败 || 执行
cd /nonexist 2>/dev/null || echo "cd failed as expected"
# 组合
echo "try" && ls /nonexist 2>/dev/null || echo "fallback"

echo "========== 5. 管道 =========="
echo "hello world" | cat

echo "========== 6. 重定向 =========="
echo "redirect test" > /tmp/test.txt
cat /tmp/test.txt
echo "append line" >> /tmp/test.txt
cat /tmp/test.txt
rm /tmp/test.txt

echo "========== 7. 后台执行 =========="
sleep 3 &
jobs
# 等几秒后应该能看到 [1] done sleep

echo "========== 8. 引号保护 =========="
echo "*.c"      # 字面输出 *.c，不展开
echo 'hello * ? [abc]'  # 字面输出

echo "========== 9. 多语句分隔 =========="
echo "one"; echo "two"; echo "three"

echo "========== all tests passed =========="