#include "frame_profile.h"
/*
** tilemapvx.cpp
**
** This file is part of mkxp.
**
** Copyright (C) 2014 - 2021 Amaryllis Kulla <ancurio@mapleshrine.eu>
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

#include "tilemapvx.h"

#include "util/debugwriter.h"

#include "tileatlasvx.h"
#include "etc-internal.h"
#include "bitmap.h"
#include "table.h"
#include "viewport.h"
#include "gl-util.h"
#include "sharedstate.h"
#include "glstate.h"
#include "vertex.h"
#include "quad.h"
#include "quadarray.h"
#include "shader.h"
#include "vita_diagnostic.h"
#include "tilemap-common.h"
#include "texpool.h"
#include "exception.h"

#include <algorithm>
#include <stdio.h>
#include <string.h>
#include <vector>
#include "sigslot/signal.hpp"

/* Shared tile atlases.
 *
 * An atlas is built only from the tilemap's nine bitmaps, so tilemaps that
 * hold the same nine Bitmap objects would build identical atlases. Layer
 * scripts do exactly that: N.P.C. Dreams (CanariAlternate's multi-layer
 * system) gives each map layer its own Tilemap, all filled from Cache, so its
 * four-layer swamp held four identical 8 MiB atlases and could fail to find
 * the fourth contiguous block (a Vita crash in 0.5.3). Tilemaps with the same
 * bitmaps now share one atlas.
 *
 * The atlas is taken when the tilemap first draws, not when it is created,
 * because its bitmaps are only known then. A bitmap change (modified signal)
 * marks the shared atlas stale and the first sharer to prepare rebuilds it.
 * A disposed bitmap retires the atlas so that a new Bitmap allocated at the
 * same address can never match it; its sharers move to new atlases. */
struct SharedAtlasVX
{
	Bitmap *key[BM_COUNT];
	TEXFBO atlas;
	TEXFBO atlasHires;
	int refs;
	bool stale;
	bool retired;
};

static std::vector<SharedAtlasVX*> sharedAtlases;

static size_t sharedAtlasBytes(const SharedAtlasVX *e)
{
	size_t bytes = (size_t)e->atlas.width * e->atlas.height * 4;
	if (e->atlasHires.tex != TEX::ID(0))
		bytes += (size_t)e->atlasHires.width * e->atlasHires.height * 4;
	return bytes;
}

static void sharedAtlasLog(const char *what, const SharedAtlasVX *e)
{
	size_t total = 0;
	for (const SharedAtlasVX *a : sharedAtlases)
		total += sharedAtlasBytes(a);
	fprintf(stderr, "Tilemap atlas: %s (refs %d); %u live, %u KiB\n",
	        what, e->refs, (unsigned)sharedAtlases.size(), (unsigned)(total / 1024));
}

static SharedAtlasVX *sharedAtlasFind(Bitmap *const key[BM_COUNT])
{
	for (SharedAtlasVX *e : sharedAtlases)
		if (!e->retired && memcmp(e->key, key, sizeof(e->key)) == 0)
			return e;
	return nullptr;
}

static SharedAtlasVX *sharedAtlasCreate(Bitmap *const key[BM_COUNT])
{
	SharedAtlasVX *e = new SharedAtlasVX();
	memcpy(e->key, key, sizeof(e->key));
	e->refs = 1;
	e->stale = true;
	e->retired = false;

	try
	{
		shState->requestAtlasTex(ATLASVX_W, ATLASVX_H, e->atlas);
		e->atlas.selfHires = nullptr;

		if (shState->config().enableHires)
		{
			double scalingFactor = shState->config().atlasScalingFactor;
			int hiresWidth = (int)lround(scalingFactor * ATLASVX_W);
			int hiresHeight = (int)lround(scalingFactor * ATLASVX_H);
			shState->requestAtlasTex(hiresWidth, hiresHeight, e->atlasHires);
			e->atlasHires.selfHires = nullptr;
			e->atlas.selfHires = &e->atlasHires;
		}
	}
	catch (...)
	{
		shState->releaseAtlasTex(e->atlas);
		delete e;
		throw;
	}

	sharedAtlases.push_back(e);
	sharedAtlasLog("new", e);
	return e;
}

