#include "user.h"

#define LINE_MAX 256


static int line_contains(const char *line, const char *pattern) {

    if (pattern[0] == '\0') {
        return 1;
    }

    for (int i = 0; line[i] != '\0'; i++) {
        int j = 0;
        while (pattern[j] != '\0' && line[i + j] == pattern[j]) {
            j++;
        }
        if (pattern[j] == '\0') {
            return 1; 
        }
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        puts("usage: grep <pattern>\n");
        return 1;
    }

    const char *pattern = argv[1];

    char line[LINE_MAX];
    int len = 0;

    char ch;
    isize n;


    while ((n = read(0, &ch, 1)) > 0) {
        if (ch == '\n') {
            line[len] = '\0';
            if (line_contains(line, pattern)) {

                write(1, line, len);
                write(1, "\n", 1);
            }
            len = 0;  
        } else {

            if (len < LINE_MAX - 1) {
                line[len++] = ch;
            }

        }
    }


    if (len > 0) {
        line[len] = '\0';
        if (line_contains(line, pattern)) {
            write(1, line, len);
            write(1, "\n", 1);
        }
    }

    return 0;
}