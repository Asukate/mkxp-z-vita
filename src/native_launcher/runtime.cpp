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
#include <map>
#include <memory>
#include <ctime>
#include <functional>
#ifdef __vita__
#include <psp2/appmgr.h>
#include <psp2/common_dialog.h>
#include <psp2/ctrl.h>
#include <psp2/gxm.h>
#include <psp2/ime_dialog.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/sysmodule.h>
#include <psp2/touch.h>
#include <psp2/power.h>
#include <psp2/rtc.h>
#include "input/vita_input.h"
#include "vita_startup_timer.h"
#include "display/gl/program-cache.h"
#include "shader-cache-build-id.h"
#include <sys/stat.h>
#include <vector>
#include <string>
extern "C" void glGetProgramBinary(GLuint, GLsizei, GLsizei *, GLenum *, void *);
extern "C" void glProgramBinary(GLuint, GLenum, const void *, GLsizei);
#include <psp2/kernel/sysmem.h>
extern "C" GLboolean vglInitWithCustomSizes(int pool_size, int width, int height,
                                            int ram_pool_size, int cdram_pool_size,
                                            int phycont_pool_size, int cdlg_pool_size,
                                            SceGxmMultisampleMode msaa);

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
  // Scene quads: one program, a unit square, and a texture per image.
  GLuint quadProgram = 0, unitSquare = 0;
  GLint rectLocation = -1, tintLocation = -1, shapeLocation = -1, solidLocation = -1, quadTextureLocation = -1;
  struct Texture {
    GLuint id;
    uint32_t used;
    uint64_t image; // the Image::id its pixels came from
    int width, height;
  };
  std::map<const Image *, Texture> textures;
  uint32_t frame = 0;
  // Linked programs are kept in the engine's program cache, so a launch only
  // compiles them (and only then loads the shader compiler) after an update.
  GLuint link(const char *name, const char *vs, const char *fs, const char *a0, const char *a1) {
#ifdef __vita__
    std::string key(MKXPZ_SHADER_CACHE_BUILD);
    for (const char *part : {name, vs, fs, a0, a1 ? a1 : ""}) {
      key += part;
      key.push_back('\0');
    }
    char path[96];
    std::snprintf(path, sizeof path, "ux0:/data/mkxpz-cache/programs-v1/%08x.bin",
                  unsigned(ProgramCache::checksum(name, std::strlen(name))));
    {
      std::vector<uint8_t> binary;
      if (ProgramCache::load(path, key, binary)) {
        GLuint p = glCreateProgram();
        glProgramBinary(p, 0, binary.data(), binary.size());
        GLint ok = GL_FALSE;
        glGetProgramiv(p, GL_LINK_STATUS, &ok);
        if (ok)
          return p;
        glDeleteProgram(p);
      }
    }
#endif
    GLuint vertex = shader(GL_VERTEX_SHADER, vs), fragment = 0;
    try {
      fragment = shader(GL_FRAGMENT_SHADER, fs);
    } catch (...) {
      glDeleteShader(vertex);
      throw;
    }
    GLuint p = glCreateProgram();
    glAttachShader(p, vertex);
    glAttachShader(p, fragment);
    glBindAttribLocation(p, 0, a0);
    if (a1)
      glBindAttribLocation(p, 1, a1);
    glLinkProgram(p);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok)
      throw std::runtime_error("Cannot link launcher shader");
#ifdef __vita__
    GLint size = 0;
    glGetProgramiv(p, 0x8741 /* GL_PROGRAM_BINARY_LENGTH */, &size);
    if (size > 0 && unsigned(size) <= ProgramCache::MaxBinary) {
      try {
        std::vector<uint8_t> binary(size);
        GLsizei written = 0;
        GLenum format = 0;
        glGetProgramBinary(p, size, &written, &format, binary.data());
        if (written == size) {
          mkdir("ux0:/data/mkxpz-cache", 0777);
          mkdir("ux0:/data/mkxpz-cache/programs-v1", 0777);
          ProgramCache::save(path, key, binary);
        }
      } catch (const std::bad_alloc &) { /* The cache is optional. */ }
    }
