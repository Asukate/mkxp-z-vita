/* HardRPG - minimal OpenAL implementation for mkxp-z on Vita
 * Copyright (C) 2026 Asukate
 *
 * This file is part of HardRPG, free software under the GNU
 * General Public License v3. See LICENSES/GPL-3.0.txt at the repository root. */

/* Minimal real OpenAL implementation for mkxp-z on Vita.
 * Replaces the no-op stub: alBufferData stores PCM; alSourceQueueBuffers /
 * alSourcePlay push the audio to SDL_QueueAudio (the vita-sdl2 shim), which
 * outputs to the SceAudio port. Sources are mixed as 48 kHz stereo S16.
 */
#include <al.h>
#include <alc.h>
#include <SDL.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <pthread.h>
#include <vorbis/vorbisfile.h>

#define DR_MP3_NO_STDIO
#define DR_MP3_IMPLEMENTATION
#include "dr_libs/dr_mp3.h"  /* deps/dr_libs vendored snapshot (MIT-0); add deps/ to include path */

#define MAX_BUFFERS 64
#define MAX_SOURCES 32
#define OUT_FREQ 48000
#define OUT_FRAMES 512

/* ---- crash-region logger (opens on first use; never crashes the app) ---- */
static int audio_trace_enabled = -1;

static int audio_trace_wanted(void) {
    if (audio_trace_enabled < 0)
        audio_trace_enabled =
            (access("ux0:/data/hardrpg/enable-audio-trace", F_OK) == 0);
    return audio_trace_enabled;
}

static void al_trace(const char *fmt, ...) {
    if (!audio_trace_wanted())
        return;
    FILE *lf = fopen("ux0:/data/openal-impl.log", "a");
    if (!lf) return;
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fprintf(lf, "%s\n", buf);
    fclose(lf);
}

static void al_event(const char *fmt, ...) {
    if (!audio_trace_wanted())
        return;
    FILE *lf = fopen("ux0:/data/openal-impl.log", "a");
    if (!lf) return;
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fprintf(lf, "%s\n", buf);
    fclose(lf);
}
static unsigned int push_count = 0;



typedef struct VitaALBuffer {
    ALuint id;
    int in_use;
    ALenum format;
    ALsizei freq;
    ALsizei size;
    void *data;
} VitaALBuffer;

typedef struct VitaALSource {
    ALuint id;
    int in_use;
    ALint state;            /* AL_INITIAL / AL_PLAYING / AL_PAUSED / AL_STOPPED */
    ALint attached_buffer;  /* static buffer id or 0 */
    ALuint queued[16];
    int queued_count;
    int processed_count;
    ALfloat gain;
    ALfloat pitch;
    ALint looping;
    float cursor_frames;
    uint64_t played_output_frames;
    unsigned int underrun_restarts;
    VitaALBuffer *current_buffer;
} VitaALSource;

static VitaALBuffer buffers[MAX_BUFFERS];
static VitaALSource sources[MAX_SOURCES];
static int audio_muted = 0;
static ALuint next_buffer_id = 1;
static ALuint next_source_id = 1;
static int alc_device_open = 0;
static SDL_AudioDeviceID out_dev = 0;
static ALenum last_error = AL_NO_ERROR;
static pthread_mutex_t mixer_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t mixer_thread;
static int mixer_started = 0;
static int mixer_running = 0;

static void set_error(ALenum e) { if (last_error == AL_NO_ERROR) last_error = e; }

static VitaALBuffer *find_buffer(ALuint id) {
    for (int i = 0; i < MAX_BUFFERS; i++)
        if (buffers[i].in_use && buffers[i].id == id)
            return &buffers[i];
    return NULL;
}

static VitaALSource *find_source(ALuint id) {
    for (int i = 0; i < MAX_SOURCES; i++)
        if (sources[i].in_use && sources[i].id == id)
            return &sources[i];
    return NULL;
}

static int buffer_channels(const VitaALBuffer *buf) {
    return (buf->format == AL_FORMAT_STEREO8 ||
            buf->format == AL_FORMAT_STEREO16) ? 2 : 1;
}

static int buffer_bytes_per_sample(const VitaALBuffer *buf) {
    return (buf->format == AL_FORMAT_MONO8 ||
            buf->format == AL_FORMAT_STEREO8) ? 1 : 2;
}

static int buffer_frames(const VitaALBuffer *buf) {
    int frame_bytes = buffer_channels(buf) * buffer_bytes_per_sample(buf);
    return frame_bytes ? buf->size / frame_bytes : 0;
}

static inline float buffer_sample(const VitaALBuffer *buf, int frame,
                                  int channel) {
    int channels = buffer_channels(buf);
    int index = frame * channels + (channels == 1 ? 0 : channel);
    if (buffer_bytes_per_sample(buf) == 1)
        return ((const unsigned char *)buf->data)[index] * 256.0f - 32768.0f;
    return ((const short *)buf->data)[index];
}

static VitaALBuffer *source_buffer(VitaALSource *source) {
    if (source->current_buffer && source->current_buffer->in_use)
        return source->current_buffer;
    if (source->attached_buffer)
        source->current_buffer = find_buffer((ALuint)source->attached_buffer);
    if (source->processed_count < source->queued_count)
        source->current_buffer =
            find_buffer(source->queued[source->processed_count]);
    return source->current_buffer;
}

