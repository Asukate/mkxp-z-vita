/* Exercise the Vita OpenAL mixer with a zero-length queued buffer.
 *
 * A streamed track whose decoded length is an exact multiple of the stream
 * buffer ends with an empty read (Exit Fate's Cento.mp3, issue 25). The
 * stream queues that empty buffer as the loop's wrap marker. The mixer must
 * treat it as played and continue with the next queued buffer; stopping on
 * it without marking it processed left the BGM silent for good.
 *
 * Run: python3 tests/openal-empty-buffer.py (stages the Vita headers). */
#include "../deps/openal-vita.c"

#include <assert.h>

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else printf("ok: %s\n", msg); \
} while (0)

static ALuint make_buffer(int frames, short value) {
    ALuint id = 0;
    alGenBuffers(1, &id);
    short *pcm = frames ? (short *)malloc(sizeof(short) * frames) : NULL;
    for (int i = 0; i < frames; i++)
        pcm[i] = value;
    alBufferData(id, AL_FORMAT_MONO16, pcm, frames * (int)sizeof(short), OUT_FREQ);
    free(pcm);
    return id;
}

static int mix_frames(VitaALSource *s, int n, int *nonzero) {
    int played = 0;
    for (int i = 0; i < n && s->state == AL_PLAYING; i++) {
        float l = 0, r = 0;
        mix_source_frame(s, &l, &r);
        if (s->state == AL_PLAYING || l != 0.0f)
            played++;
        if (l != 0.0f && nonzero)
            (*nonzero)++;
    }
    return played;
}

int main(void) {
    /* 1. Empty buffer between two full ones: playback continues into the
     *    next loop and the empty buffer counts as processed. */
    {
        ALuint src = 0;
        alGenSources(1, &src);
        ALuint ids[3] = { make_buffer(64, 1000), make_buffer(0, 0), make_buffer(64, 2000) };
        alSourceQueueBuffers(src, 3, ids);
        alSourcePlay(src);
        VitaALSource *s = find_source(src);
        int nonzero = 0;
        mix_frames(s, 64 + 32, &nonzero);
        CHECK(s->state == AL_PLAYING, "source keeps playing past an empty queued buffer");
        CHECK(s->processed_count == 2, "first and empty buffers are both processed");
        CHECK(nonzero == 64 + 32, "every output frame came from real audio (no gap)");

        ALuint done[2] = { 0, 0 };
        alSourceUnqueueBuffers(src, 2, done);
        CHECK(done[0] == ids[0] && done[1] == ids[1], "stream can unqueue both and refill them");
    }

    /* 2. Empty buffer last in the queue: it is processed, so the stream
     *    worker sees a free buffer to refill; the source stops only because
     *    the queue ran dry (an ordinary underrun it restarts from). */
    {
        ALuint src = 0;
        alGenSources(1, &src);
        ALuint ids[2] = { make_buffer(16, 500), make_buffer(0, 0) };
        alSourceQueueBuffers(src, 2, ids);
        alSourcePlay(src);
        VitaALSource *s = find_source(src);
        mix_frames(s, 64, NULL);
        CHECK(s->processed_count == 2, "trailing empty buffer is processed");

        ALuint done[2] = { 0, 0 };
        alSourceUnqueueBuffers(src, 2, done);
        ALuint next = make_buffer(16, 700);
        alSourceQueueBuffers(src, 1, &next);
        alSourcePlay(src);
        int nonzero = 0;
        mix_frames(s, 8, &nonzero);
        CHECK(s->state == AL_PLAYING && nonzero == 8, "replay after refill produces audio");
    }

    /* 3. A zero-byte upload leaves a clean empty buffer. */
    {
        ALuint id = make_buffer(32, 100);
        last_error = AL_NO_ERROR;
        alBufferData(id, AL_FORMAT_MONO16, NULL, 0, OUT_FREQ);
        VitaALBuffer *b = find_buffer(id);
        CHECK(alGetError() == AL_NO_ERROR, "zero-byte alBufferData is not an error");
        CHECK(b->size == 0 && buffer_frames(b) == 0, "zero-byte buffer reports no frames");
    }

    /* 4. A static (attached) empty buffer still stops the source. */
    {
        ALuint src = 0;
        alGenSources(1, &src);
        ALuint id = make_buffer(0, 0);
        alSourcei(src, AL_BUFFER, (ALint)id);
        alSourcePlay(src);
        VitaALSource *s = find_source(src);
        mix_frames(s, 4, NULL);
        CHECK(s->state == AL_STOPPED, "empty static buffer stops the source");
    }

    printf(failures ? "FAILED (%d)\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
