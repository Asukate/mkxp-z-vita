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
#ifdef __vita__
#include "input/vita_input.h"
#endif
#include "binding.h"

#include "sharedstate.h"
#include "eventthread.h"
#include "vita_diagnostic.h"
#ifdef __vita__
#include "vita_startup_timer.h"
#include "vita_error_report.h"
#include "util/win32-text.h"
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
#include <map>
#include <zlib.h>
#ifdef __vita__
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <SDL_cpuinfo.h>
#include <SDL_filesystem.h>
#include <SDL_loadso.h>
#include <SDL_power.h>
#include <SDL_mutex.h>
#include <SDL_thread.h>
#include <SDL_timer.h>

extern const char module_rpg1[];
extern const char module_rpg2[];
extern const char module_rpg3[];


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
void nativeEventsInit(int rgssVersion);
void hardrpgProfInit();
void nativeEventsSectionLoaded(VALUE fname, VALUE source);

RB_METHOD(mkxpDelta);
RB_METHOD(mriPrint);
RB_METHOD(mriP);
RB_METHOD(mkxpDataDirectory);
#ifdef __vita__
RB_METHOD(mkxpGameDataDirectory);
#endif
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
RB_METHOD(mkxpVitaMemorySnapshot);
#ifdef __vita__
RB_METHOD(mkxpVitaPadScript);
RB_METHOD(mkxpVitaPadStats);
#endif
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

    nativeEventsInit(rgssVer);
    hardrpgProfInit();

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
    _rb_define_module_function(mod, "delta", mkxpDelta);
    _rb_define_module_function(mod, "uptime", mkxpDelta);
    _rb_define_module_function(mod, "data_directory", mkxpDataDirectory);
#ifdef __vita__
    /* Desktop mkxp called this module MKXP. Games released on it (To the
     * Moon) ask MKXP.data_directory where their settings and saves go. */
    VALUE desktopMkxp = rb_define_module("MKXP");
    _rb_define_module_function(desktopMkxp, "data_directory", mkxpGameDataDirectory);
    _rb_define_module_function(desktopMkxp, "puts", mkxpPuts);
#endif
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
    _rb_define_module_function(mod, "vita_memory_snapshot", mkxpVitaMemorySnapshot);
#ifdef __vita__
    /* Benchmark packages: scripted pad presses (needs vitaPadTest). */
    _rb_define_module_function(mod, "vita_pad_script", mkxpVitaPadScript);
    _rb_define_module_function(mod, "vita_pad_stats", mkxpVitaPadStats);
#endif
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
#ifdef __vita__
    /* The rest of what games ask of desktop mkxp's module; resolved when
     * called, after the Input binding exists. */
    rb_eval_string(
        "module MKXP\n"
        "  def self.raw_key_states; Input.raw_key_states; end\n"
        "  def self.mouse_in_window; Input.mouse_in_window; end\n"
        "end\n");
#endif
    
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

#ifdef __vita__
/* Beside the game, with the trailing separator desktop mkxp returned: the
 * games join file names straight onto it. */
RB_METHOD(mkxpGameDataDirectory) {
    RB_UNUSED_PARAM;

    std::string path = shState->config().gameFolder;
    if (path.empty())
        path = ".";
    if (path.back() != '/')
        path += '/';
    return rb_utf8_str_new_cstr(path.c_str());
}
#endif

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

RB_METHOD(mkxpVitaMemorySnapshot) {
    RB_UNUSED_PARAM;
    rb_check_arity(argc, 0, 0);
    char snapshot[1280];
    const size_t size = vitaMemorySnapshot(snapshot, sizeof(snapshot));
    return rb_utf8_str_new(snapshot, size);
}

#ifdef __vita__
/* [[down_ms, up_ms, button_bits], ...] measured from now. */
RB_METHOD(mkxpVitaPadScript) {
    RB_UNUSED_PARAM;
    rb_check_arity(argc, 1, 1);
    VALUE list = argv[0];
    Check_Type(list, T_ARRAY);
    std::vector<std::array<double, 3>> presses;
    for (long i = 0; i < RARRAY_LEN(list); ++i) {
        VALUE item = rb_ary_entry(list, i);
        Check_Type(item, T_ARRAY);
        if (RARRAY_LEN(item) != 3)
            rb_raise(rb_eArgError, "each press is [down_ms, up_ms, bits]");
        presses.push_back({NUM2DBL(rb_ary_entry(item, 0)), NUM2DBL(rb_ary_entry(item, 1)),
                           NUM2DBL(rb_ary_entry(item, 2))});
    }
    vitaPadScript(presses);
    return Qnil;
}

RB_METHOD(mkxpVitaPadStats) {
    RB_UNUSED_PARAM;
    rb_check_arity(argc, 0, 0);
    const std::string stats = vitaPadStats();
    return rb_utf8_str_new(stats.data(), stats.size());
}
#endif

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

#ifdef __vita__
/* Compiled game scripts, kept between boots. Parsing and compiling a game's
 * scripts is most of the time before its title screen (0.8 s for Ao Oni on
 * the Vita); loading Ruby's binary form of the same instruction sequences is
 * about five times faster. One file per game holds every script that
 * compiled cleanly; an entry is used only for the same script name and the
 * same source (length, CRC-32 and Adler-32) on the same Ruby build.
 * InstructionSequence#eval runs a script at the top level with the same
 * self, constant scope and file name as Kernel#eval did. A script that does
 * not compile as is takes the eval and legacy-repair path below. */
