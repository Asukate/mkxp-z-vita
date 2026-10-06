/*
** gl-meta.cpp
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

#include "gl-meta.h"
#include "gl-fun.h"
#include "sharedstate.h"
#include "glstate.h"
#include "quad.h"
#include "config.h"
#include "etc.h"
#include "exception.h"
#include "texpool.h"

namespace FBO
{
	ID boundFramebufferID;

	ID gen()
	{
		ID id;
		gl.GenFramebuffers(1, &id.gl);
#ifdef __vita__
		if (!id.gl)
		{
			// Cached textures must not consume the slots needed by live windows.
			shState->texPool().clearCache();
			gl.GenFramebuffers(1, &id.gl);
		}
#endif
		if (!id.gl)
			throw Exception(Exception::MKXPError,
			                "Unable to allocate framebuffer: renderer object limit reached");
		return id;
	}
}

namespace GLMeta
{

void subRectImageUpload(GLint srcW, GLint srcX, GLint srcY,
                        GLint dstX, GLint dstY, GLsizei dstW, GLsizei dstH,
                        SDL_Surface *src, GLenum format)
{
#ifdef __vita__
	/*
	 * Vita fast path: vitaGL implements GL_UNPACK_ROW_LENGTH even though the
	 * desktop-style unpack_subimage capability bit is false.  The old fallback
	 * therefore allocated a temporary dstW x dstH SDL surface and software-
	 * blitted every sub-rectangle before uploading it.  XP mega tilesets split
	 * a tall tileset into three ~256x1600 lanes, making map transfers spend
	 * roughly a second in each avoidable SDL_BlitSurface on hardware.
	 *
	 * Address the source rectangle directly and describe its pitch with
	 * UNPACK_ROW_LENGTH.  Do not use SKIP_PIXELS/SKIP_ROWS: vitaGL's upload
	 * implementation honors ROW_LENGTH but not the skip fields.
	 *
	 * Restrict this to the canonical 32-bit RGBA path used by mkxp-z's Vita
	 * bitmap/tileset surfaces.  Anything unusual retains the generic fallback.
	 */
	if (src && src->pixels && src->format && src->format->BytesPerPixel == 4 &&
	    format == GL_RGBA)
	{
		const GLint rowLen = src->pitch / 4;
		const uint8_t *pixels = static_cast<const uint8_t *>(src->pixels) +
		    (size_t)srcY * (size_t)src->pitch + (size_t)srcX * 4u;

		gl.PixelStorei(GL_UNPACK_ROW_LENGTH, rowLen);
		TEX::uploadSubImage(dstX, dstY, dstW, dstH, pixels, format);
		gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
		return;
	}
#endif

	if (gl.unpack_subimage)
	{
		gl.PixelStorei(GL_UNPACK_ROW_LENGTH, srcW);
		gl.PixelStorei(GL_UNPACK_SKIP_PIXELS, srcX);
		gl.PixelStorei(GL_UNPACK_SKIP_ROWS, srcY);

		TEX::uploadSubImage(dstX, dstY, dstW, dstH, src->pixels, format);
	}
	else
	{
		SDL_PixelFormat *form = src->format;
		SDL_Surface *tmp = SDL_CreateRGBSurface(0, dstW, dstH, form->BitsPerPixel,
		                                        form->Rmask, form->Gmask, form->Bmask, form->Amask);
		SDL_Rect srcRect = { srcX, srcY, dstW, dstH };

		SDL_BlitSurface(src, &srcRect, tmp, 0);

		TEX::uploadSubImage(dstX, dstY, dstW, dstH, tmp->pixels, format);

		SDL_FreeSurface(tmp);
	}
}

void subRectImageEnd()
{
	if (gl.unpack_subimage)
	{
		gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
		gl.PixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
		gl.PixelStorei(GL_UNPACK_SKIP_ROWS, 0);
	}
}

#define HAVE_NATIVE_VAO gl.GenVertexArrays

#ifdef MKXPZ_VITA_DIAGNOSTICS
static unsigned int vitaVaoTraceCount = 0;

static bool vitaVaoTraceEnabled()
{
	return vitaVaoTraceCount++ < 96;
}
#endif

