#include "sqlite3.h"
#include "fs.h"     
#include "stdlib.h"   /* 仅保险，本文件实际用自带 LCG 做随机性 */
#include "string.h"   /* memset / strncpy */

#define RMKU_VFS_NAME "rmiku"
#define RMKU_MAX_PATH 512

/* sqlite3_file 的子类：base 必须是第一个成员，后面挂我们自己的 fd/path */
typedef struct RmikuFile {
  sqlite3_file base;
  int          fd;
  int          deleteOnClose;
  char         path[RMKU_MAX_PATH];
} RmikuFile;

/* ----------------------- sqlite3_io_methods ----------------------- */

static int rmikuClose(sqlite3_file *p) {
  RmikuFile *f = (RmikuFile*)p;
  if (f->fd >= 0) close(f->fd);
  f->fd = -1;
  if (f->deleteOnClose) unlink(f->path);
  return SQLITE_OK;
}

static int rmikuRead(sqlite3_file *p, void *buf, int iAmt, sqlite3_int64 iOfst) {
  RmikuFile *f = (RmikuFile*)p;
  char *dst = (char*)buf;
  int got = 0;
  lseek(f->fd, (usize)iOfst, SEEK_SET);
  while (got < iAmt) {
    isize n = read(f->fd, dst + got, (usize)(iAmt - got));
    if (n < 0) return SQLITE_IOERR_READ;
    if (n == 0) break;               /* 到达 EOF */
    got += (int)n;
  }
  if (got < iAmt) {
    memset(dst + got, 0, (usize)(iAmt - got));
    return SQLITE_IOERR_SHORT_READ;  /* 不足部分补 0，交给上层判断 */
  }
  return SQLITE_OK;
}

static int rmikuWrite(sqlite3_file *p, const void *buf, int iAmt, sqlite3_int64 iOfst) {
  RmikuFile *f = (RmikuFile*)p;
  const char *src = (const char*)buf;
  int written = 0;
  lseek(f->fd, (usize)iOfst, SEEK_SET);
  while (written < iAmt) {
    isize n = write(f->fd, src + written, (usize)(iAmt - written));
    if (n < 0) return SQLITE_IOERR_WRITE;
    written += (int)n;
  }
  return SQLITE_OK;
}

static int rmikuTruncate(sqlite3_file *p, sqlite3_int64 size) {
  RmikuFile *f = (RmikuFile*)p;
  return ftruncate(f->fd, (usize)size) == 0 ? SQLITE_OK : SQLITE_IOERR_TRUNCATE;
}

static int rmikuSync(sqlite3_file *p, int flags) {
  RmikuFile *f = (RmikuFile*)p;
  (void)flags;
  return fsync(f->fd) == 0 ? SQLITE_OK : SQLITE_IOERR_FSYNC;
}

static int rmikuFileSize(sqlite3_file *p, sqlite3_int64 *pSize) {
  RmikuFile *f = (RmikuFile*)p;
  struct stat st;
  if (fstat(f->fd, &st) != 0) return SQLITE_IOERR_FSTAT;
  *pSize = (sqlite3_int64)st.st_size;
  return SQLITE_OK;
}

/* 单进程，文件锁全部 no-op */
static int rmikuLock(sqlite3_file *p, int level)     { (void)p; (void)level; return SQLITE_OK; }
static int rmikuUnlock(sqlite3_file *p, int level)   { (void)p; (void)level; return SQLITE_OK; }
static int rmikuCheckReservedLock(sqlite3_file *p, int *pResOut) {
  (void)p; *pResOut = 0; return SQLITE_OK;
}
static int rmikuFileControl(sqlite3_file *p, int op, void *pArg) {
  (void)p; (void)op; (void)pArg; return SQLITE_NOTFOUND;
}
static int rmikuSectorSize(sqlite3_file *p)          { (void)p; return 4096; }
static int rmikuDeviceCharacteristics(sqlite3_file *p) { (void)p; return 0; }

static sqlite3_io_methods rmiku_io_methods = {
  .iVersion = 1,
  .xClose = rmikuClose,
  .xRead = rmikuRead,
  .xWrite = rmikuWrite,
  .xTruncate = rmikuTruncate,
  .xSync = rmikuSync,
  .xFileSize = rmikuFileSize,
  .xLock = rmikuLock,
  .xUnlock = rmikuUnlock,
  .xCheckReservedLock = rmikuCheckReservedLock,
  .xFileControl = rmikuFileControl,
  .xSectorSize = rmikuSectorSize,
  .xDeviceCharacteristics = rmikuDeviceCharacteristics,
  /* iVersion=1，后面的 xShmMap/xShmLock/.../xFetch/xUnfetch 保持 0(NULL) 即可 */
};

/* ----------------------- sqlite3_vfs ----------------------- */