namespace ScriptCache {
struct Entry
{
    uint32_t length, crc, adler;
    std::string binary;
};

static std::map<std::string, Entry> entries;
static std::string path, game;
static bool dirty = false, loaded = false;
static const char Magic[8] = {'H','R','G','I','S','Q','1','\n'};

static void source(VALUE string, uint32_t &length, uint32_t &crc, uint32_t &adler)
{
    const Bytef *bytes = reinterpret_cast<const Bytef *>(RSTRING_PTR(string));
    length = static_cast<uint32_t>(RSTRING_LEN(string));
    crc = crc32(0, bytes, length);
    adler = adler32(1, bytes, length);
}

static std::string rubyBuild()
{
    VALUE description = rb_const_get(rb_cObject, rb_intern("RUBY_DESCRIPTION"));
    return std::string(RSTRING_PTR(description), RSTRING_LEN(description));
}

static void load(const std::string &gameScripts)
{
    if (loaded)
        return;
    loaded = true;
    game = gameScripts;
    char name[96];
    snprintf(name, sizeof(name), "ux0:/data/hardrpg/cache/scripts-v1/%08lx.bin",
             (unsigned long)crc32(0, reinterpret_cast<const Bytef *>(gameScripts.data()), gameScripts.size()));
    path = name;
    FILE *f = fopen(path.c_str(), "rb");
    if (!f)
        return;
    std::string bytes;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size > 16 && size < 64 * 1024 * 1024) {
        bytes.resize(size);
        if (fread(&bytes[0], 1, size, f) != static_cast<size_t>(size))
            bytes.clear();
    }
    fclose(f);
    if (bytes.size() < 16 || memcmp(bytes.data(), Magic, 8))
        return;
    uint32_t stored;
    memcpy(&stored, bytes.data() + bytes.size() - 4, 4);
    if (stored != crc32(0, reinterpret_cast<const Bytef *>(bytes.data()), bytes.size() - 4))
        return;
    size_t at = 8, end = bytes.size() - 4;
    auto take = [&](void *out, size_t n) {
        if (end - at < n)
            return false;
        memcpy(out, bytes.data() + at, n);
        at += n;
        return true;
    };
    auto takeString = [&](std::string &out) {
        uint32_t n;
        if (!take(&n, 4) || end - at < n)
            return false;
        out.assign(bytes.data() + at, n);
        at += n;
        return true;
    };
    std::string build, game;
    uint32_t count;
    if (!takeString(build) || build != rubyBuild() || !takeString(game) || game != gameScripts ||
        !take(&count, 4))
        return;
    std::map<std::string, Entry> read;
    for (uint32_t i = 0; i < count; ++i) {
        std::string scriptName;
        Entry e;
        if (!takeString(scriptName) || !take(&e.length, 4) || !take(&e.crc, 4) ||
            !take(&e.adler, 4) || !takeString(e.binary))
            return;
        read[scriptName] = e;
    }
    if (at == end)
        entries = read;
}

static void save()
{
    if (!dirty || path.empty())
        return;
    dirty = false;
    std::string bytes(Magic, 8);
    auto put = [&](const void *data, size_t n) { bytes.append(static_cast<const char *>(data), n); };
    auto putString = [&](const std::string &value) {
        uint32_t n = value.size();
        put(&n, 4);
        bytes += value;
    };
    putString(rubyBuild());
    putString(game);
    uint32_t count = entries.size();
    put(&count, 4);
    for (std::map<std::string, Entry>::const_iterator i = entries.begin(); i != entries.end(); ++i) {
        putString(i->first);
        put(&i->second.length, 4);
        put(&i->second.crc, 4);
        put(&i->second.adler, 4);
        putString(i->second.binary);
    }
    uint32_t check = crc32(0, reinterpret_cast<const Bytef *>(bytes.data()), bytes.size());
    put(&check, 4);

    mkdir("ux0:/data/hardrpg/cache", 0777);
    mkdir("ux0:/data/hardrpg/cache/scripts-v1", 0777);
    const std::string temp = path + ".tmp";
    FILE *f = fopen(temp.c_str(), "wb");
    if (!f)
        return;
    bool ok = fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    ok = fclose(f) == 0 && ok;
    if (ok) {
        remove(path.c_str());
        ok = rename(temp.c_str(), path.c_str()) == 0;
    }
    if (!ok)
        remove(temp.c_str());
}

static VALUE loadBinary(VALUE binary)
{
    VALUE iseqClass = rb_path2class("RubyVM::InstructionSequence");
    return rb_funcall(iseqClass, rb_intern("load_from_binary"), 1, binary);
}

static VALUE compile(VALUE args)
{
    VALUE iseqClass = rb_path2class("RubyVM::InstructionSequence");
    return rb_funcall(iseqClass, rb_intern("compile"), 2, rb_ary_entry(args, 0), rb_ary_entry(args, 1));
}

static VALUE toBinary(VALUE iseq)
{
    return rb_funcall(iseq, rb_intern("to_binary"), 0);
}

static VALUE evaluate(VALUE iseq)
{
    return rb_funcall(iseq, rb_intern("eval"), 0);
}

/* Runs one game script from the cache, or compiles it and keeps it. Returns
 * false, with nothing run, when the script does not compile as is. */
