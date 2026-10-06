/*
 ** binding-mri.cpp
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

#include "audio/audio.h"
#include "filesystem/filesystem.h"
#include "display/graphics.h"
#include "display/font.h"
#include "system/system.h"

#include "util/util.h"
#include "util/sdl-util.h"
#include "util/debugwriter.h"
#include "util/boost-hash.h"
#include "util/exception.h"
#include "util/encoding.h"

#include "config.h"

#include "binding-util.h"
#include "binding.h"

#include "sharedstate.h"
#include "eventthread.h"
#include "vita_diagnostic.h"
#ifdef __vita__
#include "vita_startup_timer.h"
#include "vita_error_report.h"
#endif

#include <vector>
#include "util/rapidcsv.h"

extern "C" {
#include <ruby.h>

#if RAPI_FULL >= 190
#include <ruby/encoding.h>
#endif

#ifdef MKXPZ_VITA_DIAGNOSTICS
#include <ruby/debug.h>
#endif
}

#ifdef __WIN32__
#include "binding-mri-win32.h"
#endif

#include <assert.h>
#include <cstring>
#include <string>
#include <zlib.h>

#include <SDL_cpuinfo.h>
#include <SDL_filesystem.h>
#include <SDL_loadso.h>
#include <SDL_power.h>

extern const char module_rpg1[];
extern const char module_rpg2[];
extern const char module_rpg3[];

#ifdef MKXPZ_PROTO_LAUNCHER
void launcherFilesystemBindingInit();
#endif

static VALUE topSelf;

#ifdef MKXPZ_VITA_DIAGNOSTICS
static VALUE vitaDepthTracePoint = Qnil;
static unsigned int vitaRubyCallDepth = 0;
static unsigned int vitaRubyCallHighwater = 0;

static void vitaRubyDepthTrace(VALUE tracePoint, void *)
{
    rb_trace_arg_t *arg = rb_tracearg_from_tracepoint(tracePoint);
    const rb_event_flag_t event = rb_tracearg_event_flag(arg);

    if (event & RUBY_EVENT_CALL) {
        ++vitaRubyCallDepth;
        if (vitaRubyCallDepth > vitaRubyCallHighwater) {
            vitaRubyCallHighwater = vitaRubyCallDepth;
            if (vitaRubyCallDepth <= 32 || (vitaRubyCallDepth % 32u) == 0u) {
                VALUE pathValue = rb_tracearg_path(arg);
                VALUE methodValue = rb_tracearg_method_id(arg);
                VALUE lineValue = rb_tracearg_lineno(arg);
                const char *path = RB_TYPE_P(pathValue, RUBY_T_STRING)
                    ? RSTRING_PTR(pathValue) : "-";
                const ID method = NIL_P(methodValue) ? 0 : SYM2ID(methodValue);
                const char *methodName = method ? rb_id2name(method) : "-";
                const long line = NIL_P(lineValue) ? 0 : NUM2LONG(lineValue);
                vitaDiagLog("RUBYDEPTH",
                            "highwater=%u path=%s line=%ld method=%s",
                            vitaRubyCallDepth, path, line,
                            methodName ? methodName : "-");
            }
        }
        return;
    }

    if (event & RUBY_EVENT_RETURN) {
        if (vitaRubyCallDepth > 0)
            --vitaRubyCallDepth;
        return;
    }

    if (event & RUBY_EVENT_RAISE) {
        VALUE exc = rb_tracearg_raised_exception(arg);
        if (!NIL_P(exc) && rb_obj_is_kind_of(exc, rb_eSysStackError)) {
            VALUE pathValue = rb_tracearg_path(arg);
            VALUE methodValue = rb_tracearg_method_id(arg);
            VALUE lineValue = rb_tracearg_lineno(arg);
            const char *path = RB_TYPE_P(pathValue, RUBY_T_STRING)
                ? RSTRING_PTR(pathValue) : "-";
            const ID method = NIL_P(methodValue) ? 0 : SYM2ID(methodValue);
            const char *methodName = method ? rb_id2name(method) : "-";
            const long line = NIL_P(lineValue) ? 0 : NUM2LONG(lineValue);
            rb_tracepoint_disable(tracePoint);
            vitaDiagLog("RUBYDEPTH",
                        "SYSTEM_STACK_ERROR depth=%u highwater=%u path=%s line=%ld method=%s",
                        vitaRubyCallDepth, vitaRubyCallHighwater, path, line,
                        methodName ? methodName : "-");
        }
    }
}

static void vitaEnableRubyDepthTrace()
{
    vitaRubyCallDepth = 0;
    vitaRubyCallHighwater = 0;
    if (NIL_P(vitaDepthTracePoint)) {
        vitaDepthTracePoint = rb_tracepoint_new(
            Qnil, RUBY_EVENT_CALL | RUBY_EVENT_RETURN | RUBY_EVENT_RAISE,
            vitaRubyDepthTrace, NULL);
        rb_gc_register_address(&vitaDepthTracePoint);
    }
    rb_tracepoint_enable(vitaDepthTracePoint);
    vitaDiagLog("RUBYDEPTH", "trace_enabled");
}
#endif

static void mriBindingExecute();
static void mriBindingTerminate();
static void mriBindingReset();

ScriptBinding scriptBindingImpl = {mriBindingExecute, mriBindingTerminate,
    mriBindingReset};

ScriptBinding *scriptBinding = &scriptBindingImpl;

void tableBindingInit();
void etcBindingInit();
void fontBindingInit();
void bitmapBindingInit();
void spriteBindingInit();
void viewportBindingInit();
void planeBindingInit();
void windowBindingInit();
void tilemapBindingInit();
void windowVXBindingInit();
void tilemapVXBindingInit();

void inputBindingInit();
void audioBindingInit();
void graphicsBindingInit();

void fileIntBindingInit();

#ifdef MKXPZ_MINIFFI
void MiniFFIBindingInit();
#endif

#ifdef MKXPZ_STEAM
void CUSLBindingInit();
#endif

void httpBindingInit();

RB_METHOD(mkxpDelta);
RB_METHOD(mriPrint);
RB_METHOD(mriP);
RB_METHOD(mkxpDataDirectory);
RB_METHOD(mkxpSetTitle);
RB_METHOD(mkxpGetTitle);
RB_METHOD(mkxpDesensitize);
RB_METHOD(mkxpPuts);

RB_METHOD(mkxpPlatform);
RB_METHOD(mkxpIsMacHost);
RB_METHOD(mkxpIsWindowsHost);
RB_METHOD(mkxpIsLinuxHost);
RB_METHOD(mkxpIsUsingRosetta);
RB_METHOD(mkxpIsUsingWine);
RB_METHOD(mkxpIsReallyMacHost);
RB_METHOD(mkxpIsReallyLinuxHost);
RB_METHOD(mkxpIsReallyWindowsHost);

RB_METHOD(mkxpUserLanguage);
RB_METHOD(mkxpUserName);
RB_METHOD(mkxpGameTitle);
RB_METHOD(mkxpPowerState);
RB_METHOD(mkxpSettingsMenu);
RB_METHOD(mkxpCpuCount);
RB_METHOD(mkxpSystemMemory);
RB_METHOD(mkxpReloadPathCache);
RB_METHOD(mkxpAddPath);
RB_METHOD(mkxpRemovePath);
RB_METHOD(mkxpFileExists);
RB_METHOD(mkxpLaunch);

RB_METHOD(mkxpGetJSONSetting);
RB_METHOD(mkxpSetJSONSetting);
RB_METHOD(mkxpGetAllJSONSettings);

RB_METHOD(mkxpSetDefaultFontFamily);

RB_METHOD(mriRgssMain);
RB_METHOD(mriRgssStop);
RB_METHOD(_kernelCaller);

RB_METHOD(mkxpStringToUTF8);
RB_METHOD(mkxpStringToUTF8Bang);

VALUE json2rb(json5pp::value const &v);
json5pp::value rb2json(VALUE v);

RB_METHOD(mkxpParseCSV);

static void mriBindingInit() {
    tableBindingInit();
    etcBindingInit();
    fontBindingInit();
    bitmapBindingInit();
    spriteBindingInit();
    viewportBindingInit();
    planeBindingInit();
    
    if (rgssVer == 1) {
        windowBindingInit();
        tilemapBindingInit();
    } else {
        windowVXBindingInit();
        tilemapVXBindingInit();
    }
    
    inputBindingInit();
    audioBindingInit();
    graphicsBindingInit();
    
    fileIntBindingInit();
    
#ifdef MKXPZ_MINIFFI
    MiniFFIBindingInit();
#endif
    
#ifdef MKXPZ_STEAM
    CUSLBindingInit();
#endif
    
    httpBindingInit();
    
    if (rgssVer >= 3) {
        _rb_define_module_function(rb_mKernel, "rgss_main", mriRgssMain);
        _rb_define_module_function(rb_mKernel, "rgss_stop", mriRgssStop);
        
        _rb_define_module_function(rb_mKernel, "msgbox", mriPrint);
        _rb_define_module_function(rb_mKernel, "msgbox_p", mriP);
        
        rb_define_global_const("RGSS_VERSION", rb_utf8_str_new_cstr("3.0.1"));
    } else {
        _rb_define_module_function(rb_mKernel, "print", mriPrint);
        _rb_define_module_function(rb_mKernel, "p", mriP);
        
        rb_define_alias(rb_singleton_class(rb_mKernel), "_mkxp_kernel_caller_alias",
                        "caller");
        _rb_define_module_function(rb_mKernel, "caller", _kernelCaller);
    }
    
    if (rgssVer == 1)
        rb_eval_string(module_rpg1);
    else if (rgssVer == 2)
        rb_eval_string(module_rpg2);
    else if (rgssVer == 3)
        rb_eval_string(module_rpg3);
    else
        assert(!"unreachable");

#ifdef __vita__
    /* Vita has no console: fd 1/2 are unusable, so any game-side
     * p/puts/print (stock Ruby on RGSS3; XP/VX get native overrides
     * above) dies with Errno::EBADF and exits the game. BS1 alone
     * ships 8 such debug prints, one firing every regen tick. Replace
     * both streams with a silent null writer before any game code runs.
     * Kernel#p/puts dispatch `write` as a method (same duck-typing as
     * StringIO redirection), so plain game output just vanishes. */
    rb_eval_string(
        "class VitaNullIO\n"
        "  def write(*a); a.reduce(0) { |n, s| n + s.to_s.bytesize }; end\n"
        "  def puts(*a); nil; end\n"
        "  def print(*a); nil; end\n"
        "  def printf(*a); nil; end\n"
        "  def flush; self; end\n"
        "  def fsync; 0; end\n"
        "  def sync; true; end\n"
        "  def sync=(v); v; end\n"
        "  def tty?; false; end\n"
        "  def isatty; false; end\n"
        "  def close; nil; end\n"
        "  def closed?; false; end\n"
        "end\n"
        "$stdout = VitaNullIO.new\n"
        "$stderr = VitaNullIO.new\n");

    /* No Steam API can exist on Vita, but shipped games reference it:
     * BS1's Map068 boss-victory events call SteamUserStatsLite.instance /
     * set_achievement / update, and the Win32-bound script that defines
     * the class is skipped below, so the reference died with NameError
     * and the Vita fiber backend unwound it past all Ruby frames --
     * the game just closed after "VICTORY ACHIEVED". Define the measured
     * surface as a no-op up front (return values mirror the real script
     * in its never-initted state) so every current and future game
     * degrades instead of exiting. tests/unit/test_steam_stub_contract.rb
     * asserts this contract against the shipped bytes. */
    /* STEAM_STUB_BEGIN */
    rb_eval_string(
        "class SteamUserStatsLite\n"
        "  def self.instance\n"
        "    @vita_stub_instance ||= new\n"
        "  end\n"
        "  def initted?\n"
        "    false\n"
        "  end\n"
        "  def set_achievement(_name)\n"
        "    false\n"
        "  end\n"
        "  def update\n"
        "    nil\n"
        "  end\n"
        "end\n");
    /* STEAM_STUB_END */
