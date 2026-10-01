#include "frame_profile.h"
/*
** gl-util.h
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

#ifndef GLUTIL_H
#define GLUTIL_H

#include "gl-fun.h"
#include "etc-internal.h"
#include "sharedstate.h"
#include "config.h"
#include "vita_diagnostic.h"

/* Struct wrapping GLuint for some light type safety */
#define DEF_GL_ID \
struct ID \
{ \
	GLuint gl; \
	explicit ID(GLuint gl = 0)  \
	    : gl(gl)  \
	{}  \
	ID &operator=(const ID &o)  \
	{  \
		gl = o.gl;  \
		return *this; \
	}  \
	bool operator==(const ID &o) const  \
	{  \
		return gl == o.gl;  \
	}  \
	bool operator!=(const ID &o) const \
	{ \
		return !(*this == o); \
	} \
};

/* 2D Texture */
namespace TEX
{
	DEF_GL_ID

/* Bounded upload accounting (Vita pool forensics).
 * Per-call TEXUP logging is far too verbose for on-device runs
 * (tens of MB per session and multi-fps slowdown). Instead:
 *  - uploads >= 1 MiB log one immediate line each (rare by design);
 *  - smaller uploads are batched: one summary line per 256 uploads.
 * Function-local statics in inline functions have a single instance
 * program-wide, so these counters are safe to keep in this header. */
#define VITA_TEXSUM_LARGE_BYTES (1048576)
#define VITA_TEXSUM_BATCH (256)
	static inline void texSumNote(const char *op, GLsizei width, GLsizei height)
	{
		static uint64_t s_smallN = 0;
		static uint64_t s_smallBytes = 0;
		uint64_t bytes = (uint64_t)(width > 0 ? width : 0) *
			(uint64_t)(height > 0 ? height : 0) * 4u;
		if (bytes >= VITA_TEXSUM_LARGE_BYTES) {
			vitaDiagLog("TEXSUM", "large op=%s %dx%d est_bytes=%llu", op,
			            (int)width, (int)height,
			            (unsigned long long)bytes);
			return;
		}
		s_smallN++;
		s_smallBytes += bytes;
		if ((s_smallN % VITA_TEXSUM_BATCH) == 0) {
			vitaDiagLog("TEXSUM", "small-batch n=%llu bytes=%llu",
			            (unsigned long long)s_smallN,
			            (unsigned long long)s_smallBytes);
		}
	}

	inline ID gen()
	{
		ID id;
		gl.GenTextures(1, &id.gl);

		return id;
	}

	static inline void del(ID id)
	{
		gl.DeleteTextures(1, &id.gl);
		vitaDiagTextureDelete(id.gl);
	}

	static inline void bind(ID id)
	{
		gl.BindTexture(GL_TEXTURE_2D, id.gl);
	}

	static inline void unbind()
	{
		bind(ID(0));
	}

	static inline void uploadImage(GLsizei width, GLsizei height, const void *data, GLenum format)
	{
        FrameProfile::Scope profile(FrameProfile::TextureUpload, uint64_t(width > 0 ? width : 0) * (height > 0 ? height : 0));
		texSumNote("uploadImage", width, height);
		gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, format, GL_UNSIGNED_BYTE, data);
#ifdef MKXPZ_VITA_DIAGNOSTICS
		GLint id = 0;
		gl.GetIntegerv(GL_TEXTURE_BINDING_2D, &id);
		vitaDiagTextureStorage(id, width, height);
#endif
	}

	static inline void uploadSubImage(GLint x, GLint y, GLsizei width, GLsizei height, const void *data, GLenum format)
	{
        FrameProfile::Scope profile(FrameProfile::TextureUpload, uint64_t(width > 0 ? width : 0) * (height > 0 ? height : 0));
		texSumNote("uploadSubImage", width, height);
		gl.TexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, format, GL_UNSIGNED_BYTE, data);
	}

	static inline void allocEmpty(GLsizei width, GLsizei height)
	{
        FrameProfile::Scope profile(FrameProfile::TextureAllocate, uint64_t(width > 0 ? width : 0) * (height > 0 ? height : 0));
		texSumNote("allocEmpty", width, height);
		gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
#ifdef MKXPZ_VITA_DIAGNOSTICS
		GLint id = 0;
		gl.GetIntegerv(GL_TEXTURE_BINDING_2D, &id);
		vitaDiagTextureStorage(id, width, height);
#endif
	}

	static inline void setRepeat(bool mode)
	{
		gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, mode ? GL_REPEAT : GL_CLAMP_TO_EDGE);
		gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, mode ? GL_REPEAT : GL_CLAMP_TO_EDGE);
	}

	static inline void setSmooth(bool mode)
	{
		if (mode && shState->config().smoothScalingMipmaps) {
			gl.GenerateMipmap(GL_TEXTURE_2D);
			gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		} else {
			gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mode ? GL_LINEAR : GL_NEAREST);
		}

		gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mode ? GL_LINEAR : GL_NEAREST);
	}
}