static bool eval(VALUE string, VALUE fname, bool last, int &state)
{
    const std::string name(RSTRING_PTR(fname), RSTRING_LEN(fname));
    uint32_t length, crc, adler;
    source(string, length, crc, adler);
    VALUE iseq = Qnil;
    int failed = 0;
    std::map<std::string, Entry>::const_iterator found = entries.find(name);
    if (found != entries.end()) {
        const Entry *e = &found->second;
        if (e->length == length && e->crc == crc && e->adler == adler) {
            iseq = rb_protect(loadBinary, rb_str_new(e->binary.data(), e->binary.size()), &failed);
            if (failed) {
                rb_set_errinfo(Qnil);
                iseq = Qnil;
            }
        }
    }
    if (NIL_P(iseq)) {
        iseq = rb_protect(compile, rb_ary_new_from_args(2, string, fname), &failed);
        if (failed) {
            rb_set_errinfo(Qnil);
            return false;
        }
        VALUE binary = rb_protect(toBinary, iseq, &failed);
        if (failed) {
            rb_set_errinfo(Qnil);
        } else {
            Entry e;
            e.length = length;
            e.crc = crc;
            e.adler = adler;
            e.binary.assign(RSTRING_PTR(binary), RSTRING_LEN(binary));
            entries[name] = e;
            dirty = true;
        }
    }
    /* The last script runs the game and returns only when it ends. */
    if (last)
        save();
    rb_protect(evaluate, iseq, &state);
    return true;
}
}
#endif

#ifdef __vita__
extern "C" void vitaScriptCacheFlush(void)
{
    ScriptCache::save();
}
#endif

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

#ifdef __vita__
/* Crash rescue (launcher/crash_rescue.rb) keeps a copy of the game's own save
 * data from the last map change or battle start. Its files for this game are
 * ux0:/data/hardrpg/rescue/<name>.{rescue,meta,json}: games started from the
 * launcher use their settings key (config/<key>.json, the key the launcher
 * looks for), anything else a hash of the game folder.
 *
 * Each copy is handed to a writer thread (HardRPGRescueNative.flush), so a
 * crash, a closed app or a power loss leaves the latest copy on the card
 * without the game waiting for the card. When a Ruby error ends the game the
 * error is added to the launcher's file; when the game ends normally (it
 * quits, or Back to library) the files are removed. The setting crashRescue
 * (launcher, per game, in-game menu) is read by the script before each copy. */
