#pragma once
/* Win32 text calls answered on the Vita, where no Windows DLL exists.
 *
 * Japanese script bases (WF-RGSS among them) pass every message through
 * MultiByteToWideChar / WideCharToMultiByte before MessageBoxA, and Windows
 * games read their own text with them. Game text is UTF-8 here, so every code
 * page is treated as UTF-8; a byte that is not UTF-8 is read as Latin-1, so
 * no text is lost. Lengths follow Win32: a source length of -1 means "up to
 * and including the terminator", a destination size of 0 asks for the size
 * needed, and a destination that is too small gets nothing and returns 0. */
#include <cstddef>
#include <cstdint>
#include <string>

namespace hardrpg {
namespace win32text {

inline void appendUtf16(std::u16string &out, uint32_t cp) {
  if (cp >= 0x10000) {
    cp -= 0x10000;
    out.push_back(char16_t(0xd800 + (cp >> 10)));
    out.push_back(char16_t(0xdc00 + (cp & 0x3ff)));
  } else {
    out.push_back(char16_t(cp));
  }
}

inline std::u16string utf8ToUtf16(const char *text, size_t size) {
  const unsigned char *s = reinterpret_cast<const unsigned char *>(text);
  std::u16string out;
  size_t i = 0;
  while (i < size) {
    unsigned char c = s[i];
    int extra = c >= 0xf0 && c < 0xf5 ? 3 : c >= 0xe0 ? 2 : c >= 0xc2 && c < 0xe0 ? 1 : 0;
    if (c >= 0xf5) extra = 0;
    uint32_t cp = c;
    bool ok = c < 0x80 || extra > 0;
    if (extra > 0) {
      if (i + extra < size) {
        cp = c & (0x3f >> extra);
        for (int k = 1; k <= extra; ++k) {
          if ((s[i + k] & 0xc0) != 0x80) { ok = false; break; }
          cp = (cp << 6) | (s[i + k] & 0x3f);
        }
        // Overlong forms, surrogates and values past U+10FFFF are not UTF-8.
        if (ok && ((extra == 2 && cp < 0x800) || (extra == 3 && cp < 0x10000) ||
                   (cp >= 0xd800 && cp <= 0xdfff) || cp > 0x10ffff))
          ok = false;
      } else {
        ok = false;
      }
    }
    if (!ok) { appendUtf16(out, c); ++i; continue; } // Latin-1 byte
    appendUtf16(out, cp);
    i += extra + 1;
  }
  return out;
}

inline std::string utf16ToUtf8(const char16_t *text, size_t units) {
  std::string out;
  for (size_t i = 0; i < units; ++i) {
    uint32_t cp = text[i];
    if (cp >= 0xd800 && cp <= 0xdbff && i + 1 < units && text[i + 1] >= 0xdc00 && text[i + 1] <= 0xdfff) {
      cp = 0x10000 + ((cp - 0xd800) << 10) + (text[i + 1] - 0xdc00);
      ++i;
    } else if (cp >= 0xd800 && cp <= 0xdfff) {
      cp = 0xfffd; // unpaired surrogate
    }
    if (cp < 0x80) {
      out.push_back(char(cp));
    } else if (cp < 0x800) {
      out.push_back(char(0xc0 | (cp >> 6)));
      out.push_back(char(0x80 | (cp & 0x3f)));
    } else if (cp < 0x10000) {
      out.push_back(char(0xe0 | (cp >> 12)));
      out.push_back(char(0x80 | ((cp >> 6) & 0x3f)));
      out.push_back(char(0x80 | (cp & 0x3f)));
    } else {
      out.push_back(char(0xf0 | (cp >> 18)));
      out.push_back(char(0x80 | ((cp >> 12) & 0x3f)));
      out.push_back(char(0x80 | ((cp >> 6) & 0x3f)));
      out.push_back(char(0x80 | (cp & 0x3f)));
    }
  }
  return out;
}

/* Bytes of a narrow source the call covers: up to its terminator (counted)
 * for -1, otherwise srcLen, never past the buffer. */
inline size_t narrowSpan(const char *src, size_t available, long srcLen) {
  if (srcLen >= 0) return size_t(srcLen) < available ? size_t(srcLen) : available;
  size_t n = 0;
  while (n < available && src[n]) ++n;
  return n; // the terminator is added by the callers
}

inline size_t wideSpan(const char16_t *src, size_t available, long srcLen) {
  if (srcLen >= 0) return size_t(srcLen) < available ? size_t(srcLen) : available;
  size_t n = 0;
  while (n < available && src[n]) ++n;
  return n;
}

/* MultiByteToWideChar. dst may be null when dstUnits is 0. */
inline long multiByteToWideChar(const char *src, size_t srcBytes, long srcLen,
                                char16_t *dst, size_t dstUnits) {
  if (!src) return 0;
  std::u16string wide = utf8ToUtf16(src, narrowSpan(src, srcBytes, srcLen));
  if (srcLen < 0) wide.push_back(u'\0');
  if (wide.empty()) return 0;
  if (dstUnits == 0) return long(wide.size());
  if (!dst || dstUnits < wide.size()) return 0;
  for (size_t i = 0; i < wide.size(); ++i) dst[i] = wide[i];
  return long(wide.size());
}

/* WideCharToMultiByte. dst may be null when dstBytes is 0. */
inline long wideCharToMultiByte(const char16_t *src, size_t srcUnits, long srcLen,
                                char *dst, size_t dstBytes) {
  if (!src) return 0;
  std::string narrow = utf16ToUtf8(src, wideSpan(src, srcUnits, srcLen));
  if (srcLen < 0) narrow.push_back('\0');
  if (narrow.empty()) return 0;
  if (dstBytes == 0) return long(narrow.size());
  if (!dst || dstBytes < narrow.size()) return 0;
  for (size_t i = 0; i < narrow.size(); ++i) dst[i] = narrow[i];
  return long(narrow.size());
}

/* MessageBox answer: OK for a box whose only button is OK (MB_OK), so the
 * game carries on as if the player pressed it. Boxes that ask a question
 * keep the old answer (0, "the call failed"): guessing Yes or Retry could
 * loop or quit a game. */
inline int messageBoxAnswer(unsigned long type) {
  return (type & 0xf) == 0 ? 1 /* IDOK */ : 0;
}

/* A game that shows a Windows message box and then quits almost always
 * reported an error in it. Keep it when the game quit with a failure status,
 * or right after the box. */
inline bool reportMessageBoxOnExit(bool shown, double secondsSinceBox, int exitStatus) {
  return shown && (exitStatus != 0 || secondsSinceBox <= 15.0);
}

inline std::string messageBoxReport(const std::string &caption, const std::string &text,
                                    int exitStatus) {
  std::string out = "The game closed after showing this message";
  out += exitStatus != 0 ? " (exit status " + std::to_string(exitStatus) + ")" : "";
  out += ".\nIt was a Windows message box, which the Vita cannot display.\n\n";
  if (!caption.empty()) out += caption + "\n\n";
  out += text.empty() ? std::string("(The message was empty.)") : text;
  out += "\n";
  return out;
}

} // namespace win32text
} // namespace hardrpg
