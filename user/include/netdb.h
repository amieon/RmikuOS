/*
 * netdb.h —— 主机名 / 服务解析
 *
 * getaddrinfo 现在是**真实现**，不是空桩。原因很直接：libcurl 的
 * lib/vdns/hostip4.c 无条件调用它，**连 `curl http://127.0.0.1:8080/`
 * 这种 IP 字面量也走它**（字面量只是走 inet_pton 的快速路径，不查 DNS）。
 * 所以"只支持 IP 直连就不用实现 getaddrinfo"是错的。
 *
 * 能力边界（够 libcurl 用，不够写浏览器）：
 *   - 只有 IPv4：AF_UNSPEC 一律按 AF_INET 处理，AF_INET6 明确 EAI_FAMILY；
 *   - 数字端口 + 一小张服务名表；不读 /etc/services（没有这张文件）；
 *   - 真域名走 net_resolve() → 内核的 SYSCALL_NET_RESOLVE → dns::resolve，
 *     同步阻塞，失败返回 EAI_NONAME（不是 EAI_AGAIN —— 我们没有"稍后重试"
 *     的语义，说 EAI_NONAME 至少不骗人说这是暂时性问题）。
 *
 * 为什么不能干脆省略：git-compat-util.h 无条件 include 它，
 * 而且 connect.c / ident.c 会取 struct addrinfo 的字段，结构体必须存在。
 */
#ifndef RK_NETDB_H
#define RK_NETDB_H

#include "sys/socket.h"
#include "net.h"          /* net_resolve / struct sockaddr_in / htons */
#include "arpa/inet.h"    /* inet_pton / inet_ntop */
#include "stdlib.h"       /* malloc / free */
#include "string.h"       /* strlen / strcmp */
#include "errno.h"

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

/* ---- 服务名 -> 端口 ----
 * 不读 /etc/services（guest 里没有这个文件）。curl 传进来的全是数字端口，
 * 这张小表只是为了"手写 http://host/ 时端口能出来"。
 * AI_NUMERICSERV 时遇到非数字必须报错，不能悄悄查表 —— 那是 hints 的字面承诺。 */
static inline int rk_ai_service(const char *service, int numericserv_only,
                                unsigned short *out) {
    if (!service) { *out = 0; return 0; }

    const char *p = service;
    if (*p >= '0' && *p <= '9') {
        unsigned long v = 0;
        while (*p >= '0' && *p <= '9') {
            v = v * 10 + (unsigned long)(*p - '0');
            if (v > 65535) return EAI_SERVICE;
            p++;
        }
        if (*p) return EAI_SERVICE;          /* "80abc" 不是端口 */
        *out = (unsigned short)v;
        return 0;
    }
    if (numericserv_only) return EAI_SERVICE;

    static const struct { const char *name; unsigned short port; } tab[] = {
        { "http",   80  }, { "https", 443 }, { "ftp",   21  },
        { "domain", 53  }, { "smtp",  25  }, { "ssh",   22  },
    };
    for (unsigned i = 0; i < sizeof(tab) / sizeof(tab[0]); i++) {
        if (strcmp(service, tab[i].name) == 0) { *out = tab[i].port; return 0; }
    }
    return EAI_SERVICE;
}

/* 造一个结果节点。addrinfo + sockaddr_in + canonname 全在**同一块**内存里，
 * 所以 freeaddrinfo 只需要 free(节点) —— 不用记住哪个字段是单独分配的。
 * 每加一次单独 malloc 就多一处泄漏点，这类小结构不值得。 */
static inline struct addrinfo *rk_ai_node(unsigned int ip, unsigned short port,
                                          int socktype, int protocol,
                                          const char *canon) {
    size_t clen = canon ? strlen(canon) + 1 : 0;
    char *blk = (char *)malloc(sizeof(struct addrinfo) + sizeof(struct sockaddr_in) + clen);
    if (!blk) return (struct addrinfo *)0;

    struct addrinfo *ai = (struct addrinfo *)blk;
    struct sockaddr_in *sin = (struct sockaddr_in *)(void *)(blk + sizeof(struct addrinfo));

    ai->ai_flags     = 0;
    ai->ai_family    = AF_INET;
    ai->ai_socktype  = socktype;
    ai->ai_protocol  = protocol;
    ai->ai_addrlen   = (unsigned int)sizeof(struct sockaddr_in);
    ai->ai_addr      = (struct sockaddr *)sin;
    ai->ai_next      = (struct addrinfo *)0;

    sin->sin_family      = AF_INET;
    sin->sin_port        = htons(port);
    sin->sin_addr.s_addr = htonl(ip);
    for (int i = 0; i < 8; i++) sin->sin_zero[i] = 0;

    if (clen) {
        char *dst = blk + sizeof(struct addrinfo) + sizeof(struct sockaddr_in);
        for (size_t i = 0; i < clen; i++) dst[i] = canon[i];
        ai->ai_canonname = dst;
    } else {
        ai->ai_canonname = (char *)0;
    }
    return ai;
}

/* getaddrinfo(node, service, hints, res)
 *
 * libcurl 的实际用法：node = 主机串（可能是 IP 字面量），service = "80"，
 * hints->ai_socktype = SOCK_STREAM，flags = AI_NUMERICHOST 只在
 * "别再解析了，这就是个地址" 的场合。所以字面量路径必须真的能过，
 * 而且必须不走 DNS —— 否则 `curl http://127.0.0.1/` 也要等一次 DNS 超时。 */
