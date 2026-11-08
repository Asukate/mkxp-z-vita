#pragma once
#include "performance-metrics.h"
#include "gl/gl-fun.h"
#include "gl/gl-meta.h"
#include "gl/quadarray.h"
#include "gl/gl-util.h"
#include "gl/program-cache.h"
#include "shader-cache-build-id.h"
#include "glstate.h"
#include "shader.h"
#include "sharedstate.h"
#include "etc/etc.h"
#include "native_launcher/view.h"
#include "input/vita-keyboard.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <vector>

// vitaGL's program binary calls (as display/gl/shader.cpp declares them).
extern "C" void glProgramBinary(GLuint, GLenum, const void *, GLsizei);
extern "C" void glGetProgramBinary(GLuint, GLsizei, GLsizei *, GLenum *, void *);

// GL for the in-game menu: the launcher view's quads (hardrpg::GameMenu)
// drawn on the GPU over the paused game, as the launcher draws its screens,
// and the menu icon in the screen's empty left border.
class GameMenuLayer {
    // The launcher's quad program, same sources, so its compiled binary in
    // the program cache is found: no shader compiler in game.
    GLuint program = 0, unitSquare = 0;
    GLint rectLocation = -1, tintLocation = -1, shapeLocation = -1, solidLocation = -1, imageLocation = -1;
    bool failed = false;
    struct Texture {
        GLuint id;
        uint64_t image;
        int width, height;
        uint32_t used;
    };
    std::map<const hardrpg::Image *, Texture> textures;
    uint32_t frame = 0;
    // The in-game keyboard's frosted glass: the game behind it, halved
    // twice into small targets and stretched back (bilinear), which blurs.
    GLuint glassProgram = 0;
    GLint glassRect = -1, glassUv = -1, glassShape = -1, glassImage = -1;
    bool glassFailed = false, glassFresh = false;
    TEXFBO glassHalf, glassQuarter;
    hardrpg::Image buttons;
    int buttonsFor = -1;
    bool buttonsKeyboard = false, buttonsMenu = false;

