#include "launcher_audio.h"
#include "theme.h"
#include <vorbis/vorbisfile.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>
#ifdef __vita__
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#else
#include <SDL.h>
#include <thread>
#endif

// The pinned MP3 decoder, kept private to this file: the engine's audio
// layer links its own copy.
#define DRMP3_API static
#define DRMP3_PRIVATE static
#define DR_MP3_NO_STDIO
#define DR_MP3_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "../../deps/dr_libs/dr_mp3.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace hardrpg {
namespace {
constexpr int outRate = 48000;
constexpr int blockFrames = 1024; // about 21 ms
constexpr size_t maxTrackBytes = 40u << 20;

void pause(int ms) {
#ifdef __vita__
  sceKernelDelayThread(SceUInt(ms) * 1000);
#else
  SDL_Delay(Uint32(ms));
#endif
}

std::string readWhole(const std::string &path) {
  std::string out;
  FILE *f = std::fopen(path.c_str(), "rb");
  if (!f)
    return out;
  char buffer[65536];
  size_t n;
  while ((n = std::fread(buffer, 1, sizeof buffer, f)) > 0) {
    out.append(buffer, n);
    if (out.size() > maxTrackBytes) {
      out.clear();
      break;
    }
  }
  std::fclose(f);
  return out;
}

// ---------------------------------------------------------------- decoders
// Each reads 16-bit interleaved PCM out of a file held in memory.
struct Decoder {
  int channels = 0, rate = 0;
  virtual ~Decoder() {}
  // Up to frames frames; 0 at the end of the track (or of its loop).
  virtual long read(int16_t *out, long frames) = 0;
  // Back to where the track loops from.
  virtual bool loop() = 0;
};

struct Memory {
  const std::string *data;
  size_t at = 0;
};
size_t memRead(void *ptr, size_t size, size_t count, void *source) {
  auto &m = *static_cast<Memory *>(source);
  size_t n = std::min(size * count, m.data->size() - m.at);
  std::memcpy(ptr, m.data->data() + m.at, n);
  m.at += n;
  return size ? n / size : 0;
}
int memSeek(void *source, ogg_int64_t offset, int whence) {
  auto &m = *static_cast<Memory *>(source);
  ogg_int64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? ogg_int64_t(m.at) : ogg_int64_t(m.data->size());
  ogg_int64_t to = base + offset;
  if (to < 0 || to > ogg_int64_t(m.data->size()))
    return -1;
  m.at = size_t(to);
  return 0;
}
long memTell(void *source) { return long(static_cast<Memory *>(source)->at); }

struct OggDecoder : Decoder {
  Memory memory;
  OggVorbis_File file;
  bool open = false;
  ogg_int64_t loopStart = 0, loopEnd = 0; // RPG Maker's LOOPSTART/LOOPLENGTH tags
  bool init(const std::string &bytes) {
    memory.data = &bytes;
    ov_callbacks callbacks = {memRead, memSeek, nullptr, memTell};
    if (ov_open_callbacks(&memory, &file, nullptr, 0, callbacks) < 0)
      return false;
    open = true;
    vorbis_info *info = ov_info(&file, -1);
    if (!info || info->channels < 1 || info->channels > 2 || info->rate < 8000)
      return false;
    channels = info->channels;
    rate = int(info->rate);
    if (vorbis_comment *c = ov_comment(&file, -1))
      for (int i = 0; i < c->comments; ++i) {
        std::string tag(c->user_comments[i], size_t(c->comment_lengths[i]));
        auto eq = tag.find('=');
        if (eq == std::string::npos)
          continue;
        auto name = tag.substr(0, eq);
        std::transform(name.begin(), name.end(), name.begin(), ::toupper);
        long long value = std::atoll(tag.c_str() + eq + 1);
        if (name == "LOOPSTART")
          loopStart = value;
        else if (name == "LOOPLENGTH")
          loopEnd = value; // made absolute below
      }
    if (loopEnd > 0)
      loopEnd += loopStart;
    return true;
  }
  ~OggDecoder() {
    if (open)
      ov_clear(&file);
  }
  long read(int16_t *out, long frames) override {
    if (loopEnd > 0) {
      ogg_int64_t left = loopEnd - ov_pcm_tell(&file);
      if (left <= 0)
        return 0;
      frames = long(std::min<ogg_int64_t>(frames, left));
    }
    long done = 0;
    int section = 0;
    while (done < frames) {
      long got = ov_read(&file, reinterpret_cast<char *>(out + done * channels),
                         int((frames - done) * channels * 2), 0, 2, 1, &section);
      if (got == OV_HOLE)
        continue;
      if (got <= 0)
        break;
      done += got / (2 * channels);
    }
    return done;
  }
  bool loop() override { return ov_pcm_seek(&file, loopStart) == 0; }
};

struct Mp3Decoder : Decoder {
  drmp3 mp3;
  bool open = false;
  bool init(const std::string &bytes) {
    if (!drmp3_init_memory(&mp3, bytes.data(), bytes.size(), nullptr))
      return false;
    open = true;
    channels = int(mp3.channels);
    rate = int(mp3.sampleRate);
    return channels >= 1 && channels <= 2 && rate >= 8000;
  }
  ~Mp3Decoder() {
    if (open)
      drmp3_uninit(&mp3);
  }
  long read(int16_t *out, long frames) override {
    return long(drmp3_read_pcm_frames_s16(&mp3, drmp3_uint64(frames), out));
  }
  bool loop() override { return drmp3_seek_to_pcm_frame(&mp3, 0) != 0; }
};

// Plain PCM WAV, 8 or 16 bit.
struct WavDecoder : Decoder {
  const unsigned char *data = nullptr;
  size_t size = 0, at = 0;
  int bits = 0;
  static uint32_t u32(const unsigned char *p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
  static uint16_t u16(const unsigned char *p) { return uint16_t(p[0] | p[1] << 8); }
  bool init(const std::string &bytes) {
    auto *p = reinterpret_cast<const unsigned char *>(bytes.data());
    size_t n = bytes.size();
    if (n < 12 || std::memcmp(p, "RIFF", 4) || std::memcmp(p + 8, "WAVE", 4))
      return false;
    for (size_t i = 12; i + 8 <= n;) {
      uint32_t len = u32(p + i + 4);
      const unsigned char *body = p + i + 8;
      if (len > n - i - 8)
        len = uint32_t(n - i - 8);
      if (!std::memcmp(p + i, "fmt ", 4) && len >= 16) {
        if (u16(body) != 1)
          return false; // compressed WAV
        channels = u16(body + 2);
        rate = int(u32(body + 4));
        bits = u16(body + 14);
      } else if (!std::memcmp(p + i, "data", 4)) {
        data = body;
        size = len;
      }
      i += 8 + len + (len & 1);
    }
    return data && (bits == 8 || bits == 16) && channels >= 1 && channels <= 2 && rate >= 8000;
  }
  long read(int16_t *out, long frames) override {
    size_t frameBytes = size_t(channels * bits / 8);
    long n = long(std::min<size_t>(size_t(frames), (size - at) / frameBytes));
    for (long i = 0; i < n * channels; ++i) {
      const unsigned char *s = data + at + size_t(i) * size_t(bits / 8);
      out[i] = bits == 16 ? int16_t(u16(s)) : int16_t((int(s[0]) - 128) << 8);
    }
    at += size_t(n) * frameBytes;
    return n;
  }
  bool loop() override {
    at = 0;
    return true;
  }
};

std::unique_ptr<Decoder> decoderFor(const std::string &ext, const std::string &bytes) {
  if (ext == ".ogg") {
    std::unique_ptr<OggDecoder> d(new OggDecoder);
    if (d->init(bytes))
      return std::unique_ptr<Decoder>(d.release());
  } else if (ext == ".mp3") {
    std::unique_ptr<Mp3Decoder> d(new Mp3Decoder);
    if (d->init(bytes))
      return std::unique_ptr<Decoder>(d.release());
  } else if (ext == ".wav") {
    std::unique_ptr<WavDecoder> d(new WavDecoder);
    if (d->init(bytes))
      return std::unique_ptr<Decoder>(d.release());
  }
  return nullptr;
}

// A decoder played at the output rate in stereo, by linear interpolation.
struct Stream {
  std::string bytes; // the file, which the decoder reads
  std::unique_ptr<Decoder> decoder;
  std::vector<int16_t> buffer; // stereo frames
  double at = 0, step = 1;
  bool looping = false, ended = false;
  float volume = 1;
  std::string name;
  void begin() { step = double(decoder->rate) / outRate; }
  bool refill() {
    size_t drop = size_t(at);
    if (drop) {
      buffer.erase(buffer.begin(), buffer.begin() + std::ptrdiff_t(std::min(drop * 2, buffer.size())));
      at -= double(drop);
    }
    int16_t raw[2048 * 2];
    for (int attempt = 0; attempt < 2; ++attempt) {
      long n = decoder->read(raw, 2048);
      if (n > 0) {
        for (long i = 0; i < n; ++i) {
          int16_t l = raw[i * decoder->channels];
          int16_t r = decoder->channels == 2 ? raw[i * 2 + 1] : l;
          buffer.push_back(l);
          buffer.push_back(r);
        }
        return true;
      }
      if (!looping || !decoder->loop())
        break;
    }
    ended = true;
    return false;
  }
  // Adds frames of this stream, scaled by gain, into mix.
  void mix(int32_t *mix, int frames, float gain) {
    for (int i = 0; i < frames && !ended; ++i) {
      while (size_t(at) + 1 >= buffer.size() / 2)
        if (!refill())
          return;
      size_t k = size_t(at);
      float f = float(at - double(k));
      const int16_t *a = &buffer[k * 2];
      mix[i * 2] += int32_t((a[0] + (a[2] - a[0]) * f) * gain);
      mix[i * 2 + 1] += int32_t((a[1] + (a[3] - a[1]) * f) * gain);
      at += step;
    }
  }
};

std::unique_ptr<Stream> openStream(std::string bytes, const std::string &ext) {
  std::unique_ptr<Stream> s(new Stream);
  s->bytes = std::move(bytes);
  s->decoder = decoderFor(ext, s->bytes);
  if (!s->decoder)
    return nullptr;
  s->begin();
  return s;
}
} // namespace

struct LauncherAudio::Impl {
  std::atomic<bool> quit{false};
  bool running = false;
  // Interface sounds, fully decoded at the output rate.
  std::vector<int16_t> effects[int(Sound::Count)];
  struct Voice {
    int sound;
    size_t at;
  };
  std::vector<Voice> voices;
  // Music: what plays, and what replaces it once it has faded out.
  std::unique_ptr<Stream> music, incoming;
  bool switching = false;
  float gain = 0, target = 0, fadeIn = 1.0f / outRate, fadeOut = 1.0f / (0.3f * outRate), outStep = 0;
  std::string playing;
  std::atomic<float> effectsLevel{0.49f}, musicLevel{0.64f};
  std::mutex mixing;
  // Requests from the interface thread to the loader.
  std::mutex asking;
  std::string wanted, loaded;
  bool fadeRequested = false;
  int fadeMs = 0;
#ifdef __vita__
  int port = -1;
  SceUID mixer = -1, loader = -1;
  static int openPort() {
    int p = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, blockFrames, outRate, SCE_AUDIO_OUT_MODE_STEREO);
    if (p >= 0) {
      int volume[2] = {SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB};
      sceAudioOutSetVolume(p, SceAudioOutChannelFlag(SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH), volume);
    }
    return p;
  }
  static int mixerEntry(SceSize, void *a) {
    (*static_cast<Impl **>(a))->mixLoop();
    return sceKernelExitThread(0);
  }
  static int loaderEntry(SceSize, void *a) {
    (*static_cast<Impl **>(a))->loadLoop();
    return sceKernelExitThread(0);
  }
#else
  SDL_AudioDeviceID device = 0;
  std::thread mixer, loader;
#endif

