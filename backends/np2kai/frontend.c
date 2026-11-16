// SPDX-License-Identifier: MIT
#include "frontend.h"
#include "libretro.h"
#include <vfs/vfs_implementation.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static np2fe_config cfg;
static np2fe_frame frame;
static int pixel_xrgb8888;
static unsigned char keys[RETROK_LAST];
static struct retro_system_av_info av;
static int started;

// Defaults the core declares through SET_VARIABLES ("Label; default|other").
// The core only configures parts of the machine (such as the keyboard table)
// once it reads a value, so every option needs one, as RetroArch provides.
#define MAX_DEFAULTS 128
static struct {
  char key[64], value[64];
} defaults[MAX_DEFAULTS];
static int ndefaults;

static void remember_defaults(const struct retro_variable *v) {
  ndefaults = 0;
  for (; v && v->key && ndefaults < MAX_DEFAULTS; ++v) {
    const char *semi = v->value ? strstr(v->value, "; ") : NULL;
    if (!semi)
      continue;
    const char *start = semi + 2, *end = strchr(start, '|');
    size_t n = end ? (size_t)(end - start) : strlen(start);
    if (n >= sizeof defaults[0].value || strlen(v->key) >= sizeof defaults[0].key)
      continue;
    strcpy(defaults[ndefaults].key, v->key);
    memcpy(defaults[ndefaults].value, start, n);
    defaults[ndefaults].value[n] = 0;
    ndefaults++;
  }
}

// Mirrors NP2kai's _GDCS (io/gdc.h); every field is one byte.
extern struct {
  unsigned char access, disp, textdisp, msw_accessable, grphdisp, palchange, mode2;
} gdcs;
int np2fe_graphics_on(void) { return (gdcs.grphdisp & 0x80) != 0; }

// Added to the core by scripts/build-np2kai-vita.sh: when 0, the next frame
// runs the machine but skips drawing the PC-98 screen.
extern int np2kai_hardrpg_draw;
void np2fe_set_draw(int draw) { np2kai_hardrpg_draw = draw ? 1 : 0; }

static void logf_(const char *fmt, ...) {
  char line[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line, sizeof line, fmt, ap);
  va_end(ap);
  if (cfg.log)
    cfg.log(line, cfg.user);
}

static void core_log(enum retro_log_level level, const char *fmt, ...) {
  char line[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line, sizeof line, fmt, ap);
  va_end(ap);
  size_t n = strlen(line);
  while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
    line[--n] = 0;
  logf_("core[%d] %s", (int)level, line);
}

static const char *option_value(const char *key) {
  for (const np2fe_option *o = cfg.options; o && o->key; ++o)
    if (!strcmp(o->key, key))
      return o->value;
  for (int i = 0; i < ndefaults; ++i)
    if (!strcmp(defaults[i].key, key))
      return defaults[i].value;
  return NULL;
}

// ------------------------------------------------------------------ files
// The core reads the game disk in small seeks and reads. On the Vita's memory
// card those can stall a frame for hundreds of milliseconds, so the disk is
// kept in RAM: reads come from memory, and writes go to memory and straight
// through to the card so saves are never held back.
#define MEM_DISK_MAX (128 * 1024 * 1024)
struct retro_vfs_file_handle {
  libretro_vfs_implementation_file *real;
  int mem;     // served from mem_disk
  int64_t pos; // position in mem_disk
};
static unsigned char *mem_disk;
static int64_t mem_disk_size;
static np2fe_io_stats io;

static uint64_t now_us(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u;
}

static void load_mem_disk(libretro_vfs_implementation_file *real) {
  int64_t size = retro_vfs_file_size_impl(real);
  if (size <= 0 || size > MEM_DISK_MAX)
    return;
  unsigned char *data = malloc((size_t)size);
  if (!data)
    return;
  retro_vfs_file_seek_impl(real, 0, RETRO_VFS_SEEK_POSITION_START);
  int64_t got = 0;
  while (got < size) {
    int64_t n = retro_vfs_file_read_impl(real, data + got, (uint64_t)(size - got));
    if (n <= 0)
      break;
    got += n;
  }
  if (got != size) {
    free(data);
    return;
  }
  mem_disk = data;
  mem_disk_size = size;
  logf_("game disk held in memory (%lld bytes)", (long long)size);
}