namespace {
struct RescueWriter {
    SDL_mutex *lock = nullptr;
    SDL_cond *wake = nullptr;
    SDL_cond *idle = nullptr;
    SDL_Thread *thread = nullptr;
    std::string base;
    enum Job { None, Write, Clear } job = None;
    bool busy = false;
    std::string data, meta, json;
};
RescueWriter rescueWriter;
const char *const rescueFiles[] = {".rescue", ".meta", ".json", ".shot"};

void rescueRemoveFiles(const std::string &base) {
    /* The launcher looks for .json: it goes first. */
    remove((base + ".json").c_str());
    for (const char *ext : rescueFiles)
        remove((base + ext).c_str());
}

int rescueWriterMain(void *) {
    RescueWriter &w = rescueWriter;
    SDL_LockMutex(w.lock);
    for (;;) {
        while (w.job == RescueWriter::None)
            SDL_CondWait(w.wake, w.lock);
        RescueWriter::Job job = w.job;
        std::string data, meta, json;
        data.swap(w.data); meta.swap(w.meta); json.swap(w.json);
        w.job = RescueWriter::None;
        w.busy = true;
        SDL_UnlockMutex(w.lock);
        if (job == RescueWriter::Clear)
            rescueRemoveFiles(w.base);
        else {
            /* The launcher's file last: its presence means the set is whole. */
            remove((w.base + ".json").c_str());
            if (vitaWriteFileReplacing((w.base + ".rescue").c_str(), data.data(), data.size()) &&
                vitaWriteFileReplacing((w.base + ".meta").c_str(), meta.data(), meta.size()))
                vitaWriteFileReplacing((w.base + ".json").c_str(), json.data(), json.size());
        }
        SDL_LockMutex(w.lock);
        w.busy = false;
        SDL_CondBroadcast(w.idle);
    }
    return 0;
}

void rescueQueue(RescueWriter::Job job, std::string data = {}, std::string meta = {}, std::string json = {}) {
    RescueWriter &w = rescueWriter;
    if (!w.lock)
        return;
    SDL_LockMutex(w.lock);
    w.job = job; /* The newest job replaces one not started yet. */
    w.data.swap(data); w.meta.swap(meta); w.json.swap(json);
    SDL_CondSignal(w.wake);
    SDL_UnlockMutex(w.lock);
}

/* Waits (up to timeoutMs) until the writer has nothing left to do, then
 * keeps it from starting anything new: the caller writes on its own. */
void rescueSettle(unsigned timeoutMs) {
    RescueWriter &w = rescueWriter;
    if (!w.lock)
        return;
    SDL_LockMutex(w.lock);
    const Uint32 until = SDL_GetTicks() + timeoutMs;
    while ((w.busy || w.job != RescueWriter::None) && SDL_GetTicks() < until)
        SDL_CondWaitTimeout(w.idle, w.lock, 50);
    w.job = RescueWriter::None;
    SDL_UnlockMutex(w.lock);
}

std::string rescueBytes(VALUE v) {
    return RB_TYPE_P(v, RUBY_T_STRING) ? std::string(RSTRING_PTR(v), RSTRING_LEN(v)) : std::string();
}

/* HardRPGRescueNative.fingerprint(obj): a 64-bit hash of obj's whole content
 * (objects by class name and instance variable names, arrays, hashes, strings
 * with their encoding, numbers, symbols, and data objects that write
 * themselves with _dump, such as Table). It only uses names and bytes, so the
 * same data gives the same value in every launch. crash_rescue.rb compares
 * the game's static data (maps, common events) with what loading the files
 * gives, so a copy can name that data instead of writing it. nil when
 * something in obj cannot be hashed this way, or it is too deep or too big. */
#define RESCUE_FP_DEPTH 48
#define RESCUE_FP_NODES 4000000L
#define RESCUE_FP_CACHE 256

typedef struct {
    uint64_t h;
    int depth;
    long nodes;
    int bad;
    ID dump;
    VALUE class_key[RESCUE_FP_CACHE];
    uint64_t class_hash[RESCUE_FP_CACHE];
    ID id_key[RESCUE_FP_CACHE];
    uint64_t id_hash[RESCUE_FP_CACHE];
} RescueFp;

static inline void rescueFpMix(RescueFp *s, uint64_t v) {
    s->h ^= v;
    s->h *= 0x100000001b3ULL;
    s->h ^= s->h >> 29;
}
/* Bytes, eight at a time (fixed constants: the same in every launch). */
static uint64_t rescueFpHashBytes(const char *p, long n) {
    uint64_t h = 0x9e3779b97f4a7c15ULL ^ (uint64_t)n;
    long i = 0;
    for (; i + 8 <= n; i += 8) {
        uint64_t w;
        memcpy(&w, p + i, 8);
        h ^= w;
        h *= 0xff51afd7ed558ccdULL;
        h ^= h >> 32;
    }
    uint64_t w = 0;
    memcpy(&w, p + i, (size_t)(n - i));
    h ^= w;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 29;
    return h;
}
static uint64_t rescueFpName(const char *name) {
    return name ? rescueFpHashBytes(name, (long)strlen(name)) : 0;
}
static uint64_t rescueFpId(RescueFp *s, ID id) {
    unsigned slot = (unsigned)((id >> 3) % RESCUE_FP_CACHE);
    if (s->id_key[slot] != id) {
        s->id_key[slot] = id;
        s->id_hash[slot] = rescueFpName(rb_id2name(id));
    }
    return s->id_hash[slot];
}
static uint64_t rescueFpClass(RescueFp *s, VALUE klass) {
    unsigned slot = (unsigned)((klass >> 4) % RESCUE_FP_CACHE);
    if (s->class_key[slot] != klass) {
        s->class_key[slot] = klass;
        s->class_hash[slot] = rescueFpName(rb_class2name(klass));
    }
    return s->class_hash[slot];
}
static void rescueFpWalk(VALUE v, RescueFp *s);
static int rescueFpIvar(ID id, VALUE val, st_data_t arg) {
    RescueFp *s = (RescueFp *)arg;
    rescueFpMix(s, rescueFpId(s, id));
    rescueFpWalk(val, s);
    return s->bad ? ST_STOP : ST_CONTINUE;
}
static int rescueFpPair(VALUE key, VALUE val, VALUE arg) {
    RescueFp *s = (RescueFp *)arg;
    rescueFpWalk(key, s);
    rescueFpWalk(val, s);
    return s->bad ? ST_STOP : ST_CONTINUE;
}
static void rescueFpFloat(RescueFp *s, double d) {
    uint64_t bits;
    memcpy(&bits, &d, sizeof(bits));
    rescueFpMix(s, bits ^ 7);
}
static void rescueFpBytes(RescueFp *s, const char *p, long n) {
    rescueFpMix(s, rescueFpHashBytes(p, n));
}
static void rescueFpWalk(VALUE v, RescueFp *s) {
    if (s->bad)
        return;
    if (++s->nodes > RESCUE_FP_NODES || s->depth >= RESCUE_FP_DEPTH) {
        s->bad = 1;
        return;
    }
    if (SPECIAL_CONST_P(v)) {
        if (FLONUM_P(v))
            rescueFpFloat(s, RFLOAT_VALUE(v));
        else if (STATIC_SYM_P(v))
            rescueFpMix(s, rescueFpId(s, SYM2ID(v)) ^ 5);
        else
            rescueFpMix(s, (uint64_t)v ^ 3); /* Integer, nil, true, false */
        return;
    }
    s->depth++;
    switch (BUILTIN_TYPE(v)) {
    case T_STRING:
        rescueFpMix(s, rescueFpName(rb_enc_name(rb_enc_get(v))) ^ 11);
        rescueFpBytes(s, RSTRING_PTR(v), RSTRING_LEN(v));
        break;
    case T_ARRAY: {
        const long n = RARRAY_LEN(v);
        rescueFpMix(s, (uint64_t)n ^ 13);
        for (long i = 0; i < n && !s->bad; i++)
            rescueFpWalk(RARRAY_AREF(v, i), s);
        break;
    }
    case T_HASH:
        rescueFpMix(s, (uint64_t)RHASH_SIZE(v) ^ 17);
        rb_hash_foreach(v, rescueFpPair, (VALUE)s);
        break;
    case T_OBJECT:
        rescueFpMix(s, rescueFpClass(s, rb_obj_class(v)) ^ 19);
        rb_ivar_foreach(v, rescueFpIvar, (st_data_t)s);
        break;
    case T_FLOAT:
        rescueFpFloat(s, RFLOAT_VALUE(v));
        break;
    case T_SYMBOL:
        rescueFpMix(s, rescueFpId(s, SYM2ID(v)) ^ 5);
        break;
    case T_BIGNUM: {
        VALUE text = rb_big2str(v, 16);
        rescueFpBytes(s, RSTRING_PTR(text), RSTRING_LEN(text));
        break;
    }
    case T_DATA:
        if (rb_respond_to(v, s->dump)) {
            VALUE bytes = rb_funcall(v, s->dump, 1, INT2FIX(-1));
            if (RB_TYPE_P(bytes, T_STRING)) {
                rescueFpMix(s, rescueFpClass(s, rb_obj_class(v)) ^ 29);
                rescueFpBytes(s, RSTRING_PTR(bytes), RSTRING_LEN(bytes));
                break;
            }
        }
        s->bad = 1;
        break;
    default:
        s->bad = 1;
        break;
    }
    s->depth--;
}
/* Returns the fingerprint as an Integer, or nil. */
static VALUE rescueFingerprint(VALUE obj) {
    RescueFp *s = (RescueFp *)calloc(1, sizeof(RescueFp));
    if (!s)
        return Qnil;
    s->h = 1469598103934665603ULL;
    s->dump = rb_intern("_dump");
    rescueFpWalk(obj, s);
    VALUE result = s->bad ? Qnil : ULL2NUM(s->h);
    free(s);
    return result;
}
} // namespace