#endif
    
    VALUE mod = rb_define_module("System");
#ifdef MKXPZ_PROTO_LAUNCHER
    launcherFilesystemBindingInit();
#endif
    _rb_define_module_function(mod, "delta", mkxpDelta);
    _rb_define_module_function(mod, "uptime", mkxpDelta);
    _rb_define_module_function(mod, "data_directory", mkxpDataDirectory);
    _rb_define_module_function(mod, "set_window_title", mkxpSetTitle);
    _rb_define_module_function(mod, "window_title", mkxpGetTitle);
    _rb_define_module_function(mod, "window_title=", mkxpSetTitle);
    _rb_define_module_function(mod, "show_settings", mkxpSettingsMenu);
    _rb_define_module_function(mod, "puts", mkxpPuts);
    _rb_define_module_function(mod, "desensitize", mkxpDesensitize);
    _rb_define_module_function(mod, "platform", mkxpPlatform);
    
    _rb_define_module_function(mod, "is_mac?", mkxpIsMacHost);
    _rb_define_module_function(mod, "is_rosetta?", mkxpIsUsingRosetta);
    
    _rb_define_module_function(mod, "is_linux?", mkxpIsLinuxHost);
    
    _rb_define_module_function(mod, "is_windows?", mkxpIsWindowsHost);
    _rb_define_module_function(mod, "is_wine?", mkxpIsUsingWine);
    _rb_define_module_function(mod, "is_really_mac?", mkxpIsReallyMacHost);
    _rb_define_module_function(mod, "is_really_linux?", mkxpIsReallyLinuxHost);
    _rb_define_module_function(mod, "is_really_windows?", mkxpIsReallyWindowsHost);
    
    
    _rb_define_module_function(mod, "user_language", mkxpUserLanguage);
    _rb_define_module_function(mod, "user_name", mkxpUserName);
    _rb_define_module_function(mod, "game_title", mkxpGameTitle);
    _rb_define_module_function(mod, "power_state", mkxpPowerState);
    _rb_define_module_function(mod, "nproc", mkxpCpuCount);
    _rb_define_module_function(mod, "memory", mkxpSystemMemory);
    _rb_define_module_function(mod, "reload_cache", mkxpReloadPathCache);
    _rb_define_module_function(mod, "mount", mkxpAddPath);
    _rb_define_module_function(mod, "unmount", mkxpRemovePath);
    _rb_define_module_function(mod, "file_exist?", mkxpFileExists);
    _rb_define_module_function(mod, "launch", mkxpLaunch);
    
    _rb_define_module_function(mod, "default_font_family=", mkxpSetDefaultFontFamily);
    
    _rb_define_module_function(mod, "parse_csv", mkxpParseCSV);
    
    _rb_define_method(rb_cString, "to_utf8", mkxpStringToUTF8);
    _rb_define_method(rb_cString, "to_utf8!", mkxpStringToUTF8Bang);
    
    VALUE cmod = rb_define_module("CFG");
    _rb_define_module_function(cmod, "[]", mkxpGetJSONSetting);
    _rb_define_module_function(cmod, "[]=", mkxpSetJSONSetting);
    _rb_define_module_function(cmod, "to_hash", mkxpGetAllJSONSettings);
    
    /* Load global constants */
    rb_gv_set("MKXP", Qtrue);
    
    VALUE debug = rb_bool_new(shState->config().editor.debug);
    if (rgssVer == 1)
        rb_gv_set("DEBUG", debug);
    else if (rgssVer >= 2)
        rb_gv_set("TEST", debug);
    
    rb_gv_set("BTEST", rb_bool_new(shState->config().editor.battleTest));
    
