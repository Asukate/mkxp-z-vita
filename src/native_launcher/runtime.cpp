#include "runtime.h"
#include "text_input.h"
#include "view.h"
#include "vita_proto_launcher.h"
#ifdef __vita__
#include <vitaGL.h>
#else
#include <GLES2/gl2.h>
#endif
#include <SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#ifdef __vita__
#include <psp2/appmgr.h>
#include <psp2/common_dialog.h>
#include <psp2/ctrl.h>
#include <psp2/gxm.h>
#include <psp2/ime_dialog.h>
#include <psp2/sysmodule.h>
#include <psp2/touch.h>

namespace {
bool nativeKeyboardActive = false;
}

// The pinned vitaGL swap chain composites common dialogs into the displayed
// front buffer, but queues the back buffer next. Use the queued target while
// the native launcher keyboard owns presentation. Other dialogs are unchanged.
// These symbols belong to the pinned vitaGL GXM backend, not its public API.
extern "C" {
extern unsigned int gxm_back_buffer_index;
extern void *gxm_color_surfaces_addr[];
extern SceGxmSyncObject *gxm_sync_objects[];
int __real_sceCommonDialogUpdate(const SceCommonDialogUpdateParam *param);
int __wrap_sceCommonDialogUpdate(const SceCommonDialogUpdateParam *param) {
  if (!nativeKeyboardActive || !param)
    return __real_sceCommonDialogUpdate(param);
  SceCommonDialogUpdateParam queued = *param;
  queued.renderTarget.colorSurfaceData =
      gxm_color_surfaces_addr[gxm_back_buffer_index];
  queued.displaySyncObject = gxm_sync_objects[gxm_back_buffer_index];
  return __real_sceCommonDialogUpdate(&queued);
}
}
#endif

namespace {
using namespace hardrpg;
class Presenter {
  SDL_Window *window = nullptr;
  SDL_GLContext context = nullptr;
  GLuint program = 0, texture = 0, buffer = 0;
  GLint canvasLocation = -1;
  GLuint shader(GLenum type, const char *source) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &source, nullptr);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
      char message[1024]{};
      glGetShaderInfoLog(s, sizeof(message), nullptr, message);
      glDeleteShader(s);
      throw std::runtime_error(std::string("Launcher shader: ") + message);
    }
    return s;
  }

public:
  Presenter() {
    try {
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                          SDL_GL_CONTEXT_PROFILE_ES);
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
      window = SDL_CreateWindow(
          "HardRPG", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 544,
          SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP);
      if (!window)
        throw std::runtime_error(SDL_GetError());
      context = SDL_GL_CreateContext(window);
      if (!context)
        throw std::runtime_error(SDL_GetError());
      SDL_GL_SetSwapInterval(1);
      GLuint vertex = shader(
          GL_VERTEX_SHADER,
          "attribute vec2 position; attribute vec2 texcoord; varying vec2 uv; "
          "void main(){uv=texcoord;gl_Position=vec4(position,0.0,1.0);}");
      GLuint fragment = 0;
      try {
        fragment = shader(
            GL_FRAGMENT_SHADER,
            "precision mediump float; varying vec2 uv; uniform sampler2D "
            "canvas; void main(){gl_FragColor=texture2D(canvas,uv);}");
      } catch (...) {
        glDeleteShader(vertex);
        throw;
      }
      program = glCreateProgram();
      glAttachShader(program, vertex);
      glAttachShader(program, fragment);
      glBindAttribLocation(program, 0, "position");
      glBindAttribLocation(program, 1, "texcoord");
      glLinkProgram(program);
      glDeleteShader(vertex);
      glDeleteShader(fragment);
      GLint ok;
      glGetProgramiv(program, GL_LINK_STATUS, &ok);
      if (!ok)
        throw std::runtime_error("Cannot link launcher shader");
      canvasLocation = glGetUniformLocation(program, "canvas");
      const GLfloat vertices[] = {-1, 1, 0, 0, -1, -1, 0, 1,
                                  1,  1, 1, 0, 1,  -1, 1, 1};
      glGenBuffers(1, &buffer);
      glBindBuffer(GL_ARRAY_BUFFER, buffer);
      glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
      glGenTextures(1, &texture);
      glBindTexture(GL_TEXTURE_2D, texture);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 960, 544, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, nullptr);
    } catch (...) {
      close();
      throw;
    }
  }
  ~Presenter() { close(); }
  void close() {
    if (context) {
      glFinish();
      if (texture)
        glDeleteTextures(1, &texture);
      if (buffer)
        glDeleteBuffers(1, &buffer);
      if (program)
        glDeleteProgram(program);
      SDL_GL_DeleteContext(context);
    }
    if (window)
      SDL_DestroyWindow(window);
    window = nullptr;
    context = nullptr;
    texture = buffer = program = 0;
  }
  void show(const View &v, bool changed, bool keyboard = false) {
    glViewport(0, 0, 960, 544);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glUseProgram(program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    if (changed)
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 960, 544, GL_RGBA,
                      GL_UNSIGNED_BYTE, v.pixels.data());
    glUniform1i(canvasLocation, 0);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat),
                          nullptr);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat),
                          reinterpret_cast<void *>(2 * sizeof(GLfloat)));
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
#ifdef __vita__
    if (keyboard)
      vglSwapBuffers(GL_TRUE);
    else
