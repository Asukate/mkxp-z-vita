#include "native_launcher/ui.h"
#include <cassert>
#include <physfs.h>
#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>
#include <algorithm>
static std::atomic<unsigned> mounts{0};
static std::atomic<bool> blockMount{false}, mountEntered{false};
extern "C" int __real_PHYSFS_mount(const char*,const char*,int);
extern "C" int __wrap_PHYSFS_mount(const char*p,const char*m,int a){
 ++mounts;
 if(blockMount.load()) {
   mountEntered.store(true);
   while(blockMount.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }
 return __real_PHYSFS_mount(p,m,a);
}
extern "C" int __real_PHYSFS_mountIo(PHYSFS_Io*,const char*,const char*,int);
extern "C" int __wrap_PHYSFS_mountIo(PHYSFS_Io*io,const char*p,const char*m,int a){
 ++mounts;
 if(blockMount.load()) {
   mountEntered.store(true);
   while(blockMount.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }
 return __real_PHYSFS_mountIo(io,p,m,a);
}
int main(int argc,char**argv){
 assert(argc==2);hardrpg::Paths paths;paths.root=argv[1];
 paths.storage={std::string(argv[1])+"/"};
 // The game's settings (Triangle, then Game settings, on the home screen).
 hardrpg::UI ui(paths); ui.model.unpackZips=true; // the unpack path itself is under test
 ui.welcome.active=false; // a fresh folder is a first start; this test drives the menus
 auto before=mounts.load();ui.openGameMenu(0);
 auto first=mounts.load()-before;assert(ui.gameOptions);std::cout<<"settings mounts="<<first<<std::endl;
 assert(first==1);
 ui.action(hardrpg::Button::Circle);before=mounts.load();
 ui.openGameMenu(0);assert(mounts==before);
 ui.editName=[](const std::string&,std::string&out){out="New title";return true;};
 ui.setting=5;before=mounts.load();ui.action(hardrpg::Button::Cross);assert(mounts==before);
 unsigned enumerated=0;bool cancelled=false;
 try {ui.model.prepareZip(ui.gameEntry.node,[&](uint64_t,uint64_t){
   if(++enumerated==3) throw hardrpg::Cancelled("cancel indexing");
 });}catch(const hardrpg::Cancelled&){cancelled=true;}
 assert(cancelled);
 before=mounts.load();unsigned pumps=0;
 auto cache=ui.model.prepareZip(ui.gameEntry.node,[&](uint64_t,uint64_t){++pumps;});
 std::cout<<"prepare mounts="<<mounts-before<<" pumps="<<pumps<<std::endl;
 assert(mounts==before+1);assert(pumps>0);
 before=mounts.load();assert(ui.model.prepareZip(ui.gameEntry.node)==cache);assert(mounts==before);
 // A slow RTP read must not hold the UI thread or prevent leaving Settings.
 ui.model.setRtp("Standard",std::string(argv[1])+"/rtp/RPGXP.zip");
 ui.model.clearRtpCache();
 ui.setting=4;ui.action(hardrpg::Button::Cross);assert(ui.page==hardrpg::Page::Rtp);
 blockMount.store(true);
 ui.service();
 for(int n=0;n<1000&&!mountEntered.load();++n)
   std::this_thread::sleep_for(std::chrono::milliseconds(1));
 assert(mountEntered.load());
 auto start=std::chrono::steady_clock::now();
 ui.service();ui.action(hardrpg::Button::Down);ui.action(hardrpg::Button::Circle);
 assert(ui.page==hardrpg::Page::Game);
 assert(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(100));
 ui.setting=6;ui.action(hardrpg::Button::Cross);assert(ui.page==hardrpg::Page::GameInfo);
 ui.action(hardrpg::Button::Circle);ui.setting=4;ui.action(hardrpg::Button::Cross);
 auto generation=ui.rtpGeneration;
 blockMount.store(false);
 for(int n=0;n<1000&&!ui.service();++n)
   std::this_thread::sleep_for(std::chrono::milliseconds(1));
 assert(ui.rtpGeneration==generation);
 assert(ui.rtpStatuses.front()=="Checking..."); // Old worker result was discarded.
 ui.action(hardrpg::Button::Circle);
 std::cout<<"RTP worker: input responsive, stale result discarded"<<std::endl;
 std::vector<std::string> stages;
 ui.model.launch(ui.gameEntry,{},[&](const std::string&s){stages.push_back(s);});
 for(const auto&label:{"Preparing game ZIP","Preparing 2000 RTP ZIP","Preparing 2003 RTP ZIP"})
   assert(std::count(stages.begin(),stages.end(),label)==1);
 before=mounts.load();ui.model.launch(ui.gameEntry);assert(mounts==before);
 std::cout<<"Game/2000/2003 stages distinct; warm launch mounts=0"<<std::endl;
}
