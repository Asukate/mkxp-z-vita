// Exercise the production MIDI parser, sequencer and synth with a captured
// OpenAL upload. Only the engine/display context is substituted for the host.
#include <SDL.h>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#define CONFIG_H
struct Config {
  struct {
    std::string soundFont;
    bool chorus = false, reverb = false;
  } midi;
};
#define DEBUGWRITER_H
struct QuietLog {
  template <class T> QuietLog &operator<<(const T &) { return *this; }
};
inline QuietLog Debug() { return {}; }
#include "audio/sharedmidistate.h"
#define SHAREDSTATE_H
struct TestState {
  SharedMidiState &m;
  SharedMidiState &midiState() { return m; }
};
static TestState *shState;
#define ALUTIL_H
#define ALDATASOURCE_H
#define STREAM_BUF_SIZE 32768
#define AL_FORMAT_STEREO16 1
static std::vector<int16_t> pcm;
namespace AL {
namespace Buffer {
struct ID {};
inline void uploadData(ID, int, const void *data, int bytes, int rate) {
  assert(rate == 44100);
  auto p = static_cast<const int16_t *>(data);
  pcm.assign(p, p + bytes / 2);
}
} // namespace Buffer
} // namespace AL
struct ALDataSource {
  enum Status { NoError, EndOfStream, WrapAround, Error };
  virtual ~ALDataSource() = default;
  virtual Status fillBuffer(AL::Buffer::ID) = 0;
  virtual int sampleRate() = 0;
  virtual void seekToOffset(double) = 0;
  virtual uint32_t loopStartFrames() = 0;
  virtual bool setPitch(float) = 0;
};
#include "../src/audio/midisource.cpp"
static void be(std::vector<uint8_t> &v, unsigned n, int count) {
  for (int i = count - 1; i >= 0; --i)
    v.push_back((n >> (i * 8)) & 255);
}
static std::vector<uint8_t>
song(bool aftertouch = false, bool tempoZero = false, unsigned division = 480) {
  std::vector<uint8_t> track = {0, 0xff, 0x51, 3, 7,    0xa1, 0x20,
                                0, 0xc0, 0,    0, 0x90, 60,   100};
  if (tempoZero)
    track[4] = track[5] = track[6] = 0;
  if (aftertouch)
    track.insert(track.end(), {0, 0xa0, 60, 50});
  track.insert(track.end(), {0x83, 0x60, 0x80, 60, 0, 0, 0xff, 0x2f, 0});
  std::vector<uint8_t> data = {'M', 'T', 'h', 'd'};
  be(data, 6, 4);
  be(data, 0, 2);
  be(data, 1, 2);
  be(data, division, 2);
  data.insert(data.end(), {'M', 'T', 'r', 'k'});
  be(data, track.size(), 4);
  data.insert(data.end(), track.begin(), track.end());
  return data;
}
static std::unique_ptr<ALDataSource> source(std::vector<uint8_t> &data,
                                            bool loop) {
  auto ops = SDL_RWFromConstMem(data.data(), int(data.size()));
  assert(ops);
  return std::unique_ptr<ALDataSource>(createMidiSource(*ops, loop));
}
int main(int argc, char **argv) {
  assert(argc == 2);
  Config conf;
  conf.midi.soundFont = argv[1];
  SharedMidiState midi(conf);
  TestState test{midi};
  shState = &test;
  midi.initIfNeeded(conf);
  assert(HAVE_FLUID && midi.synths.empty());
  auto data = song(true);
  auto music = source(data, true);
  assert(midi.synths.size() == 1);
  music->fillBuffer({});
  long peak = 0;
  for (auto v : pcm)
    peak = std::max(peak, std::abs(long(v)));
  assert(peak > 100);
  for (int i = 0; i < 12; ++i)
    assert(music->fillBuffer({}) == ALDataSource::NoError);
  music->seekToOffset(0);
  music->fillBuffer({});
  assert(!pcm.empty());
  assert(music->setPitch(1.1f));
  // A second stream (for example ME over BGM) gets a separate synth.
  auto second = source(data, false);
  assert(midi.synths.size() == 2);
  second->fillBuffer({});
  second.reset();
  music.reset();
  assert(!midi.synths[0].inUse);
  // Format 1 with an empty second track remains safe and audible.
  auto multi = data;
  multi[9] = 1;
  multi[11] = 2;
  multi.insert(multi.end(), {'M', 'T', 'r', 'k', 0, 0, 0, 4, 0, 0xff, 0x2f, 0});
  auto layered = source(multi, true);
  layered->fillBuffer({});
  layered.reset();
  // GM percussion on channel ten and velocity-zero note-off.
  auto drums = song();
  for (size_t i = 14; i < drums.size(); ++i) {
    if (drums[i] == 0x90) {
      drums[i] = 0x99;
      drums[i + 1] = 38;
    }
    if (drums[i] == 0x80) {
      drums[i] = 0x99;
      drums[i + 1] = 38;
      drums[i + 2] = 0;
    }
  }
  auto kit = source(drums, false);
  kit->fillBuffer({});
  peak = 0;
  for (auto v : pcm)
    peak = std::max(peak, std::abs(long(v)));
  assert(peak > 100);
  kit.reset();
  auto once = source(data, false);
  bool ended = false;
  for (int i = 0; i < 10; ++i)
    if (once->fillBuffer({}) == ALDataSource::EndOfStream) {
      ended = true;
      break;
    }
  assert(ended);
  once.reset();
  for (auto invalid :
       {song(false, true), song(false, false, 0), song(false, false, 0x8000),
        std::vector<uint8_t>{'M', 'T'}}) {
    bool rejected = false;
    try {
      auto x = source(invalid, false);
    } catch (const Exception &) {
      rejected = true;
    }
    assert(rejected);
  }
  Config bad;
  bad.midi.soundFont = "/missing/default.sf2";
  SharedMidiState missing(bad);
  missing.initIfNeeded(bad);
  bool rejected = false;
  try {
    missing.allocateSynth();
  } catch (const Exception &) {
    rejected = true;
  }
  assert(rejected && missing.synths.empty());
  std::cout << "PASS: lazy MIDI allocation, audible PCM, aftertouch parsing, "
               "loops, replay, pitch, end, malformed input and missing bank\n";
}
