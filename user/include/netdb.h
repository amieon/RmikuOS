/*
 * netdb.h —— 主机名 / 服务解析
 *
 * 现状：**只提供编译期结构体与失败实现**。
 *   - 本地 git 完全不走 DNS（连 `git clone /path/repo` 也是 fork+exec+pipe），
 *     所以 getaddrinfo / gethostbyname 返回失败不影响 init/add/commit/log；
 *   - 真要做联网时，再在这里接内核的 DNS 客户端（net.h 的 nslookup 路径）。
 *
 * 为什么不能干脆省略：git-compat-util.h 无条件 include 它，
 * 而且 connect.c / ident.c 会取 struct addrinfo 的字段，结构体必须存在。
 */
#ifndef RK_NETDB_H
#define RK_NETDB_H

#include "sys/socket.h"

#ifdef __cplusplus
extern "C" {
#endif

struct addrinfo {
    int              ai_flags;
    int              ai_family;
    int              ai_socktype;
    int              ai_protocol;
    unsigned int     ai_addrlen;
    struct sockaddr *ai_addr;
    char            *ai_canonname;
    struct addrinfo *ai_next;
};

/* getaddrinfo 的 hints.ai_flags */
#define AI_PASSIVE      0x01
#define AI_CANONNAME    0x02
#define AI_NUMERICHOST  0x04
#define AI_NUMERICSERV  0x08

/* EAI_* 用负值（与 glibc 一致，方便 `!= 0` 判断失败） */
#define EAI_BADFLAGS   (-1)
#define EAI_NONAME     (-2)
#define EAI_AGAIN      (-3)
#define EAI_FAIL       (-4)
#define EAI_FAMILY     (-6)
#define EAI_SOCKTYPE   (-7)
#define EAI_SERVICE    (-8)
#define EAI_MEMORY    (-10)
#define EAI_SYSTEM    (-11)

struct hostent {
    char  *h_name;
    char **h_aliases;
    int    h_addrtype;
    int    h_length;
    char **h_addr_list;
};
#define h_addr h_addr_list[0]

static inline int getaddrinfo(const char *node, const char *service,
                              const struct addrinfo *hints,
                              struct addrinfo **res) {
    (void)node; (void)service; (void)hints;
    if (res) *res = (struct addrinfo *)0;
    return EAI_FAIL;      /* 明确失败，不伪造结果 */
}

static inline void freeaddrinfo(struct addrinfo *ai) { (void)ai; }

static inline const char *gai_strerror(int err) {
    switch (err) {
    case 0:            return "ok";
    case EAI_BADFLAGS: return "bad flags";
    case EAI_NONAME:   return "name not known";
    case EAI_AGAIN:    return "temporary failure";
    case EAI_FAIL:     return "non-recoverable failure";
    case EAI_FAMILY:   return "address family not supported";
    case EAI_SOCKTYPE: return "socket type not supported";
    case EAI_SERVICE:  return "service not supported";
    case EAI_MEMORY:   return "out of memory";
    case EAI_SYSTEM:   return "system error";
    default:           return "unknown error";
    }
}

static inline struct hostent *gethostbyname(const char *name) {
    (void)name;
    return (struct hostent *)0;
}

#ifdef __cplusplus
}
#endif

#endif /* RK_NETDB_H */