  void mix(int16_t *out, int frames) {
    int32_t sum[blockFrames * 2] = {};
    {
      std::lock_guard<std::mutex> lock(mixing);
      float fx = effectsLevel;
      for (auto v = voices.begin(); v != voices.end();) {
        auto &pcm = effects[v->sound];
        size_t n = std::min(size_t(frames), (pcm.size() / 2) - v->at);
        for (size_t i = 0; i < n * 2; ++i)
          sum[i] += int32_t(pcm[v->at * 2 + i] * fx);
        v->at += n;
        v = v->at * 2 >= pcm.size() ? voices.erase(v) : v + 1;
      }
      if (switching && (!music || gain <= 0)) {
        music = std::move(incoming);
        playing = music ? music->name : "";
        switching = false;
        gain = 0;
        target = music ? 1 : 0;
      } else if (switching)
        target = 0;
      if (music) {
        float level = musicLevel * music->volume;
        // Fade per block: smooth enough at 21 ms, and cheap.
        float step = (target > gain ? fadeIn : outStep > 0 ? outStep : fadeOut) * frames;
        float from = gain;
        gain = target > gain ? std::min(target, gain + step) : std::max(target, gain - step);
        int32_t part[blockFrames * 2] = {};
        music->mix(part, frames, 1.0f);
        for (int i = 0; i < frames; ++i) {
          float g = (from + (gain - from) * float(i) / float(frames)) * level;
          sum[i * 2] += int32_t(float(part[i * 2]) * g);
          sum[i * 2 + 1] += int32_t(float(part[i * 2 + 1]) * g);
        }
        if (music->ended || (gain <= 0 && target <= 0 && !switching)) {
          music.reset();
          playing.clear();
          outStep = 0;
        }
      }
    }
    for (int i = 0; i < frames * 2; ++i)
      out[i] = int16_t(std::max(-32768, std::min(32767, sum[i])));
  }

