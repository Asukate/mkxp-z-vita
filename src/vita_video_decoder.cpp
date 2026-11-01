#include "vita_video_decoder.h"

/* vitaGL directly: its header clashes with the engine's GL wrapper. */
#include "display/bitmap.h"
#include "filesystem/filesystemImpl.h"
#include "util/debugwriter.h"

#include <SDL_thread.h>
#include <SDL_timer.h>
#include <al.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <malloc.h>
#include <mutex>
#include <vector>

#include <psp2/avplayer.h>
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/sysmodule.h>
#include <vitaGL.h>

namespace VitaVideoDecoder
{
namespace
{
/* Frames the player may hold at once; each one gets its own texture so a
 * frame is never re-pointed while a draw of it may still be queued. */
const int kFrameBuffers = 5;
const int kAudioBuffers = 6;
/* No frame this long after starting: treat the video as unplayable. */
const Uint32 kStartTimeoutMs = 8000;

struct GpuBlock
{
	void *ptr;
	SceUID block; /* < 0: from vitaGL's pool */
};

std::mutex gpuMutex;
std::vector<GpuBlock> gpuBlocks;

/* Counters for the runtime log. */
std::atomic<unsigned> gpuAllocs{0}, gpuFailures{0}, gpuFailedSize{0}, cpuAllocs{0}, audioFrames{0};

void *allocCpu(void *, uint32_t alignment, uint32_t size)
{
	cpuAllocs.fetch_add(1);
	return memalign(alignment < 16 ? 16 : alignment, size ? size : 1);
}

void freeCpu(void *, void *ptr)
{
	free(ptr);
}

uint32_t alignUp(uint32_t value, uint32_t alignment)
{
	return (value + alignment - 1) & ~(alignment - 1);
}

/* Decoded frames must be GPU-readable, physically contiguous memory. vitaGL
 * already owns most of those pages, so take them from its pool first. */
void *allocGpu(void *, uint32_t alignment, uint32_t size)
{
	const uint32_t align = alignment < 0x40000u ? 0x40000u : alignment;
	void *ptr = vglAlloc(alignUp(size ? size : 1, align), VGL_MEM_SLOW);
	SceUID block = -1;
	if (!ptr)
	{
		const uint32_t mapped = alignUp(size ? size : 1, 1024 * 1024);
		block = sceKernelAllocMemBlock("hrpg_video_frame",
		                               SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW,
		                               mapped, nullptr);
		if (block < 0)
		{
			gpuFailures.fetch_add(1);
			gpuFailedSize.store(size);
			return nullptr;
		}
		if (sceKernelGetMemBlockBase(block, &ptr) < 0 || !ptr ||
		    sceGxmMapMemory(ptr, mapped, SCE_GXM_MEMORY_ATTRIB_RW) < 0)
		{
			sceKernelFreeMemBlock(block);
			gpuFailures.fetch_add(1);
			gpuFailedSize.store(size);
			return nullptr;
		}
	}
	gpuAllocs.fetch_add(1);
	std::lock_guard<std::mutex> lock(gpuMutex);
	gpuBlocks.push_back({ptr, block});
	return ptr;
}

/* closeContext() waits for the GPU before the player releases frames. */
void freeGpu(void *, void *ptr)
{
	if (!ptr)
		return;
	GpuBlock found = {nullptr, -1};
	{
		std::lock_guard<std::mutex> lock(gpuMutex);
		for (size_t i = 0; i < gpuBlocks.size(); ++i)
			if (gpuBlocks[i].ptr == ptr)
			{
				found = gpuBlocks[i];
				gpuBlocks.erase(gpuBlocks.begin() + i);
				break;
			}
	}
	if (!found.ptr)
		return;
	if (found.block < 0)
	{
		vglFree(ptr);
	}
	else
	{
		sceGxmUnmapMemory(ptr);
		sceKernelFreeMemBlock(found.block);
	}
}

struct Context
{
	SceAvPlayerHandle player = 0;
	bool moduleLoaded = false;
	Bitmap *target = nullptr;
	GLuint textures[kFrameBuffers] = {};
	SceGxmTexture *gxm[kFrameBuffers] = {};
	int frameIndex = 0;
	bool started = false;
	bool sawFrame = false;
	bool finished = false;
	Uint32 startTicks = 0;
	Uint32 lastReport = 0;
	unsigned frames = 0;
	unsigned polls = 0;
	bool everActive = false;
	std::string path;