static void sharedAtlasRelease(SharedAtlasVX *e)
{
	if (!e || --e->refs > 0)
		return;

	sharedAtlases.erase(std::remove(sharedAtlases.begin(), sharedAtlases.end(), e),
	                    sharedAtlases.end());

	/* The cached atlas slot must not point into this entry */
	e->atlas.selfHires = nullptr;
	shState->releaseAtlasTex(e->atlas);
	shState->releaseAtlasTex(e->atlasHires);
	sharedAtlasLog("freed", e);
	delete e;
}

static bool isGraphicsOOM(const Exception &e)
{
	return e.type == Exception::MKXPError &&
	       e.msg.find("Graphics memory: out of memory allocating ") == 0;
}

#ifdef MKXPZ_VITA_DIAGNOSTICS
/* S42-I: one-shot next-frame tail-trace trigger.
 * Count TileAtlasVX::build returns; on build #2, after the main TilemapVX
 * draw completes, arm the FOLLOWING prepare, which then emits the
 * NEXT_FRAME_TILEMAP_PREPARE_BEGIN/DONE pair and arms the graphics tail trace. */
static int s42iAtlasBuildCount = 0;
static bool s42iBuild2Seen = false;
static bool s42iArmNextPrepare = false;
static bool s42iTriggerDone = false;
#endif

/* Flash tiles pulsing opacity */
static const uint8_t flashAlpha[] =
{
	/* Fade in */
	0x78, 0x78, 0x78, 0x78, 0x96, 0x96, 0x96, 0x96,
	0xB4, 0xB4, 0xB4, 0xB4, 0xD2, 0xD2, 0xD2, 0xD2,
	/* Fade out */
	0xF0, 0xF0, 0xF0, 0xF0, 0xD2, 0xD2, 0xD2, 0xD2,
	0xB4, 0xB4, 0xB4, 0xB4, 0x96, 0x96, 0x96, 0x96
};

static elementsN(flashAlpha);

struct TilemapVXPrivate : public ViewportElement, TileAtlasVX::Reader
{
	Bitmap *bitmaps[BM_COUNT];

	Table *mapData;
	Table *flags;
	Vec2i origin;

	/* Subregion of the map that is drawn to screen (map viewport) */
	IntRect mapViewp;
	/* Position on screen the map subregion is drawn at */
	Vec2i dispPos;
	Scene::Geometry sceneGeo;

	std::vector<SVertex> groundVert;
	std::vector<SVertex> aboveVert;

	/* Shared with tilemaps that hold the same bitmaps (see SharedAtlasVX) */
	SharedAtlasVX *shared;
	VBO::ID vbo;
	GLMeta::VAO vao;

	size_t allocQuads;

	size_t groundQuads;
	size_t aboveQuads;

	uint16_t frameIdx;
	Vec2 aniOffset;

	FlashMap flashMap;
	uint8_t flashAlphaIdx;

	/* The bitmap set changed: find or make the matching shared atlas */
	bool atlasDirty;
	bool buffersDirty;
	bool mapViewportDirty;

	sigslot::connection mapDataCon;
	sigslot::connection flagsCon;

	sigslot::connection prepareCon;
	sigslot::connection bmChangedCons[BM_COUNT];
	sigslot::connection bmDisposedCons[BM_COUNT];

	struct AboveLayer : public ViewportElement
	{
		TilemapVXPrivate *p;

		AboveLayer(TilemapVXPrivate *p, Viewport *viewport)
		    : ViewportElement(viewport, 200),
		      p(p)
		{}

		void draw()
		{
            FrameProfile::Scope profile(FrameProfile::DrawTilemap);
			p->drawAbove();
			p->drawFlashLayer();
		}

		ABOUT_TO_ACCESS_NOOP
	};

	AboveLayer above;

