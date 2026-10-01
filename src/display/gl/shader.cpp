/*
** shader.cpp
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

#include "shader.h"
#include "frame_profile.h"
#ifdef __vita__
#include "program-cache.h"
#include "shader-cache-build-id.h"
#include <sys/stat.h>
#include <unistd.h>
extern "C" void glGetProgramBinary(GLuint, GLsizei, GLsizei *, GLenum *, void *);
extern "C" void glProgramBinary(GLuint, GLenum, const void *, GLsizei);
#endif
#include "config.h"
#include "graphics.h"
#include "sharedstate.h"
#include "glstate.h"
#include "exception.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <iostream>
#include <map>
#include <string>

#ifndef MKXPZ_BUILD_XCODE
#include "common.h.xxd"
#include "sprite.frag.xxd"
#include "hue.frag.xxd"
#include "trans.frag.xxd"
#include "transSimple.frag.xxd"
#include "bitmapBlit.frag.xxd"
#include "plane.frag.xxd"
#include "gray.frag.xxd"
#include "flatColor.frag.xxd"
#include "simple.frag.xxd"
#include "simpleColor.frag.xxd"
#include "simpleAlpha.frag.xxd"
#include "simpleAlphaUni.frag.xxd"
#include "tilemap.frag.xxd"
#include "flashMap.frag.xxd"
#include "bicubic.frag.xxd"
#include "lanczos3.frag.xxd"
#ifdef MKXPZ_SSL
#include "xbrz.frag.xxd"
#endif
#include "minimal.vert.xxd"
#include "simple.vert.xxd"
#include "simpleColor.vert.xxd"
#include "sprite.vert.xxd"
#include "tilemap.vert.xxd"
#include "blur.frag.xxd"
#include "simpleMatrix.vert.xxd"
#include "blurH.vert.xxd"
#include "blurV.vert.xxd"
#include "tilemapvx.vert.xxd"
#include "kglInvert.frag.xxd"
#include "kglCompressAlpha.frag.xxd"
#include "kglSubtract.frag.xxd"
#include "kglShadowH.frag.xxd"
#include "kglShadowV.frag.xxd"
#endif

#ifdef MKXPZ_BUILD_XCODE
#include "filesystem/filesystem.h"
#define INIT_SHADER(vert, frag, name) \
{ \
    std::string v = mkxp_fs::contentsOfAssetAsString("Shaders/" #vert, "vert"); \
    std::string f = mkxp_fs::contentsOfAssetAsString("Shaders/" #frag, "frag"); \
    Shader::init((const unsigned char*)v.c_str(), v.length(), (const unsigned char*)f.c_str(), f.length(), #vert, #frag, #name); \
}
#else
#define INIT_SHADER(vert, frag, name) \
{ \
	Shader::init(___shader_##vert##_vert, ___shader_##vert##_vert_len, ___shader_##frag##_frag, ___shader_##frag##_frag_len, \
	#vert, #frag, #name); \
}
#endif

struct VitaShaderSourceInfo
{
	size_t length;
	uint32_t hash;
	size_t segments;
	std::string firstLine;

	VitaShaderSourceInfo()
	    : length(0), hash(2166136261u), segments(0)
	{}
};

#ifdef MKXPZ_VITA_DIAGNOSTICS
namespace
{
const size_t kVitaShaderInfoLogCap = 240;

std::map<GLuint, std::string> vitaProgramNames;

const char *vitaShaderName(const char *name)
{
	return (name && name[0]) ? name : "<unnamed>";
}

uint32_t vitaHashUpdate(uint32_t hash, const char *data, size_t length)
{
	for (size_t i = 0; i < length; ++i)
	{
		hash ^= static_cast<unsigned char>(data[i]);
		hash *= 16777619u;
	}

	return hash;
}

std::string vitaFirstLine(const char *data, size_t length)
{
	size_t lineLength = 0;
	while (lineLength < length && data[lineLength] != '\n' && data[lineLength] != '\r')
		++lineLength;

	std::string line(data, lineLength);
	for (size_t i = 0; i < line.size(); ++i)
		if (line[i] == '\t')
			line[i] = ' ';

	if (line.size() > 160)
		line.resize(160);

	return line;
}

void vitaSourceInfoAdd(VitaShaderSourceInfo &info, const char *data, size_t length)
{
	if (!data)
		return;

	if (info.segments == 0)
		info.firstLine = vitaFirstLine(data, length);

	info.length += length;
	info.hash = vitaHashUpdate(info.hash, data, length);
	++info.segments;
}

std::string vitaCappedInfoLog(const char *data, size_t length)
{
	std::string result;
	result.reserve(length < kVitaShaderInfoLogCap ? length : kVitaShaderInfoLogCap);

	const size_t capped = length < kVitaShaderInfoLogCap ? length : kVitaShaderInfoLogCap;
	for (size_t i = 0; i < capped; ++i)
	{
		const char c = data[i];
		if (c == '\n')
			result += "\\n";
		else if (c == '\r')
			result += "\\r";
		else if (c == '\t')
			result += ' ';
		else
			result += c;
	}

	if (length > kVitaShaderInfoLogCap)
		result += "...";

	return result;
}

void vitaRegisterProgram(GLuint program, const char *name)
{
	if (program != 0)
		vitaProgramNames[program] = vitaShaderName(name);
}

const char *vitaProgramName(GLuint program)
{
	if (program == 0)
		return "<none>";

	std::map<GLuint, std::string>::const_iterator it = vitaProgramNames.find(program);
	return it == vitaProgramNames.end() ? "<unknown>" : it->second.c_str();
}
}
#else
static void vitaSourceInfoAdd(VitaShaderSourceInfo &, const char *, size_t)
{}
#endif

static GLint vitaGetUniformLocation(GLuint program, const char *programName,
                                    const char *uniformName)
{
#ifdef MKXPZ_VITA_DIAGNOSTICS
	vitaDiagLog("SHADER", "before api=glGetUniformLocation program=%u name=%s uniform=%s gl_error=0x%04x",
	            static_cast<unsigned int>(program), vitaShaderName(programName),
	            uniformName, static_cast<unsigned int>(gl.GetError()));
#endif
	const GLint location = gl.GetUniformLocation(program, uniformName);
#ifdef MKXPZ_VITA_DIAGNOSTICS
	vitaDiagLog("SHADER", "after api=glGetUniformLocation program=%u name=%s uniform=%s location=%d gl_error=0x%04x",
	            static_cast<unsigned int>(program), vitaShaderName(programName),
	            uniformName, static_cast<int>(location), static_cast<unsigned int>(gl.GetError()));
#endif
	return location;
}

static void vitaBindAttribLocation(GLuint program, const char *programName,
                                   GLuint index, const char *attributeName)
{
#ifdef MKXPZ_VITA_DIAGNOSTICS
	vitaDiagLog("SHADER", "before api=glBindAttribLocation program=%u name=%s index=%u attribute=%s gl_error=0x%04x",
	            static_cast<unsigned int>(program), vitaShaderName(programName),
	            static_cast<unsigned int>(index), attributeName,
	            static_cast<unsigned int>(gl.GetError()));
#endif
	gl.BindAttribLocation(program, index, attributeName);
#ifdef MKXPZ_VITA_DIAGNOSTICS
	vitaDiagLog("SHADER", "after api=glBindAttribLocation program=%u name=%s index=%u attribute=%s gl_error=0x%04x",
	            static_cast<unsigned int>(program), vitaShaderName(programName),
	            static_cast<unsigned int>(index), attributeName,
	            static_cast<unsigned int>(gl.GetError()));
#endif
}

#define GET_U(name) u_##name = vitaGetUniformLocation(program, diagnosticProgramName, #name)

#ifdef MKXPZ_BUILD_XCODE
    std::string Shader::shaderCommon = "";
#endif

static void printShaderLog(GLuint shader, GLenum shaderType,
                           const char *shaderName, const char *programName)
{
	GLint logLength = 0;
	#ifdef MKXPZ_VITA_DIAGNOSTICS
	vitaDiagLog("SHADER", "before api=glGetShaderiv shader=%u type=%s(0x%04x) name=%s program=%s pname=GL_INFO_LOG_LENGTH gl_error=0x%04x",
	            static_cast<unsigned int>(shader),
	            shaderType == GL_VERTEX_SHADER ? "GL_VERTEX_SHADER" : "GL_FRAGMENT_SHADER",
	            static_cast<unsigned int>(shaderType), vitaShaderName(shaderName),
	            vitaShaderName(programName),
            static_cast<unsigned int>(gl.GetError()));
	#endif
	gl.GetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
	#ifdef MKXPZ_VITA_DIAGNOSTICS
	vitaDiagLog("SHADER", "after api=glGetShaderiv shader=%u type=%s(0x%04x) name=%s program=%s pname=GL_INFO_LOG_LENGTH length=%d gl_error=0x%04x",
	            static_cast<unsigned int>(shader),
	            shaderType == GL_VERTEX_SHADER ? "GL_VERTEX_SHADER" : "GL_FRAGMENT_SHADER",
	            static_cast<unsigned int>(shaderType), vitaShaderName(shaderName),
	            vitaShaderName(programName), static_cast<int>(logLength),
	            static_cast<unsigned int>(gl.GetError()));
	#endif

	const size_t logSize = logLength > 0 ? static_cast<size_t>(logLength) : 1;
	std::string log(logSize, '\0');
	GLsizei actualLength = 0;
	#ifdef MKXPZ_VITA_DIAGNOSTICS
	vitaDiagLog("SHADER", "before api=glGetShaderInfoLog shader=%u type=%s(0x%04x) name=%s program=%s max_length=%lu gl_error=0x%04x",
	            static_cast<unsigned int>(shader),
	            shaderType == GL_VERTEX_SHADER ? "GL_VERTEX_SHADER" : "GL_FRAGMENT_SHADER",
	            static_cast<unsigned int>(shaderType), vitaShaderName(shaderName),
	            vitaShaderName(programName), static_cast<unsigned long>(log.size()),
	            static_cast<unsigned int>(gl.GetError()));
	#endif
	gl.GetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), &actualLength,
	                    &log[0]);
	#ifdef MKXPZ_VITA_DIAGNOSTICS
	const size_t safeLength = actualLength > 0 &&
	                          static_cast<size_t>(actualLength) < log.size()
	                      ? static_cast<size_t>(actualLength)
	                      : (actualLength > 0 ? log.size() : 0);
	const std::string capped = vitaCappedInfoLog(log.data(), safeLength);
	vitaDiagLog("SHADER", "after api=glGetShaderInfoLog shader=%u type=%s(0x%04x) name=%s program=%s actual_length=%d info_log=\"%s\" gl_error=0x%04x",
	            static_cast<unsigned int>(shader),
	            shaderType == GL_VERTEX_SHADER ? "GL_VERTEX_SHADER" : "GL_FRAGMENT_SHADER",
	            static_cast<unsigned int>(shaderType), vitaShaderName(shaderName),
	            vitaShaderName(programName), static_cast<int>(actualLength),
	            capped.c_str(), static_cast<unsigned int>(gl.GetError()));
	#endif

	std::clog << "Shader log:\n" << log;
}

static void printProgramLog(GLuint program, const char *programName)
{
	GLint logLength = 0;
	#ifdef MKXPZ_VITA_DIAGNOSTICS
	vitaDiagLog("SHADER", "before api=glGetProgramiv program=%u name=%s pname=GL_INFO_LOG_LENGTH gl_error=0x%04x",
	            static_cast<unsigned int>(program), vitaShaderName(programName),
	            static_cast<unsigned int>(gl.GetError()));
	#endif
	gl.GetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
	#ifdef MKXPZ_VITA_DIAGNOSTICS
	vitaDiagLog("SHADER", "after api=glGetProgramiv program=%u name=%s pname=GL_INFO_LOG_LENGTH length=%d gl_error=0x%04x",
	            static_cast<unsigned int>(program), vitaShaderName(programName),
	            static_cast<int>(logLength), static_cast<unsigned int>(gl.GetError()));
	#endif

	const size_t logSize = logLength > 0 ? static_cast<size_t>(logLength) : 1;
	std::string log(logSize, '\0');
	GLsizei actualLength = 0;
	#ifdef MKXPZ_VITA_DIAGNOSTICS
	vitaDiagLog("SHADER", "before api=glGetProgramInfoLog program=%u name=%s max_length=%lu gl_error=0x%04x",
	            static_cast<unsigned int>(program), vitaShaderName(programName),
	            static_cast<unsigned long>(log.size()),
	            static_cast<unsigned int>(gl.GetError()));
	#endif
	gl.GetProgramInfoLog(program, static_cast<GLsizei>(log.size()), &actualLength,
	                     &log[0]);
	#ifdef MKXPZ_VITA_DIAGNOSTICS
	const size_t safeLength = actualLength > 0 &&
	                          static_cast<size_t>(actualLength) < log.size()
	                      ? static_cast<size_t>(actualLength)
	                      : (actualLength > 0 ? log.size() : 0);
	const std::string capped = vitaCappedInfoLog(log.data(), safeLength);
	vitaDiagLog("SHADER", "after api=glGetProgramInfoLog program=%u name=%s actual_length=%d info_log=\"%s\" gl_error=0x%04x",
	            static_cast<unsigned int>(program), vitaShaderName(programName),
	            static_cast<int>(actualLength), capped.c_str(),
	            static_cast<unsigned int>(gl.GetError()));
	#endif

	std::clog << "Program log:\n" << log;
}

Shader::Shader(const char *constructionName)
    : initialized(false),
      diagnosticConstructionName(constructionName),
      diagnosticProgramName("<uninitialized>")
{
#ifdef MKXPZ_BUILD_XCODE
    if (Shader::shaderCommon.empty())
        Shader::shaderCommon = mkxp_fs::contentsOfAssetAsString("Shaders/common", "h");
#endif
	#ifdef MKXPZ_VITA_DIAGNOSTICS
	vitaDiagLog("SHADER", "before api=glCreateShader owner=%s shader_type=GL_VERTEX_SHADER(0x%04x) gl_error=0x%04x",
	            vitaShaderName(diagnosticConstructionName),
            static_cast<unsigned int>(GL_VERTEX_SHADER),
            static_cast<unsigned int>(gl.GetError()));
	#endif
	vertShader = gl.CreateShader(GL_VERTEX_SHADER);
	#ifdef MKXPZ_VITA_DIAGNOSTICS
	vitaDiagLog("SHADER", "after api=glCreateShader owner=%s shader_type=GL_VERTEX_SHADER(0x%04x) shader=%u gl_error=0x%04x",
	            vitaShaderName(diagnosticConstructionName),
	            static_cast<unsigned int>(GL_VERTEX_SHADER),
	            static_cast<unsigned int>(vertShader),
	            static_cast<unsigned int>(gl.GetError()));
	#endif

	#ifdef MKXPZ_VITA_DIAGNOSTICS
	vitaDiagLog("SHADER", "before api=glCreateShader owner=%s shader_type=GL_FRAGMENT_SHADER(0x%04x) gl_error=0x%04x",
	            vitaShaderName(diagnosticConstructionName),
	            static_cast<unsigned int>(GL_FRAGMENT_SHADER),
	            static_cast<unsigned int>(gl.GetError()));
	#endif
	fragShader = gl.CreateShader(GL_FRAGMENT_SHADER);
	#ifdef MKXPZ_VITA_DIAGNOSTICS
	vitaDiagLog("SHADER", "after api=glCreateShader owner=%s shader_type=GL_FRAGMENT_SHADER(0x%04x) shader=%u gl_error=0x%04x",
	            vitaShaderName(diagnosticConstructionName),
	            static_cast<unsigned int>(GL_FRAGMENT_SHADER),
	            static_cast<unsigned int>(fragShader),
	            static_cast<unsigned int>(gl.GetError()));
	#endif

	#ifdef MKXPZ_VITA_DIAGNOSTICS
	vitaDiagLog("SHADER", "before api=glCreateProgram owner=%s gl_error=0x%04x",
	            vitaShaderName(diagnosticConstructionName),
	            static_cast<unsigned int>(gl.GetError()));
	#endif
	program = gl.CreateProgram();
	#ifdef MKXPZ_VITA_DIAGNOSTICS
	vitaRegisterProgram(program, diagnosticConstructionName);
	vitaDiagLog("SHADER", "after api=glCreateProgram owner=%s program=%u gl_error=0x%04x",
	            vitaShaderName(diagnosticConstructionName),
	            static_cast<unsigned int>(program),
	            static_cast<unsigned int>(gl.GetError()));
	#endif
}

Shader::~Shader()
{
	gl.DeleteProgram(program);
	gl.DeleteShader(vertShader);
	gl.DeleteShader(fragShader);
}

ShaderBase::ShaderBase(const char *constructionName)
	: Shader(constructionName)
{}

ShaderSet::ShaderSet(const char *constructionName)
{
#ifdef MKXPZ_VITA_DIAGNOSTICS
	vitaDiagLog("SHADERSET", "body_after_members name=%s gl_error=0x%04x",
	            vitaShaderName(constructionName),
	            static_cast<unsigned int>(gl.GetError()));
#else
	(void) constructionName;
#endif
}

void vitaShaderDiagUseProgramBefore(GLuint program)
{
#ifdef MKXPZ_VITA_DIAGNOSTICS
	vitaDiagLog("SHADER", "before api=glUseProgram program=%u name=%s gl_error=0x%04x",
	            static_cast<unsigned int>(program), vitaProgramName(program),
	            static_cast<unsigned int>(gl.GetError()));
#else
	(void) program;
#endif
}

void vitaShaderDiagUseProgramAfter(GLuint program)
{
#ifdef MKXPZ_VITA_DIAGNOSTICS
	vitaDiagLog("SHADER", "after api=glUseProgram program=%u name=%s gl_error=0x%04x",
	            static_cast<unsigned int>(program), vitaProgramName(program),
	            static_cast<unsigned int>(gl.GetError()));
#else
	(void) program;
#endif
}

void Shader::bind()
{
	FrameProfile::Scope profile(FrameProfile::ShaderBind);
	glState.program.set(program);
}

void Shader::unbind()
{
	gl.ActiveTexture(GL_TEXTURE0);
	glState.program.set(0);
}

#ifdef MKXPZ_BUILD_XCODE
std::string &Shader::commonHeader() {
    return Shader::shaderCommon;
}
#endif

static void setupShaderSource(GLuint shader, GLenum type,
                              const unsigned char *body, int bodySize,
                              const char *shaderName, const char *programName,
                              VitaShaderSourceInfo *traceInfo)
{
#ifdef __vita__
	static const char glesDefine[] = "#define GLSLES\n#define MKXPZ_VITA\n";
#else
	static const char glesDefine[] = "#define GLSLES\n";
#endif
	static const char fragDefine[] = "#define FRAGMENT_SHADER\n";

	const GLchar *shaderSrc[4];
	GLint shaderSrcSize[4];
	size_t i = 0;

	if (gl.glsles)
	{
		shaderSrc[i] = glesDefine;
		shaderSrcSize[i] = sizeof(glesDefine)-1;
		++i;
	}

	if (type == GL_FRAGMENT_SHADER)
	{
		shaderSrc[i] = fragDefine;
		shaderSrcSize[i] = sizeof(fragDefine)-1;
		++i;
	}

#ifndef MKXPZ_BUILD_XCODE
	shaderSrc[i] = (const GLchar*) ___shader_common_h;
	shaderSrcSize[i] = ___shader_common_h_len;
#else
    shaderSrc[i] = (const GLchar*) Shader::commonHeader().c_str();
    shaderSrcSize[i] = Shader::commonHeader().length();
#endif
	++i;

	shaderSrc[i] = (const GLchar*) body;
	shaderSrcSize[i] = bodySize;
	++i;

	if (traceInfo)
		for (size_t j = 0; j < i; ++j)
			vitaSourceInfoAdd(*traceInfo, shaderSrc[j],
			                  static_cast<size_t>(shaderSrcSize[j]));

	#ifdef MKXPZ_VITA_DIAGNOSTICS
	vitaDiagLog("SHADER", "before api=glShaderSource shader=%u type=%s(0x%04x) name=%s program=%s segments=%lu gles_macro=%d fragment_macro=%d source_length=%lu source_hash=0x%08x first_line=\"%s\" gl_error=0x%04x",
	            static_cast<unsigned int>(shader),
	            type == GL_VERTEX_SHADER ? "GL_VERTEX_SHADER" : "GL_FRAGMENT_SHADER",
	            static_cast<unsigned int>(type), vitaShaderName(shaderName),
	            vitaShaderName(programName), static_cast<unsigned long>(i),
	            gl.glsles ? 1 : 0, type == GL_FRAGMENT_SHADER ? 1 : 0,
            static_cast<unsigned long>(traceInfo ? traceInfo->length : 0),
	            static_cast<unsigned int>(traceInfo ? traceInfo->hash : 0),
	            traceInfo ? traceInfo->firstLine.c_str() : "", static_cast<unsigned int>(gl.GetError()));
	#endif
	gl.ShaderSource(shader, i, shaderSrc, shaderSrcSize);
	#ifdef MKXPZ_VITA_DIAGNOSTICS
	vitaDiagLog("SHADER", "after api=glShaderSource shader=%u type=%s(0x%04x) name=%s program=%s segments=%lu gles_macro=%d fragment_macro=%d source_length=%lu source_hash=0x%08x first_line=\"%s\" gl_error=0x%04x",
	            static_cast<unsigned int>(shader),
	            type == GL_VERTEX_SHADER ? "GL_VERTEX_SHADER" : "GL_FRAGMENT_SHADER",
	            static_cast<unsigned int>(type), vitaShaderName(shaderName),
	            vitaShaderName(programName), static_cast<unsigned long>(i),
	            gl.glsles ? 1 : 0, type == GL_FRAGMENT_SHADER ? 1 : 0,
            static_cast<unsigned long>(traceInfo ? traceInfo->length : 0),
	            static_cast<unsigned int>(traceInfo ? traceInfo->hash : 0),
	            traceInfo ? traceInfo->firstLine.c_str() : "", static_cast<unsigned int>(gl.GetError()));
	#endif
}

#ifdef __vita__
static bool shaderCacheEnabled()
{
    // No cached static here: vitasdk's __cxa_guard_acquire passes the guard
    // mutex by value while pte's pthread_mutex_lock dereferences its argument,
    // so the first guarded static initialized on Vita crashes in
    // pthread_mutex_lock(NULL). Shader::init runs only a few dozen times per
    // boot, so re-check the flag file on every call.
    return access("app0:/shader-cache.off", F_OK) != 0;
}

static std::string shaderCacheKey(const unsigned char *vert, int vertSize,
                                 const unsigned char *frag, int fragSize,
                                 const char *programName)
{
    std::string key(MKXPZ_SHADER_CACHE_BUILD);
    key += programName ? programName : "";
    key.push_back(gl.glsles ? 1 : 0);
    key.append(reinterpret_cast<const char *>(&vertSize), sizeof(vertSize));
    key.append(reinterpret_cast<const char *>(vert), vertSize);
    key.append(reinterpret_cast<const char *>(&fragSize), sizeof(fragSize));
    key.append(reinterpret_cast<const char *>(frag), fragSize);
    return key;
}

static std::string shaderCachePath(const char *programName)
{
    // Stable per-program slots: new engine/library builds replace stale entries
    // instead of accumulating another directory for each build.
    const char *name = programName ? programName : "";
    char path[128];
    snprintf(path, sizeof(path), "ux0:/data/mkxpz-cache/programs-v1/%08x.bin",
             ProgramCache::checksum(name, strlen(name)));
    return path;
}
#endif

void Shader::init(const unsigned char *vert, int vertSize,
                  const unsigned char *frag, int fragSize,
                  const char *vertName, const char *fragName,
                  const char *programName)
{
	if (initialized)
	{
		/* Calling Shader::init() more than once causes a small number of graphics drivers to encounter linking errors.
		 * In particular, the Nintendo Switch homebrew toolchain's Mesa driver has this problem.
		 * So we throw this exception on every platform to reduce the probability of regressions. */
		throw Exception(Exception::MKXPError,
	                    "Attempted to call Shader::init() more than once");
	}

	GLint success;
	VitaShaderSourceInfo *vertTrace = 0;
	VitaShaderSourceInfo *fragTrace = 0;
	#ifdef MKXPZ_VITA_DIAGNOSTICS
	VitaShaderSourceInfo vertTraceStorage;
	VitaShaderSourceInfo fragTraceStorage;
	vertTrace = &vertTraceStorage;
	fragTrace = &fragTraceStorage;
	diagnosticProgramName = programName;
	vitaRegisterProgram(program, programName);
	vitaDiagLog("SHADER", "init_begin name=%s program=%u vertex=%s(%u) fragment=%s(%u) gl_error=0x%04x",
	            vitaShaderName(programName), static_cast<unsigned int>(program),
	            vitaShaderName(vertName), static_cast<unsigned int>(vertShader),
	            vitaShaderName(fragName), static_cast<unsigned int>(fragShader),
	            static_cast<unsigned int>(gl.GetError()));
	#else
	diagnosticProgramName = programName;
	#endif

