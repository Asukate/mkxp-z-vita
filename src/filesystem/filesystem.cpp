/*
** filesystem.cpp
**
** This file is part of mkxp.
**
** Copyright (C) 2013 - 2021 Amaryllis Kulla <ancurio@mapleshrine.eu>
**
** mkxp is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 2 of the License, or
** (at your option) any later version.
**
** mkxp is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with mkxp.  If not, see <http://www.gnu.org/licenses/>.
*/

/* Vita/POSIX compat — MUST be before ghc/filesystem include (included by filesystem.h) */
#if defined(__vita__) || defined(__psp2__)
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <SDL_error.h>
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef __vita__
extern "C" {
int strcasecmp(const char *a, const char *b) {
    while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}
int strncasecmp(const char *a, const char *b, size_t n) {
    while (n && *a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; n--; }
    return n ? tolower((unsigned char)*a) - tolower((unsigned char)*b) : 0;
}
int strerror_r(int errnum, char *buf, size_t buflen) { const char *s = strerror(errnum); snprintf(buf, buflen, "%s", s); return 0; }
int symlink(const char *target, const char *linkpath) { (void)target; (void)linkpath; errno = ENOTSUP; return -1; }
ssize_t readlink(const char *path, char *buf, size_t bufsiz) { (void)path; (void)buf; (void)bufsiz; errno = EINVAL; return -1; }
int utimensat(int dirfd, const char *pathname, const struct timespec times[2], int flags) { (void)dirfd; (void)pathname; (void)times; (void)flags; errno = ENOTSUP; return -1; }
int truncate(const char *path, off_t length) { (void)path; (void)length; errno = ENOTSUP; return -1; }
}
#endif
#ifndef AT_FDCWD
#define AT_FDCWD -100
#endif
#ifndef AT_SYMLINK_NOFOLLOW
#define AT_SYMLINK_NOFOLLOW 0x100
#endif
#endif

#include "filesystem.h"
#include "vita_diagnostic.h"
#ifdef __vita__
#include "vita_resume.h"
#include "resume-read.h"
#include <new>
#include <limits>
#endif

#include "util/boost-hash.h"
#include "util/debugwriter.h"
#include "util/exception.h"
#include "util/util.h"
#include "display/font.h"
#include "crypto/rgssad.h"

#include "eventthread.h"
#include "sharedstate.h"

#include <physfs.h>

#include <algorithm>
#include <stack>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <vector>

#ifdef __APPLE__
#include <iconv.h>
#endif

#ifdef __WIN32__
#include <direct.h>
#endif

struct SDLRWIoContext {
  SDL_RWops *ops;
  std::string filename;
#ifdef __vita__
  int64_t position = 0, length = -1;
  unsigned epoch = vitaResumeEpoch();

  bool reopen() {
    SDL_RWops *fresh = SDL_RWFromFile(filename.c_str(), "rb");
    if (!fresh) return false;
    if (position && SDL_RWseek(fresh, position, RW_SEEK_SET) != position) {
      SDL_RWclose(fresh);
      return false;
    }
    SDL_RWclose(ops);
    ops = fresh;
    epoch = vitaResumeEpoch();
    return true;
  }
  bool prepare() { return epoch == vitaResumeEpoch() || reopen(); }
#endif

  SDLRWIoContext(const char *filename)
      : ops(SDL_RWFromFile(filename, "r")), filename(filename) {
    if (!ops)
      throw Exception(Exception::SDLError, "Failed to open file: %s",
                      SDL_GetError());
#ifdef __vita__
    length = SDL_RWsize(ops);
#endif
  }

  ~SDLRWIoContext() { SDL_RWclose(ops); }
};

static PHYSFS_Io *createSDLRWIo(const char *filename);

#ifndef __vita__
static SDL_RWops *getSDLRWops(PHYSFS_Io *io) {
  return static_cast<SDLRWIoContext *>(io->opaque)->ops;
}
#endif

static PHYSFS_sint64 SDLRWIoRead(struct PHYSFS_Io *io, void *buf,
                                 PHYSFS_uint64 len) {
#ifdef __vita__
  auto *ctx = static_cast<SDLRWIoContext *>(io->opaque);
  if (len > SIZE_MAX || !ctx->prepare()) return -1;
  size_t count = SDL_RWread(ctx->ops, buf, 1, static_cast<size_t>(len));
  /* SDL fread reports zero both for EOF and an invalid remounted handle.
   * Retry only when the archive's known length says data remains. */
  if (!count && len && ctx->position < ctx->length && ctx->reopen())
    count = SDL_RWread(ctx->ops, buf, 1, static_cast<size_t>(len));
  ctx->position += count;
  return count;
#else
  return SDL_RWread(getSDLRWops(io), buf, 1, len);
#endif
}