/* Called with mixer_mutex held. */
static inline void mix_source_frame(VitaALSource *source, float *left,
                                    float *right) {
    while (source->state == AL_PLAYING) {
        VitaALBuffer *buf = source_buffer(source);
        int frames = buf ? buffer_frames(buf) : 0;

        if (!buf || !buf->data || frames <= 0) {
            source->state = AL_STOPPED;
            return;
        }

        if (source->cursor_frames < frames) {
            int i0 = (int)source->cursor_frames;
            int i1 = i0 + 1 < frames ? i0 + 1 : i0;
            float frac = source->cursor_frames - i0;
            float l0 = buffer_sample(buf, i0, 0);
            float l1 = buffer_sample(buf, i1, 0);
            float r0 = buffer_sample(buf, i0, 1);
            float r1 = buffer_sample(buf, i1, 1);

            *left += (l0 + (l1 - l0) * frac) * source->gain;
            *right += (r0 + (r1 - r0) * frac) * source->gain;
            source->cursor_frames +=
                ((float)buf->freq / (float)OUT_FREQ) * source->pitch;
            source->played_output_frames++;
            return;
        }

        source->cursor_frames -= frames;
        source->current_buffer = NULL;
        if (source->attached_buffer) {
            if (!source->looping) {
                source->state = AL_STOPPED;
                source->cursor_frames = 0.0f;
                return;
            }
        } else {
            source->processed_count++;
            if (source->processed_count >= source->queued_count) {
                source->state = AL_STOPPED;
                source->cursor_frames = 0.0f;
                return;
            }
        }
    }
}

static short clamp_sample(float sample) {
    if (sample > 32767.0f) return 32767;
    if (sample < -32768.0f) return -32768;
    return (short)sample;
}

static void *audio_mixer_main(void *unused) {
    (void)unused;
    short output[OUT_FRAMES * 2];
    float mix[OUT_FRAMES * 2];

    for (;;) {
        pthread_mutex_lock(&mixer_mutex);
        if (!mixer_running) {
            pthread_mutex_unlock(&mixer_mutex);
            break;
        }

        memset(mix, 0, sizeof(mix));
        /* Keep each active source hot in cache for a whole output block.
         * Scanning all 32 source slots for every one of 512 frames cost about
         * 1.5 million slot checks per second on Vita even with one BGM. */
        for (int i = 0; i < MAX_SOURCES; i++) {
            if (sources[i].in_use && sources[i].state == AL_PLAYING) {
                for (int frame = 0;
                     frame < OUT_FRAMES && sources[i].state == AL_PLAYING;
                     frame++) {
                    mix_source_frame(&sources[i], &mix[frame * 2],
                                     &mix[frame * 2 + 1]);
                }
            }
        }
        for (int frame = 0; frame < OUT_FRAMES; frame++) {
            output[frame * 2] = clamp_sample(mix[frame * 2]);
            output[frame * 2 + 1] = clamp_sample(mix[frame * 2 + 1]);
        }
        pthread_mutex_unlock(&mixer_mutex);

        if (!out_dev || SDL_GetAudioDeviceStatus(out_dev) == SDL_AUDIO_PAUSED) {
            usleep(10000);
            continue;
        }

        /* A disconnected output port must not turn the mixer into a busy
         * loop that consumes the entire track while recovery is pending. */
        while (SDL_QueueAudio(out_dev, output, sizeof(output)) < 0) {
            usleep(10000);
            pthread_mutex_lock(&mixer_mutex);
            int active = mixer_running;
            pthread_mutex_unlock(&mixer_mutex);
            if (!active) return NULL;
        }
        if ((++push_count % 256) == 1)
            al_trace("MIX_OUTPUT n=%u", push_count);
    }

    return NULL;
}

/* ---- SDL_sound (Sound_*) — real minimal WAV decoder ---- */
#include <SDL_sound.h>

typedef struct VitaSoundState {
    SDL_RWops *rw;
    long data_off;
    long data_len;
    long pos;
    int src_bits;      /* source sample bits: 8/16/24/32 */
    int src_float;     /* 1 if the source is float PCM */
    int src_channels;
    int src_rate;
    Uint32 bufferSize;
    Uint8 *scratch;    /* native-format read buffer */
    Uint8 *decoded;    /* predecoded Ogg/Vorbis PCM */
    Uint32 decoded_len;
    int decoded_mode;  /* 0 = WAV, 1 = Vorbis PCM, 2 = streaming MP3 */
    drmp3 mp3;
    int mp3_initialized;
    Uint8 *mp3_data;
} VitaSoundState;

Uint16 Sound_GetNativeAudioFormat(void) { return AUDIO_S16SYS; }

static size_t vita_ogg_read(void *ptr, size_t size, size_t nmemb, void *source) {
    return SDL_RWread((SDL_RWops *)source, ptr, size, nmemb);
}

static int vita_ogg_seek(void *source, ogg_int64_t offset, int whence) {
    return SDL_RWseek((SDL_RWops *)source, offset, whence) < 0 ? -1 : 0;
}

static long vita_ogg_tell(void *source) {
    return (long)SDL_RWtell((SDL_RWops *)source);
}

static int vita_decode_ogg(SDL_RWops *rwops, Uint8 **pcm_out, Uint32 *len_out,
                           int *channels_out, int *rate_out) {
    ov_callbacks callbacks = { vita_ogg_read, vita_ogg_seek, NULL, vita_ogg_tell };
    OggVorbis_File vf;
    SDL_RWseek(rwops, 0, RW_SEEK_SET);
    if (ov_open_callbacks(rwops, &vf, NULL, 0, callbacks) < 0)
        return 0;

    vorbis_info *info = ov_info(&vf, -1);
    if (!info || info->channels < 1 || info->rate < 1) {
        ov_clear(&vf);
        return 0;
    }

    size_t cap = 65536;
    size_t len = 0;
    Uint8 *pcm = (Uint8 *)malloc(cap);
    if (!pcm) {
        ov_clear(&vf);
        return 0;
    }

    int bitstream = 0;
    for (;;) {
        if (cap - len < 16384) {
            size_t next_cap = cap * 2;
            Uint8 *next = (Uint8 *)realloc(pcm, next_cap);
            if (!next) {
                free(pcm);
                ov_clear(&vf);
                return 0;
            }
            pcm = next;
            cap = next_cap;
        }
        long got = ov_read(&vf, (char *)pcm + len, (int)(cap - len),
                           0, 2, 1, &bitstream);
        if (got == 0)
            break;
        if (got < 0)
            continue;
        len += (size_t)got;
        if (len > UINT32_MAX) {
            free(pcm);
            ov_clear(&vf);
            return 0;
        }
    }

    *channels_out = info->channels;
    *rate_out = info->rate;
    ov_clear(&vf);
    *pcm_out = pcm;
    *len_out = (Uint32)len;
    return 1;
}