#else
    (void)keyboard;
#endif
      SDL_GL_SwapWindow(window);
  }
#ifdef __vita__
  bool editName(const View &view, const std::string &initial, std::string &out,
                const char *heading = "Rename game") {
    bool loaded = sceSysmoduleIsLoaded(SCE_SYSMODULE_IME) < 0;
    if (loaded && sceSysmoduleLoadModule(SCE_SYSMODULE_IME) < 0)
      throw std::runtime_error("Cannot load Vita keyboard");
    bool active = false;
    auto closeKeyboard = [&] {
      if (active) {
        if (sceImeDialogGetStatus() == SCE_COMMON_DIALOG_STATUS_RUNNING)
          sceImeDialogAbort();
        sceImeDialogTerm();
        active = false;
      }
      nativeKeyboardActive = false;
      if (loaded) {
        sceSysmoduleUnloadModule(SCE_SYSMODULE_IME);
        loaded = false;
      }
    };
    try {
      auto title = keyboardText(heading);
      auto text = keyboardText(initial);
      text.resize(129, 0);
      uint16_t resultText[129]{};
      SceImeDialogParam param;
      sceImeDialogParamInit(&param);
      param.type = SCE_IME_TYPE_DEFAULT;
      param.option = SCE_IME_OPTION_NO_AUTO_CAPITALIZATION;
      param.dialogMode = SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;
      param.textBoxMode = SCE_IME_DIALOG_TEXTBOX_MODE_WITH_CLEAR;
      param.title = title.data();
      param.initialText = text.data();
      param.inputTextBuffer = resultText;
      param.maxTextLength = 128;
      glFinish();
      int rc = sceImeDialogInit(&param);
      if (rc < 0)
        throw std::runtime_error("Cannot open Vita keyboard (" +
                                 std::to_string(rc) + ").");
      active = true;
      nativeKeyboardActive = true;
      while (sceImeDialogGetStatus() == SCE_COMMON_DIALOG_STATUS_RUNNING) {
        // Repaint the background before compositing each dialog frame. Reusing
        // buffers with old dialog pixels leaves stale animation positions
        // behind.
        show(view, false, true);
        sceGxmDisplayQueueFinish();
        SDL_Delay(16);
      }
      SceImeDialogResult result{};
      rc = sceImeDialogGetResult(&result);
      bool accepted = rc >= 0 && result.result == SCE_COMMON_DIALOG_RESULT_OK &&
                      result.button == SCE_IME_DIALOG_BUTTON_ENTER;
      if (accepted)
        out = keyboardResult(resultText, 129);
      closeKeyboard();
      if (rc < 0)
        throw std::runtime_error("Cannot read Vita keyboard result");
      return accepted;
    } catch (...) {
      closeKeyboard();
      throw;
    }
  }