  void mixLoop() {
    int16_t block[blockFrames * 2];
    while (!quit) {
      mix(block, blockFrames);
#ifdef __vita__
      // After a suspend the port can stop taking audio; open it again.
      if (sceAudioOutOutput(port, block) < 0) {
        sceAudioOutReleasePort(port);
        port = openPort();
        if (port < 0)
          pause(50);
      }
#else
      while (!quit && SDL_GetQueuedAudioSize(device) > 2 * sizeof block)
        SDL_Delay(2);
      SDL_QueueAudio(device, block, sizeof block);
#endif
    }
  }

  void loadLoop() {
    while (!quit) {
      std::string want;
      bool change = false, fade = false;
      int ms = 0;
      {
        std::lock_guard<std::mutex> lock(asking);
        if (fadeRequested) {
          fade = true;
          ms = fadeMs;
          fadeRequested = false;
        }
        if (wanted != loaded) {
          want = wanted;
          loaded = wanted;
          change = true;
        }
      }
      if (fade) {
        std::lock_guard<std::mutex> lock(mixing);
        outStep = 1.0f / (std::max(ms, 1) * (outRate / 1000.0f));
        incoming.reset();
        switching = music != nullptr;
      }
      if (!change) {
        pause(20);
        continue;
      }
      std::unique_ptr<Stream> next;
      Theme theme;
      if (!want.empty() && findTheme(want, theme)) {
        auto bytes = theme.file.empty() ? std::move(theme.bytes) : readWhole(theme.file);
        if (!bytes.empty())
          next = openStream(std::move(bytes), theme.ext);
        if (next) {
          next->looping = true;
          next->volume = float(theme.volume) / 100.0f;
          next->name = theme.name;
        }
        std::fprintf(stderr, "Launcher theme: %s (%s)\n", theme.name.c_str(), next ? "playing" : "unplayable");
      }
      {
        std::lock_guard<std::mutex> lock(asking);
        if (wanted != want)
          continue; // moved on while this one loaded
      }
      std::lock_guard<std::mutex> lock(mixing);
      incoming = std::move(next);
      switching = true;
      outStep = 0;
    }
  }