#ifdef MKXPZ_BUILD_XCODE
    std::string version = std::string(MKXPZ_VERSION "/") + getPlistValue("GIT_COMMIT_HASH");
    VALUE vers = rb_utf8_str_new_cstr(version.c_str());
#else
    VALUE vers = rb_utf8_str_new_cstr(MKXPZ_VERSION "/" MKXPZ_GIT_HASH);
#endif
    rb_str_freeze(vers);
    rb_define_const(mod, "VERSION", vers);
    
    // Automatically load zlib if it's present -- the correct way this time
    int state;
    rb_eval_string_protect("require('zlib') if !Kernel.const_defined?(:Zlib)", &state);
    if (state) {
        Debug() << "Could not load Zlib. If this is important, make sure Ruby was built with static extensions, or that"
        << ((MKXPZ_PLATFORM == MKXPZ_PLATFORM_MACOS) ? "zlib.bundle" : "zlib.so")
        << "is present and reachable by Ruby's loadpath.";
    }
    
    // Set $stdout and its ilk accordingly on Windows
    // I regret teaching you that word
#ifdef __WIN32__
    if (shState->config().winConsole)
        configureWindowsStreams();
#endif
}

static void showMsg(const std::string &msg) {
    shState->eThread().showMessageBox(msg.c_str());
}

static void showEngineError(const std::string &msg) {
#ifdef __vita__
    vitaWriteErrorReport("Engine error\n\n" + msg);
#else
    showMsg(msg);
#endif
}

static void printP(int argc, VALUE *argv, const char *convMethod,
                   const char *sep) {
    VALUE dispString = rb_str_buf_new(128);
    ID conv = rb_intern(convMethod);
    
    for (int i = 0; i < argc; ++i) {
        VALUE str = rb_funcall2(argv[i], conv, 0, NULL);
        rb_str_buf_append(dispString, str);
        
        if (i < argc)
            rb_str_buf_cat2(dispString, sep);
    }
    
    showMsg(RSTRING_PTR(dispString));
}


RB_METHOD_GUARD(mriPrint) {
    RB_UNUSED_PARAM;
    
    printP(argc, argv, "to_s", "");
    
    shState->checkShutdown();
    shState->checkReset();
    
    return Qnil;
}
RB_METHOD_GUARD_END

RB_METHOD_GUARD(mriP) {
    RB_UNUSED_PARAM;
    
    printP(argc, argv, "inspect", "\n");
    
    shState->checkShutdown();
    shState->checkReset();
    
    return Qnil;
}
RB_METHOD_GUARD_END

RB_METHOD(mkxpDelta) {
    RB_UNUSED_PARAM;
    return rb_float_new(shState->runTime());
}

RB_METHOD(mkxpDataDirectory) {
    RB_UNUSED_PARAM;
    
    const std::string &path = shState->config().customDataPath;
    const char *s = path.empty() ? "." : path.c_str();
    
    std::string s_nml = shState->fileSystem().normalize(s, 1, 1);
    VALUE ret = rb_utf8_str_new_cstr(s_nml.c_str());
    
    return ret;
}

RB_METHOD(mkxpSetTitle) {
    RB_UNUSED_PARAM;
    
    VALUE s;
    rb_scan_args(argc, argv, "1", &s);
    SafeStringValue(s);
    
    shState->eThread().requestWindowRename(RSTRING_PTR(s));
    return s;
}

RB_METHOD(mkxpGetTitle) {
    RB_UNUSED_PARAM;
    
    rb_check_argc(argc, 0);
    
    return rb_utf8_str_new_cstr(SDL_GetWindowTitle(shState->sdlWindow()));
}

RB_METHOD(mkxpDesensitize) {
    RB_UNUSED_PARAM;
    
    VALUE filename;
    rb_scan_args(argc, argv, "1", &filename);
    SafeStringValue(filename);
    
    return rb_utf8_str_new_cstr(
                                shState->fileSystem().desensitize(RSTRING_PTR(filename)));
}

RB_METHOD(mkxpPuts) {
    RB_UNUSED_PARAM;
    
    const char *str;
    rb_get_args(argc, argv, "z", &str RB_ARG_END);
    
    Debug() << str;
    
    return Qnil;
}

RB_METHOD(mkxpPlatform) {
    RB_UNUSED_PARAM;
    
#if MKXPZ_PLATFORM == MKXPZ_PLATFORM_MACOS
    std::string platform("macOS");
    
    if (mkxp_sys::isRosetta())
        platform += " (Rosetta)";
    
#elif MKXPZ_PLATFORM == MKXPZ_PLATFORM_WINDOWS
    std::string platform("Windows");
    
    if (mkxp_sys::isWine()) {
        platform += " (Wine - ";
        switch (mkxp_sys::getRealHostType()) {
            case mkxp_sys::WineHostType::Mac:
                platform += "macOS)";
                break;
            default:
                platform += "Linux)";
                break;
        }
    }
#else
    std::string platform("Linux");
#endif
    
    return rb_utf8_str_new_cstr(platform.c_str());
}

RB_METHOD(mkxpIsMacHost) {
    RB_UNUSED_PARAM;
    
    return rb_bool_new(MKXPZ_PLATFORM == MKXPZ_PLATFORM_MACOS);
}

RB_METHOD(mkxpIsUsingRosetta) {
    RB_UNUSED_PARAM;
    
    return rb_bool_new(mkxp_sys::isRosetta());
}

RB_METHOD(mkxpIsLinuxHost) {
    RB_UNUSED_PARAM;
    
    return rb_bool_new(MKXPZ_PLATFORM == MKXPZ_PLATFORM_LINUX);
}

RB_METHOD(mkxpIsWindowsHost) {
    RB_UNUSED_PARAM;
    
    return rb_bool_new(MKXPZ_PLATFORM == MKXPZ_PLATFORM_WINDOWS);
}

RB_METHOD(mkxpIsUsingWine) {
    RB_UNUSED_PARAM;
    return rb_bool_new(mkxp_sys::isWine());
}

RB_METHOD(mkxpIsReallyMacHost) {
    RB_UNUSED_PARAM;
    return rb_bool_new(mkxp_sys::getRealHostType() == mkxp_sys::WineHostType::Mac);
}

RB_METHOD(mkxpIsReallyLinuxHost) {
    RB_UNUSED_PARAM;
    return rb_bool_new(mkxp_sys::getRealHostType() == mkxp_sys::WineHostType::Linux);
}

RB_METHOD(mkxpIsReallyWindowsHost) {
    RB_UNUSED_PARAM;
    return rb_bool_new(mkxp_sys::getRealHostType() == mkxp_sys::WineHostType::Windows);
}

RB_METHOD(mkxpUserLanguage) {
    RB_UNUSED_PARAM;
    
    return rb_utf8_str_new_cstr(mkxp_sys::getSystemLanguage().c_str());
}