static void vaoBindRes(VAO &vao)
{
	#ifdef MKXPZ_VITA_DIAGNOSTICS
	if (vitaVaoTraceEnabled())
		vitaDiagLog("VAO", "bind_res_begin vbo=%u ibo=%u attrs=%lu stride=%d",
		            (unsigned int) vao.vbo.gl, (unsigned int) vao.ibo.gl,
		            (unsigned long) vao.attrCount, (int) vao.vertSize);
	#endif

	VBO::bind(vao.vbo);
	IBO::bind(vao.ibo);

	for (size_t i = 0; i < vao.attrCount; ++i)
	{
		const VertexAttribute &va = vao.attr[i];

		#ifdef MKXPZ_VITA_DIAGNOSTICS
		if (vitaVaoTraceEnabled())
			vitaDiagLog("VAO", "bind_res_attr i=%lu index=%d size=%d type=0x%04x offset=%p",
			            (unsigned long) i, (int) va.index, (int) va.size,
			            (unsigned int) va.type, va.offset);
		#endif

		gl.EnableVertexAttribArray(va.index);
		gl.VertexAttribPointer(va.index, va.size, va.type, GL_FALSE, vao.vertSize, va.offset);
	}

	#ifdef MKXPZ_VITA_DIAGNOSTICS
	if (vitaVaoTraceEnabled())
		vitaDiagLog("VAO", "bind_res_done gl_error=0x%04x",
		            (unsigned int) gl.GetError());
	#endif
}

void vaoInit(VAO &vao, bool keepBound)
{
	#ifdef MKXPZ_VITA_DIAGNOSTICS
	if (vitaVaoTraceEnabled())
		vitaDiagLog("VAO", "init_begin vbo=%u ibo=%u attrs=%lu keep=%d gen=%d bind=%d",
		            (unsigned int) vao.vbo.gl, (unsigned int) vao.ibo.gl,
		            (unsigned long) vao.attrCount, keepBound ? 1 : 0,
			            gl.GenVertexArrays ? 1 : 0, gl.BindVertexArray ? 1 : 0);
	#endif

	if (!HAVE_NATIVE_VAO)
		vao.nativeVAO = 0;

	if (HAVE_NATIVE_VAO)
	{
		gl.GenVertexArrays(1, &vao.nativeVAO);
		#ifdef MKXPZ_VITA_DIAGNOSTICS
		if (vitaVaoTraceEnabled())
			vitaDiagLog("VAO", "init_generated native=%u gl_error=0x%04x",
			            (unsigned int) vao.nativeVAO,
			            (unsigned int) gl.GetError());
		#endif
		gl.BindVertexArray(vao.nativeVAO);
		vaoBindRes(vao);
		if (!keepBound)
			gl.BindVertexArray(0);
	}
	else
	{
		if (keepBound)
		{
			VBO::bind(vao.vbo);
			IBO::bind(vao.ibo);
		}
	}

	#ifdef MKXPZ_VITA_DIAGNOSTICS
	if (vitaVaoTraceEnabled())
		vitaDiagLog("VAO", "init_done native=%u gl_error=0x%04x",
		            (unsigned int) vao.nativeVAO,
		            (unsigned int) gl.GetError());
	#endif
}

void vaoFini(VAO &vao)
{
	if (HAVE_NATIVE_VAO)
		gl.DeleteVertexArrays(1, &vao.nativeVAO);
}

void vaoBind(VAO &vao)
{
	#ifdef MKXPZ_VITA_DIAGNOSTICS
	if (vitaVaoTraceEnabled())
		vitaDiagLog("VAO", "bind_begin native=%u vbo=%u ibo=%u gen=%d",
		            (unsigned int) vao.nativeVAO, (unsigned int) vao.vbo.gl,
		            (unsigned int) vao.ibo.gl, gl.GenVertexArrays ? 1 : 0);
	#endif

	if (HAVE_NATIVE_VAO)
		gl.BindVertexArray(vao.nativeVAO);
	else
		vaoBindRes(vao);

	#ifdef MKXPZ_VITA_DIAGNOSTICS
	if (vitaVaoTraceEnabled())
		vitaDiagLog("VAO", "bind_done gl_error=0x%04x",
		            (unsigned int) gl.GetError());
	#endif
}