/* Framebuffer Object */
namespace FBO
{
	DEF_GL_ID

	extern ID boundFramebufferID;

	inline ID gen()
	{
		ID id;
		gl.GenFramebuffers(1, &id.gl);

		return id;
	}

	static inline void del(ID id)
	{
		gl.DeleteFramebuffers(1, &id.gl);
	}

	static inline void bind(ID id)
	{
		FrameProfile::Scope profile(FrameProfile::FramebufferBind);
		boundFramebufferID = id;
		gl.BindFramebuffer(GL_FRAMEBUFFER, id.gl);
	}

	static inline void unbind()
	{
		bind(ID(0));
	}

	static inline void setTarget(TEX::ID target, unsigned colorAttach = 0)
	{
		gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + colorAttach, GL_TEXTURE_2D, target.gl, 0);
	}

	static inline void clear()
	{
        SharedState *preScope = SharedState::instance;
        {
            FrameProfile::Scope profile(FrameProfile::FrameClear);
            SharedState *postScope = SharedState::instance;
            if (postScope != preScope)
                vitaDiagLog("ERROR", "SPLITTRAP step=after_scope entry=%p now=%p",
                            (void *)preScope, (void *)postScope);
            gl.Clear(GL_COLOR_BUFFER_BIT);
            SharedState *postClear = SharedState::instance;
            if (postClear != preScope)
                vitaDiagLog("ERROR", "SPLITTRAP step=after_glclear entry=%p now=%p",
                            (void *)preScope, (void *)postClear);
        }
        SharedState *postDtor = SharedState::instance;
        if (postDtor != preScope)
            vitaDiagLog("ERROR", "SPLITTRAP step=after_scopedtor entry=%p now=%p",
                        (void *)preScope, (void *)postDtor);
	}
}

#ifdef MKXPZ_VITA_DIAGNOSTICS
static inline void vitaFboDiag(const char *phase,
							   GLuint tex = 0,
							   GLuint fbo = 0,
							   int width = 0,
							   int height = 0)
{
	vitaDiagLog("DEBUG-FBO-20260804",
				"phase=%s tex=%u fbo=%u size=%dx%d gl_error=0x%04x",
				phase, static_cast<unsigned int>(tex), static_cast<unsigned int>(fbo),
				width, height, static_cast<unsigned int>(gl.GetError()));
}
#else
static inline void vitaFboDiag(const char *, GLuint = 0, GLuint = 0,
							   int = 0, int = 0)
{}
#endif

template<GLenum target>
struct GenericBO
{
	DEF_GL_ID

	static inline ID gen()
	{
		ID id;
		gl.GenBuffers(1, &id.gl);

		return id;
	}

	static inline void del(ID id)
	{
		gl.DeleteBuffers(1, &id.gl);
	}

	static inline void bind(ID id)
	{
		gl.BindBuffer(target, id.gl);
	}

	static inline void unbind()
	{
		bind(ID(0));
	}

	static inline void uploadData(GLsizeiptr size, const GLvoid *data, GLenum usage = GL_STATIC_DRAW)
	{
		gl.BufferData(target, size, data, usage);
	}

	static inline void uploadSubData(GLintptr offset, GLsizeiptr size, const GLvoid *data)
	{
		gl.BufferSubData(target, offset, size, data);
	}

	static inline void allocEmpty(GLsizeiptr size, GLenum usage = GL_STATIC_DRAW)
	{
		uploadData(size, 0, usage);
	}
};