#ifdef __vita__
    std::string cacheKey, cachePath;
    if (shaderCacheEnabled()) {
        FrameProfile::Scope cacheProfile(FrameProfile::ShaderCache);
        cacheKey = shaderCacheKey(vert, vertSize, frag, fragSize, programName);
        cachePath = shaderCachePath(programName);
        std::vector<uint8_t> binary;
        if (ProgramCache::load(cachePath.c_str(), cacheKey, binary)) {
            glProgramBinary(program, 0, binary.data(), binary.size());
            gl.GetProgramiv(program, GL_LINK_STATUS, &success);
            if (success) {
                initialized = true;
                vitaDiagLog("BOOTPERF", "shader_cache_hit name=%s", programName);
                return;
            }
            // A rejected binary must leave a fresh program for normal compilation.
            gl.DeleteProgram(program);
            program = gl.CreateProgram();
        }
    }
#endif
    FrameProfile::Scope compileProfile(FrameProfile::ShaderCompile);
	/* Compile vertex shader */
	setupShaderSource(vertShader, GL_VERTEX_SHADER, vert, vertSize,
	                  vertName, programName, vertTrace);
	#ifdef MKXPZ_VITA_DIAGNOSTICS
	vitaDiagLog("SHADER", "before api=glCompileShader shader=%u type=GL_VERTEX_SHADER(0x%04x) name=%s program=%s source_length=%lu source_hash=0x%08x first_line=\"%s\" gl_error=0x%04x",
	            static_cast<unsigned int>(vertShader),
	            static_cast<unsigned int>(GL_VERTEX_SHADER), vitaShaderName(vertName),
	            vitaShaderName(programName), static_cast<unsigned long>(vertTrace->length),
	            static_cast<unsigned int>(vertTrace->hash), vertTrace->firstLine.c_str(),
	            static_cast<unsigned int>(gl.GetError()));
	#endif
	gl.CompileShader(vertShader);
	#ifdef MKXPZ_VITA_DIAGNOSTICS
	vitaDiagLog("SHADER", "after api=glCompileShader shader=%u type=GL_VERTEX_SHADER(0x%04x) name=%s program=%s gl_error=0x%04x",
	            static_cast<unsigned int>(vertShader),
	            static_cast<unsigned int>(GL_VERTEX_SHADER), vitaShaderName(vertName),
	            vitaShaderName(programName), static_cast<unsigned int>(gl.GetError()));
	#endif

	#ifdef MKXPZ_VITA_DIAGNOSTICS
	vitaDiagLog("SHADER", "before api=glGetShaderiv shader=%u type=GL_VERTEX_SHADER(0x%04x) name=%s program=%s pname=GL_COMPILE_STATUS gl_error=0x%04x",
	            static_cast<unsigned int>(vertShader),
	            static_cast<unsigned int>(GL_VERTEX_SHADER), vitaShaderName(vertName),
	            vitaShaderName(programName), static_cast<unsigned int>(gl.GetError()));
	#endif
	gl.GetShaderiv(vertShader, GL_COMPILE_STATUS, &success);
	#ifdef MKXPZ_VITA_DIAGNOSTICS
	vitaDiagLog("SHADER", "after api=glGetShaderiv shader=%u type=GL_VERTEX_SHADER(0x%04x) name=%s program=%s pname=GL_COMPILE_STATUS status=%d gl_error=0x%04x",
	            static_cast<unsigned int>(vertShader),
	            static_cast<unsigned int>(GL_VERTEX_SHADER), vitaShaderName(vertName),
	            vitaShaderName(programName), static_cast<int>(success),
	            static_cast<unsigned int>(gl.GetError()));
	#endif

	if (!success)
	{
		printShaderLog(vertShader, GL_VERTEX_SHADER, vertName, programName);
		throw Exception(Exception::MKXPError,
	                    "GLSL: An error occurred while compiling vertex shader '%s' in program '%s'",
	                    vertName, programName);
	}

	/* Compile fragment shader */
	setupShaderSource(fragShader, GL_FRAGMENT_SHADER, frag, fragSize,
	                  fragName, programName, fragTrace);
	#ifdef MKXPZ_VITA_DIAGNOSTICS
	vitaDiagLog("SHADER", "before api=glCompileShader shader=%u type=GL_FRAGMENT_SHADER(0x%04x) name=%s program=%s source_length=%lu source_hash=0x%08x first_line=\"%s\" gl_error=0x%04x",
	            static_cast<unsigned int>(fragShader),
	            static_cast<unsigned int>(GL_FRAGMENT_SHADER), vitaShaderName(fragName),
	            vitaShaderName(programName), static_cast<unsigned long>(fragTrace->length),
	            static_cast<unsigned int>(fragTrace->hash), fragTrace->firstLine.c_str(),
	            static_cast<unsigned int>(gl.GetError()));
	#endif
	gl.CompileShader(fragShader);
	#ifdef MKXPZ_VITA_DIAGNOSTICS
	vitaDiagLog("SHADER", "after api=glCompileShader shader=%u type=GL_FRAGMENT_SHADER(0x%04x) name=%s program=%s gl_error=0x%04x",
	            static_cast<unsigned int>(fragShader),
	            static_cast<unsigned int>(GL_FRAGMENT_SHADER), vitaShaderName(fragName),
	            vitaShaderName(programName), static_cast<unsigned int>(gl.GetError()));
	#endif

	#ifdef MKXPZ_VITA_DIAGNOSTICS
	vitaDiagLog("SHADER", "before api=glGetShaderiv shader=%u type=GL_FRAGMENT_SHADER(0x%04x) name=%s program=%s pname=GL_COMPILE_STATUS gl_error=0x%04x",
	            static_cast<unsigned int>(fragShader),
	            static_cast<unsigned int>(GL_FRAGMENT_SHADER), vitaShaderName(fragName),
	            vitaShaderName(programName), static_cast<unsigned int>(gl.GetError()));
	#endif
	gl.GetShaderiv(fragShader, GL_COMPILE_STATUS, &success);
	#ifdef MKXPZ_VITA_DIAGNOSTICS
	vitaDiagLog("SHADER", "after api=glGetShaderiv shader=%u type=GL_FRAGMENT_SHADER(0x%04x) name=%s program=%s pname=GL_COMPILE_STATUS status=%d gl_error=0x%04x",
	            static_cast<unsigned int>(fragShader),
	            static_cast<unsigned int>(GL_FRAGMENT_SHADER), vitaShaderName(fragName),
	            vitaShaderName(programName), static_cast<int>(success),
	            static_cast<unsigned int>(gl.GetError()));
	#endif

	if (!success)
	{
		printShaderLog(fragShader, GL_FRAGMENT_SHADER, fragName, programName);
		throw Exception(Exception::MKXPError,
	                    "GLSL: An error occurred while compiling fragment shader '%s' in program '%s'",
	                    fragName, programName);
	}

	/* Link shader program */
	#ifdef MKXPZ_VITA_DIAGNOSTICS
	vitaDiagLog("SHADER", "before api=glAttachShader program=%u name=%s shader=%u type=GL_VERTEX_SHADER gl_error=0x%04x",
	            static_cast<unsigned int>(program), vitaShaderName(programName),
	            static_cast<unsigned int>(vertShader), static_cast<unsigned int>(gl.GetError()));
	#endif
	gl.AttachShader(program, vertShader);
	#ifdef MKXPZ_VITA_DIAGNOSTICS
	vitaDiagLog("SHADER", "after api=glAttachShader program=%u name=%s shader=%u type=GL_VERTEX_SHADER gl_error=0x%04x",
	            static_cast<unsigned int>(program), vitaShaderName(programName),
	            static_cast<unsigned int>(vertShader), static_cast<unsigned int>(gl.GetError()));
	vitaDiagLog("SHADER", "before api=glAttachShader program=%u name=%s shader=%u type=GL_FRAGMENT_SHADER gl_error=0x%04x",
	            static_cast<unsigned int>(program), vitaShaderName(programName),
	            static_cast<unsigned int>(fragShader), static_cast<unsigned int>(gl.GetError()));
	#endif
	gl.AttachShader(program, fragShader);
	#ifdef MKXPZ_VITA_DIAGNOSTICS
	vitaDiagLog("SHADER", "after api=glAttachShader program=%u name=%s shader=%u type=GL_FRAGMENT_SHADER gl_error=0x%04x",
	            static_cast<unsigned int>(program), vitaShaderName(programName),
	            static_cast<unsigned int>(fragShader), static_cast<unsigned int>(gl.GetError()));
	#endif

	vitaBindAttribLocation(program, programName, Position, "position");
	vitaBindAttribLocation(program, programName, TexCoord, "texCoord");
	vitaBindAttribLocation(program, programName, Color, "color");

	#ifdef MKXPZ_VITA_DIAGNOSTICS
	vitaDiagLog("SHADER", "before api=glLinkProgram program=%u name=%s vertex=%u fragment=%u gl_error=0x%04x",
	            static_cast<unsigned int>(program), vitaShaderName(programName),
	            static_cast<unsigned int>(vertShader), static_cast<unsigned int>(fragShader),
	            static_cast<unsigned int>(gl.GetError()));
	#endif
	gl.LinkProgram(program);
	#ifdef MKXPZ_VITA_DIAGNOSTICS
	vitaDiagLog("SHADER", "after api=glLinkProgram program=%u name=%s gl_error=0x%04x",
	            static_cast<unsigned int>(program), vitaShaderName(programName),
	            static_cast<unsigned int>(gl.GetError()));
	#endif

	#ifdef MKXPZ_VITA_DIAGNOSTICS
	vitaDiagLog("SHADER", "before api=glGetProgramiv program=%u name=%s pname=GL_LINK_STATUS gl_error=0x%04x",
	            static_cast<unsigned int>(program), vitaShaderName(programName),
	            static_cast<unsigned int>(gl.GetError()));
	#endif
	gl.GetProgramiv(program, GL_LINK_STATUS, &success);
	#ifdef MKXPZ_VITA_DIAGNOSTICS
	vitaDiagLog("SHADER", "after api=glGetProgramiv program=%u name=%s pname=GL_LINK_STATUS status=%d gl_error=0x%04x",
	            static_cast<unsigned int>(program), vitaShaderName(programName),
	            static_cast<int>(success), static_cast<unsigned int>(gl.GetError()));
	#endif

	if (!success)
	{
		printProgramLog(program, programName);
		throw Exception(Exception::MKXPError,
	                    "GLSL: An error occurred while linking program '%s' (vertex '%s', fragment '%s')",
	                    programName, vertName, fragName);
	}