void vaoUnbind(VAO &vao)
{
	#ifdef MKXPZ_VITA_DIAGNOSTICS
	if (vitaVaoTraceEnabled())
		vitaDiagLog("VAO", "unbind_begin native=%u gen=%d",
		            (unsigned int) vao.nativeVAO, gl.GenVertexArrays ? 1 : 0);
	#endif

	if (HAVE_NATIVE_VAO)
	{
		gl.BindVertexArray(0);
	}
	else
	{
		for (size_t i = 0; i < vao.attrCount; ++i)
			gl.DisableVertexAttribArray(vao.attr[i].index);

		VBO::unbind();
		IBO::unbind();
	}

	#ifdef MKXPZ_VITA_DIAGNOSTICS
	if (vitaVaoTraceEnabled())
		vitaDiagLog("VAO", "unbind_done gl_error=0x%04x",
		            (unsigned int) gl.GetError());
	#endif
}

#ifdef __vita__
/* vitaGL exposes glBlitFramebuffer, but copying an FBO to the default
 * framebuffer leaves the screen unchanged. Use the shader-quad path. */
#define HAVE_NATIVE_BLIT false
#else
#define HAVE_NATIVE_BLIT (gl.BlitFramebuffer && shState->config().smoothScaling <= Bilinear && shState->config().smoothScalingDown <= Bilinear)
#endif

int blitScaleIsSpecial(TEXFBO &target, bool targetPreferHires, const IntRect &targetRect, TEXFBO &source, const IntRect &sourceRect)
{
	int targetWidth = targetRect.w;
	int targetHeight = targetRect.h;

	int sourceWidth = sourceRect.w;
	int sourceHeight = sourceRect.h;

	if (targetPreferHires && target.selfHires != nullptr)
	{
		targetWidth *= target.selfHires->width;
		targetWidth /= target.width;

		targetHeight *= target.selfHires->height;
		targetHeight /= target.height;
	}

	if (source.selfHires != nullptr)
	{
		sourceWidth *= source.selfHires->width;
		sourceWidth /= source.width;

		sourceHeight *= source.selfHires->height;
		sourceHeight /= source.height;
	}

	if (targetWidth == sourceWidth && targetHeight == sourceHeight)
	{
		return SameScale;
	}

	if (targetWidth < sourceWidth && targetHeight < sourceHeight)
	{
		return DownScale;
	}

	return UpScale;
}

int smoothScalingMethod(int scaleIsSpecial)
{
	switch (scaleIsSpecial)
	{
	case SameScale:
		return NearestNeighbor;
	case DownScale:
		return shState->config().smoothScalingDown;
	}

	return shState->config().smoothScaling;
}

static void _blitBegin(FBO::ID fbo, const Vec2i &size, int scaleIsSpecial)
{
	if (HAVE_NATIVE_BLIT)
	{
		FBO::boundFramebufferID = fbo;
		gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo.gl);
	}
	else
	{
		FBO::bind(fbo);
		glState.viewport.pushSet(IntRect(0, 0, size.x, size.y));

		switch (smoothScalingMethod(scaleIsSpecial))
		{
		case Bicubic:
		{
			BicubicShader &shader = shState->shaders().bicubic;
			shader.bind();
			shader.applyViewportProj();
			shader.setTranslation(Vec2i());
			shader.setTexSize(Vec2i(size.x, size.y));
			shader.setSharpness(shState->config().bicubicSharpness);
		}

			break;
		case Lanczos3:
		{
			Lanczos3Shader &shader = shState->shaders().lanczos3;
			shader.bind();
			shader.applyViewportProj();
			shader.setTranslation(Vec2i());
			shader.setTexSize(Vec2i(size.x, size.y));
		}

			break;
#ifdef MKXPZ_SSL
		case xBRZ:
		{
			XbrzShader &shader = shState->shaders().xbrz;
			shader.bind();
			shader.applyViewportProj();
			shader.setTranslation(Vec2i());
			shader.setTexSize(Vec2i(size.x, size.y));
			shader.setTargetScale(Vec2(1., 1.));
		}

			break;
#endif
		default:
		{
			SimpleShader &shader = shState->shaders().simple;
			shader.bind();
			shader.applyViewportProj();
			shader.setTranslation(Vec2i());
			shader.setTexSize(Vec2i(size.x, size.y));
		}
		}
	}
}

int blitDstWidthLores = 1;
int blitDstWidthHires = 1;
int blitDstHeightLores = 1;
int blitDstHeightHires = 1;

int blitSrcWidthLores = 1;
int blitSrcWidthHires = 1;
int blitSrcHeightLores = 1;
int blitSrcHeightHires = 1;