RB_METHOD(rescueNativeEnabled) {
    RB_UNUSED_PARAM;
    return shState->rtData().config.crashRescue ? Qtrue : Qfalse;
}
RB_METHOD(rescueNativeFlush) {
    RB_UNUSED_PARAM;
    VALUE data, meta, json;
    rb_scan_args(argc, argv, "3", &data, &meta, &json);
    rescueQueue(RescueWriter::Write, rescueBytes(data), rescueBytes(meta), rescueBytes(json));
    return Qnil;
}
RB_METHOD(rescueNativeClear) {
    RB_UNUSED_PARAM;
    rescueQueue(RescueWriter::Clear);
    return Qnil;
}
RB_METHOD(rescueNativeFingerprint) {
    RB_UNUSED_PARAM;
    VALUE obj;
    rb_scan_args(argc, argv, "1", &obj);
    return rescueFingerprint(obj);
}

static void crashRescueGlobals(const Config &conf) {
    if (conf.gameFolder.empty() || conf.gameFolder == "app0:/stub")
        return;
    const std::string &settings = conf.vitaConfigPath;
    const size_t slash = settings.find_last_of('/');
    const std::string file = slash == std::string::npos ? settings : settings.substr(slash + 1);
    std::string name;
    if (file.size() == 21 && file.compare(16, 5, ".json") == 0 &&
        file.find_first_not_of("0123456789abcdef") == 16)
        name = file.substr(0, 16);
    else {
        unsigned long long hash = 14695981039346656037ULL;
        for (unsigned char c : conf.gameFolder) { hash ^= c; hash *= 1099511628211ULL; }
        char text[32];
        snprintf(text, sizeof(text), "folder-%016llx", hash);
        name = text;
    }
    mkdir("ux0:/data/hardrpg", 0777);
    mkdir("ux0:/data/hardrpg/rescue", 0777);
    const std::string base = "ux0:/data/hardrpg/rescue/" + name;
    rb_gv_set("$hardrpg_rescue_base", rb_utf8_str_new(base.data(), base.size()));
    rb_gv_set("$hardrpg_rescue_rgss", INT2FIX(rgssVer));
    RescueWriter &w = rescueWriter;
    if (!w.lock) {
        w.base = base;
        w.lock = SDL_CreateMutex();
        w.wake = SDL_CreateCond();
        w.idle = SDL_CreateCond();
        /* Vita threads get a small default stack; file paths go through
         * newlib's path resolver, which needs more. */
        w.thread = SDL_CreateThreadWithStackSize(rescueWriterMain, "crash rescue", 256 * 1024, nullptr);
        if (w.thread)
            SDL_DetachThread(w.thread);
    }
    VALUE native = rb_define_module("HardRPGRescueNative");
    _rb_define_module_function(native, "enabled?", rescueNativeEnabled);
    _rb_define_module_function(native, "flush", rescueNativeFlush);
    _rb_define_module_function(native, "clear", rescueNativeClear);
    _rb_define_module_function(native, "fingerprint", rescueNativeFingerprint);
}
static VALUE crashRescueSummary(VALUE exc) {
    VALUE rescue = rb_const_get(rb_cObject, rb_intern("HardRPGRescue"));
    return rb_funcall(rescue, rb_intern("report"), 1, rb_class_name(rb_obj_class(exc)));
}
static bool crashRescueFile(const std::string &path, VALUE text) {
    return RB_TYPE_P(text, RUBY_T_STRING) &&
           vitaWriteFileReplacing(path.c_str(), RSTRING_PTR(text), RSTRING_LEN(text));
}
/* A Ruby error ended the game: write the last copy (the writer may still be
 * busy with it), the launcher's file last, now with the error. Nothing here
 * needs much memory, so it also runs after NoMemoryError. */
