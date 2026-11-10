/*
** bitmap.h
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

#ifndef BITMAP_H
#define BITMAP_H

#include <stddef.h>
#include "disposable.h"
#include "etc-internal.h"
#include "etc.h"

#include "sigslot/signal.hpp"

class Font;
class ShaderBase;
struct TEXFBO;
struct SDL_Surface;

struct BitmapPrivate;
// FIXME make this class use proper RGSS classes again
class Bitmap : public Disposable
{
public:
	Bitmap(const char *filename);
	Bitmap(int width, int height, bool isHires = false);
	Bitmap(void *pixeldata, int width, int height);
	Bitmap(TEXFBO &other);
	Bitmap(SDL_Surface *imgSurf, SDL_Surface *imgSurfHires, bool forceMega = false);

	/* Clone constructor */
    
    // frame is -2 for "any and all", -1 for "current", anything else for a specific frame
	Bitmap(const Bitmap &other, int frame = -2);
	~Bitmap();

	void initFromSurface(SDL_Surface *imgSurf, Bitmap *hiresBitmap, bool forceMega = false);

	int width()  const;
	int height() const;
	bool hasHires() const;
	DECL_ATTR(Hires, Bitmap*)
	void setLores(Bitmap *lores);
	bool isMega() const;
	/* Changes on every write to the pixels (cache keys for game patches). */
	uint32_t contentVersion() const;
    bool isAnimated() const;

	IntRect rect() const;

	void blt(int x, int y,
	         const Bitmap &source, const IntRect &rect,
	         int opacity = 255);

	enum BitmapBltMode {
	    NORMAL,
	    KGL_SUBTRACT,
	};

	void stretchBlt(IntRect destRect,
	                const Bitmap &source, IntRect sourceRect,
	                int opacity = 255, bool smooth = false,
			enum BitmapBltMode mode = NORMAL);

	void fillRect(int x, int y,
	              int width, int height,
	              const Vec4 &color);
	void fillRect(const IntRect &rect, const Vec4 &color);

	void gradientFillRect(int x, int y,
	                      int width, int height,
	                      const Vec4 &color1, const Vec4 &color2,
	                      bool vertical = false);
	void gradientFillRect(const IntRect &rect,
	                      const Vec4 &color1, const Vec4 &color2,
	                      bool vertical = false);

	void clearRect(int x, int y,
	               int width, int height);
	void clearRect(const IntRect &rect);

	void blur();
	void radialBlur(int angle, int divisions);

	void clear();
	void clearRects(const IntRect *rects, size_t count);

#ifdef __vita__
    struct OpaqueLayer { Bitmap *source; int x, y, width, height, opacity; };
    static constexpr size_t maxOpaqueCommands = 64;
    bool tryComposeOpaque(const Vec4 &background, const OpaqueLayer *layers, size_t count);
#endif

	Color getPixel(int x, int y) const;
	void setPixel(int x, int y, const Color &color);
    
    bool getRaw(void *output, int output_size);
    void replaceRaw(void *pixel_data, int size);
    void saveToFile(const char *filename);

	void hueChange(int hue);

	enum TextAlign
	{
		Left = 0,
		Center = 1,
		Right = 2
	};

	void drawText(int x, int y,
	              int width, int height,
	              const char *str, int align = Left);

	void drawText(const IntRect &rect,
	              const char *str, int align = Left);

	IntRect textSize(const char *str);

	DECL_ATTR(Font, Font&)

	/* Sets initial reference without copying by value,
	 * use at construction */
	void setInitFont(Font *value);

	/* <internal> */
	TEXFBO &getGLTypes() const;
	/* Cover the whole bitmap with a texture owned elsewhere (a decoded
	 * video frame), scaled to fit. */
	void drawExternalTexture(unsigned int glTexture, int texWidth, int texHeight);
    SDL_Surface *surface() const;
	SDL_Surface *megaSurface() const;
	void ensureNonMega() const;
    void ensureNonAnimated() const;
    void ensureAnimated() const;
    
    // Animation functions
    void stop();
    void play();
    bool isPlaying() const;
    void gotoAndStop(int frame);
    void gotoAndPlay(int frame);
    int numFrames() const;
    int currentFrameI() const;
    
    int addFrame(Bitmap &source, int position = -1);
    void removeFrame(int position = -1);
    
    void nextFrame();
    void previousFrame();
    std::vector<TEXFBO> &getFrames() const;
    
    void setAnimationFPS(float FPS);
    float getAnimationFPS() const;
    
    void setLooping(bool loop);
    bool getLooping() const;

    void ensureNotPlaying() const;

    void kglInvert();
    void kglCompressAlpha();
    int kglShadowShaderH(int x1, int x2, int y, bool soft);
    int kglShadowShaderV(int y1, int y2, int x, bool wall, bool soft);

    // ----------
    
	/* Binds the backing texture and sets the correct
	 * texture size uniform in shader */
	void bindTex(ShaderBase &shader, bool substituteLoresSize = true, bool allowImageCrop = false);
	/* Banded images (too large for one allocation; Vita): bands to draw one
	 * at a time, after bindTex with allowImageCrop. 0 for any other bitmap. */
	int imageBandCount() const;
	void imageBandRows(int index, int &ownTop, int &ownBottom) const;
	void bindImageBand(ShaderBase &shader, int index);
	/* Draw the visible part of oversized CPU contents with bounded textures.
	 * Returns false when the ordinary texture path should be used. */
	bool drawCpuWindow(ShaderBase &shader, const IntRect &visible, float opacity);

	/* Adds 'rect' to tainted area */
	void taintArea(const IntRect &rect);

#ifdef __vita__
	sigslot::signal_st<> modified;
#else
	sigslot::signal<> modified;
#endif

	static int maxSize();
	static void reclaimMemory();
	static void releaseRecreatableMemory();
	/* Textures of disposed, unchanged file images kept for the next load of
	 * the same file (see "Reused file images" in bitmap.cpp). */
	static void releaseReusedImages();
	/* True when a disposed image of this name is kept for reuse. */
	static bool hasReusedImage(const char *name);
	static void suspendImageReuse(bool suspend);
	/* Frees kept images, oldest first, until this much graphics memory is
	 * free or none are left; true if any were freed. Under GFX_LOCK. */
	static bool trimReusedImages(size_t wantFree);
	/* Lets go of kept images of 8 MB or more while kernel memory is short (Vita). */
	static void trimReusedForKernel();
	static void frameTick();
	static bool cpuPixelsNearBudget();
	/* True once after a bitmap could not get graphics memory while drawing
	 * and was skipped for that frame; the next Ruby safe point reclaims. */
	static bool takeReclaimRequest();
	static size_t cpuPixelBytes();

    void assumeRubyGC();

private:
	void releaseResources();
	sigslot::connection loresDispCon;
	const char *klassName() const { return "bitmap"; }

	BitmapPrivate *p;

	void loresDisposal();
};

#endif // BITMAP_H
