#pragma once
// The launcher's languages. Every user-facing text is written in English in
// the code and looked up here when shown; a text with no translation shows
// in English. The table is in i18n.cpp.
#include <initializer_list>
#include <string>
#include <vector>

namespace hardrpg {
enum Language { English, Spanish, Portuguese, languageCount };
int language();
void setLanguage(int);
// The language's name in itself ("Español"), and the code saved in the
// preferences ("es").
const char *languageName(int);
const char *languageCode(int);
// The console's language when the launcher has it, otherwise English.
int systemLanguage();
// -1 when the code is not a language the launcher has.
int languageFromCode(const std::string &);
// The text in the current language; the English text when it has none.
const char *tr(const char *english);
std::string tr(const std::string &english);
// tr for a text that needs a different translation in one place: the table
// row "context|english" when there is one, otherwise tr(english).
const char *trc(const char *context, const char *english);
// tr, then each "{}" replaced by the next argument.
std::string trf(const char *english, std::initializer_list<std::string> args);
// For tests: every English text in the table with its translation in lang
// (empty when there is none).
std::vector<std::pair<std::string, std::string>> translations(int lang);
} // namespace hardrpg