static inline int getaddrinfo(const char *node, const char *service,
                              const struct addrinfo *hints,
                              struct addrinfo **res) {
    if (!res) return EAI_FAIL;
    *res = (struct addrinfo *)0;

    int family = AF_UNSPEC, socktype = 0, protocol = 0, flags = 0;
    if (hints) {
        family   = hints->ai_family;
        socktype = hints->ai_socktype;
        protocol = hints->ai_protocol;
        flags    = hints->ai_flags;
    }

    if (flags & ~(AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST | AI_NUMERICSERV))
        return EAI_BADFLAGS;
    if (family != AF_UNSPEC && family != AF_INET)
        return EAI_FAMILY;
    if (socktype != 0 && socktype != SOCK_STREAM && socktype != SOCK_DGRAM)
        return EAI_SOCKTYPE;
    /* 没有主机名又不是被动打开：不知道连哪儿。glibc 这里给 EAI_NONAME。 */
    if (!node && !(flags & AI_PASSIVE))
        return EAI_NONAME;

    unsigned short port = 0;
    {
        int e = rk_ai_service(service, (flags & AI_NUMERICSERV) != 0, &port);
        if (e) return e;
    }

    /* 地址：节点为空 => AI_PASSIVE 的 INADDR_ANY */
    unsigned int ip = 0;
    if (node) {
        struct in_addr a;
        if (inet_pton(AF_INET, node, &a) == 1) {
            ip = ntohl(a.s_addr);            /* IP 字面量：不查 DNS */
        } else {
            if (flags & AI_NUMERICHOST) return EAI_NONAME;
            ip = net_resolve(node);          /* 真域名：走内核 DNS */
            if (ip == 0) return EAI_NONAME;
        }
    }

    const char *canon = (flags & AI_CANONNAME) ? (node ? node : "") : (const char *)0;

    /* socktype 没指定时要给全（POSIX 要求），libcurl 会自己挑它要的那个。
     * 顺序按 TCP 优先：http 是绝大多数用途。 */
    struct addrinfo *head, *tail, *n;
    head = tail = (struct addrinfo *)0;

    if (socktype == 0) {
        n = rk_ai_node(ip, port, SOCK_STREAM, IPPROTO_TCP, canon);
        if (!n) return EAI_MEMORY;
        head = tail = n;
        n = rk_ai_node(ip, port, SOCK_DGRAM, IPPROTO_UDP, (const char *)0);
        if (!n) { free(head); return EAI_MEMORY; }
        tail->ai_next = n;
        tail = n;
    } else {
        n = rk_ai_node(ip, port, socktype,
                       protocol ? protocol : (socktype == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP),
                       canon);
        if (!n) return EAI_MEMORY;
        head = tail = n;
    }

    *res = head;
    return 0;
}

/* 链上每个节点都是独立一块，逐个释放。传 NULL 是合法的（POSIX 允许）。
 * 空实现会让每次解析都泄漏 sizeof(addrinfo)+sizeof(sockaddr_in)，
 * curl 重定向多次或 -L 跟随时会一直涨。 */
static inline void freeaddrinfo(struct addrinfo *ai) {
    while (ai) {
        struct addrinfo *next = ai->ai_next;
        free(ai);
        ai = next;
    }
}

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

/* getnameinfo：sockaddr -> 主机串 + 端口串。
 *
 * 没有反向 DNS（内核只有正向 resolve），所以**永远给数字形式**，
 * 即使调用方没传 NI_NUMERICHOST。这比返回 EAI_FAIL 好：调用方要的
 * "把这个地址变成人能看的字符串"这件事，数字形式就满足了。
 * NI_NAMEREQD（要求必须反解出名字）才是真的做不到，那种情况返回 EAI_NONAME。 */
static inline int getnameinfo(const struct sockaddr *sa, socklen_t salen,
                              char *host, socklen_t hostlen,
                              char *serv, socklen_t servlen, int flags) {
    if (!sa || salen < (socklen_t)sizeof(struct sockaddr_in)) return EAI_FAMILY;
    if (sa->sa_family != AF_INET) return EAI_FAMILY;
    if (flags & NI_NAMEREQD) return EAI_NONAME;

    const struct sockaddr_in *in = (const struct sockaddr_in *)(const void *)sa;

    if (host && hostlen) {
        if (!inet_ntop(AF_INET, &in->sin_addr, host, hostlen)) return EAI_FAIL;
    }
    if (serv && servlen) {
        unsigned int p = ntohs(in->sin_port);
        /* 手写十进制，不碰 snprintf：我们的 printf 有两份实现
         * （vfprintf / vsnprintf），能不用就不用，少一个依赖面。 */
        char tmp[6];
        int n = 0;
        if (p == 0) {
            tmp[n++] = '0';
        } else {
            char rev[6];
            int k = 0;
            while (p) { rev[k++] = (char)('0' + (p % 10)); p /= 10; }
            while (k) tmp[n++] = rev[--k];
        }
        if ((socklen_t)(n + 1) > servlen) return EAI_FAIL;
        for (int i = 0; i < n; i++) serv[i] = tmp[i];
        serv[n] = '\0';
    }
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* RK_NETDB_H */
