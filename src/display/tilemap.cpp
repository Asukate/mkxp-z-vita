/*
** tilemap.cpp
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

#include "tilemap.h"

#include "viewport.h"
#include "bitmap.h"
#include "table.h"

#include "sharedstate.h"
#include "config.h"
#include "debugwriter.h"
#include "glstate.h"
#include "gl-util.h"
#include "gl-meta.h"
#include "global-ibo.h"
#include "etc-internal.h"
#include "quadarray.h"
#include "texpool.h"
#include "quad.h"
#include "vertex.h"
#include "tileatlas.h"
#include "tilemap-common.h"

#include "sigslot/signal.hpp"

#ifdef __vita__
#include <psp2/gxm.h>
extern "C" void *vglGetTexDataPointerByID(unsigned int texture);
extern "C" void vglGetTexDescriptor(unsigned int id, int *w, int *h, int *stride,
                                    int *format, int *mipcount, int *swizzle,
                                    unsigned int *flags, void **data);
extern "C" SceGxmTexture *vglGetGxmTextureByID(unsigned int texture);
extern "C" void *vglGetFramebufferColorData(unsigned int fbo);
#endif

#include <string.h>
#include <stdint.h>
#include <algorithm>
#include <vector>

#include <SDL_surface.h>

extern const StaticRect autotileRects[];

typedef std::vector<SVertex> SVVector;

static const int tilesetW  = 8 * 32;
static const int autotileW = 3 * 32;
static const int autotileH = 4 * 32;

static const int autotileCount = 7;

static const int atFrames = 8;
static const int atFrameDur = 15;
static const int atAreaW = autotileW * atFrames;
//static const int atAreaH = autotileH * autotileCount;

static const int tsLaneW = tilesetW / 1;

/* Map viewport size */
static const int viewpW = 21;
static const int viewpH = 16;

static const size_t zlayersMax = viewpH + 5;

/* Vocabulary:
 *
 * Atlas: A texture containing both the tileset and all
 *   autotile images. This is so the entire tilemap can
 *   be drawn from one texture (for performance reasons).
 *   This means that we have to watch the 'modified' signals
 *   of all Bitmaps that make up the atlas, and update it
 *   as required during runtime.
 *   The atlas is tightly packed, with the autotiles located
 *   in the top left corener and the tileset image filing the
 *   remaining open space (below the autotiles as well as
 *   besides it). The tileset is vertically cut in half, where
 *   the first half fills available texture space, and then the
 *   other half (as if the right half was cut and pasted below
 *   the left half before fitting it all into the atlas).
 *   Internally these halves are called "tileset lanes".
 *   There is a 32 pixel wide empty buffer below the autotile
 *   area so the vertex shader can safely differentiate between
 *   autotile and tileset vertices (relevant for autotile animation).
 *
 *                  Tile atlas
 *   *-----------------------*--------------*
 *   |     |     |     |     |       ¦       |
 *   | AT0 | AT0 | AT0 | AT0 |       ¦       |
 *   | FR0 | FR1 | FR2 | FR3 |   |   ¦   |   |
 *   |-----|-----|-----|-----|   v   ¦   v   |
 *   |     |     |     |     |       ¦       |
 *   | AT1 |     |     |     |       ¦       |
 *   |     |     |     |     |       ¦       |
 *   |-----|-----|-----|-----|       ¦       |
 *   |[...]|     |     |     |       ¦       |
 *   |-----|-----|-----|-----|       ¦       |
 *   |     |     |     |     |   |   ¦   |   |
 *   | AT6 |     |     |     |   v   ¦   v   |
 *   |     |     |     |     |       ¦       |
 *   |-----|-----|-----|-----|       ¦       |
 *   |      Empty space      |       |       |
 *   |-----------------------|       |       |
 *   |       ¦       ¦       ¦       ¦       |
 *   | Tile- ¦   |   ¦   |   ¦       ¦       |
 *   |  set  ¦   v   ¦   v   ¦       ¦       |
 *   |       ¦       ¦       ¦   |   ¦   |   |
 *   |   |   ¦       ¦       ¦   v   ¦   v   |
 *   |   v   ¦   |   ¦   |   ¦       ¦       |
 *   |       ¦   v   ¦   v   ¦       ¦       |
 *   |       ¦       ¦       ¦       ¦       |
 *   *---------------------------------------*
 *
 *   When allocating the atlas size, we first expand vertically
 *   until all the space immediately below the autotile area
 *   is used up, and then, when the max texture size
 *   is reached, horizontally.
 *
 *   To animate the autotiles, we catch any autotile vertices in
 *   the tilemap shader based on their texcoord, and offset them
 *   horizontally by (animation index) * (autotile frame width = 96).
 *
 * Elements:
 *   Even though the Tilemap carries similarities with other
 *   SceneElements, it is not one itself but composed of multiple
 *   such elements (GroundLayer and ZLayers).
 *
 * GroundLayer:
 *   Every tile with priority=0 is drawn at z=0, so we
 *   collect all such tiles in one big quad array and
 *   draw them at once.
 *
 * ZLayer:
 *   Each tile in row n with priority=m is drawn at the same
 *   z as every tile in row n-1 with priority=m-1. This means
 *   we can collect all tiles sharing the same z in one quad
 *   array and draw them at once. I call these collections
 *   'zlayers'. They're drawn from the top part of the map
 *   (lowest z) to the bottom part (highest z).
 *   Objects that would end up on the same zlayer are eg. trees.
 *
 * Map viewport:
 *   This rectangle describes the subregion of the map that is
 *   actually translated to vertices and stored on the GPU ready
 *   for rendering. Whenever, ox/oy are modified, its position is
 *   adjusted if necessary and the data is regenerated. Its size
 *   is fixed. This is NOT related to the RGSS Viewport class!
 *
 */

/* Autotile animation */
// static const uint8_t atAnimation[16*8] =
// {
//     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
//     1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
//     2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
//     3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
//     4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
//     5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
//     6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
//     7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7
// };

// static elementsN(atAnimation);

/* Flash tiles pulsing opacity */
static const uint8_t flashAlpha[] =
{
	/* Fade in */
	0x3C, 0x3C, 0x3C, 0x3C, 0x4B, 0x4B, 0x4B, 0x4B,
	0x5A, 0x5A, 0x5A, 0x5A, 0x69, 0x69, 0x69, 0x69,
	/* Fade out */
	0x78, 0x78, 0x78, 0x78, 0x69, 0x69, 0x69, 0x69,
	0x5A, 0x5A, 0x5A, 0x5A, 0x4B, 0x4B, 0x4B, 0x4B
};

static elementsN(flashAlpha);

struct GroundLayer : public ViewportElement
{
	GLsizei vboCount;
	TilemapPrivate *p;

	GroundLayer(TilemapPrivate *p, Viewport *viewport);

	void updateVboCount();

	void draw();
	void drawInt();

	void onGeometryChange(const Scene::Geometry &geo);

	ABOUT_TO_ACCESS_NOOP
};

struct ZLayer : public ViewportElement
{
	size_t index;
	GLintptr vboOffset;
	GLsizei vboCount;
	TilemapPrivate *p;

	/* If this layer is part of a batch and not
	 * the head, it is 'muted' via this flag */
	bool batchedFlag;

	/* If this layer is a batch head, this variable
	 * holds the element count of the entire batch */
	GLsizei vboBatchCount;

	ZLayer(TilemapPrivate *p, Viewport *viewport);

	void setIndex(int value);

	void draw();
	void drawInt();

	static int calculateZ(TilemapPrivate *p, int index);

	void initUpdateZ();
	void finiUpdateZ(ZLayer *prev);

	ABOUT_TO_ACCESS_NOOP
};

struct TilemapPrivate
{
	Viewport *viewport;

