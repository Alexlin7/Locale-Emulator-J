// src/LEInstaller/Strings.h
// LEInstaller's own text in English, Traditional Chinese, Simplified Chinese and Japanese.
// The Shell Extension window it opens (LEGUI --installer) has LEGUI's 22 languages.
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace LeInstaller
{

enum class Lang
{
    En,
    ZhTw,
    ZhCn,
    Ja
};

// Chooses from a Windows UI language id; anything other than Chinese or Japanese is English.
// zh-TW / zh-HK / zh-MO / zh-Hant -> ZhTw, zh-CN / zh-SG / zh-Hans -> ZhCn.
Lang PickLanguage(LANGID uiLanguage);

// "{}" marks where a value is inserted (std::vformat).
struct Strings
{
    const wchar_t* title;
    const wchar_t* missingMain;
    const wchar_t* missingContent;
    const wchar_t* itemX86;
    const wchar_t* itemX64;
    const wchar_t* buttonAutoInstall;  // command link: "label\nnote"
    const wchar_t* buttonOpenPage;     // command link: "label\nnote"
    const wchar_t* buttonOpenPageShort;
    const wchar_t* downloadingMain;    // {} = x86 / x64
    const wchar_t* downloadingContent;
    const wchar_t* errorDownload;      // {} = error code
    const wchar_t* errorSignature;
    const wchar_t* errorInstall;       // {} = exit code
    const wchar_t* error32BitWindows;
    const wchar_t* errorFilesMissing;
    const wchar_t* errorLaunch;        // {} = error code
};

const Strings& GetStrings(Lang lang);

}  // namespace LeInstaller