#ifdef __vita__
    if (shaderCacheEnabled()) {
        FrameProfile::Scope cacheProfile(FrameProfile::ShaderCache);
        GLint size = 0;
        gl.GetProgramiv(program, 0x8741 /* GL_PROGRAM_BINARY_LENGTH */, &size);
        if (size > 0 && static_cast<unsigned>(size) <= ProgramCache::MaxBinary) {
            try {
                std::vector<uint8_t> binary(size);
                GLsizei written = 0;
                GLenum format = 0;
                glGetProgramBinary(program, size, &written, &format, binary.data());
                if (written == size) {
                    mkdir("ux0:/data/mkxpz-cache", 0777);
                    mkdir("ux0:/data/mkxpz-cache/programs-v1", 0777);
                    bool saved = ProgramCache::save(cachePath.c_str(), cacheKey, binary);
                    vitaDiagLog("BOOTPERF", "shader_cache_miss name=%s saved=%d bytes=%d",
                                programName, saved, size);
                }
            } catch (const std::bad_alloc &) { /* Cache is optional. */ }
        }
    }
#endif
	initialized = true;
}

void Shader::initFromFile(const char *_vertFile, const char *_fragFile,
                          const char *programName)
{
	std::string vertContents, fragContents;
	readFile(_vertFile, vertContents);
	readFile(_fragFile, fragContents);

	init((const unsigned char*) vertContents.c_str(), vertContents.size(),
	     (const unsigned char*) fragContents.c_str(), fragContents.size(),
	     _vertFile, _fragFile, programName);
}

