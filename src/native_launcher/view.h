#pragma once
#include "art_loader.h"
#include "image.h"
#include "ui.h"
#include <array>
#include <chrono>
#include <ft2build.h>
#include <map>
#include <memory>
#include <vector>
#include FT_FREETYPE_H

namespace hardrpg {
class View {
public:
  static constexpr int width = 960, height = 544;
  explicit View(const std::string &font);
  ~View();
  View(const View &) = delete;
  std::vector<uint8_t> pixels;
  void draw(const UI &);
  void splash(const std::string &png);
  void save(const std::string &png) const;
  // True while a home screen crossfade is still running.
  bool animating() const;
  // The home screen as quads: images (or solid colours) placed on screen.
  // The Vita draws them with the GPU; otherwise draw() composites them here.
  struct Quad {
    const Image *image = nullptr; // null: a solid colour
    float x = 0, y = 0, w = 0, h = 0;
    float alpha = 1, bright = 1, radius = 0;
    std::array<int, 3> color{{0, 0, 0}};
    int clipTop = 0, clipBottom = height; // rows outside are not drawn
  };
  std::vector<Quad> scene;
  bool gpu = false;      // the presenter draws scene; pixels hold nothing
  bool pendingLoads = false; // art still decoding; draw again next frame
  // Ends a running crossfade at once (snapshots and tests).
  void settle() { fading = false; }

private:
  using Color = std::array<int, 3>;
  std::string fontData;
  FT_Library library = nullptr;
  FT_Face face = nullptr;
  // Rajdhani Bold for titles on the home screen; falls back to the body font.
  std::string displayData;
  FT_Face display = nullptr;
  std::string assets;
  // Everything draws into this surface: the frame, or a cover being built.
  uint8_t *surface = nullptr;
  int surfaceW = width, surfaceH = height;
  struct Art {
    Image cover, hero, logo, label, labelSelected;
    // Loaded means looked for: the image stays empty when there is no art.
    bool coverLoaded = false, heroLoaded = false;
    bool generate = false; // no cover art: draw one here (FreeType is not thread-safe)
    std::string generatedPath; // where the loader caches it
    std::string info;          // downloaded facts (info.json), parsed by facts()
    uint64_t used = 0;     // the last frame that drew this game
    std::string labelKey;
  };
  std::unique_ptr<ArtLoader> loader;
  uint64_t frameNumber = 0;
  std::vector<std::string> heroOrder; // decoded backgrounds, oldest first
  bool blurShown = true;              // the blur preference the art was loaded with
  // Text and icons, drawn on the CPU only when their content changes.
  struct Layer {
    Image image;
    std::string key;
  };
  Layer topLayer, toolbarLayer, captionLayer, detailsLayer, hintsLayer, panelLayer, messageLayer, addLayer, frameLayer;
  Image scrimV, scrimH, tint, band, glow;
  const Image *heroShown = nullptr, *heroFading = nullptr;
  std::string heroKey;
  int loadBudget = 0;
  bool beginLayer(Layer &, const std::string &key, int w, int h);
  void endLayer();
  void buildHome(const UI &);
  void composite();
  std::map<std::string, Art> art;
  std::map<std::string, Image> icons;
  std::chrono::steady_clock::time_point fadeStart;
  bool fading = false;
  const Image &icon(const std::string &name);
  // distance: how far from the selection; nearer art loads first.
  Art &artFor(const UI &, const Entry &, double distance, bool needHero = false);
  void receiveArt();
  void trimArt();
  void blit(const Image &, int x, int y, int alpha = 255);
  void blitScaled(const Image &, double x, double y, double w, double h, int alpha = 255, int brightness = 256, int radius = 0);
  void rect(int x, int y, int w, int h, Color, int alpha, int radius = 0);
  int measure(const std::string &, int size, bool display = false);
  // Draws text with its top-left at x,y; align -1 left, 0 centre, 1 right.
  void label(int x, int y, const std::string &, int size, Color, int alpha = 255, bool display = false, int align = -1, int maxWidth = 0);
  struct Hint {
    const char *glyph; // a ui/ icon, or plain text drawn as a key badge
    std::string text;
    Button button;
  };
  void ensureStrips();
  void topBar(const UI &, int focus);
  void hintBar(const UI &, const std::vector<Hint> &left, const std::vector<Hint> &right, const std::string &counter);
  // Every screen other than home: a header, rows and notes (sheet_view.cpp).
  void buildSheet(const UI &);
  std::vector<std::string> wrap(const std::string &, int size, int width, bool display = false);
  std::map<std::string, Layer> rowCache;
  Layer headerLayer, notesLayer, bodyLayer;
  void homePanels(const UI &, const std::vector<size_t> &list);
  void homeHints(const UI &, const std::vector<size_t> &list);
  struct Glyph {
    int left, top, advance, width, height;
    std::vector<uint8_t> mask;
  };
  std::map<std::pair<int, FT_UInt>, Glyph> glyphCache;
  size_t glyphBytes = 0;
  const Glyph &glyph(int size, FT_UInt index, bool alt = false);
};
} // namespace hardrpg
