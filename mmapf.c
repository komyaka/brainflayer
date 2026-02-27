/* Copyright (c) 2015 Ryan Castellucci, All Rights Reserved */
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE

#ifdef _WIN32
# include "win_compat.h"
# include <string.h>
# include <errno.h>
# include <fcntl.h>
# include <sys/stat.h>
#else
# include <unistd.h>
# include <string.h>
# include <fcntl.h>
# include <stdlib.h>
# include <stdio.h>
# include <errno.h>
# include <sys/types.h>
# include <sys/stat.h>
# include <sys/mman.h>
#endif

#include "mmapf.h"

static char *errstr[] = {
  "Unknown error",
  "Not a regular file",
  "Incorrect file size",
  ""
};

char * mmapf_strerror(int errnum) {
  if (errnum < MMAPF_EXFIRST) {
    return strerror(errnum);
  } else if (errnum < MMAPF_EXLAST) {
    return errstr[errnum-MMAPF_EXFIRST];
  } else {
    return errstr[0];
  }
}

#ifdef _WIN32

/* ---- Windows implementation using CreateFileMapping / MapViewOfFile ---- */

int munmapf(mmapf_ctx *ctx) {
  if (ctx->mem != NULL) {
    FlushViewOfFile(ctx->mem, ctx->file_sz);
    UnmapViewOfFile(ctx->mem);
    ctx->mem = NULL;
  }
  if (ctx->aux != NULL) {
    CloseHandle((HANDLE)ctx->aux);
    ctx->aux = NULL;
  }
  if (ctx->fd >= 0) {
    _close(ctx->fd);
    ctx->fd = -1;
  }
  ctx->file_sz = 0;
  ctx->mmap_sz = 0;
  return 0;
}

int mmapf(mmapf_ctx *ctx, const unsigned char *filename, size_t size, int flags) {
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  size_t page_sz = si.dwPageSize;
  struct stat sb;
  int fmode = 0;
  int fd = -1;

  ctx->mem  = NULL;
  ctx->aux  = NULL;
  ctx->fd   = -1;
  ctx->file_sz = size;
  ctx->mmap_sz = size % page_sz ? (size/page_sz+1)*page_sz : size;

  /* translate MMAPF flags to Win32 access mode */
  DWORD desired_access = 0, share_mode = FILE_SHARE_READ;
  DWORD page_protect   = 0, view_access = 0;
  DWORD creat_disp     = OPEN_EXISTING;

  if (flags & MMAPF_RW) {
    desired_access = GENERIC_READ | GENERIC_WRITE;
    page_protect   = PAGE_READWRITE;
    view_access    = FILE_MAP_ALL_ACCESS;
    fmode          = O_RDWR;
  } else if (flags & MMAPF_RD) {
    desired_access = GENERIC_READ;
    page_protect   = PAGE_READONLY;
    view_access    = FILE_MAP_READ;
    fmode          = O_RDONLY;
  } else if (flags & MMAPF_WR) {
    desired_access = GENERIC_READ | GENERIC_WRITE;
    page_protect   = PAGE_READWRITE;
    view_access    = FILE_MAP_WRITE;
    fmode          = O_WRONLY;
  }
  if (flags & MMAPF_COW) {
    page_protect = PAGE_WRITECOPY;
    view_access  = FILE_MAP_COPY;
  }
  if (flags & MMAPF_CR) { creat_disp = OPEN_ALWAYS; }
  if (flags & MMAPF_EX) { creat_disp = CREATE_NEW; }

  if (!filename) {
    /* anonymous mapping via VirtualAlloc */
    ctx->mem = VirtualAlloc(NULL, ctx->mmap_sz,
                            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!ctx->mem) { return ENOMEM; }
    return MMAPF_OKAY;
  }

  /* file-backed mapping */
  if (stat((const char *)filename, &sb) == 0) {
    if (!S_ISREG(sb.st_mode)) { return MMAPF_ENREG; }
    if ((size_t)sb.st_size != size) { return MMAPF_ESIZE; }
  } else if (!(flags & MMAPF_CR)) {
    return ENOENT;
  }

  HANDLE hFile = CreateFileA((const char *)filename,
                             desired_access, share_mode, NULL,
                             creat_disp,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS,
                             NULL);
  if (hFile == INVALID_HANDLE_VALUE) { return EACCES; }

  /* pre-allocate if creating */
  if ((flags & MMAPF_CR) && stat((const char *)filename, &sb) != 0) {
    LARGE_INTEGER li; li.QuadPart = (LONGLONG)size;
    SetFilePointerEx(hFile, li, NULL, FILE_BEGIN);
    SetEndOfFile(hFile);
    li.QuadPart = 0;
    SetFilePointerEx(hFile, li, NULL, FILE_BEGIN);
  }

  LARGE_INTEGER map_sz; map_sz.QuadPart = (LONGLONG)size;
  HANDLE hMap = CreateFileMappingA(hFile, NULL, page_protect,
                                   map_sz.HighPart, map_sz.LowPart, NULL);
  if (!hMap) { CloseHandle(hFile); return ENOMEM; }

  ctx->mem = MapViewOfFile(hMap, view_access, 0, 0, size);
  if (!ctx->mem) { CloseHandle(hMap); CloseHandle(hFile); return ENOMEM; }

  /* store the mapping handle in aux; store CRT fd via _open_osfhandle */
  ctx->aux = (void *)hMap;
  fd = _open_osfhandle((intptr_t)hFile, fmode);
  if (fd < 0) {
    UnmapViewOfFile(ctx->mem);
    CloseHandle(hMap);
    CloseHandle(hFile);
    ctx->mem = NULL;
    ctx->aux = NULL;
    return EBADF;
  }
  ctx->fd  = fd;

  return MMAPF_OKAY;
}

