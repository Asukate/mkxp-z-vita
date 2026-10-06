#include "frame_profile.h"
/*
 ** graphics.cpp
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

#include "graphics.h"

#include "alstream.h"
#include "audio.h"
#include "binding.h"
#include "bitmap.h"
#include "config.h"
#include "debugwriter.h"
#include "disposable.h"
#include "etc.h"
#include "etc-internal.h"
#include "eventthread.h"
#include "filesystem.h"
#include "gl-fun.h"
#include "gl-util.h"
#include "glstate.h"
#include "intrulist.h"
#include "quad.h"
#include "gl/quadarray.h"
#include "scene.h"
#include "shader.h"
#include "sharedstate.h"
extern "C" void vglSwapBuffers(unsigned char has_commondialog);
extern "C" unsigned int gxm_front_buffer_index;
extern "C" unsigned int gxm_back_buffer_index;
extern "C" void *gxm_color_surfaces_addr[];
#include <psp2/gxm.h>
extern "C" SceGxmTexture *vglGetGxmTexture(unsigned int target);
extern "C" void *vglGetTexDataPointer(unsigned int target);
extern "C" SceGxmTexture *vglGetGxmTextureByID(unsigned int texture);
extern "C" void *vglGetTexDataPointerByID(unsigned int texture);
extern "C" void *vglGetFramebufferColorData(unsigned int fbo);
extern "C" void glFinish(void);
extern "C" SceGxmContext *gxm_context;
/* Minimal vitaGL texture-struct prefix (matches the pa build: no
 * TEXTURES_SPEEDHACK / HAVE_TEX_CACHE / HAVE_UNPURE_TEXTURES defines —
 * gxm_tex sits at offset 12). Used for the raw-memory TEXFBO probe. */
struct VitaGLTexturePrefix
{
    uint32_t last_frame;
    uint8_t status, mip_count, ref_counter, faces_counter;
    GLboolean use_mips, dirty, overridden;
    SceGxmTexture gxm_tex;
};
#include "texpool.h"
#include "theoraplay/theoraplay.h"
#include "vita_diagnostic.h"
#ifdef __vita__
#include <psp2/kernel/processmgr.h>
#include <psp2/display.h>
#endif
#include "util.h"
#include "input.h"
#include "sprite.h"
#include "plane.h"
#include "tilemap.h"
#include "viewport.h"

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_timer.h>
#include <SDL_video.h>
#include <SDL_mutex.h>
#include <SDL_thread.h>

#ifdef MKXPZ_STEAM
#include "steamshim_child.h"
#endif

#include <algorithm>
#include <errno.h>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>
#include <cmath>
#include <climits>


#define DEF_SCREEN_W (rgssVer == 1 ? 640 : 544)
#define DEF_SCREEN_H (rgssVer == 1 ? 480 : 416)

#define DEF_FRAMERATE (rgssVer == 1 ? 40 : 60)

#define DEF_MAX_VIDEO_FRAMES 30
#define VIDEO_DELAY 10
#define MOVIE_AUDIO_BUFFER_SIZE 2048
#define AUDIO_BUFFER_LEN_MS 2000

#ifdef __vita__
static inline void vitaBootMarkFirstGamePresent()
{
	static bool done = false;
	if (done)
		return;
	done = true;
	vitaDiagLog("BOOTPERF", "first_game_present");
}
#else
static inline void vitaBootMarkFirstGamePresent() {}
#endif

typedef struct AudioQueue
{
    const THEORAPLAY_AudioPacket *audio;
    int offset;
    struct AudioQueue *next;
} AudioQueue;


static long readMovie(THEORAPLAY_Io *io, void *buf, long buflen)
{
    SDL_RWops *f = (SDL_RWops *) io->userdata;
    return (long) SDL_RWread(f, buf, 1, buflen);
} // IoFopenRead


static void closeMovie(THEORAPLAY_Io *io)
{
    SDL_RWops *f = (SDL_RWops *) io->userdata;
    SDL_RWclose(f);
    free(io);
} // IoFopenClose


struct Movie
{
    THEORAPLAY_Decoder *decoder;
    const THEORAPLAY_AudioPacket *audio;
    const THEORAPLAY_VideoFrame *video;
    bool hasVideo;
    bool hasAudio;
    bool skippable;
    Bitmap *videoBitmap;
    SDL_RWops srcOps;
    SDL_Thread *audioThread;
    AtomicFlag audioThreadTermReq;
    volatile AudioQueue *audioQueueHead;
    volatile AudioQueue *audioQueueTail;
    ALuint audioSource;
    ALuint alBuffers[STREAM_BUFS];
    ALshort audioBuffer[MOVIE_AUDIO_BUFFER_SIZE];
    SDL_mutex *audioMutex;
    
    Movie(bool skippable_)
    : decoder(0), audio(0), video(0), skippable(skippable_), videoBitmap(0), audioThread(0)
    {
    }
    bool preparePlayback()
    {
        
        // https://theora.org/doc/libtheora-1.0/codec_8h.html
        // https://ffmpeg.org/doxygen/0.11/group__lavc__misc__pixfmt.html
        THEORAPLAY_Io *io = (THEORAPLAY_Io *) malloc(sizeof (THEORAPLAY_Io));
        if(!io) {
            SDL_RWclose(&srcOps);
            return false;
        }
        
        io->read = readMovie;
        io->close = closeMovie;
        io->userdata = &srcOps;
        decoder = THEORAPLAY_startDecode(io, DEF_MAX_VIDEO_FRAMES, THEORAPLAY_VIDFMT_RGBA);
        if (!decoder) {
            SDL_RWclose(&srcOps);
            return false;
        }
        
        // Wait until the decoder has parsed out some basic truths from the file.
        while (!THEORAPLAY_isInitialized(decoder)) {
            SDL_Delay(VIDEO_DELAY);
        }
        
        // Once we're initialized, we can tell if this file has audio and/or video.
        hasAudio = THEORAPLAY_hasAudioStream(decoder);
        hasVideo = THEORAPLAY_hasVideoStream(decoder);
        
        // Queue up the audio
        if (hasAudio) {
            while ((audio = THEORAPLAY_getAudio(decoder)) == NULL) {
                if ((THEORAPLAY_availableVideo(decoder) >= DEF_MAX_VIDEO_FRAMES)) {
                    break;  // we'll never progress, there's no audio yet but we've prebuffered as much as we plan to.
                }
                SDL_Delay(VIDEO_DELAY);
            }
        }
        
        // No video, so no point in doing anything else
        if (!hasVideo) {
            THEORAPLAY_stopDecode(decoder);
            return false;
        }
        
        // Wait until we have video
        while ((video = THEORAPLAY_getVideo(decoder)) == NULL) {
            SDL_Delay(VIDEO_DELAY);
        }
        
        // Wait until we have audio, if applicable
        audio = NULL;
        if (hasAudio) {
            while ((audio = THEORAPLAY_getAudio(decoder)) == NULL && THEORAPLAY_availableVideo(decoder) < DEF_MAX_VIDEO_FRAMES) {
                SDL_Delay(VIDEO_DELAY);
            }
        }
        // Create this Bitmap without a hires replacement, because we don't
        // support hires replacement for Movies yet.
        videoBitmap = new Bitmap(video->width, video->height, true);
        audioQueueHead = NULL;
        audioQueueTail = NULL;
        
        return true;
    }
    
    void queueAudioPacket(const THEORAPLAY_AudioPacket *audio) {
        AudioQueue *item = NULL;
        
        if (!audio) {
            return;
        }
        
        item = (AudioQueue *) malloc(sizeof (AudioQueue));
        if (!item) {
            THEORAPLAY_freeAudio(audio);
            return;  // oh well.
        }
        
        item->audio = audio;
        item->offset = 0;
        item->next = NULL;
        
        SDL_LockMutex(audioMutex);
        if (audioQueueTail) {
            audioQueueTail->next = item;
        } else {
            audioQueueHead = item;
        }
        audioQueueTail = item;
        SDL_UnlockMutex(audioMutex);
    }
    
    void bufferMovieAudio(THEORAPLAY_Decoder *decoder, const Uint32 now) {
        const THEORAPLAY_AudioPacket *audio;
        while ((audio = THEORAPLAY_getAudio(decoder)) != NULL) {
            queueAudioPacket(audio);
            if (audio->playms >= now + AUDIO_BUFFER_LEN_MS) {  // don't let this get too far ahead.
                break;
            }
        }
    }

    void streamMovieAudio(){
        ALint state = 0;
        ALint procBufs = STREAM_BUFS;	    
        volatile AudioQueue *audioPacketAndOffset;
        int channels;
        int sampleRate;
        float *sourceSamples;
        ALuint samplesToProcess;
        ALshort *sampleBuffer;
        ALuint remainingSamples;

        while(true) {
            while(procBufs--) {
                // Quit if audio thread terminate request has been made
                if (audioThreadTermReq) return;

                remainingSamples = MOVIE_AUDIO_BUFFER_SIZE;
                sampleBuffer = audioBuffer;
                SDL_LockMutex(audioMutex);

                while(audioQueueHead && (remainingSamples > 0)) {
                    audioPacketAndOffset = audioQueueHead;
                    channels = audioPacketAndOffset->audio->channels;
                    sampleRate = audioPacketAndOffset->audio->freq;
                    sourceSamples = audioPacketAndOffset->audio->samples + (audioPacketAndOffset->offset * channels);
                    samplesToProcess = (audioPacketAndOffset->audio->frames - audioPacketAndOffset->offset) * channels;

                    if (samplesToProcess > remainingSamples) samplesToProcess = remainingSamples;

                    for (ALuint i = 0; i < samplesToProcess; i++) {
                        const float val = (*(sourceSamples++));
                        if (val < -1.0f) {
                            *(sampleBuffer++) = SHRT_MIN;
                        } else if (val > 1.0f) {
                            *(sampleBuffer++) = SHRT_MAX;
                        } else {
                            *(sampleBuffer++) = (ALshort) (val * SHRT_MAX);
                        }
                    }

                    // Necessary to remember position between repeated iterations
                    audioPacketAndOffset->offset += (samplesToProcess / channels);
                    remainingSamples -= samplesToProcess;

                    // The current audio packet has been completed
                    if ((audioPacketAndOffset->offset) >= audioPacketAndOffset->audio->frames) {
                        audioQueueHead = audioPacketAndOffset->next;
                        THEORAPLAY_freeAudio(audioPacketAndOffset->audio);
                        free((void *) audioPacketAndOffset);
                    }
                }

                if(!audioQueueHead) audioQueueTail = NULL;

                SDL_UnlockMutex(audioMutex);

                alBufferData(alBuffers[procBufs], channels == 1 ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16, audioBuffer,
                    (MOVIE_AUDIO_BUFFER_SIZE - remainingSamples) * sizeof(ALshort), sampleRate);
                alSourceQueueBuffers(audioSource, 1, &alBuffers[procBufs]);     
                alGetSourcei(audioSource, AL_SOURCE_STATE, &state);
                if(state != AL_PLAYING) alSourcePlay(audioSource);
            }

            // Periodically check the buffers until one is available
            while(true) {
                // Quit if audio thread terminate request has been made
                if (audioThreadTermReq) return;

                alGetSourcei(audioSource, AL_BUFFERS_PROCESSED, &procBufs);
                if(procBufs > 0) break;
                SDL_Delay(AUDIO_SLEEP);
            }
            alSourceUnqueueBuffers(audioSource, procBufs, alBuffers);
        }
    }
    
    bool startAudio(float volume)
    {
        alGenSources(1, &audioSource);
        alGenBuffers(STREAM_BUFS, alBuffers);
        alSourcef(audioSource, AL_GAIN, volume);

        audioThreadTermReq.clear();
        audioMutex = SDL_CreateMutex();
        queueAudioPacket(audio);
        audio = NULL;
        bufferMovieAudio(decoder, 0);
        audioThread = createSDLThread <Movie, &Movie::streamMovieAudio>(this, "movieaudio");

        return true;
    }
    
    void play(float volume)
    {
        Uint32 frameMs = 0;
        Uint32 baseTicks = SDL_GetTicks();
        bool openedAudio = false;
        while (THEORAPLAY_isDecoding(decoder)) {
            // Check for reset/shutdown input
            if(shState->graphics().updateMovieInput(this)) break;
            
            // Check for attempted skip
            if (skippable) {
                shState->input().update();
                if  (shState->input().isTriggered(Input::C) || shState->input().isTriggered(Input::B)) break;
            }
            
            const Uint32 now = SDL_GetTicks() - baseTicks;
            
            if (!video) {
                video = THEORAPLAY_getVideo(decoder);
            }
            
            if (hasAudio) {
                if (!audio) {
                    audio = THEORAPLAY_getAudio(decoder);
                }
                
                if (audio && !openedAudio) {
                    if(!startAudio(volume)){
                        Debug() << "Error opening movie audio!";
                        break;
                    }
                    openedAudio = true;
                }
                
            }
            
            if (video && (video->playms <= now)) {
                frameMs = (video->fps == 0.0) ? 0 : ((Uint32) (1000.0 / video->fps));
                if ( frameMs && ((now - video->playms) >= frameMs) )
                {
                    // Skip frames to catch up
                    const THEORAPLAY_VideoFrame *last = video;
                    while ((video = THEORAPLAY_getVideo(decoder)) != NULL)
                    {
                        THEORAPLAY_freeVideo(last);
                        last = video;
                        if ((now - video->playms) < frameMs)
                            break;
                    } 

                    if (!video)
                        video = last;
                }

                // Application is too far behind
                if (!video) {
                    Debug() << "WARNING: Video playback cannot keep up!";
                    break;
                }

                // Got a video frame, now draw it
                videoBitmap->replaceRaw(video->pixels, video->width * video->height * 4);
                shState->graphics().update(false);
                THEORAPLAY_freeVideo(video);
                video = NULL;

            } else {
                // Next video frame not yet ready, let the CPU breathe
                SDL_Delay(VIDEO_DELAY);
            }
            
            if (openedAudio) {
                bufferMovieAudio(decoder, now);
            }
        }
    }
    