static struct retro_vfs_file_handle *vfs_open(const char *path, unsigned mode, unsigned hints) {
  uint64_t t0 = now_us();
  libretro_vfs_implementation_file *real = retro_vfs_file_open_impl(path, mode, hints);
  if (!real)
    return NULL;
  struct retro_vfs_file_handle *h = calloc(1, sizeof *h);
  if (!h) {
    retro_vfs_file_close_impl(real);
    return NULL;
  }
  h->real = real;
  if (cfg.content && !strcmp(path, cfg.content)) {
    if (!mem_disk)
      load_mem_disk(real);
    h->mem = mem_disk != NULL;
  }
  io.open_us += now_us() - t0;
  return h;
}
static int vfs_close(struct retro_vfs_file_handle *h) {
  uint64_t t0 = now_us();
  int r = retro_vfs_file_close_impl(h->real);
  free(h);
  io.write_us += now_us() - t0;
  return r;
}
static const char *vfs_get_path(struct retro_vfs_file_handle *h) {
  return retro_vfs_file_get_path_impl(h->real);
}
static int64_t vfs_size(struct retro_vfs_file_handle *h) {
  return h->mem ? mem_disk_size : retro_vfs_file_size_impl(h->real);
}
static int64_t vfs_tell(struct retro_vfs_file_handle *h) {
  return h->mem ? h->pos : retro_vfs_file_tell_impl(h->real);
}
static int64_t vfs_seek(struct retro_vfs_file_handle *h, int64_t offset, int whence) {
  if (!h->mem) {
    uint64_t t0 = now_us();
    int64_t r = retro_vfs_file_seek_impl(h->real, offset, whence);
    io.read_us += now_us() - t0;
    return r;
  }
  int64_t base = whence == RETRO_VFS_SEEK_POSITION_CURRENT ? h->pos
               : whence == RETRO_VFS_SEEK_POSITION_END     ? mem_disk_size
                                                           : 0;
  if (base + offset < 0)
    return -1;
  h->pos = base + offset;
  return h->pos;
}
static int64_t vfs_read(struct retro_vfs_file_handle *h, void *s, uint64_t len) {
  if (!h->mem) {
    uint64_t t0 = now_us();
    int64_t r = retro_vfs_file_read_impl(h->real, s, len);
    io.read_us += now_us() - t0;
    io.read_bytes += r > 0 ? (uint64_t)r : 0;
    return r;
  }
  int64_t n = h->pos >= mem_disk_size ? 0 : mem_disk_size - h->pos;
  if ((uint64_t)n > len)
    n = (int64_t)len;
  memcpy(s, mem_disk + h->pos, (size_t)n);
  h->pos += n;
  io.mem_read_bytes += (uint64_t)n;
  return n;
}
static int64_t vfs_write(struct retro_vfs_file_handle *h, const void *s, uint64_t len) {
  uint64_t t0 = now_us();
  int64_t r;
  if (!h->mem) {
    r = retro_vfs_file_write_impl(h->real, s, len);
  } else {
    // A disk image never grows; the core only rewrites sectors in place.
    if (h->pos + (int64_t)len > mem_disk_size)
      len = h->pos >= mem_disk_size ? 0 : (uint64_t)(mem_disk_size - h->pos);
    retro_vfs_file_seek_impl(h->real, h->pos, RETRO_VFS_SEEK_POSITION_START);
    r = retro_vfs_file_write_impl(h->real, s, len);
    if (r > 0) {
      memcpy(mem_disk + h->pos, s, (size_t)r);
      h->pos += r;
    }
  }
  io.write_us += now_us() - t0;
  io.write_bytes += r > 0 ? (uint64_t)r : 0;
  return r;
}
static int vfs_flush(struct retro_vfs_file_handle *h) {
  uint64_t t0 = now_us();
  int r = retro_vfs_file_flush_impl(h->real);
  io.write_us += now_us() - t0;
  return r;
}
static int64_t vfs_truncate(struct retro_vfs_file_handle *h, int64_t length) {
  if (h->mem)
    return -1;
  return retro_vfs_file_truncate_impl(h->real, length);
}
static int vfs_stat(const char *path, int32_t *size) { return retro_vfs_stat_impl(path, size); }
static struct retro_vfs_dir_handle *vfs_opendir(const char *dir, bool hidden) {
  return (struct retro_vfs_dir_handle *)retro_vfs_opendir_impl(dir, hidden);
}
static bool vfs_readdir(struct retro_vfs_dir_handle *d) {
  return retro_vfs_readdir_impl((libretro_vfs_implementation_dir *)d);
}
static const char *vfs_dirent_name(struct retro_vfs_dir_handle *d) {
  return retro_vfs_dirent_get_name_impl((libretro_vfs_implementation_dir *)d);
}
static bool vfs_dirent_is_dir(struct retro_vfs_dir_handle *d) {
  return retro_vfs_dirent_is_dir_impl((libretro_vfs_implementation_dir *)d);
}
static int vfs_closedir(struct retro_vfs_dir_handle *d) {
  return retro_vfs_closedir_impl((libretro_vfs_implementation_dir *)d);
}
static struct retro_vfs_interface vfs = {
    vfs_get_path, vfs_open, vfs_close, vfs_size, vfs_tell, vfs_seek, vfs_read,
    vfs_write, vfs_flush, retro_vfs_file_remove_impl, retro_vfs_file_rename_impl,
    vfs_truncate, vfs_stat, retro_vfs_mkdir_impl, vfs_opendir, vfs_readdir,
    vfs_dirent_name, vfs_dirent_is_dir, vfs_closedir};

