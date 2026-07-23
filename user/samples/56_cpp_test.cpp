// hello_cpp.cpp — 最小 C++ 裸机测试，不碰 STL

extern "C" {
    // 复用你的 syscall（按你的实际签名）
    long syscall3(long id, long a0, long a1, long a2);
}

// 你的 SYS 号（按你的实际值）
// SYS_WRITE, SYS_EXIT

extern "C" int main() {
    const char* msg = "hello from C++\n";
    long len = 0;
    while (msg[len]) len++;
    syscall3(2, 1, (long)(long)msg, len);
    syscall3(0, 0, 0, 0);
    return 0;
}