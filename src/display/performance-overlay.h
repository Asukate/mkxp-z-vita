#pragma once
#include "performance-metrics.h"
#include "gl/quadarray.h"
#include "glstate.h"
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

class PerformanceOverlay {
    PerformanceMetrics::Sampler sampler;
    std::unique_ptr<QuadArray<CVertex>> quads;
    uint64_t previousSequence = uint64_t(-1);
    int previousMode = -1;
    int framebufferHeight = 0;
    PerformanceMetrics::Placement previousPlacement{};
    unsigned resumeGeneration = 0;
    bool generationSet = false;

    static const unsigned char *glyph(char ch) {
        static const char keys[] = "0123456789FPSLIMTRAE K.-+>";
        static const unsigned char rows[][7] = {
            {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},
            {14,17,1,2,4,8,31},{30,1,1,14,1,1,30},
            {2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
            {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},
            {14,17,17,14,17,17,14},{14,17,17,15,1,1,14},
            {31,16,16,30,16,16,16},{30,17,17,30,16,16,16},
            {15,16,16,14,1,1,30},{16,16,16,16,16,16,31},
            {14,4,4,4,4,4,14},{17,27,21,21,17,17,17},
            {31,4,4,4,4,4,4},{30,17,17,30,20,18,17},
            {14,17,17,31,17,17,17},{31,16,16,30,16,16,31},
            {0,0,0,0,0,0,0},{17,18,20,24,20,18,17},
            {0,0,0,0,0,12,12},{0,0,0,31,0,0,0},
            {0,4,4,31,4,4,0},{16,8,4,2,4,8,16}};
        const char *p = std::strchr(keys, ch);
        return rows[p ? p - keys : 20];
    }
    void rectangle(int x, int y, int w, int h, const Vec4 &color) {
        CVertex vertices[4];
        const int top = PerformanceMetrics::framebufferY(framebufferHeight, y);
        const int bottom = PerformanceMetrics::framebufferY(framebufferHeight, y + h);
        vertices[0].pos = Vec2(x, top);
        vertices[1].pos = Vec2(x + w, top);
        vertices[2].pos = Vec2(x + w, bottom);
        vertices[3].pos = Vec2(x, bottom);
        for (auto &v : vertices) v.color = color;
        quads->vertices.insert(quads->vertices.end(), vertices, vertices + 4);
        ++quads->quadCount;
    }
public:
    void reset() { sampler.reset(); }
    void present(uint64_t now, unsigned generation) {
        if (generationSet && generation != resumeGeneration) sampler.reset();
        generationSet = true;
        resumeGeneration = generation;
        sampler.present(now);
    }
    void draw(int mode, int width, int height,
              int gameX, int gameY, int gameWidth, int gameHeight) {
        if (!mode) return;
        auto placement = PerformanceMetrics::place(width, height, gameX, gameY,
                                                    gameWidth, gameHeight, mode == 2);
        const auto &r = sampler.reading();
        if (!quads) {
            quads.reset(new QuadArray<CVertex>());
            quads->vertices.reserve(4096);
        }
        if (sampler.serial() != previousSequence || mode != previousMode ||
            placement.x != previousPlacement.x ||
            placement.y != previousPlacement.y || placement.width != previousPlacement.width ||
            placement.height != previousPlacement.height || framebufferHeight != height) {
            framebufferHeight = height;
            const std::string text = PerformanceMetrics::panelText(r, mode == 2);
            quads->clear();
            rectangle(placement.x, placement.y, placement.width, placement.height, Vec4(0,0,0,1));
            int x = placement.x + 6, y = placement.y + 6;
            for (char c : text) {
                if (c == '\n') { x = placement.x + 6; y += 18; continue; }
                const auto *bits = glyph(c);
                for (int row = 0; row < 7; ++row)
                    for (int column = 0; column < 5; ++column)
                        if (bits[row] & (16 >> column))
                            rectangle(x + column * 2, y + row * 2, 2, 2, Vec4(1,1,1,1));
                x += 12;
            }
            quads->commit();
            previousSequence = sampler.serial();
            previousMode = mode;
            previousPlacement = placement;
        }
        FBO::bind(FBO::ID(0));
        glState.viewport.pushSet(IntRect(0, 0, width, height));
        glState.scissorTest.pushSet(false);
        glState.blend.pushSet(false);
        SimpleColorShader &shader = shState->shaders().simpleColor;
        shader.bind();
        shader.applyViewportProj();
        shader.setTranslation(Vec2i());
        quads->draw();
        glState.blend.pop();
        glState.scissorTest.pop();
        glState.viewport.pop();
    }
};