static void crashRescueWrite(VALUE exc) {
    VALUE base = rb_gv_get("$hardrpg_rescue_base");
    if (!RB_TYPE_P(base, RUBY_T_STRING))
        return;
    rescueSettle(3000);
    VALUE data = rb_gv_get("$hardrpg_rescue_data");
    VALUE meta = rb_gv_get("$hardrpg_rescue_meta");
    if (!RB_TYPE_P(data, RUBY_T_STRING) || !RB_TYPE_P(meta, RUBY_T_STRING)) {
        vitaErrorReportFormat("\nCrash rescue: no map change or battle had been reached yet; nothing kept.\n");
        return;
    }
    if (rb_const_defined(rb_cObject, rb_intern("HardRPGRescue"))) {
        int state = 0;
        rb_protect(crashRescueSummary, exc, &state);
        rb_set_errinfo(exc);
    }
    const std::string path(RSTRING_PTR(base), RSTRING_LEN(base));
    remove((path + ".json").c_str());
    const bool ok = crashRescueFile(path + ".rescue", data) && crashRescueFile(path + ".meta", meta) &&
                    crashRescueFile(path + ".json", rb_gv_get("$hardrpg_rescue_json"));
    VALUE summary = rb_gv_get("$hardrpg_rescue_summary");
    if (ok && RB_TYPE_P(summary, RUBY_T_STRING)) {
        vitaErrorReportFormat("\nCrash rescue: ");
        vitaErrorReportAppend(RSTRING_PTR(summary), std::min<long>(RSTRING_LEN(summary), 400));
        vitaErrorReportFormat("\n");
    } else
        vitaErrorReportFormat("\nCrash rescue: %s\n", ok ? "the last map change or battle was kept." : "the last copy could not be written.");
}
#ifdef __vita__
bool hardrpgLastMessageBox(std::string &caption, std::string &text, double &secondsAgo);

/* The game quit on its own right after a Windows message box (WF-RGSS shows
 * its fatal errors that way, then calls exit(1)). The Vita could not show the
 * box, so keep its text as the error report the launcher opens next. */
static void reportMessageBoxExit(VALUE exc) {
    std::string caption, text;
    double secondsAgo = 0;
    if (!hardrpgLastMessageBox(caption, text, secondsAgo))
        return;
    int status = 0;
    if (!NIL_P(exc) && rb_obj_is_kind_of(exc, rb_eSystemExit)) {
        VALUE value = rb_funcall(exc, rb_intern("status"), 0);
        status = RB_INTEGER_TYPE_P(value) ? NUM2INT(value) : 0;
    }
    if (!hardrpg::win32text::reportMessageBoxOnExit(true, secondsAgo, status))
        return;
    vitaDiagLog("RUBY", "message_box_exit status=%d age=%.1f", status, secondsAgo);
    vitaErrorReportBegin();
    vitaErrorReportFormat("Graphics frame: %d\n\n", shState->graphics().getFrameCount());
    const std::string report = hardrpg::win32text::messageBoxReport(caption, text, status);
    vitaErrorReportAppend(report.data(), report.size());
    vitaErrorReportSave();
}
#endif

/* The game ended on its own or through Back to library: nothing to rescue. */
static void crashRescueCleanExit() {
    VALUE base = rb_gv_get("$hardrpg_rescue_base");
    if (!RB_TYPE_P(base, RUBY_T_STRING) || !rescueWriter.lock)
        return;
    rescueSettle(3000);
    rescueRemoveFiles(std::string(RSTRING_PTR(base), RSTRING_LEN(base)));
}

/* Tap-to-walk is a script that hooks the game's own player and map classes,
 * so it can only be loaded once they exist, and RGSS1 and RGSS2 offer no
 * point between loading scripts and running them. Graphics.update calls this
 * every frame until the classes are there (the first frame of the title in
 * practice), then never again. A game whose movement is not the stock tile
 * movement is left alone. */
void vitaTapToWalkTick() {
    static bool settled = false;
    static unsigned tries = 0;
    if (settled || !vitaControlProfile().tapToWalk)
        return;
    if (++tries > 1800) {
        settled = true;
        return;
    }
    int state = 0;
    VALUE ready = rb_eval_string_protect(
        "Object.const_defined?(:Game_Player) && Object.const_defined?(:Scene_Map)", &state);
    if (state) {
        rb_set_errinfo(Qnil);
        return;
    }
    if (ready != Qtrue)
        return;
    settled = true;
    VALUE problem = rb_eval_string_protect(
        "begin\n"
        "  eval(File.binread('app0:/vita_tap_to_walk.rb').force_encoding('UTF-8'),\n"
        "       TOPLEVEL_BINDING, 'vita_tap_to_walk.rb')\n"
        "  HardRPGTapWalk.install!\n"
        "  nil\n"
        "rescue Exception => e\n"
        "  \"#{e.class}: #{e.message}\"\n"
        "end\n", &state);
    if (state) {
        rb_set_errinfo(Qnil);
        fprintf(stderr, "Tap to walk could not be loaded\n");
    } else if (RB_TYPE_P(problem, RUBY_T_STRING)) {
        fprintf(stderr, "Tap to walk is off for this game: %s\n", StringValueCStr(problem));
    } else {
        fprintf(stderr, "Tap to walk is on\n");
    }
}
#endif

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

static VALUE legacySyntaxRepairHelper(VALUE arg) {
    VALUE *args = reinterpret_cast<VALUE *>(arg);
    VALUE message = rb_funcall(args[1], rb_intern("message"), 0);
    VALUE repairer = rb_const_get(rb_cObject, rb_intern("HardRPGLegacySyntax"));
    return rb_funcall(repairer, rb_intern("repair"), 2, args[0], message);
}

/* Ruby 3.1 rejects syntax the RGSS Rubies (1.8, 1.9) only warned about. A
 * SyntaxError is raised before any of the section runs, so the preloaded
 * launcher/legacy_ruby_compat.rb may rewrite the reported lines and the
 * section is evaluated again. On success the old error is cleared. */
