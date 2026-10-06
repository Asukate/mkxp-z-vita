/*
** main.cpp
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

#ifndef MKXPZ_BUILD_XCODE
#include "icon.png.xxd"
#endif

#include <alc.h>

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_sound.h>
#include <SDL_ttf.h>

#include <assert.h>
#include <string.h>
#include <string>
#include <unistd.h>
#include <regex>

#include "binding.h"
#include "sharedstate.h"
#include "eventthread.h"
#include "util/debugwriter.h"
#include "util/exception.h"
#include "display/gl/gl-debug.h"
#include "display/gl/gl-fun.h"

#include "vita_diagnostic.h"
#ifdef MKXPZ_NATIVE_LAUNCHER
#include "native_launcher/runtime.h"
#endif
#include "vita_uid_auditor_api.h"
#ifdef __vita__
#include "vita_startup_timer.h"
#include "vita_livearea.h"
#include "vita_error_report.h"
#include "vita_runtime_log.h"
#endif
#if defined(__vita__) && defined(MKXPZ_PROTO_LAUNCHER)
#include <cstdio>
#include <psp2/appmgr.h>
#endif

#include "filesystem/filesystem.h"

#include "system/system.h"

#ifdef __vita__
extern "C" void glFinish(void);
extern "C" int vglInitWithCustomThreshold(int pool_size, int width, int height,
                                          int ram_threshold,
                                          int cdram_threshold,
                                          int phycont_threshold,
                                          int cdlg_threshold, int msaa);
#endif

#if defined(__WIN32__)
#include "resource.h"
#include <Winsock2.h>
#include "util/win-consoleutils.h"

// Try to work around buggy GL drivers that tend to be in Optimus laptops
// by forcing MKXP to use the dedicated card instead of the integrated one
#include <windows.h>
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

#ifdef MKXPZ_STEAM
#include "steamshim_child.h"
#endif

#ifdef MKXPZ_BUILD_XCODE
#include <Availability.h>
#include "TouchBar.h"
#if !defined(__MAC_10_15) || __MAC_OS_X_VERSION_MAX_ALLOWED < __MAC_10_15
#define MKXPZ_INIT_GL_LATER
#endif
#endif

#ifndef MKXPZ_INIT_GL_LATER
#define GLINIT_SHOWERROR(s) showInitError(s)
#else
#define GLINIT_SHOWERROR(s) rgssThreadError(threadData, s)
#endif

static void rgssThreadError(RGSSThreadData *rtData, const std::string &msg);
static void showInitError(const std::string &msg);

static inline const char *glGetStringInt(GLenum name) {
  const char *value = (const char *)gl.GetString(name);
  return value ? value : "<null>";
}

static void printGLInfo() {
    const std::string renderer(glGetStringInt(GL_RENDERER));
    const std::string version(glGetStringInt(GL_VERSION));
    std::regex rgx("ANGLE \\((.+), ANGLE Metal Renderer: (.+), Version (.+)\\)");
        
    std::smatch matches;
    if (std::regex_search(renderer, matches, rgx)) {
        
        Debug() << "Backend           :" << "Metal";
        Debug() << "Metal Device      :" << matches[2] << "(" + matches[1].str() + ")";
        Debug() << "Renderer Version  :" << matches[3].str();
        vitaDiagLog("GL", "backend=Metal device=%s renderer=%s version=%s",
                    matches[2].str().c_str(), matches[1].str().c_str(),
                    matches[3].str().c_str());
        
    std::smatch vmatches;
        if (std::regex_search(version, vmatches, std::regex("\\(ANGLE (.+) git hash: .+\\)"))) {
            Debug() << "ANGLE Version     :" << vmatches[1].str();
        }
        return;
    }
    
  Debug() << "Backend      :" << "OpenGL";
  Debug() << "GL Vendor    :" << glGetStringInt(GL_VENDOR);
  Debug() << "GL Renderer  :" << renderer;
  Debug() << "GL Version   :" << version;
  Debug() << "GLSL Version :" << glGetStringInt(GL_SHADING_LANGUAGE_VERSION);
  vitaDiagLog("GL", "backend=OpenGL vendor=%s renderer=%s version=%s glsl=%s",
              glGetStringInt(GL_VENDOR), renderer.c_str(), version.c_str(),
              glGetStringInt(GL_SHADING_LANGUAGE_VERSION));
}

static SDL_GLContext initGL(SDL_Window *win, Config &conf,
                            RGSSThreadData *threadData);

int rgssThreadFun(void *userdata) {
  RGSSThreadData *threadData = static_cast<RGSSThreadData *>(userdata);

#ifdef __vita__
  vitaStartupTimerMark("rgss_thread_start");
#endif
  vitaDiagLogThread("rgss_thread_start");
  vitaDiagLog("BOOTPERF", "rgss_thread_start");
  ba_snapshot("M07_rgss_thread_start");

#ifdef MKXPZ_INIT_GL_LATER
  threadData->glContext =
      initGL(threadData->window, threadData->config, threadData);
  if (!threadData->glContext)
    return 0;
#else
  SDL_GL_MakeCurrent(threadData->window, threadData->glContext);
#endif
  vitaDiagLog("GL", "rgss_context_current");
  vitaDiagLog("BOOTPERF", "gl_context_current");
#ifdef __vita__
  vitaStartupTimerMark("gl_context_current");
#endif

  /* Setup AL context */
  ALCcontext *alcCtx = alcCreateContext(threadData->alcDev, 0);

  if (!alcCtx) {
    rgssThreadError(threadData, "Error creating OpenAL context");
    return 0;
  }

  alcMakeContextCurrent(alcCtx);
  vitaDiagLog("AUDIO", "openal_context_ready");
  vitaDiagLog("BOOTPERF", "openal_context_ready");
