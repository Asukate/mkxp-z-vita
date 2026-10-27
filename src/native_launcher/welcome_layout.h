#pragma once
// Where the welcome guide draws a slide, its language rows and its two
// library view previews; touch uses the same boxes.
namespace hardrpg {
namespace welcomeLayout {
constexpr int slideX = 100, slideY = 80, slideW = 760, slideH = 402;
constexpr int textX = 30, textW = 700;
constexpr int langTop = 150, langRowH = 52, langBox = 46;
constexpr int dotsY = 486;
// Example folder lists (games, RTP): a header row, then rows that drop in.
constexpr int treeHeaderH = 36, treeRowH = 28, treeIndent = 34;
// Library view slide: carousel, grid and list side by side, a label under each.
constexpr int viewTop = 110, viewW = 220, viewH = 180, viewLabelH = 44, viewGap = 20;
constexpr int viewX(int i) { return textX + i * (viewW + viewGap); }
} // namespace welcomeLayout
} // namespace hardrpg
