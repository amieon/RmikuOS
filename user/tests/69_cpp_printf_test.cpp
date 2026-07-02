
#include "my/stdcompat.h"

extern "C" int main() {
    int errors = 0;

    // 基本格式
    uprintf("TEST: printf basic\n");
    uprintf("  int: %d\n", 42);
    uprintf("  neg: %d\n", -99);
    uprintf("  uint: %u\n", 12345);
    uprintf("  hex: %x\n", 255);
    uprintf("  ptr: %p\n", (void*)0x10000);
    uprintf("  char: %c\n", 'X');
    uprintf("  str: %s\n", "hello");
    uprintf("  percent: %%\n");
    uprintf("  long: %ld\n", -123456789L);
    uprintf("  ulong: %lu\n", 9876543210UL);
    uprintf("  lhex: %lx\n", 0xDEADBEEFUL);

    // printf 桥接
    printf("TEST: printf bridge\n");
    printf("  value=%d\n", 100);

    // C printf 桥接
    printf("TEST: C printf bridge\n");
    printf("  value=%d\n", 200);

    // 注意：uprintf 不支持 %f/%g，GCN 里用了这些
    // 下面测试会暴露这个问题：
    uprintf("  float (%%f): ");
    // 如果 uprintf 不支持 %f，这里会输出 "float (f): " 或乱码
    uprintf("%f\n", 3.14159);  // 期望：如果未支持，会原样输出 %f 或忽略

    uprintf("printf: done (check output manually for %f support)\n");
    return errors;
}