static bool repairLegacySyntax(VALUE &string) {
    VALUE exc = rb_errinfo();
    if (!rb_obj_is_kind_of(exc, rb_eSyntaxError) ||
        !rb_const_defined(rb_cObject, rb_intern("HardRPGLegacySyntax")))
        return false;
    VALUE args[] = {string, exc};
    int state = 0;
    VALUE repaired = rb_protect(legacySyntaxRepairHelper, (VALUE)args, &state);
    if (state || !RB_TYPE_P(repaired, RUBY_T_STRING)) {
        rb_set_errinfo(exc);
        return false;
    }
    rb_set_errinfo(Qnil);
    string = repaired;
    return true;
}


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
    
#ifdef __vita__
    crashRescueGlobals(conf);
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
    
#ifdef __vita__
    ScriptCache::load(mkxp_fs::getCurrentDirectory() + "|" + scriptPack);
#endif
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
            
#ifdef __vita__
            if (!ScriptCache::eval(string, fname, i + 1 == scriptCount, state)) {
                if (i + 1 == scriptCount)
                    ScriptCache::save();
                evalString(string, fname, &state);
            }
#else
            evalString(string, fname, &state);
#endif
            if (!state)
                nativeEventsSectionLoaded(fname, string);
            for (int repairs = 0; state && repairs < 16 && repairLegacySyntax(string); ++repairs) {
                vitaDiagLog("RUBY", "rgss_legacy_syntax_repair index=%ld name=%s pass=%d",
                            i, scriptName, repairs + 1);
                evalString(string, fname, &state);
                /* Keep the repair for resets and for games that read
                 * $RGSS_SCRIPTS themselves. */
                if (!state)
                    rb_ary_store(script, 3, string);
            }
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

#ifdef __vita__
static VALUE reportIvar(VALUE object, const char *name) {
    return NIL_P(object) ? Qnil : rb_iv_get(object, name);
}
static void reportValue(const char *label, VALUE value) {
    if (NIL_P(value)) return;
    if (RB_TYPE_P(value, RUBY_T_STRING)) {
        vitaErrorReportFormat("%s=", label);
        vitaErrorReportAppend(RSTRING_PTR(value), std::min<long>(RSTRING_LEN(value), 256));
        vitaErrorReportFormat("\n");
    } else if (RB_INTEGER_TYPE_P(value)) {
        vitaErrorReportFormat("%s=%ld\n", label, NUM2LONG(value));
    } else if (RB_TYPE_P(value, RUBY_T_CLASS) || RB_TYPE_P(value, RUBY_T_MODULE)) {
        vitaErrorReportFormat("%s=%s\n", label, rb_class2name(value));
    } else if (SYMBOL_P(value)) {
        vitaErrorReportFormat("%s=%s\n", label, rb_id2name(SYM2ID(value)));
    } else {
        vitaErrorReportFormat("%s.class=%s\n", label, rb_obj_classname(value));
    }
}
static void reportFields(VALUE object, const char *fields[], size_t count) {
    for (size_t i = 0; i < count; ++i) reportValue(fields[i], reportIvar(object, fields[i]));
}
static VALUE reportGameContext(VALUE) {
    vitaErrorReportFormat("\nGame state at exception (unavailable fields omitted):\n");
    VALUE scene = rb_gv_get("$scene");
    if (rb_const_defined(rb_cObject, rb_intern("SceneManager")))
        scene = reportIvar(rb_const_get(rb_cObject, rb_intern("SceneManager")), "@scene");
    reportValue("scene", scene);
    const char *mapFields[] = {"@map_id"};
    reportFields(rb_gv_get("$game_map"), mapFields, 1);
    const char *playerFields[] = {"@x", "@y", "@direction"};
    reportFields(rb_gv_get("$game_player"), playerFields, 3);
    const char *troopFields[] = {"@troop_id", "@turn_count"};
    reportFields(rb_gv_get("$game_troop"), troopFields, 2);
    const char *systemFields[] = {"@save_count", "@battle_count"};
    reportFields(rb_gv_get("$game_system"), systemFields, 2);
    VALUE subject = reportIvar(scene, "@subject");
    reportValue("subject", subject);
    const char *actorFields[] = {"@actor_id", "@enemy_id", "@class_id", "@level", "@hp", "@mp"};
    reportFields(subject, actorFields, 6);
    VALUE equips = reportIvar(subject, "@equips");
    const char *itemFields[] = {"@class", "@item_id"};
    if (RB_TYPE_P(equips, RUBY_T_ARRAY))
        for (long i = 0; i < RARRAY_LEN(equips) && i < 16; ++i) {
            vitaErrorReportFormat("equipment[%ld]:\n", i);
            reportFields(rb_ary_entry(equips, i), itemFields, 2);
        }
    VALUE actions = reportIvar(subject, "@actions");
    if (RB_TYPE_P(actions, RUBY_T_ARRAY))
        for (long i = 0; i < RARRAY_LEN(actions) && i < 8; ++i) {
            VALUE item = reportIvar(rb_ary_entry(actions, i), "@item");
            vitaErrorReportFormat("action[%ld]:\n", i);
            reportFields(item, itemFields, 2);
        }
    VALUE spriteset = reportIvar(scene, "@spriteset");
    const char *spriteLists[] = {"@actor_sprites", "@enemy_sprites"};
    const char *animationFields[] = {"@id", "@name", "@animation1_name", "@animation2_name", "@frame_max"};
    for (const char *list : spriteLists) {
        VALUE sprites = reportIvar(spriteset, list);
        if (!RB_TYPE_P(sprites, RUBY_T_ARRAY)) continue;
        for (long i = 0; i < RARRAY_LEN(sprites) && i < 32; ++i) {
            VALUE sprite = rb_ary_entry(sprites, i);
            VALUE animation = reportIvar(sprite, "@animation");
            if (NIL_P(animation)) continue;
            vitaErrorReportFormat("%s[%ld] animation:\n", list, i);
            reportFields(animation, animationFields, 5);
            const char *spriteFields[] = {"@ani_duration", "@animation_mirror"};
            reportFields(sprite, spriteFields, 2);
        }
    }
    if (rb_const_defined(rb_cObject, rb_intern("Cache"))) {
        VALUE cache = reportIvar(rb_const_get(rb_cObject, rb_intern("Cache")), "@cache");
        if (RB_TYPE_P(cache, RUBY_T_HASH)) vitaErrorReportFormat("Cache entries=%ld\n", RHASH_SIZE(cache));
    }
    VALUE gc = rb_const_get(rb_cObject, rb_intern("GC"));
    const char *stats[] = {"count", "heap_live_slots", "heap_free_slots", "total_allocated_objects", "malloc_increase_bytes", "oldmalloc_increase_bytes"};
    for (const char *key : stats) {
        VALUE value = rb_funcall(gc, rb_intern("stat"), 1, ID2SYM(rb_intern(key)));
        reportValue(key, value);
    }
    return Qnil;
}
struct ErrorCapture { VALUE exc; const BacktraceData *backtrace; };
static VALUE reportException(VALUE argument) {
    const ErrorCapture &capture = *reinterpret_cast<ErrorCapture *>(argument);
    VALUE exc = capture.exc;
    vitaErrorReportFormat("Exception: %s\n\n", rb_obj_classname(exc));
    VALUE message = rb_funcall2(exc, rb_intern("message"), 0, NULL);
    if (RB_TYPE_P(message, RUBY_T_STRING))
        vitaErrorReportAppend(RSTRING_PTR(message), std::min<long>(RSTRING_LEN(message), 8192));
    vitaErrorReportFormat("\n\nBacktrace:\n");
    VALUE trace = rb_funcall2(exc, rb_intern("backtrace"), 0, NULL);
    if (RB_TYPE_P(trace, RUBY_T_ARRAY)) {
        size_t traceBytes = 0;
        for (long i = 0; i < RARRAY_LEN(trace) && i < 32; ++i) {
            VALUE entry = rb_ary_entry(trace, i);
            if (!RB_TYPE_P(entry, RUBY_T_STRING)) continue;
            const char *line = RSTRING_PTR(entry);
            const size_t size = std::min<long>(RSTRING_LEN(entry), 1024);
            const char *colon = static_cast<const char *>(memchr(line, ':', size));
            const std::string *mapped = nullptr;
            if (colon) {
                const size_t prefix = colon - line;
                for (auto it = capture.backtrace->scriptNames.cbegin(); it != capture.backtrace->scriptNames.cend(); ++it)
                    if (it->first.size() == prefix && !memcmp(it->first.data(), line, prefix)) { mapped = &it->second; break; }
            }
            // Reserve space for game context even with unusually long traces.
            const size_t outputSize = mapped ? std::min<size_t>(mapped->size(), 512) + size - (colon - line) : size;
            if (traceBytes + outputSize + 1 > 8192) {
                vitaErrorReportFormat("[Backtrace truncated]\n");
                break;
            }
            traceBytes += outputSize + 1;
            if (mapped) {
                vitaErrorReportAppend(mapped->data(), std::min<size_t>(mapped->size(), 512));
                vitaErrorReportAppend(colon, size - (colon - line));
            } else vitaErrorReportAppend(line, size);
            vitaErrorReportFormat("\n");
        }
    } else vitaErrorReportFormat("No backtrace supplied.\n");
    return Qnil;
}
#endif