#endif
    return p;
  }
  // One texture per Image object. When its pixels change (a text layer drawn
  // again) the same texture is refilled rather than a new one made: a layer
  // that changes every frame would otherwise fill graphics memory with
  // textures waiting to be freed.
  GLuint textureFor(const Image &img) {
    auto found = textures.find(&img);
    if (found != textures.end()) {
      auto &t = found->second;
      t.used = frame;
      if (t.image == img.id)
        return t.id;
      glBindTexture(GL_TEXTURE_2D, t.id);
      if (t.width == img.width && t.height == img.height)
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, img.width, img.height, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
      else
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width, img.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
      t.image = img.id;
      t.width = img.width;
      t.height = img.height;
      return t.id;
    }
    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width, img.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
    textures[&img] = {id, frame, img.id, img.width, img.height};
    return id;
  }
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
#if defined(__vita__) && !defined(HARDRPG_LAUNCHER_FULL_POOL)
      {
        /* vitaGL allocates and maps its whole pool at start-up, which costs
         * about 13 ms per MiB. Left to its defaults it takes all free main
         * memory; the menu draws a handful of textures, so give it a small
         * main-memory pool and all of video memory. vitaGL ignores SDL's own
         * initialization after this one. Multisampling stays as before. */
        SceKernelFreeMemorySizeInfo info;
        info.size = sizeof(info);
        sceKernelGetFreeMemorySize(&info);
        vglInitWithCustomSizes(0, 960, 544, 32 * 1024 * 1024, info.size_cdram,
                               1024 * 1024, 0, SCE_GXM_MULTISAMPLE_4X);
      }
#endif
      context = SDL_GL_CreateContext(window);
      if (!context)
        throw std::runtime_error(SDL_GetError());
#ifdef __vita__
      vitaStartupTimerMark("launcher_gl_ready");
#endif
      SDL_GL_SetSwapInterval(1);
      program = link(
          "launcher-canvas",
          "attribute vec2 position; attribute vec2 texcoord; varying vec2 uv; "
          "void main(){uv=texcoord;gl_Position=vec4(position,0.0,1.0);}",
          "precision mediump float; varying vec2 uv; uniform sampler2D "
          "canvas; void main(){gl_FragColor=texture2D(canvas,uv);}",
          "position", "texcoord");
      canvasLocation = glGetUniformLocation(program, "canvas");
      quadProgram = link(
          "launcher-quad",
          "attribute vec2 corner; uniform vec4 rect; varying vec2 local; "
          "void main(){local=corner;vec2 p=rect.xy+corner*rect.zw;"
          "gl_Position=vec4(p.x/480.0-1.0,1.0-p.y/272.0,0.0,1.0);}",
          "precision mediump float; varying vec2 local; uniform sampler2D image; "
          "uniform vec4 tint; uniform vec3 shape; uniform float solid; "
          "void main(){vec4 c=mix(texture2D(image,local),vec4(1.0),solid);"
          "vec2 p=local*shape.xy; vec2 q=min(p,shape.xy-p); float r=shape.z; float k=1.0;"
          "if(q.x<r&&q.y<r){k=clamp(r+0.5-length(vec2(r,r)-q),0.0,1.0);}"
          "gl_FragColor=vec4(c.rgb*tint.rgb,c.a)*(tint.a*k);}",
          "corner", nullptr);
      rectLocation = glGetUniformLocation(quadProgram, "rect");
      tintLocation = glGetUniformLocation(quadProgram, "tint");
      shapeLocation = glGetUniformLocation(quadProgram, "shape");
      solidLocation = glGetUniformLocation(quadProgram, "solid");
      quadTextureLocation = glGetUniformLocation(quadProgram, "image");
      const GLfloat square[] = {0, 0, 0, 1, 1, 0, 1, 1};
      glGenBuffers(1, &unitSquare);
      glBindBuffer(GL_ARRAY_BUFFER, unitSquare);
      glBufferData(GL_ARRAY_BUFFER, sizeof(square), square, GL_STATIC_DRAW);
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
      for (auto &t : textures)
        glDeleteTextures(1, &t.second.id);
      textures.clear();
      if (unitSquare)
        glDeleteBuffers(1, &unitSquare);
      if (quadProgram)
        glDeleteProgram(quadProgram);
      SDL_GL_DeleteContext(context);
    }
    if (window)
      SDL_DestroyWindow(window);
    window = nullptr;
    context = nullptr;
    texture = buffer = program = quadProgram = unitSquare = 0;
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
#ifdef __vita__
    static bool firstFrame = true;
    if (firstFrame) {
      firstFrame = false;
      vitaStartupTimerMark("launcher_first_frame");
    }
