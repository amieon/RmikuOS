#include "user.h"
#include "SHA256.h"


struct User {
    const char *name;
    usize uid, gid;
    const char *home;
    const char *hash; 
};
static const struct User USERS[] = {
    {"root",  0,   0,   "/home/root",  "4813494d137e1631bba301d5acab6e7bb7aa74ce1185d456565ef51d737677b2"},
    {"alice", 100, 100, "/home/alice", "2bd806c97f0e00af1a1fc3328fa763a9269723c8db8fac4f93af71db186d6e90"},
    {"bob",   101, 100, "/home/bob",   "81b637d8fcd2c6da6359e6963113a1170de795e4b725b84d1e0b4cfd9ec58ce9"},
    {NULL, 0, 0, NULL, NULL}
};

static int str_eq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}


static int readline(char *buf, int max) {
    int i = 0;
    for (;;) {
        int c = getchar();
        if (c < 0) return -1;
        if (c == '\n' || c == '\r') { buf[i] = 0; return i; }
        if (i < max - 1) buf[i++] = (char)c;
    }
}

int main(void) {
    char name[64], pass[64], hex[65];
    unsigned char dig[32];

    for (;;) {
        puts("RmikuOS login: ");
        if (readline(name, sizeof(name)) <= 0) continue;
        puts("Password: ");
        if (readline(pass, sizeof(pass)) <= 0) continue;

        const struct User *u = NULL;
        for (int i = 0; USERS[i].name; i++) {
            if (str_eq(USERS[i].name, name)) { u = &USERS[i]; break; }
        }
        if (!u) { puts("login: unknown user\n"); continue; }

        usize plen = 0;
        while (pass[plen]) plen++;
        sha256((const unsigned char *)pass, plen, dig);
        to_hex(dig, hex);
        if (!str_eq(hex, u->hash)) { puts("login: incorrect password\n"); continue; }

        isize pc = fork();
        if (pc == 0) {
            setgid(u->gid);         
            setuid(u->uid);         
            chdir(u->home);         
            exec("/bin/shell");
            puts("[login] exec /bin/shell failed\n");
            exit(1);
        } else if (pc > 0) {
            int code = -1;
            waitpid(pc, &code, 0);
        } else {
            puts("[login] fork failed\n");
        }
    }
    return 0;
}
