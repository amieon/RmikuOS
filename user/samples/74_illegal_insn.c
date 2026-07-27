#include "user.h"

int main() {
    printf("about to execute illegal instruction\n");
    __asm__ volatile(".word 0x00000000");  // 全零指令，非法
    printf("should not reach here\n");
    return 0;
}