void Shader::setVec2Uniform(GLint location, const Vec2 &vec)
{
    gl.Uniform2f(location, vec.x, vec.y);
}

void Shader::setVec4Uniform(GLint location, const Vec4 &vec)
{
	gl.Uniform4f(location, vec.x, vec.y, vec.z, vec.w);
}

void Shader::setTexUniform(GLint location, unsigned unitIndex, TEX::ID texture)
{
	GLenum texUnit = GL_TEXTURE0 + unitIndex;

	gl.ActiveTexture(texUnit);
	gl.BindTexture(GL_TEXTURE_2D, texture.gl);
	gl.Uniform1i(location, unitIndex);
	gl.ActiveTexture(GL_TEXTURE0);
}

void ShaderBase::GLProjMat::apply(const Vec2i &value)
{
	/* glOrtho replacement */
	const float a = 2.f / value.x;
	const float b = 2.f / value.y;

#ifdef __vita__
	static int vitaProjectionLogged = 0;
	if (vitaProjectionLogged < 8) {
		++vitaProjectionLogged;
		vitaDiagLog("VITA-PROJ", "applyViewportProj value=%dx%d u_scale=%d scale=%f,%f",
		            value.x, value.y, (int)u_scale, (double)a, (double)b);
	}
	gl.Uniform2f(u_scale, a, b);
#else
	const float c = -2.f;

	GLfloat mat[16] =
	{
		 a,  0,  0,  0,
		 0,  b,  0,  0,
		 0,  0,  c,  0,
		-1, -1, -1,  1
	};

	/* S37: projection-upload diagnostic.
	 * Log the 16 floats + location right before the call.
	 * Log even when u_mat==0 — location 0 is silently DROPPED by
	 * vitaGL's glUniformMatrix4fv (custom_shaders.c:2932), which
	 * would leave the GXP uniform at its default (all-zero / no
	 * -1,-1 translation) → clip (0,0) = viewport center. */
	static int s37ProjLogged = 0;
	if (s37ProjLogged < 8) {
		++s37ProjLogged;
		vitaDiagLog("S37-PROJ", "applyViewportProj value=%dx%d u_mat=%d mat=[%f %f %f %f | %f %f %f %f | %f %f %f %f | %f %f %f %f]",
		            value.x, value.y, (int)u_mat,
		            (double)mat[0], (double)mat[1], (double)mat[2], (double)mat[3],
		            (double)mat[4], (double)mat[5], (double)mat[6], (double)mat[7],
		            (double)mat[8], (double)mat[9], (double)mat[10], (double)mat[11],
		            (double)mat[12], (double)mat[13], (double)mat[14], (double)mat[15]);
	}

	gl.UniformMatrix4fv(u_mat, 1, GL_FALSE, mat);
#endif
}

