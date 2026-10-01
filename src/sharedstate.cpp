/*
** sharedstate.cpp
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

#include "sharedstate.h"
#ifdef __vita__
#include "filesystem/vita-content.h"
#endif

#include "util.h"
#include "filesystem.h"
#include "graphics.h"
#include "input.h"
#include "audio.h"
#include "glstate.h"
#include "shader.h"
#include "texpool.h"
#include "font.h"
#include "eventthread.h"
#include "gl-util.h"
#include "global-ibo.h"
#include "quad.h"
#include "binding.h"
#include "exception.h"
#include "sharedmidistate.h"
#include "vita_diagnostic.h"
#ifdef __vita__
#include "vita_startup_timer.h"
#endif

#include <unistd.h>
#include <stdio.h>
#include <string>
#include <chrono>

SharedState *SharedState::instance = 0;
int SharedState::rgssVersion = 0;
static GlobalIBO *_globalIBO = 0;

struct VitaMemberBoundary
{
	const char *name;

	explicit VitaMemberBoundary(const char *name)
	    : name(name)
	{
		vitaDiagLog("MEMBER", "before name=%s", name ? name : "<unnamed>");
		vitaDiagLog("BOOTPERF", "shared_member_begin name=%s",
		            name ? name : "<unnamed>");
#ifdef __vita__
		char phase[96];
		snprintf(phase, sizeof(phase), "shared_member_%s_begin",
		         name ? name : "unnamed");
		vitaStartupTimerMark(phase);
#endif
	}

	~VitaMemberBoundary()
	{
		vitaDiagLog("MEMBER", "after name=%s", name ? name : "<unnamed>");
		vitaDiagLog("BOOTPERF", "shared_member_end name=%s",
		            name ? name : "<unnamed>");
#ifdef __vita__
		char phase[96];
		snprintf(phase, sizeof(phase), "shared_member_%s_end",
		         name ? name : "unnamed");
		vitaStartupTimerMark(phase);
#endif
	}
};

static const char *gameArchExt()
{
	if (rgssVer == 1)
		return ".rgssad";
	else if (rgssVer == 2)
		return ".rgss2a";
	else if (rgssVer == 3)
		return ".rgss3a";

	assert(!"unreachable");
	return 0;
}

struct SharedStatePrivate
{
	void *bindingData;
	SDL_Window *sdlWindow;
	Scene *screen;

	FileSystem fileSystem;

	EventThread &eThread;
	RGSSThreadData &rtData;
	Config &config;

	SharedMidiState midiState;

	Graphics graphics;
	Input input;
	Audio audio;

	GLState _glState;

	ShaderSet shaders;

	TexPool texPool;

	SharedFontState fontState;
	Font *defaultFont;

	TEX::ID globalTex;
	int globalTexW, globalTexH;
	bool globalTexDirty;

	TEXFBO gpTexFBO;

	TEXFBO atlasTex;

	Quad gpQuad;

	unsigned int stampCounter;
    
    std::chrono::time_point<std::chrono::steady_clock> startupTime;

	SharedStatePrivate(RGSSThreadData *threadData)
	    : bindingData(0),
	      sdlWindow(threadData->window),
	      fileSystem((VitaMemberBoundary("fileSystem"), threadData->argv0),
	                 threadData->config.allowSymlinks),
	      eThread((VitaMemberBoundary("eThread"), *threadData->ethread)),
	      rtData((VitaMemberBoundary("rtData"), *threadData)),
	      config((VitaMemberBoundary("config"), threadData->config)),
	      midiState((VitaMemberBoundary("midiState"), threadData->config)),
	      graphics((VitaMemberBoundary("graphics"), threadData)),
	      input((VitaMemberBoundary("input"), *threadData)),
	      audio((VitaMemberBoundary("audio"), *threadData)),
	      _glState((VitaMemberBoundary("_glState"), threadData->config)),
	      shaders((VitaMemberBoundary("shaders"), "shaders")),
	      texPool((VitaMemberBoundary("texPool"), 4194304u)),
	      fontState((VitaMemberBoundary("fontState"), threadData->config)),
	      gpTexFBO((VitaMemberBoundary("gpTexFBO"), "gpTexFBO")),
	      atlasTex((VitaMemberBoundary("atlasTex"), "atlasTex")),
	      gpQuad((VitaMemberBoundary("gpQuad"), "gpQuad")),
	      stampCounter(0)
	{
#ifdef __vita__
		vitaStartupTimerMark("shared_members_ready");
#endif
	}
	
	void init(RGSSThreadData *threadData)
	{
        startupTime = std::chrono::steady_clock::now();
        
		/* Vita compiles shaders lazily on first use.  Releasing its runtime
		 * compiler here would make those later constructions fail. */