Sound_Sample *Sound_NewSample(SDL_RWops *rwops, const char *ext,
                                     const Sound_AudioInfo *desired, Uint32 bufferSize) {
    (void)ext; (void)desired;
    if (!rwops) return NULL;
    Uint8 hdr[12];
    if (SDL_RWread(rwops, hdr, 1, 12) != 12) return NULL;
    if (!memcmp(hdr, "OggS", 4)) {
        Uint8 *pcm = NULL;
        Uint32 pcm_len = 0;
        int channels = 0;
        int rate = 0;
        if (!vita_decode_ogg(rwops, &pcm, &pcm_len, &channels, &rate))
            return NULL;

        VitaSoundState *st = (VitaSoundState *)calloc(1, sizeof(VitaSoundState));
        Sound_Sample *s = (Sound_Sample *)calloc(1, sizeof(Sound_Sample));
        if (!st || !s) {
            free(pcm);
            free(st);
            free(s);
            return NULL;
        }
        st->rw = rwops;
        st->src_bits = 16;
        st->src_channels = channels;
        st->src_rate = rate;
        st->bufferSize = bufferSize ? bufferSize : 32768;
        st->decoded = pcm;
        st->decoded_len = pcm_len;
        st->decoded_mode = 1;
        s->buffer = (Uint8 *)malloc(st->bufferSize);
        if (!s->buffer) {
            free(st->decoded);
            free(st);
            free(s);
            return NULL;
        }
        s->opaque = st;
        s->buffer_size = st->bufferSize;
        s->actual.format = AUDIO_S16SYS;
        s->actual.channels = channels;
        s->actual.rate = rate;
        s->rate = rate;
        s->format = 16;
        s->channels = channels;
        s->flags = SOUND_SAMPLEFLAG_CANSEEK;
        al_event("NS_OGG rate=%d ch=%d pcm=%u", rate, channels, pcm_len);
        return s;
    }
    if (memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4)) {
        VitaSoundState *st = (VitaSoundState *)calloc(1, sizeof(VitaSoundState));
        Sound_Sample *s = (Sound_Sample *)calloc(1, sizeof(Sound_Sample));
        if (!st || !s) {
            free(st);
            free(s);
            return NULL;
        }

        Sint64 compressed_size = SDL_RWseek(rwops, 0, RW_SEEK_END);
        if (compressed_size <= 0 || compressed_size > UINT32_MAX ||
            SDL_RWseek(rwops, 0, RW_SEEK_SET) < 0) {
            free(st);
            free(s);
            return NULL;
        }
        st->mp3_data = (Uint8 *)malloc((size_t)compressed_size);
        if (!st->mp3_data) {
            free(st);
            free(s);
            return NULL;
        }
        size_t compressed_read = 0;
        while (compressed_read < (size_t)compressed_size) {
            size_t got = SDL_RWread(rwops, st->mp3_data + compressed_read, 1,
                                    (size_t)compressed_size - compressed_read);
            if (!got)
                break;
            compressed_read += got;
        }
        al_event("NS_MP3_BEGIN bytes=%u", (unsigned)compressed_read);
        if (compressed_read != (size_t)compressed_size ||
            !drmp3_init_memory(&st->mp3, st->mp3_data, compressed_read, NULL)) {
            al_event("NS_MP3_FAIL bytes=%u", (unsigned)compressed_read);
            free(st->mp3_data);
            free(st);
            free(s);
            return NULL;
        }

        st->rw = rwops;
        st->src_bits = 16;
        st->src_channels = (int)st->mp3.channels;
        st->src_rate = (int)st->mp3.sampleRate;
        st->bufferSize = bufferSize ? bufferSize : 32768;
        st->decoded_mode = 2;
        st->mp3_initialized = 1;
        s->buffer = (Uint8 *)malloc(st->bufferSize);
        if (!s->buffer) {
            drmp3_uninit(&st->mp3);
            free(st->mp3_data);
            free(st);
            free(s);
            return NULL;
        }
        s->opaque = st;
        s->buffer_size = st->bufferSize;
        s->actual.format = AUDIO_S16SYS;
        s->actual.channels = st->src_channels;
        s->actual.rate = st->src_rate;
        s->rate = st->src_rate;
        s->format = 16;
        s->channels = st->src_channels;
        s->flags = SOUND_SAMPLEFLAG_CANSEEK;
        al_event("NS_MP3 rate=%d ch=%d buffer=%u", st->src_rate,
                 st->src_channels, st->bufferSize);
        return s;
    }

    int channels = 1, rate = 48000, bits = 16, is_float = 0;
    long data_off = 0, data_len = 0;
    long file_size = SDL_RWseek(rwops, 0, RW_SEEK_END);
    SDL_RWseek(rwops, 12, RW_SEEK_SET);

    while (1) {
        Uint8 ch[8];
        long here = SDL_RWtell(rwops);
        if (here < 0 || here + 8 > file_size) break;
        if (SDL_RWread(rwops, ch, 1, 8) != 8) break;
        Uint32 csz = ch[4] | (ch[5] << 8) | (ch[6] << 16) | (ch[7] << 24);
        if (!memcmp(ch, "fmt ", 4)) {
            Uint8 fmt[16];
            size_t got = SDL_RWread(rwops, fmt, 1, csz < 16 ? csz : 16);
            if (got < 16) break;
            Uint16 tag = fmt[0] | (fmt[1] << 8);
            if (tag == 0xFFFE && csz >= 40) {
                /* extensible: subformat GUID's first 2 bytes carry the tag */
                Uint8 sub[2];
                SDL_RWseek(rwops, here + 8 + 24, RW_SEEK_SET);
                if (SDL_RWread(rwops, sub, 1, 2) == 2)
                    tag = sub[0] | (sub[1] << 8);
            }
            channels = fmt[2] | (fmt[3] << 8);
            rate = fmt[4] | (fmt[5] << 8) | (fmt[6] << 16) | (fmt[7] << 24);
            bits = fmt[14] | (fmt[15] << 8);
            is_float = (tag == 3);
            if (channels < 1) channels = 1;
            if (rate <= 0) rate = 48000;
        } else if (!memcmp(ch, "data", 4)) {
            data_off = here + 8;
            data_len = csz;
            if (data_off + data_len > file_size) data_len = file_size - data_off;
            break;
        }
        SDL_RWseek(rwops, here + 8 + csz + (csz & 1), RW_SEEK_SET);
    }
    if (!data_off) return NULL;

    VitaSoundState *st = (VitaSoundState *)calloc(1, sizeof(VitaSoundState));
    Sound_Sample *s = (Sound_Sample *)calloc(1, sizeof(Sound_Sample));
    if (!st || !s) { free(st); free(s); return NULL; }
    st->rw = rwops;
    st->data_off = data_off;
    st->data_len = data_len;
    st->src_bits = bits;
    st->src_float = is_float;
    st->src_channels = channels;
    st->src_rate = rate;
    st->bufferSize = bufferSize ? bufferSize : 32768;
    st->scratch = (Uint8 *)malloc(65536);
    s->buffer = (Uint8 *)malloc(65536);
    if (!st->scratch || !s->buffer) {
        free(st->scratch);
        free(s->buffer);
        free(st);
        free(s);
        return NULL;
    }
    al_trace("NS rate=%d ch=%d bits=%d float=%d data_len=%ld", rate, channels, bits, is_float, data_len);
    al_trace("NS_SCRATCH=%p", st->scratch);
    s->opaque = st;
    al_trace("NS_OPAQUE_OK");
    al_trace("NS_BUF=%p", s->buffer);
    s->buffer_size = st->bufferSize;
    s->actual.format = AUDIO_S16SYS;
    s->actual.channels = channels;
    s->actual.rate = rate;
    s->rate = rate;
    s->format = 16;
    s->channels = channels;
    s->flags = SOUND_SAMPLEFLAG_CANSEEK;
    al_trace("NS_DONE");
    return s;
}

