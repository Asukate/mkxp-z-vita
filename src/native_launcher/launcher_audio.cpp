#include "launcher_audio.h"
#include "theme.h"
#include <vorbis/vorbisfile.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>
#include <sys/stat.h>
#ifdef __vita__
#include <psp2/audioout.h>
#include <psp2/kernel/processmgr.h>
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
// Tracks up to this size are read into memory (and kept for a while); larger
// ones are decoded straight from the file, a little at a time.
constexpr long long memoryTrackBytes = 8ll << 20;

void pause(int ms) {
#ifdef __vita__
  sceKernelDelayThread(SceUInt(ms) * 1000);
#else
  SDL_Delay(Uint32(ms));
#endif
}

long long fileSize(const std::string &path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) ? (long long)st.st_size : -1;
}

// The whole file, or "" when it is missing or larger than limit. Sized once:
// growing a string while reading needs twice the memory for a moment.
std::string readWhole(const std::string &path, long long limit = memoryTrackBytes) {
  std::string out;
  long long size = fileSize(path);
  if (size <= 0 || size > limit)
    return out;
  FILE *f = std::fopen(path.c_str(), "rb");
  if (!f)
    return out;
  out.resize(size_t(size));
  if (std::fread(&out[0], 1, out.size(), f) != out.size())
    out.clear();
  std::fclose(f);
  return out;
}

// ---------------------------------------------------------------- input
// What a decoder reads: a file held in memory, or one on disk.
struct Input {
  virtual ~Input() {}
  virtual size_t read(void *to, size_t n) = 0;
  virtual bool seek(long long offset, int whence) = 0;
  virtual long long tell() = 0;
};
struct MemoryInput : Input {
  std::shared_ptr<const std::string> data;
  size_t at = 0;
  size_t read(void *to, size_t n) override {
    n = std::min(n, data->size() - at);
    std::memcpy(to, data->data() + at, n);
    at += n;
    return n;
  }
  bool seek(long long offset, int whence) override {
    long long base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (long long)at : (long long)data->size();
    long long to = base + offset;
    if (to < 0 || to > (long long)data->size())
      return false;
    at = size_t(to);
    return true;
  }
  long long tell() override { return (long long)at; }
};
struct FileInput : Input {
  FILE *f = nullptr;
  std::vector<char> buffer = std::vector<char>(64 * 1024); // few, larger reads from the card
  ~FileInput() {
    if (f)
      std::fclose(f);
  }
  bool open(const std::string &path) {
    f = std::fopen(path.c_str(), "rb");
    if (f)
      std::setvbuf(f, buffer.data(), _IOFBF, buffer.size());
    return f != nullptr;
  }
  size_t read(void *to, size_t n) override { return std::fread(to, 1, n, f); }
  bool seek(long long offset, int whence) override { return fseeko(f, off_t(offset), whence) == 0; }
  long long tell() override { return (long long)ftello(f); }
};

// ---------------------------------------------------------------- decoders
// Each reads 16-bit interleaved PCM out of its input.
struct Decoder {
  int channels = 0, rate = 0;
  std::unique_ptr<Input> input;
  virtual ~Decoder() {}
  // Up to frames frames; 0 at the end of the track (or of its loop).
  virtual long read(int16_t *out, long frames) = 0;
  // Back to where the track loops from.
  virtual bool loop() = 0;
};

size_t oggRead(void *ptr, size_t size, size_t count, void *source) {
  return size ? static_cast<Input *>(source)->read(ptr, size * count) / size : 0;
}
int oggSeek(void *source, ogg_int64_t offset, int whence) {
  return static_cast<Input *>(source)->seek((long long)offset, whence) ? 0 : -1;
}
long oggTell(void *source) { return long(static_cast<Input *>(source)->tell()); }

