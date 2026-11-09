#!/usr/bin/env python3
"""Run actual SDL/VitaGL ownership paths with a denied second image allocation."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parent.parent
vgl = Path(sys.argv[1])
shim = (root / 'deps/vendor/vita-sdl2-shim/src/SDL_vita_stubs.c').read_text()

def function(text, signature):
    start = text.index(signature)
    brace = text.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

start = shim.index('#define VITA_BLOCK_MIN')
end = shim.index('\n#endif\n\n/* outside', start)
registry = shim[start:end]
alloc = function(shim, 'static void *vita_big_alloc(')
release = function(shim, 'static void vita_big_free(')
upload = function((vgl / 'source/utils/gpu_utils.c').read_text(), 'void gpu_alloc_texture(')
mem = (vgl / 'source/utils/mem_utils.c').read_text()
free = function(mem, 'void vgl_free(')
size = function(mem, 'size_t vgl_malloc_usable_size(')
resize = function(mem, 'void *vgl_realloc(')
preamble = r'''
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <map>
#include <vector>
#define __vita__
#define HAVE_CUSTOM_HEAP
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_RW 1
#define SCE_GXM_MEMORY_ATTRIB_RW 3
static std::map<int,void*> blocks;
static unsigned maps,unmaps,frees,allocations;
static int next_uid;
static bool map_fail,kernel_fail,heap_fail,pool_allowed;
static unsigned forced_allocations;
static void *pixel_memalign(size_t alignment,size_t n){return heap_fail?nullptr:memalign(alignment,n);}
#define memalign pixel_memalign
int sceKernelAllocMemBlock(const char*,int,size_t n,void*) {
 if(kernel_fail)return -1;
 void *p=aligned_alloc(4096,n);assert(p);blocks[++next_uid]=p;return next_uid;
}
int sceKernelGetMemBlockBase(int uid,void **p){*p=blocks.at(uid);return 0;}
int sceKernelFreeMemBlock(int uid){std::free(blocks.at(uid));blocks.erase(uid);++frees;return 0;}
int sceKernelDelayThread(unsigned){return 0;}
int sceGxmMapMemory(void*,size_t,int){if(map_fail)return -1;++maps;return 0;}
int sceGxmUnmapMemory(void*){++unmaps;return 0;}
extern "C" void *vglMalloc(uint32_t n){return pool_allowed?std::malloc(n):nullptr;}
extern "C" void *vglForceAlloc(uint32_t n){
 ++forced_allocations;
 return std::aligned_alloc(64,n);
}
extern "C" void vglFree(void *p){std::free(p);}
using GLboolean=bool;
using SceGxmTextureFormat=unsigned;
enum vglMemType{VGL_MEM_MAIN,VGL_MEM_RAM,VGL_MEM_EXTERNAL};
enum{TEX_VALID=1,TEX_UNINITIALIZED=0,OBJ_NOT_USED=0,SCE_GXM_TEXTURE_BASE_FORMAT_P8=0x95000000};
#define VGL_ALIGN(n,a) (((n)+(a)-1)&~((a)-1))
struct GxmTexture{void *data;unsigned w,h;};
struct texture{int status=0,mip_count=0,last_frame=0;GxmTexture gxm_tex={};void *data=nullptr,*palette_data=nullptr;};
static unsigned vgl_texture_alloc_failures;
static void *color_table;
unsigned tex_format_to_bytespp(unsigned n){return n;}
void *gpu_alloc_mapped(size_t n,vglMemType){++allocations;return pool_allowed?std::malloc(n):nullptr;}
void vgl_fast_memcpy(void *d,const void *s,size_t n){std::memcpy(d,s,n);}
void vgl_memset(void *d,int c,size_t n){std::memset(d,c,n);}
void vglInitLinearTexture(GxmTexture *t,void *p,unsigned,unsigned w,unsigned h,int){*t={p,w,h};}
static std::vector<void*> deferred;
void gpu_free_texture_data(texture *t){deferred.push_back(t->data);t->status=TEX_UNINITIALIZED;}
vglMemType vgl_mem_get_type_by_addr(void*){return VGL_MEM_EXTERNAL;}
size_t sceClibMspaceMallocUsableSize(void*){assert(false);return 0;}
void heap_blk_free(void*){assert(false);}
void *vgl_malloc(size_t n,vglMemType){return pool_allowed?std::malloc(n):nullptr;}
'''
checks = r'''
static const unsigned w=2048,h=1024,bytes=w*h*4;
static void *image(unsigned n=bytes){int outside=0;void *p=vita_big_alloc(n,0,&outside,0);assert(p&&outside);memset(p,0xab,n);return p;}
static void clean(){assert(blocks.empty()&&vita_block_bytes==0&&vita_texture_count==0);}
int main(){
 // A real owned decode succeeds with the ordinary GPU allocation denied.
 void *p=image();texture t;unsigned before=allocations;
 vitaBeginPixelTransfer(p);gpu_alloc_texture(w,h,4,p,&t,4,nullptr,nullptr,true);vitaEndPixelTransfer();
 assert(t.status==TEX_VALID&&t.data==p&&t.gxm_tex.data==p&&allocations==before+1);
 assert(((unsigned char*)p)[0]==0xab&&((unsigned char*)p)[bytes-1]==0xab);
 assert(vgl_malloc_usable_size(p)==bytes&&maps==1);
 uint32_t count,total;vitaPixelBlockFigures(&count,&total);assert(count==0&&total==0);
 vitaPixelTextureFigures(&count,&total);assert(count==1&&total==bytes);
 // Releasing SDL's surface must keep mapped storage alive until GPU free.
 vita_big_free(p);assert(blocks.size()==1&&unmaps==0&&frees==0);
 vgl_free(p);assert(unmaps==1&&frees==1);assert(!mkxp_free_texture_pixels(p));clean();
 // A failed map leaves SDL in charge; failure does not leak or double-free.
 p=image();map_fail=true;vitaBeginPixelTransfer(p);texture failed;
 gpu_alloc_texture(w,h,4,p,&failed,4,nullptr,nullptr,true);vitaEndPixelTransfer();
 assert(failed.status==TEX_UNINITIALIZED&&!mkxp_texture_pixels_size(p));
 map_fail=false;vita_big_free(p);clean();
 // Ordinary uploads never surrender caller-owned pixels, even with a kernel block.
 p=image();texture ordinary;pool_allowed=true;
 gpu_alloc_texture(w,h,4,p,&ordinary,4,nullptr,nullptr,true);
 assert(ordinary.data!=p&&ordinary.status==TEX_VALID&&memcmp(ordinary.data,p,bytes)==0);
 vgl_free(ordinary.data);vita_big_free(p);pool_allowed=false;clean();
 // An offered image still prefers copying into graphics pools when it fits.
 p=image();texture preferred;pool_allowed=true;vitaBeginPixelTransfer(p);
 gpu_alloc_texture(w,h,4,p,&preferred,4,nullptr,nullptr,true);vitaEndPixelTransfer();
 assert(preferred.data!=p&&preferred.status==TEX_VALID&&!mkxp_texture_pixels_size(p));
 vgl_free(preferred.data);vita_big_free(p);pool_allowed=false;clean();
 // Padded rows and format conversions retain the copying path.
 p=image();vitaBeginPixelTransfer(p);texture padded;
 gpu_alloc_texture(w-1,h,4,p,&padded,4,nullptr,nullptr,true);vitaEndPixelTransfer();
 assert(padded.status==TEX_UNINITIALIZED&&!mkxp_texture_pixels_size(p));vita_big_free(p);clean();
 p=image();vitaBeginPixelTransfer(p);texture converted;
 gpu_alloc_texture(w,h,4,p,&converted,3,nullptr,nullptr,false);vitaEndPixelTransfer();
 assert(converted.status==TEX_UNINITIALIZED&&!mkxp_texture_pixels_size(p));vita_big_free(p);clean();
 // A cropped image must not keep a larger original block; small images copy too.
 p=image(bytes*2);vitaBeginPixelTransfer(p);assert(!mkxp_adopt_texture_pixels(p,bytes));vitaEndPixelTransfer();vita_big_free(p);clean();
 p=image(128*1024);vitaBeginPixelTransfer(p);assert(!mkxp_adopt_texture_pixels(p,128*1024));vitaEndPixelTransfer();vita_big_free(p);clean();
 // A tracked buffer with insufficient address alignment must still copy.
 p=image();int slot=0;while(vita_blocks[slot].ptr!=p)++slot;
 void *unaligned=(char*)p+8;vita_blocks[slot].ptr=unaligned;vitaBeginPixelTransfer(unaligned);
 assert(!mkxp_adopt_texture_pixels(unaligned,bytes));vitaEndPixelTransfer();
 vita_blocks[slot].ptr=p;vita_big_free(p);clean();
 // Untracked caller-owned heap input is never imported.
 p=malloc(bytes);vitaBeginPixelTransfer(p);assert(!mkxp_adopt_texture_pixels(p,bytes));vitaEndPixelTransfer();free(p);clean();
 // A decoder-owned heap fallback is aligned, already mapped, and transferable.
 kernel_fail=true;p=image();vitaBeginPixelTransfer(p);texture heap;
 unsigned mapped_before=maps;
 gpu_alloc_texture(w,h,4,p,&heap,4,nullptr,nullptr,true);vitaEndPixelTransfer();
 assert(heap.status==TEX_VALID&&heap.data==p&&maps==mapped_before);
 vita_big_free(p);assert(vgl_malloc_usable_size(p)==bytes);vgl_free(p);clean();
 // The same ownership rule applies to the graphics-pool fallback.
 heap_fail=true;pool_allowed=true;p=image();pool_allowed=false;vitaBeginPixelTransfer(p);texture pool;
 gpu_alloc_texture(w,h,4,p,&pool,4,nullptr,nullptr,true);vitaEndPixelTransfer();
 assert(pool.status==TEX_VALID&&pool.data==p&&maps==mapped_before);
 vita_big_free(p);vgl_free(p);clean();kernel_fail=false;heap_fail=false;pool_allowed=false;
 // Removing a middle GPU entry must preserve the other registry lookups.
 void *active[3];for(auto &q:active){q=image();vitaBeginPixelTransfer(q);assert(mkxp_adopt_texture_pixels(q,bytes));vitaEndPixelTransfer();vita_big_free(q);}
 vgl_free(active[1]);assert(vgl_malloc_usable_size(active[0])==bytes&&vgl_malloc_usable_size(active[2])==bytes);
 vgl_free(active[0]);vgl_free(active[2]);clean();
 // Replacing a live texture queues its former storage, rather than freeing it early.
 p=image();texture replaced;vitaBeginPixelTransfer(p);
 gpu_alloc_texture(w,h,4,p,&replaced,4,nullptr,nullptr,true);vitaEndPixelTransfer();vita_big_free(p);
 void *replacement=image();vitaBeginPixelTransfer(replacement);
 gpu_alloc_texture(w,h,4,replacement,&replaced,4,nullptr,nullptr,true);vitaEndPixelTransfer();vita_big_free(replacement);
 assert(deferred.size()==1&&mkxp_texture_pixels_size(p)==bytes&&replaced.data==replacement);
 vgl_free(deferred.back());deferred.clear();vgl_free(replaced.data);clean();
 // Growing an imported allocation preserves ownership if allocation fails.
 p=image();vitaBeginPixelTransfer(p);assert(mkxp_adopt_texture_pixels(p,bytes));vitaEndPixelTransfer();
 assert(vgl_realloc(p,bytes-1)==p);assert(vgl_realloc(p,bytes+4096)==nullptr);
 assert(mkxp_texture_pixels_size(p)==bytes);pool_allowed=true;
 void *grown=vgl_realloc(p,bytes+4096);assert(grown&&memcmp(grown,"\xab\xab\xab\xab",4)==0);
 assert(!mkxp_texture_pixels_size(p));vgl_free(grown);pool_allowed=false;clean();
 // Repeated handoffs reclaim every mapped block and registry slot.
 for(int i=0;i<100;i++){p=image();vitaBeginPixelTransfer(p);texture repeat;
  gpu_alloc_texture(w,h,4,p,&repeat,4,nullptr,nullptr,true);vitaEndPixelTransfer();
  assert(repeat.status==TEX_VALID);vita_big_free(p);vgl_free(repeat.data);clean();}
 assert(maps==unmaps&&frees==next_uid);
 // Deferred GPU recovery is available only to the synchronous decoder.
 kernel_fail=true;
 heap_fail=true;
 pool_allowed=false;
 int outside=0;
 assert(!vita_big_alloc(bytes,1,&outside,0)&&forced_allocations==0);
 void *recovered=vita_big_alloc(bytes,1,&outside,1);
 assert(recovered&&outside&&forced_allocations==1);
 assert(static_cast<unsigned char *>(recovered)[bytes-1]==0);
 vita_big_free(recovered);
 clean();
}
'''
with tempfile.TemporaryDirectory(prefix='hardrpg-pixel-transfer-') as temporary:
    path = Path(temporary)
    cpp, exe = path / 'transfer.cpp', path / 'transfer'
    cpp.write_text(preamble+registry+alloc+release+upload+size+free+resize+checks)
    subprocess.run(['c++','-std=c++17','-g','-fsanitize=address,undefined',str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('PASS: actual SDL/VitaGL upload transfers pixels without a second image; fallback and deferred ownership verified')