static int SDLRWIoSeek(struct PHYSFS_Io *io, PHYSFS_uint64 offset) {
#ifdef __vita__
  auto *ctx = static_cast<SDLRWIoContext *>(io->opaque);
  if (offset > INT64_MAX || !ctx->prepare()) return 0;
  auto result = SDL_RWseek(ctx->ops, static_cast<int64_t>(offset), RW_SEEK_SET);
  if (result < 0 && ctx->reopen())
    result = SDL_RWseek(ctx->ops, static_cast<int64_t>(offset), RW_SEEK_SET);
  if (result < 0) return 0;
  ctx->position = result;
  return result == static_cast<int64_t>(offset);
#else
  return (SDL_RWseek(getSDLRWops(io), offset, RW_SEEK_SET) != -1);
#endif
}

static PHYSFS_sint64 SDLRWIoTell(struct PHYSFS_Io *io) {
#ifdef __vita__
  auto *ctx = static_cast<SDLRWIoContext *>(io->opaque);
  return ctx->prepare() ? ctx->position : -1;
#else
  return SDL_RWseek(getSDLRWops(io), 0, RW_SEEK_CUR);
#endif
}

static PHYSFS_sint64 SDLRWIoLength(struct PHYSFS_Io *io) {
#ifdef __vita__
  auto *ctx = static_cast<SDLRWIoContext *>(io->opaque);
  return ctx->prepare() ? ctx->length : -1;
#else
  return SDL_RWsize(getSDLRWops(io));
#endif
}

static struct PHYSFS_Io *SDLRWIoDuplicate(struct PHYSFS_Io *io) {
  SDLRWIoContext *ctx = static_cast<SDLRWIoContext *>(io->opaque);
  int64_t offset = io->tell(io);
  PHYSFS_Io *dup = createSDLRWIo(ctx->filename.c_str());

  if (dup && (offset < 0 || !SDLRWIoSeek(dup, offset))) {
    dup->destroy(dup);
    return nullptr;
  }

  return dup;
}

static void SDLRWIoDestroy(struct PHYSFS_Io *io) {
  delete static_cast<SDLRWIoContext *>(io->opaque);
  delete io;
}

static PHYSFS_Io SDLRWIoTemplate = {0,
                                    0, /* version, opaque */
                                    SDLRWIoRead,
                                    0, /* write */
                                    SDLRWIoSeek,
                                    SDLRWIoTell,
                                    SDLRWIoLength,
                                    SDLRWIoDuplicate,
                                    0, /* flush */
                                    SDLRWIoDestroy};

static PHYSFS_Io *createSDLRWIo(const char *filename) {
  SDLRWIoContext *ctx;

  try {
    ctx = new SDLRWIoContext(filename);
  } catch (const Exception &e) {
    Debug() << "Failed mounting" << filename;
    return 0;
  }

  PHYSFS_Io *io = new PHYSFS_Io;
  *io = SDLRWIoTemplate;
  io->opaque = ctx;

  return io;
}

static int mountPath(const char *path, const char *mountpoint) {
#ifdef __vita__
  /* Physical archive handles survive for the lifetime of the mount. Route
   * those through the wake-aware stream too, including RGSS archives. */
  struct stat info;
  if (stat(path, &info) == 0 && S_ISREG(info.st_mode)) {
    PHYSFS_Io *io = createSDLRWIo(path);
    if (!io) return 0;
    const int mounted = PHYSFS_mountIo(io, path, mountpoint, 1);
    if (!mounted) io->destroy(io);
    return mounted;
  }
#endif
  int mounted = PHYSFS_mount(path, mountpoint, 1);
  if (!mounted) {
    PHYSFS_Io *io = createSDLRWIo(path);
    if (io) {
      mounted = PHYSFS_mountIo(io, path, mountpoint, 1);
      if (!mounted) io->destroy(io);
    }
  }
  return mounted;
}

static inline PHYSFS_File *sdlPHYS(SDL_RWops *ops) {
  return static_cast<PHYSFS_File *>(ops->hidden.unknown.data1);
}