void ShaderBase::init()
{
	GET_U(texSizeInv);
	GET_U(translation);

#ifdef __vita__
	projMat.u_scale = vitaGetUniformLocation(program, diagnosticProgramName,
	                                         "projScale");
#else
	projMat.u_mat = vitaGetUniformLocation(program, diagnosticProgramName,
	                                       "projMat");
#endif
}

void ShaderBase::applyViewportProj()
{
	// High-res: scale the matrix if we're rendering to the PingPong framebuffer.
	const IntRect &vp = glState.viewport.get();
	if (shState->config().enableHires && shState->graphics().isPingPongFramebufferActive() && framebufferScalingAllowed()) {
		projMat.set(Vec2i(shState->graphics().width(), shState->graphics().height()));
	}
	else {
		projMat.set(Vec2i(vp.w, vp.h));
	}
}

bool ShaderBase::framebufferScalingAllowed()
{
	return true;
}

void ShaderBase::setTexSize(const Vec2i &value)
{
	gl.Uniform2f(u_texSizeInv, 1.f / value.x, 1.f / value.y);
}

void ShaderBase::setTranslation(const Vec2i &value)
{
	gl.Uniform2f(u_translation, value.x, value.y);
}


FlatColorShader::FlatColorShader()
	: ShaderBase("FlatColorShader")
{
	INIT_SHADER(minimal, flatColor, FlatColorShader);

	ShaderBase::init();

	GET_U(color);
}