	Bitmap *autotiles[autotileCount];

	Bitmap *tileset;

	Table *mapData;
	Table *priorities;
	bool visible;
	Vec2i origin;

	Vec2i dispPos;

	/* Tile atlas */
	struct {
		TEXFBO gl;

		Vec2i size;

		/* Effective tileset height,
		 * clamped to a multiple of 32 */
		int efTilesetH;

		/* Indices of usable
		 * (not null, not disposed) autotiles */
		std::vector<uint8_t> usableATs;

		/* Indices of animated autotiles */
		std::vector<uint8_t> animatedATs;

		/* Whether each autotile is 3x4 or not */
		bool smallATs[autotileCount] = {false};

		/* The number of frames for each autotile */
		int nATFrames[autotileCount] = {1};
	} atlas;

#ifdef __vita__
	/*
	 * FBO that was active before atlas allocation/build temporarily bound its
	 * own target. TEXFBO::linkFBO() leaves the newly linked atlas bound, so
	 * merely sampling FBO::boundFramebufferID at buildAtlas() can otherwise
	 * mistake the atlas itself for the caller target.
	 */
	FBO::ID vitaAtlasCallerFbo;
#endif

	/* Map viewport position */
	Vec2i viewpPos;

	/* Ground layer vertices */
	SVVector groundVert;

	/* ZLayer vertices */
	SVVector zlayerVert[zlayersMax];

	/* Base quad indices of each zlayer
	 * in the shared buffer */
	size_t zlayerBases[zlayersMax+1];

	/* Shared buffers for all tiles */
	struct
	{
		GLMeta::VAO vao;
		VBO::ID vbo;
		bool animated;

		/* Animation state */
		uint32_t aniIdx;
	} tiles;

	FlashMap flashMap;
	uint8_t flashAlphaIdx;

	/* Scene elements */
	struct
	{
		GroundLayer *ground;
		ZLayer* zlayers[zlayersMax];
		/* Used layers out of 'zlayers' (rest is hidden) */
		size_t activeLayers;
		Scene::Geometry sceneGeo;
	} elem;

	/* Affected by: autotiles, tileset */
	bool atlasSizeDirty;
	/* Affected by: autotiles(.changed), tileset(.changed), allocateAtlas */
	bool atlasDirty;
	/* Affected by: mapData(.changed), priorities(.changed) */
	bool buffersDirty;
	/* Affected by: ox, oy */
	bool mapViewportDirty;
	/* Affected by: oy */
	bool zOrderDirty;

	/* Resources are sufficient and tilemap is ready to be drawn */
	bool tilemapReady;

	/* Change watches */
	sigslot::connection tilesetCon;
	sigslot::connection autotilesCon[autotileCount];
	sigslot::connection mapDataCon;
	sigslot::connection prioritiesCon;

	/* Dispose watches */
	sigslot::connection autotilesDispCon[autotileCount];

	sigslot::connection tilesetDispCon;

	/* Draw prepare call */
	sigslot::connection prepareCon;

	NormValue opacity;
	BlendType blendType;
	Color *color;
	Tone *tone;

	EtcTemps tmp;

	TilemapPrivate(Viewport *viewport)
	    : viewport(viewport),
	      tileset(0),
	      mapData(0),
	      priorities(0),
	      visible(true),
	      flashAlphaIdx(0),
	      atlasSizeDirty(false),
	      atlasDirty(false),
	      buffersDirty(false),
	      mapViewportDirty(false),
	      zOrderDirty(false),
	      tilemapReady(false),

		  opacity(255),
	      blendType(BlendNormal),
	      color(&tmp.color),
	      tone(&tmp.tone)
	{
		memset(autotiles, 0, sizeof(autotiles));

		atlas.animatedATs.reserve(autotileCount);
		atlas.efTilesetH = 0;

		tiles.animated = false;
		tiles.aniIdx = 0;

		/* Init tile buffers */
		tiles.vbo = VBO::gen();

		GLMeta::vaoFillInVertexData<SVertex>(tiles.vao);
		tiles.vao.vbo = tiles.vbo;
		tiles.vao.ibo = shState->globalIBO().ibo;

		GLMeta::vaoInit(tiles.vao);

		elem.ground = new GroundLayer(this, viewport);

		for (size_t i = 0; i < zlayersMax; ++i)
			elem.zlayers[i] = new ZLayer(this, viewport);

		prepareCon = shState->prepareDraw.connect
		        (&TilemapPrivate::prepare, this);

		updateFlashMapViewport();
	}

	~TilemapPrivate()
	{
		/* Destroy elements */
		delete elem.ground;
		for (size_t i = 0; i < zlayersMax; ++i)
			delete elem.zlayers[i];

		shState->releaseAtlasTex(atlas.gl);

		/* Destroy tile buffers */
		GLMeta::vaoFini(tiles.vao);
		VBO::del(tiles.vbo);

		/* Disconnect signal handlers */
		tilesetCon.disconnect();
		tilesetDispCon.disconnect();
		for (int i = 0; i < autotileCount; ++i)
		{
			autotilesCon[i].disconnect();
			autotilesDispCon[i].disconnect();
		}
		mapDataCon.disconnect();
		prioritiesCon.disconnect();

		prepareCon.disconnect();
	}

	void updateFlashMapViewport()
	{
		flashMap.setViewport(IntRect(viewpPos, Vec2i(viewpW, viewpH)));
	}

	void updateAtlasInfo()
	{
		if (nullOrDisposed(tileset))
		{
			atlas.size = Vec2i();
			return;
		}

		int tsH = tileset->height();
		atlas.efTilesetH = tsH - (tsH % 32);

		atlas.size = TileAtlas::minSize(atlas.efTilesetH, glState.caps.maxTexSize);

		if (atlas.size.x < 0)
			throw Exception(Exception::MKXPError,
		                    "Cannot allocate big enough texture for tileset atlas");
	}

	void updateAutotileInfo()
	{
		/* Check if and which autotiles are animated */
		std::vector<uint8_t> &usableATs = atlas.usableATs;
		std::vector<uint8_t> &animatedATs = atlas.animatedATs;

		usableATs.clear();
		animatedATs.clear();

		for (int i = 0; i < autotileCount; ++i)
		{
			if (nullOrDisposed(autotiles[i]) || autotiles[i]->megaSurface())
			{
				atlas.nATFrames[i] = 1;
				continue;
			}

			usableATs.push_back(i);

			if (autotiles[i]->height() == 32)
			{
				atlas.smallATs[i] = true;
				atlas.nATFrames[i] = autotiles[i]->width()/32;
				animatedATs.push_back(i);
			}
			else
			{
				atlas.smallATs[i] = false;
				atlas.nATFrames[i] = autotiles[i]->width()/autotileW;
				if (atlas.nATFrames[i] > 1)
					animatedATs.push_back(i);
			}
		}

		tiles.animated = !animatedATs.empty();
	}

	void updateSceneGeometry(const Scene::Geometry &geo)
	{
		elem.sceneGeo = geo;
		mapViewportDirty = true;
	}

	void invalidateAtlasSize()
	{
		atlasSizeDirty = true;
	}

	void invalidateAtlasContents()
	{
		atlasDirty = true;
	}

	void atlasContentsDisposal(int i)
	{
		// Guard against deleted bitmaps
		autotiles[i] = 0;
		
		invalidateAtlasContents();
	}

	void tilesetDisposal()
	{
		tileset = 0;
		tilesetDispCon.disconnect();
	}

	void invalidateBuffers()
	{
		buffersDirty = true;
	}

	/* Checks for the minimum amount of data needed to display */
	bool verifyResources()
	{
		if (nullOrDisposed(tileset))
			return false;

		if (!mapData)
			return false;

		return true;
	}