    ~Movie()
    {
        if (hasAudio) {
            if (audioQueueTail) {
                THEORAPLAY_freeAudio(audioQueueTail->audio);
            }
            audioQueueTail = NULL;
            
            if (audioQueueHead) {
                THEORAPLAY_freeAudio(audioQueueHead->audio);
            }
            audioQueueHead = NULL;
            SDL_DestroyMutex(audioMutex);
            audioThreadTermReq.set();
            if(audioThread) {
                SDL_WaitThread(audioThread, 0);
                audioThread = 0;
            }
            alSourceStop(audioSource);
            alDeleteSources(1, &audioSource);
            alDeleteBuffers(STREAM_BUFS, alBuffers);
        }
        if (video) THEORAPLAY_freeVideo(video);
        if (audio) THEORAPLAY_freeAudio(audio);
        if (decoder) THEORAPLAY_stopDecode(decoder);
        delete videoBitmap;
    }
};

struct MovieOpenHandler : FileSystem::OpenHandler
{
    SDL_RWops *srcOps;
    
    MovieOpenHandler(SDL_RWops &srcOps)
    :   srcOps(&srcOps)
    {}
    
    bool tryRead(SDL_RWops &ops, const char *ext)
    {
        *srcOps = ops;
        return true;
    }
};

struct PingPong {
    TEXFBO rt[2];
    uint8_t srcInd, dstInd;
    int screenW, screenH;
    
    PingPong(int screenW, int screenH)
    : srcInd(0), dstInd(1), screenW(screenW), screenH(screenH) {
        for (int i = 0; i < 2; ++i) {
            TEXFBO::init(rt[i]);
            TEXFBO::allocEmpty(rt[i], screenW, screenH);
            TEXFBO::linkFBO(rt[i]);
            vitaFboDiag("pingpong_clear_begin", rt[i].tex.gl, rt[i].fbo.gl,
                        screenW, screenH);
            gl.ClearColor(0, 0, 0, 1);
            FBO::clear();
            vitaFboDiag("pingpong_clear_done", rt[i].tex.gl, rt[i].fbo.gl,
                        screenW, screenH);
        }
    }
    
    ~PingPong() {
        for (int i = 0; i < 2; ++i)
            TEXFBO::fini(rt[i]);
    }
    
    TEXFBO &backBuffer() { return rt[srcInd]; }
    
    TEXFBO &frontBuffer() { return rt[dstInd]; }
    
    /* Better not call this during render cycles */
    void resize(int width, int height) {
        screenW = width;
        screenH = height;

        for (int i = 0; i < 2; ++i) {
            /* Vita: realloc without fini leaks the previous backing store
             * in the GPU pools (observed as repeated 640x480 allocs with no
             * matching finis across window-adjust events). */
            TEXFBO::fini(rt[i]);
            TEXFBO::init(rt[i]);
            TEXFBO::allocEmpty(rt[i], width, height);
            TEXFBO::linkFBO(rt[i]);
        }
    }
    
    void startRender() { bind(); }
    
    void swapRender() {
        std::swap(srcInd, dstInd);
        
        bind();
    }
    
    void clearBuffers() {
        glState.clearColor.pushSet(Vec4(0, 0, 0, 1));
        
        for (int i = 0; i < 2; ++i) {
            FBO::bind(rt[i].fbo);
            FBO::clear();
        }
        
        glState.clearColor.pop();
    }
    
private:
    void bind() { FBO::bind(rt[dstInd].fbo); }
};


#ifdef MKXPZ_VITA_DIAGNOSTICS
static unsigned int vitaDiagGridReadback(const char *tag, const char *which,
                                         int fboW, int fboH, unsigned int frame)
{
    unsigned char px[4] = {0, 0, 0, 0};
    uint32_t crc = 0x811C9DC5u;
    unsigned int nonblack = 0;
    const int cols = 12;
    const int rows = 8;
    const unsigned int pendingError = gl.GetError();
    for (int r = 0; r < rows; ++r) {
        const int y = (fboH > 1) ? (r * (fboH - 1)) / (rows - 1) : 0;
        for (int cc = 0; cc < cols; ++cc) {
            const int x = (fboW > 1) ? (cc * (fboW - 1)) / (cols - 1) : 0;
            gl.ReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
            const uint32_t v = (uint32_t)((px[0] << 24) | (px[1] << 16) | (px[2] << 8) | px[3]);
            crc = (crc ^ v) * 16777619u;
            if (px[0] || px[1] || px[2])
                ++nonblack;
        }
    }
    const unsigned int readError = gl.GetError();
    vitaDiagLog("PROBE", "grid %s frame=%u size=%dx%d nonblack=%u crc=0x%08x err=0x%04x",
                which, frame, fboW, fboH, nonblack, crc, readError | pendingError);
    return nonblack;
}
#endif

#ifdef MKXPZ_VITA_DIAGNOSTICS
/* S42-I: one-shot post-scene tail trace.
 * Armed from TilemapVXPrivate::prepare (vitaDiagS42IArmTailTrace) after the
 * next-frame prepare completes; traces exactly one Graphics::update tail:
 * ScreenScene::composite -> redrawScreen -> swapGLBuffer -> updateAvgFPS. */
static bool s42iTailActive = false;
static bool s42iTailConsumed = false;
static bool s42lBindingTracePending = false;

void vitaDiagS42IArmTailTrace()
{
	if (s42iTailActive || s42iTailConsumed)
		return;

	s42iTailActive = true;
	vitaDiagLog("TEXTFLOW", "S42I TAIL_TRACE_ARMED");
}

bool vitaDiagS42LTakeBindingTrace()
{
	if (!s42lBindingTracePending)
		return false;

	s42lBindingTracePending = false;
	return true;
}