#ifndef __vita__
		if (gl.ReleaseShaderCompiler)
			gl.ReleaseShaderCompiler();
#endif

		/* Vita's POSIX current-directory emulation is not the same path
		 * namespace used by PhysFS.  Pass the resolved game root explicitly;
		 * it is app0:/ for self-contained packages and may be another ux0:
		 * directory when gameFolder points at an existing installation. */
#ifdef __vita__
		std::string gameRoot = mkxp_fs::getCurrentDirectory();
		if (!gameRoot.empty() && gameRoot[gameRoot.size() - 1] != '/')
			gameRoot += '/';
		std::string archPath = gameRoot + config.execName + gameArchExt();
#else
		std::string archPath = config.execName + gameArchExt();
#endif

#ifdef __vita__
        FILE *archiveProbe = fopen(archPath.c_str(), "rb");
        const bool haveArchive = archiveProbe != nullptr;
        if (archiveProbe) fclose(archiveProbe);
        const std::string assetsPath = gameRoot + "Assets.zip";
        FILE *assetsProbe = fopen(assetsPath.c_str(), "rb");
        const bool haveAssets = assetsProbe != nullptr;
        if (assetsProbe) fclose(assetsProbe);
        const auto contentPaths = VitaContent::mountOrder(
            gameRoot, haveArchive ? archPath : "", config.patches, config.rtps,
            haveAssets ? assetsPath : "");
        for (size_t i = 0; i < contentPaths.size(); ++i) {
            vitaDiagLog("CONTENT", "mount priority=%u path=%s",
                        static_cast<unsigned>(i), contentPaths[i].c_str());
            vitaDiagLog("BOOTPERF", "mount_begin priority=%u path=%s",
                        static_cast<unsigned>(i), contentPaths[i].c_str());
            const auto rtpRoot = config.vitaRtpArchiveRoots.find(contentPaths[i]);
            fileSystem.addPath(contentPaths[i].c_str(), nullptr, false,
                rtpRoot == config.vitaRtpArchiveRoots.end() ? nullptr : rtpRoot->second.c_str());
            vitaDiagLog("BOOTPERF", "mount_end priority=%u path=%s",
                        static_cast<unsigned>(i), contentPaths[i].c_str());
        }
#else
        for (const auto &path : config.patches) fileSystem.addPath(path.c_str());
        FILE *archiveProbe = fopen(archPath.c_str(), "rb");
        if (archiveProbe) {
            fclose(archiveProbe);
            fileSystem.addPath(archPath.c_str());
        }
        fileSystem.addPath(".");
        for (const auto &path : config.rtps) fileSystem.addPath(path.c_str());
#endif

#ifdef __vita__
		vitaStartupTimerMark("filesystem_mounts_ready");
#endif

		if (config.pathCache)
		{
			vitaDiagLog("BOOTPERF", "path_cache_begin");
			fileSystem.createPathCache();
			vitaDiagLog("BOOTPERF", "path_cache_end");
		}

		vitaDiagLog("BOOT", "before_font_discovery");
		vitaDiagLog("BOOTPERF", "font_discovery_begin");
#ifdef __vita__
		vitaStartupTimerMark("font_discovery_begin");
#endif
		fileSystem.initFontSets(fontState);
#ifdef __vita__
		vitaStartupTimerMark("font_discovery_end");
#endif
		vitaDiagLog("BOOT", "after_font_discovery");
		vitaDiagLog("BOOTPERF", "font_discovery_end");
		vitaDiagLog("FS", "font_sets_ready");

		globalTexW = 128;
		globalTexH = 64;

		globalTex = TEX::gen();
		TEX::bind(globalTex);
		TEX::setRepeat(false);
		TEX::setSmooth(false);
		TEX::allocEmpty(globalTexW, globalTexH);
		globalTexDirty = false;

		TEXFBO::init(gpTexFBO);
		/* Reuse starting values */
		TEXFBO::allocEmpty(gpTexFBO, globalTexW, globalTexH);
		TEXFBO::linkFBO(gpTexFBO);
		vitaDiagLog("GL", "global_texture_and_fbo_ready size=%dx%d",
		            globalTexW, globalTexH);

		/* RGSS3 games will call setup_midi, so there's
		 * no need to do it on startup */
		if (rgssVer <= 2)
		{
			vitaDiagLog("BOOTPERF", "midi_init_begin");
			midiState.initIfNeeded(threadData->config);
			vitaDiagLog("BOOTPERF", "midi_init_end");
		}