#ifdef __vita__
struct PhysReadIO {
  static PHYSFS_File *open(const char *path) { return PHYSFS_openRead(path); }
  static bool seek(PHYSFS_File *file, int64_t offset) { return PHYSFS_seek(file, offset) != 0; }
  static void close(PHYSFS_File *file) { PHYSFS_close(file); }
};
using RecoverRead = ResumeRead<PHYSFS_File *, PhysReadIO>;
static RecoverRead *sdlRecovery(SDL_RWops *ops) {
  return static_cast<RecoverRead *>(ops->hidden.unknown.data2);
}
static bool prepareRead(SDL_RWops *ops) {
  RecoverRead *read = sdlRecovery(ops);
  if (!read) return true;
  bool ok = read->ensure(vitaResumeEpoch());
  ops->hidden.unknown.data1 = read->file;
  return ok;
}
static bool repairRead(SDL_RWops *ops, int64_t position) {
  RecoverRead *read = sdlRecovery(ops);
  bool ok = read && read->reopen(position);
  if (read) ops->hidden.unknown.data1 = read->file;
  return ok;
}
#endif

static Sint64 SDL_RWopsSize(SDL_RWops *ops) {
#ifdef __vita__
  if (!prepareRead(ops)) return -1;
#endif
  PHYSFS_File *f = sdlPHYS(ops);

  if (!f)
    return -1;

  Sint64 length = PHYSFS_fileLength(f);
#ifdef __vita__
  RecoverRead *read = sdlRecovery(ops);
  if (length < 0 && read && repairRead(ops, read->position))
    length = PHYSFS_fileLength(sdlPHYS(ops));
#endif
  return length;
}

static Sint64 SDL_RWopsSeek(SDL_RWops *ops, int64_t offset, int whence) {
#ifdef __vita__
  if (!prepareRead(ops)) return -1;
#endif
  PHYSFS_File *f = sdlPHYS(ops);

  if (!f)
    return -1;

  int64_t base;

  switch (whence) {
  default:
  case RW_SEEK_SET:
    base = 0;
    break;
  case RW_SEEK_CUR:
#ifdef __vita__
    base = sdlRecovery(ops) ? sdlRecovery(ops)->position : PHYSFS_tell(f);
#else
    base = PHYSFS_tell(f);
#endif
    break;
  case RW_SEEK_END:
    base = PHYSFS_fileLength(f);
    break;
  }

  if (base < 0 || (offset < 0 && offset < -base)) return -1;
#ifdef __vita__
  if (offset > 0 && base > std::numeric_limits<int64_t>::max() - offset) return -1;
#endif
  const int64_t target = base + offset;
  int result = PHYSFS_seek(f, target);
#ifdef __vita__
  if (!result) {
    Sint64 length = PHYSFS_fileLength(f);
    if (length < 0 || target <= length) result = repairRead(ops, target);
  }
  if (result && sdlRecovery(ops)) sdlRecovery(ops)->position = target;
  f = sdlPHYS(ops);
#endif

  return (result != 0) ? PHYSFS_tell(f) : -1;
}

static size_t SDL_RWopsRead(SDL_RWops *ops, void *buffer, size_t size,
                            size_t maxnum) {
  if (!size || !maxnum || maxnum > SIZE_MAX / size) return 0;
#ifdef __vita__
  if (!prepareRead(ops)) return 0;
#endif
  PHYSFS_File *f = sdlPHYS(ops);

  if (!f)
    return 0;

  PHYSFS_sint64 result = PHYSFS_readBytes(f, buffer, size * maxnum);
#ifdef __vita__
  RecoverRead *read = sdlRecovery(ops);
  const PHYSFS_sint64 length = result == 0 ? PHYSFS_fileLength(f) : 0;
  if (read && (result < 0 || (result == 0 && (length < 0 || read->position < length))) &&
      repairRead(ops, read->position))
    result = PHYSFS_readBytes(sdlPHYS(ops), buffer, size * maxnum);
  if (read && result > 0) read->position += result;
#endif

  return (result != -1) ? (result / size) : 0;
}

static size_t SDL_RWopsWrite(SDL_RWops *ops, const void *buffer, size_t size,
                             size_t num) {
  PHYSFS_File *f = sdlPHYS(ops);

  if (!f)
    return 0;

  PHYSFS_sint64 result = PHYSFS_writeBytes(f, buffer, size * num);

  return (result != -1) ? (result / size) : 0;
}

