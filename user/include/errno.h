#ifndef ERRNO_H
#define ERRNO_H
extern int errno;

/* 完整 Linux errno 表（asm-generic 编号，1..133）。
 * 数值与 kernel/src/syscall/errno.rs 一一对应，改一边必须改另一边。
 * 本内核只产生其中一部分，其余用于移植代码里的 switch/case 比较。 */

/* ---- 经典 SysV 段 ---- */
#define EPERM             1  /* 不允许的操作 */
#define ENOENT            2  /* 没有这个文件/目录 */
#define ESRCH             3  /* 没有这个进程 */
#define EINTR             4  /* 被信号中断 */
#define EIO               5  /* 输入输出错误 */
#define ENXIO             6  /* 没有这个设备或地址 */
#define E2BIG             7  /* 参数表太长 */
#define ENOEXEC           8  /* 执行格式错误 */
#define EBADF             9  /* 文件描述符无效 */
#define ECHILD           10  /* 没有子进程 */
#define EAGAIN           11  /* 资源暂时不可用 */
#define ENOMEM           12  /* 内存不足 */
#define EACCES           13  /* 权限不足 */
#define EFAULT           14  /* 地址错误 */
#define ENOTBLK          15  /* 需要块设备 */
#define EBUSY            16  /* 设备或资源忙 */
#define EEXIST           17  /* 文件已存在 */
#define EXDEV            18  /* 跨设备链接 */
#define ENODEV           19  /* 没有这个设备 */
#define ENOTDIR          20  /* 不是目录 */
#define EISDIR           21  /* 是目录 */
#define EINVAL           22  /* 参数无效 */
#define ENFILE           23  /* 系统打开文件数超限 */
#define EMFILE           24  /* 进程打开文件数超限 */
#define ENOTTY           25  /* 不是终端设备 */
#define ETXTBSY          26  /* 文本文件忙 */
#define EFBIG            27  /* 文件太大 */
#define ENOSPC           28  /* 设备空间不足 */
#define ESPIPE           29  /* 非法定位 */
#define EROFS            30  /* 只读文件系统 */
#define EMLINK           31  /* 链接数太多 */
#define EPIPE            32  /* 管道破裂 */
#define EDOM             33  /* 数学参数超出定义域 */
#define ERANGE           34  /* 结果超出范围 */

/* ---- 文件系统补充 ---- */
#define EDEADLK          35  /* 会造成死锁 */
#define ENAMETOOLONG     36  /* 文件名太长 */
#define ENOLCK           37  /* 没有可用锁 */
#define ENOSYS           38  /* 功能未实现 */
#define ENOTEMPTY        39  /* 目录非空 */
#define ELOOP            40  /* 符号链接层数太多 */

/* ---- 消息 / 流（本内核不用，仅给移植代码） ---- */
#define ENOMSG           42
#define EIDRM            43
#define ECHRNG           44
#define EL2NSYNC         45
#define EL3HLT           46
#define EL3RST           47
#define ELNRNG           48
#define EUNATCH          49
#define ENOCSI           50
#define EL2HLT           51
#define EBADE            52
#define EBADR            53
#define EXFULL           54
#define ENOANO           55
#define EBADRQC          56
#define EBADSLT          57
#define EBFONT           59
#define ENOSTR           60
#define ENODATA          61
#define ETIME            62
#define ENOSR            63
#define ENONET           64
#define ENOPKG           65
#define EREMOTE          66
#define ENOLINK          67
#define EADV             68
#define ESRMNT           69
#define ECOMM            70
#define EPROTO           71
#define EMULTIHOP        72
#define EDOTDOT          73
#define EBADMSG          74
#define EOVERFLOW        75
#define ENOTUNIQ         76
#define EBADFD           77
#define EREMCHG          78
#define ELIBACC          79
#define ELIBBAD          80
#define ELIBSCN          81
#define ELIBMAX          82
#define ELIBEXEC         83
#define EILSEQ           84  /* 非法字节序列 */
#define ERESTART         85
#define ESTRPIPE         86
#define EUSERS           87

/* ---- 网络 / socket ---- */
#define ENOTSOCK         88
#define EDESTADDRREQ     89
#define EMSGSIZE         90
#define EPROTOTYPE       91   /* socket 类型与协议不匹配(tr2_dst 用到) */
#define ENOPROTOOPT      92
#define EPROTONOSUPPORT  93
#define ESOCKTNOSUPPORT  94
#define EOPNOTSUPP       95
#define ENOTSUP          EOPNOTSUPP
#define EPFNOSUPPORT     96
#define EAFNOSUPPORT     97
#define EADDRINUSE       98
#define EADDRNOTAVAIL    99
#define ENETDOWN        100
#define ENETUNREACH     101
#define ENETRESET       102   /* 对端重置了网络连接 */
#define ECONNABORTED    103
#define ECONNRESET      104
#define ENOBUFS         105
#define EISCONN         106
#define ENOTCONN        107
#define ESHUTDOWN       108   /* 传输端点已关闭后仍发送 */
#define ETOOMANYREFS    109
#define ETIMEDOUT       110
#define ECONNREFUSED    111
#define EHOSTDOWN       112
#define EHOSTUNREACH    113
#define EALREADY        114
#define EINPROGRESS     115

/* ---- 其它 ---- */
#define ESTALE          116
#define EUCLEAN         117
#define ENOTNAM         118
#define ENAVAIL         119
#define EISNAM          120
#define EREMOTEIO       121
#define EDQUOT          122
#define ENOMEDIUM       123
#define EMEDIUMTYPE     124
#define ECANCELED       125   /* 操作被取消 */
#define ENOKEY          126
#define EKEYEXPIRED     127
#define EKEYREVOKED     128
#define EKEYREJECTED    129
#define EOWNERDEAD      130
#define ENOTRECOVERABLE 131
#define ERFKILL         132
#define EHWPOISON       133

/* 别名 */
#define EDEADLOCK        EDEADLK
#define EWOULDBLOCK      EAGAIN
#endif