#ifdef __vita__
		vitaStartupTimerMark("shared_private_init_done");
#endif
	}

	~SharedStatePrivate()
	{
		TEX::del(globalTex);
		TEXFBO::fini(gpTexFBO);
		TEXFBO::fini(atlasTex);
	}
};

void SharedState::initInstance(RGSSThreadData *threadData)
{
	/* This section is tricky because of dependencies:
	 * SharedState depends on GlobalIBO existing,
	 * Font depends on SharedState existing */

	rgssVersion = threadData->config.rgssVersion;
    
	vitaDiagLog("BOUNDARY", "before object=GlobalIBO action=new");
	_globalIBO = new GlobalIBO();
	#ifdef MKXPZ_VITA_DIAGNOSTICS
	vitaDiagLog("BOUNDARY", "after object=GlobalIBO action=new ibo=%u gl_error=0x%04x",
	            static_cast<unsigned int>(_globalIBO->ibo.gl),
	            static_cast<unsigned int>(gl.GetError()));
	#endif
	_globalIBO->ensureSize(1);

	SharedState::instance = 0;
	Font *defaultFont = 0;

	try
	{
		vitaDiagLog("BOUNDARY", "before object=SharedState action=new");
		SharedState::instance = new SharedState(threadData);
		#ifdef MKXPZ_VITA_DIAGNOSTICS
		vitaDiagLog("BOUNDARY", "after object=SharedState action=new gl_error=0x%04x",
		            static_cast<unsigned int>(gl.GetError()));
		#endif
		#ifdef __vita__
		vitaStartupTimerMark("font_defaults_begin");
		#endif
		vitaDiagLog("BOOTPERF", "font_defaults_begin");
	Font::initDefaults(instance->p->fontState);
	vitaDiagLog("BOOTPERF", "font_defaults_end");
		#ifdef __vita__
		vitaStartupTimerMark("font_defaults_end");
		#endif
		defaultFont = new Font();
	}
	catch (const Exception &exc)
	{
		delete _globalIBO;
		delete SharedState::instance;
		delete defaultFont;

		throw exc;
	}

	SharedState::instance->p->defaultFont = defaultFont;
}

void SharedState::finiInstance()
{
	delete SharedState::instance->p->defaultFont;

	delete SharedState::instance;

	delete _globalIBO;
}

void SharedState::setScreen(Scene &screen)
{
	p->screen = &screen;
}

#define GSATT(type, lower) \
	type SharedState :: lower() const \
	{ \
		return p->lower; \
	}

GSATT(void*, bindingData)
GSATT(SDL_Window*, sdlWindow)
GSATT(Scene*, screen)
GSATT(FileSystem&, fileSystem)
GSATT(EventThread&, eThread)
GSATT(RGSSThreadData&, rtData)
GSATT(Config&, config)
GSATT(Graphics&, graphics)
GSATT(Input&, input)
GSATT(Audio&, audio)
GSATT(GLState&, _glState)
GSATT(ShaderSet&, shaders)
GSATT(TexPool&, texPool)
GSATT(Quad&, gpQuad)
GSATT(SharedFontState&, fontState)
GSATT(SharedMidiState&, midiState)

void SharedState::setBindingData(void *data)
{
	p->bindingData = data;
}

void SharedState::ensureQuadIBO(size_t minSize)
{
	_globalIBO->ensureSize(minSize);
}

GlobalIBO &SharedState::globalIBO()
{
	return *_globalIBO;
}

void SharedState::bindTex()
{
	TEX::bind(p->globalTex);

	if (p->globalTexDirty)
	{
		TEX::allocEmpty(p->globalTexW, p->globalTexH);
		p->globalTexDirty = false;
	}
}

void SharedState::ensureTexSize(int minW, int minH, Vec2i &currentSizeOut)
{
	if (minW > p->globalTexW)
	{
		p->globalTexDirty = true;
		p->globalTexW = findNextPow2(minW);
	}

	if (minH > p->globalTexH)
	{
		p->globalTexDirty = true;
		p->globalTexH = findNextPow2(minH);
	}

	currentSizeOut = Vec2i(p->globalTexW, p->globalTexH);
}