#ifdef __vita__
  vitaStartupTimerMark("openal_context_ready");
#endif
  ba_snapshot("M08_openal_context_ready");

  try {
#ifdef __vita__
    vitaStartupTimerMark("shared_state_begin");
#endif
    SharedState::initInstance(threadData);
    vitaDiagLogThread("shared_state_ready");
    vitaDiagLog("BOOTPERF", "shared_state_ready");
    ba_snapshot("M09_shared_state_ready");
#ifdef __vita__
    vitaStartupTimerMark("shared_state_ready");
#endif
  } catch (const Exception &exc) {
    rgssThreadError(threadData, exc.msg);
    alcDestroyContext(alcCtx);

    return 0;
  }

  /* Start script execution */
  vitaDiagLog("RUBY", "script_binding_execute_begin");
  vitaDiagLog("BOOTPERF", "script_execute_begin");
#ifdef __vita__
  vitaStartupTimerMark("script_execute_begin");
#endif
  ba_script_begin();
  scriptBinding->execute();
  vitaDiagLog("RUBY", "script_binding_execute_return");
  ba_dump_final("script_return");

  threadData->rqTermAck.set();
  threadData->ethread->requestTerminate();

  SharedState::finiInstance();

#ifdef __vita__
  /* vitaGL has no context-destruction entry point.  Finish the final scene
   * and wait for GXM after all RGSS GL resources have been released, before
   * the process returns control to LiveArea. */
  vitaDiagLog("GL", "shutdown_finish_begin");
  glFinish();
  vitaDiagLog("GL", "shutdown_finish_done");
#endif

  alcDestroyContext(alcCtx);

  return 0;
}

static void printRgssVersion(int ver) {
  const char *const makers[] = {"", "XP", "VX", "VX Ace"};

  char buf[128];
  snprintf(buf, sizeof(buf), "RGSS version %d (RPG Maker %s)", ver,
           makers[ver]);

  Debug() << buf;
  vitaDiagLog("CONFIG", "%s", buf);
}

static void rgssThreadError(RGSSThreadData *rtData, const std::string &msg) {
  vitaDiagLog("ERROR", "%s", msg.c_str());
  rtData->rgssErrorMsg = msg;
  rtData->ethread->requestTerminate();
  rtData->rqTermAck.set();
}