static int rmikuOpen(sqlite3_vfs *pVfs, const char *zName,
                     sqlite3_file *pFile, int flags, int *pOutFlags) {
  RmikuFile *f = (RmikuFile*)pFile;
  int oflags;
  (void)pVfs;
  if (zName == 0) return SQLITE_IOERR;

  if (flags & SQLITE_OPEN_READONLY) {
    oflags = O_RDONLY;
  } else {
    oflags = O_RDWR;
    if (flags & SQLITE_OPEN_CREATE) oflags |= O_CREAT;
  }
  /* 注意：打开时绝不截断。SQLite 通过 xTruncate 自行管理文件长度。 */

  int fd = (int)open(zName, (usize)oflags, 0644);
  if (fd < 0) return SQLITE_CANTOPEN;

  memset(f, 0, sizeof(*f));
  f->base.pMethods = &rmiku_io_methods;
  f->fd = fd;
  f->deleteOnClose = (flags & SQLITE_OPEN_DELETEONCLOSE) ? 1 : 0;
  strncpy(f->path, zName, RMKU_MAX_PATH - 1);
  f->path[RMKU_MAX_PATH - 1] = '\0';
  /* SQLite 规范: pOutFlags 允许为 NULL(调用方不需要输出 flags)。
   * 交互 shell 的 CREATE TABLE 等路径会传 NULL —— 必须判空, 否则空指针写崩溃。 */
  if (pOutFlags) *pOutFlags = flags;
  return SQLITE_OK;
}

static int rmikuDelete(sqlite3_vfs *pVfs, const char *zName, int dirSync) {
  (void)pVfs; (void)dirSync;
  unlink(zName);                 /* 教学 OS：删除失败也当成功，避免 journal 清理中断 */
  return SQLITE_OK;
}

/* SQLite 打开/关闭数据库时会 stat 这些辅助文件名（-journal / -wal / -shm），
 * 但本 VFS 使用 MEMORY 日志，这些文件永不落盘创建。直接返回"不存在"，
 * 避免无谓的 stat 触达内核并打印 [ WARN] stat failed。 */
static int rmikuLooksLikeSqliteAux(const char *zName) {
  static const char *const suffixes[] = {"-journal", "-wal", "-shm"};
  int i;
  usize n = strlen(zName);
  for (i = 0; i < 3; i++) {
    usize sl = strlen(suffixes[i]);
    if (n >= sl && strncmp(zName + n - sl, suffixes[i], sl) == 0) return 1;
  }
  return 0;
}

static int rmikuAccess(sqlite3_vfs *pVfs, const char *zName, int flags, int *pResOut) {
  struct stat st;
  (void)pVfs; (void)flags;
  if (rmikuLooksLikeSqliteAux(zName)) {
    /* 这些辅助文件本就不创建，直接告知"不存在"，跳过内核 stat。 */
    *pResOut = 0;
    return SQLITE_OK;
  }
  *pResOut = (stat(zName, &st) == 0) ? 1 : 0;
  return SQLITE_OK;
}

static int rmikuFullPathname(sqlite3_vfs *pVfs, const char *zName,
                             int nOut, char *zOut) {
  (void)pVfs;
  strncpy(zOut, zName, (usize)nOut - 1);
  zOut[nOut - 1] = '\0';
  return SQLITE_OK;
}

/* 随机性：自带 LCG。仅用于临时文件名/查询计划随机化，非加密用途。 */
static int rmikuRandomness(sqlite3_vfs *pVfs, int nByte, char *zOut) {
  (void)pVfs;
  static unsigned seed = 0x9e3779b9u;
  int i;
  for (i = 0; i < nByte; i++) {
    seed = seed * 1664525u + 1013904223u;
    zOut[i] = (char)(seed >> 24);
  }
  return nByte;
}

static int rmikuSleep(sqlite3_vfs *pVfs, int microseconds) {
  (void)pVfs; (void)microseconds;
  return 0;
}

/* 仅当启用日期函数时才会被调用（我们已 OMIT），给个常数近似值即可。 */
static int rmikuCurrentTime(sqlite3_vfs *pVfs, double *pTime) {
  (void)pVfs;
  *pTime = 2440587.5;   /* 1970-01-01 的儒略日 */
  return SQLITE_OK;
}
static int rmikuCurrentTimeInt64(sqlite3_vfs *pVfs, sqlite3_int64 *pTime) {
  (void)pVfs;
  *pTime = 0;
  return SQLITE_OK;
}

static int rmikuGetLastError(sqlite3_vfs *pVfs, int nBuf, char *zBuf) {
  (void)pVfs; (void)nBuf; (void)zBuf;
  return 0;
}

static sqlite3_vfs rmiku_vfs = {
  .iVersion = 2,
  .szOsFile = sizeof(RmikuFile),
  .mxPathname = RMKU_MAX_PATH,
  .pNext = 0,
  .zName = RMKU_VFS_NAME,
  .pAppData = 0,
  .xOpen = rmikuOpen,
  .xDelete = rmikuDelete,
  .xAccess = rmikuAccess,
  .xFullPathname = rmikuFullPathname,
  .xDlOpen = 0,
  .xDlError = 0,
  .xDlSym = 0,
  .xDlClose = 0,
  .xRandomness = rmikuRandomness,
  .xSleep = rmikuSleep,
  .xCurrentTime = rmikuCurrentTime,
  .xGetLastError = rmikuGetLastError,
  .xCurrentTimeInt64 = rmikuCurrentTimeInt64,
  .xSetSystemCall = 0,
  .xGetSystemCall = 0,
  .xNextSystemCall = 0,
};

/* SQLITE_OS_OTHER 下，这两个由应用程序提供（amalgamation 不再定义） */
int sqlite3_os_init(void) {
  return sqlite3_vfs_register(&rmiku_vfs, 1);  /* 1 = 注册为默认 VFS */
}
int sqlite3_os_end(void) {
  return SQLITE_OK;
}
