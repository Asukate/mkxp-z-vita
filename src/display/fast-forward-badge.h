#pragma once
#include "performance-metrics.h"
#include "gl/quadarray.h"
#include "glstate.h"
#include <memory>

// The two chevrons drawn in the top right corner while fast forward is on:
// white with a black outline, so they read on light and dark pictures.
class FastForwardBadge {
    std::unique_ptr<QuadArray<CVertex>> quads;
    int builtWidth = 0, builtHeight = 0;

    void rectangle(int height, int x, int y, int w, int h, const Vec4 &color) {
        CVertex vertices[4];
        const int top = PerformanceMetrics::framebufferY(height, y);
        const int bottom = PerformanceMetrics::framebufferY(height, y + h);
        vertices[0].pos = Vec2(x, top);
        vertices[1].pos = Vec2(x + w, top);
        vertices[2].pos = Vec2(x + w, bottom);
        vertices[3].pos = Vec2(x, bottom);
        for (auto &v : vertices) v.color = color;
        quads->vertices.insert(quads->vertices.end(), vertices, vertices + 4);
        ++quads->quadCount;
    }
    // One chevron: rows of 2x2 cells stepping right to the middle row and back.
    void chevrons(int height, int left, int top, int grow, const Vec4 &color) {
        const int half = 6, cell = 2, thick = 3;
        for (int c = 0; c < 2; ++c)
            for (int row = 0; row <= half * 2; ++row) {
                int step = half - (row > half ? row - half : half - row);
                rectangle(height, left + c * 16 + step * cell - grow, top + row * cell - grow,
                          thick * cell + grow * 2, cell + grow * 2, color);
            }
    }

public:
    void draw(int width, int height) {
        if (!quads) {
            quads.reset(new QuadArray<CVertex>());
            quads->vertices.reserve(256);
        }
        if (width != builtWidth || height != builtHeight) {
            quads->clear();
            const int left = width - 50, top = 10;
            chevrons(height, left, top, 2, Vec4(0, 0, 0, 1));
            chevrons(height, left, top, 0, Vec4(1, 1, 1, 1));
            quads->commit();
            builtWidth = width;
            builtHeight = height;
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
