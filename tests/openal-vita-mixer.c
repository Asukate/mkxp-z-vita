/* Host test: the block mixer (mix_source_block) gives exactly the samples and
 * source state of the per-frame mixer (mix_source_frame) it replaced, for
 * mono/stereo, 8/16-bit, many rates and pitches, static (looping or not) and
 * queued buffers. Also times both.
 *
 * INC holds copies of the repo's shim headers (the system ones conflict):
 * SDL_sound.h from the repo, al.h and alc.h from deps/vita-compat.
 * cc -O2 -Wall -I$INC -Ideps $(pkg-config --cflags sdl2 vorbisfile) \
 *    tests/openal-vita-mixer.c $(pkg-config --libs sdl2 vorbisfile) -lm -lpthread -o mixer && ./mixer
 */
#include "../deps/openal-vita.c"
#include <time.h>

static unsigned rng = 12345;
static unsigned rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }

static VitaALBuffer *make_buffer(int stereo, int bits, int freq, int frames) {
    for (int b = 0; b < MAX_BUFFERS; b++) {
        if (buffers[b].in_use) continue;
        VitaALBuffer *buf = &buffers[b];
        buf->in_use = 1;
        buf->id = next_buffer_id++;
        buf->format = stereo ? (bits == 8 ? AL_FORMAT_STEREO8 : AL_FORMAT_STEREO16)
                             : (bits == 8 ? AL_FORMAT_MONO8 : AL_FORMAT_MONO16);
        buf->freq = freq;
        buf->size = frames * (stereo ? 2 : 1) * (bits / 8);
        buf->data = malloc(buf->size);
        for (int i = 0; i < buf->size; i++) ((unsigned char *)buf->data)[i] = (unsigned char)rnd();
        return buf;
    }
    abort();
}

static void reset_buffers(void) {
    for (int b = 0; b < MAX_BUFFERS; b++) {
        if (buffers[b].in_use) free(buffers[b].data);
        memset(&buffers[b], 0, sizeof(buffers[b]));
    }
}

static void setup(VitaALSource *s, int queued, int looping, float pitch, float gain,
                  VitaALBuffer **bufs, int nbufs) {
    memset(s, 0, sizeof(*s));
    s->in_use = 1;
    s->state = AL_PLAYING;
    s->gain = gain;
    s->pitch = pitch;
    s->looping = looping;
    if (queued) {
        for (int i = 0; i < nbufs; i++) s->queued[i] = bufs[i]->id;
        s->queued_count = nbufs;
    } else {
        s->attached_buffer = (ALint)bufs[0]->id;
    }
}

int main(void) {
    const int freqs[] = {11025, 22050, 32000, 44100, 48000, 96000};
    const float pitches[] = {0.5f, 0.75f, 1.0f, 1.17f, 1.5f};
    int cases = 0, failures = 0;
    float a[OUT_FRAMES * 2], b[OUT_FRAMES * 2];

    for (int c = 0; c < 600; c++) {
        reset_buffers();
        int stereo = rnd() & 1, bits = (rnd() % 4) ? 16 : 8;
        int freq = freqs[rnd() % 6];
        int queued = rnd() % 3 == 0;
        int looping = !queued && (rnd() & 1);
        float pitch = pitches[rnd() % 5], gain = (rnd() % 100) / 50.0f;
        int nbufs = queued ? 1 + rnd() % 3 : 1;
        VitaALBuffer *bufs[3];
        for (int i = 0; i < nbufs; i++) bufs[i] = make_buffer(stereo, bits, freq, 1 + rnd() % 3000);

        VitaALSource s1, s2;
        setup(&s1, queued, looping, pitch, gain, bufs, nbufs);
        setup(&s2, queued, looping, pitch, gain, bufs, nbufs);
        for (int block = 0; block < 12; block++) {
            memset(a, 0, sizeof(a));
            memset(b, 0, sizeof(b));
            for (int f = 0; f < OUT_FRAMES && s1.state == AL_PLAYING; f++)
                mix_source_frame(&s1, &a[f * 2], &a[f * 2 + 1]);
            mix_source_block(&s2, b, OUT_FRAMES);
            if (memcmp(a, b, sizeof(a)) || s1.state != s2.state || s1.cursor_frames != s2.cursor_frames ||
                s1.processed_count != s2.processed_count || s1.played_output_frames != s2.played_output_frames) {
                printf("FAIL case %d block %d (stereo=%d bits=%d freq=%d queued=%d loop=%d pitch=%g)\n",
                       c, block, stereo, bits, freq, queued, looping, pitch);
                failures++;
                break;
            }
        }
        cases++;
    }

    /* Timing: 16 looping 16-bit stereo 44.1 kHz voices, 2000 blocks. */
    reset_buffers();
    VitaALSource voices[2][16];
    for (int v = 0; v < 16; v++) {
        VitaALBuffer *buf = make_buffer(1, 16, 44100, 44100);
        setup(&voices[0][v], 0, 1, 1.0f, 0.5f, &buf, 1);
        setup(&voices[1][v], 0, 1, 1.0f, 0.5f, &buf, 1);
    }
    double t[2];
    for (int k = 0; k < 2; k++) {
        clock_t t0 = clock();
        for (int block = 0; block < 2000; block++) {
            memset(a, 0, sizeof(a));
            for (int v = 0; v < 16; v++) {
                if (k == 0)
                    for (int f = 0; f < OUT_FRAMES && voices[0][v].state == AL_PLAYING; f++)
                        mix_source_frame(&voices[0][v], &a[f * 2], &a[f * 2 + 1]);
                else
                    mix_source_block(&voices[1][v], a, OUT_FRAMES);
            }
        }
        t[k] = (double)(clock() - t0) / CLOCKS_PER_SEC;
    }
    printf("%d cases, %d failures; 16 voices x 2000 blocks: per-frame %.3fs, block %.3fs (%.1fx)\n",
           cases, failures, t[0], t[1], t[0] / (t[1] > 0 ? t[1] : 1e-9));
    return failures ? 1 : 0;
}