	ALuint source = 0;
	ALuint buffers[kAudioBuffers] = {};
	SDL_Thread *audioThread = nullptr;
	std::atomic<bool> audioRunning{false};
	std::atomic<int> volume{128};
};

Context *ctx = nullptr;
bool badTermination = false;
int internalError = 0;
std::string internalErrorMessage;

/* Errors are logged and the video is skipped: the game's script would show
 * a message box for each one, and the game carries on either way. */
void fail(int code, const char *what)
{
	internalError = code;
	char text[160];
	snprintf(text, sizeof(text), "%s (0x%08x)", what, (unsigned)code);
	internalErrorMessage = text;
	Debug() << "VideoDecoder:" << internalErrorMessage;
}

void report(const char *phase)
{
	SceKernelFreeMemorySizeInfo info;
	memset(&info, 0, sizeof(info));
	info.size = sizeof(info);
	sceKernelGetFreeMemorySize(&info);
	char text[320];
	const Context *c = ctx;
	snprintf(text, sizeof(text),
	         "%s t=%ums polls=%u active=%d ever_active=%d frames=%u audio=%u "
	         "player_ms=%llu gpu=%u gpu_fail=%u fail_size=%u cpu=%u "
	         "vgl_slow_free=%uK phycont_free=%uK",
	         phase, c && c->started ? (unsigned)(SDL_GetTicks() - c->startTicks) : 0u,
	         c ? c->polls : 0u,
	         c && c->player ? (int)sceAvPlayerIsActive(c->player) : -1,
	         c ? (int)c->everActive : 0, c ? c->frames : 0u, audioFrames.load(),
	         c && c->player ? (unsigned long long)sceAvPlayerCurrentTime(c->player) : 0ull,
	         gpuAllocs.load(), gpuFailures.load(), gpuFailedSize.load(), cpuAllocs.load(),
	         (unsigned)(vglMemFree(VGL_MEM_SLOW) / 1024), (unsigned)(info.size_phycont / 1024));
	Debug() << "VideoDecoder:" << text;
}

int audioMain(void *)
{
	Context &c = *ctx;
	std::vector<ALuint> idle(c.buffers, c.buffers + kAudioBuffers);
	while (c.audioRunning.load())
	{
		ALint processed = 0;
		alGetSourcei(c.source, AL_BUFFERS_PROCESSED, &processed);
		while (processed-- > 0)
		{
			ALuint done = 0;
			alSourceUnqueueBuffers(c.source, 1, &done);
			idle.push_back(done);
		}

		bool queued = false;
		if (!idle.empty() && sceAvPlayerIsActive(c.player))
		{
			SceAvPlayerFrameInfo frame;
			memset(&frame, 0, sizeof(frame));
			if (sceAvPlayerGetAudioData(c.player, &frame) && frame.pData)
			{
				const int channels = frame.details.audio.channelCount == 1 ? 1 : 2;
				uint32_t bytes = frame.details.audio.size;
				if (bytes == 0)
					bytes = 1024 * channels * 2;
				ALuint buffer = idle.back();
				idle.pop_back();
				alBufferData(buffer, channels == 1 ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16,
				             frame.pData, bytes, frame.details.audio.sampleRate);
				alSourceQueueBuffers(c.source, 1, &buffer);
				audioFrames.fetch_add(1);
				queued = true;
			}
		}

		alSourcef(c.source, AL_GAIN, c.volume.load() / 128.0f);
		ALint state = 0, waiting = 0;
		alGetSourcei(c.source, AL_SOURCE_STATE, &state);
		alGetSourcei(c.source, AL_BUFFERS_QUEUED, &waiting);
		if (state != AL_PLAYING && waiting > 0)
			alSourcePlay(c.source);
		if (!queued)
			SDL_Delay(2);
	}
	return 0;
}

std::string resolvePath(std::string path)
{
	for (char &ch : path)
		if (ch == '\\')
			ch = '/';
	while (!path.empty() && (path.back() == '\n' || path.back() == '\r' || path.back() == ' '))
		path.pop_back();
	if (path.find(':') != std::string::npos)
		return path;
	std::string root = filesystemImpl::getCurrentDirectory();
	if (!root.empty() && root.back() != '/')
		root += '/';
	return root + path;
}

void destroy()
{
	if (!ctx)
		return;
	Context *c = ctx;
	if (c->audioThread)
	{
		c->audioRunning.store(false);
		SDL_WaitThread(c->audioThread, nullptr);
		c->audioThread = nullptr;
	}
	if (c->source)
	{
		alSourceStop(c->source);
		alSourcei(c->source, AL_BUFFER, 0);
		alDeleteSources(1, &c->source);
		alDeleteBuffers(kAudioBuffers, c->buffers);
	}
	/* Queued draws may still read a frame; finish them before the player
	 * gives its frame memory back. */
	glFinish();
	if (c->player)
	{
		sceAvPlayerStop(c->player);
		sceAvPlayerClose(c->player);
	}
	for (int i = 0; i < kFrameBuffers; ++i)
		if (c->textures[i] != 0)
			glDeleteTextures(1, &c->textures[i]);
	if (c->moduleLoaded)
		sceSysmoduleUnloadModule(SCE_SYSMODULE_AVPLAYER);
	delete c;
	ctx = nullptr;
}
/* A context that reports the video as already finished. */
int skipVideo()
{
	destroy();
	ctx = new Context;
	ctx->started = true;
	ctx->finished = true;
	return Success;
}
} // namespace

int createContext(const std::string &requested, int volume, Bitmap *bitmap)
{
	if (ctx)
		return DecoderInstanceAlreadyCreated;
	if (!bitmap || bitmap->isDisposed())
		return BitmapIsDisposed;

	badTermination = false;
	internalError = 0;
	internalErrorMessage.clear();

	const std::string path = resolvePath(requested);
	FILE *probe = fopen(path.c_str(), "rb");
	if (!probe)
	{
		Debug() << "VideoDecoder: not found:" << path;
		return FileNotFound;
	}
	fclose(probe);

	gpuAllocs.store(0);
	gpuFailures.store(0);
	gpuFailedSize.store(0);
	cpuAllocs.store(0);
	audioFrames.store(0);
	ctx = new Context;
	ctx->target = bitmap;
	ctx->volume.store(volume < 0 ? 0 : volume > 128 ? 128 : volume);

	if (sceSysmoduleIsLoaded(SCE_SYSMODULE_AVPLAYER) < 0)
	{
		if (sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER) < 0)
		{
			fail(InternalError, "could not load the video player module");
			return skipVideo();
		}
		ctx->moduleLoaded = true;
	}