static void showExc(VALUE exc, const BacktraceData &btData) {
#ifdef __vita__
    vitaErrorReportBegin();
    vitaErrorReportFormat("Graphics frame: %d\n", shState->graphics().getFrameCount());
    ErrorCapture capture = {exc, &btData};
    int state = 0;
    rb_protect(reportException, reinterpret_cast<VALUE>(&capture), &state);
    if (state) vitaErrorReportFormat("\nException details could not be fully read.\n");
    rb_set_errinfo(exc);
    state = 0;
    rb_protect(reportGameContext, Qnil, &state);
    if (state) vitaErrorReportFormat("\nAdditional game context unavailable (Ruby raised while collecting it).\n");
    rb_set_errinfo(exc);
    crashRescueWrite(exc);
    vitaErrorReportSave();
    RB_GC_GUARD(exc);
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
        std::string fallback(1280, '\0');
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
#ifdef __vita__
    vitaErrorSessionStart(conf.gameFolder.c_str(), conf.rgssVersion);
#endif
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
#ifdef MKXPZ_VITA_DIAGNOSTICS
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
#endif
    if (!NIL_P(exc) && !rb_obj_is_kind_of(exc, rb_eSystemExit))
        showExc(exc, btData);
#ifdef __vita__
    else {
        reportMessageBoxExit(exc);
        crashRescueCleanExit();
    }
#endif

    ruby_cleanup(0);
    vitaDiagLog("RUBY", "ruby_cleanup_ok");
    
    shState->rtData().rqTermAck.set();
}

static void mriBindingTerminate() { throw Exception(Exception::SystemExit, " "); }

static void mriBindingReset() { throw Exception(Exception::Reset, " "); }