static inline void s42iMark(const char *fmt, ...)
{
	if (!s42iTailActive)
		return;

	char buf[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	vitaDiagLog("TEXTFLOW", "S42I %s", buf);
}
#endif

class ScreenScene : public Scene {
public:
    ScreenScene(int width, int height) : pp(width, height) {
        updateReso(width, height);
        
        brightEffect = false;
        brightnessQuad.setColor(Vec4());
    }
    
    void composite() {
        FrameProfile::Scope profile(FrameProfile::SceneComposite);
        const int w = geometry.rect.w;
        const int h = geometry.rect.h;
        
        { FrameProfile::Scope prepare(FrameProfile::PrepareDraw); shState->prepareDraw(); }
#ifdef MKXPZ_VITA_DIAGNOSTICS
        s42iMark("PREPAREDRAW_ALL_DONE");
        s42iMark("PP_START_RENDER_BEGIN");
#endif
        { FrameProfile::Scope setup(FrameProfile::SceneSetup); pp.startRender(); }
#ifdef MKXPZ_VITA_DIAGNOSTICS
        s42iMark("PP_START_RENDER_DONE");
#endif
        
        glState.viewport.set(IntRect(0, 0, w, h));
        
#ifdef MKXPZ_VITA_DIAGNOSTICS
        /* Legacy probe: draws an offscreen quad and reads it back. This
         * crashes the physical-Vita GPU during real-game composites, so it
         * is opt-in via the enable-probe-quad trigger file. The normal
         * scene composite below is what actually renders the game. */
        if (access("ux0:/data/hardrpg/enable-probe-quad", F_OK) == 0) {
            glState.scissorTest.pushSet(false);
            glState.blend.pushSet(false);
            glState.clearColor.pushSet(Vec4(0.03f, 0.18f, 0.80f, 1.0f));
            FBO::clear();

            FlatColorShader &probeShader = shState->shaders().flatColor;
            probeShader.bind();
            probeShader.applyViewportProj();
            probeShader.setColor(Vec4(0.95f, 0.05f, 0.02f, 1.0f));
            vitaProbeQuad.draw();

            unsigned char corner[4] = {0, 0, 0, 0};
            unsigned char center[4] = {0, 0, 0, 0};
            const unsigned int drawError = gl.GetError();
            gl.ReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, corner);
            const unsigned int cornerError = gl.GetError();
            gl.ReadPixels(w / 2, h / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, center);
            const unsigned int centerError = gl.GetError();
            const unsigned int probeFrame = ++vitaProbeFrame;
            if (probeFrame <= 3 || (probeFrame % 60) == 0) {
                vitaDiagLog("PROBE", "offscreen_quad frame=%u target=%u size=%dx%d corner=%u,%u,%u,%u center=%u,%u,%u,%u errors=0x%04x,0x%04x,0x%04x",
                            probeFrame, pp.frontBuffer().fbo.gl, w, h,
                            corner[0], corner[1], corner[2], corner[3],
                            center[0], center[1], center[2], center[3],
                            drawError, cornerError, centerError);
            }

            glState.clearColor.pop();
            glState.blend.pop();
            glState.scissorTest.pop();
        }
#ifdef MKXPZ_VITA_DIAGNOSTICS
        s42iMark("ROOT_FBO_CLEAR_BEGIN");
#endif
        FBO::clear();
#ifdef MKXPZ_VITA_DIAGNOSTICS
        s42iMark("ROOT_FBO_CLEAR_DONE");
#endif
        /* Session 33: ONE-SHOT FBO-reattach A/B at composite #120.
         * vitaGL scene_reset now reattaches stale FBO attachments (backport).
         * 1) Per-top-level-element error attribution (drain/PRE/draw/POST).
         * 2) NORMAL_RESOLVED scan (glFinish first) — THE KEY TEST: with the
         *    reattach fix, the color surface should now match tex storage and
         *    NORMAL_RESOLVED should contain the scene.
         * 3) Conditional MAGENTA control ONLY IF NORMAL_RESOLVED is still
         *    entirely zero (diagnostic fallback). */
        static bool s33Gate = false;
        static int s33Frames = 0;
        ++s33Frames;
        /* S41: instrumentation gate DISABLED (regression-bisect diagnostic).
         * Force s33Fire=false so the plain Scene::composite() path runs every
         * frame — reverting composite() to S33-clean behavior while keeping the
         * genuine vitaGL lifecycle fixes. The S35/S37 probe bodies stay compiled
         * but are dead. */
        const bool s33Fire = false;
        /* S34b: early geometry capture at composite #5 (before any exit/crash) */
        static bool s34GeoDone = false;
        if (!s34GeoDone && s33Frames == 5) {
            s34GeoDone = true;
            const TEXFBO &etgt = pp.frontBuffer();
            const IntRect &evp = glState.viewport.get();
            const IntRect &esc = glState.scissorBox.get();
            vitaDiagLog("S34-GEO", "early composite#5 tex=%u fbo=%u tex_w=%u tex_h=%u",
                        (unsigned)etgt.tex.gl, (unsigned)etgt.fbo.gl,
                        (unsigned)etgt.width, (unsigned)etgt.height);
            vitaDiagLog("S34-GEO", "early scene_viewport=%d,%d %dx%d scissorEnabled=%d scissorBox=%d,%d %dx%d",
                        evp.x, evp.y, evp.w, evp.h,
                        glState.scissorTest.get() ? 1 : 0,
                        esc.x, esc.y, esc.w, esc.h);
        }
        if (s33Fire) {
            s33Gate = true;
            vitaDiagLog("S33", "PP index src=%u dst=%u", (unsigned)pp.srcInd, (unsigned)pp.dstInd);
            const TEXFBO &tgt = pp.frontBuffer();
            vitaDiagLog("S33", "target_obj=%p tex_id=%u fbo_id=%u", (const void *)&tgt,
                        (unsigned)tgt.tex.gl, (unsigned)tgt.fbo.gl);
            vitaDiagLog("S33", "final_source_obj=%p tex_id=%u", (const void *)&tgt,
                        (unsigned)tgt.tex.gl);

            /* Element census (aggregate, one-shot) */
            int total = 0, visible = 0;
            int vp = 0, sp = 0, pl = 0, tl = 0, ot = 0;
            for (IntruListLink<SceneElement> *it = elements.begin();
                 it != elements.end(); it = it->next) {
                SceneElement *e = it->data;
                ++total;
                if (!e->getVisible()) continue;
                ++visible;
                if (dynamic_cast<Viewport *>(e)) ++vp;
                else if (dynamic_cast<Sprite *>(e)) ++sp;
                else if (dynamic_cast<Plane *>(e)) ++pl;
                else if (dynamic_cast<Tilemap *>(e)) ++tl;
                else ++ot;
            }
            vitaDiagLog("S33", "SCENE elements_total=%d visible=%d composited=%d", total, visible, visible);
            vitaDiagLog("S33", "SCENE Viewport=%d Sprite=%d Plane=%d Tilemap=%d other=%d", vp, sp, pl, tl, ot);

            vitaDiagLog("S33", "PRE_SCENE_GLERR=0x%04x", (unsigned)gl.GetError());
            const IntRect &svp = glState.viewport.get();
            vitaDiagLog("S34-GEO", "scene_viewport=%d,%d %dx%d (during ScreenScene::composite)",
                        svp.x, svp.y, svp.w, svp.h);
            vitaDiagLog("S34-GEO", "screenGeometry=%d,%d %dx%d",
                        geometry.rect.x, geometry.rect.y, geometry.rect.w, geometry.rect.h);
        }
        if (s33Fire) {
            /* Attributed composite: exactly what Scene::composite() does
             * (iterate visible top-level elements, call draw()), but with a
             * drained PRE error and a POST error checkpoint per element. */
            int ei = 0;
            for (IntruListLink<SceneElement> *it = elements.begin();
                 it != elements.end(); it = it->next, ++ei) {
                SceneElement *e = it->data;
                if (!e->getVisible()) continue;
                const char *etype = "other";
                if (dynamic_cast<Viewport *>(e)) etype = "Viewport";
                else if (dynamic_cast<Sprite *>(e)) etype = "Sprite";
                else if (dynamic_cast<Plane *>(e)) etype = "Plane";
                else if (dynamic_cast<Tilemap *>(e)) etype = "Tilemap";
                (void)gl.GetError(); /* drain stale error */
                vitaDiagLog("S33-ELEM", "i=%d type=%s PRE_ERR=0x%04x", ei, etype, (unsigned)gl.GetError());
                /* S35: per-element geometry at the one title composite.
                 * The quadrant bug shifts content by ~(+320,+240) — find the
                 * element whose effective draw coordinates carry that offset. */
                {
                    int gx = -99999, gy = -99999, gox = 0, goy = 0;
                    float gzx = 1.f, gzy = 1.f;
                    int srx = -1, sry = -1, srw = -1, srh = -1;
                    int vpx = -1, vpy = -1, vpw = -1, vph = -1;
                    int vpox = -99999, vpoy = -99999;
                    if (Sprite *sp = dynamic_cast<Sprite *>(e)) {
                        gx = sp->getX(); gy = sp->getY();
                        gox = sp->getOX(); goy = sp->getOY();
                        gzx = sp->getZoomX(); gzy = sp->getZoomY();
                        Rect &sr = sp->getSrcRect();
                        srx = sr.x; sry = sr.y; srw = sr.width; srh = sr.height;
                        if (Viewport *vp = sp->getViewport()) {
                            Rect &vr = vp->getRect();
                            vpx = vr.x; vpy = vr.y; vpw = vr.width; vph = vr.height;
                            vpox = vp->getOX(); vpoy = vp->getOY();
                        }
                    } else if (Viewport *vp = dynamic_cast<Viewport *>(e)) {
                        Rect &vr = vp->getRect();
                        vpx = vr.x; vpy = vr.y; vpw = vr.width; vph = vr.height;
                        vpox = vp->getOX(); vpoy = vp->getOY();
                    }
                    vitaDiagLog("S35-ELEM", "i=%d type=%s obj=%p x=%d y=%d ox=%d oy=%d zoom=%f,%f src=%d,%d %dx%d vp=%d,%d %dx%d vpox=%d vpoy=%d",
                                ei, etype, (const void *)e, gx, gy, gox, goy,
                                (double)gzx, (double)gzy, srx, sry, srw, srh,
                                vpx, vpy, vpw, vph, vpox, vpoy);
                }
                e->draw();
                vitaDiagLog("S33-ELEM", "i=%d type=%s POST_ERR=0x%04x", ei, etype, (unsigned)gl.GetError());
            }
        } else {
#ifdef MKXPZ_VITA_DIAGNOSTICS
            s42iMark("SCENE_COMPOSITE_BEGIN");
#endif
            Scene::composite();
#ifdef MKXPZ_VITA_DIAGNOSTICS
            s42iMark("SCENE_COMPOSITE_DONE");
#endif
        }
        if (s33Fire) {
            vitaDiagLog("S33", "POST_SCENE_GLERR=0x%04x", (unsigned)gl.GetError());
            glFinish();
            const TEXFBO &tgt = pp.frontBuffer();
            vitaDiagLog("S33", "TARGET tex=%u fbo=%u tex_data=%p fbo_color_data=%p",
                        (unsigned)tgt.tex.gl, (unsigned)tgt.fbo.gl,
                        vglGetTexDataPointerByID(tgt.tex.gl),
                        vglGetFramebufferColorData(tgt.fbo.gl));
            SceGxmTexture *gxmTex = vglGetGxmTextureByID(tgt.tex.gl);
            void *tdata = vglGetTexDataPointerByID(tgt.tex.gl);
            bool normalZero = false;
            if (gxmTex && tdata) {
                unsigned int tw = sceGxmTextureGetWidth(gxmTex);
                unsigned int th = sceGxmTextureGetHeight(gxmTex);
                SceGxmTextureFormat tfmt = sceGxmTextureGetFormat(gxmTex);
                vitaDiagLog("S33", "NORMAL_RESOLVED tex=%u fbo=%u data=%p w=%u h=%u format=0x%08x",
                            (unsigned)tgt.tex.gl, (unsigned)tgt.fbo.gl, tdata, tw, th, (unsigned)tfmt);
                bool is32 = ((unsigned)tfmt & 0xFF000000u) == 0x0C000000u;
                bool sane = tw > 0 && th > 0 && tw <= 4096 && th <= 4096;
                if (is32 && sane) {
                    const uint32_t *w = (const uint32_t *)tdata;
                    size_t totalWords = (size_t)tw * th;
                    if (totalWords >= 4) {
                        uint32_t first = w[0];
                        uint64_t same = 0, diff = 0, zero = 0;
                        uint32_t orAll = 0;
                        uint32_t hash = 2166136261u;
                        for (size_t i = 0; i < totalWords; ++i) {
                            uint32_t v = w[i];
                            orAll |= v;
                            hash ^= v;
                            hash *= 16777619u;
                            if (v == first) ++same;
                            else ++diff;
                            if (v == 0) ++zero;
                        }
                        size_t q1 = totalWords / 4, q3 = (totalWords * 3) / 4;
                        vitaDiagLog("S33", "NORMAL_RESOLVED first=%08x total=%u same=%u different=%u zero=%u or=%08x hash=%08x",
                                    first, (unsigned)totalWords, (unsigned)same,
                                    (unsigned)diff, (unsigned)zero, orAll, hash);
                        vitaDiagLog("S33", "NORMAL_RESOLVED words first=%08x quarter=%08x center=%08x threeq=%08x last=%08x",
                                    w[0], w[q1], w[totalWords/2], w[q3], w[totalWords-1]);
                        normalZero = (zero == totalWords);
                    } else {
                        vitaDiagLog("S33", "NORMAL_RESOLVED totalWords too small: %u", (unsigned)totalWords);
                    }
                } else {
                    vitaDiagLog("S33", "NORMAL_RESOLVED scan skipped is32=%d sane=%d", (int)is32, (int)sane);
                }
            } else {
                vitaDiagLog("S33", "NORMAL_RESOLVED lookup failed gxmTex=%p tdata=%p", (void *)gxmTex, tdata);
            }

            /* CONDITIONAL magenta control (diagnostic fallback): only if the normal
             * frame is still entirely zero. */
            if (normalZero) {
                vitaDiagLog("S33", "CONTROL_CLEAR_BEGIN tex=%u fbo=%u (normal was zero)",
                            (unsigned)tgt.tex.gl, (unsigned)tgt.fbo.gl);
                glState.clearColor.pushSet(Vec4(1, 0, 1, 1)); /* R=255 G=0 B=255 A=255 */
                FBO::clear();
                glState.clearColor.pop();
                glFinish();
                SceGxmTexture *cTex = vglGetGxmTextureByID(tgt.tex.gl);
                void *cdata = vglGetTexDataPointerByID(tgt.tex.gl);
                if (cTex && cdata) {
                    unsigned int tw = sceGxmTextureGetWidth(cTex);
                    unsigned int th = sceGxmTextureGetHeight(cTex);
                    SceGxmTextureFormat tfmt = sceGxmTextureGetFormat(cTex);
                    vitaDiagLog("S33", "CONTROL_RESOLVED tex=%u fbo=%u data=%p w=%u h=%u format=0x%08x",
                                (unsigned)tgt.tex.gl, (unsigned)tgt.fbo.gl, cdata, tw, th, (unsigned)tfmt);
                    bool is32 = ((unsigned)tfmt & 0xFF000000u) == 0x0C000000u;
                    bool sane = tw > 0 && th > 0 && tw <= 4096 && th <= 4096;
                    if (is32 && sane) {
                        const uint32_t *w = (const uint32_t *)cdata;
                        size_t totalWords = (size_t)tw * th;
                        if (totalWords >= 4) {
                            uint32_t first = w[0];
                            uint64_t same = 0, diff = 0, zero = 0;
                            uint32_t orAll = 0;
                            uint32_t hash = 2166136261u;
                            for (size_t i = 0; i < totalWords; ++i) {
                                uint32_t v = w[i];
                                orAll |= v;
                                hash ^= v;
                                hash *= 16777619u;
                                if (v == first) ++same;
                                else ++diff;
                                if (v == 0) ++zero;
                            }
                            size_t q1 = totalWords / 4, q3 = (totalWords * 3) / 4;
                            vitaDiagLog("S33", "CONTROL_RESOLVED first=%08x total=%u same=%u different=%u zero=%u or=%08x hash=%08x",
                                        first, (unsigned)totalWords, (unsigned)same,
                                        (unsigned)diff, (unsigned)zero, orAll, hash);
                            vitaDiagLog("S33", "CONTROL_RESOLVED words first=%08x quarter=%08x center=%08x threeq=%08x last=%08x",
                                        w[0], w[q1], w[totalWords/2], w[q3], w[totalWords-1]);
                        } else {
                            vitaDiagLog("S33", "CONTROL_RESOLVED totalWords too small: %u", (unsigned)totalWords);
                        }
                    } else {
                        vitaDiagLog("S33", "CONTROL_RESOLVED scan skipped is32=%d sane=%d", (int)is32, (int)sane);
                    }
                } else {
                    vitaDiagLog("S33", "CONTROL_RESOLVED lookup failed gxmTex=%p cdata=%p", (void *)cTex, cdata);
                }
            } else {
                vitaDiagLog("S33", "CONTROL SKIPPED (normal frame has content)");
            }
            /* S37 Probe 3: clip-space control quad. Raw clip coords
             * (-1,-1)->(1,1) with IDENTITY projMat. If GXM viewport maps
             * clip -1..1 to the full 640x480 FBO, corner+center both read
             * red. If only center reads red, clip (0,0) maps to FBO (0,0)
             * → viewport half-offset. */
            {
                glState.scissorTest.pushSet(false);
                glState.blend.pushSet(false);
                FlatColorShader &probeShader = shState->shaders().flatColor;
                probeShader.bind();
                GLfloat ident[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
                GLint curProg = 0;
                gl.GetIntegerv(GL_CURRENT_PROGRAM, &curProg);
                GLint projLoc = gl.GetUniformLocation((GLuint)curProg, "projMat");
                gl.UniformMatrix4fv(projLoc, 1, GL_FALSE, ident);
                probeShader.setColor(Vec4(0.9f, 0.1f, 0.05f, 1.0f));
                SimpleQuadArray clipQuad;
                clipQuad.resize(1);
                clipQuad.vertices[0].pos = Vec2(-1.f, -1.f);
                clipQuad.vertices[1].pos = Vec2(1.f, -1.f);
                clipQuad.vertices[2].pos = Vec2(1.f, 1.f);
                clipQuad.vertices[3].pos = Vec2(-1.f, 1.f);
                clipQuad.commit();
                clipQuad.draw();
                glFinish();
                unsigned char corner[4] = {0}, center[4] = {0};
                gl.ReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, corner);
                gl.ReadPixels(w / 2, h / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, center);
                const unsigned int clipErr = gl.GetError();
                vitaDiagLog("S37-CLIP", "control_quad clip(-1,-1)->(1,1) identity_proj corner=%u,%u,%u,%u center=%u,%u,%u,%u err=0x%04x",
                            corner[0], corner[1], corner[2], corner[3],
                            center[0], center[1], center[2], center[3],
                            (unsigned)clipErr);
                /* S37b A/B: same FlatColorShader, but PIXEL coords
                 * (0,0)->(640,480) with the ORTHO projection — exactly what
                 * a full-screen Sprite submits. If this fills the FBO, the
                 * ortho matrix works and the bug is sprite-shader-specific.
                 * If content lands bottom-right, the mat4 uniform is being
                 * interpreted transposed (row-major) → -1,-1 lands in the
                 * bottom row → clip (0,0) = viewport center. */
                {
                    GLfloat ortho[16] = {
                        2.f/640, 0, 0, 0,
                        0, 2.f/480, 0, 0,
                        0, 0, -2, 0,
                        -1, -1, -1, 1
                    };
                    gl.UniformMatrix4fv(projLoc, 1, GL_FALSE, ortho);
                    probeShader.setColor(Vec4(0.1f, 0.9f, 0.1f, 1.0f));
                    SimpleQuadArray pixelQuad;
                    pixelQuad.resize(1);
                    pixelQuad.vertices[0].pos = Vec2(0.f, 0.f);
                    pixelQuad.vertices[1].pos = Vec2(640.f, 0.f);
                    pixelQuad.vertices[2].pos = Vec2(640.f, 480.f);
                    pixelQuad.vertices[3].pos = Vec2(0.f, 480.f);
                    pixelQuad.commit();
                    pixelQuad.draw();
                    glFinish();
                    unsigned char qcorner[4] = {0}, qcenter[4] = {0};
                    unsigned char qmid[4] = {0}; /* (480,360): mid BR quadrant */
                    gl.ReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, qcorner);
                    gl.ReadPixels(w / 2, h / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, qcenter);
                    gl.ReadPixels(w*3/4, h*3/4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, qmid);
                    const unsigned int orthoErr = gl.GetError();
                    vitaDiagLog("S37-CLIP", "ortho_pixel_quad (0,0)->(640,480) corner=%u,%u,%u,%u center=%u,%u,%u,%u midBR=%u,%u,%u,%u err=0x%04x",
                                qcorner[0], qcorner[1], qcorner[2], qcorner[3],
                                qcenter[0], qcenter[1], qcenter[2], qcenter[3],
                                qmid[0], qmid[1], qmid[2], qmid[3],
                                (unsigned)orthoErr);
                }
                /* S37c A/B: the SAME pixel quad through SimpleSpriteShader
                 * (the actual sprite program: projMat * spriteMat * pos)
                 * with spriteMat=identity + ortho projMat. Isolates whether
                 * sprite.vert itself miscompiles in the GXP path. */
                {
                    SimpleSpriteShader &spr = shState->shaders().simpleSprite;
                    spr.bind();
                    GLint sprProg = 0;
                    gl.GetIntegerv(GL_CURRENT_PROGRAM, &sprProg);
                    GLint sprProjLoc = gl.GetUniformLocation((GLuint)sprProg, "projMat");
                    GLint sprMatLoc = gl.GetUniformLocation((GLuint)sprProg, "spriteMat");
                    GLfloat ortho[16] = {
                        2.f/640, 0, 0, 0,
                        0, 2.f/480, 0, 0,
                        0, 0, -2, 0,
                        -1, -1, -1, 1
                    };
                    GLfloat ident[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
                    gl.UniformMatrix4fv(sprProjLoc, 1, GL_FALSE, ortho);
                    gl.UniformMatrix4fv(sprMatLoc, 1, GL_FALSE, ident);
                    SimpleQuadArray sprQuad;
                    sprQuad.resize(1);
                    sprQuad.vertices[0].pos = Vec2(0.f, 0.f);
                    sprQuad.vertices[1].pos = Vec2(640.f, 0.f);
                    sprQuad.vertices[2].pos = Vec2(640.f, 480.f);
                    sprQuad.vertices[3].pos = Vec2(0.f, 480.f);
                    /* texPos full-rect so sampling is defined; we only
                     * care about the POSITION mapping. */
                    sprQuad.vertices[0].texPos = Vec2(0.f, 0.f);
                    sprQuad.vertices[1].texPos = Vec2(640.f, 0.f);
                    sprQuad.vertices[2].texPos = Vec2(640.f, 480.f);
                    sprQuad.vertices[3].texPos = Vec2(0.f, 480.f);
                    sprQuad.commit();
                    sprQuad.draw();
                    glFinish();
                    unsigned char sc[4] = {0}, sctr[4] = {0}, smid[4] = {0};
                    gl.ReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, sc);
                    gl.ReadPixels(w / 2, h / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, sctr);
                    gl.ReadPixels(w*3/4, h*3/4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, smid);
                    const unsigned int sprErr = gl.GetError();
                    vitaDiagLog("S37-CLIP", "spriteShader_pixel_quad (0,0)->(640,480) projLoc=%d matLoc=%d corner=%u,%u,%u,%u center=%u,%u,%u,%u midBR=%u,%u,%u,%u err=0x%04x",
                                (int)sprProjLoc, (int)sprMatLoc,
                                sc[0], sc[1], sc[2], sc[3],
                                sctr[0], sctr[1], sctr[2], sctr[3],
                                smid[0], smid[1], smid[2], smid[3],
                                (unsigned)sprErr);
                }
                glState.blend.pop();
                glState.scissorTest.pop();
            }
        }
        if (access("ux0:/data/hardrpg/enable-probe-quad", F_OK) == 0) {
            if (vitaProbeFrame < 8 || (vitaProbeFrame % 60) == 0) {
                ++vitaProbeFrame;
                unsigned char srcCorner[4] = {0, 0, 0, 0};
                unsigned char srcCenter[4] = {0, 0, 0, 0};
                gl.ReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, srcCorner);
                const unsigned int cornerError = gl.GetError();
                gl.ReadPixels(w / 2, h / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, srcCenter);
                const unsigned int centerError = gl.GetError();
                vitaDiagLog("PROBE", "scene_frame frame=%u target=%u size=%dx%d corner=%u,%u,%u,%u center=%u,%u,%u,%u errors=0x%04x,0x%04x",
                            vitaProbeFrame, pp.frontBuffer().fbo.gl, w, h,
                            srcCorner[0], srcCorner[1], srcCorner[2], srcCorner[3],
                            srcCenter[0], srcCenter[1], srcCenter[2], srcCenter[3],
                            cornerError, centerError);
            }
        }
