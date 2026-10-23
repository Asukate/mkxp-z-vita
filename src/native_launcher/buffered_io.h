#pragma once
#include <physfs.h>
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <string>

namespace hardrpg {
// A ZIP opened through PhysFS has its index read a few bytes at a time:
// about seventeen reads per file in the archive. On the Vita each one is a
// memory card request, so a ZIP with thousands of files took seconds to
// open. This reader serves those small reads from a 64 KB block instead.
namespace bufferedio {
constexpr size_t blockSize = 64 * 1024;

struct Reader {
  std::string path;
  int fd = -1;
  int64_t size = 0, pos = 0;
  int64_t blockAt = 0; // file offset of block[0]
  size_t blockLen = 0;
  char *block = nullptr;
};

inline Reader *openReader(const std::string &path) {
  int fd = ::open(path.c_str(), O_RDONLY);
  if (fd < 0)
    return nullptr;
  int64_t size = ::lseek(fd, 0, SEEK_END);
  char *block = static_cast<char *>(std::malloc(blockSize));
  if (size < 0 || !block || ::lseek(fd, 0, SEEK_SET) != 0) {
    std::free(block);
    ::close(fd);
    return nullptr;
  }
  auto *r = new Reader;
  r->path = path;
  r->fd = fd;
  r->size = size;
  r->block = block;
  return r;
}

inline PHYSFS_sint64 readAt(Reader &r, int64_t at, void *out, size_t len) {
  if (::lseek(r.fd, at, SEEK_SET) != at)
    return -1;
  size_t done = 0;
  while (done < len) {
    ssize_t n = ::read(r.fd, static_cast<char *>(out) + done, len - done);
    if (n < 0)
      return -1;
    if (n == 0)
      break;
    done += size_t(n);
  }
  return PHYSFS_sint64(done);
}

PHYSFS_Io *makeIo(Reader *r);

inline PHYSFS_sint64 ioRead(PHYSFS_Io *io, void *buf, PHYSFS_uint64 len) {
  auto &r = *static_cast<Reader *>(io->opaque);
  if (r.pos >= r.size || len == 0)
    return 0;
  if (PHYSFS_uint64(r.size - r.pos) < len)
    len = PHYSFS_uint64(r.size - r.pos);
  char *out = static_cast<char *>(buf);
  size_t done = 0;
  while (done < len) {
    int64_t at = r.pos;
    if (at >= r.blockAt && at < r.blockAt + int64_t(r.blockLen)) {
      size_t off = size_t(at - r.blockAt), n = std::min(size_t(len - done), r.blockLen - off);
      std::memcpy(out + done, r.block + off, n);
      done += n;
      r.pos += int64_t(n);
      continue;
    }
    // Large reads (file contents) go straight to the file.
    if (len - done >= blockSize) {
      PHYSFS_sint64 n = readAt(r, at, out + done, size_t(len - done));
      if (n <= 0)
        return done ? PHYSFS_sint64(done) : n;
      done += size_t(n);
      r.pos += n;
      break;
    }
    PHYSFS_sint64 n = readAt(r, at, r.block, blockSize);
    if (n <= 0) {
      r.blockLen = 0;
      return done ? PHYSFS_sint64(done) : n;
    }
    r.blockAt = at;
    r.blockLen = size_t(n);
  }
  return PHYSFS_sint64(done);
}

inline PHYSFS_sint64 ioWrite(PHYSFS_Io *, const void *, PHYSFS_uint64) { return -1; }
inline int ioSeek(PHYSFS_Io *io, PHYSFS_uint64 offset) {
  auto &r = *static_cast<Reader *>(io->opaque);
  if (offset > PHYSFS_uint64(r.size))
    return 0;
  r.pos = int64_t(offset);
  return 1;
}
inline PHYSFS_sint64 ioTell(PHYSFS_Io *io) { return static_cast<Reader *>(io->opaque)->pos; }
inline PHYSFS_sint64 ioLength(PHYSFS_Io *io) { return static_cast<Reader *>(io->opaque)->size; }
inline PHYSFS_Io *ioDuplicate(PHYSFS_Io *io) {
  Reader *copy = openReader(static_cast<Reader *>(io->opaque)->path);
  return copy ? makeIo(copy) : nullptr;
}
inline int ioFlush(PHYSFS_Io *) { return 1; }
inline void ioDestroy(PHYSFS_Io *io) {
  auto *r = static_cast<Reader *>(io->opaque);
  ::close(r->fd);
  std::free(r->block);
  delete r;
  delete io;
}

inline PHYSFS_Io *makeIo(Reader *r) {
  auto *io = new PHYSFS_Io{};
  io->version = 0;
  io->opaque = r;
  io->read = ioRead;
  io->write = ioWrite;
  io->seek = ioSeek;
  io->tell = ioTell;
  io->length = ioLength;
  io->duplicate = ioDuplicate;
  io->flush = ioFlush;
  io->destroy = ioDestroy;
  return io;
}
} // namespace bufferedio

// PHYSFS_mount for an archive file, read through the 64 KB block. Unmount it
// with PHYSFS_unmount(path) as usual.
inline bool mountArchive(const std::string &path, const std::string &point) {
  // Mounted already: PhysFS keeps that mount and would not take a new reader.
  bufferedio::Reader *r = PHYSFS_getMountPoint(path.c_str()) ? nullptr : bufferedio::openReader(path);
  if (!r)
    return PHYSFS_mount(path.c_str(), point.c_str(), 0) != 0;
  PHYSFS_Io *io = bufferedio::makeIo(r);
  if (PHYSFS_mountIo(io, path.c_str(), point.c_str(), 0))
    return true;
  io->destroy(io);
  return false;
}
} // namespace hardrpg