void Sound_FreeSample(Sound_Sample *sample) {
    if (!sample) return;
    VitaSoundState *st = (VitaSoundState *)sample->opaque;
    if (st) {
        if (st->mp3_initialized)
            drmp3_uninit(&st->mp3);
        if (st->rw) SDL_RWclose(st->rw);
        free(st->scratch);
        free(st->decoded);
        free(st->mp3_data);
        free(st);
    }
    free(sample->buffer);
    free(sample);
}

/* Convert src_bits PCM (channels interleaved) -> S16 into dst. */
static Uint32 vita_convert_pcm(Uint8 *src, Uint32 src_bytes, int bits, int is_float,
                               int channels, short *dst, Uint32 max_samples) {
    Uint32 out = 0;
    if (is_float) {
        Uint32 n = src_bytes / 4;
        for (Uint32 i = 0; i < n && out < max_samples; i++) {
            float f;
            memcpy(&f, src + i * 4, 4);
            double v = f * 32767.0;
            if (v > 32767.0) v = 32767.0;
            if (v < -32768.0) v = -32768.0;
            dst[out++] = (short)v;
        }
    } else if (bits == 8) {
        for (Uint32 i = 0; i < src_bytes && out < max_samples; i++)
            dst[out++] = (short)(((int)src[i] - 128) << 8);
    } else if (bits == 16) {
        Uint32 n = src_bytes / 2;
        memcpy(dst, src, n * 2 < max_samples * 2 ? n * 2 : max_samples * 2);
        out = n < max_samples ? n : max_samples;
    } else if (bits == 24) {
        Uint32 n = src_bytes / 3;
        for (Uint32 i = 0; i < n && out < max_samples; i++) {
            int v = (int)(src[i*3] | (src[i*3+1] << 8) | (src[i*3+2] << 16));
            if (v & 0x800000) v |= ~0xFFFFFF;
            dst[out++] = (short)(v >> 8);
        }
    } else { /* 32-bit int */
        Uint32 n = src_bytes / 4;
        for (Uint32 i = 0; i < n && out < max_samples; i++) {
            int v;
            memcpy(&v, src + i * 4, 4);
            dst[out++] = (short)(v >> 16);
        }
    }
    (void)channels;
    return out * 2;
}