  bool loadEffects(const std::string &folder) {
    bool any = false;
    for (int i = 0; i < int(Sound::Count); ++i) {
      auto s = openStream(readWhole(folder + "/" + soundName(Sound(i)) + ".ogg"), ".ogg");
      if (!s)
        continue;
      int32_t block[blockFrames * 2];
      for (int guard = 0; guard < 2000 && !s->ended; ++guard) {
        std::fill(block, block + blockFrames * 2, 0);
        s->mix(block, blockFrames, 1.0f);
        for (auto v : block)
          effects[i].push_back(int16_t(std::max(-32768, std::min(32767, v))));
      }
      any = true;
    }
    return any;
  }
};

LauncherAudio::LauncherAudio() : impl(new Impl) {}
LauncherAudio::~LauncherAudio() { stop(); }

bool LauncherAudio::start(const std::string &folder) {
  if (impl->running)
    return true;
  impl->loadEffects(folder);
  impl->quit = false;
#ifdef __vita__
  impl->port = Impl::openPort();
  if (impl->port < 0) {
    std::fprintf(stderr, "Launcher audio: no port (0x%08x)\n", unsigned(impl->port));
    return false;
  }
  Impl *self = impl.get();
  // The mixer runs on core 1, above the interface; the loader on core 2.
  impl->mixer = sceKernelCreateThread("hardrpg_audio_mix", Impl::mixerEntry, 64, 64 * 1024, 0,
                                      SCE_KERNEL_CPU_MASK_USER_1, nullptr);
  impl->loader = sceKernelCreateThread("hardrpg_audio_load", Impl::loaderEntry, 0x10000100, 256 * 1024, 0,
                                       SCE_KERNEL_CPU_MASK_USER_2, nullptr);
  if (impl->mixer < 0 || impl->loader < 0) {
    if (impl->mixer >= 0)
      sceKernelDeleteThread(impl->mixer);
    if (impl->loader >= 0)
      sceKernelDeleteThread(impl->loader);
    sceAudioOutReleasePort(impl->port);
    impl->port = impl->mixer = impl->loader = -1;
    return false;
  }
  sceKernelStartThread(impl->mixer, sizeof self, &self);
  sceKernelStartThread(impl->loader, sizeof self, &self);
#else
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
    return false;
  SDL_AudioSpec want{}, have{};
  want.freq = outRate;
  want.format = AUDIO_S16SYS;
  want.channels = 2;
  want.samples = blockFrames;
  impl->device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
  if (!impl->device) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return false;
  }
  SDL_PauseAudioDevice(impl->device, 0);
  impl->mixer = std::thread([this] { impl->mixLoop(); });
  impl->loader = std::thread([this] { impl->loadLoop(); });
