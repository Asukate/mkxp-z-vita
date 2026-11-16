// SPDX-License-Identifier: MIT
// Desktop test harness for the NP2kai backend: runs the core headless,
// feeds scripted key presses straight into the frontend (no desktop input),
// and writes screenshots, a WAV of the audio, and speed numbers.
//
// usage: np2kai-harness --system DIR --save DIR --content FILE --out DIR
//                       --frames N [--script FILE] [--opt key=value]...
// script lines: "<frame> tap|down|up <key>" or "<frame> shot <name>"
#include "frontend.h"
#include "libretro.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
  long frame;
  char cmd[8], arg[64];
} step;

static step steps[4096];
static int nsteps;
static FILE *wav;
static unsigned long audio_frames;
static double audio_energy;
static const char *outdir;

static void on_log(const char *line, void *user) {
  (void)user;
  fprintf(stderr, "%s\n", line);
}

static void on_audio(const int16_t *s, size_t n, void *user) {
  (void)user;
  if (wav)
    fwrite(s, 4, n, wav);
  for (size_t i = 0; i < n * 2; ++i)
    audio_energy += (double)s[i] * s[i];
  audio_frames += n;
}

static unsigned keycode(const char *name) {
  static const struct { const char *n; unsigned k; } map[] = {
      {"up", RETROK_UP},       {"down", RETROK_DOWN},     {"left", RETROK_LEFT},
      {"right", RETROK_RIGHT}, {"enter", RETROK_RETURN},  {"esc", RETROK_ESCAPE},
      {"space", RETROK_SPACE}, {"z", RETROK_z},           {"x", RETROK_x},
      {"shift", RETROK_LSHIFT}, {"1", RETROK_1}, {"2", RETROK_2}, {"3", RETROK_3},
      {"4", RETROK_4}, {"y", RETROK_y}, {"n", RETROK_n}, {"f1", RETROK_F1}};
  for (size_t i = 0; i < sizeof map / sizeof *map; ++i)
    if (!strcmp(map[i].n, name))
      return map[i].k;
  fprintf(stderr, "unknown key %s\n", name);
  exit(2);
}

static void shot(const char *name) {
  const np2fe_frame *f = np2fe_current_frame();
  if (!f->pixels) {
    fprintf(stderr, "shot %s: no frame yet\n", name);
    return;
  }
  char path[1024];
  snprintf(path, sizeof path, "%s/%s.ppm", outdir, name);
  FILE *o = fopen(path, "wb");
  if (!o)
    return;
  fprintf(o, "P6\n%u %u\n255\n", f->width, f->height);
  for (unsigned y = 0; y < f->height; ++y) {
    const unsigned char *row = (const unsigned char *)f->pixels + y * f->pitch;
    for (unsigned x = 0; x < f->width; ++x) {
      unsigned char rgb[3];
      if (f->xrgb8888) {
        uint32_t p = ((const uint32_t *)row)[x];
        rgb[0] = p >> 16, rgb[1] = p >> 8, rgb[2] = p;
      } else {
        uint16_t p = ((const uint16_t *)row)[x];
        rgb[0] = (p >> 11) << 3, rgb[1] = ((p >> 5) & 63) << 2, rgb[2] = (p & 31) << 3;
      }
      fwrite(rgb, 1, 3, o);
    }
  }
  fclose(o);
  fprintf(stderr, "shot %s (%ux%u)\n", path, f->width, f->height);
}

static void wav_header(FILE *f, unsigned long frames, unsigned rate) {
  unsigned long data = frames * 4;
  unsigned char h[44] = "RIFF....WAVEfmt \x10\0\0\0\x01\0\x02\0........\x04\0\x10\0data....";
  unsigned long v[] = {36 + data, rate, rate * 4, data};
  int at[] = {4, 24, 28, 40};
  for (int i = 0; i < 4; ++i)
    for (int b = 0; b < 4; ++b)
      h[at[i] + b] = (v[i] >> (8 * b)) & 255;
  fseek(f, 0, SEEK_SET);
  fwrite(h, 1, 44, f);
}

