#include "crypto/rgssad.h"
#include <cassert>
#include <string>
#include <iostream>
static std::string read(const char *path) {
 auto *file=PHYSFS_openRead(path);assert(file);
 char bytes[32]{};auto n=PHYSFS_readBytes(file,bytes,sizeof(bytes));assert(n>=0);
 assert(PHYSFS_close(file));return std::string(bytes,n);
}
int main(int argc,char **argv) {
 assert(argc==2);assert(PHYSFS_init(argv[0]));
 assert(PHYSFS_registerArchiver(&RGSS1_Archiver));
 assert(PHYSFS_registerArchiver(&RGSS2_Archiver));
 assert(PHYSFS_registerArchiver(&RGSS3_Archiver));
 assert(PHYSFS_mount(argv[1],nullptr,1));
 assert(read("graphics/characters/hero.png")=="hero");
 assert(read("Graphics/Characters/Hero.png")=="hero");
 assert(read("Case/Foo.txt")=="upper");
 assert(read("Case/foo.txt")=="lower");
 assert(read("CASE/FOO.TXT")=="upper");
 PHYSFS_Stat st{};
 assert(PHYSFS_stat("graphics/characters/hero.png",&st)&&st.filesize==4);
 assert(PHYSFS_stat("graphics/characters",&st)&&st.filetype==PHYSFS_FILETYPE_DIRECTORY);
 assert(PHYSFS_stat("Exact",&st)&&st.filetype==PHYSFS_FILETYPE_DIRECTORY);
 auto **names=PHYSFS_enumerateFiles("graphics/characters");assert(names&&names[0]);
 assert(std::string(names[0])=="Hero.png");PHYSFS_freeList(names);
 assert(read("graphics/\xe6\x97\xa5\xe6\x9c\xac.png")=="unicode");
 assert(!PHYSFS_exists("graphics/\xc3\xa9.png")); // Non-ASCII case is unchanged.
 assert(!PHYSFS_exists("graphics/missing.png"));
 assert(PHYSFS_deinit());std::cout<<"exact/folded read, stat, enumerate, collisions, Unicode: passed"<<std::endl;
}