#else
        FBO::clear();
        Scene::composite();
#endif
        
        if (brightEffect) {
#ifdef MKXPZ_VITA_DIAGNOSTICS
            s42iMark("BRIGHTNESS_STATE active=%d", 1);
            s42iMark("BRIGHTNESS_DRAW_BEGIN");
#endif
            SimpleColorShader &shader = shState->shaders().simpleColor;
            shader.bind();
            shader.applyViewportProj();
            shader.setTranslation(Vec2i());

            brightnessQuad.draw();
#ifdef MKXPZ_VITA_DIAGNOSTICS
            s42iMark("BRIGHTNESS_DRAW_DONE");
#endif
        }
#ifdef MKXPZ_VITA_DIAGNOSTICS
        else
        {
            s42iMark("BRIGHTNESS_STATE active=%d", 0);
        }
#endif
#ifdef MKXPZ_VITA_DIAGNOSTICS
        /* Diagnostic overlay: flashes a small colored rect 1s on / 1s off,
         * color cycling every 2s, drawn after the real scene composite and
         * before final presentation. Opt-in via the enable-probe-overlay
         * trigger file. Reports a coarse 12x8 grid CRC/nonblack count from
         * the source FBO right after the overlay draw. */
        if (access("ux0:/data/hardrpg/enable-probe-overlay", F_OK) == 0) {
            ++vitaOverlayFrame;
            const bool on = (vitaOverlayFrame % 120) < 60;
            if (on) {
                static const Vec4 colors[6] = {
                    Vec4(1.0f, 0.0f, 0.0f, 1.0f),
                    Vec4(0.0f, 1.0f, 0.0f, 1.0f),
                    Vec4(0.0f, 0.0f, 1.0f, 1.0f),
                    Vec4(1.0f, 1.0f, 0.0f, 1.0f),
                    Vec4(0.0f, 1.0f, 1.0f, 1.0f),
                    Vec4(1.0f, 0.0f, 1.0f, 1.0f)
                };
                const int phase = (vitaOverlayFrame / 120) % 6;
                SimpleColorShader &shader = shState->shaders().simpleColor;
                shader.bind();
                shader.applyViewportProj();
                shader.setTranslation(Vec2i());
                vitaOverlayQuad.setColor(colors[phase]);
                vitaOverlayQuad.draw();
            }
            ++vitaProbeFrame;
            if (vitaProbeFrame < 8 || (vitaProbeFrame % 60) == 0) {
                const TEXFBO &src = pp.frontBuffer();
                const unsigned int nb = vitaDiagGridReadback("PROBE", "src_fbo",
                                                             src.width, src.height,
                                                             vitaProbeFrame);
                if (nb != vitaLastNonblack || vitaProbeFrame < 8) {
                    vitaDiagLog("PROBE", "grid_note src_nonblack=%u frame=%u",
                                nb, vitaProbeFrame);
                    vitaLastNonblack = nb;
                }
            }
        }
#endif
#ifdef MKXPZ_VITA_DIAGNOSTICS
        s42iMark("SCREEN_COMPOSITE_DONE");
#endif
    }

    void requestViewportRender(const Vec4 &c, const Vec4 &f, const Vec4 &t) {
        const IntRect &viewpRect = glState.scissorBox.get();
        const IntRect &screenRect = geometry.rect;
        
        const bool toneRGBEffect = t.xyzNotNull();
        const bool toneGrayEffect = t.w != 0;
        const bool colorEffect = c.w > 0;
        const bool flashEffect = f.w > 0;
        
        if (toneGrayEffect) {
            pp.swapRender();
            
            if (!viewpRect.encloses(screenRect)) {
                /* Scissor test _does_ affect FBO blit operations,
                 * and since we're inside the draw cycle, it will
                 * be turned on, so turn it off temporarily */
                glState.scissorTest.pushSet(false);
                
                int scaleIsSpecial = GLMeta::blitScaleIsSpecial(pp.frontBuffer(), false, geometry.rect, pp.backBuffer(), geometry.rect);

                GLMeta::blitBegin(pp.frontBuffer(), false, scaleIsSpecial);
                GLMeta::blitSource(pp.backBuffer(), scaleIsSpecial);
                GLMeta::blitRectangle(geometry.rect, Vec2i());
                GLMeta::blitEnd();
                
                glState.scissorTest.pop();
            }
            
            GrayShader &shader = shState->shaders().gray;
            shader.bind();
            shader.setGray(t.w);
            shader.applyViewportProj();
            shader.setTexSize(screenRect.size());
            
            TEX::bind(pp.backBuffer().tex);
            
            glState.blend.pushSet(false);
            screenQuad.draw();
            glState.blend.pop();
        }
        
        if (!toneRGBEffect && !colorEffect && !flashEffect)
            return;
        
        FlatColorShader &shader = shState->shaders().flatColor;
        shader.bind();
        shader.applyViewportProj();
        
        if (toneRGBEffect) {
            /* First split up additive / substractive components */
            Vec4 add, sub;
            
            if (t.x > 0)
                add.x = t.x;
            if (t.y > 0)
                add.y = t.y;
            if (t.z > 0)
                add.z = t.z;
            
            if (t.x < 0)
                sub.x = -t.x;
            if (t.y < 0)
                sub.y = -t.y;
            if (t.z < 0)
                sub.z = -t.z;
            
            /* Then apply them using hardware blending */
            gl.BlendFuncSeparate(GL_ONE, GL_ONE, GL_ZERO, GL_ONE);
            
            if (add.xyzNotNull()) {
                gl.BlendEquation(GL_FUNC_ADD);
                shader.setColor(add);
                
                screenQuad.draw();
            }
            
            if (sub.xyzNotNull()) {
                gl.BlendEquation(GL_FUNC_REVERSE_SUBTRACT);
                shader.setColor(sub);
                
                screenQuad.draw();
            }
        }
        
        if (colorEffect || flashEffect) {
            gl.BlendEquation(GL_FUNC_ADD);
            gl.BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO,
                                 GL_ONE);
        }
        
        if (colorEffect) {
            shader.setColor(c);
            screenQuad.draw();
        }
        
        if (flashEffect) {
            shader.setColor(f);
            screenQuad.draw();
        }
        
        glState.blendMode.refresh();
    }
    
    void setBrightness(float norm) {
        brightnessQuad.setColor(Vec4(0, 0, 0, 1.0f - norm));
        
        brightEffect = norm < 1.0f;
    }
    
    void updateReso(int width, int height) {
        geometry.rect.w = width;
        geometry.rect.h = height;
        
        screenQuad.setTexPosRect(geometry.rect, geometry.rect);
        brightnessQuad.setTexPosRect(geometry.rect, geometry.rect);
#ifdef MKXPZ_VITA_DIAGNOSTICS
        vitaProbeQuad.setPosRect(FloatRect(width * 0.25f, height * 0.25f,
                                           width * 0.50f, height * 0.50f));
        vitaOverlayQuad.setPosRect(FloatRect(0.0f, 0.0f,
                                             width * 0.10f, height * 0.10f));
#endif
        
        notifyGeometryChange();
    }
    
    void setResolution(int width, int height) {
        pp.resize(width, height);
        updateReso(width, height);
    }
    
    PingPong &getPP() { return pp; }
    
private:
    PingPong pp;
    Quad screenQuad;
    
    Quad brightnessQuad;
#ifdef MKXPZ_VITA_DIAGNOSTICS
    Quad vitaProbeQuad;
    Quad vitaOverlayQuad;
    unsigned int vitaProbeFrame = 0;
    unsigned int vitaOverlayFrame = 0;
    unsigned int vitaLastNonblack = 0;
    unsigned int vitaLastDefNonblack = 0;
    unsigned int vitaLastSrcNonblack = 0;
#endif
    bool brightEffect;
};

/* Nanoseconds per second */
#define NS_PER_S 1000000000

struct FPSLimiter {
    uint64_t lastTickCount;
    
    /* ticks per frame */
    int64_t tpf;
    
    /* Ticks per second */
    const uint64_t tickFreq;
    
    /* Ticks per milisecond */
    const uint64_t tickFreqMS;
    
    /* Ticks per nanosecond */
    const double tickFreqNS;
    
    bool disabled;
    
    /* Data for frame timing adjustment */
    struct {
        /* Last tick count */
        uint64_t last;
        
        /* How far behind/in front we are for ideal frame timing */
        int64_t idealDiff;
        
        bool resetFlag;
    } adj;
    
    FPSLimiter(uint16_t desiredFPS)
    : lastTickCount(SDL_GetPerformanceCounter()),
    tickFreq(SDL_GetPerformanceFrequency()), tickFreqMS(tickFreq / 1000),
    tickFreqNS((double)tickFreq / NS_PER_S), disabled(false) {
        setDesiredFPS(desiredFPS);
        
        adj.last = SDL_GetPerformanceCounter();
        adj.idealDiff = 0;
        adj.resetFlag = false;
    }
    
    void setDesiredFPS(uint16_t value) { tpf = tickFreq / value; }
    
    void delay() {
        if (disabled)
            return;
        
        int64_t tickDelta = SDL_GetPerformanceCounter() - lastTickCount;
        int64_t toDelay = tpf - tickDelta;
        
        /* Compensate for the last delta
         * to the ideal timestep */
        toDelay -= adj.idealDiff;
        
        if (toDelay < 0)
            toDelay = 0;
        
        delayTicks(toDelay);
        
        uint64_t now = lastTickCount = SDL_GetPerformanceCounter();
        int64_t diff = now - adj.last;
        adj.last = now;
        
        /* Recalculate our temporal position
         * relative to the ideal timestep */
        adj.idealDiff = diff - tpf + adj.idealDiff;
        
        if (adj.resetFlag) {
            adj.idealDiff = 0;
            adj.resetFlag = false;
        }
    }
    
    void resetFrameAdjust() { adj.resetFlag = true; }
    
    /* If we're more than a full frame's worth
     * of ticks behind the ideal timestep,
     * there's no choice but to skip frame(s)
     * to catch up */
    bool frameSkipRequired() const {
        if (disabled)
            return false;
        
        return adj.idealDiff > tpf;
    }
    
private:
    void delayTicks(uint64_t ticks) {
#if defined(HAVE_NANOSLEEP)
        struct timespec req;
        uint64_t nsec = ticks / tickFreqNS;
        req.tv_sec = nsec / NS_PER_S;
        req.tv_nsec = nsec % NS_PER_S;
        errno = 0;
        
        while (nanosleep(&req, &req) == -1) {
            int err = errno;
            errno = 0;
            
            if (err == EINTR)
                continue;
            
            Debug() << "nanosleep failed. errno:" << err;
            SDL_Delay(ticks / tickFreqMS);
            break;
        }
#else
        SDL_Delay(ticks / tickFreqMS);
#endif
    }
};

struct GraphicsPrivate {
    /* Screen resolution, ie. the resolution at which
     * RGSS renders at (settable with Graphics.resize_screen).
     * Can only be changed from within RGSS */
    Vec2i scRes;
    Vec2i scResLores;
    
    /* Screen size, to which the rendered frames are scaled up.
     * This can be smaller than the window size when fixed aspect
     * ratio is enforced */
    Vec2i scSize;
    
    /* Actual physical size of the game window */
    Vec2i winSize;
    
    /* Offset in the game window at which the scaled game screen
     * is blitted inside the game window */
    Vec2i scOffset;
    
    // Scaling factor, used to display the screen properly
    // on Retina displays
    int scalingFactor;
    
    ScreenScene screen;
    RGSSThreadData *threadData;
    SDL_GLContext glCtx;
    
    int frameRate;
    int frameCount;
    int brightness;
    
    double last_update;
    
    
    FPSLimiter fpsLimiter;
    
    // Can be set from Ruby. Takes priority over config setting.
    bool useFrameSkip;
    
    bool frozen;
    TEXFBO frozenScene;
    Quad screenQuad;
    
    float backingScaleFactor;
    
    Vec2i integerScaleFactor;
    TEXFBO integerScaleBuffer;
    bool integerScaleActive;
    bool integerLastMileScaling;
    
    std::vector<double> avgFPSData;
    double last_avg_update;
    SDL_mutex *avgFPSLock;
    
    SDL_mutex *glResourceLock;
    bool multithreadedMode;
    
    /* Global list of all live Disposables
     * (disposed on reset) */

#ifdef MKXPZ_VITA_DIAGNOSTICS
    unsigned int vitaLastDefNonblack = 0;
    unsigned int vitaLastSrcNonblack = 0;
#endif
    IntruList<Disposable> dispList;
    
