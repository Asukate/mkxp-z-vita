#include "util/win32-text.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace hardrpg::win32text;

int main() {
  // WF-RGSS String.utf82ansi: size query, then convert, both ways.
  const char *jp = "\xe3\x82\xa8\xe3\x83\xa9\xe3\x83\xbc A\xf0\x9f\x98\x80"; // "エラー A😀"
  long units = multiByteToWideChar(jp, std::strlen(jp) + 1, -1, nullptr, 0);
  assert(units == 3 + 2 + 2 + 1); // three kana, space, A, a surrogate pair, terminator
  char16_t wide[16] = {};
  assert(multiByteToWideChar(jp, std::strlen(jp) + 1, -1, wide, units) == units);
  assert(wide[0] == 0x30a8 && wide[3] == u' ' && wide[5] == 0xd83d && wide[6] == 0xde00 && wide[7] == 0);
  long bytes = wideCharToMultiByte(wide, 16, -1, nullptr, 0);
  assert(bytes == long(std::strlen(jp) + 1));
  char back[32] = {};
  assert(wideCharToMultiByte(wide, 16, -1, back, bytes) == bytes);
  assert(std::strcmp(back, jp) == 0);

  // A destination too small gets nothing; an explicit length has no terminator.
  assert(multiByteToWideChar(jp, std::strlen(jp) + 1, -1, wide, 2) == 0);
  assert(multiByteToWideChar("abc", 4, 2, wide, 8) == 2 && wide[0] == u'a' && wide[1] == u'b');
  // The source never reads past its buffer, even without a terminator.
  assert(multiByteToWideChar("abcdef", 3, -1, nullptr, 0) == 4);
  assert(multiByteToWideChar(nullptr, 0, -1, nullptr, 0) == 0);

  // Bytes that are not UTF-8 survive as Latin-1, never dropped.
  std::u16string latin = utf8ToUtf16("caf\xe9 \xff\xe3\x82", 8);
  assert(latin == u"café ÿã\u0082");
  assert(utf8ToUtf16("\xc0\xaf", 2) == std::u16string(u"À¯")); // overlong
  char16_t lone[] = {0xd800, u'x'};
  assert(utf16ToUtf8(lone, 2) == "\xef\xbf\xbdx");

  // MessageBox answers and the exit report.
  assert(messageBoxAnswer(0x042010) == 1); // WF-RGSS MB_FATAL: OK only
  assert(messageBoxAnswer(0x4) == 0 && messageBoxAnswer(0x24) == 0); // Yes/No
  assert(reportMessageBoxOnExit(true, 300.0, 1));
  assert(reportMessageBoxOnExit(true, 3.0, 0));
  assert(!reportMessageBoxOnExit(true, 300.0, 0)); // a box long before a normal quit
  assert(!reportMessageBoxOnExit(false, 0.0, 1));
  std::string report = messageBoxReport("SMT", "undefined method `to_a'", 1);
  assert(report.find("exit status 1") != std::string::npos);
  assert(report.find("SMT\n\nundefined method `to_a'\n") != std::string::npos);
  assert(messageBoxReport("", "", 0).find("(The message was empty.)") != std::string::npos);
  std::cout << "win32 text checks passed\n";
}
