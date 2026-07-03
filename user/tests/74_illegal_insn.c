#include "user.h"

int main() {
    uprintf("about to execute illegal instruction\n");
    __asm__ volatile(".word 0x00000000");  // 全零指令，非法
    uprintf("should not reach here\n");
    return 0;
}