    GraphicsPrivate(RGSSThreadData *rtData)
    : scResLores(DEF_SCREEN_W, DEF_SCREEN_H),
    scRes(rtData->config.enableHires ? (int)lround(rtData->config.framebufferScalingFactor * DEF_SCREEN_W) : DEF_SCREEN_W,
        rtData->config.enableHires ? (int)lround(rtData->config.framebufferScalingFactor * DEF_SCREEN_H) : DEF_SCREEN_H),
    scSize(scRes),
    winSize(rtData->config.defScreenW, rtData->config.defScreenH),
    screen(scRes.x, scRes.y), threadData(rtData),
    glCtx(SDL_GL_GetCurrentContext()), multithreadedMode(true),
    frameRate(DEF_FRAMERATE), frameCount(0), brightness(255),
    fpsLimiter(frameRate), useFrameSkip(rtData->config.frameSkip), frozen(false),
    last_update(0), last_avg_update(0), backingScaleFactor(1), integerScaleFactor(0, 0),
    integerScaleActive(rtData->config.integerScaling.active),
    integerLastMileScaling(rtData->config.integerScaling.lastMileScaling) {
        avgFPSData = std::vector<double>();
        avgFPSLock = SDL_CreateMutex();
        glResourceLock = SDL_CreateMutex();
        
        if (integerScaleActive) {
            integerScaleFactor = Vec2i(0, 0);
            rebuildIntegerScaleBuffer();
        }
        
        recalculateScreenSize(rtData->config.fixedAspectRatio);
        updateScreenResoRatio(rtData);
        
        vitaFboDiag("frozen_init_begin", frozenScene.tex.gl, frozenScene.fbo.gl,
                    scRes.x, scRes.y);
        TEXFBO::init(frozenScene);
        TEXFBO::allocEmpty(frozenScene, scRes.x, scRes.y);
        TEXFBO::linkFBO(frozenScene);
        vitaFboDiag("frozen_link_done", frozenScene.tex.gl, frozenScene.fbo.gl,
                    scRes.x, scRes.y);
        
        FloatRect screenRect(0, 0, scRes.x, scRes.y);
        screenQuad.setTexPosRect(screenRect, screenRect);
        
        fpsLimiter.resetFrameAdjust();
    }
    
    ~GraphicsPrivate() {
        TEXFBO::fini(frozenScene);
        TEXFBO::fini(integerScaleBuffer);
        SDL_DestroyMutex(avgFPSLock);
        SDL_DestroyMutex(glResourceLock);
    }
    
    void updateScreenResoRatio(RGSSThreadData *rtData) {
        Vec2 &ratio = rtData->sizeResoRatio;
        ratio.x = (float)scRes.x / scSize.x * backingScaleFactor;
        ratio.y = (float)scRes.y / scSize.y * backingScaleFactor;
        
        rtData->screenOffset = scOffset / backingScaleFactor;
    }
    
    /* Enforces fixed aspect ratio, if desired */
    void recalculateScreenSize(bool fixedAspectRatio) {
        scSize = winSize;
        
        if (!fixedAspectRatio) {
            if (!integerScaleActive || (integerScaleActive && integerLastMileScaling)) {
                scOffset = Vec2i(0, 0);
                return;
            }
        }
        
        if (integerScaleActive && !integerLastMileScaling) {
            scOffset.x = ((winSize.x / 2) - (scRes.x / 2) * integerScaleFactor.x);
            scOffset.y = ((winSize.y / 2) - (scRes.y / 2) * integerScaleFactor.y);
            
            scSize = Vec2i(scRes.x * integerScaleFactor.x, scRes.y * integerScaleFactor.y);
            return;
        }
        
        float resRatio = (float)scRes.x / scRes.y;
        float winRatio = (float)winSize.x / winSize.y;
        
        if (resRatio > winRatio)
            scSize.y = scSize.x / resRatio;
        else if (resRatio < winRatio)
            scSize.x = scSize.y * resRatio;
        
        scOffset.x = (winSize.x - scSize.x) / 2.f;
        scOffset.y = (winSize.y - scSize.y) / 2.f;
    }
    
    static int findHighestFittingScale(int base, int target) {
        int scale = 1;
        
        while (base * scale <= target)
            scale++;
        
        return std::max(scale - 1, 1);
    }
    
    /* Returns whether a new scale was found */
    bool findHighestIntegerScale()
    {
        Vec2i newScale(findHighestFittingScale(scRes.x, winSize.x),
                       findHighestFittingScale(scRes.y, winSize.y));
        
        if (threadData->config.fixedAspectRatio)
        {
            /* Limit both factors to the smaller of the two */
            newScale.x = newScale.y = std::min(newScale.x, newScale.y);
        }
        
        if (newScale == integerScaleFactor)
            return false;
        
        integerScaleFactor = newScale;
        return true;
    }
    
    void rebuildIntegerScaleBuffer()
    {
        TEXFBO::fini(integerScaleBuffer);
        TEXFBO::init(integerScaleBuffer);
        TEXFBO::allocEmpty(integerScaleBuffer, scRes.x * integerScaleFactor.x,
                           scRes.y * integerScaleFactor.y);
        TEXFBO::linkFBO(integerScaleBuffer);
    }
    
    bool integerScaleStepApplicable() const
    {
        if (!integerScaleActive)
            return false;
        
        if (integerScaleFactor.x < 1 || integerScaleFactor.y < 1) // XXX should be < 2, this is for testing only
            return false;
        
        return true;
    }
    
    void checkResize(bool skipIntScaleBuffer = false) {
        if (threadData->windowSizeMsg.poll(winSize)) {
            /* Query the actual size in pixels, not units */
            Vec2i drawableSize(winSize);
            threadData->drawableSizeMsg.poll(drawableSize);
            
            backingScaleFactor = drawableSize.x / winSize.x;
            winSize = drawableSize;
            
            /* Make sure integer buffers are rebuilt before screen offsets are
             * calculated so we have the final allocated buffer size ready */
            if (integerScaleActive && findHighestIntegerScale() && !skipIntScaleBuffer)
                rebuildIntegerScaleBuffer();
            
            /* some GL drivers change the viewport on window resize */
            glState.viewport.refresh();
            recalculateScreenSize(threadData->config.fixedAspectRatio);
            updateScreenResoRatio(threadData);
            
            SDL_Rect screen = {scOffset.x, scOffset.y, scSize.x, scSize.y};
            threadData->ethread->notifyGameScreenChange(screen);
        }
    }
    
    void checkShutDownReset() {
        shState->checkShutdown();
        shState->checkReset();
    }
    
    void shutdown() {
        threadData->rqTermAck.set();
        shState->texPool().disable();
        
        scriptBinding->terminate();
    }
    
    void swapGLBuffer() {
#ifdef MKXPZ_VITA_DIAGNOSTICS
        s42iMark("FPS_DELAY_BEGIN");
#endif
        { FrameProfile::Scope pacing(FrameProfile::Pacing); fpsLimiter.delay(); }
#ifdef MKXPZ_VITA_DIAGNOSTICS
        s42iMark("FPS_DELAY_DONE");
#endif

#ifdef MKXPZ_VITA_DIAGNOSTICS
        /* GPU readback probes are DISABLED by default: glReadPixels on the
         * live pipeline faults the physical-Vita GPU (confirmed in GPUCRASH
         * dumps 2026-08-07). Opt-in via the enable-probe-overlay trigger. */
        if (access("ux0:/data/hardrpg/enable-probe-overlay", F_OK) == 0) {
            if (frameCount < 3 || frameCount == 9 || (frameCount % 60) == 0) {
                /* Default framebuffer (post-blit, what would be presented) */
                const unsigned int defNb = vitaDiagGridReadback(
                    "PROBE", "def_fb", winSize.x, winSize.y, frameCount + 1);
                /* Source game FBO */
                TEXFBO &source = screen.getPP().frontBuffer();
                gl.BindFramebuffer(GL_FRAMEBUFFER, source.fbo.gl);
                const unsigned int srcNb = vitaDiagGridReadback(
                    "PROBE", "src_fbo_swap", source.width, source.height, frameCount + 1);
                FBO::bind(FBO::ID(0));
                if (defNb != vitaLastDefNonblack || srcNb != vitaLastSrcNonblack ||
                    frameCount < 3) {
                    vitaDiagLog("PROBE", "grid_note def_nonblack=%u src_nonblack=%u frame=%u",
                                defNb, srcNb, frameCount + 1);
                    vitaLastDefNonblack = defNb;
                    vitaLastSrcNonblack = srcNb;
                }
            }
        }
#endif

        /* Session 27: gated tail markers (frames 35-50 only) around every
         * post-swap statement. Minimal perturbation elsewhere. */
        const bool s27Gate = (frameCount >= 35 && frameCount <= 50);
        if (s27Gate) vitaDiagLog("S27", "f=%d BEFORE_SWAP", frameCount);
#ifdef MKXPZ_VITA_DIAGNOSTICS
        s42iMark("SDL_SWAP_BEGIN");
#endif
        { FrameProfile::Scope present(FrameProfile::Present); SDL_GL_SwapWindow(threadData->window); }
        FrameProfile::boundary();
#ifdef MKXPZ_VITA_DIAGNOSTICS
        s42iMark("SDL_SWAP_DONE");
        if (s42iTailActive) {
            s42iMark("S42K DISPLAY_QUEUE_FINISH_BEGIN");
            const int s42kDisplayQueueRet = sceGxmDisplayQueueFinish();
            s42iMark("S42K DISPLAY_QUEUE_FINISH_DONE rc=0x%08x",
                     (unsigned int)s42kDisplayQueueRet);
        }
#endif
        if (s27Gate) vitaDiagLog("S27", "f=%d AFTER_SWAP", frameCount);
        
        ++frameCount;
#ifdef MKXPZ_VITA_DIAGNOSTICS
        s42iMark("FRAMECOUNT_DONE value=%d", frameCount);
#endif

        if (s27Gate) vitaDiagLog("S27", "f=%d AFTER_FRAMECOUNT", frameCount);
        if (s27Gate) vitaDiagLog("S27", "f=%d BEFORE_NOTIFYFRAME", frameCount);
#ifdef MKXPZ_VITA_DIAGNOSTICS
        s42iMark("NOTIFY_FRAME_BEGIN");
#endif
        threadData->ethread->notifyFrame();
#ifdef MKXPZ_VITA_DIAGNOSTICS
        s42iMark("NOTIFY_FRAME_DONE");
#endif
        if (s27Gate) vitaDiagLog("S27", "f=%d AFTER_NOTIFYFRAME", frameCount);
#ifdef MKXPZ_VITA_DIAGNOSTICS
        s42iMark("SWAPGLBUFFER_DONE");
#endif
    }
    
    void compositeToBuffer(TEXFBO &buffer) {
        compositeToBufferScaled(buffer, scRes.x, scRes.y);
    }

    void compositeToBufferScaled(TEXFBO &buffer, int destWidth, int destHeight) {
        screen.composite();
        
        int scaleIsSpecial = GLMeta::blitScaleIsSpecial(buffer, false, IntRect(0, 0, destWidth, destHeight), screen.getPP().frontBuffer(), IntRect(0, 0, scRes.x, scRes.y));

        GLMeta::blitBegin(buffer, false, scaleIsSpecial);
        GLMeta::blitSource(screen.getPP().frontBuffer(), scaleIsSpecial);
        GLMeta::blitRectangle(IntRect(0, 0, scRes.x, scRes.y), IntRect(0, 0, destWidth, destHeight));
        GLMeta::blitEnd();
    }
    
    void metaBlitBufferFlippedScaled(int scaleIsSpecial) {
        metaBlitBufferFlippedScaled(scRes, scaleIsSpecial);
        GLMeta::blitRectangle(
                              IntRect(0, 0, scRes.x, scRes.y),
                              IntRect(scOffset.x,
                                      (scSize.y + scOffset.y),
                                      scSize.x,
                                      -scSize.y),
                              GLMeta::smoothScalingMethod(scaleIsSpecial) == Bilinear);
    }
    
    void metaBlitBufferFlippedScaled(const Vec2i &sourceSize, int scaleIsSpecial, bool forceNearestNeighbor=false) {
        GLMeta::blitRectangle(IntRect(0, 0, sourceSize.x, sourceSize.y),
                              IntRect(scOffset.x, scSize.y+scOffset.y, scSize.x, -scSize.y),
                              !forceNearestNeighbor && GLMeta::smoothScalingMethod(scaleIsSpecial) == Bilinear);
    }
    