	TilemapVXPrivate(Viewport *viewport)
	    : ViewportElement(viewport),
	      mapData(0),
	      shared(nullptr),
	      flags(0),
	      allocQuads(0),
	      groundQuads(0),
	      aboveQuads(0),
	      frameIdx(0),
	      flashAlphaIdx(0),
	      atlasDirty(true),
	      buffersDirty(false),
	      mapViewportDirty(false),
	      above(this, viewport)
	{
		memset(bitmaps, 0, sizeof(bitmaps));

		vbo = VBO::gen();

		GLMeta::vaoFillInVertexData<SVertex>(vao);
		vao.vbo = vbo;
		vao.ibo = shState->globalIBO().ibo;
		GLMeta::vaoInit(vao);

		onGeometryChange(scene->getGeometry());

		prepareCon = shState->prepareDraw.connect
			(&TilemapVXPrivate::prepare, this);
	}

	virtual ~TilemapVXPrivate()
	{
		GLMeta::vaoFini(vao);
		VBO::del(vbo);

		sharedAtlasRelease(shared);
		shared = nullptr;

		prepareCon.disconnect();

		mapDataCon.disconnect();
		flagsCon.disconnect();

		for (size_t i = 0; i < BM_COUNT; ++i)
		{
			bmChangedCons[i].disconnect();
			bmDisposedCons[i].disconnect();
		}
	}

	/* A bitmap's pixels changed: every sharer's atlas is out of date */
	void invalidateAtlas()
	{
		if (shared)
			shared->stale = true;
	}

	void atlasDisposal(int i)
	{
		// Guard against deleted bitmaps
		bitmaps[i] = 0;

		/* Its address may be reused by a new Bitmap */
		if (shared)
			shared->retired = true;

		atlasDirty = true;
	}

	/* Point this tilemap at the shared atlas for its current bitmaps. When
	 * none exists, this tilemap's old atlas is let go first if no one else
	 * uses it, so its texture can be reused for the new one instead of
	 * needing a second 8 MiB block. */
	void acquireAtlas()
	{
		SharedAtlasVX *next = sharedAtlasFind(bitmaps);
		if (next && next == shared)
			return;

		if (next)
		{
			++next->refs;
			sharedAtlasRelease(shared);
			shared = next;
			sharedAtlasLog("shared", next);
			return;
		}

		sharedAtlasRelease(shared);
		shared = nullptr;

		try
		{
			shared = sharedAtlasCreate(bitmaps);
		}
		catch (const Exception &e)
		{
			if (!isGraphicsOOM(e))
				throw;

			/* Same reclaim as Tilemap.new's retry, minus the Ruby GC,
			 * which cannot run while drawing */
			fprintf(stderr, "Tilemap atlas: reclaiming graphics memory before retry\n");
			shState->texPool().clearCache();
			Bitmap::releaseRecreatableMemory();
			shState->dropCachedAtlasTex();
			shared = sharedAtlasCreate(bitmaps);
			fprintf(stderr, "Tilemap atlas: graphics memory retry succeeded\n");
		}
	}

	void invalidateBuffers()
	{
		buffersDirty = true;
	}

	void rebuildAtlas()
	{
		TEXFBO &atlas = shared->atlas;
		TileAtlasVX::build(atlas, bitmaps);

#ifdef MKXPZ_VITA_DIAGNOSTICS
		if (!s42iTriggerDone)
		{
			++s42iAtlasBuildCount;
			if (s42iAtlasBuildCount == 2)
				s42iBuild2Seen = true;
		}
#endif

		if (shState->config().dumpAtlas)
		{
			Debug() << "Dumping tile atlas...";

			Bitmap dump(atlas);
			if (dump.hasHires())
			{
				dump.getHires()->saveToFile("dumped_atlas_hires.png");
			}
			else
			{
				dump.saveToFile("dumped_atlas.png");
			}

			Debug() << "Tile atlas dump completed.";
		}
	}