static int SDL_RWopsClose(SDL_RWops *ops) {
  PHYSFS_File *f = sdlPHYS(ops);

  if (!f)
    return -1;

  int result = PHYSFS_close(f);
#ifdef __vita__
  delete sdlRecovery(ops);
  ops->hidden.unknown.data2 = 0;
#endif
  ops->hidden.unknown.data1 = 0;

  return (result != 0) ? 0 : -1;
}

static int SDL_RWopsCloseFree(SDL_RWops *ops) {
  int result = SDL_RWopsClose(ops);

  SDL_FreeRW(ops);

  return result;
}

/* Copies the first srcN characters from src into dst,
 * or the full string if srcN == -1. Never writes more
 * than dstMax, and guarantees dst to be null terminated.
 * Returns copied bytes (minus terminating null) */
static size_t strcpySafe(char *dst, const char *src, size_t dstMax, int srcN) {
  if (srcN < 0)
    srcN = strlen(src);

  size_t cpyMax = std::min<size_t>(dstMax - 1, srcN);

  memcpy(dst, src, cpyMax);
  dst[cpyMax] = '\0';

  return cpyMax;
}

/* Attempt to locate an extension string in a filename.
 * Either a pointer into the input string pointing at the
 * extension, or null is returned */
static const char *findExt(const char *filename) {
  size_t len;

  for (len = strlen(filename); len > 0; --len) {
    if (filename[len] == '/')
      return 0;

    if (filename[len] == '.')
      return &filename[len + 1];
  }

  return 0;
}

static void initReadOps(PHYSFS_File *handle, SDL_RWops &ops, bool freeOnClose,
                        const char *path = nullptr) {
  ops.size = SDL_RWopsSize;
  ops.seek = SDL_RWopsSeek;
  ops.read = SDL_RWopsRead;
  ops.write = SDL_RWopsWrite;

  if (freeOnClose)
    ops.close = SDL_RWopsCloseFree;
  else
    ops.close = SDL_RWopsClose;

  ops.type = SDL_RWOPS_PHYSFS;
  ops.hidden.unknown.data1 = handle;
  ops.hidden.unknown.data2 = nullptr;
#ifdef __vita__
  if (path && strlen(path) < sizeof(RecoverRead::path))
    ops.hidden.unknown.data2 = new (std::nothrow) RecoverRead(handle, path, vitaResumeEpoch());
#endif
}

static void strTolower(std::string &str) {
  for (size_t i = 0; i < str.size(); ++i)
    str[i] = tolower(str[i]);
}

const Uint32 SDL_RWOPS_PHYSFS = SDL_RWOPS_UNKNOWN + 10;

struct FileSystemPrivate {
  /* Maps: lower case full filepath,
   * To:   mixed case full filepath */
  BoostHash<std::string, std::string> pathCache;
  /* Maps: lower case directory path,
   * To:   list of lower case filenames */
  BoostHash<std::string, std::vector<std::string>> fileLists;

  /* This is for compatibility with games that take Windows'
   * case insensitivity for granted */
  bool havePathCache;
};

static void throwPhysfsError(const char *desc) {
  PHYSFS_ErrorCode ec = PHYSFS_getLastErrorCode();
  const char *englishStr;
    if (ec == 0) {
        // Sometimes on Windows PHYSFS_init can return null
        // but the error code never changes
        englishStr = "unknown error";
    } else {
        englishStr = PHYSFS_getErrorByCode(ec);
    }

  throw Exception(Exception::PHYSFSError, "%s: %s", desc, englishStr);
}

FileSystem::FileSystem(const char *argv0, bool allowSymlinks) {
  if (PHYSFS_init(argv0) == 0)
    throwPhysfsError("Error initializing PhysFS");

#ifdef __vita__
  /* Vita: hardcode all physfs paths to avoid SceAppUtil/SceCommonDialog crash */
  if (!PHYSFS_setWriteDir("ux0:/data/"))
    Debug() << "PhysFS: setWriteDir failed, using default pref dir";
  /* Content mounts belong to SharedState's active-game plan. Never mount a
   * different game's assets here: they would shadow this game's archive/RTP. */
#endif

  /* One error (=return 0) turns the whole product to 0 */

  int er = 1;

  er *= PHYSFS_registerArchiver(&RGSS1_Archiver);
  er *= PHYSFS_registerArchiver(&RGSS2_Archiver);
  er *= PHYSFS_registerArchiver(&RGSS3_Archiver);

  if (er == 0)
    throwPhysfsError("Error registering PhysFS RGSS archiver");

  p = new FileSystemPrivate;
  p->havePathCache = false;

  if (allowSymlinks)
    PHYSFS_permitSymbolicLinks(1);
}

