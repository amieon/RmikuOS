#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include "syscall.h"

static inline int net_socket(void){ 
    return syscall3(SYS_NET_SOCKET0, 0, 0, 0); 
}
static inline int net_bind(int fd, int port){
    return syscall3(SYS_NET_BIND, fd, port, 0); 
}
static inline int net_sendto(int fd, const void *buf, int len, unsigned ip, int port)
{ 
    return syscall6(SYS_NET_SENDTO0, fd, (long)buf, len, ip, port, 0); 
}
static inline int net_recvfrom(int fd, void *buf, int maxlen, void *info)
{ 
    return syscall6(SYS_NET_RECVFROM, fd, (long)buf, maxlen, (long)info,0 ,0);
}
static inline int net_close(int fd){
     return syscall3(SYS_NET_CLOSE, fd, 0, 0); 
}

#ifdef __cplusplus
}
#endif