    void redrawScreen() {
        FrameProfile::Scope profile(FrameProfile::ScreenBlit);
        screen.composite();
#ifdef MKXPZ_VITA_DIAGNOSTICS
        s42iMark("REDRAW_SCREEN_COMPOSITE_RETURN");
        s42iMark("SCALE_STATE integerActive=%d stepApplicable=%d lastMile=%d scRes=%dx%d scSize=%dx%d scOffset=%d,%d win=%dx%d",
                 (int)integerScaleActive, (int)integerScaleStepApplicable(),
                 (int)integerLastMileScaling,
                 scRes.x, scRes.y, scSize.x, scSize.y,
                 scOffset.x, scOffset.y, winSize.x, winSize.y);
#endif
        
        // maybe unspaghetti this later
        if (integerScaleStepApplicable() && !integerLastMileScaling)
        {
#ifdef MKXPZ_VITA_DIAGNOSTICS
            s42iMark("I_FIRST_PASS_BEGIN");
#endif
            int scaleIsSpecial = GLMeta::blitScaleIsSpecial(integerScaleBuffer, false, IntRect(0, 0, scSize.x, scSize.y), screen.getPP().frontBuffer(), IntRect(0, 0, scRes.x, scRes.y));
#ifdef MKXPZ_VITA_DIAGNOSTICS
            s42iMark("I_SCALE_CALC_DONE special=%d", scaleIsSpecial);
#endif

            GLMeta::blitBeginScreen(winSize, scaleIsSpecial);
#ifdef MKXPZ_VITA_DIAGNOSTICS
            s42iMark("I_BLIT_BEGIN_SCREEN_DONE");
#endif
            GLMeta::blitSource(screen.getPP().frontBuffer(), scaleIsSpecial);
#ifdef MKXPZ_VITA_DIAGNOSTICS
            s42iMark("I_BLIT_SOURCE_DONE");
#endif
            
            FBO::clear();
#ifdef MKXPZ_VITA_DIAGNOSTICS
            s42iMark("I_DEFAULT_CLEAR_DONE");
#endif
            metaBlitBufferFlippedScaled(scRes, scaleIsSpecial, true);
#ifdef MKXPZ_VITA_DIAGNOSTICS
            s42iMark("I_BLIT_RECT_DONE");
#endif
            GLMeta::blitEnd();
#ifdef MKXPZ_VITA_DIAGNOSTICS
            s42iMark("I_BLIT_END_DONE");
            s42iMark("I_SWAPGLBUFFER_CALL_BEGIN");
#endif
            
            swapGLBuffer();
            vitaBootMarkFirstGamePresent();
#ifdef MKXPZ_VITA_DIAGNOSTICS
            s42iMark("I_SWAPGLBUFFER_CALL_DONE");
#endif
            if (frameCount >= 35 && frameCount <= 50)
                vitaDiagLog("S27", "f=%d BEFORE_AVGFPS", frameCount);
            updateAvgFPS();
            if (frameCount >= 35 && frameCount <= 50)
                vitaDiagLog("S27", "f=%d AFTER_AVGFPS", frameCount);
            return;
        }
        
        if (integerScaleStepApplicable())
        {
#ifdef MKXPZ_VITA_DIAGNOSTICS
            s42iMark("I_SECOND_PASS_BEGIN");
#endif
            int scaleIsSpecial = GLMeta::blitScaleIsSpecial(integerScaleBuffer, false, IntRect(0, 0, integerScaleBuffer.width, integerScaleBuffer.height), screen.getPP().frontBuffer(), IntRect(0, 0, scRes.x, scRes.y));
#ifdef MKXPZ_VITA_DIAGNOSTICS
            s42iMark("I_SECOND_SCALE_CALC_DONE special=%d", scaleIsSpecial);
#endif

            assert(integerScaleBuffer.tex != TEX::ID(0));
            GLMeta::blitBegin(integerScaleBuffer, false, scaleIsSpecial);
#ifdef MKXPZ_VITA_DIAGNOSTICS
            s42iMark("I_SECOND_BLIT_BEGIN_DONE");
#endif
            GLMeta::blitSource(screen.getPP().frontBuffer(), scaleIsSpecial);
#ifdef MKXPZ_VITA_DIAGNOSTICS
            s42iMark("I_SECOND_BLIT_SOURCE_DONE");
#endif
            
            GLMeta::blitRectangle(IntRect(0, 0, scRes.x, scRes.y),
                                  IntRect(0, 0, integerScaleBuffer.width, integerScaleBuffer.height),
                                  false);
#ifdef MKXPZ_VITA_DIAGNOSTICS
            s42iMark("I_SECOND_BLIT_RECT_DONE");
#endif
            
            GLMeta::blitEnd();
#ifdef MKXPZ_VITA_DIAGNOSTICS
            s42iMark("I_SECOND_BLIT_END_DONE");
#endif
        }
        

        Vec2i sourceSize;

        if (integerScaleActive)
        {
            sourceSize = Vec2i(integerScaleBuffer.width, integerScaleBuffer.height);
        }
        else
        {
            sourceSize = scRes;
        }

        int scaleIsSpecial = GLMeta::blitScaleIsSpecial(integerScaleBuffer, false, IntRect(0, 0, scSize.x, scSize.y), integerScaleActive ? integerScaleBuffer : screen.getPP().frontBuffer(), IntRect(0, 0, sourceSize.x, sourceSize.y));
#ifdef MKXPZ_VITA_DIAGNOSTICS
        s42iMark("FINAL_SCALE_CALC_DONE special=%d source=%dx%d", scaleIsSpecial, sourceSize.x, sourceSize.y);
        s42iMark("BLIT_BEGIN_SCREEN_BEGIN");
#endif

        GLMeta::blitBeginScreen(winSize, scaleIsSpecial);
#ifdef MKXPZ_VITA_DIAGNOSTICS
        s42iMark("BLIT_BEGIN_SCREEN_DONE");
        s42iMark("BLIT_SOURCE_BEGIN");
#endif
        //GLMeta::blitSource(screen.getPP().frontBuffer(), scaleIsSpecial);

        if (integerScaleActive)
        {
            GLMeta::blitSource(integerScaleBuffer, scaleIsSpecial);
        }
        else
        {
            GLMeta::blitSource(screen.getPP().frontBuffer(), scaleIsSpecial);
        }
#ifdef MKXPZ_VITA_DIAGNOSTICS
        s42iMark("BLIT_SOURCE_DONE");
#endif
        
        /* Session 31B: ONE-SHOT final-source probe at frame 120.
         * blitSource has JUST bound the real final source texture to
         * GL_TEXTURE_2D. Call glFinish() FIRST (forces scene_reset ->
         * sceGxmEndScene -> sceGxmFinish) so the previous ScreenScene
         * render target is definitely resolved, then scan its memory by
         * target (NOT texture id). No GL state changes, no readback. */
        #if 0 /* Historical hardware probe: never run in a playable build. */
        {
            static bool s31bDone = false;
            if (!s31bDone && frameCount == 120) {
                s31bDone = true;
                vitaDiagLog("S33", "FINAL_SOURCE_PROBE_BEGIN frame=%d", frameCount);
                
                /* Session 34 geometry probe: dump the final-blit scaling state
                 * to explain the y=272 half-screen cutoff. */
                vitaDiagLog("S34-GEO", "winSize=%dx%d scRes=%dx%d scSize=%dx%d scOffset=%dx%d integerScaleActive=%d integerLastMile=%d integerScaleStepApplicable=%d",
                            winSize.x, winSize.y, scRes.x, scRes.y,
                            scSize.x, scSize.y, scOffset.x, scOffset.y,
                            (int)integerScaleActive, (int)integerLastMileScaling,
                            (int)integerScaleStepApplicable());
                const IntRect &vp = glState.viewport.get();
                vitaDiagLog("S34-GEO", "viewport=%d,%d %dx%d scissorEnabled=%d",
                            vp.x, vp.y, vp.w, vp.h, (int)glState.scissorTest.get());
                const IntRect &sb = glState.scissorBox.get();
                vitaDiagLog("S34-GEO", "scissorBox=%d,%d %dx%d",
                            sb.x, sb.y, sb.w, sb.h);
                vitaDiagLog("S34-GEO", "destRect=%d,%d %dx%d (flipped)",
                            scOffset.x, scSize.y + scOffset.y, scSize.x, -scSize.y);
                glFinish();

                SceGxmTexture *gxmTex = vglGetGxmTexture(GL_TEXTURE_2D);
                if (gxmTex) {
                    unsigned int tw = sceGxmTextureGetWidth(gxmTex);
                    unsigned int th = sceGxmTextureGetHeight(gxmTex);
                    SceGxmTextureFormat tfmt = sceGxmTextureGetFormat(gxmTex);
                    void *tdata = sceGxmTextureGetData(gxmTex);
                    vitaDiagLog("S33", "FINAL_SOURCE_RESOLVED tex_ptr=%p data=%p width=%u height=%u format=0x%08x",
                                gxmTex, tdata, tw, th, (unsigned)tfmt);

                    /* 32-bit color: base format U8U8U8U8 (0x0C000000) */
                    bool is32 = ((unsigned)tfmt & 0xFF000000u) == 0x0C000000u;
                    bool sane = tdata && tw > 0 && th > 0 && tw <= 4096 && th <= 4096;
                    if (is32 && sane) {
                        const uint32_t *w = (const uint32_t *)tdata;
                        size_t totalWords = (size_t)tw * th;
                        if (totalWords >= 4) {
                            uint32_t first = w[0];
                            uint64_t same = 0, diff = 0, zero = 0;
                            uint32_t orAll = 0;
                            uint32_t hash = 2166136261u;
                            for (size_t i = 0; i < totalWords; ++i) {
                                uint32_t v = w[i];
                                orAll |= v;
                                hash ^= v;
                                hash *= 16777619u;
                                if (v == first) ++same;
                                else ++diff;
                                if (v == 0) ++zero;
                            }
                            size_t q1 = totalWords / 4, q3 = (totalWords * 3) / 4;
                            vitaDiagLog("S33", "FINAL_SOURCE_RESOLVED first=%08x total=%u same=%u different=%u zero=%u or=%08x hash=%08x",
                                        first, (unsigned)totalWords, (unsigned)same,
                                        (unsigned)diff, (unsigned)zero, orAll, hash);
                            vitaDiagLog("S33", "FINAL_SOURCE_RESOLVED words first=%08x quarter=%08x center=%08x threeq=%08x last=%08x",
                                        w[0], w[q1], w[totalWords/2], w[q3], w[totalWords-1]);
                        } else {
                            vitaDiagLog("S33", "FINAL_SOURCE_RESOLVED totalWords too small: %u", (unsigned)totalWords);
                        }
                    } else {
                        vitaDiagLog("S33", "FINAL_SOURCE_RESOLVED scan skipped is32=%d sane=%d", (int)is32, (int)sane);
                    }
                } else {
                    vitaDiagLog("S33", "FINAL_SOURCE_RESOLVED vglGetGxmTexture(GL_TEXTURE_2D)=NULL");
                }
            }
        }
        #endif
        
#ifdef MKXPZ_VITA_DIAGNOSTICS
        s42iMark("DEFAULT_CLEAR_BEGIN");
#endif
        FBO::clear();
#ifdef MKXPZ_VITA_DIAGNOSTICS
        s42iMark("DEFAULT_CLEAR_DONE");
        s42iMark("BLIT_RECT_BEGIN");
#endif
        metaBlitBufferFlippedScaled(sourceSize, scaleIsSpecial);
#ifdef MKXPZ_VITA_DIAGNOSTICS
        s42iMark("BLIT_RECT_DONE");
        s42iMark("BLIT_END_BEGIN");
#endif
        
        GLMeta::blitEnd();
#ifdef MKXPZ_VITA_DIAGNOSTICS
        s42iMark("BLIT_END_DONE");
#endif

        /* Session 31C: ONE-SHOT resolved display-backbuffer probe at frame 120.
         * glFinish() forces the final blit's scene to end/resolve, THEN scan
         * the display backbuffer. Renamed from S28 (which used raw
         * sceGxmFinish — not guaranteed to resolve the active scene). */
        #if 0 /* Historical hardware probe: never run in a playable build. */
        {
            static bool s31cPreDone = false;
            if (!s31cPreDone && frameCount == 120) {
                s31cPreDone = true;
                vitaDiagLog("S33", "DISPLAY_PROBE_BEGIN frame=%d front_idx=%u back_idx=%u",
                            frameCount, gxm_front_buffer_index, gxm_back_buffer_index);
                vitaDiagLog("S33", "front_addr=%p back_addr=%p",
                            gxm_color_surfaces_addr[gxm_front_buffer_index],
                            gxm_color_surfaces_addr[gxm_back_buffer_index]);
                glFinish();
                const unsigned char *bb =
                    (const unsigned char *)gxm_color_surfaces_addr[gxm_back_buffer_index];
                if (bb) {
                    unsigned black = 0, nonblack = 0;
                    int fnx = -1, fny = -1;
                    unsigned char fa = 0, fb = 0, fg = 0, fr = 0;
                    for (int y = 0; y < 544; ++y) {
                        const unsigned char *row = bb + (size_t)y * 960 * 4;
                        for (int x = 0; x < 960; ++x) {
                            const unsigned char *p = row + x * 4; /* byte0=R byte1=G byte2=B byte3=A (A8B8G8R8 LE) */
                            const uint32_t word = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                                                  ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
                            if ((word & 0x00FFFFFFu) != 0) { /* ignore alpha — S34 decoder correction */
                                ++nonblack;
                                if (fnx < 0) {
                                    fnx = x; fny = y;
                                    fa = p[3]; fb = p[2]; fg = p[1]; fr = p[0]; /* A B G R labeled */
                                }
                            } else {
                                ++black;
                            }
                        }
                    }
                    vitaDiagLog("S33", "DISPLAY_RESOLVED back_idx=%u black_pixels=%u nonblack_pixels=%u",
                                gxm_back_buffer_index, black, nonblack);
                    if (fnx >= 0)
                        vitaDiagLog("S33", "DISPLAY_RESOLVED first_nonblack x=%d y=%d A=%02x B=%02x G=%02x R=%02x",
                                    fnx, fny, fa, fb, fg, fr);
                    else
                        vitaDiagLog("S33", "DISPLAY_RESOLVED first_nonblack NONE (all black)");
                } else {
                    vitaDiagLog("S33", "DISPLAY_RESOLVED back_addr NULL");
                }
            }
        }
        #endif

#ifdef MKXPZ_VITA_DIAGNOSTICS
        s42iMark("SWAPGLBUFFER_CALL_BEGIN");
#endif
        swapGLBuffer();
            vitaBootMarkFirstGamePresent();
#ifdef MKXPZ_VITA_DIAGNOSTICS
        s42iMark("SWAPGLBUFFER_CALL_DONE");
#endif

        #if 0 /* Historical title telemetry and framebuffer captures. */
        /* S41: TITLE_READY marker — emitted once after the real title has
         * completed at least one normal post-Main render/swap. Hermes polls
         * the diag log for this instead of screenshotting on a fixed delay. */
        {
            static bool s41TitleReady = false;
            static int s41TitleFrames = 0;
            if (!s41TitleReady) {
                ++s41TitleFrames;
                if (s41TitleFrames >= 250) {
                    s41TitleReady = true;
                    vitaDiagLog("S41", "TITLE_READY frames=%d", s41TitleFrames);
                }
            }
        }

        /* S36: accumulate + one-shot summary at frame 300. */
        if (!s36Done) {
            s36T3 = sceKernelGetSystemTimeWide();
            s36SceneUs += s36T1 - s36T0;
            s36BlitUs += s36T2 - s36T1;
            s36SwapUs += s36T3 - s36T2;
            s36FrameUs += s36T3 - s36T0;
            ++s36Frames;
            if (s36Frames >= 300) {
                s36Done = true;
                vitaDiagLog("S36-PERF", "frames=%d total_ms=%lld fps=%d scene_ms=%lld blit_ms=%lld swap_ms=%lld",
                            s36Frames, (long long)(s36FrameUs / 1000),
                            (int)(s36Frames * 1000000 / s36FrameUs),
                            (long long)(s36SceneUs / 1000),
                            (long long)(s36BlitUs / 1000),
                            (long long)(s36SwapUs / 1000));
            }
        }

        /* Session 31 post-swap half: wait one vblank, scan the NEW front
         * buffer, done. Exactly once, immediately after the frame-120
         * swap (frameCount is now 121). */
        {
            static bool s31PostDone = false;
            if (!s31PostDone && frameCount == 121) {
                s31PostDone = true;
                vitaDiagLog("S33", "AFTER_NORMAL_SWAP front_idx=%u back_idx=%u",
                            gxm_front_buffer_index, gxm_back_buffer_index);
                /* Wait ONE vblank after the swap so the display queue has time to
         * present. Use the Multi variant (the single sceDisplayWaitVblankStart
         * stub broke the SELF import table — S28 loader hang, zero logs). */
        sceDisplayWaitVblankStartMulti(1);
                const unsigned char *fbuf =
                    (const unsigned char *)gxm_color_surfaces_addr[gxm_front_buffer_index];
                if (fbuf) {
                    unsigned black = 0, nonblack = 0;
                    int fnx = -1, fny = -1;
                    unsigned char fa = 0, fb = 0, fg = 0, fr = 0;
                    for (int y = 0; y < 544; ++y) {
                        const unsigned char *row = fbuf + (size_t)y * 960 * 4;
                        for (int x = 0; x < 960; ++x) {
                            const unsigned char *p = row + x * 4; /* byte0=R byte1=G byte2=B byte3=A */
                            const uint32_t word = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                                                  ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
                            if ((word & 0x00FFFFFFu) != 0) { /* ignore alpha — S34 decoder correction */
                                ++nonblack;
                                if (fnx < 0) {
                                    fnx = x; fny = y;
                                    fa = p[3]; fb = p[2]; fg = p[1]; fr = p[0]; /* A B G R labeled */
                                }
                            } else {
                                ++black;
                            }
                        }
                    }
                    vitaDiagLog("S33", "FRONT_POST_SWAP front_idx=%u black_pixels=%u nonblack_pixels=%u",
                                gxm_front_buffer_index, black, nonblack);
                    if (fnx >= 0)
                        vitaDiagLog("S33", "FRONT_POST_SWAP first_nonblack x=%d y=%d A=%02x B=%02x G=%02x R=%02x",
                                    fnx, fny, fa, fb, fg, fr);
                    else
                        vitaDiagLog("S33", "FRONT_POST_SWAP first_nonblack NONE (all black)");
                } else {
                    vitaDiagLog("S33", "FRONT_POST_SWAP front_addr NULL");
                }
                vitaDiagLog("S33", "probe_done");
            }
        }
        
        /* S41: engine-side BMP capture of the REAL front buffer.
         * Fires at TITLE_READY frame, +280 (+5s), +560 (+10s) to track
         * text degradation. A8B8G8R8 surface: remap B=src[1] G=src[2] R=src[3].
         */
        {
            static bool s41CapDone[3] = {false, false, false};
            static const int s41CapFrames[3] = {250, 530, 810};
            for (int ci = 0; ci < 3; ++ci) {
                if (!s41CapDone[ci] && frameCount == s41CapFrames[ci]) {
                    s41CapDone[ci] = true;
                    const unsigned char *fbuf =
                        (const unsigned char *)gxm_color_surfaces_addr[gxm_front_buffer_index];
                    if (fbuf) {
                        char path[128];
                        snprintf(path, sizeof(path),
                                 "ux0:/data/hardrpg/title-cap-%d.bmp", ci);
                        FILE *f = fopen(path, "wb");
                        if (f) {
                            int w = 960, h = 544, stride = w * 4;
                            unsigned char hdr[54] = {0};
                            hdr[0] = 'B'; hdr[1] = 'M';
                            uint32_t dataSize = (uint32_t)(stride * h);
                            uint32_t fileSize = 54 + dataSize;
                            hdr[2] = (unsigned char)(fileSize & 0xFF);
                            hdr[3] = (unsigned char)((fileSize >> 8) & 0xFF);
                            hdr[4] = (unsigned char)((fileSize >> 16) & 0xFF);
                            hdr[5] = (unsigned char)((fileSize >> 24) & 0xFF);
                            hdr[10] = 54;
                            hdr[14] = 40;
                            hdr[18] = (unsigned char)(w & 0xFF);
                            hdr[19] = (unsigned char)((w >> 8) & 0xFF);
                            hdr[22] = (unsigned char)(h & 0xFF);
                            hdr[23] = (unsigned char)((h >> 8) & 0xFF);
                            hdr[26] = 1;
                            hdr[28] = 32;
                            hdr[34] = (unsigned char)(dataSize & 0xFF);
                            hdr[35] = (unsigned char)((dataSize >> 8) & 0xFF);
                            hdr[36] = (unsigned char)((dataSize >> 16) & 0xFF);
                            hdr[37] = (unsigned char)((dataSize >> 24) & 0xFF);
                            fwrite(hdr, 1, 54, f);
                            /* BMP is bottom-up: write rows in reverse. */
                            for (int y = h - 1; y >= 0; --y) {
                                const unsigned char *row = fbuf + (size_t)y * stride;
                                static unsigned char rowb[960 * 4];
                                for (int x = 0; x < w; ++x) {
                                    const unsigned char *p = row + x * 4;
                                    /* S34-corrected: surface is RGBA byte0=R. */
                                    rowb[x * 4 + 0] = p[2]; /* B */
                                    rowb[x * 4 + 1] = p[1]; /* G */
                                    rowb[x * 4 + 2] = p[0]; /* R */
                                    rowb[x * 4 + 3] = 0xFF;
                                }
                                fwrite(rowb, 1, stride, f);
                            }
                            fclose(f);
                            vitaDiagLog("S41", "CAP ci=%d frame=%d written=%s", ci,
                                        frameCount, path);
                        } else {
                            vitaDiagLog("S41", "CAP ci=%d fopen FAILED", ci);
                        }
                    } else {
                        vitaDiagLog("S41", "CAP ci=%d front_addr NULL", ci);
                    }
                }
            }
        }

        #endif

        if (frameCount >= 35 && frameCount <= 50)
            vitaDiagLog("S27", "f=%d BEFORE_AVGFPS", frameCount);
#ifdef MKXPZ_VITA_DIAGNOSTICS
        s42iMark("AVGFPS_BEGIN");
#endif
        updateAvgFPS();
#ifdef MKXPZ_VITA_DIAGNOSTICS
        s42iMark("AVGFPS_DONE");
#endif
        if (frameCount >= 35 && frameCount <= 50)
            vitaDiagLog("S27", "f=%d AFTER_AVGFPS", frameCount);
#ifdef MKXPZ_VITA_DIAGNOSTICS
        s42iMark("REDRAW_SCREEN_DONE");
#endif
    }
    