static void showInitError(const std::string &msg) {
#ifdef __vita__
  vitaWriteErrorReport("Startup error\n\n" + msg);
#endif
  vitaDiagLog("ERROR", "%s", msg.c_str());
  Debug() << msg;
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "mkxp-z", msg.c_str(), 0);
}

static void setupWindowIcon(const Config &conf, SDL_Window *win) {
  SDL_RWops *iconSrc;

  if (conf.iconPath.empty())
#ifndef MKXPZ_BUILD_XCODE
    iconSrc = SDL_RWFromConstMem(___assets_icon_png, ___assets_icon_png_len);
#else
    iconSrc = SDL_RWFromFile(mkxp_fs::getPathForAsset("icon", "png").c_str(), "rb");
#endif
  else
    iconSrc = SDL_RWFromFile(conf.iconPath.c_str(), "rb");

  SDL_Surface *iconImg = IMG_Load_RW(iconSrc, SDL_TRUE);

  if (iconImg) {
    SDL_SetWindowIcon(win, iconImg);
    SDL_FreeSurface(iconImg);
  }
}

int main(int argc, char *argv[]) {
#ifdef __vita__
    /*
     * LiveArea links (for example psla:-config) relaunch eboot.bin with a
     * boot event.  Dispatch the companion before SDL/vitaGL/Ruby startup.
     */
    if (vitaHandleLiveAreaLaunch())
        return 0;
    vitaStartupTimerInit();
    vitaRuntimeLogInit();
    fprintf(stderr, "HardRPG runtime %s started\n", MKXPZ_GIT_HASH);
#endif
    vitaDiagInit(argc > 0 ? argv[0] : "mkxp-z");
    vitaDiagLog("MAIN", "startup argc=%d", argc);
    vitaDiagLog("BOOTPERF", "process_entry");
    ba_snapshot("M00_diag_ready");

    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
    SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");

#ifdef GLES2_HEADER
    SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "1");
#endif

    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");

    /* initialize SDL first */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER | SDL_INIT_EVENTS) < 0) {
      showInitError(std::string("Error initializing SDL: ") + SDL_GetError());
      return 0;
    }
    vitaDiagLog("SDL", "SDL_Init_ok");
    vitaDiagLog("BOOTPERF", "sdl_ready");
    ba_snapshot("M01_sdl_ready");
#ifdef __vita__
    vitaStartupTimerMark("sdl_ready");
#endif

    if (!EventThread::allocUserEvents()) {
      showInitError("Error allocating SDL user events");
      return 0;
    }

#ifdef MKXPZ_NATIVE_LAUNCHER
    if (nativeLauncherRequested(argc, argv)) {
        bool returning = false;
        for (int i = 1; i < argc; ++i)
            returning = returning || !strcmp(argv[i], "--hardrpg-return") ||
                        !strcmp(argv[i], "--hardrpg-play");
        int rc = runNativeLauncher(returning);
        SDL_Quit();
        vitaDiagShutdown();
        vitaStartupTimerShutdown();
        return rc;
    }
#endif

#ifndef WORKDIR_CURRENT
    char dataDir[512]{};
#if defined(__linux__)
    char *tmp{};
    tmp = getenv("SRCDIR");
    if (tmp) {
      strncpy(dataDir, tmp, sizeof(dataDir));
    }
#endif
    if (!dataDir[0]) {
        strncpy(dataDir, mkxp_fs::getDefaultGameRoot().c_str(), sizeof(dataDir));
    }
    mkxp_fs::setCurrentDirectory(dataDir);
#endif
    
    /* now we load the config */
    vitaDiagLog("BOOTPERF", "config_begin");
    Config conf;
#ifdef MKXPZ_NATIVE_LAUNCHER
    std::string configError;
    try { conf.read(argc, argv); }
    catch (const Exception &e) { configError = e.msg.c_str(); }
    catch (const std::exception &e) { configError = e.what(); }
    if (!configError.empty()) {
        vitaWriteErrorReport("Unable to start game\n" + configError);
        // Configuration can fail before a game GL context exists. The native
        // menu can show that error directly, without starting Ruby.
        int rc = runNativeLauncher(true);
        SDL_Quit();
        vitaDiagShutdown();
        vitaStartupTimerShutdown();
        return rc;
    }