	void updateMapViewport()
	{
		/* Note: We include one extra row at the top above
		 * the normal map viewport to ensure the legs of table
		 * tiles off screen are properly drawn */

		IntRect newMvp;

		const Vec2i combOrigin = origin + sceneGeo.orig;
		const Vec2i geoSize = sceneGeo.rect.size();

		newMvp.setPos(getTilePos(combOrigin) - Vec2i(0, 1));

		/* Ensure that the size is big enough to cover the whole viewport,
		 * and add one tile row/column as a buffer for scrolling */
		newMvp.setSize((geoSize / 32) + !!(geoSize % 32) + Vec2i(1, 2));

		if (newMvp != mapViewp)
		{
			mapViewp = newMvp;
			flashMap.setViewport(newMvp);
			buffersDirty = true;
		}

		dispPos = sceneGeo.rect.pos() - wrap(combOrigin, 32) - Vec2i(0, 32);
	}

	static size_t quadBytes(size_t quads)
	{
		return quads * 4 * sizeof(SVertex);
	}

	void rebuildBuffers()
	{
		if (!mapData)
			return;

		groundVert.clear();
		aboveVert.clear();

		TileAtlasVX::readTiles(*this, *mapData, flags,
		                       mapViewp.x, mapViewp.y, mapViewp.w, mapViewp.h);

		groundQuads = groundVert.size() / 4;
		aboveQuads = aboveVert.size() / 4;
		size_t totalQuads = groundQuads + aboveQuads;

		VBO::bind(vbo);

		if (totalQuads > allocQuads)
		{
			VBO::allocEmpty(quadBytes(totalQuads), GL_DYNAMIC_DRAW);
			allocQuads = totalQuads;
		}

		VBO::uploadSubData(0, quadBytes(groundQuads), dataPtr(groundVert));
		VBO::uploadSubData(quadBytes(groundQuads), quadBytes(aboveQuads), dataPtr(aboveVert));

		VBO::unbind();

		shState->ensureQuadIBO(totalQuads);
	}

	void prepare()
	{
		if (!mapData)
			return;

#ifdef MKXPZ_VITA_DIAGNOSTICS
		const bool s42iThisPrepare = s42iArmNextPrepare;
		if (s42iThisPrepare)
			vitaDiagLog("TEXTFLOW", "S42I NEXT_FRAME_TILEMAP_PREPARE_BEGIN atlasDirty=%d buffersDirty=%d", atlasDirty, buffersDirty);
#endif

		vitaDiagLog("TMPREP", "prepare begin atlasDirty=%d buffersDirty=%d", atlasDirty, buffersDirty);
		if (atlasDirty || !shared)
		{
			acquireAtlas();
			atlasDirty = false;
		}

		if (shared->stale)
		{
			vitaDiagLog("TMPREP", "before rebuildAtlas");
			rebuildAtlas();
			vitaDiagLog("TMPREP", "after rebuildAtlas");
			shared->stale = false;
		}

		if (mapViewportDirty)
		{
			updateMapViewport();
			mapViewportDirty = false;
		}

		if (buffersDirty)
		{
			vitaDiagLog("TMPREP", "before rebuildBuffers");
			rebuildBuffers();
			vitaDiagLog("TMPREP", "after rebuildBuffers");
			buffersDirty = false;
		}

		flashMap.prepare();
		vitaDiagLog("TMPREP", "prepare done");

#ifdef MKXPZ_VITA_DIAGNOSTICS
		if (s42iThisPrepare)
		{
			vitaDiagLog("TEXTFLOW", "S42I NEXT_FRAME_TILEMAP_PREPARE_DONE");
			vitaDiagS42IArmTailTrace();
			s42iArmNextPrepare = false;
			s42iTriggerDone = true;
		}
#endif
	}

	SVertex *allocVert(std::vector<SVertex> &vec, size_t count)
	{
		size_t size = vec.size();
		vec.resize(size + count);

		return &vec[size];
	}

	/* SceneElement */
	void draw()
	{
        FrameProfile::Scope profile(FrameProfile::DrawTilemap);
		vitaDiagLog("TMDRAW", "draw begin groundQuads=%d aboveQuads=%d", groundQuads, aboveQuads);
		drawGround();
		vitaDiagLog("TMDRAW", "draw ground done");
		drawFlashLayer();
		vitaDiagLog("TMDRAW", "draw flash done");

#ifdef MKXPZ_VITA_DIAGNOSTICS
		/* S42-I: after the main TilemapVX draw (ground + first flash) of the
		 * atlas-build-#2 frame completes, arm the FOLLOWING prepare. */
		if (!s42iTriggerDone && s42iBuild2Seen && !s42iArmNextPrepare)
			s42iArmNextPrepare = true;
#endif
	}