	/* Allocates correctly sized TexFBO for atlas */
	void allocateAtlas()
	{
		updateAtlasInfo();

#ifdef __vita__
		/*
		 * Preserve the real caller render target. requestAtlasTex() may create a
		 * fresh TEXFBO, and TEXFBO::linkFBO() leaves that atlas FBO bound.
		 */
		if (FBO::boundFramebufferID != atlas.gl.fbo)
			vitaAtlasCallerFbo = FBO::boundFramebufferID;
#endif

		/* Aquire atlas tex */
		shState->releaseAtlasTex(atlas.gl);
		shState->requestAtlasTex(atlas.size.x, atlas.size.y, atlas.gl);

#ifdef __vita__
		/* Do not leak the allocation helper's atlas binding into buildAtlas(). */
		if (FBO::boundFramebufferID == atlas.gl.fbo &&
		    vitaAtlasCallerFbo != atlas.gl.fbo)
			FBO::bind(vitaAtlasCallerFbo);
#endif

		atlasDirty = true;
	}

#ifdef __vita__
	/* BUILD 9 (read-only atlas forensics, BUG-A): TexSubImage in
	 * vitaGL is a CPU memcpy into malloc'd backing, so the atlas
	 * content is directly readable after each upload stage. Dump
	 * per-128-row-band checksums (bands 0-6 = autotile slots,
	 * band 7 = first lane rows) at postATraw (after autotile
	 * uploads), postAT (after the mid-Finish), postLane (after lane
	 * uploads), postBuild (end of buildAtlas). No behavior change.
	 * BUILD 11: also called as postBind (after TEX::bind, before
	 * any upload) and stepAT0..6 (after each autotile's uploads)
	 * to pinpoint the exact call that first shows spread.
	 * BUILD 10: also log all three backing pointers. The sampler
	 * reads gxmData, the FBO renders into fboData, uploads land in
	 * base (tex->data). Any divergence is the bug; when gxmData
	 * diverges, checksum its band 2 (rug rows) too. */
	static void tm9AtRead(const char *point, GLuint atlasTex, GLuint atlasFbo)
	{
		if (!vitaDiagAtlasTraceEnabled()) return;
		FrameProfile::Scope profile(FrameProfile::Diagnostics);
		int tw = 0, th = 0, tstride = 0, tfmt = 0, tmips = 0;
		vglGetTexDescriptor(atlasTex, &tw, &th, &tstride, &tfmt, &tmips, 0, 0, 0);
		uint8_t *base = static_cast<uint8_t*>(vglGetTexDataPointerByID(atlasTex));
		if (!base)
		{
			vitaDiagLog("TMXP", "ATREAD %s NOBASE tw=%d th=%d stride=%d fmt=0x%x",
			            point, tw, th, tstride, (unsigned)tfmt);
			return;
		}
		const int rowPx = (tw + 7) & ~7;
		SceGxmTexture *gxm = vglGetGxmTextureByID(atlasTex);
		uint8_t *gxmData = gxm ? static_cast<uint8_t*>(sceGxmTextureGetData(gxm)) : 0;
		void *fboData = vglGetFramebufferColorData(atlasFbo);
		vitaDiagLog("TMXP", "ATREAD %s ptrs tex=%p gxm=%p fbo=%p",
		            point, base, gxmData, fboData);
		if (gxmData && gxmData != base && th >= 384)
		{
			uint32_t gfnv = 2166136261u;
			for (int y = 256; y < 384; ++y)
			{
				const uint8_t *row = gxmData + (size_t)y * (size_t)rowPx * 4;
				for (int x = 0; x < tw * 4; x += 64)
					gfnv = (gfnv ^ row[x]) * 16777619u;
			}
			const size_t goff = (size_t)256 * (size_t)rowPx * 4;
			vitaDiagLog("TMXP", "GXMMEM %s band=2 gfnv=%08x g0=%02x%02x%02x%02x",
			            point, gfnv, gxmData[goff], gxmData[goff + 1],
			            gxmData[goff + 2], gxmData[goff + 3]);
		}
		for (int band = 0; band < 8; ++band)
		{
			const int y0 = band * 128;
			if (y0 + 127 >= th)
				break;
			uint32_t fnv = 2166136261u;
			for (int y = y0; y < y0 + 128; ++y)
			{
				const uint8_t *row = base + (size_t)y * (size_t)rowPx * 4;
				for (int x = 0; x < tw * 4; x += 64)
					fnv = (fnv ^ row[x]) * 16777619u;
			}
			const uint8_t *r0 = base + (size_t)y0 * (size_t)rowPx * 4;
			unsigned bmin = 255, bmax = 0;
			uint64_t bsum = 0;
			for (int x = 0; x < tw * 4; ++x)
			{
				const unsigned v = r0[x];
				if (v < bmin)
					bmin = v;
				if (v > bmax)
					bmax = v;
				bsum += v;
			}
			vitaDiagLog("TMXP", "ATREAD %s band=%d y=%d fnv=%08x b0=%02x%02x%02x%02x min=%u max=%u mean=%u",
			            point, band, y0, fnv, r0[0], r0[1], r0[2], r0[3],
			            bmin, bmax, (unsigned)(bsum / ((size_t)tw * 4)));
		}
		vitaDiagLog("TMXP", "ATREAD %s geom tw=%d th=%d stride=%d fmt=0x%x",
		            point, tw, th, tstride, (unsigned)tfmt);
	}
#endif