#endif
};
class Input {
  uint32_t previous = 0, repeatAt = 0;
  bool armed = false;
  bool touching = false, dragged = false;
  int touchX = 0, touchY = 0, dragY = 0;

public:
  bool touch(UI &ui) {
#ifdef __vita__
    SceTouchData data{};
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &data, 1);
    bool down = data.reportNum > 0;
    if (down) {
      int x = data.report[0].x / 2, y = data.report[0].y / 2;
      if (!touching) {
        touchX = x;
        touchY = dragY = y;
        dragged = false;
      } else if (touchX >= 184 && std::abs(y - dragY) >= 24) {
        dragged = true;
        ui.scroll((dragY - y) / 24);
        dragY = y;
        touching = true;
        return true;
      }
    } else if (touching && !dragged) {
      touching = false;
      ui.touch(touchX, touchY);
      return true;
    }
    touching = down;
#else
    (void)ui;
#endif
    return false;
  }

private:
public:
  void reset() {
    previous = repeatAt = 0;
    armed = false;
    touching = dragged = false;
  }
  Button poll(bool &quit) {
    SDL_Event event;
    while (SDL_PollEvent(&event))
      if (event.type == SDL_QUIT)
        quit = true;
#ifdef __vita__
    SceCtrlData pad{};
    sceCtrlPeekBufferPositive(0, &pad, 1);
    uint32_t held = pad.buttons;
    if (pad.lx < 80)
      held |= SCE_CTRL_LEFT;
    else if (pad.lx > 176)
      held |= SCE_CTRL_RIGHT;
    if (pad.ly < 80)
      held |= SCE_CTRL_UP;
    else if (pad.ly > 176)
      held |= SCE_CTRL_DOWN;
    static const uint32_t masks[] = {
        SCE_CTRL_UP,     SCE_CTRL_DOWN,     SCE_CTRL_CROSS,
        SCE_CTRL_CIRCLE, SCE_CTRL_SQUARE,   SCE_CTRL_TRIANGLE,
        SCE_CTRL_LEFT,   SCE_CTRL_RIGHT,    SCE_CTRL_START,
        SCE_CTRL_SELECT, SCE_CTRL_LTRIGGER, SCE_CTRL_RTRIGGER};
#else
    const Uint8 *keys = SDL_GetKeyboardState(nullptr);
    uint32_t held = 0;
    const SDL_Scancode codes[] = {
        SDL_SCANCODE_UP,     SDL_SCANCODE_DOWN,  SDL_SCANCODE_RETURN,
        SDL_SCANCODE_ESCAPE, SDL_SCANCODE_SPACE, SDL_SCANCODE_R,
        SDL_SCANCODE_LEFT,   SDL_SCANCODE_RIGHT, SDL_SCANCODE_TAB,
        SDL_SCANCODE_F,      SDL_SCANCODE_Q,     SDL_SCANCODE_E};
    static const uint32_t masks[] = {1,  2,   4,   8,   16,   32,
                                     64, 128, 256, 512, 1024, 2048};
    for (int i = 0; i < 12; ++i)
      if (keys[codes[i]])
        held |= masks[i];
#endif
    const Button buttons[] = {Button::Up,     Button::Down,   Button::Cross,
                              Button::Circle, Button::Square, Button::Triangle,
                              Button::Left,   Button::Right,  Button::Start,
                              Button::Select, Button::L,      Button::R};
    if (!armed) {
      previous = held;
      if (!held)
        armed = true;
      return Button::None;
    }
    auto edge = held & ~previous;
    auto now = SDL_GetTicks();
    if (edge) {
      repeatAt = now + 400;
      previous = held;
      if ((held & masks[10]) && (held & masks[11]))
        return Button::Paths;
      for (int i = 0; i < 12; ++i)
        if (edge & masks[i])
          return buttons[i];
    }
    if (held == previous && (held & (masks[0] | masks[1])) &&
        int32_t(now - repeatAt) >= 0) {
      repeatAt = now + 80;
      return held & masks[0] ? Button::Up : Button::Down;
    }
    previous = held;
    return Button::None;
  }
};
} // namespace