int main(int argc, char **argv) {
  np2fe_option opts[64] = {{0}};
  int nopts = 0;
  np2fe_config c = {0};
  long frames = 600, draw_every = 1;
  const char *script = NULL;
  for (int i = 1; i + 1 < argc; i += 2) {
    if (!strcmp(argv[i], "--system")) c.system_dir = argv[i + 1];
    else if (!strcmp(argv[i], "--save")) c.save_dir = argv[i + 1];
    else if (!strcmp(argv[i], "--content")) c.content = argv[i + 1];
    else if (!strcmp(argv[i], "--out")) outdir = argv[i + 1];
    else if (!strcmp(argv[i], "--frames")) frames = atol(argv[i + 1]);
    else if (!strcmp(argv[i], "--script")) script = argv[i + 1];
    else if (!strcmp(argv[i], "--draw-every")) draw_every = atol(argv[i + 1]);
    else if (!strcmp(argv[i], "--opt") && nopts < 63) {
      char *kv = argv[i + 1], *eq = strchr(kv, '=');
      if (!eq) return 2;
      *eq = 0;
      opts[nopts].key = kv;
      opts[nopts++].value = eq + 1;
    } else {
      fprintf(stderr, "bad argument %s\n", argv[i]);
      return 2;
    }
  }
  if (!c.system_dir || !c.content || !outdir)
    return 2;
  if (!c.save_dir) c.save_dir = outdir;
  if (script) {
    FILE *s = fopen(script, "r");
    if (!s) return 2;
    char line[256];
    while (fgets(line, sizeof line, s) && nsteps < 4095) {
      step *st = &steps[nsteps];
      st->arg[0] = 0;
      if (line[0] == '#' || sscanf(line, "%ld %7s %63s", &st->frame, st->cmd, st->arg) < 2)
        continue;
      if (!strcmp(st->cmd, "tap")) { // press for 6 frames
        steps[nsteps + 1] = *st;
        strcpy(st->cmd, "down");
        steps[nsteps + 1].frame += 6;
        strcpy(steps[nsteps + 1].cmd, "up");
        nsteps += 2;
      } else
        nsteps++;
    }
    fclose(s);
  }
  c.options = opts;
  c.log = on_log;
  c.audio = on_audio;
  char path[1024];
  snprintf(path, sizeof path, "%s/audio.wav", outdir);
  wav = fopen(path, "wb");
  if (wav) fseek(wav, 44, SEEK_SET);
  snprintf(path, sizeof path, "%s/frametimes.txt", outdir);
  FILE *times = fopen(path, "w"); // "<frame> <microseconds>" per frame
  if (np2fe_start(&c)) return 1;
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  for (long f = 0; f < frames; ++f) {
    for (int i = 0; i < nsteps; ++i) {
      if (steps[i].frame != f) continue;
      if (!strcmp(steps[i].cmd, "down")) np2fe_set_key(keycode(steps[i].arg), 1);
      else if (!strcmp(steps[i].cmd, "up")) np2fe_set_key(keycode(steps[i].arg), 0);
      else if (!strcmp(steps[i].cmd, "shot")) shot(steps[i].arg);
    }
    np2fe_set_draw(draw_every <= 1 || f % draw_every == 0);
    unsigned long frame_audio0 = audio_frames;
    struct timespec f0, f1;
    clock_gettime(CLOCK_MONOTONIC, &f0);
    np2fe_run_frame();
    clock_gettime(CLOCK_MONOTONIC, &f1);
    if (times)
      fprintf(times, "%ld %.1f %lu\n", f, ((f1.tv_sec - f0.tv_sec) * 1e9 + (f1.tv_nsec - f0.tv_nsec)) / 1e3, audio_frames - frame_audio0);
    static int seen;
    if (!seen && np2fe_graphics_on()) {
      seen = 1;
      fprintf(stderr, "graphics on at frame %ld\n", f);
      char name[32];
      snprintf(name, sizeof name, "gfx-on-%ld", f);
      shot(name);
    }
  }
  clock_gettime(CLOCK_MONOTONIC, &t1);
  double secs = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
  shot("final");
  double rms = audio_frames ? __builtin_sqrt(audio_energy / (audio_frames * 2.0)) : 0;
  printf("frames=%ld wall=%.2fs speed=%.1f fps (target %.2f) audio_frames=%lu rms=%.1f\n",
         frames, secs, frames / secs, np2fe_fps(), audio_frames, rms);
  if (wav) {
    wav_header(wav, audio_frames, (unsigned)np2fe_sample_rate());
    fclose(wav);
  }
  if (times) fclose(times);
  np2fe_stop();
  return 0;
}
