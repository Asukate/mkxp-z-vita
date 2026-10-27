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
    int clipLeft = 0, clipRight = width;  // and columns
  };
  std::vector<Quad> scene;
  // The in-game performance overlay: a small rounded panel with a headline
  // (the frame rate) and rows of name and value. tone: 0 plain, 1 good, 2
  // warning, 3 bad. Drawn again only when its text changes.
  struct StatRow {
    std::string name, value;
    int tone = 0;
  };
  const Image &statsPanel(const std::string &headline, int tone, const std::vector<StatRow> &rows, int width);
  bool gpu = false;      // the presenter draws scene; pixels hold nothing
  // Drawn over a running game: sheets keep a see-through background and
  // pixels start transparent (premultiplied alpha).
  bool overlay = false;
  bool pendingLoads = false; // art still decoding (the splash waits for it)
  bool coversToMake = false; // generated covers still to draw, one a frame
  // Ends a running crossfade at once (snapshots and tests).
  void settle() { fading = false; }
  // Whether the picture loader is running (the list view stops it).
  bool artLoaderActive() const { return bool(loader); }

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
    bool coverLoaded = false, heroLoaded = false, shotsLoaded = false;
    std::vector<Image> shots; // screenshot thumbnails, for the details panel
    Image viewed;             // one screenshot, large, for the viewer
    int viewedShot = 0;       // which one (1-4), 0 none
    bool generate = false; // no cover art: draw one here (FreeType is not thread-safe)
    std::string generatedPath; // where the loader caches it
    std::string info;          // downloaded facts (info.json), parsed by facts()
    uint64_t used = 0;     // the last frame that drew this game
    std::string labelKey;
  };
  std::unique_ptr<ArtLoader> loader;
  uint64_t frameNumber = 0;
  std::vector<std::string> heroOrder; // decoded backgrounds, oldest first
  bool blurShown = false;             // the blur preference the art was loaded with
  // Text and icons, drawn on the CPU only when their content changes.
  struct Layer {
    Image image;
    std::string key;
  };
  Layer topLayer, toolbarLayer, captionLayer, detailsLayer, buttonsLayer, viewerLayer, hintsLayer, panelLayer, messageLayer, addLayer, frameLayer;
  Image scrimV, scrimH, tint, band, glow;
  // The tint's two visible ends (top 70 px, bottom 56 px): drawn as strips,
  // the GPU does not shade the clear middle of the screen.
  Image tintTop, tintBottom;
  // Backgrounds with no transparent pixels need nothing drawn under them.
  std::map<uint64_t, bool> opaqueHeroes; // by Image::id
  bool opaque(const Image *img);
  Image vitaBack; // the console's outline, for the rear pad page
  Layer statsLayer;
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
  void receiveArt(const UI &u);
  void trimArt();
  void blit(const Image &, int x, int y, int alpha = 255);
  void blitScaled(const Image &, double x, double y, double w, double h, int alpha = 255, int brightness = 256, int radius = 0);
  void rect(int x, int y, int w, int h, Color, int alpha, int radius = 0);
  int measure(const std::string &, int size, bool display = false);
  // Button tokens in text ("<cross>", "<l>"): their width, and drawing one.
  int tokenWidth(const void *token, int size);
  void drawToken(const void *token, int x, int y, int size, int alpha);
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
  // The welcome guide (welcome.cpp): the screen, and one slide as a layer.
  void buildWelcome(const UI &);
  const Image &welcomeSlide(const UI &, int slide);
  void welcomeMotion(const UI &, int slide, double ox, double alpha, double clock, bool behind);
  std::map<std::string, Layer> welcomeCache;
  std::map<std::string, int> welcomeExtra; // where a slide's moving part sits
  void homePanels(const UI &, const std::vector<size_t> &list);
  // The list view: each game's name as a small layer, made when its row is
  // first shown and forgotten once many rows have scrolled away.
  struct ListRow {
    Layer layer;
    uint64_t used = 0;
    bool cut = false; // the name did not fit and ends in "..."
  };
  // The selected row's name, when cut, slides along to show the rest: the
  // whole name as one layer, and when that row was selected.
  Layer marqueeLayer;
  std::string marqueeId;
  std::chrono::steady_clock::time_point marqueeStart;
  bool marqueeMoving = false;
  uint64_t marqueeFrame = 0; // the last frame that showed it
  std::map<std::string, ListRow> listNames;
  Layer listAddLayer, listTagLayers[formatMZ + 1];
  void buildList(const UI &, const std::vector<size_t> &list, int target);
  template <class AddQuad>
  bool marquee(const std::string &id, const std::string &name, int maxW, int x, double y, int h, AddQuad &&quad);
  // Forgets every picture and stops the picture loader (the list view).
  void dropArt();
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