void FlatColorShader::setColor(const Vec4 &value)
{
	setVec4Uniform(u_color, value);
}


SimpleShader::SimpleShader()
	: ShaderBase("SimpleShader")
{
	INIT_SHADER(simple, simple, SimpleShader);

	ShaderBase::init();

	GET_U(texOffsetX);
}

SimpleShader::SimpleShader(const ShaderNoConstructTag &tag)
	: ShaderBase(tag.name ? tag.name : "SimpleShader(no_construct)")
{
}

void SimpleShader::setTexOffsetX(int value)
{
	gl.Uniform1f(u_texOffsetX, value);
}


SimpleColorShader::SimpleColorShader()
	: ShaderBase("SimpleColorShader")
{
	INIT_SHADER(simpleColor, simpleColor, SimpleColorShader);

	ShaderBase::init();
}


SimpleAlphaShader::SimpleAlphaShader()
	: ShaderBase("SimpleAlphaShader")
{
	INIT_SHADER(simpleColor, simpleAlpha, SimpleAlphaShader);

	ShaderBase::init();
}


SimpleSpriteShader::SimpleSpriteShader()
	: ShaderBase("SimpleSpriteShader")
{
	INIT_SHADER(sprite, simple, SimpleSpriteShader);

	ShaderBase::init();

	GET_U(spriteMat);
}

SimpleSpriteShader::SimpleSpriteShader(const ShaderNoConstructTag &tag)
	: ShaderBase(tag.name ? tag.name : "SimpleSpriteShader(no_construct)")
{
}

void SimpleSpriteShader::setSpriteMat(const float value[16])
{
	gl.UniformMatrix4fv(u_spriteMat, 1, GL_FALSE, value);
}

BicubicSpriteShader::BicubicSpriteShader() : Lanczos3SpriteShader(ShaderNoConstructTag("BicubicSpriteShader"))
{
	INIT_SHADER(sprite, bicubic, BicubicSpriteShader);

	ShaderBase::init();

	GET_U(spriteMat);
	GET_U(sourceSize);
	GET_U(bc);
}

void BicubicSpriteShader::setSharpness(int sharpness)
{
	gl.Uniform2f(u_bc, 1.f - sharpness * 0.01f, sharpness * 0.005f);
}

Lanczos3SpriteShader::Lanczos3SpriteShader() : SimpleSpriteShader(ShaderNoConstructTag("Lanczos3SpriteShader"))
{
	INIT_SHADER(sprite, lanczos3, Lanczos3SpriteShader);

	ShaderBase::init();

	GET_U(spriteMat);
	GET_U(sourceSize);
}

Lanczos3SpriteShader::Lanczos3SpriteShader(const ShaderNoConstructTag &tag) : SimpleSpriteShader(tag)
{
}

void Lanczos3SpriteShader::setTexSize(const Vec2i &value)
{
	ShaderBase::setTexSize(value);
	gl.Uniform2f(u_sourceSize, (float)value.x, (float)value.y);
}

#ifdef MKXPZ_SSL
XbrzSpriteShader::XbrzSpriteShader() : Lanczos3SpriteShader(ShaderNoConstructTag("XbrzSpriteShader"))
{
	INIT_SHADER(sprite, xbrz, XbrzSpriteShader);

	ShaderBase::init();

	GET_U(spriteMat);
	GET_U(sourceSize);
	GET_U(targetScale);
}

