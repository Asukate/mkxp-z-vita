#!/usr/bin/env python3
"""Fault-inject the production mounted-archive SDL callbacks."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
source = (root / 'src/filesystem/filesystem.cpp').read_text()
source = source[source.index('struct SDLRWIoContext'):source.index('static inline PHYSFS_File *sdlPHYS')]
prefix = r'''
#include <cassert>
#include <cstring>
#include <string>
#include <stdexcept>
#include <cstdint>
#include <algorithm>
#include <sys/stat.h>
using PHYSFS_sint64=int64_t; using PHYSFS_uint64=uint64_t;
struct SDL_RWops { int64_t pos=0; bool stale=false,closed=false; };
struct PHYSFS_Io {
 int version; void *opaque;
 int64_t (*read)(PHYSFS_Io*,void*,uint64_t);
 int64_t (*write)(PHYSFS_Io*,const void*,uint64_t);
 int (*seek)(PHYSFS_Io*,uint64_t);
 int64_t (*tell)(PHYSFS_Io*); int64_t (*length)(PHYSFS_Io*);
 PHYSFS_Io *(*duplicate)(PHYSFS_Io*); int (*flush)(PHYSFS_Io*); void (*destroy)(PHYSFS_Io*);
};
enum { RW_SEEK_SET=0, RW_SEEK_CUR=1 };
static unsigned generation; static int opens,closes;
static bool failOpen,failSeek; static SDL_RWops files[64];
unsigned vitaResumeEpoch() { return generation; }
SDL_RWops *SDL_RWFromFile(const char*,const char*) { return failOpen ? nullptr : &files[opens++]; }
int64_t SDL_RWsize(SDL_RWops *f) { return f->stale ? -1 : 8; }
int64_t SDL_RWseek(SDL_RWops *f,int64_t p,int mode) {
 if(f->stale||failSeek||p<0||p>8)return -1;
 if(mode==RW_SEEK_CUR)p+=f->pos;
 return f->pos=p;
}
size_t SDL_RWread(SDL_RWops *f,void *out,size_t,size_t n) {
 if(f->stale)return 0;
 n=std::min<size_t>(n,8-f->pos); memcpy(out,"abcdefgh"+f->pos,n); f->pos+=n; return n;
}
int SDL_RWclose(SDL_RWops *f) { assert(!f->closed);f->closed=true;++closes;return 0; }
const char *SDL_GetError() { return "fault"; }
struct Exception:std::runtime_error {
 enum { SDLError }; Exception(int,const char*,...):std::runtime_error("fault") {}
};
struct DebugStream { template<class T> DebugStream &operator<<(const T&) { return *this; } };
DebugStream Debug() { return {}; }
static int nativeMounts, streamMounts; static bool mountFailure;
static PHYSFS_Io *mountedIO; static std::string mountedAt;
int PHYSFS_mount(const char*,const char*,int) { ++nativeMounts; return 1; }
int PHYSFS_mountIo(PHYSFS_Io *io,const char*,const char *point,int) {
 ++streamMounts; mountedAt=point ? point : "";
 if(mountFailure)return 0;
 mountedIO=io; return 1;
}
'''
test = r'''
int main(int argc, char **argv) {
 assert(argc==3);
 assert(mountPath(argv[1],"rtp") && nativeMounts==0 && streamMounts==1 && mountedAt=="rtp");
 mountedIO->destroy(mountedIO);
 int before=closes; mountFailure=true;
 assert(!mountPath(argv[1],nullptr) && nativeMounts==0 && closes==before+1);
 mountFailure=false;
 assert(mountPath(argv[2],nullptr) && nativeMounts==1); // directories use native mount
 auto *io=createSDLRWIo("ux0:/data/game/Game.rgssad"); char out[8]{};
 assert(io && io->length(io)==8 && io->read(io,out,3)==3 && !memcmp(out,"abc",3));
 auto *ctx=static_cast<SDLRWIoContext*>(io->opaque); auto *old=ctx->ops;
 auto *clone=io->duplicate(io); assert(clone && clone->tell(clone)==3);
 old->stale=true; static_cast<SDLRWIoContext*>(clone->opaque)->ops->stale=true;
 ++generation;
 assert(io->read(io,out,3)==3 && !memcmp(out,"def",3) && old->closed);
 assert(clone->read(clone,out,2)==2 && !memcmp(out,"de",2));
 assert(io->read(io,out,8)==2 && !memcmp(out,"gh",2));
 int count=opens; assert(io->read(io,out,8)==0 && opens==count); // true EOF
 ctx->ops->stale=true; assert(io->seek(io,1) && io->tell(io)==1); // missed wake event
 ++generation; failOpen=true; old=ctx->ops;
 assert(io->read(io,out,1)==-1 && !old->closed && ctx->position==1);
 failOpen=false; failSeek=true;
 assert(io->read(io,out,1)==-1 && !old->closed && files[opens-1].closed);
 failSeek=false;
 assert(io->read(io,out,2)==2 && !memcmp(out,"bc",2) && old->closed);
 count=opens; assert(!io->seek(io,UINT64_MAX) && opens==count);
 failSeek=true; assert(!io->duplicate(io)); failSeek=false;
 io->destroy(io); clone->destroy(clone);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp) / 'test.cpp'
    path.write_text(prefix + source + test)
    subprocess.run(['c++', '-std=c++14', '-D__vita__', '-Wall', '-Wextra', '-I', str(root), str(path), '-o', tmp + '/test'], check=True)
    archive = Path(tmp) / 'archive.rgssad'
    archive.write_bytes(b'abcdefgh')
    subprocess.run([tmp + '/test', str(archive), tmp], check=True)
print('Mounted-archive resume, cursor, duplication, EOF and failure tests passed')