FileSystem::~FileSystem() {
  delete p;

  if (PHYSFS_deinit() == 0)
    Debug() << "PhyFS failed to deinit.";
}

void FileSystem::addPath(const char *path, const char *mountpoint, bool reload, const char *archiveRoot) {
    int state = mountPath(path, mountpoint);
    if (!state) {
        PHYSFS_ErrorCode err = PHYSFS_getLastErrorCode();
        throw Exception(Exception::PHYSFSError, "Failed to mount %s (%s)", path, PHYSFS_getErrorByCode(err));
    }
    if (archiveRoot && *archiveRoot && !PHYSFS_setRoot(path, archiveRoot)) {
        const auto err = PHYSFS_getLastErrorCode();
        PHYSFS_unmount(path);
        throw Exception(Exception::PHYSFSError, "Failed to use RTP folder %s in %s (%s)",
                        archiveRoot, path, PHYSFS_getErrorByCode(err));
    }
    
    if (reload) reloadPathCache();
}

void FileSystem::removePath(const char *path, bool reload) {
    
    if (!PHYSFS_unmount(path)) {
        PHYSFS_ErrorCode err = PHYSFS_getLastErrorCode();
        throw Exception(Exception::PHYSFSError, "Failed to unmount %s (%s)", path, PHYSFS_getErrorByCode(err));
    }
    
    if (reload) reloadPathCache();
}

struct CacheEnumData {
  FileSystemPrivate *p;
  std::stack<std::vector<std::string> *> fileLists;

#ifdef __APPLE__
  iconv_t nfd2nfc;
  char buf[512];
#endif

  CacheEnumData(FileSystemPrivate *p) : p(p) {
#ifdef __APPLE__
    nfd2nfc = iconv_open("utf-8", "utf-8-mac");
#endif
  }

  ~CacheEnumData() {
#ifdef __APPLE__
    iconv_close(nfd2nfc);
#endif
  }

  /* Converts in-place */
  void toNFC(char *inout) {
#ifdef __APPLE__
    size_t srcSize = strlen(inout);
    size_t bufSize = sizeof(buf);
    char *bufPtr = buf;
    char *inoutPtr = inout;

    /* Reserve room for null terminator */
    --bufSize;

    iconv(nfd2nfc, &inoutPtr, &srcSize, &bufPtr, &bufSize);
    /* Null-terminate */
    *bufPtr = 0;
    strcpy(inout, buf);
#else
    (void)inout;
#endif
  }
};

static PHYSFS_EnumerateCallbackResult cacheEnumCB(void *d, const char *origdir,
                                                  const char *fname) {
  if (shState && shState->rtData().rqTerm)
    throw Exception(Exception::MKXPError, "Game close requested. Aborting path cache enumeration.");

  CacheEnumData &data = *static_cast<CacheEnumData *>(d);
  char fullPath[512];

  if (!*origdir)
    snprintf(fullPath, sizeof(fullPath), "%s", fname);
  else
    snprintf(fullPath, sizeof(fullPath), "%s/%s", origdir, fname);

  /* Deal with OSX' weird UTF-8 standards */
  data.toNFC(fullPath);

  std::string mixedCase(fullPath);
  std::string lowerCase = mixedCase;
  strTolower(lowerCase);

  PHYSFS_Stat stat;
  PHYSFS_stat(fullPath, &stat);

  if (stat.filetype == PHYSFS_FILETYPE_DIRECTORY) {
    /* Create a new list for this directory */
    std::vector<std::string> &list = data.p->fileLists[lowerCase];

    /* Iterate over its contents */
    data.fileLists.push(&list);
    PHYSFS_enumerate(fullPath, cacheEnumCB, d);
    data.fileLists.pop();
  } else {
    /* Get the file list for the directory we're currently
     * traversing and append this filename to it */
    std::vector<std::string> &list = *data.fileLists.top();

    std::string lowerFilename(fname);
    strTolower(lowerFilename);
    list.push_back(lowerFilename);

    /* Add the lower -> mixed mapping of the file's full path */
    data.p->pathCache.insert(lowerCase, mixedCase);
  }

  return PHYSFS_ENUM_OK;
}

