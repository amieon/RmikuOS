#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include "syscall.h"



/* socket 类型：1 = TCP(STREAM)，2 = UDP(DGRAM) */


static inline int net_socket(int ty){
    return syscall3(SYS_NET_SOCKET0, ty, 0, 0);
}

static inline int net_socket_tcp(){
    return syscall3(SYS_NET_SOCKET0, 1, 0, 0);
}
static inline int net_socket_udp(){
    return syscall3(SYS_NET_SOCKET0, 2, 0, 0);
}

static inline int net_bind(int fd, int port){
    return syscall3(SYS_NET_BIND, fd, port, 0);
}
static inline int net_sendto(int fd, const void *buf, int len, unsigned ip, int port){
    return syscall6(SYS_NET_SENDTO0, fd, (long)buf, len, ip, port, 0);
}
static inline int net_recvfrom(int fd, void *buf, int maxlen, void *info){
    return syscall6(SYS_NET_RECVFROM, fd, (long)buf, maxlen, (long)info, 0, 0);
}
static inline int net_close(int fd){
    return syscall3(SYS_NET_CLOSE, fd, 0, 0);
}

/* TCP */
static inline int net_connect(int fd, unsigned ip, int port){
    return syscall3(SYS_NET_CONNECT, fd, ip, port);
}
static inline int net_listen(int fd, int backlog){
    return syscall3(SYS_NET_LISTEN, fd, backlog, 0);
}
static inline int net_accept(int fd, void *info){
    return syscall3(SYS_NET_ACCEPT, fd, (long)info, 0);
}
static inline int net_send(int fd, const void *buf, int len){
    return syscall3(SYS_NET_SEND, fd, (long)buf, len);
}
static inline int net_recv(int fd, void *buf, int max){
    return syscall3(SYS_NET_RECV, fd, (long)buf, max);
}

#ifdef __cplusplus
}
#endif