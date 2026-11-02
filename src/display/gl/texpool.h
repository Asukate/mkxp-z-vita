/*
** texpool.h
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

#ifndef TEXPOOL_H
#define TEXPOOL_H

#include "gl-util.h"

struct TexPoolPrivate;

class TexPool
{
public:
	TexPool(uint32_t maxMemSize = 20000000 /* 20 MB */);
	~TexPool();

	TEXFBO request(int width, int height);
	/* A render target holding these RGBA pixels. A new texture gets its
	 * storage once, straight from the pixels: request() then an upload
	 * would reserve the image twice (vitaGL replaces the storage on a full
	 * upload and frees the old one only after the frame), zero-fill the
	 * first copy, and leave the framebuffer on the replaced storage. */
	TEXFBO requestImage(int width, int height, const void *pixels);
	void release(TEXFBO &obj);
	/* Frees obj now instead of caching it. */
	void releaseUncached(TEXFBO &obj);

	// Release only unused textures retained by the pool.
	void clearCache();
	void disable();

private:
	TexPoolPrivate *p;
};

/* Called before graphics memory is reserved for a new texture of this many
 * bytes: gives back images kept only for reuse (Bitmap) so they never cause
 * the allocation to fail. Defined in bitmap.cpp. */
void texMakeRoom(size_t bytes);

#endif // TEXPOOL_H