void FileSystem::createPathCache() {
  Debug() << "Loading path cache...";

  CacheEnumData data(p);
  data.fileLists.push(&p->fileLists[""]);
  PHYSFS_enumerate("", cacheEnumCB, &data);

  p->havePathCache = true;

  Debug() << "Path cache completed.";
}

void FileSystem::reloadPathCache() {
    if (!p->havePathCache) return;
    
    p->fileLists.clear();
    p->pathCache.clear();
    createPathCache();
}

struct FontSetsCBData {
  FileSystemPrivate *p;
  SharedFontState *sfs;
};

static PHYSFS_EnumerateCallbackResult fontSetEnumCB(void *data, const char *dir,
                                                    const char *fname) {
  FontSetsCBData *d = static_cast<FontSetsCBData *>(data);

  /* Only consider filenames with font extensions */
  const char *ext = findExt(fname);

  if (!ext)
    return PHYSFS_ENUM_OK;

  char lowExt[8];
  size_t i;

  for (i = 0; i < sizeof(lowExt) - 1 && ext[i]; ++i)
    lowExt[i] = tolower(ext[i]);
  lowExt[i] = '\0';

  if (strcmp(lowExt, "ttf") && strcmp(lowExt, "otf"))
    return PHYSFS_ENUM_OK;

  char filename[512];
  snprintf(filename, sizeof(filename), "%s/%s", dir, fname);

  PHYSFS_File *handle = PHYSFS_openRead(filename);

  if (!handle)
    return PHYSFS_ENUM_ERROR;

  SDL_RWops ops;
  initReadOps(handle, ops, false);

  d->sfs->initFontSetCB(ops, filename);

  SDL_RWclose(&ops);

  return PHYSFS_ENUM_OK;
}

/* Basically just a case-insensitive search
 * for the folder "Fonts"... */
static PHYSFS_EnumerateCallbackResult
findFontsFolderCB(void *data, const char *, const char *fname) {
  size_t i = 0;
  char buffer[512];
  const char *s = fname;

  while (*s && i < sizeof(buffer))
    buffer[i++] = tolower(*s++);

  buffer[i] = '\0';

  if (strcmp(buffer, "fonts") == 0)
    PHYSFS_enumerate(fname, fontSetEnumCB, data);

  return PHYSFS_ENUM_OK;
}

void FileSystem::initFontSets(SharedFontState &sfs) {
  FontSetsCBData d = {p, &sfs};

  PHYSFS_enumerate("", findFontsFolderCB, &d);
}

struct OpenReadEnumData {
  FileSystem::OpenHandler &handler;
  SDL_RWops ops;

  /* The filename (without directory) we're looking for */
  const char *filename;
  size_t filenameN;

  /* Optional hash to translate full filepaths
   * (used with path cache) */
  BoostHash<std::string, std::string> *pathTrans;

  /* Number of files we've attempted to read and parse */
  size_t matchCount;
  bool stopSearching;

  /* In case of a PhysFS error, save it here so it
   * doesn't get changed before we get back into our code */
  const char *physfsError;

  OpenReadEnumData(FileSystem::OpenHandler &handler, const char *filename,
                   size_t filenameN,
                   BoostHash<std::string, std::string> *pathTrans)
      : handler(handler), filename(filename), filenameN(filenameN),
        pathTrans(pathTrans), matchCount(0), stopSearching(false),
        physfsError(0) {}
};

