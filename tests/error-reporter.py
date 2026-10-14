#!/usr/bin/env python3
"""Exercise bounded native storage and protected Ruby exception capture."""
from pathlib import Path
import os, subprocess, tempfile
root = Path(__file__).resolve().parent.parent
source = (root/'binding/binding-mri.cpp').read_text()
start = source.index('static VALUE reportIvar(')
end = source.index('\n#endif', start)
helpers = source[start:end]
# Compile the real Vita showExc branch without requiring Vita headers.
start = source.index('static void showExc(VALUE exc, const BacktraceData &btData) {')
end = source.index('\n#endif', start)
show = source[start:end].replace('#ifdef __vita__','').replace('shState->graphics().getFrameCount()', '1234') + '\n}\n'
test = r'''
#include <ruby.h>
#undef NDEBUG
#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include "src/util/boost-hash.h"
#include "src/vita_error_report.h"
extern "C" void mkxp_gpu_allocation_recovery(void);
struct BacktraceData { BoostHash<std::string, std::string> scriptNames; };
'''+helpers+show+r'''
std::string read(const char *name) { std::ifstream f(name); return {std::istreambuf_iterator<char>(f), {}}; }
int main(int argc, char **argv) {
 ruby_sysinit(&argc,&argv); RUBY_INIT_STACK; ruby_init();
 char opt0[]="ruby", opt1[]="-e", opt2[]="";
 char *options[]={opt0,opt1,opt2}; ruby_options(3,options);
 // The default directory is a valid relative POSIX name in this host fixture.
 system("mkdir -p ux0:/data/hardrpg");
 vitaErrorSessionStart("game-fixture", 3);
 mkxp_gpu_allocation_recovery();
 mkxp_gpu_allocation_recovery();
 // Explicit samples must not add failure events or write a report.
 char snapshot[512];
 for(int i=0;i<100;++i) {
   size_t n = vitaMemorySnapshot(snapshot, sizeof(snapshot));
   assert(n > 0 && n < sizeof(snapshot));
   assert(strstr(snapshot, "newlib_arena=") != nullptr);
   assert(strstr(snapshot, "kernel_user_valid=0") != nullptr);
   assert(strstr(snapshot, "graphics_gc_cycles=2") != nullptr);
 }
 char small[8];
 assert(vitaMemorySnapshot(small, sizeof(small)) == sizeof(small)-1);
 assert(small[sizeof(small)-1] == 0);
 assert(vitaMemorySnapshot(small, 0) == 0);
 assert(read("ux0:/data/hardrpg/last-error.txt").empty());
 for(int i=0;i<30;++i) vitaErrorMemoryEvent("retry-succeeded", "asset");
 rb_eval_string("class FixtureScene; end; module SceneManager; @scene=FixtureScene.new; end; $game_map=Object.new; $game_map.instance_variable_set(:@map_id,35); module RPG; class Weapon; end; class Skill; end; end; subject=Object.new; subject.instance_variable_set(:@actor_id,2); item=Object.new; item.instance_variable_set(:@class,RPG::Weapon); item.instance_variable_set(:@item_id,255); subject.instance_variable_set(:@equips,[item]); action=Object.new; skill=Object.new; skill.instance_variable_set(:@class,RPG::Skill); skill.instance_variable_set(:@item_id,31); action.instance_variable_set(:@item,skill); subject.instance_variable_set(:@actions,[action]); SceneManager.instance_variable_get(:@scene).instance_variable_set(:@subject,subject); anim=Object.new; anim.instance_variable_set(:@id,169); anim.instance_variable_set(:@animation2_name,'Sword2'); sprite=Object.new; sprite.instance_variable_set(:@animation,anim); spriteset=Object.new; spriteset.instance_variable_set(:@actor_sprites,[sprite]); SceneManager.instance_variable_get(:@scene).instance_variable_set(:@spriteset,spriteset)");
 VALUE exc = rb_eval_string("begin; raise 'fixture failure'; rescue => e; e; end");
 BacktraceData bt;
 showExc(exc,bt);
 assert(rb_errinfo()==exc);
 std::string first=read("ux0:/data/hardrpg/last-error.txt");
 assert(first.find("last 16 of 31")!=std::string::npos);
 assert(first.find("recovery cycles (process lifetime): 2")!=std::string::npos);
 assert(first.find("fixture failure")!=std::string::npos);
 assert(first.find("scene.class=FixtureScene")!=std::string::npos);
 assert(first.find("@map_id=35")!=std::string::npos);
 assert(first.find("@class=RPG::Weapon")!=std::string::npos);
 assert(first.find("@item_id=255")!=std::string::npos);
 assert(first.find("@class=RPG::Skill")!=std::string::npos);
 assert(first.find("@item_id=31")!=std::string::npos);
 assert(first.find("@animation2_name=Sword2")!=std::string::npos);
 assert(first.find("heap_live_slots=")!=std::string::npos);
 // Context failure must preserve the original exception and persisted trace.
 rb_eval_string("module GC; def self.stat(*); raise 'context failure'; end; end");
 showExc(exc,bt);
 std::string second=read("ux0:/data/hardrpg/last-error.txt");
 assert(rb_errinfo()==exc);
 assert(second.find("fixture failure")!=std::string::npos);
 assert(second.find("Additional game context unavailable")!=std::string::npos);
 assert(read("ux0:/data/hardrpg/last-error.txt.prev")==first);
 vitaErrorReportBegin(); std::string large(65536,'x'); vitaErrorReportAppend(large.data(),large.size());
 assert(vitaErrorReportSave());
 assert(read("ux0:/data/hardrpg/last-error.txt").size()<32800);
 assert(read("ux0:/data/hardrpg/last-error.txt").find("[Report truncated]")!=std::string::npos);
 assert(!vitaErrorReportSave(std::string(1100,'x').c_str()));
 puts("PASS: bounded failure ring/report, atomic previous retention, Ruby state capture and original exception preserved after context failure");
 rb_set_errinfo(Qnil); return ruby_cleanup(0);
}
'''
ruby_pkg = os.environ.get('RUBY_PKG_CONFIG')
if not ruby_pkg:
    available = subprocess.check_output(['pkg-config', '--list-all'], text=True).splitlines()
    ruby_pkg = next(line.split()[0] for line in available if line.split()[0].startswith('ruby-'))
flags = subprocess.check_output(['pkg-config','--cflags','--libs',ruby_pkg],text=True).split()
with tempfile.TemporaryDirectory(prefix='hardrpg-error-reporter-') as name:
 p=Path(name); (p/'check.cpp').write_text(test)
 subprocess.run(['c++','-std=c++14','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-I'+str(root),str(p/'check.cpp'),str(root/'src/vita_error_report.cpp'),*flags,'-o',str(p/'check')],check=True)
 subprocess.run([str(p/'check')],cwd=p,check=True)
