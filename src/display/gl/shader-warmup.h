#pragma once
// Compiles the engine's common shaders ahead of a game, once per build.
//
// The Vita compiles a shader the first time a game draws with it (0.3-0.7 s
// each). After an update changes the shader cache, that lands in the middle
// of play: the title, the first map, the first menu each freeze briefly. The
// launcher runs these steps behind a progress screen instead, so the cache is
// full before the game starts. Steps write the same cache the engine reads.
namespace ShaderWarmup {
// True when this build's shaders have not been prepared yet.
bool needed();
int count();
// What step i prepares, for the progress screen ("map tiles").
const char *label(int i);
// Compiles (or loads) step i. False if it failed; the game then compiles it
// on first use as before.
bool run(int i);
// Records that this build's shaders are prepared.
void finish();
} // namespace ShaderWarmup