np2fe_io_stats np2fe_take_io_stats(void) {
  np2fe_io_stats s = io;
  memset(&io, 0, sizeof io);
  return s;
}

static bool environment(unsigned cmd, void *data) {
  switch (cmd) {
  case RETRO_ENVIRONMENT_GET_VFS_INTERFACE: {
    struct retro_vfs_interface_info *info = data;
    if (info->required_interface_version > 3)
      return false;
    info->required_interface_version = 3;
    info->iface = &vfs;
    return true;
  }
  case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    *(const char **)data = cfg.system_dir;
    return true;
  case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
    *(const char **)data = cfg.save_dir;
    return true;
  case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
    enum retro_pixel_format f = *(const enum retro_pixel_format *)data;
    if (f != RETRO_PIXEL_FORMAT_RGB565 && f != RETRO_PIXEL_FORMAT_XRGB8888)
      return false;
    pixel_xrgb8888 = f == RETRO_PIXEL_FORMAT_XRGB8888;
    return true;
  }
  case RETRO_ENVIRONMENT_GET_VARIABLE: {
    struct retro_variable *v = data;
    v->value = option_value(v->key);
    return v->value != NULL;
  }
  case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
    *(bool *)data = false;
    return true;
  case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
    ((struct retro_log_callback *)data)->log = core_log;
    return true;
  case RETRO_ENVIRONMENT_SET_VARIABLES:
    remember_defaults(data);
    return true;
  case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
  case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:
  case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
    return true;
  default:
    return false;
  }
}

static void video(const void *data, unsigned w, unsigned h, size_t pitch) {
  if (!data) // duplicate frame
    return;
  frame.pixels = data;
  frame.width = w;
  frame.height = h;
  frame.pitch = pitch;
  frame.xrgb8888 = pixel_xrgb8888;
  frame.serial++;
}

static void audio_sample(int16_t l, int16_t r) {
  int16_t s[2] = {l, r};
  if (cfg.audio)
    cfg.audio(s, 1, cfg.user);
}

static size_t audio_batch(const int16_t *data, size_t frames) {
  if (cfg.audio)
    cfg.audio(data, frames, cfg.user);
  return frames;
}

static void input_poll(void) {}

static int16_t input_state(unsigned port, unsigned device, unsigned index,
                           unsigned id) {
  (void)index;
  if (port != 0)
    return 0;
  if (device == RETRO_DEVICE_KEYBOARD && id < RETROK_LAST)
    return keys[id];
  return 0;
}

int np2fe_start(const np2fe_config *config) {
  cfg = *config;
  memset(&frame, 0, sizeof frame);
  memset(keys, 0, sizeof keys);
  retro_set_environment(environment);
  retro_set_video_refresh(video);
  retro_set_audio_sample(audio_sample);
  retro_set_audio_sample_batch(audio_batch);
  retro_set_input_poll(input_poll);
  retro_set_input_state(input_state);
  retro_init();
  struct retro_game_info game = {cfg.content, NULL, 0, NULL};
  if (!retro_load_game(&game)) {
    logf_("retro_load_game failed for %s", cfg.content);
    retro_deinit();
    return -1;
  }
  retro_get_system_av_info(&av);
  started = 1;
  logf_("NP2kai started: %ux%u %.2f fps %.0f Hz", av.geometry.base_width,
        av.geometry.base_height, av.timing.fps, av.timing.sample_rate);
  return 0;
}

void np2fe_run_frame(void) {
  if (started)
    retro_run();
}

void np2fe_stop(void) {
  if (!started)
    return;
  retro_unload_game();
  retro_deinit();
  started = 0;
  free(mem_disk);
  mem_disk = NULL;
  mem_disk_size = 0;
}

void np2fe_set_audio(np2fe_audio_fn audio) { cfg.audio = audio; }

const np2fe_frame *np2fe_current_frame(void) { return &frame; }
double np2fe_fps(void) { return av.timing.fps; }
double np2fe_sample_rate(void) { return av.timing.sample_rate; }

void np2fe_set_key(unsigned retrok, int down) {
  if (retrok < RETROK_LAST)
    keys[retrok] = down ? 1 : 0;
}

void np2fe_release_all_keys(void) { memset(keys, 0, sizeof keys); }

size_t np2fe_state_size(void) { return retro_serialize_size(); }
int np2fe_save_state(void *data, size_t size) {
  return retro_serialize(data, size) ? 0 : -1;
}
int np2fe_load_state(const void *data, size_t size) {
  return retro_unserialize(data, size) ? 0 : -1;
}
