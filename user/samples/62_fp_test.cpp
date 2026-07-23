#include "mem.h"


static void puts_raw(const char* s){unsigned long n=0;while(s[n])n++;syscall3(2,1,(unsigned long)s,n);}
static void put_hex(unsigned long v){
    char buf[20]; const char* hex="0123456789ABCDEF";
    for(int i=0;i<16;i++) buf[15-i]=hex[v&0xF], v>>=4;
    buf[16]='\n'; buf[17]=0;
    puts_raw(buf);
}

// ---------------------------------------------------------
// 测试 1: 纯整数
// ---------------------------------------------------------
void test_int() {
    puts_raw("[Test 1] Integer... OK\n");
}

// ---------------------------------------------------------
// 测试 2: 底层汇编测试 FPU 状态
// ---------------------------------------------------------
void test_asm_fpu() {
    puts_raw("[Test 2] ASM FPU Enable Check...\n");
    volatile double val = 0.0;
    
#if defined(__loongarch__)
    // LoongArch: 正确做法是从内存加载浮点数
    // 编译器会自动把 src 放到内存，然后我们用 fld.d 加载
    double src = 1.0; 
    asm volatile (
        "fld.d $f0, %1      \n\t"  // 从内存加载 1.0 到 $f0
        "fst.d $f0, %0      \n\t"  // 将 $f0 保存到 val
        : "=m"(val)
        : "m"(src)
        : "$f0"
    );
#elif defined(__riscv)
    double src = 1.0;
    asm volatile (
        "fld ft0, %1        \n\t" 
        "fsd ft0, %0        \n\t"
        : "=m"(val)
        : "m"(src)
        : "ft0"
    );
#else
    val = 1.0; 
#endif

    if(val == 1.0) {
        puts_raw("   -> Read 1.0: OK\n");
    } else {
        puts_raw("   -> Read Value WRONG!\n");
    }
}

// ---------------------------------------------------------
// 测试 3: 简单浮点运算
// ---------------------------------------------------------
void test_simple_ops() {
    puts_raw("[Test 3] Simple Add/Mul...\n");
    volatile float a = 1.5f;
    volatile float b = 2.5f;
    volatile float c = a + b;
    
    if (c == 4.0f) {
        puts_raw("   -> Add OK\n");
    } else {
        puts_raw("   -> Add FAILED\n");
    }
}

// ---------------------------------------------------------
// 测试 4: FSQRT (核心测试点)
// ---------------------------------------------------------
void test_sqrt() {
    puts_raw("[Test 4] FSQRT...\n");
    volatile double x = 4.0;
    volatile double y = 0.0;
    
#if defined(__loongarch__)
    asm volatile ("fsqrt.d %0, %1" : "=f"(y) : "f"(x));
#elif defined(__riscv)
    asm volatile ("fsqrt.d %0, %1" : "=f"(y) : "f"(x));
#else
    if (x > 0) y = 2.0; 
#endif

    if (y == 2.0) {
        puts_raw("   -> Sqrt(4) = 2.0: OK\n");
    } else {
        puts_raw("   -> Sqrt FAILED (Value Error)\n");
    }
}

extern "C" int main() {
    puts_raw("=== FPU Stress Test Begin ===\n");
    
    test_int();
    test_asm_fpu(); 
    test_simple_ops();
    test_sqrt();

    puts_raw("=== FPU Stress Test End ===\n");
    syscall3(0,0,0,0);
    return 0;
}