#endif
  }
  // Draws the home scene's quads; the canvas texture is not used.
  void showScene(const View &v, bool keyboard = false) {
    ++frame;
    glViewport(0, 0, 960, 544);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(quadProgram);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(quadTextureLocation, 0);
    glBindBuffer(GL_ARRAY_BUFFER, unitSquare);
    glEnableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), nullptr);
    bool scissor = false;
    for (auto &q : v.scene) {
      if (q.w <= 0 || q.h <= 0 || q.alpha <= 0.002f)
        continue;
      bool clip = q.clipTop > 0 || q.clipBottom < 544;
      if (clip != scissor) {
        if (clip)
          glEnable(GL_SCISSOR_TEST);
        else
          glDisable(GL_SCISSOR_TEST);
        scissor = clip;
      }
      if (clip)
        glScissor(0, 544 - q.clipBottom, 960, q.clipBottom - q.clipTop);
      if (q.image) {
        if (q.image->empty())
          continue;
        glBindTexture(GL_TEXTURE_2D, textureFor(*q.image));
        glUniform1f(solidLocation, 0.0f);
        glUniform4f(tintLocation, q.bright, q.bright, q.bright, q.alpha);
      } else {
        glUniform1f(solidLocation, 1.0f);
        glUniform4f(tintLocation, q.color[0] / 255.0f, q.color[1] / 255.0f, q.color[2] / 255.0f, q.alpha);
      }
      glUniform4f(rectLocation, q.x, q.y, q.w, q.h);
      glUniform3f(shapeLocation, q.w, q.h, q.radius);
      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    if (scissor)
      glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    // Textures not drawn for a while (old layers, evicted art) are freed.
    if (frame % 60 == 0)
      for (auto it = textures.begin(); it != textures.end();)
        if (frame - it->second.used > 120) {
          glDeleteTextures(1, &it->second.id);
          it = textures.erase(it);
        } else
          ++it;
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
  // paint draws one frame under the keyboard; the CPU canvas by default.
  bool editName(const View &view, const std::string &initial, std::string &out,
                const char *heading = "Rename game", std::function<void()> paint = nullptr) {
    if (!paint)
      paint = [&] { show(view, false, true); };
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
        paint();
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
  bool touching = false, dragged = false, homeTouch = false;
  int touchX = 0, touchY = 0, dragY = 0;

public:
  bool touch(UI &ui) {
#ifdef __vita__
    SceTouchData data{};
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &data, 1);
    bool down = data.reportNum > 0;
    if (ui.homeActive() || homeTouch) {
      // The home screen tracks presses itself: drags, flings and taps.
      int x = down ? data.report[0].x / 2 : touchX, y = down ? data.report[0].y / 2 : touchY;
      bool changed = false;
      if (down && !touching) {
        homeTouch = true;
        changed = ui.homePointer(0, x, y, SDL_GetTicks());
      } else if (down)
        changed = ui.homePointer(1, x, y, SDL_GetTicks());
      else if (touching) {
        homeTouch = false;
        changed = ui.homePointer(2, x, y, SDL_GetTicks());
      }
      touching = down;
      touchX = x;
      touchY = y;
      return changed;
    }
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
    touching = dragged = homeTouch = false;
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
    if (held == previous && (held & (masks[0] | masks[1] | masks[6] | masks[7])) &&
        int32_t(now - repeatAt) >= 0) {
      repeatAt = now + 110;
      return held & masks[0] ? Button::Up : held & masks[1] ? Button::Down : held & masks[6] ? Button::Left : Button::Right;
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
#ifdef __vita__
    vitaStartupTimerMark("launcher_begin");
#endif
#ifdef __vita__
    // Loading the keyboard module takes a moment; do it now, off the UI
    // thread, so the search button opens the keyboard at once.
    {
      auto thread = sceKernelCreateThread("hardrpg_ime_preload", [](SceSize, void *) -> int {
        if (sceSysmoduleIsLoaded(SCE_SYSMODULE_IME) < 0)
          sceSysmoduleLoadModule(SCE_SYSMODULE_IME);
        sceKernelExitDeleteThread(0);
        return 0;
      }, 0x10000100, 64 * 1024, 0, 0, nullptr);
      if (thread >= 0 && sceKernelStartThread(thread, 0, nullptr) < 0)
        sceKernelDeleteThread(thread);
    }
#endif
#ifdef __vita__
    // A cold start reads the library (about 0.75 s with twenty games) on a
    // worker thread while the screen starts and the splash is up. Until
    // loadHome() joins it, this thread touches no library state.
    UI ui({}, returning);
    struct LibraryScan {
      UI *ui;
      std::string error;
      void work() {
        try {
          ui->scanLibrary();
        } catch (const std::exception &e) {
          error = e.what();
        }
      }
      static int run(SceSize, void *argument) {
        (*static_cast<LibraryScan **>(argument))->work();
        return sceKernelExitThread(0);
      }
    } scan{&ui, {}};
    SceUID scanThread = -1;
    if (!returning) {
      LibraryScan *argument = &scan;
      scanThread = sceKernelCreateThread("hardrpg_library_scan", LibraryScan::run, 0x10000100,
                                         256 * 1024, 0, 0, nullptr);
      if (scanThread >= 0 && sceKernelStartThread(scanThread, sizeof argument, &argument) < 0) {
        sceKernelDeleteThread(scanThread);
        scanThread = -1;
      }
      if (scanThread < 0)
        scan.work();
    }
#else
    UI ui;
#endif
    UpdateCheck update;
    // The library is read while the splash is up (a cold start) instead of
    // before the screen shows anything; the splash lasts only that long.
    // Returning from a game has no splash.
    bool homeLoaded = false;
    const bool startRelease = !returning;
    auto loadHome = [&] {
#ifdef __vita__
      if (scanThread >= 0) {
        sceKernelWaitThreadEnd(scanThread, nullptr, nullptr);
        sceKernelDeleteThread(scanThread);
        scanThread = -1;
      }
      if (!scan.error.empty())
        ui.message = scan.error;
#endif
      try {
        ui.model.settleActivity(returning);
        ui.loadHome();
      } catch (const std::exception &e) {
        ui.message = e.what();
      }
#ifdef __vita__
      vitaStartupTimerMark("launcher_library_ready");
#endif
      if (startRelease && ui.automaticUpdates)
        update.start();
#ifdef __vita__
      ui.model.backendAvailable = [](const Engine &engine) {
        FILE *f = std::fopen(engine.executable, "rb");
        if (!f)
          return false;
        std::fclose(f);
        return true;
      };
#endif
      homeLoaded = true;
    };
    if (returning)
      loadHome();
#ifdef __vita__
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT,
                             SCE_TOUCH_SAMPLING_STATE_START);
#endif
    View view("app0:/font.ttf");
    // The home screen's covers, art and fades are drawn by the GPU.
    view.gpu = true;
#ifdef __vita__
    vitaStartupTimerMark("launcher_view_ready");
#endif
    std::fprintf(stderr, "HardRPG native launcher backend\n");
    for (;;) {
      bool quit = false;
      Input input;
      {
        Presenter presenter;
#ifdef __vita__
        // The keyboard is drawn over the screen each frame; every screen
        // is a GPU scene, redrawn as is under it.
        auto keyboard = [&](const std::string &initial, std::string &out, const char *heading) {
          view.draw(ui);
          return presenter.editName(view, initial, out, heading, [&] { presenter.showScene(view, true); });
        };
        ui.enterText = [&](const std::string &heading, const std::string &initial, std::string &out) {
          try {
            bool accepted = keyboard(initial, out, heading.c_str());
            input.reset();
            return accepted;
          } catch (...) {
            input.reset();
            throw;
          }
        };
        ui.editName = [&](const std::string &initial, std::string &name) {
          try {
            bool accepted = keyboard(initial, name, "Rename game");
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
            bool accepted = keyboard(initial, out, "Search games");
            input.reset();
            return accepted;
          } catch (...) {
            input.reset();
            throw;
          }
        };
#endif
        auto present = [&](bool changed) {
          if (view.gpu)
            presenter.showScene(view);
          else
            presenter.show(view, changed);
        };
        ui.beginLaunch = [&] {
          view.draw(ui);
          present(true);
        };
        if (!returning) {
          try {
            // The splash stays up while the library is read and the home
            // screen's first pictures are decoded, and no longer: the list
            // then appears complete. Pictures still loading after 1.5 s
            // appear as they finish. Nothing is presented meanwhile, so the
            // splash stays on screen.
            view.splash("app0:/stub/Graphics/Pictures/hardrpg-splash.png");
            presenter.show(view, true);
            auto start = SDL_GetTicks();
            if (!homeLoaded)
              loadHome();
            for (;;) {
              view.draw(ui);
              if (!view.pendingLoads || quit || SDL_GetTicks() - start > 1500)
                break;
              input.poll(quit);
              SDL_Delay(8);
            }
          } catch (const std::exception &e) {
            ui.message = e.what();
          }
        }
        if (!homeLoaded)
          loadHome();
        returning = true;
        view.draw(ui);
        present(true);
#ifdef __vita__
        vitaStartupTimerMark("launcher_list_shown");
#endif
        uint32_t progressAt = 0;
        ui.progressPump = [&] {
          auto button = input.poll(quit);
          if (quit || button == Button::Circle)
            throw Cancelled("Preparation cancelled.");
          auto now = SDL_GetTicks();
          if (int32_t(now - progressAt) >= 50) {
            progressAt = now;
            view.draw(ui);
            present(true);
          }
        };
        uint32_t lastFrame = SDL_GetTicks();
        // Messages go away on their own a few seconds after they last change.
        std::string shownMessage;
        uint32_t messageAt = 0;
        uint64_t frameStamp = SDL_GetPerformanceCounter();
        uint32_t statsAt = 0;
        int frames = 0;
        double frameSum = 0, frameWorst = 0, drawSum = 0;
        std::string lastClock;
        auto status = [&] {
          // The clock and battery in the home screen's top bar.
#ifdef __vita__
          SceDateTime t{};
          if (sceRtcGetCurrentClockLocalTime(&t) >= 0) {
            char clock[8];
            std::snprintf(clock, sizeof clock, "%02d:%02d", t.hour, t.minute);
            ui.clockText = clock;
          }
          int battery = scePowerGetBatteryLifePercent();
          ui.batteryPercent = battery >= 0 && battery <= 100 ? battery : -1;
#else
          std::time_t now = std::time(nullptr);
          char clock[8];
          std::strftime(clock, sizeof clock, "%H:%M", std::localtime(&now));
          ui.clockText = clock;
#endif
        };
        status();
        while (!quit && !ui.exitRequested && !ui.launchRequested) {
          auto button = input.poll(quit);
          bool touched = input.touch(ui);
          bool serviced = false;
          if(ui.checkReleaseRequested) {update.start();ui.checkReleaseRequested=false;}
          ReleaseNotice notice;
          bool retrieved = false;
          if(update.poll(notice, &retrieved)) {
            ui.offerRelease(notice);
            if(ui.page==Page::Updates)ui.message=!notice.tag.empty()?"New release: "+notice.tag:
                retrieved?"No newer release found.":"Could not check releases. Offline or request failed.";
            serviced=true;
          }
#ifdef __vita__
          if(ui.controlsTesting) {
            ui.controlsSample=vitaReadControls();
            ui.controlsResult=ui.controlsTestState.update(ui.controls,ui.controlsSample);
            serviced=true;
          }
#endif
          if (button == Button::None && !touched)
            serviced = ui.service() || serviced;
          serviced = ui.pollArt() || serviced;
          uint32_t now = SDL_GetTicks();
          double dt = std::min(0.05, (now - lastFrame) / 1000.0);
          lastFrame = now;
          bool moving = ui.animating() || view.animating();
          if (ui.message != shownMessage) {
            shownMessage = ui.message;
            messageAt = now;
          } else if (!shownMessage.empty() && now - messageAt > 4500 && !(ui.artFetch && ui.artFetch->running()) &&
                     ui.launchingName.empty()) {
            ui.message.clear();
            shownMessage.clear();
            serviced = true;
          }
          if (moving)
            ui.tick(dt);
          // Refresh the top bar clock once a minute.
          if (ui.homeActive() && now / 15000 != (now - uint32_t(dt * 1000)) / 15000) {
            auto before = ui.clockText;
            status();
            serviced = serviced || ui.clockText != before;
          }
          if (button != Button::None || touched || serviced || moving) {
            if (button != Button::None)
              ui.action(button);
            auto drawStart = SDL_GetPerformanceCounter();
            view.draw(ui);
            present(true);
            if (ui.showFrameTime) {
              // Whole frames (input to vsync) and the part spent building them.
              auto ticks = SDL_GetPerformanceCounter();
              double frameMs = double(ticks - frameStamp) * 1000 / SDL_GetPerformanceFrequency();
              double drawMs = double(ticks - drawStart) * 1000 / SDL_GetPerformanceFrequency();
              frameStamp = ticks;
              if (frameMs < 250) {
                ++frames;
                frameSum += frameMs;
                frameWorst = std::max(frameWorst, frameMs);
                drawSum += drawMs;
              }
              if (now - statsAt >= 500 && frames) {
                char text[96];
                std::snprintf(text, sizeof text, "%.0f fps  %.1f ms  worst %.0f ms  draw %.1f ms", 1000 * frames / frameSum,
                              frameSum / frames, frameWorst, drawSum / frames);
                ui.frameText = text;
                statsAt = now;
                frames = 0;
                frameSum = frameWorst = drawSum = 0;
              }
            }
          } else {
            present(false);
            frameStamp = SDL_GetPerformanceCounter();
          }
          // Vsync paces animation; idle frames still sleep a little.
          if (!moving)
            SDL_Delay(12);
        }
      }
      if (quit || ui.exitRequested)
        return 0;
      // All menu GL resources and mounts are closed before LoadExec.
      // Neither Ruby nor OpenAL has been initialized in this process.
#ifdef __vita__
      update.cancel();
      SDL_Quit();
      char play[] = "--hardrpg-play";
      char *args[] = {play, nullptr};
      const auto *engine = engineFor(ui.launchFormat);
      int rc = sceAppMgrLoadExec(engine->executable, args, nullptr);
      std::fprintf(stderr, "LoadExec failed: %s (%d, 0x%08x)\n",
                   engine->executable, rc, unsigned(rc));
      std::remove("ux0:/data/hardrpg/selection.json");
      if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER |
                   SDL_INIT_EVENTS) < 0)
        return 1;
      ui.launchFailed(rc, engine->executable);
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