#else
    conf.read(argc, argv);
#endif
    vitaDiagLog("BOOTPERF", "config_ready rgss=%d", conf.rgssVersion);
#ifdef __vita__
    vitaStartupTimerMark("config_ready");
#endif
    vitaDiagLog("CONFIG", "exec=%s game_folder=%s screen=%dx%d current=%s",
                conf.execName.c_str(), conf.gameFolder.c_str(), conf.defScreenW,
                conf.defScreenH, mkxp_fs::getCurrentDirectory().c_str());

#if defined(__WIN32__)
    // Create a debug console in debug mode
    if (conf.winConsole) {
      if (setupWindowsConsole()) {
        reopenWindowsStreams();
      } else {
        char buf[200];
        snprintf(buf, sizeof(buf), "Error allocating console: %lu",
                GetLastError());
        showInitError(std::string(buf));
      }
    }
#endif

#ifdef MKXPZ_STEAM
    if (!STEAMSHIM_init()) {
      showInitError("Failed to initialize Steamworks. The application cannot "
                    "continue launching.");
      SDL_Quit();
      return 0;
    }
#endif

    if (conf.windowTitle.empty())
      conf.windowTitle = conf.game.title;

    assert(conf.rgssVersion >= 1 && conf.rgssVersion <= 3);
    printRgssVersion(conf.rgssVersion);

    int imgFlags = IMG_INIT_PNG | IMG_INIT_JPG;
    if (IMG_Init(imgFlags) != imgFlags) {
      showInitError(std::string("Error initializing SDL_image: ") +
                    SDL_GetError());
      SDL_Quit();

#ifdef MKXPZ_STEAM
      STEAMSHIM_deinit();
#endif

      return 0;
    }

    if (TTF_Init() < 0) {
      showInitError(std::string("Error initializing SDL_ttf: ") +
                    SDL_GetError());
      IMG_Quit();
      SDL_Quit();

#ifdef MKXPZ_STEAM
      STEAMSHIM_deinit();
#endif

      return 0;
    }

    vitaDiagLog("BOOTPERF", "support_libs_begin");
    if (Sound_Init() == 0) {
      showInitError(std::string("Error initializing SDL_sound: ") +
                    Sound_GetError());
      TTF_Quit();
      IMG_Quit();
      SDL_Quit();

#ifdef MKXPZ_STEAM
      STEAMSHIM_deinit();
#endif

      return 0;
    }
    vitaDiagLog("BOOTPERF", "support_libs_ready");
#if defined(__WIN32__)
    WSAData wsadata = {0};
    if (WSAStartup(0x101, &wsadata) || wsadata.wVersion != 0x101) {
      char buf[200];
      snprintf(buf, sizeof(buf), "Error initializing winsock: %08X",
               WSAGetLastError());
      showInitError(
          std::string(buf)); // Not an error worth ending the program over
    }
#endif

    SDL_Window *win;
    Uint32 winFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_ALLOW_HIGHDPI;

    if (conf.winResizable)
      winFlags |= SDL_WINDOW_RESIZABLE;
    if (conf.fullscreen)
      winFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    
#ifdef GLES2_HEADER
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    // LoadLibrary properly initializes EGL, it won't work otherwise.
    // Doesn't completely do it though, needs a small patch to SDL
#ifdef MKXPZ_BUILD_XCODE
    SDL_setenv("ANGLE_DEFAULT_PLATFORM", (conf.preferMetalRenderer) ? "metal" : "opengl", true);
    SDL_GL_LoadLibrary("@rpath/libEGL.dylib");
