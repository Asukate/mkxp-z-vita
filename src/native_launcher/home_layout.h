#pragma once
#include <algorithm>
#include <cmath>

// Geometry of the home screen, shared by drawing (view) and touch (ui).
namespace hardrpg {
namespace homeLayout {
constexpr int topHeight = 62;
// Top bar, right to left: settings gear, battery, clock, About.
constexpr int gearX = 896, aboutX = 696, topIconY = 2, topIcon = 58;
// Toolbar row: buttons 50 px, icons 31 px.
constexpr int barY = 66, barH = 50;
constexpr int viewX = 14, addX = 70, addW = 170, searchX = 840, filterX = 896, barIcon = 50;
constexpr int tabH = 40, tabPad = 38;
// Carousel.
constexpr int cardW = 154, cardH = 231, cardGap = 172, cardPush = 52;
constexpr int shelfX = 480, shelfY = 284, detailsShelfX = 182, detailsShelfY = 280;
constexpr double focusScale = 1.4, detailsScale = 1.25;
constexpr int captionY = 457;
// Grid.
constexpr int gridCols = 5, gridGapX = 22, gridGapY = 16, gridTop = 132, gridLabel = 22;
constexpr int gridX = (960 - (gridCols * cardW + (gridCols - 1) * gridGapX)) / 2;
// Details panel and its buttons.
constexpr int detailsX = 344, detailsY = 70;
constexpr int playX = 344, playY = 440, playW = 112, optionsX = 464, optionsW = 140, buttonH = 44;
// Side panel (filter) and options menu.
constexpr int panelX = 490, panelY = 120, panelW = 470, panelRowH = 33;
constexpr int menuX = 612, menuY = 150, menuW = 290, menuRowH = 44;
// Hint bar.
constexpr int hintsY = 502;
// Sheets (every screen but home): a header, then rows in one centred column.
// Seven rows show at a time; RTP rows are taller (two lines), browser rows
// shorter.
constexpr int sheetX = 130, sheetW = 700, sheetTitleY = 72, sheetTop = 151, sheetRowH = 42, sheetRows = 7;
constexpr int rtpRowH = 54, browserRowH = 40, browserRows = 8;
// Artwork chooser: a row per kind (cover, background, logo), thumbnails left
// to right with the source named underneath.
constexpr int pickerX = 168, pickerGap = 16, pickerLabel = 18;
inline void pickerSlot(int kind, int i, int &x, int &y, int &w, int &h) {
  static const int ws[3] = {80, 144, 144}, hs[3] = {120, 81, 54}, ys[3] = {118, 274, 391};
  w = ws[kind];
  h = hs[kind];
  x = pickerX + i * (w + pickerGap);
  y = ys[kind];
}

// Where card i sits when the carousel is at position p, blended between the
// centred shelf (amount 0) and the details layout (amount 1).
struct CardRect {
  double x, y, w, h;
  double nearness; // 1 for the focused card, 0 one card away or more
};
// side moves the shelf into the left half while the filter panel is open.
constexpr int sideShelfX = 245;
inline CardRect card(int i, double p, double amount, double side = 0) {
  double cx = shelfX + (detailsShelfX - shelfX) * amount, cy = shelfY + (detailsShelfY - shelfY) * amount;
  cx += (sideShelfX - cx) * side;
  double fs = focusScale + (detailsScale - focusScale) * amount;
  double d = i - p, ad = std::fabs(d), near = std::max(0.0, 1 - ad);
  double scale = 1 + (fs - 1) * near;
  double centre = cx + d * cardGap + (d > 0 ? 1 : d < 0 ? -1 : 0) * std::min(ad, 1.0) * cardPush;
  double w = cardW * scale, h = cardH * scale;
  return {centre - w / 2, cy + (1 - near) * 10 - h / 2, w, h, near};
}
inline bool inside(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}
} // namespace homeLayout
} // namespace hardrpg
