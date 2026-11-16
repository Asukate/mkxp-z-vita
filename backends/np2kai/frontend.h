// SPDX-License-Identifier: MIT
// Minimal libretro frontend for the NP2kai (PC-98) core. Shared by the Vita
// backend and the desktop test harness, so both drive the core the same way.
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct np2fe_option {
  const char *key, *value;
} np2fe_option;

// Latest frame from the core, in the core's own pixel format.
typedef struct np2fe_frame {
  const void *pixels;
  unsigned width, height;
  size_t pitch;       // bytes per row
  int xrgb8888;       // 0 = RGB565, 1 = XRGB8888
  unsigned long serial; // bumps on every new frame
} np2fe_frame;

typedef void (*np2fe_audio_fn)(const int16_t *stereo, size_t frames, void *user);
typedef void (*np2fe_log_fn)(const char *line, void *user);

typedef struct np2fe_config {
  const char *system_dir; // NP2kai reads <system_dir>/np2kai/{font.bmp,bios.rom,np2kai.cfg}
  const char *save_dir;
  const char *content;    // .hdi/.fdi/.d88/... or .cmd
  const np2fe_option *options; // NULL-terminated by key
  np2fe_audio_fn audio;
  np2fe_log_fn log;
  void *user;
} np2fe_config;

int np2fe_start(const np2fe_config *config); // 0 on success
void np2fe_run_frame(void);
void np2fe_set_audio(np2fe_audio_fn audio); // e.g. drop sound during a hidden boot
void np2fe_stop(void);
const np2fe_frame *np2fe_current_frame(void);
double np2fe_fps(void);
double np2fe_sample_rate(void);

// True once the PC-98 graphics screen is on. DOS boots in text mode; a game
// switches graphics on when it starts, which ends the hidden fast boot.
int np2fe_graphics_on(void);

// Skip drawing the next frames (the machine still runs) to catch up when the
// Vita falls behind; the last picture stays on screen.
void np2fe_set_draw(int draw);

// Keyboard state, indexed by libretro RETROK_* codes.
void np2fe_set_key(unsigned retrok, int down);
void np2fe_release_all_keys(void);

// File time and traffic since the last call (the game disk is read from RAM).
typedef struct np2fe_io_stats {
  uint64_t open_us, read_us, write_us;
  uint64_t read_bytes, write_bytes, mem_read_bytes;
} np2fe_io_stats;
np2fe_io_stats np2fe_take_io_stats(void);

// Whole-machine state (memory, disks, devices) for quick save / resume.
size_t np2fe_state_size(void);
int np2fe_save_state(void *data, size_t size);
int np2fe_load_state(const void *data, size_t size);

#ifdef __cplusplus
}
#endif