#endif
#endif
    
    vitaDiagLog("BOOTPERF", "window_begin");
    win = SDL_CreateWindow(conf.windowTitle.c_str(), SDL_WINDOWPOS_UNDEFINED,
                           SDL_WINDOWPOS_UNDEFINED, conf.defScreenW,
                           conf.defScreenH, winFlags);

    if (!win) {
      showInitError(std::string("Error creating window: ") + SDL_GetError());

#ifdef MKXPZ_STEAM
      STEAMSHIM_deinit();
#endif
      return 0;
    }
    vitaDiagLog("WINDOW", "created requested=%dx%d fullscreen=%d",
                conf.defScreenW, conf.defScreenH, conf.fullscreen ? 1 : 0);
    vitaDiagLog("BOOTPERF", "window_ready");
#ifdef __vita__
    vitaStartupTimerMark("window_ready");
#endif
    
#ifdef MKXPZ_BUILD_XCODE
    {
        std::string downloadsPath = "/Users/" + mkxp_sys::getUserName() + "/Downloads";
        
        if (mkxp_fs::getCurrentDirectory().find(downloadsPath) == 0) {
            showInitError(conf.game.title +
                          " cannot run from the Downloads directory.\n\n" +
                          "Please move the application to the Applications folder (or anywhere else) " +
                          "and try again.");
#ifdef MKXPZ_STEAM
            STEAMSHIM_deinit();
#endif
            return 0;
        }
    }
#endif
    
#if defined(MKXPZ_BUILD_XCODE)
#define DEBUG_FSELECT_MSG "Select the folder from which to load game files. This is the folder containing the game's INI."
#define DEBUG_FSELECT_PROMPT "Load Game"
    if (conf.manualFolderSelect) {
        std::string dataDirStr = mkxp_fs::selectPath(win, DEBUG_FSELECT_MSG, DEBUG_FSELECT_PROMPT);
        if (!dataDirStr.empty()) {
            conf.gameFolder = dataDirStr;
            mkxp_fs::setCurrentDirectory(dataDirStr.c_str());
            Debug() << "Current directory set to" << dataDirStr;
            conf.read(argc, argv);
            conf.readGameINI();
        }
    }
#endif

    /* OSX and Windows have their own native ways of
     * dealing with icons; don't interfere with them */
#ifdef __LINUX__
    setupWindowIcon(conf, win);
#else
    (void)setupWindowIcon;
#endif

    vitaDiagPollTrigger();
    vitaDiagLog("BOOTPERF", "openal_device_begin");
    vitaDiagLog("AUDIO", "openal_device_begin");
    ALCdevice *alcDev = alcOpenDevice(0);
    vitaDiagLog("AUDIO", "openal_device_returned ptr=%p", (void *)alcDev);

    if (!alcDev) {
#ifdef __vita__
      /* DIAG: on Vita a missing audio device is not fatal — continue
       * without audio (the stream path is under suspicion for the
       * memory corruption). */
      vitaDiagLog("AUDIO", "device_missing_continuing_without_audio");
#else
      showInitError("Could not detect an available audio device.");
      SDL_DestroyWindow(win);
      TTF_Quit();
      IMG_Quit();
      SDL_Quit();

#ifdef MKXPZ_STEAM
      STEAMSHIM_deinit();
#endif
      return 0;
#endif
    }
    vitaDiagLog("AUDIO", "openal_device_ready");
    vitaDiagLog("BOOTPERF", "openal_device_ready");
#ifdef __vita__
    vitaStartupTimerMark("audio_ready");
#endif

    vitaDiagLog("DISPLAY", "mode_query_begin");
    SDL_DisplayMode mode{};
    int modeResult = SDL_GetDisplayMode(0, 0, &mode);
    vitaDiagLog("DISPLAY", "mode_query_returned rc=%d refresh=%d",
                modeResult, mode.refresh_rate);

    /* Can't sync to display refresh rate if its value is unknown */
    if (!mode.refresh_rate)
      conf.syncToRefreshrate = false;

    vitaDiagLog("DEBUG-EVENT", "event_thread_constructor_begin");
    EventThread eventThread;
#if !defined(MKXPZ_SKIP_POST_EVENT_LOG)
    vitaDiagLog("THREAD", "event_thread_constructed");
#endif

#ifndef MKXPZ_INIT_GL_LATER
    SDL_GLContext glCtx = initGL(win, conf, 0);