#ifdef MKXPZ_VITA_DIAGNOSTICS
/*
 * S42-F: identify only the VX atlas A2 copy without touching TileAtlasVX.
 * A2 is the unique low-res atlas blit src=(0,0 512x384) ->
 * dst=(0,416 512x384) while the destination atlas is 1024x2048.
 * TEXTFLOW is intentionally used because vitaDiagLog flushes that tag on each
 * line; this probe runs only a handful of times per atlas rebuild, not per
 * frame.  A glFinish fence after the A2 Quad::draw forces the GXM scene to
 * complete so asynchronous bad GPU work cannot hide behind later CPU code.
 */
static bool s42fAtlasProbeActive = false;
static unsigned int s42fAtlasBuildSerial = 0;
static unsigned int s42fAtlas512SourceOrdinal = 0;
static unsigned int s42fA2FenceCount = 0;
#endif

void blitBegin(TEXFBO &target, bool preferHires, int scaleIsSpecial)
{
	blitDstWidthLores = target.width;
	blitDstHeightLores = target.height;

#ifdef MKXPZ_VITA_DIAGNOSTICS
	s42fAtlasProbeActive = (target.width == 1024 && target.height == 2048);
	if (s42fAtlasProbeActive)
	{
		++s42fAtlasBuildSerial;
		s42fAtlas512SourceOrdinal = 0;
		vitaDiagLog("TEXTFLOW",
		            "S42F build=%u ATLAS_BEGIN target=%dx%d tex=%u fbo=%u selfHires=%p",
		            s42fAtlasBuildSerial, target.width, target.height,
		            (unsigned int) target.tex.gl, (unsigned int) target.fbo.gl,
		            (void *) target.selfHires);
	}
#endif

	if (preferHires && target.selfHires != nullptr) {
		blitDstWidthHires = target.selfHires->width;
		blitDstHeightHires = target.selfHires->height;
		_blitBegin(target.selfHires->fbo, Vec2i(target.selfHires->width, target.selfHires->height), scaleIsSpecial);
	}
	else {
		blitDstWidthHires = blitDstWidthLores;
		blitDstHeightHires = blitDstHeightLores;
		_blitBegin(target.fbo, Vec2i(target.width, target.height), scaleIsSpecial);
	}
}

void blitBeginScreen(const Vec2i &size, int scaleIsSpecial)
{
	blitDstWidthLores = 1;
	blitDstWidthHires = 1;
	blitDstHeightLores = 1;
	blitDstHeightHires = 1;

	_blitBegin(FBO::ID(0), size, scaleIsSpecial);
}

void blitSource(TEXFBO &source, int scaleIsSpecial)
{
	blitSrcWidthLores = source.width;
	blitSrcHeightLores = source.height;
	if (source.selfHires != nullptr) {
		blitSrcWidthHires = source.selfHires->width;
		blitSrcHeightHires = source.selfHires->height;
	}
	else {
		blitSrcWidthHires = blitSrcWidthLores;
		blitSrcHeightHires = blitSrcHeightLores;
	}

#ifdef MKXPZ_VITA_DIAGNOSTICS
	if (s42fAtlasProbeActive && source.width == 512 && source.height == 384)
	{
		++s42fAtlas512SourceOrdinal;
		vitaDiagLog("TEXTFLOW",
		            "S42F build=%u SOURCE512 ordinal=%u src=%p tex=%u fbo=%u size=%dx%d selfHires=%p hiTex=%u hiFbo=%u hiSize=%dx%d",
		            s42fAtlasBuildSerial, s42fAtlas512SourceOrdinal,
		            (void *) &source, (unsigned int) source.tex.gl,
		            (unsigned int) source.fbo.gl, source.width, source.height,
		            (void *) source.selfHires,
		            source.selfHires ? (unsigned int) source.selfHires->tex.gl : 0u,
		            source.selfHires ? (unsigned int) source.selfHires->fbo.gl : 0u,
		            source.selfHires ? source.selfHires->width : 0,
		            source.selfHires ? source.selfHires->height : 0);
	}
#endif

	if (HAVE_NATIVE_BLIT)
	{
		gl.BindFramebuffer(GL_READ_FRAMEBUFFER, source.fbo.gl);
	}
	else
	{
		switch (smoothScalingMethod(scaleIsSpecial))
		{
		case Bicubic:
		{
			BicubicShader &shader = shState->shaders().bicubic;
			shader.bind();
			shader.setTexSize(Vec2i(blitSrcWidthHires, blitSrcHeightHires));
		}

			break;
		case Lanczos3:
		{
			Lanczos3Shader &shader = shState->shaders().lanczos3;
			shader.bind();
			shader.setTexSize(Vec2i(blitSrcWidthHires, blitSrcHeightHires));
		}

			break;
#ifdef MKXPZ_SSL
		case xBRZ:
		{
			XbrzShader &shader = shState->shaders().xbrz;
			shader.bind();
			shader.setTexSize(Vec2i(blitSrcWidthHires, blitSrcHeightHires));
		}

			break;
#endif
		default:
		{
			SimpleShader &shader = shState->shaders().simple;
			shader.bind();
			shader.setTexSize(Vec2i(blitSrcWidthHires, blitSrcHeightHires));
		}
		}
		if (source.selfHires != nullptr) {
			TEX::bind(source.selfHires->tex);
		}
		else {
			TEX::bind(source.tex);
		}
	}

#ifdef MKXPZ_VITA_DIAGNOSTICS
	if (s42fAtlasProbeActive && source.width == 512 && source.height == 384)
	{
		vitaDiagLog("TEXTFLOW",
		            "S42F build=%u SOURCE512_RETURN ordinal=%u srcLo=%dx%d srcHi=%dx%d glerr=0x%04x",
		            s42fAtlasBuildSerial, s42fAtlas512SourceOrdinal,
		            blitSrcWidthLores, blitSrcHeightLores,
		            blitSrcWidthHires, blitSrcHeightHires,
		            (unsigned int) gl.GetError());
	}
#endif
}

