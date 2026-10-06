#pragma once
// Update this metadata with each tagged release. UTC avoids locale ambiguity.
#define HARDRPG_VERSION "0.2.1 Alpha"
#define HARDRPG_APP_VER "00.21"
#define HARDRPG_RELEASE_UTC "2026-10-06 19:48 UTC"
namespace hardrpg {
static const char *const releaseNotes[] = {
    "0.2.1 Alpha - " HARDRPG_RELEASE_UTC,
    "- Native launcher added",
    "- Game search added",
    "- Engine filters added",
    "- ZIP games show their engine",
    "- Library scrollbar added",
    "- Touch scrolling added",
    "- Settings layout tightened",
    "- Inactive selection highlights fixed",
    "- RTP detection made faster",
    "- Timestamped game error history added",
    "- Error report exports added",
    "- Full library paths shown",
    "- Starting game message added",
    "- Game display names can be changed",
    "- Sleep and resume fixed",
    "- Window decorations restored",
    "- HP/MP bar rendering fixed",
    "- Combat performance improved",
    "- Menu loading made faster",
    "- Disappearing windows fixed",
    "- Image allocation recovery improved",
    "- Image errors include backtraces",
    "- Font glyphs cached",
    "- Bubble artwork added",
    "- Splash shown once per app boot",
    "- Shared RTP locations added",
    "- Extra game folders supported",
    "- ZIP game caches reused",
    "",
    "0.1 Alpha - 2026-10-01 15:37 UTC",
    "- Initial RPG Maker XP/VX/VX Ace release"};
}