	void drawGround()
	{
		if (groundQuads == 0 || !shared)
			return;

		const TEXFBO &atlas = shared->atlas;

		ShaderBase *shader;

		if (!nullOrDisposed(bitmaps[BM_A1]))
		{
			/* Animated tileset */
			TilemapVXShader &tmShader = shState->shaders().tilemapVX;
			vitaDiagLog("TMDRAW", "before shader_bind tm");
			tmShader.bind();
			vitaDiagLog("TMDRAW", "after shader_bind tm");
			tmShader.setAniOffset(aniOffset);

			shader = &tmShader;
		}
		else
		{
			/* Static tileset */
			shader = &shState->shaders().simple;
			vitaDiagLog("TMDRAW", "before shader_bind simple");
			shader->bind();
			vitaDiagLog("TMDRAW", "after shader_bind simple");
		}

		vitaDiagLog("TMDRAW", "before uniforms");
		shader->setTexSize(Vec2i(atlas.width, atlas.height));
		shader->applyViewportProj();
		shader->setTranslation(dispPos);
		vitaDiagLog("TMDRAW", "after uniforms");

		vitaDiagLog("TMDRAW", "before texture_bind");
		if (atlas.selfHires != nullptr) {
			TEX::bind(atlas.selfHires->tex);
		}
		else {
			TEX::bind(atlas.tex);
		}
		vitaDiagLog("TMDRAW", "after texture_bind");

		vitaDiagLog("TMDRAW", "before vao_bind");
		GLMeta::vaoBind(vao);
		vitaDiagLog("TMDRAW", "after vao_bind");

		vitaDiagLog("TMDRAW", "before draw_elements ground=%d", groundQuads);
		gl.DrawElements(GL_TRIANGLES, groundQuads*6, _GL_INDEX_TYPE, 0);
		vitaDiagLog("TMDRAW", "after draw_elements");

		GLMeta::vaoUnbind(vao);
	}

	void drawAbove()
	{
		if (aboveQuads == 0 || !shared)
			return;

		const TEXFBO &atlas = shared->atlas;

		SimpleShader &shader = shState->shaders().simple;
		shader.bind();
		shader.setTexSize(Vec2i(atlas.width, atlas.height));
		shader.applyViewportProj();
		shader.setTranslation(dispPos);

		if (atlas.selfHires != nullptr) {
			TEX::bind(atlas.selfHires->tex);
		}
		else {
			TEX::bind(atlas.tex);
		}
		GLMeta::vaoBind(vao);

		gl.DrawElements(GL_TRIANGLES, aboveQuads*6, _GL_INDEX_TYPE,
		                (GLvoid*) (groundQuads*6*sizeof(index_t)));

		GLMeta::vaoUnbind(vao);
	}

	void drawFlashLayer()
	{
		/* Flash tiles are drawn twice at half opacity, once over the
		 * ground layer, and once over the above layer */
		float alpha = (flashAlpha[flashAlphaIdx] / 255.f) / 2;
		flashMap.draw(alpha, dispPos);
	}

	void onGeometryChange(const Scene::Geometry &geo)
	{
		sceneGeo = geo;

		buffersDirty = true;
		mapViewportDirty = true;
	}

	ABOUT_TO_ACCESS_NOOP

	/* TileAtlasVX::Reader */
	void onQuads(const FloatRect *t, const FloatRect *p,
	             size_t n, bool overPlayer)
	{
		SVertex *vert = allocVert(overPlayer ? aboveVert : groundVert, n*4);

		for (size_t i = 0; i < n; ++i)
			Quad::setTexPosRect(&vert[i*4], t[i], p[i]);
	}
};