Uint32 Sound_Decode(Sound_Sample *sample) {
    if (!sample || !sample->opaque) return 0;
    VitaSoundState *st = (VitaSoundState *)sample->opaque;
    if (st->decoded_mode == 2) {
        drmp3_uint64 frames_requested =
            sample->buffer_size / (sizeof(drmp3_int16) * st->src_channels);
        drmp3_uint64 frames_read = drmp3_read_pcm_frames_s16(
            &st->mp3, frames_requested, (drmp3_int16 *)sample->buffer);
        Uint32 bytes_read = (Uint32)(frames_read * st->src_channels *
                                     sizeof(drmp3_int16));
        st->pos += bytes_read;
        if (frames_read < frames_requested)
            sample->flags |= SOUND_SAMPLEFLAG_EOF;
        return bytes_read;
    }
    if (st->decoded_mode) {
        if (st->pos >= st->decoded_len) {
            sample->flags |= SOUND_SAMPLEFLAG_EOF;
            return 0;
        }
        Uint32 remaining = st->decoded_len - (Uint32)st->pos;
        Uint32 out = remaining < sample->buffer_size ? remaining : sample->buffer_size;
        memcpy(sample->buffer, st->decoded + st->pos, out);
        st->pos += out;
        if (st->pos >= st->decoded_len) sample->flags |= SOUND_SAMPLEFLAG_EOF;
        return out;
    }
    if (st->pos >= st->data_len) { sample->flags |= SOUND_SAMPLEFLAG_EOF; return 0; }
    long want = st->data_len - st->pos;
    Uint32 cap = st->bufferSize;
    if (st->src_bits == 8) cap /= 2;          /* 8-bit expands 2x */
    if (st->src_float) cap /= 2;
    if (want > (long)cap) want = cap;
    if (want <= 0) { sample->flags |= SOUND_SAMPLEFLAG_EOF; return 0; }
    al_trace("DEC_ENTER pos=%ld want=%ld data_off=%ld", st->pos, want, st->data_off);
    SDL_RWseek(st->rw, st->data_off + st->pos, RW_SEEK_SET);
    al_trace("DEC_SEEK_OK");
    size_t got = SDL_RWread(st->rw, st->scratch, 1, want);
    al_trace("DEC_READ_OK got=%u", (unsigned)got);
    if (got <= 0) { sample->flags |= SOUND_SAMPLEFLAG_EOF; return 0; }
    st->pos += (long)got;
    Uint32 out = vita_convert_pcm(st->scratch, got, st->src_bits, st->src_float,
                                  st->src_channels, (short *)sample->buffer,
                                  st->bufferSize / 2);
    if (st->pos >= st->data_len) sample->flags |= SOUND_SAMPLEFLAG_EOF;
    return out;
}

Uint32 Sound_DecodeAll(Sound_Sample *sample) {
    if (!sample || !sample->opaque) return 0;
    al_trace("DECALL");
    VitaSoundState *st = (VitaSoundState *)sample->opaque;
    if (st->decoded_mode == 2) {
        size_t capacity = 65536;
        size_t length = 0;
        Uint8 *decoded = (Uint8 *)malloc(capacity);
        if (!decoded)
            return 0;

        drmp3_uint64 frames_requested =
            sample->buffer_size / (sizeof(drmp3_int16) * st->src_channels);
        for (;;) {
            drmp3_uint64 frames_read = drmp3_read_pcm_frames_s16(
                &st->mp3, frames_requested, (drmp3_int16 *)sample->buffer);
            size_t bytes_read = (size_t)(frames_read * st->src_channels *
                                        sizeof(drmp3_int16));
            if (length + bytes_read > UINT32_MAX) {
                free(decoded);
                return 0;
            }
            if (length + bytes_read > capacity) {
                size_t next_capacity = capacity;
                while (next_capacity < length + bytes_read)
                    next_capacity *= 2;
                Uint8 *next = (Uint8 *)realloc(decoded, next_capacity);
                if (!next) {
                    free(decoded);
                    return 0;
                }
                decoded = next;
                capacity = next_capacity;
            }
            memcpy(decoded + length, sample->buffer, bytes_read);
            length += bytes_read;
            if (frames_read < frames_requested)
                break;
        }

        free(sample->buffer);
        sample->buffer = decoded;
        sample->buffer_size = (Uint32)length;
        st->pos = (long)length;
        sample->flags |= SOUND_SAMPLEFLAG_EOF;
        return (Uint32)length;
    }
    if (st->decoded_mode) {
        Uint8 *nb = (Uint8 *)realloc(sample->buffer,
                                     st->decoded_len ? st->decoded_len : 1);
        if (!nb) return 0;
        sample->buffer = nb;
        sample->buffer_size = st->decoded_len;
        memcpy(sample->buffer, st->decoded, st->decoded_len);
        st->pos = st->decoded_len;
        sample->flags |= SOUND_SAMPLEFLAG_EOF;
        return st->decoded_len;
    }
    long total = st->data_len;
    if (total <= 0) { sample->flags |= SOUND_SAMPLEFLAG_EOF; return 0; }
    Uint32 need = (Uint32)total;
    if (st->src_bits == 8 || st->src_float) need = (Uint32)(total / 2);
    Uint8 *nb = (Uint8 *)realloc(sample->buffer, need ? need : 1);
    if (!nb) return 0;
    sample->buffer = nb;
    sample->buffer_size = need;
    st->bufferSize = need;
    Uint8 *nscr = (Uint8 *)realloc(st->scratch, st->data_len ? st->data_len : 1);
    if (!nscr) return 0;
    st->scratch = nscr;
    SDL_RWseek(st->rw, st->data_off, RW_SEEK_SET);
    size_t got = SDL_RWread(st->rw, st->scratch, 1, st->data_len);
    st->pos = st->data_len;
    Uint32 out = vita_convert_pcm(st->scratch, got, st->src_bits, st->src_float,
                                  st->src_channels, (short *)sample->buffer,
                                  sample->buffer_size / 2);
    sample->flags |= SOUND_SAMPLEFLAG_EOF;
    return out;
}

