#pragma once

#include <filesystem>
#include <string>
#include <string_view>

enum class AppLanguage { German, English };

AppLanguage currentLanguage() noexcept;
void setCurrentLanguage(AppLanguage language) noexcept;
bool loadLanguagePreference();
bool saveLanguagePreference();

// German is the canonical source language. Unknown technical text is kept
// intact, while known UI labels and messages are translated at presentation.
std::string localizedText(std::string_view german);
const char* localizedCString(const char* german);
std::wstring localizedWide(std::wstring_view german, std::wstring_view english);
