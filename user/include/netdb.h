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

/* getnameinfo 的 flags（值与 glibc 一致） */
#define NI_NUMERICHOST  0x01
#define NI_NUMERICSERV  0x02
#define NI_NOFQDN       0x04
#define NI_NAMEREQD     0x08
#define NI_DGRAM        0x10

#define NI_MAXHOST      1025
#define NI_MAXSERV      32

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

/* 老 gethostbyname 那套的错误码（glibc 也放在 <netdb.h> 里）。
 * 没有真解析器，h_errno 永远是 NO_RECOVERY；声明成 static 是为了不产生
 * "未定义符号"（每个翻译单元一份，反正是假值）。 */
#ifndef HOST_NOT_FOUND
#define HOST_NOT_FOUND 1
#define TRY_AGAIN      2
#define NO_RECOVERY    3
#define NO_DATA        4
#endif

static int h_errno = 0;

static inline struct hostent *gethostbyname(const char *name) {
    (void)name;
    h_errno = NO_RECOVERY;
    return (struct hostent *)0;
}

/* 反向解析同样没有。必须把 h_errno 设成 NO_RECOVERY 而不是留 0：
 * 调用方一般写 `p = gethostbyaddr(...); if (!p) switch (h_errno) ...`，
 * 留 0 会让它把 "根本没有解析器" 误判成 "成功但没结果"。 */
static inline struct hostent *gethostbyaddr(const void *addr, socklen_t len,
                                           int type) {
    (void)addr; (void)len; (void)type;
    h_errno = NO_RECOVERY;
    return (struct hostent *)0;
}

static inline const char *hstrerror(int err) {
    (void)err;
    return "no resolver";
}

/* 服务名（/etc/services）。我们没有这张表，明确返回 NULL，
 * 调用方（git 的 connect.c）会 die 出一条清楚的消息，而不是连错端口。 */
struct servent {
    char  *s_name;
    char **s_aliases;
    int    s_port;       /* 网络字节序 */
    char  *s_proto;
};

static inline struct servent *getservbyname(const char *name, const char *proto) {
    (void)name; (void)proto;
    return (struct servent *)0;
}

static inline struct servent *getservbyport(int port, const char *proto) {
    (void)port; (void)proto;
    return (struct servent *)0;
}

/* 协议名（/etc/protocols）。同样没有这张表，明确返回 NULL。
 * 注意返回 NULL 之后调用方一般会退回 IPPROTO_TCP 之类的默认值，
 * 这比编一个 "tcp = 6" 的表更不容易骗人 —— 我们确实不知道。 */
struct protoent {
    char  *p_name;
    char **p_aliases;
    int    p_proto;
};

static inline struct protoent *getprotobyname(const char *name) {
    (void)name;
    return (struct protoent *)0;
}

static inline struct protoent *getprotobynumber(int proto) {
    (void)proto;
    return (struct protoent *)0;
}

/* getnameinfo：sockaddr -> "主机:端口" 字符串。
 * 只有在 getaddrinfo 成功之后才有意义，而我们的 getaddrinfo 永远失败，
 * 所以这里明确返回 EAI_FAIL —— 不伪造 "(unknown)"。 */
static inline int getnameinfo(const struct sockaddr *sa, socklen_t salen,
                              char *host, socklen_t hostlen,
                              char *serv, socklen_t servlen, int flags) {
    (void)sa; (void)salen; (void)host; (void)hostlen;
    (void)serv; (void)servlen; (void)flags;
    return EAI_FAIL;
}

#ifdef __cplusplus
}
#endif

#endif /* RK_NETDB_H */