struct OggDecoder : Decoder {
  OggVorbis_File file;
  bool open = false;
  ogg_int64_t loopStart = 0, loopEnd = 0; // RPG Maker's LOOPSTART/LOOPLENGTH tags
  bool init() {
    ov_callbacks callbacks = {oggRead, oggSeek, nullptr, oggTell};
    if (ov_open_callbacks(input.get(), &file, nullptr, 0, callbacks) < 0)
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

size_t mp3Read(void *source, void *to, size_t n) { return static_cast<Input *>(source)->read(to, n); }
drmp3_bool32 mp3Seek(void *source, int offset, drmp3_seek_origin origin) {
  int whence = origin == DRMP3_SEEK_SET ? SEEK_SET : origin == DRMP3_SEEK_END ? SEEK_END : SEEK_CUR;
  return static_cast<Input *>(source)->seek(offset, whence) ? DRMP3_TRUE : DRMP3_FALSE;
}
drmp3_bool32 mp3Tell(void *source, drmp3_int64 *at) {
  *at = drmp3_int64(static_cast<Input *>(source)->tell());
  return DRMP3_TRUE;
}

struct Mp3Decoder : Decoder {
  drmp3 mp3;
  bool open = false;
  bool init() {
    if (!drmp3_init(&mp3, mp3Read, mp3Seek, mp3Tell, nullptr, input.get(), nullptr))
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
  long long data = -1, size = 0, at = 0; // the samples: where, how many bytes, how far read
  int bits = 0;
  static uint32_t u32(const unsigned char *p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
  static uint16_t u16(const unsigned char *p) { return uint16_t(p[0] | p[1] << 8); }
  bool init() {
    unsigned char head[12];
    if (input->read(head, 12) != 12 || std::memcmp(head, "RIFF", 4) || std::memcmp(head + 8, "WAVE", 4))
      return false;
    long long pos = 12;
    for (int guard = 0; guard < 64; ++guard) {
      unsigned char chunk[8];
      if (!input->seek(pos, SEEK_SET) || input->read(chunk, 8) != 8)
        break;
      uint32_t len = u32(chunk + 4);
      if (!std::memcmp(chunk, "fmt ", 4) && len >= 16) {
        unsigned char body[16];
        if (input->read(body, 16) != 16 || u16(body) != 1)
          return false; // compressed WAV
        channels = u16(body + 2);
        rate = int(u32(body + 4));
        bits = u16(body + 14);
      } else if (!std::memcmp(chunk, "data", 4)) {
        data = pos + 8;
        size = len;
        break;
      }
      pos += 8 + len + (len & 1);
    }
    return data >= 0 && (bits == 8 || bits == 16) && channels >= 1 && channels <= 2 && rate >= 8000 &&
           input->seek(data, SEEK_SET);
  }
  long read(int16_t *out, long frames) override {
    long long frameBytes = channels * bits / 8;
    long n = long(std::min<long long>(frames, (size - at) / frameBytes));
    unsigned char raw[2048 * 4];
    n = std::min<long>(n, long(sizeof raw / frameBytes));
    n = long(input->read(raw, size_t(n * frameBytes)) / size_t(frameBytes));
    for (long i = 0; i < n * channels; ++i) {
      const unsigned char *s = raw + size_t(i) * size_t(bits / 8);
      out[i] = bits == 16 ? int16_t(u16(s)) : int16_t((int(s[0]) - 128) << 8);
    }
    at += n * frameBytes;
    return n;
  }
  bool loop() override {
    at = 0;
    return input->seek(data, SEEK_SET);
  }
};

std::unique_ptr<Decoder> decoderFor(const std::string &ext, std::unique_ptr<Input> input) {
  std::unique_ptr<Decoder> d;
  if (ext == ".ogg")
    d.reset(new OggDecoder);
  else if (ext == ".mp3")
    d.reset(new Mp3Decoder);
  else if (ext == ".wav")
    d.reset(new WavDecoder);
  else
    return nullptr;
  d->input = std::move(input);
  bool ok = ext == ".ogg" ? static_cast<OggDecoder *>(d.get())->init()
          : ext == ".mp3" ? static_cast<Mp3Decoder *>(d.get())->init()
                          : static_cast<WavDecoder *>(d.get())->init();
  return ok ? std::move(d) : nullptr;
}

// A decoder played at the output rate in stereo, by linear interpolation.
struct Stream {
  std::unique_ptr<Decoder> decoder;
  std::vector<int16_t> buffer; // stereo frames
  double at = 0, step = 1;
  bool looping = false, ended = false;
  float volume = 1;
  std::string name;
  // pitch: the game's pitch in percent, which RPG Maker plays as speed.
  void begin(int pitch = 100) { step = double(decoder->rate) / outRate * std::max(50, std::min(150, pitch)) / 100.0; }
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

// From bytes in memory, or (bytes empty) from the file at path.
std::unique_ptr<Stream> openStream(std::shared_ptr<const std::string> bytes, const std::string &ext,
                                   const std::string &path = "") {
  std::unique_ptr<Input> input;
  if (bytes && !bytes->empty()) {
    std::unique_ptr<MemoryInput> m(new MemoryInput);
    m->data = std::move(bytes);
    input = std::move(m);
  } else if (!path.empty()) {
    std::unique_ptr<FileInput> f(new FileInput);
    if (!f->open(path))
      return nullptr;
    input = std::move(f);
  } else
    return nullptr;
  std::unique_ptr<Stream> s(new Stream);
  s->decoder = decoderFor(ext, std::move(input));
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
  float gain = 0, target = 0, fadeIn = 1.0f / (0.2f * outRate), fadeOut = 1.0f / (0.3f * outRate), outStep = 0;
  std::string playing;
  std::atomic<float> effectsLevel{0.49f}, musicLevel{0.64f};
  mutable std::mutex mixing;
  // Requests from the interface thread to the loader.
  std::mutex asking;
  std::string wanted, loaded;
  bool fadeRequested = false;
  int fadeMs = 0;
  std::string cache, useCache; // set by the interface; the loader's copy
  std::vector<std::string> rtps, useRtps;
  std::vector<std::string> prepare, nearby; // the library, and the games beside the highlighted one
  size_t prepared = 0;
  uint32_t prepareFrom = 0; // background work waits until the screen is up
  // Tracks read lately, so going back to a game starts at once.
  struct Track {
    std::shared_ptr<const std::string> bytes;
    Theme theme;
    bool found = false;
  };
  std::map<std::string, Track> tracks;
  std::vector<std::string> trackOrder; // oldest first
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

  // Called on the audio thread only. The lock covers the effect voices and
  // the handover of a new track; the music itself is decoded outside it, so a
  // slow decode (a track starting, a stream read from the card) never holds
  // up the screen thread, which takes the lock for every button sound.
  void mix(int16_t *out, int frames) {
    int32_t sum[blockFrames * 2] = {};
    Stream *current = nullptr;
    float level = 0, from = 0, to = 0;
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
        outStep = 0;
      } else if (switching)
        target = 0;
      if (music) {
        level = musicLevel * music->volume;
        // Fade per block: smooth enough at 21 ms, and cheap.
        float step = (target > gain ? fadeIn : outStep > 0 ? outStep : fadeOut) * frames;
        from = gain;
        gain = target > gain ? std::min(target, gain + step) : std::max(target, gain - step);
        to = gain;
        current = music.get();
      }
    }
    if (current) {
      // Only this thread replaces or releases `music`, so it stays valid here.
      int32_t part[blockFrames * 2] = {};
      current->mix(part, frames, 1.0f);
      for (int i = 0; i < frames; ++i) {
        float g = (from + (to - from) * float(i) / float(frames)) * level;
        sum[i * 2] += int32_t(float(part[i * 2]) * g);
        sum[i * 2 + 1] += int32_t(float(part[i * 2 + 1]) * g);
      }
      std::lock_guard<std::mutex> lock(mixing);
      if (music.get() == current && (current->ended || (gain <= 0 && target <= 0 && !switching))) {
        music.reset();
        playing.clear();
        outStep = 0;
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

  // Fades out whatever plays, quickly, without waiting for what comes next.
  void fadeNow(int ms) {
    std::lock_guard<std::mutex> lock(mixing);
    outStep = 1.0f / (std::max(ms, 1) * (outRate / 1000.0f));
    incoming.reset();
    switching = music != nullptr;
  }

  // A game's track: from memory, or found (the theme cache makes that one
  // small file read) and read into memory, keeping the last few.
  Track *track(const std::string &source) {
    auto it = tracks.find(source);
    if (it != tracks.end()) {
      trackOrder.erase(std::find(trackOrder.begin(), trackOrder.end(), source));
      trackOrder.push_back(source);
      return &it->second;
    }
    Track t;
    t.found = findTheme(source, t.theme, useCache, useRtps);
    if (t.found) {
      // Small tracks are read into memory; large ones play from the file.
      std::string bytes = std::move(t.theme.bytes);
      t.theme.bytes.clear();
      if (bytes.empty()) {
        long long size = fileSize(t.theme.file);
        if (size <= 0)
          t.found = false, t.theme.note = "the track could not be read";
        else if (size <= memoryTrackBytes)
          bytes = readWhole(t.theme.file);
      }
      if (!bytes.empty())
        t.bytes = std::make_shared<const std::string>(std::move(bytes));
    }
    // At most six tracks and 24 MB stay in memory: the launcher's heap is
    // shared with its artwork.
    size_t total = t.bytes ? t.bytes->size() : 0;
    for (auto &k : trackOrder)
      total += tracks[k].bytes ? tracks[k].bytes->size() : 0;
    while (!trackOrder.empty() && (trackOrder.size() >= 6 || total > (24u << 20))) {
      auto &old = tracks[trackOrder.front()];
      total -= old.bytes ? old.bytes->size() : 0;
      tracks.erase(trackOrder.front());
      trackOrder.erase(trackOrder.begin());
    }
    trackOrder.push_back(source);
    return &(tracks[source] = std::move(t));
  }

  static uint32_t millis() {
#ifdef __vita__
    return uint32_t(sceKernelGetProcessTimeWide() / 1000);
#else
    return SDL_GetTicks();
#endif
  }

  void loadLoop() {
    while (!quit) {
      // Nothing here may end the launcher: a track that fails is skipped.
      try {
        loadStep();
      } catch (const std::exception &e) {
        std::fprintf(stderr, "Launcher theme loader: %s\n", e.what());
        tracks.clear();
        trackOrder.clear();
        pause(50);
      } catch (...) {
        tracks.clear();
        trackOrder.clear();
        pause(50);
      }
    }
  }

  // One request, or one piece of background work.
  void loadStep() {
    {
      std::string want;
      bool change = false, fade = false;
      int ms = 0;
      std::vector<std::string> near;
      std::string next;
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
        near = nearby;
        useCache = cache;
        useRtps = rtps;
        if (!change && prepared < prepare.size() && int32_t(millis() - prepareFrom) >= 0)
          next = prepare[prepared++];
      }
      if (fade)
        fadeNow(ms);
      if (change) {
        // The old track goes at once; the new one comes in when it is read.
        fadeNow(250);
        std::unique_ptr<Stream> stream;
        if (!want.empty()) {
          Track *t = track(want);
          if (t->found) {
            stream = openStream(t->bytes, t->theme.ext, t->theme.file);
            if (stream) {
              stream->looping = true;
              // The game's own volume, softened: a quiet title still plays.
              stream->volume = 0.55f + 0.45f * float(std::max(0, std::min(100, t->theme.volume))) / 100.0f;
              stream->begin(t->theme.pitch);
              stream->name = t->theme.name;
            }
          }
        }
        {
          std::lock_guard<std::mutex> lock(asking);
          if (wanted != want)
            return; // moved on while this one loaded
        }
        std::lock_guard<std::mutex> lock(mixing);
        incoming = std::move(stream);
        switching = true;
        return;
      }
      // Idle: read the neighbours' tracks, then find every game's track
      // (kept in the theme cache), so the music is ready when asked for.
      bool worked = false;
      for (auto &n : near)
        if (!n.empty() && !tracks.count(n)) {
          track(n);
          worked = true;
          break;
        }
      if (!worked && !next.empty()) {
        Theme t;
        findTheme(next, t, useCache, useRtps);
        worked = true;
      }
      // Never hold the core: the art loader shares it.
      pause(worked ? 2 : 20);
    }
  }

  bool loadEffects(const std::string &folder) {
    bool any = false;
    for (int i = 0; i < int(Sound::Count); ++i) {
      auto s = openStream(std::make_shared<const std::string>(readWhole(folder + "/" + soundName(Sound(i)) + ".ogg")), ".ogg");
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
  themeInit();
  impl->loadEffects(folder);
  impl->quit = false;
  impl->loaded.clear();
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
  // Below the interface and the art loader: finding tracks can wait.
  impl->loader = sceKernelCreateThread("hardrpg_audio_load", Impl::loaderEntry, 0x10000100 + 20, 256 * 1024, 0,
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
  impl->playing.clear();
  // Memory goes back to the game; the theme cache on disk stays.
  impl->tracks.clear();
  impl->trackOrder.clear();
}

void LauncherAudio::play(Sound s) {
  if (!impl->running || impl->effects[int(s)].empty() || impl->effectsLevel <= 0)
    return;
  std::lock_guard<std::mutex> lock(impl->mixing);
  if (impl->voices.size() >= 8)
    impl->voices.erase(impl->voices.begin());
  impl->voices.push_back({int(s), 0});
}

void LauncherAudio::theme(const std::string &source) {
  std::lock_guard<std::mutex> lock(impl->asking);
  impl->wanted = source;
}

void LauncherAudio::search(const std::string &cache, const std::vector<std::string> &rtps) {
  std::lock_guard<std::mutex> lock(impl->asking);
  impl->cache = cache;
  impl->rtps = rtps;
}

void LauncherAudio::prepare(const std::vector<std::string> &sources) {
  std::lock_guard<std::mutex> lock(impl->asking);
  if (sources == impl->prepare)
    return;
  impl->prepare = sources;
  impl->prepared = 0;
  impl->prepareFrom = Impl::millis() + 4000;
}

void LauncherAudio::nearby(const std::vector<std::string> &sources) {
  std::lock_guard<std::mutex> lock(impl->asking);
  impl->nearby = sources;
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