void XbrzSpriteShader::setTargetScale(const Vec2 &value)
{
	gl.Uniform2f(u_targetScale, value.x, value.y);
}
#endif

AlphaSpriteShader::AlphaSpriteShader()
	: ShaderBase("AlphaSpriteShader")
{
	INIT_SHADER(sprite, simpleAlphaUni, AlphaSpriteShader);

	ShaderBase::init();

	GET_U(spriteMat);
	GET_U(alpha);
}

void AlphaSpriteShader::setSpriteMat(const float value[16])
{
	gl.UniformMatrix4fv(u_spriteMat, 1, GL_FALSE, value);
}

void AlphaSpriteShader::setAlpha(float value)
{
	gl.Uniform1f(u_alpha, value);
}


TransShader::TransShader()
	: ShaderBase("TransShader")
{
	INIT_SHADER(simple, trans, TransShader);

	ShaderBase::init();

	GET_U(currentScene);
	GET_U(frozenScene);
	GET_U(transMap);
	GET_U(prog);
	GET_U(vague);
}

void TransShader::setCurrentScene(TEX::ID tex)
{
	setTexUniform(u_currentScene, 1, tex);
}

void TransShader::setFrozenScene(TEX::ID tex)
{
	setTexUniform(u_frozenScene, 2, tex);
}

void TransShader::setTransMap(TEX::ID tex)
{
	setTexUniform(u_transMap, 3, tex);
}

void TransShader::setProg(float value)
{
	gl.Uniform1f(u_prog, value);
}

void TransShader::setVague(float value)
{
	gl.Uniform1f(u_vague, value);
}


SimpleTransShader::SimpleTransShader()
	: ShaderBase("SimpleTransShader")
{
	INIT_SHADER(simple, transSimple, SimpleTransShader);

	ShaderBase::init();

	GET_U(currentScene);
	GET_U(frozenScene);
	GET_U(prog);
}

void SimpleTransShader::setCurrentScene(TEX::ID tex)
{
	setTexUniform(u_currentScene, 1, tex);
}

void SimpleTransShader::setFrozenScene(TEX::ID tex)
{
	setTexUniform(u_frozenScene, 2, tex);
}

void SimpleTransShader::setProg(float value)
{
	gl.Uniform1f(u_prog, value);
}


SpriteShader::SpriteShader()
	: ShaderBase("SpriteShader")
{
	INIT_SHADER(sprite, sprite, SpriteShader);

	ShaderBase::init();

	GET_U(spriteMat);
	GET_U(tone);
	GET_U(color);
	GET_U(opacity);
	GET_U(bushY);
	GET_U(bushUnder);
	GET_U(bushSlope);
	GET_U(bushIntercept);
	GET_U(bushOpacity);
    GET_U(pattern);
    GET_U(patternBlendType);
    GET_U(patternTile);
    GET_U(renderPattern);
    GET_U(patternSizeInv);
    GET_U(patternOpacity);
    GET_U(patternScroll);
    GET_U(patternZoom);
    GET_U(invert);
}

void SpriteShader::setSpriteMat(const float value[16])
{
	gl.UniformMatrix4fv(u_spriteMat, 1, GL_FALSE, value);
}

void SpriteShader::setTone(const Vec4 &tone)
{
	setVec4Uniform(u_tone, tone);
}

void SpriteShader::setColor(const Vec4 &color)
{
	setVec4Uniform(u_color, color);
}

void SpriteShader::setOpacity(float value)
{
	gl.Uniform1f(u_opacity, value);
}

void SpriteShader::setBushDepth(bool bushY, bool bushUnder, float bushSlope, float bushIntercept)
{
	gl.Uniform1f(u_bushY, bushY);
	gl.Uniform1f(u_bushUnder, bushUnder);
	gl.Uniform1f(u_bushSlope, bushSlope);
	gl.Uniform1f(u_bushIntercept, bushIntercept);
}

void SpriteShader::setBushOpacity(float value)
{
	gl.Uniform1f(u_bushOpacity, value);
}

void SpriteShader::setPattern(const TEX::ID pattern, const Vec2 &dimensions)
{
    setTexUniform(u_pattern, 1, pattern);
    gl.Uniform2f(u_patternSizeInv, 1.f / dimensions.x, 1.f / dimensions.y);
}

void SpriteShader::setPatternBlendType(int blendType)
{
    gl.Uniform1i(u_patternBlendType, blendType);
}

void SpriteShader::setPatternTile(bool value)
{
    gl.Uniform1i(u_patternTile, value);
}

void SpriteShader::setShouldRenderPattern(bool value)
{
    gl.Uniform1i(u_renderPattern, value);
}

void SpriteShader::setPatternOpacity(float value)
{
    gl.Uniform1f(u_patternOpacity, value);
}

void SpriteShader::setPatternScroll(const Vec2 &scroll)
{
    setVec2Uniform(u_patternScroll, scroll);
}

void SpriteShader::setPatternZoom(const Vec2 &zoom)
{
    setVec2Uniform(u_patternZoom, zoom);
}

void SpriteShader::setInvert(bool value)
{
    gl.Uniform1i(u_invert, value);
}


PlaneShader::PlaneShader()
	: ShaderBase("PlaneShader")
{
	INIT_SHADER(simple, plane, PlaneShader);

	ShaderBase::init();

	GET_U(tone);
	GET_U(color);
	GET_U(flash);
	GET_U(opacity);
}

void PlaneShader::setTone(const Vec4 &tone)
{
	setVec4Uniform(u_tone, tone);
}

void PlaneShader::setColor(const Vec4 &color)
{
	setVec4Uniform(u_color, color);
}

void PlaneShader::setFlash(const Vec4 &flash)
{
	setVec4Uniform(u_flash, flash);
}

void PlaneShader::setOpacity(float value)
{
	gl.Uniform1f(u_opacity, value);
}


GrayShader::GrayShader()
	: ShaderBase("GrayShader")
{
	INIT_SHADER(simple, gray, GrayShader);

	ShaderBase::init();

	GET_U(gray);
}

bool GrayShader::framebufferScalingAllowed()
{
	// This shader is used with input textures that have already had a
	// framebuffer scale applied. So we don't want to double-apply it.
	return false;
}

void GrayShader::setGray(float value)
{
	gl.Uniform1f(u_gray, value);
}


TilemapShader::TilemapShader()
	: ShaderBase("TilemapShader")
{
	INIT_SHADER(tilemap, tilemap, TilemapShader);

	ShaderBase::init();

	GET_U(tone);
	GET_U(color);
	GET_U(opacity);

	GET_U(cpuAniOffsets);
	GET_U(aniIndex);
	GET_U(atFrames);
}

void TilemapShader::setTone(const Vec4 &tone)
{
	setVec4Uniform(u_tone, tone);
}

void TilemapShader::setColor(const Vec4 &color)
{
	setVec4Uniform(u_color, color);
}

void TilemapShader::setOpacity(float value)
{
	gl.Uniform1f(u_opacity, value);
}

void TilemapShader::setAnimation(int value, int frames[7])
{
	if (u_cpuAniOffsets >= 0)
	{
		GLfloat offsets[14];
		for (int i = 0; i < 7; ++i)
		{
			const int frameCount = frames[i] > 0 ? frames[i] : 1;
			const int frame = value % frameCount;
			offsets[i * 2] = static_cast<GLfloat>((frame % 8) * 96);
			offsets[i * 2 + 1] = static_cast<GLfloat>((frame / 8) * 32);
		}

		gl.Uniform2fv(u_cpuAniOffsets, 7, offsets);
		return;
	}

	gl.Uniform1i(u_aniIndex, value);
	gl.Uniform1iv(u_atFrames, 7, frames);
}



