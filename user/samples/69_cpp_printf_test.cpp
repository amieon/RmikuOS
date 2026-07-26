
#include "my/stdcompat.h"

extern "C" int main() {
    int errors = 0;

    // 基本格式
    printf("TEST: printf basic\n");
    printf("  int: %d\n", 42);
    printf("  neg: %d\n", -99);
    printf("  uint: %u\n", 12345);
    printf("  hex: %x\n", 255);
    printf("  ptr: %p\n", (void*)0x10000);
    printf("  char: %c\n", 'X');
    printf("  str: %s\n", "hello");
    printf("  percent: %%\n");
    printf("  long: %ld\n", -123456789L);
    printf("  ulong: %lu\n", 9876543210UL);
    printf("  lhex: %lx\n", 0xDEADBEEFUL);

    // printf 桥接
    printf("TEST: printf bridge\n");
    printf("  value=%d\n", 100);

    // C printf 桥接
    printf("TEST: C printf bridge\n");
    printf("  value=%d\n", 200);

    // 注意：uprintf 不支持 %f/%g，GCN 里用了这些
    // 下面测试会暴露这个问题：
    printf("  float (%%f): ");
    // 如果 printf 不支持 %f，这里会输出 "float (f): " 或乱码
    printf("%f\n", 3.14159);  // 期望：如果未支持，会原样输出 %f 或忽略

    printf("printf: done (check output manually for %f support)\n");
    return errors;
}