#ifdef __vita__
TEXFBO &SharedState::uploadScratchFBO(int width, int height, const void *pixels)
{
    // TexImage replaces storage. vitaGL retires the old backing until queued
    // draws complete, without copying pixels that this upload supersedes.
    TEXFBO &tex = p->gpTexFBO;
    {
        static uint64_t n = 0;
        const int prevW = tex.width, prevH = tex.height;
        if ((++n % 32) == 1)
            vitaDiagLog("SCRATCH", "fbo_upload n=%llu prev=%dx%d next=%dx%d",
                        (unsigned long long)n, prevW, prevH, width, height);
    }
    tex.width = width;
    tex.height = height;
    TEX::bind(tex.tex);
    TEX::uploadImage(width, height, pixels, GL_RGBA);
    return tex;
}

void SharedState::uploadScratchTexture(int width, int height, const void *pixels,
                                      Vec2i &sizeOut)
{
    {
        static uint64_t n = 0;
        if ((++n % 32) == 1)
            vitaDiagLog("SCRATCH", "tex_upload n=%llu prev=%dx%d next=%dx%d",
                        (unsigned long long)n, p->globalTexW, p->globalTexH, width, height);
    }
    p->globalTexW = width;
    p->globalTexH = height;
    p->globalTexDirty = false;
    TEX::bind(p->globalTex);
    TEX::uploadImage(width, height, pixels, GL_RGBA);
    sizeOut = Vec2i(width, height);
}
#endif

TEXFBO &SharedState::gpTexFBO(int minW, int minH)
{
	bool needResize = false;

	if (minW > p->gpTexFBO.width)
	{
		p->gpTexFBO.width = findNextPow2(minW);
		needResize = true;
	}

	if (minH > p->gpTexFBO.height)
	{
		p->gpTexFBO.height = findNextPow2(minH);
		needResize = true;
	}

	if (needResize)
	{
		vitaDiagLog("SCRATCH", "gp_resize req=%dx%d actual=%dx%d",
		            minW, minH, p->gpTexFBO.width, p->gpTexFBO.height);
		TEX::bind(p->gpTexFBO.tex);
		TEX::allocEmpty(p->gpTexFBO.width, p->gpTexFBO.height);
	}

	return p->gpTexFBO;
}

void SharedState::requestAtlasTex(int w, int h, TEXFBO &out)
{
	TEXFBO tex;

	if (w == p->atlasTex.width && h == p->atlasTex.height)
	{
		tex = p->atlasTex;
		p->atlasTex = TEXFBO();
	}
	else
	{
		TEXFBO::init(tex);
		TEXFBO::allocEmpty(tex, w, h);
		TEXFBO::linkFBO(tex);
	}

	out = tex;
}

void SharedState::releaseAtlasTex(TEXFBO &tex)
{
	/* No point in caching an invalid object */
	if (tex.tex == TEX::ID(0))
		return;

	TEXFBO::fini(p->atlasTex);

	p->atlasTex = tex;
}

void SharedState::checkShutdown()
{
	if (!p->rtData.rqTerm)
		return;

	p->rtData.rqTermAck.set();
	p->texPool.disable();
	scriptBinding->terminate();
}

void SharedState::checkReset()
{
	if (!p->rtData.rqReset)
		return;

	p->rtData.rqReset.clear();
	scriptBinding->reset();
}

Font &SharedState::defaultFont() const
{
	return *p->defaultFont;
}

double SharedState::runTime() {
    if (!p) return 0;
    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(now - p->startupTime).count() / 1000.0 / 1000.0;
}

unsigned int SharedState::genTimeStamp()
{
	return p->stampCounter++;
}

SharedState::SharedState(RGSSThreadData *threadData)
{
	p = new SharedStatePrivate(threadData);
	SharedState::instance = this;
	try
	{
		p->init(threadData);
		p->screen = p->graphics.getScreen();
	}
	catch (const Exception &exc)
	{
		// If the "error" was the user quitting the game before the path cache finished building,
		// then just return
		if (rtData().rqTerm)
			return;
		
		delete p;
		SharedState::instance = 0;
		
		throw exc;
	}
}

SharedState::~SharedState()
{
	delete p;
}