    void checkSyncLock() {
        if (!threadData->syncPoint.mainSyncLocked())
            return;
        
        /* Releasing the GL context before sleeping and making it
         * current again on wakeup seems to avoid the context loss
         * when the app moves into the background on Android */
        SDL_GL_MakeCurrent(threadData->window, 0);
        threadData->syncPoint.waitMainSync();
        SDL_GL_MakeCurrent(threadData->window, glCtx);
        
        fpsLimiter.resetFrameAdjust();
    }
    
    double averageFPS() {
        double ret = 0;
        SDL_LockMutex(avgFPSLock);
        for (double times : avgFPSData)
            ret += times;
        
        ret = 1 / (ret / avgFPSData.size());
        SDL_UnlockMutex(avgFPSLock);
        return ret;
    }
    
    void setLock(bool force = false) {
        if (!(force || multithreadedMode)) return;

        int lockResult = SDL_LockMutex(glResourceLock);
        SDL_GL_MakeCurrent(threadData->window, threadData->glContext);
    }
    
    void releaseLock(bool force = false) {
        if (!(force || multithreadedMode)) return;

        int unlockResult = SDL_UnlockMutex(glResourceLock);
    }

    void updateAvgFPS() {
        SDL_LockMutex(avgFPSLock);
        if (avgFPSData.size() > 40)
            avgFPSData.erase(avgFPSData.begin());
        
        double time = shState->runTime();
        avgFPSData.push_back(time - last_avg_update);
        last_avg_update = time;
        SDL_UnlockMutex(avgFPSLock);
    }
};

Graphics::Graphics(RGSSThreadData *data) {
    p = new GraphicsPrivate(data);
#ifdef MKXPZ_VITA
    /* Vita: the GXM driver has a bounded live render-target count. The
     * texture pool caches TEXFBOs (and their render targets) indefinitely,
     * so long RGSS initializations without frame swaps exhaust the driver
     * pool. Destroy released FBOs immediately instead of caching them. */
    shState->texPool().disable();
#endif
    if (data->config.syncToRefreshrate) {
        p->frameRate = data->refreshRate;
        p->fpsLimiter.disabled = true;
    } else if (data->config.fixedFramerate > 0) {
        p->fpsLimiter.setDesiredFPS(data->config.fixedFramerate);
    } else if (data->config.fixedFramerate < 0) {
        p->fpsLimiter.disabled = true;
    }
}

Graphics::~Graphics() { delete p; }

double Graphics::getDelta() {
    return shState->runTime() - p->last_update;
}

double Graphics::lastUpdate() {
    return p->last_update;
}

void Graphics::update(bool checkForShutdown) {
    FrameProfile::Scope profile(FrameProfile::GraphicsOther);
    /* Session 27: heavy per-frame logging removed (only gated S27 tail
     * markers remain in swapGLBuffer, frames 35-50). */
    p->threadData->rqWindowAdjust.wait();
    p->last_update = shState->runTime();
    if (p->threadData->rqFrameReset) {
        p->threadData->rqFrameReset.clear();
        p->fpsLimiter.resetFrameAdjust();
    }
    
    // update Input.repeat timing, rounding the framerate to the nearest 2
    {
        static const double mult = 2.0;
        double afr = std::abs(averageFrameRate()); // abs shouldn't be necessary but that's ok
        afr += mult / 2;
        afr -= std::fmod(afr, mult);
        shState->input().recalcRepeat(std::floor(afr));
    }
    
    if (checkForShutdown)
        p->checkShutDownReset();
    
    p->checkSyncLock();
    
    
#ifdef MKXPZ_STEAM
    if (STEAMSHIM_alive())
        STEAMSHIM_pump();
#endif
    
    if (p->frozen) {
        return;
    }
    
    if (p->fpsLimiter.frameSkipRequired()) {
        if (p->useFrameSkip) {
            /* Skip frame */
            { FrameProfile::Scope pacing(FrameProfile::Pacing); p->fpsLimiter.delay(); }
            ++p->frameCount;
            p->threadData->ethread->notifyFrame();
            
            return;
        } else {
            /* Just reset frame adjust counter */
            p->fpsLimiter.resetFrameAdjust();
        }
    }
    
    p->checkResize();
    p->redrawScreen();
#ifdef MKXPZ_VITA_DIAGNOSTICS
    if (s42iTailActive)
    {
        s42iMark("GRAPHICS_UPDATE_DONE frame=%d", p->frameCount);
        s42lBindingTracePending = true;
        s42iTailActive = false;
        s42iTailConsumed = true;
    }
#endif
    if (p->frameCount >= 35 && p->frameCount <= 50)
        vitaDiagLog("S27", "f=%d UPDATE_RETURN", p->frameCount);
}

void Graphics::freeze() {
    p->frozen = true;
    
    p->checkShutDownReset();
    p->checkResize();
    
    /* Capture scene into frozen buffer */
    p->compositeToBuffer(p->frozenScene);
}

void Graphics::transition(int duration, const char *filename, int vague) {
    p->checkSyncLock();
    
    if (!p->frozen)
        return;
    
    vague = clamp(vague, 1, 256);
    Bitmap *transMap = *filename ? new Bitmap(filename) : 0;
    
    setBrightness(255);
    
    /* Capture new scene */
    p->screen.composite();
    
    /* The PP frontbuffer will hold the current scene after the
     * composition step. Since the backbuffer is unused during
     * the transition, we can reuse it as the target buffer for
     * the final rendered image. */
    TEXFBO &currentScene = p->screen.getPP().frontBuffer();
    TEXFBO &transBuffer = p->screen.getPP().backBuffer();
    
    /* If no transition bitmap is provided,
     * we can use a simplified shader */
    TransShader &transShader = shState->shaders().trans;
    SimpleTransShader &simpleShader = shState->shaders().simpleTrans;
    
    // Handle high-res.
    Vec2i transSize(p->scResLores.x, p->scResLores.y);

    if (transMap) {
        TransShader &shader = transShader;
        shader.bind();
        shader.applyViewportProj();
        shader.setFrozenScene(p->frozenScene.tex);
        shader.setCurrentScene(currentScene.tex);
        if (transMap->hasHires()) {
            Debug() << "BUG: High-res Graphics transMap not implemented";
        }
        shader.setTransMap(transMap->getGLTypes().tex);
        shader.setVague(vague / 256.0f);
        shader.setTexSize(transSize);
    } else {
        SimpleTransShader &shader = simpleShader;
        shader.bind();
        shader.applyViewportProj();
        shader.setFrozenScene(p->frozenScene.tex);
        shader.setCurrentScene(currentScene.tex);
        shader.setTexSize(transSize);
    }
    
    glState.blend.pushSet(false);
    
    #if 0 /* Historical transition probe: raw sceGxmFinish during an open
           * scene can fault the physical Vita GPU. */
    /* Diagnostic instrumentation (MKXPZ_VITA_DIAGNOSTICS build):
     * - skip-transition trigger: bypass the SimpleTransShader pass entirely,
     *   composite normally and present (A/B test for the transition shader).
     * - Transition GPU-sync probe: sceGxmFinish after the first transition
     *   quad draw isolates async GPU faults from CPU-side death. */
    const bool skipTransition = access("ux0:/data/hardrpg/skip-transition", F_OK) == 0;
    bool transSyncDone = false;
    if (skipTransition) {
        vitaDiagLog("TRANS", "bypass active: skipping SimpleTransShader pass");
        glState.blend.pop();
        delete transMap;
        vitaDiagLog("TRANS", "before_unfreeze frozen=%d", (int)p->frozen);
        p->frozen = false;
        vitaDiagLog("TRANS", "after_unfreeze frozen=%d", (int)p->frozen);
        p->swapGLBuffer();
            vitaBootMarkFirstGamePresent();
        return;
    }
    #endif
    
    for (int i = 0; i < duration; ++i) {
        /* We need to clean up transMap properly before
         * a possible longjmp, so we manually test for
         * shutdown/reset here */
        if (p->threadData->rqTerm) {
            glState.blend.pop();
            delete transMap;
            p->shutdown();
            return;
        }
        
        if (p->threadData->rqReset) {
            glState.blend.pop();
            delete transMap;
            scriptBinding->reset();
            return;
        }
        
        p->checkSyncLock();
        
        const float prog = i * (1.0f / duration);
        
        if (transMap) {
            transShader.bind();
            transShader.setProg(prog);
        } else {
            simpleShader.bind();
            simpleShader.setProg(prog);
        }
        
        /* Draw the composed frame to a buffer first
         * (we need this because we're skipping PingPong) */
        FBO::bind(transBuffer.fbo);
        FBO::clear();
        p->screenQuad.draw();
        
        p->checkResize();
        
        /* Then blit it flipped and scaled to the screen */
        FBO::unbind();
        FBO::clear();
        
        int scaleIsSpecial = GLMeta::blitScaleIsSpecial(p->integerScaleBuffer, false, IntRect(0, 0, p->scSize.x, p->scSize.y), transBuffer, IntRect(0, 0, p->scRes.x, p->scRes.y));

        GLMeta::blitBeginScreen(Vec2i(p->winSize), scaleIsSpecial);
        GLMeta::blitSource(transBuffer, scaleIsSpecial);
        p->metaBlitBufferFlippedScaled(scaleIsSpecial);
        GLMeta::blitEnd();
        
        p->swapGLBuffer();
            vitaBootMarkFirstGamePresent();
        /* Call this manually, as redrawScreen() is not called during this loop. */
        p->updateAvgFPS();
    }
    
    glState.blend.pop();
    
    delete transMap;
    
    p->frozen = false;
}