/* Vertex Buffer Object */
typedef struct GenericBO<GL_ARRAY_BUFFER> VBO;

/* Index Buffer Object */
typedef struct GenericBO<GL_ELEMENT_ARRAY_BUFFER> IBO;

#undef DEF_GL_ID

/* Convenience struct wrapping a framebuffer
 * and a 2D texture as its target */
struct TEXFBO
{
	TEX::ID tex;
	FBO::ID fbo;
	int width, height;

	TEXFBO *selfHires;

	explicit TEXFBO(const char *diagnosticName = 0)
	    : tex(0), fbo(0), width(0), height(0), selfHires(nullptr)
	{
	#ifdef MKXPZ_VITA_DIAGNOSTICS
		vitaDiagLog("MEMBER", "constructor name=%s type=TEXFBO gl_error=0x%04x",
		            diagnosticName ? diagnosticName : "<unnamed>",
		            static_cast<unsigned int>(gl.GetError()));
	#else
		(void) diagnosticName;
	#endif
	}

	bool operator==(const TEXFBO &other) const
	{
		return (tex == other.tex) && (fbo == other.fbo);
	}

	static inline void init(TEXFBO &obj)
	{
		FrameProfile::Scope profile(FrameProfile::RenderTargetAlloc);
		vitaFboDiag("init_begin");
		obj.tex = TEX::gen();
		vitaFboDiag("init_tex_generated", obj.tex.gl);
		obj.fbo = FBO::gen();
		vitaFboDiag("init_fbo_generated", obj.tex.gl, obj.fbo.gl);
		TEX::bind(obj.tex);
		vitaFboDiag("init_texture_bound", obj.tex.gl, obj.fbo.gl);
		TEX::setRepeat(false);
		vitaFboDiag("init_repeat_set", obj.tex.gl, obj.fbo.gl);
		TEX::setSmooth(false);
		vitaFboDiag("init_filter_set", obj.tex.gl, obj.fbo.gl);
	}

	static inline void allocEmpty(TEXFBO &obj, int width, int height)
	{
		FrameProfile::Scope profile(FrameProfile::RenderTargetAlloc, uint64_t(width > 0 ? width : 0) * (height > 0 ? height : 0));
		vitaFboDiag("alloc_begin", obj.tex.gl, obj.fbo.gl, width, height);
		vitaDiagLogMemory("texfbo_alloc_begin");
		vitaDiagLogVglMemory("texfbo_alloc_begin");
		TEX::bind(obj.tex);
		vitaFboDiag("alloc_texture_bound", obj.tex.gl, obj.fbo.gl, width, height);
		TEX::allocEmpty(width, height);
		vitaDiagLogMemory("texfbo_alloc_done");
		vitaDiagLogVglMemory("texfbo_alloc_done");
		vitaFboDiag("alloc_teximage_done", obj.tex.gl, obj.fbo.gl, width, height);
		obj.width = width;
		obj.height = height;
	}

	static inline void linkFBO(TEXFBO &obj)
	{
		FrameProfile::Scope profile(FrameProfile::RenderTargetAlloc);
		vitaFboDiag("link_begin", obj.tex.gl, obj.fbo.gl, obj.width, obj.height);
		FBO::bind(obj.fbo);
		vitaFboDiag("link_framebuffer_bound", obj.tex.gl, obj.fbo.gl, obj.width, obj.height);
		FBO::setTarget(obj.tex);
		vitaFboDiag("link_attachment_done", obj.tex.gl, obj.fbo.gl, obj.width, obj.height);
	}

	static inline void fini(TEXFBO &obj)
	{
		FrameProfile::Scope profile(FrameProfile::RenderTargetAlloc);
		vitaFboDiag("fini_begin", obj.tex.gl, obj.fbo.gl, obj.width, obj.height);
		FBO::del(obj.fbo);
		TEX::del(obj.tex);
		vitaFboDiag("fini_done", obj.tex.gl, obj.fbo.gl, obj.width, obj.height);
	}

	static inline void clear(TEXFBO &obj)
	{
		obj.tex = TEX::ID(0);
		obj.fbo = FBO::ID(0);
		obj.width = obj.height = 0;
	}
};

#endif // GLUTIL_H
