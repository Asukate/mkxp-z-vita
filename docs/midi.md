# MIDI playback on Vita

The Vita runtime statically links FluidLite 1.2.2 into mkxp-z's existing MIDI
sequencer and PCM audio stream. No external executable, DLL or MIDI service is
needed. Standard MIDI files of type 0 and 1 are supported, with tempo changes,
program changes, channel controllers, pitch bend and RPG Maker's CC 111 loop
marker. Type 2 and SMPTE timing are unsupported. MIDI files are limited to 8 MiB
and 256 tracks.

The default bank is the bundled TimGM6mb General MIDI SoundFont. It includes
melodic programs and percussion. To use a different bank, set `midiSoundFont`
in a game's existing mkxp configuration to an absolute SF2 path, for example
`ux0:/data/hardrpg/soundfonts/my-bank.sf2`. SoundFont files must be valid SF2
RIFF files no larger than 32 MiB. An invalid or missing bank produces a MIDI
error instead of silent playback. The initial implementation supports SF2;
compressed SF3 banks are not part of this release gate.

Synthesizers and sample memory are allocated when the first MIDI source opens,
with 64 voices per synth, a 44.1 kHz stereo output and effects disabled by
default. BGM and ME can use independent synths. Reusable synths retain their
bank until the game process ends. Games using other audio formats do not load
the bank. Triangle -> Audio / MIDI toggles audio, MIDI chorus and reverb for the
highlighted game. These changes apply on the next launch. Enabling effects
needs a separate performance check.

The bundled editable bank and its notices are in `assets/TimGM6mb.sf2` and
`LICENSES/TimGM6mb.txt`. FluidLite's source revision and license are listed in
`THIRD-PARTY-NOTICES.md`; the build receipt records the exact linked archive.

For host verification, run `scripts/test-midi-host.sh BUILD_DIRECTORY` with
CMake, a C++ compiler, SDL2 development headers and pkg-config. It builds the
same pinned FluidLite source and tests the production sequencer with captured
PCM and address/undefined-behavior sanitizers. For a hardware probe, run
`scripts/build-midi-probe.py OUTPUT_DIRECTORY` and add its generated game folder
to the library. Hardware gates include audible playback, loop boundaries,
BGM/ME transitions, pitch/volume/fade behavior, sleep/resume and sustained frame
time in a real MIDI game. Host PCM and a compiled VPK do not establish those
hardware results.
