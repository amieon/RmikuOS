

extern "C" {
    long syscall3(unsigned long id, unsigned long a0, unsigned long a1, unsigned long a2);
}

// 你的 SYS 号
static const unsigned long SYS_EXIT  = 0;
static const unsigned long SYS_WRITE = 2;

// 简单的字符串长度(不依赖任何库)
static unsigned long cstr_len(const char* s) {
    unsigned long n = 0;
    while (s[n]) n++;
    return n;
}

// 打印(用你的 write syscall,fd=1)
static void puts_raw(const char* s) {
    syscall3(SYS_WRITE, 1, (unsigned long)s, cstr_len(s));
}

// C++ 入口:走你的 crt0(call main)
extern "C" int main() {
    puts_raw("hello from C++\n");

    // 测一点 C++ 特性:引用、模板、局部对象(都不需要运行时库)
    int a = 21;
    int& ra = a;
    ra = ra * 2;             // = 42

    // 打印 a(简单转字符串)
    char buf[16];
    int n = 0;
    int v = a;
    if (v == 0) { buf[n++] = '0'; }
    char tmp[16]; int t = 0;
    while (v > 0) { tmp[t++] = char('0' + v % 10); v /= 10; }
    while (t > 0) { buf[n++] = tmp[--t]; }
    buf[n++] = '\n';
    buf[n] = 0;
    puts_raw(buf);            // 应打印 42

    syscall3(SYS_EXIT, 0, 0, 0);
    return 0;
}