RB_METHOD(mkxpUserName) {
    RB_UNUSED_PARAM;
    
    // Using the Windows API isn't working with usernames that involve Unicode
    // characters for some dumb reason
#ifdef __WIN32__
    VALUE env = rb_const_get(rb_mKernel, rb_intern("ENV"));
    return rb_funcall(env, rb_intern("[]"), 1, rb_str_new_cstr("USERNAME"));
#else
    return rb_utf8_str_new_cstr(mkxp_sys::getUserName().c_str());
#endif
}

RB_METHOD(mkxpGameTitle) {
    RB_UNUSED_PARAM;
    
    return rb_utf8_str_new_cstr(shState->config().game.title.c_str());
}

RB_METHOD(mkxpPowerState) {
    RB_UNUSED_PARAM;
    
    int secs, pct;
    SDL_PowerState ps = SDL_GetPowerInfo(&secs, &pct);
    
    VALUE hash = rb_hash_new();
    
    rb_hash_aset(hash, ID2SYM(rb_intern("seconds")),
                 (secs > -1) ? INT2NUM(secs) : RUBY_Qnil);
    
    rb_hash_aset(hash, ID2SYM(rb_intern("percent")),
                 (pct > -1) ? INT2NUM(pct) : RUBY_Qnil);
    
    rb_hash_aset(hash, ID2SYM(rb_intern("discharging")),
                 rb_bool_new(ps == SDL_POWERSTATE_ON_BATTERY));
    
    return hash;
}

RB_METHOD(mkxpSettingsMenu) {
    RB_UNUSED_PARAM;
    
    shState->eThread().requestSettingsMenu();
    
    return Qnil;
}

RB_METHOD(mkxpCpuCount) {
    RB_UNUSED_PARAM;
    
    return INT2NUM(SDL_GetCPUCount());
}

RB_METHOD(mkxpSystemMemory) {
    RB_UNUSED_PARAM;
    
    return INT2NUM(SDL_GetSystemRAM());
}

RB_METHOD_GUARD(mkxpReloadPathCache) {
    RB_UNUSED_PARAM;
    
    shState->fileSystem().reloadPathCache();
    return Qnil;
}
RB_METHOD_GUARD_END

RB_METHOD_GUARD(mkxpAddPath) {
    RB_UNUSED_PARAM;
    
    VALUE path, mountpoint, reload;
    rb_scan_args(argc, argv, "12", &path, &mountpoint, &reload);
    SafeStringValue(path);
    if (mountpoint != Qnil) SafeStringValue(mountpoint);
    
    const char *mp = (mountpoint == Qnil) ? 0 : RSTRING_PTR(mountpoint);
    
    bool rl = true;
    if (reload != Qnil)
        rb_bool_arg(reload, &rl);
    
    shState->fileSystem().addPath(RSTRING_PTR(path), mp, rl);
    
    return path;
}
RB_METHOD_GUARD_END

RB_METHOD_GUARD(mkxpRemovePath) {
    RB_UNUSED_PARAM;
    
    VALUE path, reload;
    rb_scan_args(argc, argv, "11", &path, &reload);
    SafeStringValue(path);
    
    bool rl = true;
    if (reload != Qnil)
        rb_bool_arg(reload, &rl);
    
    shState->fileSystem().removePath(RSTRING_PTR(path), rl);
    
    return path;
}
RB_METHOD_GUARD_END

RB_METHOD(mkxpFileExists) {
    RB_UNUSED_PARAM;
    
    VALUE path;
    rb_scan_args(argc, argv, "1", &path);
    SafeStringValue(path);
    
    if (shState->fileSystem().exists(RSTRING_PTR(path)))
        return Qtrue;
    return Qfalse;
}

RB_METHOD(mkxpSetDefaultFontFamily) {
    RB_UNUSED_PARAM;
    
    VALUE familyV;
    rb_scan_args(argc, argv, "1", &familyV);
    SafeStringValue(familyV);
    
    std::string family(RSTRING_PTR(familyV));
    shState->fontState().setDefaultFontFamily(family);
    
    return Qnil;
}

RB_METHOD_GUARD(mkxpStringToUTF8) {
    RB_UNUSED_PARAM;
    
    rb_check_argc(argc, 0);
    
    std::string ret(RSTRING_PTR(self), RSTRING_LEN(self));
    ret = Encoding::convertString(ret);
    
    return rb_utf8_str_new(ret.c_str(), ret.length());
}
RB_METHOD_GUARD_END

RB_METHOD_GUARD(mkxpStringToUTF8Bang) {
    RB_UNUSED_PARAM;
    
    rb_check_argc(argc, 0);
    
    std::string ret(RSTRING_PTR(self), RSTRING_LEN(self));
    ret = Encoding::convertString(ret);
    
    rb_str_resize(self, ret.length());
    memcpy(RSTRING_PTR(self), ret.c_str(), RSTRING_LEN(self));
    
#if RAPI_FULL >= 190
    rb_funcall(self, rb_intern("force_encoding"), 1, rb_enc_from_encoding(rb_utf8_encoding()));
#endif
    
    return self;
}
RB_METHOD_GUARD_END

#ifdef __APPLE__
#define OPENCMD "open "
#define OPENARGS "--args"
#elif defined(__linux__)
#define OPENCMD "xdg-open "
#define OPENARGS ""
#else
#define OPENCMD "start /b \"launch\" "
#define OPENARGS ""
#endif

RB_METHOD_GUARD(mkxpLaunch) {
    RB_UNUSED_PARAM;
    
    VALUE cmdname, args;
    
    rb_scan_args(argc, argv, "11", &cmdname, &args);
    SafeStringValue(cmdname);
    
    std::string command(OPENCMD);
    command += "\""; command += RSTRING_PTR(cmdname); command += "\"";
    
    if (args != RUBY_Qnil) {
#ifndef __linux__
        command += " ";
        command += OPENARGS;
        Check_Type(args, T_ARRAY);
        
        for (int i = 0; i < RARRAY_LEN(args); i++) {
            VALUE arg = rb_ary_entry(args, i);
            SafeStringValue(arg);
            
            if (RSTRING_LEN(arg) <= 0)
                continue;
            
            command += " ";
            command += RSTRING_PTR(arg);
        }
#else
        Debug() << command << ":" << "Arguments are not supported with xdg-open. Ignoring.";
#endif
    }
    
    if (std::system(command.c_str()) != 0) {
        throw Exception(Exception::MKXPError, "Failed to launch \"%s\"", RSTRING_PTR(cmdname));
    }
    
    return RUBY_Qnil;
}
RB_METHOD_GUARD_END

RB_METHOD_GUARD(mkxpParseCSV) {
    RB_UNUSED_PARAM;
    
    VALUE str;
    rb_scan_args(argc, argv, "1", &str);
    SafeStringValue(str);
    
    VALUE ret = rb_ary_new();
    std::stringstream stream(RSTRING_PTR(str));
    try {
        rapidcsv::Document doc(stream, rapidcsv::LabelParams(-1,-1), rapidcsv::SeparatorParams(',', false, true, true, true));
        for (int r = 0; r < doc.GetRowCount(); r++) {
            VALUE col = rb_ary_new();
            for (int c = 0; c < doc.GetColumnCount(); c++) {
                std::string str = doc.GetCell<std::string>(c, r);
                rb_ary_push(col, rb_utf8_str_new(str.c_str(), str.length()));
            }
            rb_ary_push(ret, col);
        }
    } catch (std::exception &e) {
        throw Exception(Exception::MKXPError, "Failed to parse CSV: %s", e.what());
    }

    return ret;
}
RB_METHOD_GUARD_END

