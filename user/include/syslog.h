/*
 * syslog.h —— 系统日志
 *
 * RmikuOS 没有 syslogd，所以这里全部是**空操作宏**：
 * 不产生符号、不产生调用，只为了让移植代码编得过。
 *
 * 用宏而不是函数的原因：函数会在每个链接进来的目标里留下一个未解析符号，
 * 而我们永远不会有实现；宏在预处理阶段就消失了。
 */
#ifndef RK_SYSLOG_H
#define RK_SYSLOG_H

#define LOG_EMERG   0
#define LOG_ALERT   1
#define LOG_CRIT    2
#define LOG_ERR     3
#define LOG_WARNING 4
#define LOG_NOTICE  5
#define LOG_INFO    6
#define LOG_DEBUG   7

#define LOG_PID     0x01
#define LOG_CONS    0x02
#define LOG_NDELAY  0x08
#define LOG_PERROR  0x20

#define LOG_USER   (1 << 3)
#define LOG_DAEMON (3 << 3)

#define openlog(ident, option, facility)  ((void)(ident), (void)(option), (void)(facility))
#define closelog()                        ((void)0)
#define syslog(priority, ...)             ((void)(priority))
#define vsyslog(priority, fmt, ap)        ((void)(priority), (void)(fmt), (void)(ap))

#endif /* RK_SYSLOG_H */