void Sound_Rewind(Sound_Sample *sample) {
    if (!sample || !sample->opaque) return;
    VitaSoundState *st = (VitaSoundState *)sample->opaque;
    al_trace("RW pos=%ld", st->pos);
    if (st->decoded_mode == 2)
        drmp3_seek_to_pcm_frame(&st->mp3, 0);
    st->pos = 0;
    sample->flags &= ~SOUND_SAMPLEFLAG_EOF;
}

void Sound_SetVolume(Sound_Sample *sample, int volume) { (void)sample; (void)volume; }

int Sound_Seek(Sound_Sample *sample, Uint32 ms) {
    if (!sample || !sample->opaque) return -1;
    VitaSoundState *st = (VitaSoundState *)sample->opaque;
    if (st->decoded_mode == 2) {
        drmp3_uint64 frame = (drmp3_uint64)ms * st->src_rate / 1000;
        if (!drmp3_seek_to_pcm_frame(&st->mp3, frame))
            return -1;
        st->pos = (long)(frame * st->src_channels * sizeof(drmp3_int16));
        sample->flags &= ~SOUND_SAMPLEFLAG_EOF;
        return 0;
    }
    if (st->decoded_mode) {
        long frame = (long)ms * st->src_rate / 1000;
        long bytepos = frame * 2 * st->src_channels;
        if (bytepos > (long)st->decoded_len) bytepos = st->decoded_len;
        st->pos = bytepos;
        sample->flags &= ~SOUND_SAMPLEFLAG_EOF;
        return 0;
    }
    long frame = (long)ms * st->src_rate / 1000;
    long bytepos = frame * (st->src_bits / 8) * st->src_channels;
    if (st->src_bits == 8) bytepos = frame * st->src_channels;
    if (bytepos > st->data_len) bytepos = st->data_len;
    st->pos = bytepos;
    sample->flags &= ~SOUND_SAMPLEFLAG_EOF;
    return 0;
}

int Sound_Init(void) { return 1; }
void Sound_Quit(void) {}
const char *Sound_GetError(void) { return ""; }

/* ---- ALC ---- */
ALCdevice *alcOpenDevice(const ALCchar *devicename) {
    (void)devicename;
    /* Audio is part of the normal game contract. Keep a device-side emergency
     * opt-out for diagnosis, but do not silently mute every release unless an
     * enable marker happens to exist. */
    audio_muted = (access("ux0:/data/hardrpg/disable-audio", F_OK) == 0);
    if (!alc_device_open && !audio_muted) {
        SDL_AudioSpec spec;
        memset(&spec, 0, sizeof(spec));
        spec.freq = OUT_FREQ;
        spec.format = AUDIO_S16SYS;
        spec.channels = 2;
        spec.samples = OUT_FRAMES;
        out_dev = SDL_OpenAudioDevice(NULL, 0, &spec, NULL, 0);
        alc_device_open = (out_dev != 0);
        al_event("SESSION_OPEN muted=%d dev=%u", audio_muted,
                 (unsigned int)out_dev);
        if (!alc_device_open)
            return NULL;

        pthread_mutex_lock(&mixer_mutex);
        mixer_running = 1;
        pthread_mutex_unlock(&mixer_mutex);
        if (pthread_create(&mixer_thread, NULL, audio_mixer_main, NULL) != 0) {
            pthread_mutex_lock(&mixer_mutex);
            mixer_running = 0;
            pthread_mutex_unlock(&mixer_mutex);
            SDL_CloseAudioDevice(out_dev);
            out_dev = 0;
            alc_device_open = 0;
            return NULL;
        }
        mixer_started = 1;
    }
    return (ALCdevice *)0x1;
}
ALCboolean alcCloseDevice(ALCdevice *device) {
    (void)device;
    if (mixer_started) {
        pthread_mutex_lock(&mixer_mutex);
        mixer_running = 0;
        pthread_mutex_unlock(&mixer_mutex);
        pthread_join(mixer_thread, NULL);
        mixer_started = 0;
    }
    if (out_dev) {
        SDL_CloseAudioDevice(out_dev);
        out_dev = 0;
    }
    alc_device_open = 0;
    al_event("SESSION_CLOSE");
    return 1;
}
ALCcontext *alcCreateContext(ALCdevice *device, const ALCint *attrlist) {
    (void)device; (void)attrlist;
    return (ALCcontext *)0x2;
}
ALCboolean alcMakeContextCurrent(ALCcontext *context) { (void)context; return 1; }
void alcDestroyContext(ALCcontext *context) { (void)context; }
ALCcontext *alcGetCurrentContext(void) { return (ALCcontext *)0x2; }
ALCdevice *alcGetContextsDevice(ALCcontext *context) { (void)context; return (ALCdevice *)0x1; }
ALCenum alcGetError(ALCdevice *device) { (void)device; return ALC_NO_ERROR; }
const ALCchar *alcGetString(ALCdevice *device, ALCenum param) {
    (void)device;
    if (param == ALC_EXTENSIONS) return "ALC_SOFT_pause_device";
    if (param == ALC_DEVICE_SPECIFIER) return "Vita SDL";
    return "";
}
void alcGetIntegerv(ALCdevice *device, ALCenum param, ALCsizei size, ALCint *values) {
    (void)device; (void)param; (void)size; (void)values;
}
void *alcGetProcAddress(ALCdevice *device, const ALCchar *funcname) {
    (void)device; (void)funcname;
    return NULL;
}

/* ---- AL ---- */
ALenum alGetError(void) { ALenum e = last_error; last_error = AL_NO_ERROR; return e; }
void alGetIntegerv(ALenum param, ALint *values) {
    (void)param; if (values) *values = 0;
}
const ALchar *alGetString(ALenum param) {
    (void)param; return "";
}
void alDistanceModel(ALenum value) { (void)value; }
void alListenerf(ALenum param, ALfloat value) { (void)param; (void)value; }

