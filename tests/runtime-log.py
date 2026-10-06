#!/usr/bin/env python3
"""Exercise the real console-write adapter with remount and I/O failures."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
source = (root / 'src/vita_runtime_log.c').read_text()
source = '\n'.join(line for line in source.splitlines() if not line.startswith('#include <psp2/') and line != '#include <reent.h>')
prefix = r'''
#include <assert.h>
#include <sys/types.h>
#include <string.h>
typedef ssize_t _ssize_t;
struct _reent { int _errno; };
typedef struct { long long st_size; } SceIoStat;
enum { SCE_O_WRONLY=1, SCE_O_CREAT=2, SCE_O_APPEND=4 };
static int opens, closes, writes, real_calls, next_handle=10, fail_open, fail_write, redirected;
static char received[128]; static size_t received_size;
int sceIoMkdir(const char *p, int m) { (void)p; (void)m; return 0; }
int sceIoGetstat(const char *p, SceIoStat *s) { (void)p; s->st_size=0; return 0; }
int sceIoRemove(const char *p) { (void)p; return 0; }
int sceIoRename(const char *a, const char *b) { (void)a; (void)b; return 0; }
int sceIoOpen(const char *p, int f, int m) {
 assert(strstr(p,"hardrpg/runtime.log") && f==7 && m==0777); ++opens;
 return fail_open ? -1 : next_handle++;
}
int sceIoWrite(int h, const void *p, size_t n) {
 assert(h==next_handle-1); ++writes;
 if(fail_write) return -1;
 if(n>2)n=2;
 memcpy(received+received_size,p,n); received_size+=n; return n;
}
int sceIoClose(int h) { assert(h==next_handle-1); ++closes; return 0; }
int isatty(int fd) { return (fd==1 || fd==2) && !redirected; }
_ssize_t __real__write_r(struct _reent *r, int fd, const void *p, size_t n) {
 (void)fd; (void)p; (void)n; ++real_calls; r->_errno=5; return -1;
}
'''
test = r'''
int main(void) {
 struct _reent r={123}; vitaRuntimeLogInit();
 assert(__wrap__write_r(&r,2,"before",6)==6 && r._errno==0);
 assert(opens==1 && closes==1 && writes==3);
 /* A new kernel handle is used after wake; the old one is not reused. */
 assert(__wrap__write_r(&r,1,"wake",4)==4 && opens==2 && closes==2);
 assert(received_size==10 && !memcmp(received,"beforewake",10));
 fail_open=1; assert(__wrap__write_r(&r,2,"x",1)==1 && r._errno==0 && closes==2);
 fail_open=0; fail_write=1;
 assert(__wrap__write_r(&r,2,"x",1)==1 && r._errno==0 && closes==3);
 assert(__wrap__write_r(&r,7,"save",4)==-1 && r._errno==5 && real_calls==1);
 redirected=1;
 assert(__wrap__write_r(&r,1,"save",4)==-1 && r._errno==5 && real_calls==2);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp) / 'test.c'
    path.write_text(prefix + source + test)
    subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', str(path), '-o', tmp + '/test'], check=True)
    subprocess.run([tmp + '/test'], check=True)
print('Console resume/write failure tests passed')