void Graphics::frameReset() {p->fpsLimiter.resetFrameAdjust();}

static void guardDisposed() {}

DEF_ATTR_RD_SIMPLE(Graphics, FrameRate, int, p->frameRate)

DEF_ATTR_SIMPLE(Graphics, FrameCount, int, p->frameCount)

void Graphics::setFrameRate(int value) {
    p->frameRate = std::max(value, 1);
    
    if (p->threadData->config.syncToRefreshrate)
        return;
    
    if (p->threadData->config.fixedFramerate > 0)
        return;
    
    p->fpsLimiter.setDesiredFPS(p->frameRate);
    //shState->input().recalcRepeat((unsigned int)p->frameRate);
}

double Graphics::averageFrameRate() {
    return p->averageFPS();
}

void Graphics::wait(int duration) {
    for (int i = 0; i < duration; ++i) {
        p->checkShutDownReset();
        p->redrawScreen();
    }
}

void Graphics::fadeout(int duration) {
    FBO::unbind();
    
    float curr = p->brightness;
    float diff = 255.0f - curr;
    
    for (int i = duration - 1; i > -1; --i) {
        setBrightness(diff + (curr / duration) * i);
        
        if (p->frozen) {
            int scaleIsSpecial = GLMeta::blitScaleIsSpecial(p->integerScaleBuffer, false, IntRect(0, 0, p->scSize.x, p->scSize.y), p->frozenScene, IntRect(0, 0, p->scRes.x, p->scRes.y));

            GLMeta::blitBeginScreen(p->scSize, scaleIsSpecial);
            GLMeta::blitSource(p->frozenScene, scaleIsSpecial);
            
            FBO::clear();
            p->metaBlitBufferFlippedScaled(scaleIsSpecial);
            
            GLMeta::blitEnd();
            
            p->swapGLBuffer();
            vitaBootMarkFirstGamePresent();
        } else {
            update();
        }
    }
}

void Graphics::fadein(int duration) {
    FBO::unbind();
    
    float curr = p->brightness;
    float diff = 255.0f - curr;
    
    for (int i = 1; i <= duration; ++i) {
        setBrightness(curr + (diff / duration) * i);
        
        if (p->frozen) {
            int scaleIsSpecial = GLMeta::blitScaleIsSpecial(p->integerScaleBuffer, false, IntRect(0, 0, p->scSize.x, p->scSize.y), p->frozenScene, IntRect(0, 0, p->scRes.x, p->scRes.y));

            GLMeta::blitBeginScreen(p->scSize, scaleIsSpecial);
            GLMeta::blitSource(p->frozenScene, scaleIsSpecial);
            
            FBO::clear();
            p->metaBlitBufferFlippedScaled(scaleIsSpecial);
            
            GLMeta::blitEnd();
            
            p->swapGLBuffer();
            vitaBootMarkFirstGamePresent();
        } else {
            update();
        }
    }
}

Bitmap *Graphics::snapToBitmap() {
    p->screen.composite();

    if (shState->config().enableHires) {
        // TODO: Maybe don't reconstruct this struct every time?
        TEXFBO tf;
        tf.width = width();
        tf.height = height();
        tf.selfHires = &p->screen.getPP().frontBuffer();

        return new Bitmap(tf);
    }

    return new Bitmap(p->screen.getPP().frontBuffer());
}

int Graphics::width() const { return p->scResLores.x; }

int Graphics::height() const { return p->scResLores.y; }

int Graphics::widthHires() const { return p->scRes.x; }

int Graphics::heightHires() const { return p->scRes.y; }

bool Graphics::isPingPongFramebufferActive() const {
    return p->screen.getPP().frontBuffer().fbo == FBO::boundFramebufferID || p->screen.getPP().backBuffer().fbo == FBO::boundFramebufferID;
}

int Graphics::displayContentWidth() const {
    return p->scSize.x;
}

int Graphics::displayContentHeight() const {
    return p->scSize.y;
}

int Graphics::displayWidth() const {
    SDL_DisplayMode dm{};
    SDL_GetCurrentDisplayMode(SDL_GetWindowDisplayIndex(shState->sdlWindow()), &dm);
    return dm.w / p->backingScaleFactor;
}

int Graphics::displayHeight() const {
    SDL_DisplayMode dm{};
    SDL_GetCurrentDisplayMode(SDL_GetWindowDisplayIndex(shState->sdlWindow()), &dm);
    return dm.h / p->backingScaleFactor;
}

void Graphics::resizeScreen(int width, int height) {
    p->threadData->rqWindowAdjust.wait();
    p->checkResize(true);
    
    Vec2i sizeLores(width, height);

    if (shState->config().enableHires) {
        double framebufferScalingFactor = shState->config().framebufferScalingFactor;
        width = (int)lround(framebufferScalingFactor * width);
        height = (int)lround(framebufferScalingFactor * height);
    }

    Vec2i size(width, height);
    
    if (p->scRes == size && p->scResLores == sizeLores)
        return;
    
    p->scRes = size;
    p->scResLores = sizeLores;
    
    p->screen.setResolution(width, height);
    
    if (p->integerScaleActive)
        p->rebuildIntegerScaleBuffer();

    /* Vita: realloc without fini leaks the previous frozen frame in the
     * GPU pools (same class of bug as PingPong::resize above). */
    TEXFBO::fini(p->frozenScene);
    TEXFBO::init(p->frozenScene);
    TEXFBO::allocEmpty(p->frozenScene, width, height);
    TEXFBO::linkFBO(p->frozenScene);
    
    FloatRect screenRect(0, 0, width, height);
    p->screenQuad.setTexPosRect(screenRect, screenRect);
    
    glState.scissorBox.set(IntRect(0, 0, p->scRes.x, p->scRes.y));
    
    shState->eThread().requestWindowResize(width, height);
}

void Graphics::resizeWindow(int width, int height, bool center) {
    p->threadData->rqWindowAdjust.wait();
    p->checkResize();
    
    if (width == p->winSize.x / p->backingScaleFactor &&
        height == p->winSize.y / p->backingScaleFactor)
            return;

    shState->eThread().requestWindowResize(width, height);
    
    if (center)
        this->center();
}

bool Graphics::updateMovieInput(Movie *movie) {
    return  p->threadData->rqTerm || p->threadData->rqReset;
}

void Graphics::playMovie(const char *filename, int volume_, bool skippable) {
    if (shState->config().enableHires) {
        Debug() << "BUG: High-res Graphics playMovie not implemented";
    }

    Movie *movie = new Movie(skippable);
    MovieOpenHandler handler(movie->srcOps);
    shState->fileSystem().openRead(handler, filename);
    float volume = volume_ * 0.01f;
    
    if (movie->preparePlayback()) {        
        Sprite movieSprite;
        
        // Currently this stretches to fit the screen. VX Ace behavior is to center it and let the edges run off
        movieSprite.setBitmap(movie->videoBitmap);
        double ratio = std::min((double)width() / movie->video->width, (double)height() / movie->video->height);
        movieSprite.setZoomX(ratio);
        movieSprite.setZoomY(ratio);
        movieSprite.setX((width() / 2) - (movie->video->width * ratio / 2));
        movieSprite.setY((height() / 2) - (movie->video->height * ratio / 2));
        
        Sprite letterboxSprite;
        Bitmap letterbox(width(), height());
        letterbox.fillRect(0, 0, width(), height(), Vec4(0,0,0,255));
        letterboxSprite.setBitmap(&letterbox);
        
        letterboxSprite.setZ(4999);
        movieSprite.setZ(5001);
        
        movie->play(volume);
    }
    
    delete movie;
}

void Graphics::screenshot(const char *filename) {
    p->threadData->rqWindowAdjust.wait();
    Bitmap *ss = snapToBitmap();
    ss->saveToFile(filename);
    ss->dispose();
    delete ss;
}

DEF_ATTR_RD_SIMPLE(Graphics, Brightness, int, p->brightness)

void Graphics::setBrightness(int value) {
    value = clamp(value, 0, 255);
    
    if (p->brightness == value)
        return;
    
    p->brightness = value;
    p->screen.setBrightness(value / 255.0);
}

void Graphics::reset() {
    /* Dispose all live Disposables */
    IntruListLink<Disposable> *iter;
    
    for (iter = p->dispList.begin(); iter != p->dispList.end();
         iter = iter->next) {
        iter->data->dispose();
    }
    
    p->dispList.clear();
    
    /* Reset attributes (frame count not included) */
    p->fpsLimiter.resetFrameAdjust();
    p->frozen = false;
    p->screen.getPP().clearBuffers();
    
    setFrameRate(DEF_FRAMERATE);
    setBrightness(255);
    
    // Always update at least once to clear the screen
    if (p->threadData->rqResetFinish)
        update();
    else
        repaintWait(p->threadData->rqResetFinish, false);
    p->threadData->rqReset.clear();
}

void Graphics::center() {
    p->threadData->rqWindowAdjust.wait();
    if (getFullscreen())
        return;
    
    p->threadData->ethread->requestWindowCenter();
}

bool Graphics::getFullscreen() const {
    return p->threadData->ethread->getFullscreen();
}

void Graphics::setFullscreen(bool value) {
    p->threadData->ethread->requestFullscreenMode(value);
}

bool Graphics::getShowCursor() const {
    return p->threadData->ethread->getShowCursor();
}

void Graphics::setShowCursor(bool value) {
    p->threadData->ethread->requestShowCursor(value);
}

bool Graphics::getFixedAspectRatio() const
{
    // It's a bit hacky to expose config values as a Graphics
    // attribute, but there's really no point in state duplication
    return shState->config().fixedAspectRatio;
}

void Graphics::setFixedAspectRatio(bool value)
{
    shState->config().fixedAspectRatio = value;
    p->recalculateScreenSize(p->threadData->config.fixedAspectRatio);
    p->findHighestIntegerScale();
    p->recalculateScreenSize(p->threadData->config.fixedAspectRatio);
    p->updateScreenResoRatio(p->threadData);
}

int Graphics::getSmoothScaling() const
{
    // Same deal as with fixed aspect ratio
    return shState->config().smoothScaling;
}

void Graphics::setSmoothScaling(int value)
{
    shState->config().smoothScaling = value;
}

bool Graphics::getIntegerScaling() const
{
    return p->integerScaleActive;
}

void Graphics::setIntegerScaling(bool value)
{
    p->integerScaleActive = value;
    p->findHighestIntegerScale();
    p->rebuildIntegerScaleBuffer();
    
    p->recalculateScreenSize(p->threadData->config.fixedAspectRatio);
    p->updateScreenResoRatio(p->threadData);
}

bool Graphics::getLastMileScaling() const
{
    return p->integerLastMileScaling;
}

void Graphics::setLastMileScaling(bool value)
{
    p->integerLastMileScaling = value;
    p->recalculateScreenSize(p->threadData->config.fixedAspectRatio);
    p->updateScreenResoRatio(p->threadData);
}

bool Graphics::getThreadsafe() const
{
    return p->multithreadedMode;
}

void Graphics::setThreadsafe(bool value)
{
    p->multithreadedMode = value;
}

double Graphics::getScale() const {
    p->checkResize();
    return (double)(p->winSize.y / p->backingScaleFactor) / p->scRes.y;
    
}

void Graphics::setScale(double factor) {
    p->threadData->rqWindowAdjust.wait();
    factor = clamp(factor, 0.5, 4.0);
    
    if (factor == getScale())
        return;
    
    int widthpx = p->scRes.x * factor;
    int heightpx = p->scRes.y * factor;
    
    shState->eThread().requestWindowResize(widthpx, heightpx);
}

bool Graphics::getFrameskip() const { return p->useFrameSkip; }

void Graphics::setFrameskip(bool value) { p->useFrameSkip = value; }

Scene *Graphics::getScreen() const { return &p->screen; }

void Graphics::repaintWait(const AtomicFlag &exitCond, bool checkReset) {
    if (exitCond)
        return;
    
    /* Repaint the screen with the last good frame we drew */
    TEXFBO &lastFrame = p->screen.getPP().frontBuffer();

    int scaleIsSpecial = GLMeta::blitScaleIsSpecial(p->integerScaleBuffer, false, IntRect(0, 0, p->scSize.x, p->scSize.y), lastFrame, IntRect(0, 0, p->scRes.x, p->scRes.y));

    GLMeta::blitBeginScreen(p->winSize, scaleIsSpecial);
    GLMeta::blitSource(lastFrame, scaleIsSpecial);
    
    while (!exitCond) {
        shState->checkShutdown();
        
        if (checkReset)
            shState->checkReset();
        
        FBO::clear();
        p->metaBlitBufferFlippedScaled(scaleIsSpecial);
        { FrameProfile::Scope present(FrameProfile::Present); SDL_GL_SwapWindow(p->threadData->window); }
        FrameProfile::boundary();
        { FrameProfile::Scope pacing(FrameProfile::Pacing); p->fpsLimiter.delay(); }
        
        p->threadData->ethread->notifyFrame();
    }
    
    GLMeta::blitEnd();
}

void Graphics::lock(bool force) {
    p->setLock(force);
}

void Graphics::unlock(bool force) {
    p->releaseLock(force);
}

void Graphics::addDisposable(Disposable *d) { p->dispList.append(d->link); }

void Graphics::remDisposable(Disposable *d) { p->dispList.remove(d->link); }

#undef GRAPHICS_THREAD_LOCK