    static GLuint compile(GLenum type, const char *source) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &source, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            glDeleteShader(s);
            return 0;
        }
        return s;
    }
    // As the launcher's Presenter::link: the cached binary first.
    static GLuint link(const char *name, const char *vs, const char *fs, const char *a0) {
        std::string key(MKXPZ_SHADER_CACHE_BUILD);
        for (const char *part : {name, vs, fs, a0, ""}) {
            key += part;
            key.push_back('\0');
        }
        char path[96];
        std::snprintf(path, sizeof path, "ux0:/data/mkxpz-cache/programs-v1/%08x.bin",
                      unsigned(ProgramCache::checksum(name, std::strlen(name))));
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
        GLuint vertex = compile(GL_VERTEX_SHADER, vs), fragment = compile(GL_FRAGMENT_SHADER, fs);
        if (!vertex || !fragment) {
            if (vertex) glDeleteShader(vertex);
            if (fragment) glDeleteShader(fragment);
            return 0;
        }
        GLuint p = glCreateProgram();
        glAttachShader(p, vertex);
        glAttachShader(p, fragment);
        glBindAttribLocation(p, 0, a0);
        glLinkProgram(p);
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        GLint ok = 0;
        glGetProgramiv(p, GL_LINK_STATUS, &ok);
        if (!ok) {
            glDeleteProgram(p);
            return 0;
        }
        GLint size = 0;
        glGetProgramiv(p, 0x8741 /* GL_PROGRAM_BINARY_LENGTH */, &size);
        if (size > 0 && unsigned(size) <= ProgramCache::MaxBinary) {
            std::vector<uint8_t> out(size);
            GLsizei written = 0;
            GLenum format = 0;
            glGetProgramBinary(p, size, &written, &format, out.data());
            if (written == size) {
                mkdir("ux0:/data/mkxpz-cache", 0777);
                mkdir("ux0:/data/mkxpz-cache/programs-v1", 0777);
                ProgramCache::save(path, key, out);
            }
        }
        return p;
    }
    bool ready() {
        if (program || failed)
            return program != 0;
        program = link("launcher-quad",
            "attribute vec2 corner; uniform vec4 rect; varying vec2 local; "
            "void main(){local=corner;vec2 p=rect.xy+corner*rect.zw;"
            "gl_Position=vec4(p.x/480.0-1.0,1.0-p.y/272.0,0.0,1.0);}",
            "precision mediump float; varying vec2 local; uniform sampler2D image; "
            "uniform vec4 tint; uniform vec3 shape; uniform float solid; "
            "void main(){vec4 c=mix(texture2D(image,local),vec4(1.0),solid);"
            "vec2 p=local*shape.xy; vec2 q=min(p,shape.xy-p); float r=shape.z; float k=1.0;"
            "if(q.x<r&&q.y<r){k=clamp(r+0.5-length(vec2(r,r)-q),0.0,1.0);}"
            "gl_FragColor=vec4(c.rgb*tint.rgb,c.a)*(tint.a*k);}",
            "corner");
        if (!program) {
            failed = true;
            std::fprintf(stderr, "Game menu: no quad program\n");
            return false;
        }
        rectLocation = glGetUniformLocation(program, "rect");
        tintLocation = glGetUniformLocation(program, "tint");
        shapeLocation = glGetUniformLocation(program, "shape");
        solidLocation = glGetUniformLocation(program, "solid");
        imageLocation = glGetUniformLocation(program, "image");
        const GLfloat square[] = {0, 0, 0, 1, 1, 0, 1, 1};
        glGenBuffers(1, &unitSquare);
        glBindBuffer(GL_ARRAY_BUFFER, unitSquare);
        glBufferData(GL_ARRAY_BUFFER, sizeof(square), square, GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        return true;
    }
    // One texture per image, refilled when its pixels change.
    GLuint textureFor(const hardrpg::Image &img) {
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
        textures[&img] = {id, img.id, img.width, img.height, frame};
        return id;
    }

public:
    ~GameMenuLayer() { release(); }
    // Frees the textures (the view that owned the images is gone); the
    // program and square stay for the next time.
    void release() {
        for (auto &t : textures)
            glDeleteTextures(1, &t.second.id);
        textures.clear();
    }

    // GL state for quads, and back to what the engine's cache believes.
    bool begin(int winW, int winH) {
        if (!ready())
            return false;
        ++frame;
        FBO::bind(FBO::ID(0));
        glState.viewport.pushSet(IntRect(0, 0, winW, winH));
        glState.scissorTest.pushSet(false);
        glState.blend.pushSet(true);
        glState.blendMode.pushSet(BlendNormal);
        glState.program.set(program);
        gl.BlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        gl.ActiveTexture(GL_TEXTURE0);
        glUniform1i(imageLocation, 0);
        glBindBuffer(GL_ARRAY_BUFFER, unitSquare);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), nullptr);
        return true;
    }
    void quad(float x, float y, float w, float h, float r, float g, float b, float a, float radius,
              const hardrpg::Image *image) {
        if (image) {
            glBindTexture(GL_TEXTURE_2D, textureFor(*image));
            glUniform1f(solidLocation, 0.0f);
        } else
            glUniform1f(solidLocation, 1.0f);
        glUniform4f(tintLocation, r, g, b, a);
        glUniform4f(rectLocation, x, y, w, h);
        glUniform3f(shapeLocation, w, h, radius);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    void end() {
        glDisableVertexAttribArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        gl.BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glState.blendMode.pop();
        glState.blend.pop();
        glState.scissorTest.pop();
        glState.viewport.pop();
        // Textures of layers no longer drawn are freed now and then.
        if (frame % 60 == 0)
            for (auto it = textures.begin(); it != textures.end();)
                if (frame - it->second.used > 120) {
                    glDeleteTextures(1, &it->second.id);
                    it = textures.erase(it);
                } else
                    ++it;
    }

    // The game, dimmed, then the view's quads. winW x winH is the screen;
    // the view is laid out for 960x544, which is the Vita's screen.
    void draw(const hardrpg::View &view, int winW, int winH) {
        if (!begin(winW, winH))
            return;
        quad(0, 0, 960, 544, 0.03f, 0.03f, 0.04f, 0.64f, 0, nullptr);
        bool clipping = false;
        for (auto &q : view.scene) {
            if (q.w <= 0 || q.h <= 0 || q.alpha <= 0.002f || (q.image && q.image->empty()))
                continue;
            bool clip = q.clipTop > 0 || q.clipBottom < 544 || q.clipLeft > 0 || q.clipRight < 960;
            if (clip != clipping) {
                glState.scissorTest.set(clip);
                clipping = clip;
            }
            if (clip)
                glState.scissorBox.set(IntRect(q.clipLeft, 544 - q.clipBottom, q.clipRight - q.clipLeft, q.clipBottom - q.clipTop));
            if (q.image)
                quad(q.x, q.y, q.w, q.h, q.bright, q.bright, q.bright, q.alpha, q.radius, q.image);
            else
                quad(q.x, q.y, q.w, q.h, q.color[0] / 255.0f, q.color[1] / 255.0f, q.color[2] / 255.0f, q.alpha,
                     q.radius, nullptr);
        }
        end();
    }

    // One image (the stats panel) at x, y.
    void drawImage(const hardrpg::Image &image, int x, int y, int winW, int winH) {
        if (image.empty() || !begin(winW, winH))
            return;
        quad(float(x), float(y), float(image.width), float(image.height), 1, 1, 1, 1, 0, &image);
        end();
    }

    bool glassReady() {
        if (glassProgram || glassFailed)
            return glassProgram != 0;
        glassProgram = link("keyboard-glass",
            "attribute vec2 corner; uniform vec4 rect; uniform vec4 uv; varying vec2 local; varying vec2 tc; "
            "void main(){local=corner;tc=mix(uv.xy,uv.zw,corner);vec2 p=rect.xy+corner*rect.zw;"
            "gl_Position=vec4(p.x/480.0-1.0,1.0-p.y/272.0,0.0,1.0);}",
            "precision mediump float; varying vec2 local; varying vec2 tc; uniform sampler2D image; "
            "uniform vec3 shape; "
            "void main(){vec4 c=texture2D(image,tc);"
            "vec2 p=local*shape.xy; vec2 q=min(p,shape.xy-p); float r=shape.z; float k=1.0;"
            "if(q.x<r&&q.y<r){k=clamp(r+0.5-length(vec2(r,r)-q),0.0,1.0);}"
            "gl_FragColor=vec4(c.rgb*k,k);}",
            "corner");
        if (!glassProgram) {
            glassFailed = true;
            std::fprintf(stderr, "Game keyboard: no glass program\n");
            return false;
        }
        glassRect = glGetUniformLocation(glassProgram, "rect");
        glassUv = glGetUniformLocation(glassProgram, "uv");
        glassShape = glGetUniformLocation(glassProgram, "shape");
        glassImage = glGetUniformLocation(glassProgram, "image");
        try {
            for (TEXFBO *t : {&glassHalf, &glassQuarter}) {
                const auto area = hardrpg::GameKeyboard::glassArea();
                const int div = t == &glassHalf ? 2 : 4;
                TEXFBO::init(*t);
                TEXFBO::allocEmpty(*t, (area.w + div - 1) / div, (area.h + div - 1) / div);
                TEXFBO::linkFBO(*t);
                gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            }
        } catch (const Exception &) {
            glassFailed = true;
            FBO::bind(FBO::ID(0));
            return false;
        }
        FBO::bind(FBO::ID(0));
        return true;
    }
    void glassQuad(float x, float y, float w, float h, float u0, float v0, float u1, float v1, float radius) {
        glUniform4f(glassRect, x, y, w, h);
        glUniform4f(glassUv, u0, v0, u1, v1);
        glUniform3f(glassShape, w, h, radius);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

public:
    // Before the game is drawn to the screen: the glass area of this frame,
    // a quarter size. game is the frame's texture, shown at gameRect.
    void prepareGlass(GLuint game, const IntRect &gameRect) {
        if (!ready() || !glassReady())
            return;
        const auto area = hardrpg::GameKeyboard::glassArea();
        glState.scissorTest.pushSet(false);
        glState.blend.pushSet(false);
        glState.program.set(glassProgram);
        glUniform1i(glassImage, 0);
        gl.ActiveTexture(GL_TEXTURE0);
        glBindBuffer(GL_ARRAY_BUFFER, unitSquare);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), nullptr);
        // Each pass fills its whole target (the 960x544 space is the
        // viewport), drawn upside down so rows stay top first in memory.
        FBO::bind(glassHalf.fbo);
        glState.viewport.pushSet(IntRect(0, 0, glassHalf.width, glassHalf.height));
        glState.clearColor.pushSet(Vec4(0, 0, 0, 1));
        FBO::clear();
        glState.clearColor.pop();
        glBindTexture(GL_TEXTURE_2D, game);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        const float sx = 960.f / area.w, sy = 544.f / area.h;
        const float gy = (gameRect.y - area.y) * sy, gh = gameRect.h * sy;
        glassQuad((gameRect.x - area.x) * sx, 544 - gy - gh, gameRect.w * sx, gh, 0, 1, 1, 0, 0);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glState.viewport.pop();
        FBO::bind(glassQuarter.fbo);
        glState.viewport.pushSet(IntRect(0, 0, glassQuarter.width, glassQuarter.height));
        glBindTexture(GL_TEXTURE_2D, glassHalf.tex.gl);
        glassQuad(0, 0, 960, 544, 0, 1, 1, 0, 0);
        glState.viewport.pop();
        glDisableVertexAttribArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glState.blend.pop();
        glState.scissorTest.pop();
        FBO::bind(FBO::ID(0));
        glassFresh = true;
    }

    // The keyboard over the game: glass, keys, the held key lit (fading
    // after release) and, for a held character, its preview above it.
    void drawKeyboard(const hardrpg::Image &panel, const hardrpg::Image *preview,
                      const hardrpg::GameKeyboard &keyboard, uint64_t now, int winW, int winH) {
        if (panel.empty() || !begin(winW, winH))
            return;
        const auto &layout = keyboard.layout();
        const auto area = hardrpg::GameKeyboard::glassArea();
        const int lit = keyboard.litIndex();
        const bool showPreview = preview && !preview->empty() && keyboard.previewing() && lit >= 0;
        hardrpg::KeyboardRect previewAt{};
        if (showPreview)
            previewAt = hardrpg::GameKeyboard::previewRect(layout.keys[lit].rect);
        auto frosted = [&](const hardrpg::KeyboardRect &r, float radius) {
            glassQuad(r.x, r.y, r.w, r.h, float(r.x - area.x) / area.w, float(r.y - area.y) / area.h,
                      float(r.x + r.w - area.x) / area.w, float(r.y + r.h - area.y) / area.h, radius);
        };
        if (glassFresh && glassProgram) {
            glState.program.set(glassProgram);
            glUniform1i(glassImage, 0);
            glBindTexture(GL_TEXTURE_2D, glassQuarter.tex.gl);
            for (const auto &p : layout.panels)
                frosted(p, 16);
            glState.program.set(program);
        } else {
            // No blur this frame (or none at all): a darker tint instead.
            for (const auto &p : layout.panels)
                quad(p.x, p.y, p.w, p.h, 0.05f, 0.06f, 0.08f, 0.55f, 16, nullptr);
        }
        quad(8, hardrpg::GameKeyboard::top, panel.width, panel.height, 1, 1, 1, 1, 0, &panel);
        const float glow = keyboard.litAlpha(now);
        if (lit >= 0 && glow > 0) {
            const auto &r = layout.keys[lit].rect;
            quad(r.x, r.y, r.w, r.h, 1, 1, 1, 0.18f * glow, 8, nullptr);
        }
        if (keyboard.controlledByPad() && keyboard.focusIndex() != lit) {
            const auto &r = layout.keys[keyboard.focusIndex()].rect;
            quad(r.x, r.y, r.w, r.h, 1, 1, 1, 0.14f, 8, nullptr);
        }
        if (showPreview) {
            if (glassFresh && glassProgram) {
                glState.program.set(glassProgram);
                glBindTexture(GL_TEXTURE_2D, glassQuarter.tex.gl);
                frosted(previewAt, 12);
                glState.program.set(program);
            } else {
                quad(previewAt.x, previewAt.y, previewAt.w, previewAt.h, 0.05f, 0.06f, 0.08f, 0.55f, 12, nullptr);
            }
            quad(previewAt.x, previewAt.y, previewAt.w, previewAt.h, 1, 1, 1, 1, 0, preview);
        }
        glassFresh = false;
        end();
    }

    // The menu and keyboard buttons in the left border.
    void drawButtons(int winW, int winH, int gameX, bool menuLit, bool keyboardOpen) {
        if (buttonsFor != gameX || buttonsKeyboard != keyboardOpen || buttonsMenu != menuLit) {
            hardrpg::drawGameButtons(buttons, gameX, keyboardOpen, menuLit);
            buttonsFor = gameX;
            buttonsKeyboard = keyboardOpen;
            buttonsMenu = menuLit;
        }
        drawImage(buttons, 0, 0, winW, winH);
    }
};
