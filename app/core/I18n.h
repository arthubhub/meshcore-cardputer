#pragma once
// User interface language. English by default, French selectable in Settings > Language.
//
//   app.notify(tr("Contact deleted", "Contact supprimé"), ...);
//
// Both versions of every text stay side by side at the call site: easy to review, nothing to keep
// in sync elsewhere, no allocation. tr() returns one of its two arguments (string literals).
#include <stdint.h>

enum Lang : uint8_t { LANG_EN = 0, LANG_FR = 1, LANG_COUNT };

namespace i18n {
extern Lang current;
inline void set(uint8_t lang) { current = lang < LANG_COUNT ? (Lang)lang : LANG_EN; }
inline bool french() { return current == LANG_FR; }
// Name of a language, in that language (shown in the settings).
inline const char* name(uint8_t lang) { return lang == LANG_FR ? "Français" : "English"; }
}  // namespace i18n

inline const char* tr(const char* en, const char* fr) { return i18n::current == LANG_FR ? fr : en; }