#else  /* !_WIN32 — original POSIX implementation */

int munmapf(mmapf_ctx *ctx) {
  // TODO error checking
  if (ctx->fd >= 0) {
    msync(ctx->mem, ctx->file_sz, MS_SYNC);
    fsync(ctx->fd);
    close(ctx->fd);
  }
  if (ctx->mem != NULL) {
    munmap(ctx->mem, ctx->mmap_sz);
  }
  ctx->file_sz = 0;
  ctx->mmap_sz = 0;
  ctx->mem = NULL;
  ctx->fd = -1;
  ctx->aux = NULL;
  return 0;
}

int mmapf(mmapf_ctx *ctx, const unsigned char *filename, size_t size, int flags) {
  size_t page_sz = sysconf(_SC_PAGESIZE);
  struct stat sb;

  int mmode = 0, mflags = 0, madv = 0;
  int fmode = 0, fadv = 0;
  int ret, fd;

  // initialize
  ctx->mem = NULL;
  ctx->fd = -1;
  ctx->aux = NULL;
  ctx->file_sz = size;

  // round up to the next multiple of the page size
  ctx->mmap_sz = size % page_sz ? (size/page_sz+1)*page_sz : size;

  mflags |= flags & MMAPF_COW ? MAP_PRIVATE : MAP_SHARED;

  if (flags & MMAPF_RW) {
    mmode |= PROT_READ|PROT_WRITE;
    fmode |= O_RDWR;
  } else if (flags & MMAPF_RD) {
    mflags |= MAP_NORESERVE;
    mmode |= PROT_READ;
    fmode |= O_RDONLY;
  } else if (flags & MMAPF_WR) {
    mmode |= PROT_WRITE;
    fmode |= O_WRONLY;
  }

  if (flags & MMAPF_CR) { fmode |= O_CREAT; }
  if (flags & MMAPF_EX) { fmode |= O_EXCL; }
  if (flags & MMAPF_PRE) { mflags |= MAP_POPULATE; }
  if (flags & MMAPF_NOREUSE) { fadv |= POSIX_FADV_NOREUSE; }
  if (flags & MMAPF_RND) { fadv |= POSIX_FADV_RANDOM; madv |= POSIX_MADV_RANDOM; }
  if (flags & MMAPF_SEQ) { fadv |= POSIX_FADV_SEQUENTIAL; madv |= POSIX_MADV_SEQUENTIAL; }
  if (flags & MMAPF_DONTNEED) { fadv |= POSIX_FADV_DONTNEED; madv |= POSIX_MADV_DONTNEED; }
  if (flags & MMAPF_WILLNEED) {
    fadv |= POSIX_FADV_WILLNEED;
    // seems to fail on anonymous maps
    if (filename) { madv |= POSIX_MADV_WILLNEED; }
  }

  if (!filename) {
    ctx->mem = mmap(NULL, ctx->mmap_sz, mmode, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
  } else {
    if (stat(filename, &sb) == 0) { // file exists
      if (!S_ISREG(sb.st_mode)) { return MMAPF_ENREG; } // not a regular file
      if (sb.st_size != size) { return MMAPF_ESIZE; } // wrong size
      if ((fd = open64(filename, fmode)) < 0) { return errno; } // open failed
    } else if (flags & MMAPF_CR) { // file missing, but creation requested
      if ((fd = open64(filename, fmode)) < 0) { return errno; } // open failed
      if ((ret = posix_fallocate(fd, 0, size)) != 0) {
        // EBADF is returned on an unsupported filesystem, ignore it
        if (ret != EBADF) { return ret; }
      }
    } else { // file missing, creation *not* requested
      return ENOENT;
    }

    //if ((ret = posix_fadvise(fd, 0, size, fadv)) != 0) { return ret; }
    posix_fadvise(fd, 0, size, fadv); // ignore result
    ctx->mem = mmap(NULL, ctx->mmap_sz, mmode, mflags, fd, 0);
    ctx->fd = fd;
  }

  if (ctx->mem == MAP_FAILED) {
    return errno;
  } else if (ctx->mem == NULL) {
    return ENOMEM;
  }

  if ((ret = posix_madvise(ctx->mem, ctx->mmap_sz, madv)) != 0) {
    munmap(ctx->mem, ctx->mmap_sz);
    ctx->mem = NULL;
    return ret;
  }

#ifdef MADV_HUGEPAGE
  // reduce overhead for large mappings
  if (size > (1<<26)) { madvise(ctx->mem, ctx->mmap_sz, MADV_HUGEPAGE); }
#endif
#ifdef MADV_DONTDUMP
  // don't include in a core dump
  madvise(ctx->mem, ctx->mmap_sz, MADV_DONTDUMP);
#endif

  return MMAPF_OKAY;
}

#endif /* _WIN32 */