void TilemapVX::BitmapArray::set(int i, Bitmap *bitmap)
{
	if (!p)
		return;

	if (i < 0 || i >= BM_COUNT)
		return;

	if (p->bitmaps[i] == bitmap)
		return;

	p->bitmaps[i] = bitmap;
	p->atlasDirty = true;

	p->bmChangedCons[i].disconnect();
	p->bmDisposedCons[i].disconnect();

	if (nullOrDisposed(bitmap))
	{
		p->bitmaps[i] = 0;
		return;
	}

	p->bmChangedCons[i] = bitmap->modified.connect
        (&TilemapVXPrivate::invalidateAtlas, p);

	p->bmDisposedCons[i] = bitmap->wasDisposed.connect( [i, this] { p->atlasDisposal(i); } );
}

Bitmap *TilemapVX::BitmapArray::get(int i) const
{
	if (!p)
		return 0;

	if (i < 0 || i >= BM_COUNT)
		return 0;

	return p->bitmaps[i];
}

TilemapVX::TilemapVX(Viewport *viewport)
{
	p = new TilemapVXPrivate(viewport);
	bmProxy.p = p;
}

TilemapVX::~TilemapVX()
{
	dispose();
}

void TilemapVX::update()
{
	guardDisposed();

	/* Animate tiles */
	if (++p->frameIdx >= 30*3*4)
		p->frameIdx = 0;

	const uint8_t aniIndicesA[3*4] =
		{ 0, 1, 2, 1, 0, 1, 2, 1, 0, 1, 2, 1 };
	const uint8_t aniIndicesC[3*4] =
		{ 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2 };

	uint8_t aniIdxA = aniIndicesA[p->frameIdx / 30];
	uint8_t aniIdxC = aniIndicesC[p->frameIdx / 30];

	p->aniOffset = Vec2(aniIdxA * 2 * 32, aniIdxC * 32);

	/* Animate flash */
	if (++p->flashAlphaIdx >= flashAlphaN)
		p->flashAlphaIdx = 0;
}

TilemapVX::BitmapArray &TilemapVX::getBitmapArray()
{
	guardDisposed();

	return bmProxy;
}

DEF_ATTR_RD_SIMPLE(TilemapVX, MapData, Table*, p->mapData)
DEF_ATTR_RD_SIMPLE(TilemapVX, FlashData, Table*, p->flashMap.getData())
DEF_ATTR_RD_SIMPLE(TilemapVX, Flags, Table*, p->flags)
DEF_ATTR_RD_SIMPLE(TilemapVX, OX, int, p->origin.x)
DEF_ATTR_RD_SIMPLE(TilemapVX, OY, int, p->origin.y)

Viewport *TilemapVX::getViewport() const
{
	guardDisposed();

	return p->getViewport();
}

bool TilemapVX::getVisible() const
{
	guardDisposed();

	return p->getVisible();
}

void TilemapVX::setViewport(Viewport *value)
{
	guardDisposed();

	p->setViewport(value);
	p->above.setViewport(value);
}

void TilemapVX::setMapData(Table *value)
{
	guardDisposed();

	if (p->mapData == value)
		return;

	p->mapData = value;
	p->buffersDirty = true;

	p->mapDataCon.disconnect();
	p->mapDataCon = value->modified.connect
		(&TilemapVXPrivate::invalidateBuffers, p);
}

void TilemapVX::setFlashData(Table *value)
{
	guardDisposed();

	p->flashMap.setData(value);
}

void TilemapVX::setFlags(Table *value)
{
	guardDisposed();

	if (p->flags == value)
		return;

	p->flags = value;
	p->buffersDirty = true;

	p->flagsCon.disconnect();
	p->flagsCon = value->modified.connect
		(&TilemapVXPrivate::invalidateBuffers, p);
}

void TilemapVX::setVisible(bool value)
{
	guardDisposed();

	p->setVisible(value);
	p->above.setVisible(value);
}

void TilemapVX::setOX(int value)
{
	guardDisposed();

	if (p->origin.x == value)
		return;

	p->origin.x = value;
	p->mapViewportDirty = true;
}

void TilemapVX::setOY(int value)
{
	guardDisposed();

	if (p->origin.y == value)
		return;

	p->origin.y = value;
	p->mapViewportDirty = true;
}

void TilemapVX::releaseResources()
{
	delete p;
	bmProxy.p = 0;
}