	/* Assembles atlas from tileset and autotile bitmaps */
	void buildAtlas()
	{
        updateAutotileInfo();
        tileset->ensureNonAnimated();

		TileAtlas::BlitVec blits = TileAtlas::calcBlits(atlas.efTilesetH, atlas.size);

		/* Clear atlas */
#ifdef __vita__
		/*
		 * BUILD 13: remember the caller's render target.  Vita's SGX/GXM is a
		 * tile-based renderer: an FBO scene may still own on-chip tile contents
		 * after its color texture backing has been modified by CPU TexSubImage
		 * calls.  Ending that scene can then store stale tile memory back over
		 * the freshly uploaded atlas.  Builds 9-11 showed exactly that shape:
		 * CPU readback was correct until a scene-ending Finish, then the atlas
		 * deterministically became the structured 0x292929ff "spread".
		 *
		 * Finish the clear while the atlas is the target, then RELEASE the atlas
		 * FBO before any CPU texture writes.  We intentionally retain the later
		 * diagnostic Finish calls for this build: if the bytes survive those
		 * finishes with a different FBO current, it isolates FBO scene ownership
		 * as the corruption mechanism.
		 */
		FBO::ID atlasPreviousFbo = FBO::boundFramebufferID;
		if (atlasPreviousFbo == atlas.gl.fbo)
			atlasPreviousFbo = vitaAtlasCallerFbo;
		else
			vitaAtlasCallerFbo = atlasPreviousFbo;
		vitaDiagLog("TMXP", "atlas-fbo-caller current=%u caller=%u atlas=%u",
		            (unsigned)FBO::boundFramebufferID.gl,
		            (unsigned)atlasPreviousFbo.gl,
		            (unsigned)atlas.gl.fbo.gl);
#endif
		FBO::bind(atlas.gl.fbo);
		glState.clearColor.pushSet(Vec4());
		glState.scissorTest.pushSet(false);

		FBO::clear();

		glState.scissorTest.pop();
		glState.clearColor.pop();

#ifdef __vita__
		/*
		 * IMPORTANT ORDER: switch active_write_fb BEFORE Finish.
		 *
		 * vitaGL glFinish() calls scene_reset(), which ends the current GXM
		 * scene and immediately begins a new one on active_write_fb before it
		 * waits.  Finishing while the atlas is still active therefore leaves a
		 * fresh atlas scene open; a later Finish can end that scene after our
		 * CPU uploads and store stale tile memory over them.  Binding the caller
		 * first makes this Finish close/drain the atlas-clear scene and open the
		 * replacement scene on the caller's FBO instead.
		 */
		FBO::bind(atlasPreviousFbo);
		gl.Finish();
		vitaDiagLog("TMXP", "atlas-fbo-release atlas=%u restored=%u before-finish=1",
		            (unsigned)atlas.gl.fbo.gl, (unsigned)atlasPreviousFbo.gl);
#endif

		/* Blit autotiles */
#ifdef __vita__
		/* BUILD 8 (Vita CPU/direct autotile upload, BUG-A): composing
		 * autotile rows through GPU/FBO blits leaves every autotile
		 * row black, while lanes uploaded directly from CPU memory
		 * render fine. Bypass the FBO path and upload the autotile
		 * pixels straight into the atlas texture with the same
		 * mechanism as the lane path. Layout and rects are unchanged.
		 *
		 * Pixel source: the VitaGL linear texture backing
		 * (Bitmap::surface() is null for loaded bitmaps: the SDL
		 * surface is discarded after upload). Rows are padded to a
		 * multiple of 8px (see vitaGL gpu_alloc_texture); bytes are
		 * ABGR8888 in R,G,B,A order, matching a GL_RGBA upload.
		 * vitaGL honors UNPACK_ROW_LENGTH in TexSubImage2D (but
		 * ignores the SKIPs), so each source rect is addressed by
		 * offsetting the base pointer explicitly. */
		TEX::bind(atlas.gl.tex);

		/* BUILD 11: atlas content after TEX::bind, before any
		 * upload. Spread here (vs zeros at clear) indicts the
		 * bind itself. */
		tm9AtRead("postBind", atlas.gl.tex.gl, atlas.gl.fbo.gl);

		for (size_t i = 0; i < atlas.usableATs.size(); ++i)
		{
			const uint8_t atInd = atlas.usableATs[i];
			Bitmap *autotile = autotiles[atInd];
			autotile->ensureNonAnimated();

			int atW = autotile->width();
			int atH = autotile->height();
			int blitW = std::min(atW, atAreaW);
			int blitH = std::min(atH, autotileH);

			uint8_t *backing = static_cast<uint8_t*>(
			    vglGetTexDataPointerByID(autotile->getGLTypes().tex.gl));
			if (!backing)
			{
				vitaDiagLog("TMXP", "autotile-direct at=%d NOBACKING", (int)atInd);
				continue;
			}

			const int rowLen = (atW + 7) & ~7;

			uint32_t fnv = 2166136261u;
			const size_t nbytes = (size_t)rowLen * (size_t)atH * 4;
			for (size_t b = 0; b < nbytes; b += 64)
				fnv = (fnv ^ backing[b]) * 16777619u;

			/* BUILD 8D: characterize the backing (8C proved rows+upload
			 * work, so the AT bytes themselves must be wrong). Log GPU
			 * format/dims/stride, first bytes, and value spread to tell
			 * palette indices apart from resolved art or zeros. */
			int tw = 0, th = 0, tstride = 0, tfmt = 0, tmips = 0;
			vglGetTexDescriptor(autotile->getGLTypes().tex.gl,
			                    &tw, &th, &tstride, &tfmt, &tmips, 0, 0, 0);
			unsigned bmin = 255, bmax = 0;
			uint64_t bsum = 0;
			for (size_t b = 0; b < nbytes; ++b)
			{
				const unsigned v = backing[b];
				if (v < bmin)
					bmin = v;
				if (v > bmax)
					bmax = v;
				bsum += v;
			}

			gl.PixelStorei(GL_UNPACK_ROW_LENGTH, rowLen);

			vitaDiagLog("TMXP", "autotile-direct at=%d src=%dx%d dstY=%d fnv=%08x",
			            (int)atInd, blitW, blitH, (int)(atInd*autotileH), fnv);
			vitaDiagLog("TMXP", "atdump at=%d gpu=%dx%d stride=%d fmt=0x%x mips=%d "
			            "surf=%p frames=%d b0=%02x%02x%02x%02x min=%u max=%u mean=%u",
			            (int)atInd, tw, th, tstride, (unsigned)tfmt, tmips,
			            (void*)autotile->surface(), autotile->numFrames(),
			            backing[0], backing[1], backing[2], backing[3],
			            bmin, bmax, (unsigned)(bsum / (nbytes ? nbytes : 1)));

			/* BUILD 8E: bounce the backing through SDL heap before
			 * upload. Direct TexSubImage from GPU-mapped backing
			 * yields black while the probe (heap src, identical call)
			 * works; single-variable test on source memory. */
			SDL_Surface *bounce = SDL_CreateRGBSurface(0, rowLen, atH, 32,
			    0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
			if (!bounce || !bounce->pixels)
			{
				vitaDiagLog("TMXP", "autotile-direct at=%d NOBOUNCE", (int)atInd);
				if (bounce)
					SDL_FreeSurface(bounce);
				continue;
			}
			for (int r = 0; r < atH; ++r)
				memcpy(static_cast<uint8_t*>(bounce->pixels) + (size_t)r * (size_t)bounce->pitch,
				       backing + (size_t)r * (size_t)rowLen * 4, (size_t)rowLen * 4);
			uint8_t *upsrc = static_cast<uint8_t*>(bounce->pixels);

			/* BUILD 9: FNV over the exact bytes handed to TexSubImage.
			 * Directly comparable to the backing fnv above when
			 * pitch == rowLen*4; a mismatch proves source volatility. */
			{
				uint32_t bfnv = 2166136261u;
				for (int r = 0; r < atH; ++r)
				{
					const uint8_t *brow = static_cast<const uint8_t*>(bounce->pixels) +
					    (size_t)r * (size_t)bounce->pitch;
					for (int b = 0; b < rowLen * 4; b += 64)
						bfnv = (bfnv ^ brow[b]) * 16777619u;
				}
				vitaDiagLog("TMXP", "autotile-direct at=%d bouncefnv=%08x pitch=%d",
				            (int)atInd, bfnv, bounce->pitch);
			}

			if (atW <= autotileW && tiles.animated && !atlas.smallATs[atInd])
			{
				/* Static autotile */
				for (int j = 0; j < atFrames; ++j)
					TEX::uploadSubImage(autotileW*j, atInd*autotileH,
					                     blitW, blitH, upsrc, GL_RGBA);
			}
			else
			{
				/* Animated autotile */
				if (atlas.smallATs[atInd])
				{
					int frames = atW/32;
					for (int j = 0; j < atFrames*autotileH/32; ++j)
					{
						const int sx = 32*(j % frames);
						TEX::uploadSubImage(autotileW*(j % atFrames),
						                     atInd*autotileH + 32*(j / atFrames),
						                     32, 32, upsrc + sx*4, GL_RGBA);
					}
				}
				else
					TEX::uploadSubImage(0, atInd*autotileH,
					                     blitW, blitH, upsrc, GL_RGBA);
			}

			/* BUILD 12: inline peek immediately after this AT's
			 * uploads (no helper call): branch taken, bound
			 * texture id, tex pointer, destination first bytes.
			 * Catches a moved dest or a lost binding red-handed. */
			{
				GLint boundTex = 0;
				gl.GetIntegerv(GL_TEXTURE_BINDING_2D, &boundTex);
				uint8_t *peekBase = static_cast<uint8_t*>(
				    vglGetTexDataPointerByID(atlas.gl.tex.gl));
				const int peekRowPx = (atlas.size.x + 7) & ~7;
				const size_t peekOff = (size_t)((int)atInd * autotileH) *
				    (size_t)peekRowPx * 4;
				vitaDiagLog("TMXP", "ATPEEK at=%d br=%d bound=%d texid=%u tex=%p b0=%02x%02x%02x%02x",
				            (int)atInd,
				            (atW <= autotileW && tiles.animated && !atlas.smallATs[atInd]) ? 0 :
				            (atlas.smallATs[atInd] ? 1 : 2),
				            boundTex, atlas.gl.tex.gl, peekBase,
				            peekBase ? peekBase[peekOff] : 0,
				            peekBase ? peekBase[peekOff + 1] : 0,
				            peekBase ? peekBase[peekOff + 2] : 0,
				            peekBase ? peekBase[peekOff + 3] : 0);
			}

			/* BUILD 11: full atlas readback after EACH autotile's
			 * uploads. The first stepAT block showing spread (vs
			 * art) pinpoints the exact upload that corrupts. */
			{
				static const char *stepNames[7] =
				    {"stepAT0", "stepAT1", "stepAT2", "stepAT3",
				     "stepAT4", "stepAT5", "stepAT6"};
				tm9AtRead(stepNames[atInd & 7], atlas.gl.tex.gl,
				          atlas.gl.fbo.gl);
			}

			SDL_FreeSurface(bounce);
		}

		gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#else
		GLMeta::blitBegin(atlas.gl);

		for (size_t i = 0; i < atlas.usableATs.size(); ++i)
		{
			const uint8_t atInd = atlas.usableATs[i];
			Bitmap *autotile = autotiles[atInd];
            autotile->ensureNonAnimated();

			int atW = autotile->width();
			int atH = autotile->height();
			int blitW = std::min(atW, atAreaW);
			int blitH = std::min(atH, autotileH);

			if (autotile->hasHires()) {
				Debug() << "BUG: High-res Tilemap blit autotiles not implemented";
			}

			GLMeta::blitSource(autotile->getGLTypes());

			if (atW <= autotileW && tiles.animated && !atlas.smallATs[atInd])
			{
				/* Static autotile */
				for (int j = 0; j < atFrames; ++j)
					GLMeta::blitRectangle(IntRect(0, 0, blitW, blitH),
					                      Vec2i(autotileW*j, atInd*autotileH));
			}
			else
			{
				/* Animated autotile */
				if (atlas.smallATs[atInd])
				{
					int frames = atW/32;
					for (int j = 0; j < atFrames*autotileH/32; ++j)
					{
						GLMeta::blitRectangle(IntRect(32*(j % frames), 0, 32, 32),
						                      Vec2i(autotileW*(j % atFrames), atInd*autotileH + 32*(j / atFrames)));
					}
				}
				else
					GLMeta::blitRectangle(IntRect(0, 0, blitW, blitH),
					                      Vec2i(0, atInd*autotileH));
			}
		}

		GLMeta::blitEnd();
#endif

#ifdef __vita__
		/* BUILD 9: atlas content immediately after the autotile
		 * uploads, BEFORE the mid-Finish. */
		tm9AtRead("postATraw", atlas.gl.tex.gl, atlas.gl.fbo.gl);
#endif

#ifdef __vita__
		/* VITA-ATLAS-SYNC (black/flickering autotiles): GXM scenes execute
		 * asynchronously. Without a drain here, the mega-path lane memcpy
		 * below can COW-clone the atlas before the queued autotile blits
		 * execute (clone keeps stale clear pixels; blits land in the freed
		 * backing), and sampling can race the resolve. glFinish submits the
		 * build scene, bumps the finish epoch (lane memcpy then updates in
		 * place), and disarms the upload latch. Rare path: map loads only.
		 * BUILD 11: restored after Build 10 proved moving it kills the
		 * tilemap draw (the exact dependency remains unresolved; the smear
		 * reproduces with and without it in the middle, so it is not the
		 * smear source). */
		gl.Finish();
#endif

#ifdef __vita__
		/* BUILD 9: atlas content after the mid-Finish, before lanes. */
		tm9AtRead("postAT", atlas.gl.tex.gl, atlas.gl.fbo.gl);
#endif

		/* Blit tileset */
		if (tileset->megaSurface())
		{
			/* Mega surface tileset */
			SDL_Surface *tsSurf = tileset->megaSurface();

			if (shState->config().subImageFix)
			{
				/* Implementation for broken GL drivers */
				FBO::bind(atlas.gl.fbo);
				glState.blend.pushSet(false);
				glState.viewport.pushSet(IntRect(0, 0, atlas.size.x, atlas.size.y));

				SimpleShader &shader = shState->shaders().simple;
				shader.bind();
				shader.applyViewportProj();
				shader.setTranslation(Vec2i());

				Quad &quad = shState->gpQuad();

				for (size_t i = 0; i < blits.size(); ++i)
				{
					const TileAtlas::Blit &blitOp = blits[i];

					Vec2i texSize;
					shState->ensureTexSize(tsLaneW, blitOp.h, texSize);
					shState->bindTex();
					GLMeta::subRectImageUpload(tsSurf->w, blitOp.src.x, blitOp.src.y,
					                           0, 0, tsLaneW, blitOp.h, tsSurf, GL_RGBA);

					shader.setTexSize(texSize);
					quad.setTexRect(FloatRect(0, 0, tsLaneW, blitOp.h));
					quad.setPosRect(FloatRect(blitOp.dst.x, blitOp.dst.y, tsLaneW, blitOp.h));

					quad.draw();
				}

				GLMeta::subRectImageEnd();
				glState.viewport.pop();
				glState.blend.pop();
			}
			else
			{
				/* Clean implementation */
				TEX::bind(atlas.gl.tex);

				for (size_t i = 0; i < blits.size(); ++i)
				{
					const TileAtlas::Blit &blitOp = blits[i];

					/* BUILD 9: lane rects; a dst overlapping rows
					 * 0-896 would stomp the autotile rows. */
					vitaDiagLog("TMXP", "LANE src=%d,%d dst=%d,%d w=%d h=%d",
					            blitOp.src.x, blitOp.src.y,
					            blitOp.dst.x, blitOp.dst.y, tsLaneW, blitOp.h);
					GLMeta::subRectImageUpload(tsSurf->w, blitOp.src.x, blitOp.src.y,
					                           blitOp.dst.x, blitOp.dst.y, tsLaneW, blitOp.h, tsSurf, GL_RGBA);
				}

				GLMeta::subRectImageEnd();

#ifdef __vita__
				/* BUILD 9: atlas content after the lane uploads. */
				tm9AtRead("postLane", atlas.gl.tex.gl, atlas.gl.fbo.gl);
#endif
			}

		}
		else
		{
			if (tileset->hasHires()) {
				Debug() << "BUG: High-res Tilemap regular tileset not implemented";
			}

#ifdef __vita__
			/* Keep atlas construction on the CPU upload path. Reopening the
			 * atlas as a render target for normal tiles can overwrite the
			 * autotile rows populated above, outside the requested blit rect.
			 * Use the same linear RGBA backing contract as the autotile upload.
			 */
			gl.Finish();
			const uint8_t *source = static_cast<const uint8_t *>(
			    vglGetTexDataPointerByID(tileset->getGLTypes().tex.gl));
			if (!source)
				throw Exception(Exception::MKXPError, "Tileset texture has no CPU backing");
			const int sourcePitch = (tileset->width() + 7) & ~7;
			TEX::bind(atlas.gl.tex);
			gl.PixelStorei(GL_UNPACK_ROW_LENGTH, sourcePitch);
			for (size_t i = 0; i < blits.size(); ++i)
			{
				const TileAtlas::Blit &op = blits[i];
				const uint8_t *pixels = source +
				    ((size_t)op.src.y * sourcePitch + op.src.x) * 4;
				TEX::uploadSubImage(op.dst.x, op.dst.y, tsLaneW, op.h, pixels, GL_RGBA);
				vitaDiagLog("TMXP", "atlas-normal-cpu src=%d,%d dst=%d,%d size=%d,%d",
				            op.src.x, op.src.y, op.dst.x, op.dst.y, tsLaneW, op.h);
			}
			gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#else
			/* Regular tileset */
			GLMeta::blitBegin(atlas.gl);
			GLMeta::blitSource(tileset->getGLTypes());

			for (size_t i = 0; i < blits.size(); ++i)
			{
				const TileAtlas::Blit &blitOp = blits[i];

				GLMeta::blitRectangle(IntRect(blitOp.src.x, blitOp.src.y, tsLaneW, blitOp.h),
				                      blitOp.dst);
			}

			GLMeta::blitEnd();
#endif
		}

#ifdef __vita__
	/* VITA-ATLAS-SYNC: resolve all atlas writes before any sampling draw.
	 * Without this scene break, tilemap draws can sample the atlas before
	 * the build scene's FBO resolve lands (black/flickering tiles). */
	gl.Finish();
	vitaDiagLog("TMXP", "atlas-sync mid+end");
	/* BUILD 9: atlas content at end of build, after all uploads. */
	tm9AtRead("postBuild", atlas.gl.tex.gl, atlas.gl.fbo.gl);
#endif

	/* TMXP: atlas census + content checksums. Runs per map load (rare):
	 * proves whether a black tile region is an empty atlas row vs bad UVs. */
	{
		vitaDiagLog("TMXP", "atlas ts=%dx%d efH=%d atlas=%dx%d blits=%u usableAT=%u maxTex=%d",
		            tileset ? tileset->width() : -1, tileset ? tileset->height() : -1,
		            atlas.efTilesetH, atlas.size.x, atlas.size.y,
		            (unsigned)blits.size(), (unsigned)atlas.usableATs.size(),
		            glState.caps.maxTexSize);
		/* BUILD 8: log effective lane-path flag + user-override path once
		 * per process. Settles the ux0:/data/mkxp.json merge question:
		 * if userConfPath points elsewhere the override file was never
		 * a candidate. */
		{
			static bool confLogged = false;
			if (!confLogged)
			{
				confLogged = true;
				vitaDiagLog("TMXP", "effective subImageFix=%d",
				            shState->config().subImageFix ? 1 : 0);
				vitaDiagLog("TMXP", "userConfPath=%s",
				            shState->config().userConfPath.c_str());
			}
		}
	}
	}

	int samplePriority(int tileInd)
	{
		if (!priorities)
			return 0;

		if (tileInd > priorities->xSize()-1)
			return 0;

		int value = priorities->at(tileInd);

		if (value > 5)
			return -1;

		return value;
	}

	void handleAutotile(int x, int y, int tileInd, SVVector *array)
	{
		/* Which autotile [0-7] */
		int atInd = tileInd / 48 - 1;
		if (!atlas.smallATs[atInd])
		{
			/* Which tile pattern of the autotile [0-47] */
			int subInd = tileInd % 48;

			const StaticRect *pieceRect = &autotileRects[subInd*4];

			/* Iterate over the 4 tile pieces */
			for (int i = 0; i < 4; ++i)
			{
				FloatRect posRect(x*32, y*32, 16, 16);
				atSelectSubPos(posRect, i);

				FloatRect texRect = pieceRect[i];

				/* Adjust to atlas coordinates */
				texRect.y += atInd * autotileH;

				SVertex v[4];
				Quad::setTexPosRect(v, texRect, posRect);

				/* Iterate over 4 vertices */
				for (size_t j = 0; j < 4; ++j)
					array->push_back(v[j]);
			}
		}
		else
		{
			FloatRect posRect(x*32, y*32, 32, 32);
			FloatRect texRect(0.5f, atInd * autotileH + 0.5f, 31, 31);
			SVertex v[4];
			Quad::setTexPosRect(v, texRect, posRect);

			/* Iterate over 4 vertices */
			for (size_t j = 0; j < 4; ++j)
				array->push_back(v[j]);
		}
	}

	void handleTile(int x, int y, int z)
	{
		int tileInd =
			tableGetWrapped(*mapData, x + viewpPos.x, y + viewpPos.y, z);

		/* Check for empty space */
		if (tileInd < 48)
			return;

		int prio = samplePriority(tileInd);

		/* Check for faulty data */
		if (prio == -1)
			return;

		SVVector *targetArray;

		/* Prio 0 tiles are all part of the same ground layer */
		if (prio == 0)
		{
			targetArray = &groundVert;
		}
		else
		{
			int layerInd = y + prio;
			if ((size_t)layerInd >= zlayersMax)
				return;
			targetArray = &zlayerVert[layerInd];
		}

		/* Check for autotile */
		if (tileInd < 48*8)
		{
			handleAutotile(x, y, tileInd, targetArray);
			return;
		}

		int tsInd = tileInd - 48*8;
		int tileX = tsInd % 8;
		int tileY = tsInd / 8;

		Vec2i texPos = TileAtlas::tileToAtlasCoor(tileX, tileY, atlas.efTilesetH, atlas.size.y);
		FloatRect texRect((float) texPos.x+0.5f, (float) texPos.y+0.5f, 31, 31);
		FloatRect posRect(x*32, y*32, 32, 32);

		SVertex v[4];
		Quad::setTexPosRect(v, texRect, posRect);

		for (size_t i = 0; i < 4; ++i)
			targetArray->push_back(v[i]);
	}

	void clearQuadArrays()
	{
		groundVert.clear();

		for (size_t i = 0; i < zlayersMax; ++i)
			zlayerVert[i].clear();
	}

	void buildQuadArray()
	{
		clearQuadArrays();

		int ox = viewpPos.x;
		int oy = viewpPos.y;
		int mapW = mapData->xSize();
		int mapH = mapData->ySize();

		int minX = 0;
		int minY = 0;
		if (ox < 0)
			minX = -ox;
		if (oy < 0)
			minY = -oy;

		// There could be off-by-one issues in these couple sections.
		int maxX = viewpW;
		int maxY = viewpH;
		if (ox + maxX >= mapW)
			maxX = mapW - ox - 1;
		if (oy + maxY >= mapH)
			maxY = mapH - oy - 1;

		if ((minX > maxX) || (minY > maxY))
			return;
		for (int x = minX; x <= maxX; ++x)
			for (int y = minY; y <= maxY; ++y)
				for (int z = 0; z < mapData->zSize(); ++z)
					handleTile(x, y, z);
	}

	static size_t quadDataSize(size_t quadCount)
	{
		return quadCount * sizeof(SVertex) * 4;
	}

	size_t zlayerSize(size_t index)
	{
		return zlayerBases[index+1] - zlayerBases[index];
	}

	void uploadBuffers()
	{
		/* Calculate total quad count */
		size_t groundQuadCount = groundVert.size() / 4;
		size_t quadCount = groundQuadCount;

		for (size_t i = 0; i < zlayersMax; ++i)
		{
			zlayerBases[i] = quadCount;
			quadCount += zlayerVert[i].size() / 4;
		}

		zlayerBases[zlayersMax] = quadCount;

		VBO::bind(tiles.vbo);
		VBO::allocEmpty(quadDataSize(quadCount));

		VBO::uploadSubData(0, quadDataSize(groundQuadCount), dataPtr(groundVert));

		for (size_t i = 0; i < zlayersMax; ++i)
		{
			if (zlayerVert[i].empty())
				continue;

			VBO::uploadSubData(quadDataSize(zlayerBases[i]),
			                   quadDataSize(zlayerSize(i)), dataPtr(zlayerVert[i]));
		}

		VBO::unbind();

		/* Ensure global IBO size */
		shState->ensureQuadIBO(quadCount);

		vitaDiagLog("TMXP", "quads ground=%u total=%u vbo=%u",
		            (unsigned)groundQuadCount, (unsigned)quadCount,
		            (unsigned)quadDataSize(quadCount));

		/* TMXP: vertex ground truth. Rug quads have UV-Y in atlas row 2
		 * (256-384); the Map006 hole sits at map pixels x 320-512, y 224-352. */
		{
			uint32_t h = 2166136261u;
			const uint8_t *bytes =
			    reinterpret_cast<const uint8_t *>(dataPtr(groundVert));
			const size_t nbytes = groundVert.size() * sizeof(SVertex);
			for (size_t i = 0; i < nbytes; i += 64)
				h = (h ^ bytes[i]) * 16777619u;
			size_t rugQuads = 0, holeQuads = 0;
			for (size_t i = 0; i + 3 < groundVert.size(); i += 4)
			{
				const SVertex &v0 = groundVert[i];
				if (v0.texPos.y >= 256.0f && v0.texPos.y < 384.0f)
				{
					if (rugQuads < 3)
						vitaDiagLog("TMXP", "rugquad=%u pos=%.0f,%.0f uv=%.0f,%.0f",
						            (unsigned)rugQuads, v0.pos.x, v0.pos.y,
						            v0.texPos.x, v0.texPos.y);
					++rugQuads;
				}
				if (v0.pos.x >= 320.0f && v0.pos.x < 512.0f &&
				    v0.pos.y >= 224.0f && v0.pos.y < 352.0f)
					++holeQuads;
			}
			vitaDiagLog("TMXP", "vertfnv=%08x rugquads=%u holequads=%u",
			            h, (unsigned)rugQuads, (unsigned)holeQuads);
		}
	}

	void bindShader(ShaderBase *&shaderVar)
	{
		{
			static int n = 0;
			if (n < 8)
			{
				++n;
				vitaDiagLog("TMXP", "shaderinputs n=%d ani=%d dur=%d frames=%d,%d,%d,%d,%d,%d,%d",
				            n, tiles.aniIdx, atFrameDur,
				            atlas.nATFrames[0], atlas.nATFrames[1], atlas.nATFrames[2],
				            atlas.nATFrames[3], atlas.nATFrames[4], atlas.nATFrames[5],
				            atlas.nATFrames[6]);
			}
		}
		if (tiles.animated || color->hasEffect() || tone->hasEffect() || opacity != 255)
		{
			TilemapShader &tilemapShader = shState->shaders().tilemap;
			tilemapShader.bind();
			tilemapShader.applyViewportProj();
			tilemapShader.setTone(tone->norm);
			tilemapShader.setColor(color->norm);
			tilemapShader.setOpacity(opacity.norm);
			tilemapShader.setAnimation(tiles.aniIdx / atFrameDur,
			                           atlas.nATFrames);
			shaderVar = &tilemapShader;
		}
		else
		{
			shaderVar = &shState->shaders().simple;
			shaderVar->bind();
		}

		shaderVar->applyViewportProj();
	}

	void bindAtlas(ShaderBase &shader)
	{
		TEX::bind(atlas.gl.tex);
		shader.setTexSize(atlas.size);
	}

	void updateActiveElements(std::vector<int> &zlayerInd)
	{
		elem.ground->updateVboCount();

		for (size_t i = 0; i < zlayersMax; ++i)
		{
			if (i < zlayerInd.size())
			{
				int index = zlayerInd[i];
				elem.zlayers[i]->setVisible(visible);
				elem.zlayers[i]->setIndex(index);
			}
			else
			{
				/* Hide unused layers */
				elem.zlayers[i]->setVisible(false);
			}
		}
	}

	void updateSceneElements()
	{
		/* Only allocate elements for non-emtpy zlayers */
		std::vector<int> zlayerInd;

		for (size_t i = 0; i < zlayersMax; ++i)
			if (zlayerVert[i].size() > 0)
				zlayerInd.push_back(i);

		updateActiveElements(zlayerInd);
		elem.activeLayers = zlayerInd.size();
		zOrderDirty = false;
	}

	void hideElements()
	{
		elem.ground->setVisible(false);

		for (size_t i = 0; i < zlayersMax; ++i)
			elem.zlayers[i]->setVisible(false);
	}

	void updateZOrder()
	{
		if (elem.activeLayers == 0)
			return;

		for (size_t i = 0; i < elem.activeLayers; ++i)
			elem.zlayers[i]->initUpdateZ();

		ZLayer *prev = elem.zlayers[0];
		prev->finiUpdateZ(0);

		for (size_t i = 1; i < elem.activeLayers; ++i)
		{
			ZLayer *layer = elem.zlayers[i];
			layer->finiUpdateZ(prev);
			prev = layer;
		}
	}

	/* When there are two or more zlayers with no other
	 * elements between them in the scene list, we can
	 * render them in a batch (as the zlayer data itself
	 * is ordered sequentially in VRAM). Every frame, we
	 * scan the scene list for such sequential layers and
	 * batch them up for drawing. The first layer of the batch
	 * (the "batch head") executes the draw call, all others
	 * are muted via the 'batchedFlag'. For simplicity,
	 * single sized batches are possible. */
	void prepareZLayerBatches()
	{
		ZLayer *const *zlayers = elem.zlayers;

		for (size_t i = 0; i < elem.activeLayers; ++i)
		{
			ZLayer *batchHead = zlayers[i];
			batchHead->batchedFlag = false;

			GLsizei vboBatchCount = batchHead->vboCount;
			IntruListLink<SceneElement> *iter = &batchHead->link;

			for (i = i+1; i < elem.activeLayers; ++i)
			{
				iter = iter->next;
				ZLayer *layer = zlayers[i];

				/* Check if the next SceneElement is also
				 * the next zlayer in our list. If not,
				 * the current batch is complete */
				if (iter != &layer->link)
					break;

				vboBatchCount += layer->vboCount;
				layer->batchedFlag = true;
			}

			batchHead->vboBatchCount = vboBatchCount;
			--i;
		}
	}

	void updateMapViewport()
	{
		const Vec2i combOrigin = origin + elem.sceneGeo.orig;
		const Vec2i mvpPos = getTilePos(combOrigin);

		if (mvpPos != viewpPos)
		{
			viewpPos = mvpPos;
			buffersDirty = true;
			updateFlashMapViewport();
		}

		dispPos = elem.sceneGeo.rect.pos() - wrap(combOrigin, 32);
	}

	void prepare()
	{
		if (!verifyResources())
		{
			if (tilemapReady)
				hideElements();
			tilemapReady = false;

			return;
		}

		if (atlasSizeDirty)
		{
			allocateAtlas();
			atlasSizeDirty = false;
		}

		if (atlasDirty)
		{
			buildAtlas();
			atlasDirty = false;
		}

		if (mapViewportDirty)
		{
			updateMapViewport();
			mapViewportDirty = false;
		}

		if (buffersDirty)
		{
			buildQuadArray();
			uploadBuffers();
			updateSceneElements();
			buffersDirty = false;
		}

		flashMap.prepare();

		if (zOrderDirty)
		{
			updateZOrder();
			zOrderDirty = false;
		}

		prepareZLayerBatches();

		tilemapReady = true;
	}
};

GroundLayer::GroundLayer(TilemapPrivate *p, Viewport *viewport)
    : ViewportElement(viewport, 0),
      vboCount(0),
      p(p)
{
	onGeometryChange(scene->getGeometry());
}

void GroundLayer::updateVboCount()
{
	vboCount = p->zlayerBases[0] * 6;
}

void GroundLayer::draw()
{
	if (p->groundVert.size() == 0)
		return;

	if (!p->opacity)
		return;

	ShaderBase *shader;

	p->bindShader(shader);
	p->bindAtlas(*shader);

	glState.blendMode.pushSet(p->blendType);

	GLMeta::vaoBind(p->tiles.vao);

	shader->setTranslation(p->dispPos);
	drawInt();

	GLMeta::vaoUnbind(p->tiles.vao);

	p->flashMap.draw(flashAlpha[p->flashAlphaIdx] / 255.f, p->dispPos);

	glState.blendMode.pop();
}

void GroundLayer::drawInt()
{
	gl.DrawElements(GL_TRIANGLES, vboCount, _GL_INDEX_TYPE, (GLvoid*) 0);
}

void GroundLayer::onGeometryChange(const Scene::Geometry &geo)
{
	p->updateSceneGeometry(geo);
}

ZLayer::ZLayer(TilemapPrivate *p, Viewport *viewport)
    : ViewportElement(viewport, 0),
      index(0),
      vboOffset(0),
      vboCount(0),
      p(p),
      vboBatchCount(0)
{}

void ZLayer::setIndex(int value)
{
	index = value;

	z = calculateZ(p, index);
	scene->reinsert(*this);

	vboOffset = p->zlayerBases[index] * sizeof(index_t) * 6;
	vboCount = p->zlayerSize(index) * 6;
}

void ZLayer::draw()
{
	if (batchedFlag)
		return;

	ShaderBase *shader;

	p->bindShader(shader);
	p->bindAtlas(*shader);

	glState.blendMode.pushSet(p->blendType);

	GLMeta::vaoBind(p->tiles.vao);

	shader->setTranslation(p->dispPos);
	drawInt();

	GLMeta::vaoUnbind(p->tiles.vao);

	glState.blendMode.pop();
}

void ZLayer::drawInt()
{
	gl.DrawElements(GL_TRIANGLES, vboBatchCount, _GL_INDEX_TYPE, (GLvoid*) vboOffset);
}

int ZLayer::calculateZ(TilemapPrivate *p, int index)
{
	return 32 * (index + p->viewpPos.y + 1) - p->origin.y;
}

void ZLayer::initUpdateZ()
{
	unlink();
}

void ZLayer::finiUpdateZ(ZLayer *prev)
{
	z = calculateZ(p, index);

	if (prev)
		scene->insertAfter(*this, *prev);
	else
		scene->insert(*this);
}

void Tilemap::Autotiles::set(int i, Bitmap *bitmap)
{
	if (!p)
		return;

	if (i < 0 || i > autotileCount-1)
		return;

	if (p->autotiles[i] == bitmap)
		return;

	p->autotiles[i] = bitmap;

	p->invalidateAtlasContents();

	p->autotilesCon[i].disconnect();
	p->autotilesDispCon[i].disconnect();

	if (nullOrDisposed(bitmap))
	{
		p->autotiles[i] = 0;
		return;
	}

	p->autotilesCon[i] = bitmap->modified.connect
	        (&TilemapPrivate::invalidateAtlasContents, p);

	p->autotilesDispCon[i] = bitmap->wasDisposed.connect( [i, this] { p->atlasContentsDisposal(i); } );

	p->updateAutotileInfo();
}

Bitmap *Tilemap::Autotiles::get(int i) const
{
	if (!p)
		return 0;

	if (i < 0 || i > autotileCount-1)
		return 0;

	return p->autotiles[i];
}

Tilemap::Tilemap(Viewport *viewport)
{
	p = new TilemapPrivate(viewport);
	atProxy.p = p;
}

Tilemap::~Tilemap()
{
	dispose();
}

void Tilemap::update()
{
	guardDisposed();

	if (!p->tilemapReady)
		return;

	/* Animate flash */
	if (++p->flashAlphaIdx >= flashAlphaN)
		p->flashAlphaIdx = 0;

	/* Animate autotiles */
	if (!p->tiles.animated)
		return;

	++p->tiles.aniIdx;
}

Tilemap::Autotiles &Tilemap::getAutotiles()
{
	guardDisposed();

	return atProxy;
}

DEF_ATTR_RD_SIMPLE(Tilemap, Viewport, Viewport*, p->viewport)
DEF_ATTR_RD_SIMPLE(Tilemap, Tileset, Bitmap*, p->tileset)
DEF_ATTR_RD_SIMPLE(Tilemap, MapData, Table*, p->mapData)
DEF_ATTR_RD_SIMPLE(Tilemap, FlashData, Table*, p->flashMap.getData())
DEF_ATTR_RD_SIMPLE(Tilemap, Priorities, Table*, p->priorities)
DEF_ATTR_RD_SIMPLE(Tilemap, Visible, bool, p->visible)
DEF_ATTR_RD_SIMPLE(Tilemap, OX, int, p->origin.x)
DEF_ATTR_RD_SIMPLE(Tilemap, OY, int, p->origin.y)

DEF_ATTR_RD_SIMPLE(Tilemap, BlendType, int, p->blendType)
DEF_ATTR_SIMPLE(Tilemap, Opacity,   int,     p->opacity)
DEF_ATTR_SIMPLE(Tilemap, Color,     Color&, *p->color)
DEF_ATTR_SIMPLE(Tilemap, Tone,      Tone&,  *p->tone)

void Tilemap::setTileset(Bitmap *value)
{
	guardDisposed();

	if (p->tileset == value)
		return;

	p->tileset = value;

	p->tilesetDispCon.disconnect();
	p->tilesetCon.disconnect();

	if (nullOrDisposed(value))
	{
		p->tileset = 0;
		return;
	}

	p->invalidateAtlasSize();

	p->tilesetCon = value->modified.connect
	        (&TilemapPrivate::invalidateAtlasSize, p);

	p->tilesetDispCon = value->wasDisposed.connect
	        (&TilemapPrivate::tilesetDisposal, p);

	p->updateAtlasInfo();
}

void Tilemap::setMapData(Table *value)
{
	guardDisposed();

	if (p->mapData == value)
		return;

	p->mapData = value;

	if (!value)
		return;

	p->invalidateBuffers();
	p->mapDataCon.disconnect();
	p->mapDataCon = value->modified.connect
	        (&TilemapPrivate::invalidateBuffers, p);
}

void Tilemap::setFlashData(Table *value)
{
	guardDisposed();

	p->flashMap.setData(value);
}

void Tilemap::setPriorities(Table *value)
{
	guardDisposed();

	if (p->priorities == value)
		return;

	p->priorities = value;

	if (!value)
		return;

	p->invalidateBuffers();
	p->prioritiesCon.disconnect();
	p->prioritiesCon = value->modified.connect
	        (&TilemapPrivate::invalidateBuffers, p);
}

void Tilemap::setVisible(bool value)
{
	guardDisposed();

	if (p->visible == value)
		return;

	p->visible = value;

	if (!p->tilemapReady)
		return;

	p->elem.ground->setVisible(value);
	for (size_t i = 0; i < p->elem.activeLayers; ++i)
		p->elem.zlayers[i]->setVisible(value);
}

void Tilemap::setOX(int value)
{
	guardDisposed();

	if (p->origin.x == value)
		return;

	p->origin.x = value;
	p->mapViewportDirty = true;
}

void Tilemap::setOY(int value)
{
	guardDisposed();

	if (p->origin.y == value)
		return;

	p->origin.y = value;
	p->zOrderDirty = true;
	p->mapViewportDirty = true;
}

void Tilemap::setBlendType(int value)
{
	guardDisposed();

	switch (value)
	{
	default :
	case BlendNormal :
		p->blendType = BlendNormal;
		return;
	case BlendAddition :
		p->blendType = BlendAddition;
		return;
	case BlendSubstraction :
		p->blendType = BlendSubstraction;
		return;
	}
}

void Tilemap::initDynAttribs()
{
	p->color = new Color;
	p->tone = new Tone;
}

void Tilemap::releaseResources()
{
	delete p;
	atProxy.p = 0;
}