	/* Placeholder textures: each is re-pointed at a decoded frame. The 8x8
	 * storage stays allocated so vitaGL frees its own memory, never the
	 * player's. */
	for (int i = 0; i < kFrameBuffers; ++i)
	{
		glGenTextures(1, &ctx->textures[i]);
		glBindTexture(GL_TEXTURE_2D, ctx->textures[i]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		ctx->gxm[i] = vglGetGxmTexture(GL_TEXTURE_2D);
		if (!ctx->gxm[i] || !vglGetTexDataPointer(GL_TEXTURE_2D))
		{
			glBindTexture(GL_TEXTURE_2D, 0);
			fail(InternalError, "could not prepare video textures");
			return skipVideo();
		}
	}
	glBindTexture(GL_TEXTURE_2D, 0);

	SceAvPlayerInitData init;
	memset(&init, 0, sizeof(init));
	init.memoryReplacement.allocate = allocCpu;
	init.memoryReplacement.deallocate = freeCpu;
	init.memoryReplacement.allocateTexture = allocGpu;
	init.memoryReplacement.deallocateTexture = freeGpu;
	init.basePriority = 0xA0;
	init.numOutputVideoFrameBuffers = kFrameBuffers;
	/* Playback starts when the source is added in startRender(), the order
	 * vitaGL's sample uses. */
	init.autoStart = SCE_TRUE;
	const SceAvPlayerHandle player = sceAvPlayerInit(&init);
	if (player == 0 || (uint32_t)player == SCE_AVPLAYER_ERROR_INVALID_PARAM ||
	    (uint32_t)player == SCE_AVPLAYER_ERROR_OUT_OF_MEMORY)
	{
		fail(player, "could not start the video player");
		return skipVideo();
	}
	ctx->player = player;
	ctx->path = path;

	alGenSources(1, &ctx->source);
	alGenBuffers(kAudioBuffers, ctx->buffers);
	alSourcei(ctx->source, AL_SOURCE_RELATIVE, AL_TRUE);
	alSourcef(ctx->source, AL_GAIN, ctx->volume.load() / 128.0f);

	Debug() << "VideoDecoder: opened" << path;
	report("opened");
	return Success;
}

int startRender()
{
	if (!ctx)
		return DecoderNotCreated;
	if (ctx->started || ctx->finished)
		return Success;
	const int rc = sceAvPlayerAddSource(ctx->player, ctx->path.c_str());
	if (rc < 0)
	{
		fail(rc, "could not open the video");
		ctx->started = true;
		ctx->finished = true;
		return Success;
	}
	ctx->started = true;
	ctx->startTicks = SDL_GetTicks();
	ctx->lastReport = ctx->startTicks;
	report("started");
	ctx->audioRunning.store(true);
	ctx->audioThread = SDL_CreateThread(audioMain, "hrpg video audio", nullptr);
	if (!ctx->audioThread)
		ctx->audioRunning.store(false);
	return Success;
}

int getVideoState()
{
	if (!ctx)
		return DecoderNotCreated;
	Context &c = *ctx;
	if (c.finished)
		return Success;
	if (!c.started)
		return VideoNotFinished;

	const bool active = sceAvPlayerIsActive(c.player);
	++c.polls;
	if (active)
		c.everActive = true;
	if (SDL_GetTicks() - c.lastReport >= 1000 && c.frames < 120)
	{
		c.lastReport = SDL_GetTicks();
		report("playing");
	}
	if (active)
	{
		SceAvPlayerFrameInfo frame;
		memset(&frame, 0, sizeof(frame));
		if (sceAvPlayerGetVideoData(c.player, &frame) && frame.pData)
		{
			const int w = (int)frame.details.video.width;
			const int h = (int)frame.details.video.height;
			if (w > 0 && h > 0 && w <= 1920 && h <= 1088)
			{
				c.frameIndex = (c.frameIndex + 1) % kFrameBuffers;
				SceGxmTexture *tex = c.gxm[c.frameIndex];
				if (sceGxmTextureInitLinear(tex, frame.pData, SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC1,
				                            w, h, 0) >= 0)
				{
					sceGxmTextureSetMinFilter(tex, SCE_GXM_TEXTURE_FILTER_LINEAR);
					sceGxmTextureSetMagFilter(tex, SCE_GXM_TEXTURE_FILTER_LINEAR);
					if (c.target && !c.target->isDisposed())
						c.target->drawExternalTexture(c.textures[c.frameIndex], w, h);
					c.sawFrame = true;
					++c.frames;
				}
			}
		}
	}
	else if (c.sawFrame)
	{
		report("finished");
		c.finished = true;
		return Success;
	}

	if (!c.sawFrame && SDL_GetTicks() - c.startTicks > kStartTimeoutMs)
	{
		report("no picture");
		fail(InternalError, "the video produced no picture");
		c.finished = true;
		return Success;
	}
	return VideoNotFinished;
}

int setVolume(int volume)
{
	if (!ctx)
		return DecoderNotCreated;
	ctx->volume.store(volume < 0 ? 0 : volume > 128 ? 128 : volume);
	return Success;
}

int closeContext()
{
	if (!ctx)
		return DecoderNotCreated;
	destroy();
	return Success;
}

int wasBadTermination()
{
	return badTermination ? 1 : 0;
}

int getInternalError()
{
	return internalError;
}

const char *getInternalErrorMessage()
{
	return internalErrorMessage.c_str();
}

Bitmap *target()
{
	return ctx ? ctx->target : nullptr;
}
} // namespace VitaVideoDecoder