void blitRectangle(const IntRect &src, const Vec2i &dstPos)
{
	blitRectangle(src, IntRect(dstPos.x, dstPos.y, src.w, src.h), false);
}

void blitRectangle(const IntRect &src, const IntRect &dst, bool smooth)
{
#ifdef MKXPZ_VITA_DIAGNOSTICS
	const bool s42fA2 = s42fAtlasProbeActive &&
		(src.x == 0 && src.y == 0 && src.w == 512 && src.h == 384) &&
		(dst.x == 0 && dst.y == 416 && dst.w == 512 && dst.h == 384) &&
		(blitDstWidthLores == 1024 && blitDstHeightLores == 2048);
	if (s42fA2)
	{
		vitaDiagLog("TEXTFLOW",
		            "S42F build=%u A2_RECT_ENTER src=%d,%d %dx%d dst=%d,%d %dx%d srcLo=%dx%d srcHi=%dx%d dstLo=%dx%d dstHi=%dx%d boundFbo=%u",
		            s42fAtlasBuildSerial,
		            src.x, src.y, src.w, src.h,
		            dst.x, dst.y, dst.w, dst.h,
		            blitSrcWidthLores, blitSrcHeightLores,
		            blitSrcWidthHires, blitSrcHeightHires,
		            blitDstWidthLores, blitDstHeightLores,
		            blitDstWidthHires, blitDstHeightHires,
		            (unsigned int) FBO::boundFramebufferID.gl);
	}
#endif

	// Handle high-res dest
	int scaledDstX = dst.x * blitDstWidthHires / blitDstWidthLores;
	int scaledDstY = dst.y * blitDstHeightHires / blitDstHeightLores;
	int scaledDstWidth = dst.w * blitDstWidthHires / blitDstWidthLores;
	int scaledDstHeight = dst.h * blitDstHeightHires / blitDstHeightLores;
	IntRect dstScaled(scaledDstX, scaledDstY, scaledDstWidth, scaledDstHeight);

	// Handle high-res source
	int scaledSrcX = src.x * blitSrcWidthHires / blitSrcWidthLores;
	int scaledSrcY = src.y * blitSrcHeightHires / blitSrcHeightLores;
	int scaledSrcWidth = src.w * blitSrcWidthHires / blitSrcWidthLores;
	int scaledSrcHeight = src.h * blitSrcHeightHires / blitSrcHeightLores;
	IntRect srcScaled(scaledSrcX, scaledSrcY, scaledSrcWidth, scaledSrcHeight);

#ifdef MKXPZ_VITA_DIAGNOSTICS
	if (s42fA2)
	{
		vitaDiagLog("TEXTFLOW",
		            "S42F build=%u A2_SCALED src=%d,%d %dx%d dst=%d,%d %dx%d",
		            s42fAtlasBuildSerial,
		            srcScaled.x, srcScaled.y, srcScaled.w, srcScaled.h,
		            dstScaled.x, dstScaled.y, dstScaled.w, dstScaled.h);
	}
#endif

	if (HAVE_NATIVE_BLIT)
	{
		gl.BlitFramebuffer(srcScaled.x, srcScaled.y, srcScaled.x+srcScaled.w, srcScaled.y+srcScaled.h,
		                   dstScaled.x, dstScaled.y, dstScaled.x+dstScaled.w, dstScaled.y+dstScaled.h,
		                   GL_COLOR_BUFFER_BIT, smooth ? GL_LINEAR : GL_NEAREST);
	}
	else
	{
#ifdef MKXPZ_SSL
		if (shState->config().smoothScaling == xBRZ)
		{
			XbrzShader &shader = shState->shaders().xbrz;
			shader.setTargetScale(Vec2((float)(shState->config().xbrzScalingFactor), (float)(shState->config().xbrzScalingFactor)));
		}
#endif
		if (smooth)
			TEX::setSmooth(true);

#ifdef MKXPZ_VITA_DIAGNOSTICS
		if (s42fA2)
			vitaDiagLog("TEXTFLOW", "S42F build=%u A2_BEFORE_BLEND_PUSH glerr=0x%04x",
			            s42fAtlasBuildSerial, (unsigned int) gl.GetError());
#endif

		glState.blend.pushSet(false);

#ifdef MKXPZ_VITA_DIAGNOSTICS
		if (s42fA2)
			vitaDiagLog("TEXTFLOW", "S42F build=%u A2_AFTER_BLEND_PUSH",
			            s42fAtlasBuildSerial);
#endif

		Quad &quad = shState->gpQuad();

#ifdef MKXPZ_VITA_DIAGNOSTICS
		if (s42fA2)
			vitaDiagLog("TEXTFLOW", "S42F build=%u A2_GOT_QUAD quad=%p",
			            s42fAtlasBuildSerial, (void *) &quad);
#endif

		quad.setTexPosRect(srcScaled, dstScaled);

#ifdef MKXPZ_VITA_DIAGNOSTICS
		if (s42fA2)
			vitaDiagLog("TEXTFLOW", "S42F build=%u A2_AFTER_QUAD_SET before_draw glerr=0x%04x",
			            s42fAtlasBuildSerial, (unsigned int) gl.GetError());
#endif

		quad.draw();

#ifdef MKXPZ_VITA_DIAGNOSTICS
		if (s42fA2)
			vitaDiagLog("TEXTFLOW", "S42F build=%u A2_AFTER_QUAD_DRAW glerr=0x%04x",
			            s42fAtlasBuildSerial, (unsigned int) gl.GetError());
#endif

		glState.blend.pop();

#ifdef MKXPZ_VITA_DIAGNOSTICS
		if (s42fA2)
			vitaDiagLog("TEXTFLOW", "S42F build=%u A2_AFTER_BLEND_POP",
			            s42fAtlasBuildSerial);
#endif

		if (smooth)
			TEX::setSmooth(false);

#ifdef MKXPZ_VITA_DIAGNOSTICS
		if (s42fA2)
		{
			++s42fA2FenceCount;
			vitaDiagLog("TEXTFLOW",
			            "S42F build=%u A2_GLFINISH_BEGIN fence=%u",
			            s42fAtlasBuildSerial, s42fA2FenceCount);
			::glFinish();
			vitaDiagLog("TEXTFLOW",
			            "S42F build=%u A2_GLFINISH_RETURN fence=%u glerr=0x%04x",
			            s42fAtlasBuildSerial, s42fA2FenceCount,
			            (unsigned int) gl.GetError());
		}
#endif
	}
}

void blitEnd()
{
	blitDstWidthLores = 1;
	blitDstWidthHires = 1;
	blitDstHeightLores = 1;
	blitDstHeightHires = 1;

	blitSrcWidthLores = 1;
	blitSrcWidthHires = 1;
	blitSrcHeightLores = 1;
	blitSrcHeightHires = 1;

	if (!HAVE_NATIVE_BLIT) {
		glState.viewport.pop();
	}

#ifdef MKXPZ_VITA_DIAGNOSTICS
	if (s42fAtlasProbeActive)
		vitaDiagLog("TEXTFLOW", "S42F build=%u ATLAS_BLIT_END",
		            s42fAtlasBuildSerial);
	s42fAtlasProbeActive = false;
#endif
}

}