json5pp::value loadUserSettings() {
    json5pp::value ret;
    VALUE cpath = rb_utf8_str_new_cstr(shState->config().userConfPath.c_str());
    
    if (rb_funcall(rb_cFile, rb_intern("exists?"), 1, cpath) == Qtrue) {
        VALUE f = rb_funcall(rb_cFile, rb_intern("open"), 2, cpath, rb_str_new("r", 1));
        VALUE data = rb_funcall(f, rb_intern("read"), 0);
        rb_funcall(f, rb_intern("close"), 0);
        ret = json5pp::parse5(RSTRING_PTR(data));
    }
    
    if (!ret.is_object())
        ret = json5pp::object({});
    
    return ret;
}

void saveUserSettings(json5pp::value &settings) {
    VALUE cpath = rb_utf8_str_new_cstr(shState->config().userConfPath.c_str());
    VALUE f = rb_funcall(rb_cFile, rb_intern("open"), 2, cpath, rb_str_new("w", 1));
    rb_funcall(f, rb_intern("write"), 1, rb_utf8_str_new_cstr(settings.stringify5(json5pp::rule::space_indent<>()).c_str()));
    rb_funcall(f, rb_intern("close"), 0);
}

RB_METHOD(mkxpGetJSONSetting) {
    RB_UNUSED_PARAM;
    
    VALUE sname;
    rb_scan_args(argc, argv, "1", &sname);
    SafeStringValue(sname);
    
    auto settings = loadUserSettings();
    auto &s = settings.as_object();
    
    if (s[RSTRING_PTR(sname)].is_null()) {
        return json2rb(shState->config().raw.as_object()[RSTRING_PTR(sname)]);
    }
    
    return json2rb(s[RSTRING_PTR(sname)]);
    
}

RB_METHOD_GUARD(mkxpSetJSONSetting) {
    RB_UNUSED_PARAM;
    
    VALUE sname, svalue;
    rb_scan_args(argc, argv, "2", &sname, &svalue);
    SafeStringValue(sname);
    
    auto settings = loadUserSettings();
    auto &s = settings.as_object();
    s[RSTRING_PTR(sname)] = rb2json(svalue);
    saveUserSettings(settings);
    
    return Qnil;
}
RB_METHOD_GUARD_END

RB_METHOD(mkxpGetAllJSONSettings) {
    RB_UNUSED_PARAM;
    
    return json2rb(shState->config().raw);
}

static VALUE rgssMainCb(VALUE block) {
    rb_funcall2(block, rb_intern("call"), 0, 0);
    return Qnil;
}

static VALUE rgssMainRescue(VALUE arg, VALUE exc) {
    VALUE *excRet = (VALUE *)arg;
    
    *excRet = exc;
    
    return Qnil;
}

static bool processReset(bool rubyExc) {
	const char *str = "Audio.__reset__; Graphics.__reset__;";
	
	if (rubyExc) {
		rb_eval_string(str);
	} else {
		int state;
		rb_eval_string_protect(str, &state);
		return state;
	}
	
	return 0;
}

#if RAPI_FULL > 187
static VALUE newStringUTF8(const char *string, long length) {
    return rb_enc_str_new(string, length, rb_utf8_encoding());
}
#else
#define newStringUTF8 rb_str_new
#endif

struct evalArg {
    VALUE string;
    VALUE filename;
};

static VALUE evalHelper(evalArg *arg) {
    VALUE argv[] = {arg->string, Qnil, arg->filename};
#ifdef MKXPZ_VITA_DIAGNOSTICS
    vitaDiagLog("RUBY", "eval_call_begin file=%s bytes=%ld",
                RSTRING_PTR(arg->filename), RSTRING_LEN(arg->string));
#endif
    VALUE result = rb_funcall2(topSelf, rb_intern("eval"), ARRAY_SIZE(argv), argv);
#ifdef MKXPZ_VITA_DIAGNOSTICS
    vitaDiagLog("RUBY", "eval_call_return file=%s", RSTRING_PTR(arg->filename));
#endif
    return result;
}

static VALUE evalString(VALUE string, VALUE filename, int *state) {
    evalArg arg = {string, filename};
#ifdef MKXPZ_VITA_DIAGNOSTICS
    vitaDiagLog("RUBY", "eval_protect_begin file=%s bytes=%ld",
                RSTRING_PTR(filename), RSTRING_LEN(string));
#endif
    VALUE result = rb_protect((VALUE(*)(VALUE))evalHelper, (VALUE)&arg, state);
#ifdef MKXPZ_VITA_DIAGNOSTICS
    vitaDiagLog("RUBY", "eval_protect_end file=%s state=%d",
                RSTRING_PTR(filename), *state);
#endif
    return result;
}

static void runCustomScript(const std::string &filename) {
    std::string scriptData;
    std::string scriptPath = filename;

#ifdef __vita__
    /* The Vita process may not have app0: as its libc current directory.
     * Resolve packaged scripts to the mounted application explicitly before
     * going through SDL/PhysFS. */
    scriptPath = mkxp_fs::normalizePath(filename.c_str(), false, true);
#endif
    
    if (!readFileSDL(scriptPath.c_str(), scriptData)) {
        showEngineError(std::string("Unable to open '") + filename + "'");
        return;
    }
    
    int state = 0;
    evalString(newStringUTF8(scriptData.c_str(), scriptData.size()),
               newStringUTF8(filename.c_str(), filename.size()), &state);
}

RB_METHOD_GUARD(mriRgssMain) {
    RB_UNUSED_PARAM;

    /* Execute postload scripts */
    const Config &conf = shState->rtData().config;
    for (std::vector<std::string>::const_iterator i = conf.postloadScripts.begin();
        i != conf.postloadScripts.end(); ++i)
    {
        if (shState->rtData().rqTerm)
            break;
        runCustomScript(*i);
    }

#ifdef MKXPZ_VITA_DIAGNOSTICS
    vitaEnableRubyDepthTrace();
#endif

    while (true) {
        VALUE exc = Qnil;
#if RAPI_FULL < 270
        rb_rescue2((VALUE(*)(ANYARGS))rgssMainCb, rb_block_proc(),
                   (VALUE(*)(ANYARGS))rgssMainRescue, (VALUE)&exc, rb_eException,
                   (VALUE)0);
#else
        rb_rescue2(rgssMainCb, rb_block_proc(), rgssMainRescue, (VALUE)&exc,
                   rb_eException, (VALUE)0);
#endif
        
        if (NIL_P(exc))
            break;
        
        if (rb_obj_class(exc) == getRbData()->exc[Reset])
            processReset(true);
        else
            rb_exc_raise(exc);
    }
    
    return Qnil;
}
RB_METHOD_GUARD_END

RB_METHOD_GUARD(mriRgssStop) {
    RB_UNUSED_PARAM;
    
    while (true)
        shState->graphics().update();
    
    return Qnil;
}
RB_METHOD_GUARD_END

RB_METHOD(_kernelCaller) {
    RB_UNUSED_PARAM;
    
    VALUE trace =
    rb_funcall2(rb_mKernel, rb_intern("_mkxp_kernel_caller_alias"), 0, 0);
    
    if (!RB_TYPE_P(trace, RUBY_T_ARRAY))
        return trace;
    
    long len = RARRAY_LEN(trace);
    
    if (len < 2)
        return trace;
    
    /* Remove useless "ruby:1:in 'eval'" */
    rb_ary_pop(trace);
    
    /* Also remove trace of this helper function */
    rb_ary_shift(trace);
    
    len -= 2;
    
    if (len == 0)
        return trace;
    
    /* RMXP does this, not sure if specific or 1.8 related */
    VALUE args[] = {rb_utf8_str_new_cstr(":in `<main>'"), rb_utf8_str_new_cstr("")};
    rb_funcall2(rb_ary_entry(trace, len - 1), rb_intern("gsub!"), 2, args);
    
    return trace;
}

