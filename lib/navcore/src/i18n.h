// User interface language (Polish / English), switchable at run time (Menu > Display > Language).
// Every user-visible text is written as TR("polski", "English"): both versions sit next to each
// other in the source, so a text cannot be forgotten in one language.
#pragma once
#include <cstdint>

namespace nav {

enum class Lang : uint8_t { PL = 0, EN = 1 };
inline Lang gLang = Lang::PL;
inline void setLang(Lang l) { gLang = l; }
inline Lang lang() { return gLang; }
inline bool langEn() { return gLang == Lang::EN; }
inline const char* tr(const char* pl, const char* en) { return gLang == Lang::EN ? en : pl; }

}  // namespace nav

#define TR(pl, en) (::nav::tr(pl, en))