FlashMapShader::FlashMapShader()
	: ShaderBase("FlashMapShader")
{
	INIT_SHADER(simpleColor, flashMap, FlashMapShader);

	ShaderBase::init();

	GET_U(alpha);
}

void FlashMapShader::setAlpha(float value)
{
	gl.Uniform1f(u_alpha, value);
}


HueShader::HueShader()
	: ShaderBase("HueShader")
{
	INIT_SHADER(simple, hue, HueShader);

	ShaderBase::init();

	GET_U(hueAdjust);
}

void HueShader::setHueAdjust(float value)
{
	gl.Uniform1f(u_hueAdjust, value);
}


SimpleMatrixShader::SimpleMatrixShader()
	: ShaderBase("SimpleMatrixShader")
{
	INIT_SHADER(simpleMatrix, simpleAlpha, SimpleMatrixShader);

	ShaderBase::init();

	GET_U(matrix);
}

void SimpleMatrixShader::setMatrix(const float value[16])
{
	gl.UniformMatrix4fv(u_matrix, 1, GL_FALSE, value);
}


BlurShader::HPass::HPass()
	: ShaderBase("BlurShader::HPass")
{
	INIT_SHADER(blurH, blur, BlurShader::HPass);

	ShaderBase::init();
}

BlurShader::VPass::VPass()
	: ShaderBase("BlurShader::VPass")
{
	INIT_SHADER(blurV, blur, BlurShader::VPass);

	ShaderBase::init();
}


TilemapVXShader::TilemapVXShader()
	: ShaderBase("TilemapVXShader")
{
	INIT_SHADER(tilemapvx, simple, TilemapVXShader);

	ShaderBase::init();

	GET_U(aniOffset);
}

void TilemapVXShader::setAniOffset(const Vec2 &value)
{
	gl.Uniform2f(u_aniOffset, value.x, value.y);
}


BltShader::BltShader()
	: ShaderBase("BltShader")
{
	INIT_SHADER(simple, bitmapBlit, BltShader);

	init();
}

BltShader::BltShader(const ShaderNoConstructTag &tag)
	: ShaderBase(tag.name ? tag.name : "BltShader(no_construct)")
{
}

void BltShader::init()
{
	ShaderBase::init();

	GET_U(source);
	GET_U(destination);
	GET_U(subRect);
	GET_U(opacity);
}

void BltShader::setSource()
{
	gl.Uniform1i(u_source, 0);
}

void BltShader::setDestination(const TEX::ID value)
{
	setTexUniform(u_destination, 1, value);
}

void BltShader::setSubRect(const FloatRect &value)
{
	gl.Uniform4f(u_subRect, value.x, value.y, value.w, value.h);
}

void BltShader::setOpacity(float value)
{
	gl.Uniform1f(u_opacity, value);
}

KglInvertShader::KglInvertShader()
	: ShaderBase("KglInvertShader")
{
	INIT_SHADER(simple, kglInvert, KglInvertShader);

	ShaderBase::init();
}

KglCompressAlphaShader::KglCompressAlphaShader()
	: ShaderBase("KglCompressAlphaShader")
{
	INIT_SHADER(simple, kglCompressAlpha, KglCompressAlphaShader);

	ShaderBase::init();
}

KglSubtractShader::KglSubtractShader() : BltShader(ShaderNoConstructTag("KglSubtractShader"))
{
	INIT_SHADER(simple, kglSubtract, KglSubtractShader);

	BltShader::init();
}

KglShadowShaderH::KglShadowShaderH()
	: ShaderBase("KglShadowShaderH")
{
	INIT_SHADER(simple, kglShadowH, KglShadowShaderH);

	ShaderBase::init();

	GET_U(x1);
	GET_U(x2);
	GET_U(y);
	GET_U(soft);
	GET_U(w);
	GET_U(h);
	GET_U(x_center);
	GET_U(y_center);
	GET_U(slope1);
	GET_U(slope2);
}

void KglShadowShaderH::setParams(int x1, int x2, int y, bool soft, int w, int h, int x_center, int y_center, double slope1, double slope2)
{
	gl.Uniform1i(u_x1, x1);
	gl.Uniform1i(u_x2, x2);
	gl.Uniform1i(u_y, y);
	gl.Uniform1i(u_soft, soft);
	gl.Uniform1i(u_w, w);
	gl.Uniform1i(u_h, h);
	gl.Uniform1i(u_x_center, x_center);
	gl.Uniform1i(u_y_center, y_center);
	gl.Uniform1f(u_slope1, slope1);
	gl.Uniform1f(u_slope2, slope2);
}

KglShadowShaderV::KglShadowShaderV()
	: ShaderBase("KglShadowShaderV")
{
	INIT_SHADER(simple, kglShadowV, KglShadowShaderV);

	ShaderBase::init();

	GET_U(y1);
	GET_U(y2);
	GET_U(x);
	GET_U(wall);
	GET_U(soft);
	GET_U(w);
	GET_U(h);
	GET_U(x_center);
	GET_U(y_center);
	GET_U(slope1);
	GET_U(slope2);
}

void KglShadowShaderV::setParams(int y1, int y2, int x, bool wall, bool soft, int w, int h, int x_center, int y_center, double slope1, double slope2)
{
	gl.Uniform1i(u_y1, y1);
	gl.Uniform1i(u_y2, y2);
	gl.Uniform1i(u_x, x);
	gl.Uniform1i(u_wall, wall);
	gl.Uniform1i(u_soft, soft);
	gl.Uniform1i(u_w, w);
	gl.Uniform1i(u_h, h);
	gl.Uniform1i(u_x_center, x_center);
	gl.Uniform1i(u_y_center, y_center);
	gl.Uniform1f(u_slope1, slope1);
	gl.Uniform1f(u_slope2, slope2);
}

BicubicShader::BicubicShader() : Lanczos3Shader(ShaderNoConstructTag("BicubicShader"))
{
	INIT_SHADER(simple, bicubic, BicubicShader);

	ShaderBase::init();

	GET_U(texOffsetX);
	GET_U(sourceSize);
	GET_U(bc);
}

void BicubicShader::setSharpness(int sharpness)
{
	gl.Uniform2f(u_bc, 1.f - sharpness * 0.01f, sharpness * 0.005f);
}

Lanczos3Shader::Lanczos3Shader() : SimpleShader(ShaderNoConstructTag("Lanczos3Shader"))
{
	INIT_SHADER(simple, lanczos3, Lanczos3Shader);

	ShaderBase::init();

	GET_U(texOffsetX);
	GET_U(sourceSize);
}

Lanczos3Shader::Lanczos3Shader(const ShaderNoConstructTag &tag) : SimpleShader(tag)
{
}

void Lanczos3Shader::setTexSize(const Vec2i &value)
{
	ShaderBase::setTexSize(value);
	gl.Uniform2f(u_sourceSize, (float)value.x, (float)value.y);
}

#ifdef MKXPZ_SSL
XbrzShader::XbrzShader() : Lanczos3Shader(ShaderNoConstructTag("XbrzShader"))
{
	INIT_SHADER(simple, xbrz, XbrzShader);

	ShaderBase::init();

	GET_U(texOffsetX);
	GET_U(sourceSize);
	GET_U(targetScale);
}

void XbrzShader::setTargetScale(const Vec2 &value)
{
	gl.Uniform2f(u_targetScale, value.x, value.y);
}
#endif