VALUE kernelLoadDataInt(const char *filename, bool rubyExc, bool raw);

struct BacktraceData {
    /* Maps: Ruby visible filename, To: Actual script name */
    BoostHash<std::string, std::string> scriptNames;
};

bool evalScript(VALUE string, const char *filename)
{
    int state;
    evalString(string, rb_utf8_str_new_cstr(filename), &state);
    if (state) return false;
    return true;
}


#define SCRIPT_SECTION_FMT (rgssVer >= 3 ? "{%04ld}" : "Section%03ld")


static void runRMXPScripts(BacktraceData &btData) {
    const Config &conf = shState->rtData().config;
    const std::string &scriptPack = conf.game.scripts;
    
    if (scriptPack.empty()) {
        showEngineError("No script file has been specified. Check the game's INI and try again.");
        return;
    }
    
    if (!shState->fileSystem().exists(scriptPack.c_str())) {
        showEngineError("Unable to load scripts from '" + scriptPack + "'");
        return;
    }
    
    VALUE scriptArray;
    
    /* We checked if Scripts.rxdata exists, but something might
     * still go wrong */
    vitaDiagLog("BOOTPERF", "rgss_script_pack_load_begin file=%s",
                scriptPack.c_str());
    try {
        scriptArray = kernelLoadDataInt(scriptPack.c_str(), false, false);
    } catch (const Exception &e) {
        showEngineError(std::string("Failed to read script data: ") + e.msg);
        return;
    }
    
    if (!RB_TYPE_P(scriptArray, RUBY_T_ARRAY)) {
        showEngineError("Failed to read script data");
        return;
    }

#ifdef __vita__
    vitaStartupTimerMark("rgss_script_pack_loaded");
#endif
    vitaDiagLog("BOOTPERF", "rgss_script_pack_load_end");
    
    rb_gv_set("$RGSS_SCRIPTS", scriptArray);
    
    long scriptCount = RARRAY_LEN(scriptArray);
    vitaDiagLog("RUBY", "rgss_script_array count=%ld", scriptCount);
    
    std::string decodeBuffer;
    decodeBuffer.resize(0x1000);
    vitaDiagLog("BOOTPERF", "rgss_decode_begin count=%ld", scriptCount);
    
    for (long i = 0; i < scriptCount; ++i) {
        VALUE script = rb_ary_entry(scriptArray, i);
        
        if (!RB_TYPE_P(script, RUBY_T_ARRAY))
            continue;
        
        VALUE scriptName = rb_ary_entry(script, 1);
        VALUE scriptString = rb_ary_entry(script, 2);

        if (i == 0 || (i % 100) == 0 || i + 1 == scriptCount)
            vitaDiagLog("RUBY", "rgss_decode index=%ld/%ld name=%s", i,
                        scriptCount, RSTRING_PTR(scriptName));
        
        int result = Z_OK;
        unsigned long bufferLen;
        
        while (true) {
            unsigned char *bufferPtr = reinterpret_cast<unsigned char *>(
                                                                         const_cast<char *>(decodeBuffer.c_str()));
            const unsigned char *sourcePtr =
            reinterpret_cast<const unsigned char *>(RSTRING_PTR(scriptString));
            
            bufferLen = decodeBuffer.length();
            
            result = uncompress(bufferPtr, &bufferLen, sourcePtr,
                                RSTRING_LEN(scriptString));
            
            bufferPtr[bufferLen] = '\0';
            
            if (result != Z_BUF_ERROR)
                break;
            
            decodeBuffer.resize(decodeBuffer.size() * 2);
        }
        
        if (result != Z_OK) {
            static char buffer[256];
            snprintf(buffer, sizeof(buffer), "Error decoding script %ld: '%s'", i,
                     RSTRING_PTR(scriptName));
            
            showEngineError(buffer);
            
            break;
        }
        
        rb_ary_store(script, 3, rb_utf8_str_new_cstr(decodeBuffer.c_str()));

    }

    vitaDiagLog("RUBY", "rgss_decode_complete count=%ld", scriptCount);
    vitaDiagLog("BOOTPERF", "rgss_decode_end count=%ld", scriptCount);
#ifdef MKXPZ_VITA_DIAGNOSTICS
    vitaDiagLogMemory("rgss_decode_complete");
#endif
#ifdef __vita__
    vitaStartupTimerMark("rgss_decode_complete");
#endif
    
    /* Execute preloaded scripts */
    vitaDiagLog("BOOTPERF", "preload_begin count=%u",
                (unsigned)conf.preloadScripts.size());
    for (std::vector<std::string>::const_iterator i = conf.preloadScripts.begin();
         i != conf.preloadScripts.end(); ++i)
    {
        if (shState->rtData().rqTerm)
            break;
        runCustomScript(*i);
    }
    vitaDiagLog("BOOTPERF", "preload_end");
    
    VALUE exc = rb_gv_get("$!");
    if (exc != Qnil)
        return;
    
    while (true) {
        vitaDiagLog("RUBY", "rgss_eval_begin count=%ld", scriptCount);
        vitaDiagLog("BOOTPERF", "rgss_eval_begin count=%ld", scriptCount);
#ifdef __vita__
        vitaStartupTimerMark("rgss_eval_begin");
#endif
        for (long i = 0; i < scriptCount; ++i) {
            if (shState->rtData().rqTerm)
                break;
            
            VALUE script = rb_ary_entry(scriptArray, i);
            VALUE scriptDecoded = rb_ary_entry(script, 3);
            VALUE string =
            newStringUTF8(RSTRING_PTR(scriptDecoded), RSTRING_LEN(scriptDecoded));
            
            VALUE fname;
            const char *scriptName = RSTRING_PTR(rb_ary_entry(script, 1));
            if (i == 0 || (i % 25) == 0 || i + 1 == scriptCount)
                vitaDiagLog("BOOTPERF", "rgss_eval_checkpoint index=%ld/%ld name=%s",
                            i, scriptCount, scriptName);
            if (i + 1 == scriptCount)
                vitaDiagLog("BOOTPERF", "rgss_main_enter index=%ld name=%s",
                            i, scriptName);
            char buf[512];
            int len;

            if (i == 0 || (i % 100) == 0 || i + 1 == scriptCount)
                vitaDiagLog("RUBY", "rgss_eval index=%ld/%ld name=%s", i,
                            scriptCount, scriptName);
#ifdef MKXPZ_VITA_DIAGNOSTICS
            if (i == 0 || (i % 25) == 0 || i + 1 == scriptCount)
                vitaDiagLogMemory("rgss_eval_progress");
#endif
            
            if (conf.useScriptNames)
                len = snprintf(buf, sizeof(buf), "%03ld:%s", i, scriptName);
            else
                len = snprintf(buf, sizeof(buf), SCRIPT_SECTION_FMT, i);
            
            fname = newStringUTF8(buf, len);
            btData.scriptNames.insert(buf, scriptName);

#ifdef __vita__
            /* Toro's Steam script (BS1, BS2) binds Win32 Steam DLLs in its
             * class body, which cannot load on Vita. Match by exact script
             * name at any index; the Vita bootstrap above defines the
             * no-op class the victory events reference. */
            if (std::strcmp(scriptName, "steam_acheivement") == 0) {
                vitaDiagLog("RUBY", "rgss_eval_skip_vita index=%ld name=%s", i,
                            scriptName);
                continue;
            }
#endif
            
            
            // if the script name starts with |s|, only execute
            // it if "s" is the same first letter as the platform
            // we're running on
            
            // |W| - Windows, |M| - Mac OS X, |L| - Linux
            
            // Adding a 'not' symbol means it WON'T run on that
            // platform (i.e. |!W| won't run on Windows)
            /*
             if (scriptName[0] == '|') {
             int len = strlen(scriptName);
             if (len > 2) {
             if (scriptName[1] == '!' && len > 3 &&
             scriptName[3] == scriptName[0]) {
             if (toupper(scriptName[2]) == platform[0])
             continue;
             }
             if (scriptName[2] == scriptName[0] &&
             toupper(scriptName[1]) != platform[0])
             continue;
             }
             }
             */
            
            int state;
            
            evalString(string, fname, &state);
            if (state)
                break;
        }

        vitaDiagLog("RUBY", "rgss_eval_pass_complete");
        
        VALUE exc = rb_gv_get("$!");
        if (rb_obj_class(exc) != getRbData()->exc[Reset])
            break;
        
        if (processReset(false))
            break;
    }
}

