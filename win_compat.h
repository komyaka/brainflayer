/* Windows / MinGW compatibility layer for brainflayer
 *
 * Include this header in place of the POSIX-only headers listed below when
 * targeting _WIN32 (MinGW / MSYS2 toolchain):
 *
 *   arpa/inet.h  — ntohl/htonl provided by winsock2.h
 *   sys/sysinfo.h — sysinfo() shim via GlobalMemoryStatusEx
 *   sys/mman.h   — mmap/munmap shims via VirtualAlloc / CreateFileMapping
 *   sys/wait.h   — WIFEXITED/WEXITSTATUS shims (for test harness)
 *
 * On non-Windows platforms, arpa/inet.h is included for ntohl/htonl.
 */
#ifndef BRAINFLAYER_WIN_COMPAT_H
#define BRAINFLAYER_WIN_COMPAT_H

#ifdef _WIN32

# ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
# endif
# include <winsock2.h>   /* ntohl / htonl */
# include <windows.h>    /* HANDLE, VirtualAlloc, CreateFileMapping, … */
# include <io.h>         /* _open, _close, _read, _write */
# include <sys/stat.h>   /* stat, S_ISREG */
# include <stdint.h>
# include <stdlib.h>
# include <stdio.h>

/* open64 / O_LARGEFILE — MinGW maps open() to 64-bit file ops already */
# ifndef O_LARGEFILE
#  define O_LARGEFILE 0
# endif
# ifndef open64
#  define open64 open
# endif

/* sysconf(_SC_PAGESIZE) */
# ifndef _SC_PAGESIZE
#  define _SC_PAGESIZE 30
# endif
static inline long _win_sysconf(int name) {
  if (name == _SC_PAGESIZE) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (long)si.dwPageSize;
  }
  return -1;
}
# define sysconf _win_sysconf

/* sysinfo (Linux-only RAM query; used in brainflayer.c) */
struct sysinfo {
  unsigned long  mem_unit;
  unsigned long long totalram;
};
static inline int sysinfo(struct sysinfo *info) {
  MEMORYSTATUSEX ms;
  if (info == NULL) { return -1; }
  ms.dwLength = sizeof(ms);
  if (!GlobalMemoryStatusEx(&ms)) return -1;
  info->mem_unit = 1;
  info->totalram = ms.ullTotalPhys;
  return 0;
}

/* POSIX memory / file advice — no-ops on Windows */
# define POSIX_MADV_NORMAL     0
# define POSIX_MADV_RANDOM     1
# define POSIX_MADV_SEQUENTIAL 2
# define POSIX_MADV_WILLNEED   3
# define POSIX_MADV_DONTNEED   4
# define POSIX_FADV_NORMAL     0
# define POSIX_FADV_RANDOM     1
# define POSIX_FADV_SEQUENTIAL 2
# define POSIX_FADV_WILLNEED   3
# define POSIX_FADV_DONTNEED   4
# define POSIX_FADV_NOREUSE    5
# define MAP_NORESERVE  0
# define MAP_POPULATE   0
# define MAP_SHARED     0
# define MAP_PRIVATE    0
# define MAP_ANONYMOUS  0
# define PROT_READ      0x01
# define PROT_WRITE     0x02
# define MAP_FAILED     ((void *)-1)
static inline int posix_madvise(void *a, size_t l, int adv) {
  (void)a; (void)l; (void)adv; return 0;
}
static inline int posix_fadvise(int fd, off_t off, off_t len, int adv) {
  (void)fd; (void)off; (void)len; (void)adv; return 0;
}

/* sys/wait.h shims — used in tests/normalize_test.c */
# define WIFEXITED(s)   (1)
# define WEXITSTATUS(s) ((s) & 0xff)

/* isatty is in io.h on Windows */
# include <io.h>

/* fsync / msync shims */
static inline int fsync(int fd) {
  HANDLE h = (HANDLE)_get_osfhandle(fd);
  return FlushFileBuffers(h) ? 0 : -1;
}
static inline int msync(void *addr, size_t len, int flags) {
  (void)flags;
  return FlushViewOfFile(addr, len) ? 0 : -1;
}

#else  /* !_WIN32 */

# include <arpa/inet.h>  /* ntohl / htonl */

#endif /* _WIN32 */

#endif /* BRAINFLAYER_WIN_COMPAT_H */
/*  vim: set ts=2 sw=2 et ai si: */