#else
    SDL_GLContext glCtx = NULL;
#endif

    RGSSThreadData rtData(&eventThread, argv[0], win, alcDev, mode.refresh_rate,
                          mkxp_sys::getScalingFactor(), conf, glCtx);

    int winW, winH, drwW, drwH;
    SDL_GetWindowSize(win, &winW, &winH);
    rtData.windowSizeMsg.post(Vec2i(winW, winH));
    
    SDL_GL_GetDrawableSize(win, &drwW, &drwH);
    rtData.drawableSizeMsg.post(Vec2i(drwW, drwH));

    /* Load and post key bindings */
    rtData.bindingUpdateMsg.post(loadBindings(conf));
    
#ifdef MKXPZ_BUILD_XCODE
    // Create Touch Bar
    initTouchBar(win, conf);
#endif

    /* The old Vita port used 5 MiB for RGSS. Keep that as a diagnostic-only
     * candidate while the normal build remains at the smaller baseline. */
#ifdef MKXPZ_VITA_DIAGNOSTICS
    constexpr int rgssStackSize = 5 * 1024 * 1024;
#else
    constexpr int rgssStackSize = 4 * 1024 * 1024;
#endif
    vitaDiagPollTrigger();
    vitaDiagLog("THREAD", "creating_rgss_thread stack=%d", rgssStackSize);
    SDL_Thread *rgssThread = SDL_CreateThreadWithStackSize(rgssThreadFun, "rgss", rgssStackSize, &rtData);
    vitaDiagLog("THREAD", "rgss_thread_created ptr=%p", (void *)rgssThread);
    vitaDiagLog("BOOTPERF", "rgss_thread_created");
#ifdef __vita__
    vitaStartupTimerMark("rgss_thread_created");
#endif

    /* Start event processing */
    eventThread.process(rtData);

    /* Request RGSS thread to stop */
    rtData.rqTerm.set();

    /* Wait for RGSS thread response */
    for (int i = 0; i < 1000; ++i) {
      /* We can stop waiting when the request was ack'd */
      if (rtData.rqTermAck) {
        Debug() << "RGSS thread ack'd request after" << i * 10 << "ms";
        break;
      }

      /* Give RGSS thread some time to respond */
      SDL_Delay(10);
    }

    /* If RGSS thread ack'd request, wait for it to shutdown,
     * otherwise abandon hope and just end the process as is. */
    if (rtData.rqTermAck)
      SDL_WaitThread(rgssThread, 0);
    else {
#ifdef __vita__
      vitaWriteErrorReport("The game stopped responding and had to close.\n"
                           "A backtrace could not be retrieved from its active script thread.");
#endif
      SDL_ShowSimpleMessageBox(
          SDL_MESSAGEBOX_ERROR, conf.game.title.c_str(),
          std::string("The RGSS script seems to be stuck. "+conf.game.title+" will now force quit.").c_str(),
          win);
    }

    if (!rtData.rgssErrorMsg.empty()) {
#ifdef __vita__
      vitaWriteErrorReport("Engine error\n\n" + rtData.rgssErrorMsg);
#endif
      Debug() << rtData.rgssErrorMsg;
      vitaDiagLog("ERROR", "rgss_shutdown_error: %s", rtData.rgssErrorMsg.c_str());
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, conf.game.title.c_str(),
                               rtData.rgssErrorMsg.c_str(), win);
    }
    else {
      vitaDiagLog("MAIN", "clean shutdown, no error");
    }

    if (rtData.glContext)
      SDL_GL_DeleteContext(rtData.glContext);

    /* Clean up any remainin events */
    eventThread.cleanup();

    Debug() << "Shutting down.";

    alcCloseDevice(alcDev);
    SDL_DestroyWindow(win);

#if defined(__WIN32__)
    if (wsadata.wVersion)
      WSACleanup();
#endif

#ifdef MKXPZ_STEAM
    STEAMSHIM_deinit();