static void showExc(VALUE exc, const BacktraceData &btData) {
#ifdef __vita__
    VALUE message = rb_funcall2(exc, rb_intern("message"), 0, NULL);
    VALUE trace = rb_funcall2(exc, rb_intern("backtrace"), 0, NULL);
    std::string report = std::string(rb_obj_classname(exc)) + "\n\n";
    if (RB_TYPE_P(message, RUBY_T_STRING))
        report.append(RSTRING_PTR(message), std::min<long>(RSTRING_LEN(message), 8192));
    else
        report += "Exception message unavailable.";
    report += "\n\nBacktrace:\n";
    if (RB_TYPE_P(trace, RUBY_T_ARRAY) && RARRAY_LEN(trace) > 0) {
        for (long i = 0; i < RARRAY_LEN(trace) && i < 32; ++i) {
            VALUE entry = rb_ary_entry(trace, i);
            if (!RB_TYPE_P(entry, RUBY_T_STRING)) continue;
            std::string line(RSTRING_PTR(entry), std::min<long>(RSTRING_LEN(entry), 1024));
            const size_t colon = line.find(':');
            if (colon != std::string::npos) {
                const std::string name = line.substr(0, colon);
                line.replace(0, colon, btData.scriptNames.value(name, name));
            }
            report += line + "\n";
        }
    } else {
        report += "No backtrace was supplied by the game.\n";
    }
    vitaWriteErrorReport(report);
    return;
#endif
#ifdef MKXPZ_VITA_DIAGNOSTICS
    /* Ruby exceptions are otherwise silent on Vita (no message box).
     * Always leave the crash name in the log, even in quiet mode. */
    {
        VALUE msg0 = rb_funcall2(exc, rb_intern("message"), 0, NULL);
        VALUE btArr = rb_funcall2(exc, rb_intern("backtrace"), 0, NULL);
        VALUE bt0v = RB_TYPE_P(btArr, RUBY_T_ARRAY) && RARRAY_LEN(btArr) > 0
            ? rb_ary_entry(btArr, 0) : Qnil;
        vitaDiagLog("ERROR", "ruby_exc class=%s msg=%.200s at=%.120s",
                    rb_obj_classname(exc),
                    RB_TYPE_P(msg0, RUBY_T_STRING) ? RSTRING_PTR(msg0) : "?",
                    RB_TYPE_P(bt0v, RUBY_T_STRING) ? RSTRING_PTR(bt0v) : "?");
    }
#endif
    VALUE bt = rb_funcall2(exc, rb_intern("backtrace"), 0, NULL);
    VALUE msg = rb_funcall2(exc, rb_intern("message"), 0, NULL);
    VALUE name = rb_class_path(rb_obj_class(exc));

    if (!RB_TYPE_P(bt, RUBY_T_ARRAY) || RARRAY_LEN(bt) == 0) {
        VALUE msgString = rb_obj_as_string(msg);
        VALUE nameString = rb_obj_as_string(name);
        vitaDiagLog("RUBY", "show_exc_without_backtrace class=%s message=%s",
                    StringValueCStr(nameString), StringValueCStr(msgString));
        std::string fallback(768, '\0');
        snprintf(&fallback[0], fallback.size(), "%s occurred.\n\n%s",
                 RSTRING_PTR(nameString), RSTRING_PTR(msgString));
        fallback.resize(strlen(fallback.c_str()));
        showMsg(fallback);
        return;
    }

    VALUE bt0 = rb_ary_entry(bt, 0);
    if (!RB_TYPE_P(bt0, RUBY_T_STRING)) {
        vitaDiagLog("RUBY", "show_exc_invalid_backtrace_entry class=%s",
                    rb_obj_classname(exc));
        showMsg("Ruby exception occurred without a usable backtrace.");
        return;
    }
    
    VALUE ds = rb_sprintf("%" PRIsVALUE ": %" PRIsVALUE " (%" PRIsVALUE ")",
#if RAPI_MAJOR >= 2
                          bt0, exc, name);
#else
    // Ruby 1.9's version of this function needs char*
    RSTRING_PTR(bt0), RSTRING_PTR(exc), RSTRING_PTR(name));
#endif
    /* omit "useless" last entry (from ruby:1:in `eval') */
    for (long i = 1, btlen = RARRAY_LEN(bt) - 1; i < btlen; ++i)
        rb_str_catf(ds, "\n\tfrom %" PRIsVALUE,
#if RAPI_MAJOR >= 2
                    rb_ary_entry(bt, i));
#else
    RSTRING_PTR(rb_ary_entry(bt, i)));
#endif
    Debug() << StringValueCStr(ds);
    
    char *s = RSTRING_PTR(bt0);
    
    char line[16];
    std::string file(512, '\0');
    
    char *p = s + strlen(s);
    char *e;
    
    while (p != s)
        if (*--p == ':')
            break;
    
    e = p;
    
    while (p != s)
        if (*--p == ':')
            break;
    
    /* s         p  e
     * SectionXXX:YY: in 'blabla' */
    
    *e = '\0';
    strncpy(line, *p ? p + 1 : p, sizeof(line));
    line[sizeof(line) - 1] = '\0';
    *e = ':';
    e = p;
    
    /* s         e
     * SectionXXX:YY: in 'blabla' */
    
    *e = '\0';
    strncpy(&file[0], s, file.size());
    *e = ':';
    
    /* Shrink to fit */
    file.resize(strlen(file.c_str()));
    file = btData.scriptNames.value(file, file);
    
    std::string ms(640, '\0');
    snprintf(&ms[0], ms.size(), "Script '%s' line %s: %s occurred.\n\n%s",
             file.c_str(), line, RSTRING_PTR(name), RSTRING_PTR(msg));
    
    showMsg(ms);
}