static PHYSFS_EnumerateCallbackResult
openReadEnumCB(void *d, const char *dirpath, const char *filename) {
  OpenReadEnumData &data = *static_cast<OpenReadEnumData *>(d);
  char buffer[512];
  const char *fullPath;

  if (data.stopSearching)
    return PHYSFS_ENUM_STOP;

  /* If there's not even a partial match, continue searching */
#ifdef __vita__
  /* VX Ace games commonly rely on Windows' case-insensitive filesystem.
   * Keep Vita startup lazy when the full path cache is disabled, but accept
   * a differently-cased leaf name returned by PhysFS enumeration. */
  if (strncasecmp(filename, data.filename, data.filenameN) != 0)
#else
  if (strncmp(filename, data.filename, data.filenameN) != 0)
#endif
    return PHYSFS_ENUM_OK;

  if (!*dirpath) {
    fullPath = filename;
  } else {
    snprintf(buffer, sizeof(buffer), "%s/%s", dirpath, filename);
    fullPath = buffer;
  }

  char last = filename[data.filenameN];
  /* If fname matches up to a following '.' (meaning the rest is part
   * of the extension), or up to a following '\0' (full match), we've
   * found our file */
  if (last != '.' && last != '\0')
    return PHYSFS_ENUM_OK;

  /* If the path cache is active, translate from lower case
   * to mixed case path */
  if (data.pathTrans)
    fullPath = (*data.pathTrans)[fullPath].c_str();

  PHYSFS_File *phys = PHYSFS_openRead(fullPath);

  if (!phys) {
    /* Failing to open this file here means there must
     * be a deeper rooted problem somewhere within PhysFS.
     * Just abort alltogether. */
    data.stopSearching = true;
    data.physfsError = PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode());

    return PHYSFS_ENUM_ERROR;
  }
  initReadOps(phys, data.ops, false, fullPath);

#if defined(__vita__) && defined(MKXPZ_VITA_DIAGNOSTICS)
  if (strncmp(fullPath, "Graphics/Tilesets/", 18) == 0) {
    const char *origin = PHYSFS_getRealDir(fullPath);
    vitaDiagLog("CONTENT", "tileset requested=%s selected=%s origin=%s",
                data.filename, fullPath, origin ? origin : "unknown");
  }
#endif

  const char *ext = findExt(filename);

  if (data.handler.tryRead(data.ops, ext))
    data.stopSearching = true;

  ++data.matchCount;
  return PHYSFS_ENUM_OK;
}

void FileSystem::openRead(OpenHandler &handler, const char *filename) {
  std::string filename_nm = normalize(filename, false, false);
  char buffer[512];
  size_t len = strcpySafe(buffer, filename_nm.c_str(), sizeof(buffer), -1);
  char *delim;

  if (p->havePathCache)
    for (size_t i = 0; i < len; ++i)
      buffer[i] = tolower(buffer[i]);

  /* Find the deliminator separating directory and file name */
  for (delim = buffer + len; delim > buffer; --delim)
    if (*delim == '/')
      break;

  const bool root = (delim == buffer);

  const char *file = buffer;
  const char *dir = "";

  if (!root) {
    /* Cut the buffer in half so we can use it
     * for both filename and directory path */
    *delim = '\0';
    file = delim + 1;
    dir = buffer;
  }
  OpenReadEnumData data(handler, file, len + buffer - delim - !root,
                        p->havePathCache ? &p->pathCache : 0);

  if (p->havePathCache) {
    /* Get the list of files contained in this directory
     * and manually iterate over them */
    const std::vector<std::string> &fileList = p->fileLists[dir];

    for (size_t i = 0; i < fileList.size(); ++i)
      openReadEnumCB(&data, dir, fileList[i].c_str());
  } else {
    PHYSFS_enumerate(dir, openReadEnumCB, &data);
  }

  if (data.physfsError)
    throw Exception(Exception::PHYSFSError, "PhysFS: %s", data.physfsError);

  if (data.matchCount == 0)
    throw Exception(Exception::NoFileError, "%s", filename);
}

void FileSystem::openReadRaw(SDL_RWops &ops, const char *filename,
                             bool freeOnClose) {

  PHYSFS_File *handle = PHYSFS_openRead(normalize(filename, 0, 0).c_str());

  if (!handle)
    throw Exception(Exception::NoFileError, "%s", filename);

  initReadOps(handle, ops, freeOnClose, normalize(filename, 0, 0).c_str());
    return;
}

std::string FileSystem::normalize(const char *pathname, bool preferred,
                            bool absolute) {
    return filesystemImpl::normalizePath(pathname, preferred, absolute);
}

bool FileSystem::exists(const char *filename) {
  return PHYSFS_exists(normalize(filename, false, false).c_str());
}

const char *FileSystem::desensitize(const char *filename) {
  std::string fn_lower(filename);
    
  std::transform(fn_lower.begin(), fn_lower.end(), fn_lower.begin(), [](unsigned char c){
      return std::tolower(c);
  });
  if (p->havePathCache && p->pathCache.contains(fn_lower))
    return p->pathCache[fn_lower].c_str();
  return filename;
}
