/* brainfuck.c —— Brainfuck 解释器（RmikuOS）
 *
 * 8 条指令:
 *   > 指针右移   < 指针左移   + 当前格+1   - 当前格-1
 *   . 输出字符   , 读入字符   [ 若格为0跳至匹配]后   ] 跳回匹配[
 * 其他字符为注释(跳过)。
 *
 * 设计:
 *   - 磁带: malloc(TAPE_SIZE=30000) 字节, 初始全 0
 *   - 循环配对: 预处理扫一遍源, 用栈算出每个 [ 的匹配 ] 位置(jump 表),
 *     执行时 O(1) 跳转(不每圈重扫)
 *   - 容错: 括号不匹配 / 指针越界 / malloc 失败均报错退出
 *
 * 用法: brainfuck <file.bf>
 */
#include "user.h"
#include <stdio.h>

#define TAPE_SIZE 30000

static const char *usage = "usage: brainfuck <file.bf>\n";

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("%s", usage);
        return 1;
    }

    /* ---- 读源文件 ---- */
    FILE *fp = fopen(argv[1], "r");
    if (!fp) {
        printf("brainfuck: cannot open %s\n", argv[1]);
        return 1;
    }
    /* 源缓冲用堆(malloc): RmikuOS 用户栈只有 64KB, 64KB 局部数组会撑爆栈 */
    char *src = (char *)malloc(65536);
    if (!src) {
        printf("brainfuck: malloc failed\n");
        fclose(fp);
        return 1;
    }
    size_t len = fread(src, 1, 65536, fp);
    fclose(fp);
    if (len == 0) {
        printf("brainfuck: empty program\n");
        free(src);
        return 1;
    }

    /* ---- 磁带 ---- */
    unsigned char *tape = (unsigned char *)malloc(TAPE_SIZE);
    if (!tape) {
        printf("brainfuck: malloc failed\n");
        free(src);
        return 1;
    }
    memset(tape, 0, TAPE_SIZE);

    /* ---- 括号配对 (jump 表, -1 = 无配对) ---- */
    int *jump = (int *)malloc(sizeof(int) * len);
    if (!jump) {
        printf("brainfuck: malloc failed\n");
        free(tape);
        free(src);
        return 1;
    }
    for (size_t i = 0; i < len; i++) jump[i] = -1;

    int *stack = (int *)malloc(sizeof(int) * len);   /* 括号栈 */
    if (!stack) {
        printf("brainfuck: malloc failed\n");
        free(tape);
        free(jump);
        free(src);
        return 1;
    }
    int sp = 0;
    for (size_t i = 0; i < len; i++) {
        if (src[i] == '[') {
            stack[sp++] = (int)i;
        } else if (src[i] == ']') {
            if (sp == 0) {
                printf("brainfuck: unmatched ] at %d\n", (int)i);
                goto fail;
            }
            int open = stack[--sp];
            jump[open] = (int)i;
            jump[i] = open;
        }
    }
    if (sp != 0) {
        printf("brainfuck: unmatched [ at %d\n", stack[sp - 1]);
        goto fail;
    }
    free(stack);
    stack = NULL;   /* 已释放, 执行期 fail 不再 free */

    /* ---- 执行 ---- */
    int pc = 0;
    int ptr = 0;
    while (pc < (int)len) {
        switch (src[pc]) {
        case '>':
            if (++ptr >= TAPE_SIZE) {
                printf("brainfuck: pointer overflow at %d\n", pc);
                goto fail;
            }
            pc++;
            break;
        case '<':
            if (--ptr < 0) {
                printf("brainfuck: pointer underflow at %d\n", pc);
                goto fail;
            }
            pc++;
            break;
        case '+': tape[ptr]++; pc++; break;
        case '-': tape[ptr]--; pc++; break;
        case '.': putchar(tape[ptr]); pc++; break;
        case ',': tape[ptr] = (unsigned char)getchar(); pc++; break;
        case '[':
            if (tape[ptr] == 0) {
                pc = jump[pc] + 1;   /* 跳到匹配 ] 之后 */
            } else {
                pc++;
            }
            break;
        case ']':
            if (tape[ptr] != 0) {
                pc = jump[pc];       /* 跳回匹配 [ */
            } else {
                pc++;
            }
            break;
        default:
            pc++;                    /* 注释字符 */
            break;
        }
    }

    free(tape);
    free(jump);
    free(src);
    return 0;

fail:
    free(tape);
    free(jump);
    if (stack) free(stack);
    free(src);
    return 1;
}