void alGenBuffers(ALsizei n, ALuint *ids) {
    al_trace("GENB n=%d", (int)n);
    pthread_mutex_lock(&mixer_mutex);
    for (ALsizei i = 0; i < n; i++) {
        ALuint id = 0;
        if (!audio_muted) {
            for (int b = 0; b < MAX_BUFFERS; b++) {
                if (!buffers[b].in_use) {
                    buffers[b].in_use = 1;
                    buffers[b].id = next_buffer_id++;
                    buffers[b].format = 0;
                    buffers[b].freq = 0;
                    buffers[b].size = 0;
                    buffers[b].data = NULL;
                    id = buffers[b].id;
                    break;
                }
            }
        }
        ids[i] = id;
        if (!id) set_error(AL_OUT_OF_MEMORY);
    }
    pthread_mutex_unlock(&mixer_mutex);
}
void alDeleteBuffers(ALsizei n, ALuint *ids) {
    pthread_mutex_lock(&mixer_mutex);
    for (ALsizei i = 0; i < n; i++) {
        VitaALBuffer *b = find_buffer(ids[i]);
        if (b) { free(b->data); b->data = NULL; b->in_use = 0; }
    }
    pthread_mutex_unlock(&mixer_mutex);
}
void alBufferData(ALuint id, ALenum format, const void *data, ALsizei size, ALsizei freq) {
    pthread_mutex_lock(&mixer_mutex);
    VitaALBuffer *b = find_buffer(id);
    if (!b) {
        set_error(AL_INVALID_VALUE);
        pthread_mutex_unlock(&mixer_mutex);
        return;
    }
    al_trace("BUFDATA id=%u fmt=%d size=%d freq=%d", id, (int)format, (int)size, (int)freq);
    free(b->data);
    b->data = malloc(size);
    if (!b->data) {
        set_error(AL_OUT_OF_MEMORY);
        pthread_mutex_unlock(&mixer_mutex);
        return;
    }
    memcpy(b->data, data, size);
    b->format = format;
    b->freq = freq;
    b->size = size;
    pthread_mutex_unlock(&mixer_mutex);
}
void alGetBufferi(ALuint id, ALenum param, ALint *value) {
    pthread_mutex_lock(&mixer_mutex);
    VitaALBuffer *b = find_buffer(id);
    if (!b) {
        set_error(AL_INVALID_VALUE);
        pthread_mutex_unlock(&mixer_mutex);
        return;
    }
    switch (param) {
    case AL_SIZE: *value = b->size; break;
    case AL_BITS: *value = 16; break;
    case AL_CHANNELS: *value = (b->format == AL_FORMAT_STEREO16) ? 2 : 1; break;
    case AL_FREQUENCY: *value = b->freq; break;
    default: *value = 0; break;
    }
    pthread_mutex_unlock(&mixer_mutex);
}

void alGenSources(ALsizei n, ALuint *ids) {
    al_trace("GENS n=%d", (int)n);
    pthread_mutex_lock(&mixer_mutex);
    for (ALsizei i = 0; i < n; i++) {
        ALuint id = 0;
        if (!audio_muted) {
            for (int s = 0; s < MAX_SOURCES; s++) {
                if (!sources[s].in_use) {
                    sources[s].in_use = 1;
                    sources[s].id = next_source_id++;
                    sources[s].state = AL_INITIAL;
                    sources[s].attached_buffer = 0;
                    sources[s].queued_count = 0;
                    sources[s].processed_count = 0;
                    sources[s].gain = 1.0f;
                    sources[s].pitch = 1.0f;
                    sources[s].looping = AL_FALSE;
                    sources[s].cursor_frames = 0.0f;
                    sources[s].played_output_frames = 0;
                    sources[s].underrun_restarts = 0;
                    sources[s].current_buffer = NULL;
                    id = sources[s].id;
                    break;
                }
            }
        }
        ids[i] = id;
        if (!id) set_error(AL_OUT_OF_MEMORY);
    }
    pthread_mutex_unlock(&mixer_mutex);
}
void alDeleteSources(ALsizei n, ALuint *ids) {
    pthread_mutex_lock(&mixer_mutex);
    for (ALsizei i = 0; i < n; i++) {
        VitaALSource *s = find_source(ids[i]);
        if (s) { s->in_use = 0; s->queued_count = 0; s->processed_count = 0; }
    }
    pthread_mutex_unlock(&mixer_mutex);
}
void alSourcef(ALuint id, ALenum param, ALfloat value) {
    pthread_mutex_lock(&mixer_mutex);
    VitaALSource *s = find_source(id);
    if (!s) {
        set_error(AL_INVALID_VALUE);
        pthread_mutex_unlock(&mixer_mutex);
        return;
    }
    al_trace("SRCF id=%u param=%d", id, (int)param);
    if (param == AL_GAIN) s->gain = value;
    else if (param == AL_PITCH) s->pitch = value;
    pthread_mutex_unlock(&mixer_mutex);
}
void alSourcei(ALuint id, ALenum param, ALint value) {
    pthread_mutex_lock(&mixer_mutex);
    VitaALSource *s = find_source(id);
    if (!s) {
        set_error(AL_INVALID_VALUE);
        pthread_mutex_unlock(&mixer_mutex);
        return;
    }
    al_trace("SRCI id=%u param=%d val=%d", id, (int)param, (int)value);
    if (param == AL_BUFFER) {
        /* mkxp clears a streaming source by attaching buffer 0 before it
         * starts a new track.  OpenAL treats that as removing the queued
         * stream buffers too.  Keeping those IDs here makes the next track
         * replay buffers that have already been overwritten with new PCM,
         * which sounds like repeated or skipped sections after a BGM switch. */
        s->attached_buffer = value;
        s->queued_count = 0;
        s->processed_count = 0;
        s->cursor_frames = 0.0f;
        s->played_output_frames = 0;
        s->underrun_restarts = 0;
        s->current_buffer = NULL;
    }
    else if (param == AL_LOOPING) s->looping = value ? AL_TRUE : AL_FALSE;
    else if (param == AL_SOURCE_RELATIVE) { (void)value; }
    pthread_mutex_unlock(&mixer_mutex);
}
void alGetSourcei(ALuint id, ALenum param, ALint *value) {
    pthread_mutex_lock(&mixer_mutex);
    VitaALSource *s = find_source(id);
    if (!s) {
        set_error(AL_INVALID_VALUE);
        pthread_mutex_unlock(&mixer_mutex);
        return;
    }
    al_trace("GETI id=%u param=%d", id, (int)param);
    switch (param) {
    case AL_SOURCE_STATE: *value = s->state; break;
    case AL_BUFFERS_QUEUED: *value = s->queued_count; break;
    case AL_BUFFERS_PROCESSED: *value = s->processed_count; break;
    case AL_BUFFER: *value = s->attached_buffer; break;
    case AL_LOOPING: *value = s->looping; break;
    default: *value = 0; break;
    }
    pthread_mutex_unlock(&mixer_mutex);
}
void alGetSourcef(ALuint id, ALenum param, ALfloat *value) {
    pthread_mutex_lock(&mixer_mutex);
    VitaALSource *s = find_source(id);
    if (!s) {
        set_error(AL_INVALID_VALUE);
        pthread_mutex_unlock(&mixer_mutex);
        return;
    }
    if (param == AL_SEC_OFFSET)
        *value = (ALfloat)s->played_output_frames / (ALfloat)OUT_FREQ;
    else if (param == AL_GAIN)
        *value = s->gain;
    else if (param == AL_PITCH)
        *value = s->pitch;
    else
        *value = 0.0f;
    pthread_mutex_unlock(&mixer_mutex);
}