bool nativeLauncherRequested(int argc, char **argv) {
  for (int i = 1; i < argc; ++i)
    if (!std::strcmp(argv[i], "--hardrpg-play")) {
      // A missing handoff must return to the native menu, never an RGSS stub.
      std::string folder, config;
      std::vector<std::string> rtps;
      std::map<std::string, std::string> roots;
      int version = 0;
      protoLauncherPick(folder, version, rtps, config, roots, false);
      if (folder != "app0:/stub")
        return false;
      return true;
    }
  // A configured single-game VPK must keep its direct boot behavior.
  try {
    auto conf = json5pp::parse5(hardrpg::readFile("app0:/mkxp.json", 65536));
    for (auto k : {"gameFolder", "customScript"}) {
      auto i = conf.as_object().find(k);
      if (i != conf.as_object().end() && i->second.is_string() &&
          !i->second.as_string().empty())
        return false;
    }
  } catch (const std::exception &) {
  }
  return true;
}
int runNativeLauncher(bool returning) {
  using namespace hardrpg;
  try {
    // Selections are only consumed on an explicit --hardrpg-play boot.
    // A leftover selection cannot silently launch a game on a fresh boot.
    std::remove("ux0:/data/hardrpg/selection.json");
    UI ui;
#ifdef __vita__
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT,
                             SCE_TOUCH_SAMPLING_STATE_START);
#endif
    View view("app0:/font.ttf");
    std::fprintf(stderr, "HardRPG native launcher backend\n");
    for (;;) {
      bool quit = false;
      Input input;
      {
        Presenter presenter;
#ifdef __vita__
        ui.editName = [&](const std::string &initial, std::string &name) {
          try {
            bool accepted = presenter.editName(view, initial, name);
            // Closing the keyboard must not launch or leave a game.
            input.reset();
            return accepted;
          } catch (...) {
            input.reset();
            throw;
          }
        };
        ui.searchText = [&](const std::string &initial, std::string &out) {
          try {
            bool accepted =
                presenter.editName(view, initial, out, "Search games");
            input.reset();
            return accepted;
          } catch (...) {
            input.reset();
            throw;
          }
        };
#endif
        ui.beginLaunch = [&] {
          view.draw(ui);
          presenter.show(view, true);
        };
        if (!returning) {
          try {
            view.splash("app0:/stub/Graphics/Pictures/hardrpg-splash.png");
            presenter.show(view, true);
            auto start = SDL_GetTicks();
            while (SDL_GetTicks() - start < 1000 && !quit) {
              input.poll(quit);
              presenter.show(view, false);
              SDL_Delay(16);
            }
          } catch (const std::exception &e) {
            ui.message = e.what();
          }
        }
        returning = true;
        view.draw(ui);
        presenter.show(view, true);
        uint32_t progressAt = 0;
        ui.progressPump = [&] {
          auto button = input.poll(quit);
          if (quit || button == Button::Circle)
            throw Cancelled("Preparation cancelled.");
          auto now = SDL_GetTicks();
          if (int32_t(now - progressAt) >= 50) {
            progressAt = now;
            view.draw(ui);
            presenter.show(view, true);
          }
        };
        while (!quit && !ui.exitRequested && !ui.launchRequested) {
          auto button = input.poll(quit);
          bool touched = input.touch(ui);
          bool serviced = false;
          if (button == Button::None && !touched)
            serviced = ui.service();
          if (button != Button::None || touched || serviced) {
            if (button != Button::None)
              ui.action(button);
            view.draw(ui);
            presenter.show(view, true);
          } else
            presenter.show(view, false);
          SDL_Delay(12);
        }
      }
      if (quit || ui.exitRequested)
        return 0;
      // All menu GL resources and mounts are closed before LoadExec.
      // Neither Ruby nor OpenAL has been initialized in this process.
#ifdef __vita__
      SDL_Quit();
      char play[] = "--hardrpg-play";
      char *args[] = {play, nullptr};
      int rc = sceAppMgrLoadExec("app0:/eboot.bin", args, nullptr);
      std::remove("ux0:/data/hardrpg/selection.json");
      if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER |
                   SDL_INIT_EVENTS) < 0)
        return 1;
      ui.launchRequested = false;
      ui.message =
          "Could not start game (LoadExec " + std::to_string(rc) + ").";
#else
      return 0;
#endif
    }
  } catch (const std::exception &e) {
    std::fprintf(stderr, "Native launcher: %s\n", e.what());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "HardRPG", e.what(),
                             nullptr);
    return 1;
  }
}