static void mriBindingExecute() {
    Config &conf = shState->rtData().config;
    vitaDiagLogThread("ruby_thread_enter");
    
#if RAPI_MAJOR >= 2
    /* Normally only a ruby executable would do a sysinit,
     * but not doing it will lead to crashes due to closed
     * stdio streams on some platforms (eg. Windows) */
    int argc = 0;
    char **argv = 0;
    ruby_sysinit(&argc, &argv);
    vitaDiagLog("RUBY", "ruby_sysinit_ok");

    RUBY_INIT_STACK;
    ruby_init();
    vitaDiagLog("RUBY", "ruby_init_ok");
#ifdef MKXPZ_VITA_DIAGNOSTICS
    vitaDiagLog("RUBY", "features_after_init enumerator=%d thread=%d fiber=%d",
                rb_provided("enumerator.so"),
                rb_provided("thread.rb"),
                rb_provided("fiber.so"));
#endif
    
    std::vector<const char*> rubyArgsC{"mkxp-z"};
    rubyArgsC.push_back("-e ");
    void *node;
    if (conf.jit.enabled) {
#if RAPI_FULL >= 310
        // Ruby v3.1.0 renamed the --jit options to --mjit.
        std::string verboseLevel("--mjit-verbose=");
        std::string maxCache("--mjit-max-cache=");
        std::string minCalls("--mjit-min-calls=");
        rubyArgsC.push_back("--mjit");
#else
        std::string verboseLevel("--jit-verbose=");
        std::string maxCache("--jit-max-cache=");
        std::string minCalls("--jit-min-calls=");
        rubyArgsC.push_back("--jit");
#endif
        verboseLevel += std::to_string(conf.jit.verboseLevel);
        maxCache += std::to_string(conf.jit.maxCache);
        minCalls += std::to_string(conf.jit.minCalls);

        rubyArgsC.push_back(verboseLevel.c_str());
        rubyArgsC.push_back(maxCache.c_str());
        rubyArgsC.push_back(minCalls.c_str());
        node = ruby_options(rubyArgsC.size(), const_cast<char**>(rubyArgsC.data()));
    } else if (conf.yjit.enabled) {
        rubyArgsC.push_back("--yjit");
        // TODO: Maybe support --yjit-exec-mem-size, --yjit-call-threshold
        node = ruby_options(rubyArgsC.size(), const_cast<char**>(rubyArgsC.data()));
    } else {
        node = ruby_options(rubyArgsC.size(), const_cast<char**>(rubyArgsC.data()));
    }
    
    int state = 0;
    bool valid = ruby_executable_node(node, &state);
    vitaDiagLog("RUBY", "ruby_options_complete node_valid=%d state=%d",
                valid ? 1 : 0, state);
    if (valid)
        state = ruby_exec_node(node);
    vitaDiagLog("RUBY", "features_after_options enumerator=%d thread=%d fiber=%d",
                rb_provided("enumerator.so"),
                rb_provided("thread.rb"),
                rb_provided("fiber.so"));
    if (state || !valid) {
        // The message is formatted for and automatically spits
        // out to the terminal, so let's leave it that way for now
        /*
         VALUE exc = rb_errinfo();
         #if RAPI_FULL >= 250
         VALUE msg = rb_funcall(exc, rb_intern("full_message"), 0);
         #else
         VALUE msg = rb_funcall(exc, rb_intern("message"), 0);
         #endif
         */
        showEngineError("An error occurred while initializing Ruby. (Invalid JIT settings?)");
        ruby_cleanup(state);
        shState->rtData().rqTermAck.set();
        return;
    }
    rb_enc_set_default_internal(rb_enc_from_encoding(rb_utf8_encoding()));
    rb_enc_set_default_external(rb_enc_from_encoding(rb_utf8_encoding()));
#else
    ruby_init();
    rb_eval_string("$KCODE='U'");
#ifdef __WIN32__
    if (!conf.winConsole) {
        VALUE iostr = rb_str_new2("NUL");
        // Sysinit isn't a thing yet, so send io to /dev/null instead
        rb_funcall(rb_gv_get("$stderr"), rb_intern("reopen"), 1, iostr);
        rb_funcall(rb_gv_get("$stdout"), rb_intern("reopen"), 1, iostr);
    }
#endif
#endif
    
    topSelf = rgssVer == 1 ? Qnil : rb_eval_string("self");
    
    VALUE rbArgv = rb_get_argv();
    for (const auto &str : conf.launchArgs)
        rb_ary_push(rbArgv, rb_utf8_str_new_cstr(str.c_str()));
    
    // Duplicates get pushed for some reason
    rb_funcall(rbArgv, rb_intern("uniq!"), 0);
    
    VALUE lpaths = rb_gv_get(":");
    rb_ary_clear(lpaths);
    
#if defined(MKXPZ_BUILD_XCODE) && RAPI_MAJOR >= 2
    std::string resPath = mkxp_fs::getResourcePath();
    resPath += "/Ruby/" + std::to_string(RAPI_MAJOR) + "." + std::to_string(RAPI_MINOR) + ".0";
    rb_ary_push(lpaths, rb_str_new(resPath.c_str(), resPath.size()));
#endif
    
    if (!conf.rubyLoadpaths.empty()) {
        /* Setup custom load paths */
        for (size_t i = 0; i < conf.rubyLoadpaths.size(); ++i) {
            std::string &path = conf.rubyLoadpaths[i];
            
            VALUE pathv = rb_str_new(path.c_str(), path.size());
            rb_ary_push(lpaths, pathv);
        }
    }
#ifndef WORKDIR_CURRENT
    else {
        rb_ary_push(lpaths, rb_utf8_str_new_cstr(mkxp_fs::getCurrentDirectory().c_str()));
    }
#endif
    
    RbData rbData;
    shState->setBindingData(&rbData);
    BacktraceData btData;

    mriBindingInit();
    vitaDiagLog("RUBY", "binding_init_ok");
#ifdef MKXPZ_VITA_DIAGNOSTICS
    vitaDiagLog("RUBY", "features_before_game enumerator=%d thread=%d fiber=%d",
                rb_provided("enumerator.so"),
                rb_provided("thread.rb"),
                rb_provided("fiber.so"));
#endif

    std::string &customScript = conf.customScript;
    if (!customScript.empty())
        runCustomScript(customScript);
    else
        runRMXPScripts(btData);
    vitaDiagLog("RUBY", "game_scripts_returned");
    
#if RAPI_FULL > 187
    VALUE exc = rb_errinfo();
#else
    VALUE exc = rb_gv_get("$!");
#endif
    vitaDiagLog("RUBY", "game_error nil=%d system_exit=%d",
                NIL_P(exc) ? 1 : 0,
                (!NIL_P(exc) && rb_obj_is_kind_of(exc, rb_eSystemExit)) ? 1 : 0);
    if (!NIL_P(exc) && !rb_obj_is_kind_of(exc, rb_eSystemExit)) {
        VALUE message = rb_funcall2(exc, rb_intern("message"), 0, NULL);
        vitaDiagLog("RUBY", "game_error_detail class=%s message=%s",
                    rb_obj_classname(exc), StringValueCStr(message));
        VALUE backtrace = rb_funcall2(exc, rb_intern("backtrace"), 0, NULL);
        long backtraceCount = RB_TYPE_P(backtrace, RUBY_T_ARRAY)
            ? RARRAY_LEN(backtrace) : 0;
        for (long i = 0; i < backtraceCount && i < 8; ++i) {
            VALUE entry = rb_ary_entry(backtrace, i);
            if (RB_TYPE_P(entry, RUBY_T_STRING))
                vitaDiagLog("RUBY", "game_error_backtrace[%ld]=%s", i,
                            StringValueCStr(entry));
        }
    }
    if (!NIL_P(exc) && !rb_obj_is_kind_of(exc, rb_eSystemExit))
        showExc(exc, btData);
    
    ruby_cleanup(0);
    vitaDiagLog("RUBY", "ruby_cleanup_ok");
    
    shState->rtData().rqTermAck.set();
}

static void mriBindingTerminate() { throw Exception(Exception::SystemExit, " "); }

static void mriBindingReset() { throw Exception(Exception::Reset, " "); }