void alSourceQueueBuffers(ALuint id, ALsizei n, ALuint *ids) {
    pthread_mutex_lock(&mixer_mutex);
    VitaALSource *s = find_source(id);
    if (!s) {
        set_error(AL_INVALID_VALUE);
        pthread_mutex_unlock(&mixer_mutex);
        return;
    }
    al_trace("QUEUE id=%u n=%d q=%d p=%d", id, (int)n, s->queued_count, s->processed_count);
    for (ALsizei i = 0; i < n && s->queued_count < 16; i++) {
        s->queued[s->queued_count++] = ids[i];
    }
    pthread_mutex_unlock(&mixer_mutex);
}
void alSourceUnqueueBuffers(ALuint id, ALsizei n, ALuint *ids) {
    pthread_mutex_lock(&mixer_mutex);
    VitaALSource *s = find_source(id);
    if (!s) {
        set_error(AL_INVALID_VALUE);
        pthread_mutex_unlock(&mixer_mutex);
        return;
    }
    al_trace("UNQUEUE id=%u n=%d p=%d", id, (int)n, s->processed_count);
    for (ALsizei i = 0; i < n && s->processed_count > 0; i++) {
        ids[i] = s->queued[0];
        for (int q = 0; q < s->queued_count - 1; q++)
            s->queued[q] = s->queued[q + 1];
        s->queued_count--;
        s->processed_count--;
    }
    pthread_mutex_unlock(&mixer_mutex);
}
void alSourcePlay(ALuint id) {
    int stream_restart = 0;
    unsigned int restart_count = 0;
    int queued = 0;
    int processed = 0;
    pthread_mutex_lock(&mixer_mutex);
    VitaALSource *s = find_source(id);
    if (!s) {
        set_error(AL_INVALID_VALUE);
        pthread_mutex_unlock(&mixer_mutex);
        return;
    }
    al_trace("PLAY id=%u q=%d p=%d", id, s->queued_count, s->processed_count);
    stream_restart = s->state == AL_STOPPED && !s->attached_buffer &&
                     s->queued_count > 0 && s->processed_count > 0;
    if (stream_restart)
        restart_count = ++s->underrun_restarts;
    queued = s->queued_count;
    processed = s->processed_count;
    if (s->state == AL_STOPPED && s->attached_buffer)
        s->cursor_frames = 0.0f;
    if (s->state == AL_STOPPED)
        s->current_buffer = NULL;
    s->state = AL_PLAYING;
    pthread_mutex_unlock(&mixer_mutex);
    /* This runs on the stream worker after playback has already resumed, not
     * on the real-time mixer thread.  Keep it event-only so the next hardware
     * run can distinguish decoder/queue starvation from rendering FPS. */
    if (stream_restart)
        al_event("STREAM_RESTART id=%u count=%u queued=%d processed=%d",
                 id, restart_count, queued, processed);
}
void alSourceStop(ALuint id) {
    pthread_mutex_lock(&mixer_mutex);
    VitaALSource *s = find_source(id);
    if (!s) {
        set_error(AL_INVALID_VALUE);
        pthread_mutex_unlock(&mixer_mutex);
        return;
    }
    s->state = AL_STOPPED;
    s->cursor_frames = 0.0f;
    s->current_buffer = NULL;
    pthread_mutex_unlock(&mixer_mutex);
}
void alSourcePause(ALuint id) {
    pthread_mutex_lock(&mixer_mutex);
    VitaALSource *s = find_source(id);
    if (!s) {
        set_error(AL_INVALID_VALUE);
        pthread_mutex_unlock(&mixer_mutex);
        return;
    }
    if (s->state == AL_PLAYING) s->state = AL_PAUSED;
    pthread_mutex_unlock(&mixer_mutex);
}