#endif
  impl->running = true;
  return true;
}

void LauncherAudio::stop() {
  if (!impl || !impl->running)
    return;
  impl->quit = true;
#ifdef __vita__
  for (SceUID t : {impl->mixer, impl->loader}) {
    sceKernelWaitThreadEnd(t, nullptr, nullptr);
    sceKernelDeleteThread(t);
  }
  if (impl->port >= 0)
    sceAudioOutReleasePort(impl->port);
  impl->port = impl->mixer = impl->loader = -1;
#else
  impl->mixer.join();
  impl->loader.join();
  SDL_CloseAudioDevice(impl->device);
  SDL_QuitSubSystem(SDL_INIT_AUDIO);
  impl->device = 0;
#endif
  impl->running = false;
  std::lock_guard<std::mutex> lock(impl->mixing);
  impl->music.reset();
  impl->incoming.reset();
  impl->voices.clear();
}

void LauncherAudio::play(Sound s) {
  if (!impl->running || impl->effects[int(s)].empty() || impl->effectsLevel <= 0)
    return;
  std::lock_guard<std::mutex> lock(impl->mixing);
  if (impl->voices.size() >= 8)
    impl->voices.erase(impl->voices.begin());
  impl->voices.push_back({int(s), 0});
}

void LauncherAudio::theme(const std::string &gameDir) {
  std::lock_guard<std::mutex> lock(impl->asking);
  impl->wanted = gameDir;
}

void LauncherAudio::setLevels(int effects, int music) {
  // Squared, so steps sound even.
  auto curve = [](int v) {
    float f = float(std::max(0, std::min(100, v))) / 100.0f;
    return f * f;
  };
  impl->effectsLevel = curve(effects);
  impl->musicLevel = curve(music);
}

void LauncherAudio::fadeMusic(int ms) {
  std::lock_guard<std::mutex> lock(impl->asking);
  impl->fadeRequested = true;
  impl->fadeMs = ms;
  impl->wanted = impl->loaded = "";
}

std::string LauncherAudio::playingTheme() const {
  std::lock_guard<std::mutex> lock(impl->mixing);
  return impl->playing;
}
} // namespace hardrpg