#endif
#if defined(__vita__) && defined(MKXPZ_PROTO_LAUNCHER)
    /* The picker hands off its selection; completed games return to the
     * picker. Restart only after the RGSS thread has acknowledged shutdown.
     * A picker exit without a selection returns to LiveArea. */
    {
        FILE *protoPick = std::fopen("ux0:/data/hardrpg/selection.json", "rb");
        const bool playingGame = conf.gameFolder != "app0:/stub";
        if (rtData.rqTermAck && (protoPick || playingGame || eventThread.returnToLauncherRequested())) {
            if (protoPick)
                std::fclose(protoPick);
            if (eventThread.returnToLauncherRequested())
                std::remove("ux0:/data/hardrpg/selection.json");
            // An internal return must not replay the cold-start splash.
            char returnArg[] = "--hardrpg-return";
            char *returnArgs[] = {returnArg, nullptr};
            const bool returningToPicker = playingGame || eventThread.returnToLauncherRequested();
            sceAppMgrLoadExec("app0:/eboot.bin", returningToPicker ? returnArgs : nullptr, nullptr);
        }
        else if (protoPick)
            std::fclose(protoPick);
    }
#endif
    Sound_Quit();
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();

    vitaDiagShutdown();
#ifdef __vita__
    vitaStartupTimerShutdown();
#endif

    return 0;
}

static SDL_GLContext initGL(SDL_Window *win, Config &conf,
                            RGSSThreadData *threadData) {
  SDL_GLContext glCtx{};

  /* Setup GL context. Must be done in main thread since macOS 10.15 */
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    
  if (conf.debugMode)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);

  vitaDiagLog("GL", "context_create_begin");
  vitaDiagLog("BOOTPERF", "gl_init_begin");
#ifdef __vita__
  /* Generic Vita memory fix: pre-init vitaGL with MSAA disabled before
   * SDL's fallback init runs (that path forces 4X MSAA, which
   * quadruples every display surface and drains the shared VRAM/RAM pools
   * until textures can no longer allocate). vitaGL suppresses SDL's later
   * init call, so this single pre-init governs all games. Sprite-edge
   * pixels are NEAREST-filtered 2D art; MSAA buys nothing visible here.
   * Vertex arrays also need no legacy glBegin/glEnd pool; reserving one
   * repeatedly overflows temporary storage and forces allocation waits. */
  {
    vitaDiagLog("GL", "vgl_preinit_begin msaa=none ram64 phycont1m");
    vglInitWithCustomThreshold(0, 960, 544,
                               28 * 1024 * 1024, 0, 25 * 1024 * 1024,
                               0x8C6000, 0);
    vitaDiagLog("GL", "vgl_preinit_done");
    vitaDiagLog("BOOTPERF", "vgl_preinit_done");
  }
#endif
  glCtx = SDL_GL_CreateContext(win);

  if (!glCtx) {
    GLINIT_SHOWERROR(std::string("Could not create OpenGL context: ") + SDL_GetError());
    return 0;
  }
  vitaDiagLog("GL", "context_created");
  vitaDiagLog("BOOTPERF", "sdl_gl_context_ready");

  try {
    initGLFunctions();
  } catch (const Exception &exc) {
    GLINIT_SHOWERROR(exc.msg);
    SDL_GL_DeleteContext(glCtx);

    return 0;
  }
  vitaDiagLog("GL", "function_table_ready");
  vitaDiagLog("BOOTPERF", "gl_functions_ready");

  if (!conf.enableBlitting)
    gl.BlitFramebuffer = 0;

  gl.ClearColor(0, 0, 0, 1);
  gl.Clear(GL_COLOR_BUFFER_BIT);
  SDL_GL_SwapWindow(win);
  vitaDiagLog("FRAME", "initial_clear_swap_complete");
  vitaDiagLog("BOOTPERF", "gl_initial_swap_done");

  printGLInfo();

  bool vsync = conf.vsync || conf.syncToRefreshrate;
  SDL_GL_SetSwapInterval(vsync ? 1 : 0);
  vitaDiagLog("BOOTPERF", "gl_init_ready");

  // GLDebugLogger dLogger;
  return glCtx;
}
