/*
** texpool.cpp
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

#include "texpool.h"
#include "exception.h"
#include "sharedstate.h"
#include "glstate.h"
#include "boost-hash.h"
#include "debugwriter.h"

#include <list>
#include <utility>
#include <assert.h>
#include <string.h>
#include <map>
#include <vector>
#include <algorithm>
#include <stdio.h>

/* Render targets handed out and not yet returned, by size, for the memory
 * snapshot: what bitmaps and the engine itself hold in graphics memory. */
static std::map<std::pair<int, int>, int> liveTargets;
static size_t liveTargetBytes = 0;
static size_t poolCacheBytes = 0;
static void countTarget(int w, int h, int by)
{
	if ((liveTargets[std::make_pair(w, h)] += by) <= 0)
		liveTargets.erase(std::make_pair(w, h));
	liveTargetBytes += (size_t)w * h * 4 * by;
}
extern "C" void vitaTexPoolFigures(size_t figures[3], char *top, size_t capacity)
{
	size_t count = 0;
	std::vector<std::pair<size_t, std::pair<int, int> > > order;
	for (std::map<std::pair<int, int>, int>::const_iterator i = liveTargets.begin(); i != liveTargets.end(); ++i)
	{
		count += i->second;
		order.push_back(std::make_pair((size_t)i->first.first * i->first.second * 4 * i->second, i->first));
	}
	std::sort(order.rbegin(), order.rend());
	figures[0] = count;
	figures[1] = liveTargetBytes;
	figures[2] = poolCacheBytes;
	size_t used = 0;
	if (capacity)
		top[0] = 0;
	for (size_t i = 0; i < order.size() && i < 8 && used + 24 < capacity; ++i)
		used += snprintf(top + used, capacity - used, "%s%dx%d*%d", i ? "," : "",
		                 order[i].second.first, order[i].second.second,
		                 liveTargets[order[i].second]);
}

typedef std::pair<uint16_t, uint16_t> Size;

static uint32_t byteCount(Size &s)
{
	return s.first * s.second * 4;
}

struct CacheNode
{
	TEXFBO obj;
	std::list<TEXFBO>::iterator prioIter;

	bool operator==(const CacheNode &o) const
	{
		return obj == o.obj;
	}
};

typedef std::list<CacheNode> CNodeList;

struct TexPoolPrivate
{
	/* Contains all cached TexFBOs, grouped by size */
	BoostHash<Size, CNodeList> poolHash;

	/* Contains all cached TexFBOs, sorted by release time */
	std::list<TEXFBO> priorityQueue;

	/* Maximal allowed cache memory */
	const uint32_t maxMemSize;

	/* Current amound of memory consumed by the cache */
	uint32_t memSize;

	/* Current amount of TexFBOs cached */
	uint16_t objCount;

	/* Has this pool been disabled? */
	bool disabled;

	TexPoolPrivate(uint32_t maxMemSize)
	    : maxMemSize(maxMemSize),
	      memSize(0),
	      objCount(0),
	      disabled(false)
	{}
};

TexPool::TexPool(uint32_t maxMemSize)
{
	p = new TexPoolPrivate(maxMemSize);
}

TexPool::~TexPool()
{
	clearCache();
	delete p;
}

void TexPool::clearCache()
{
	std::list<TEXFBO>::iterator iter;

	for (iter = p->priorityQueue.begin();
	     iter != p->priorityQueue.end();
	     ++iter)
	{
		TEXFBO obj = *iter;
		TEXFBO::fini(obj);
		--p->objCount;
	}

	assert(p->objCount == 0);
	p->priorityQueue.clear();
	p->poolHash.clear();
	p->memSize = 0;
}

TEXFBO TexPool::request(int width, int height)
{
	CacheNode cnode;
	Size size(width, height);

	/* See if we can statisfy request from cache */
	CNodeList &bucket = p->poolHash[size];

	if (!bucket.empty())
	{
		/* Found one! */
		cnode = bucket.back();
		bucket.pop_back();

		p->priorityQueue.erase(cnode.prioIter);

		p->memSize -= byteCount(size);
		--p->objCount;
		poolCacheBytes = p->memSize;
		countTarget(width, height, 1);

//		Debug() << "TexPool: <?+> (" << width << height << ")";

		return cnode.obj;
	}

	int maxSize = glState.caps.maxTexSize;
	if (width > maxSize || height > maxSize)
		throw Exception(Exception::MKXPError,
		                "Texture dimensions [%d, %d] exceed hardware capabilities",
		                width, height);

	/* Nope, create it instead */
	TEXFBO::init(cnode.obj);
	try
	{
		TEXFBO::allocEmpty(cnode.obj, width, height);
	}
	catch (const Exception &)
	{
		/* Cached objects have no owner; give their storage back and try
		 * once more before reporting the failure. */
		if (p->objCount == 0)
			throw;
		clearCache();
		TEXFBO::init(cnode.obj);
		TEXFBO::allocEmpty(cnode.obj, width, height);
	}
	TEXFBO::linkFBO(cnode.obj);
	countTarget(width, height, 1);

//	Debug() << "TexPool: <?-> (" << width << height << ")";

	return cnode.obj;
}

void TexPool::release(TEXFBO &obj)
{
	if (obj.tex == TEX::ID(0) || obj.fbo == FBO::ID(0))
	{
		TEXFBO::fini(obj);
		return;
	}

	if (p->disabled)
	{
		/* If we're disabled, delete without caching */
//		Debug() << "TexPool: <!#> (" << obj.width << obj.height << ")";
		TEXFBO::fini(obj);
		return;
	}

	Size size(obj.width, obj.height);
	countTarget(obj.width, obj.height, -1);

	uint32_t newMemSize = p->memSize + byteCount(size);

	/* If caching this object would spill over the allowed memory budget,
	 * delete least used objects until we're good again */
	// VitaGL has a fixed framebuffer table. Small cached textures can fill
	// it long before reaching the byte budget, starving active windows.
#ifdef __vita__
	while (newMemSize > p->maxMemSize || p->objCount >= 64)
#else
	while (newMemSize > p->maxMemSize)
#endif
	{
		if (p->objCount == 0)
			break;

//		Debug() << "TexPool: <!~> Size:" << p->memSize;

		/* Retrieve object with lowest priority for deletion */
		CacheNode last;
		last.obj = p->priorityQueue.back();
		Size removedSize(last.obj.width, last.obj.height);

		CNodeList &bucket = p->poolHash[removedSize];

		std::list<CacheNode>::iterator toRemove =
		        std::find(bucket.begin(), bucket.end(), last);
		assert(toRemove != bucket.end());
		bucket.erase(toRemove);

		p->priorityQueue.pop_back();

		TEXFBO::fini(last.obj);

		newMemSize -= byteCount(removedSize);
		--p->objCount;

//		Debug() << "TexPool: <!-> (" << last.obj.width << last.obj.height << ")";
	}

	p->memSize = newMemSize;
	poolCacheBytes = newMemSize;

	/* Retain object */
	p->priorityQueue.push_front(obj);
	CacheNode cnode;
	cnode.obj = obj;
	cnode.prioIter = p->priorityQueue.begin();
	CNodeList &bucket = p->poolHash[size];
	bucket.push_back(cnode);

	++p->objCount;

//	Debug() << "TexPool: <!+> (" << obj.width << obj.height << ") Current size:" << p->memSize;
}

void TexPool::disable()
{
	p->disabled = true;
}


