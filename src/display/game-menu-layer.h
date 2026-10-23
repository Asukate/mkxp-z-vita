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
    std::unique_ptr<QuadArray<CVertex>> icon;
    int iconBuiltFor = -1, iconHeight = 0;
    bool iconPressed = false;

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
            bool clip = q.clipTop > 0 || q.clipBottom < 544;
            if (clip != clipping) {
                glState.scissorTest.set(clip);
                clipping = clip;
            }
            if (clip)
                glState.scissorBox.set(IntRect(0, 544 - q.clipBottom, 960, q.clipBottom - q.clipTop));
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

    // Where the icon goes: centred in the left border, near the top. Empty
    // when the border is too narrow to hold it (the game fills the width).
    static bool iconRect(int gameX, int &x, int &y, int &w, int &h) {
        if (gameX < 52)
            return false;
        w = 35, h = 30;
        x = (gameX - w) / 2;
        y = 14;
        return true;
    }

    // Three bars, white over a black outline, a little see-through; ruby on
    // a soft plate while pressed, so a touch shows at once.
    void drawIcon(int winW, int winH, int gameX, bool pressed) {
        int x, y, w, h;
        if (!iconRect(gameX, x, y, w, h))
            return;
        if (!icon)
            icon.reset(new QuadArray<CVertex>());
        if (iconBuiltFor != gameX || iconHeight != winH || iconPressed != pressed) {
            iconPressed = pressed;
            icon->clear();
            auto bar = [&](int bx, int by, int bw, int bh, const Vec4 &c) {
                CVertex v[4];
                const int top = PerformanceMetrics::framebufferY(winH, by);
                const int bottom = PerformanceMetrics::framebufferY(winH, by + bh);
                v[0].pos = Vec2(bx, top);
                v[1].pos = Vec2(bx + bw, top);
                v[2].pos = Vec2(bx + bw, bottom);
                v[3].pos = Vec2(bx, bottom);
                for (auto &e : v)
                    e.color = c;
                icon->vertices.insert(icon->vertices.end(), v, v + 4);
                ++icon->quadCount;
            };
            if (pressed)
                bar(x - 9, y - 9, w + 18, h + 18, Vec4(1, 1, 1, 0.22f));
            const int step = 12, thick = 7;
            for (int i = 0; i < 3; ++i)
                bar(x - 2, y + i * step - 2, w + 4, thick + 4, Vec4(0, 0, 0, 0.55f));
            for (int i = 0; i < 3; ++i)
                bar(x, y + i * step, w, thick,
                    pressed ? Vec4(0.86f, 0.13f, 0.25f, 1.f) : Vec4(1, 1, 1, 0.75f));
            icon->commit();
            iconBuiltFor = gameX;
            iconHeight = winH;
        }
        FBO::bind(FBO::ID(0));
        glState.viewport.pushSet(IntRect(0, 0, winW, winH));
        glState.scissorTest.pushSet(false);
        glState.blend.pushSet(true);
        glState.blendMode.pushSet(BlendNormal);
        SimpleColorShader &shader = shState->shaders().simpleColor;
        shader.bind();
        shader.applyViewportProj();
        shader.setTranslation(Vec2i());
        icon->draw();
        glState.blendMode.pop();
        glState.blend.pop();
        glState.scissorTest.pop();
        glState.viewport.pop();
    }
};
