// tests/LEInstaller.Tests/StringsAndSignatureTests.cpp
// LEInstaller's language choice, string tables and Authenticode check.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <stdio.h>

#include <filesystem>
#include <string>
#include <string_view>

#include <atomic>

#include "Download.h"
#include "RuntimeCheck.h"
#include "Signature.h"
#include "Strings.h"

namespace fs = std::filesystem;
using namespace LeInstaller;

void Check(bool condition, const wchar_t* name);  // main.cpp

namespace
{

size_t CountPlaceholders(std::wstring_view s)
{
    size_t n = 0;
    for (size_t pos = s.find(L"{}"); pos != std::wstring_view::npos; pos = s.find(L"{}", pos + 2))
        ++n;
    return n;
}

void TestPickLanguage()
{
    Check(PickLanguage(MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL)) == Lang::ZhTw, L"PickLanguage: zh-TW");
    Check(PickLanguage(MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_HONGKONG)) == Lang::ZhTw, L"PickLanguage: zh-HK");
    Check(PickLanguage(MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_MACAU)) == Lang::ZhTw, L"PickLanguage: zh-MO");
    Check(PickLanguage(0x7C04) == Lang::ZhTw, L"PickLanguage: zh-Hant");
    Check(PickLanguage(MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED)) == Lang::ZhCn, L"PickLanguage: zh-CN");
    Check(PickLanguage(MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SINGAPORE)) == Lang::ZhCn, L"PickLanguage: zh-SG");
    Check(PickLanguage(0x0004) == Lang::ZhCn, L"PickLanguage: zh-Hans");
    Check(PickLanguage(MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN)) == Lang::Ja, L"PickLanguage: ja-JP");
    Check(PickLanguage(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US)) == Lang::En, L"PickLanguage: en-US");
    Check(PickLanguage(MAKELANGID(LANG_KOREAN, SUBLANG_KOREAN)) == Lang::En, L"PickLanguage: ko-KR falls back to English");
    Check(PickLanguage(MAKELANGID(LANG_GERMAN, SUBLANG_GERMAN)) == Lang::En, L"PickLanguage: de-DE falls back to English");
}

void TestStringTables()
{
    // Every language fills every field, with the same "{}" placeholders as English,
    // so std::vformat never throws for a missing argument.
    constexpr size_t kFields = sizeof(Strings) / sizeof(const wchar_t*);
    const auto* english = reinterpret_cast<const wchar_t* const*>(&GetStrings(Lang::En));

    for (Lang lang : {Lang::En, Lang::ZhTw, Lang::ZhCn, Lang::Ja})
    {
        const auto* fields = reinterpret_cast<const wchar_t* const*>(&GetStrings(lang));
        bool filled = true;
        bool sameArgs = true;
        for (size_t i = 0; i < kFields; ++i)
        {
            if (!fields[i] || !fields[i][0])
                filled = false;
            else if (CountPlaceholders(fields[i]) != CountPlaceholders(english[i]))
                sameArgs = false;
        }
        const std::wstring name = L"Strings: language " + std::to_wstring(static_cast<int>(lang));
        Check(filled, (name + L" has every field").c_str());
        Check(sameArgs, (name + L" has the same placeholders as English").c_str());
    }

    // Command links put the note after the first line break.
    for (Lang lang : {Lang::En, Lang::ZhTw, Lang::ZhCn, Lang::Ja})
    {
        const auto& s = GetStrings(lang);
        Check(std::wstring_view(s.buttonAutoInstall).find(L'\n') != std::wstring_view::npos &&
                  std::wstring_view(s.buttonOpenPage).find(L'\n') != std::wstring_view::npos,
              L"Strings: command links have a note line");
    }
}

void TestSignature()
{
    // dotnet.exe (x64 .NET install) carries an embedded Microsoft signature.
    const fs::path dotnet = ResolveDotnetRoot(Arch::X64) / L"dotnet.exe";
    if (fs::exists(dotnet))
    {
        Check(IsSignedByMicrosoft(dotnet), L"Signature: dotnet.exe is signed by Microsoft");
        wprintf(L"INFO: dotnet.exe signer organization: %s\n", GetSignerOrganization(dotnet).c_str());
    }
    else
    {
        wprintf(L"SKIP: Signature: %s not found\n", dotnet.c_str());
    }

    // Validly signed, but not by Microsoft: Git for Windows (installed here and on the CI runner).
    wchar_t programFiles[MAX_PATH] = {};
    GetEnvironmentVariableW(L"ProgramW6432", programFiles, MAX_PATH);
    const fs::path git = fs::path(programFiles) / L"Git" / L"cmd" / L"git.exe";
    if (fs::exists(git))
    {
        Check(!GetSignerOrganization(git).empty() && !IsSignedByMicrosoft(git),
              L"Signature: valid non-Microsoft signature is rejected");
    }
    else
    {
        wprintf(L"SKIP: Signature: %s not found\n", git.c_str());
    }

    // This test exe is not signed.
    wchar_t self[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    Check(!IsSignedByMicrosoft(self), L"Signature: unsigned exe is rejected");

    // A text file renamed to .exe.
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    const fs::path fake = fs::path(tmp) / (L"LEInstallerTests_fake_" + std::to_wstring(GetCurrentProcessId()) + L".exe");
    FILE* f = nullptr;
    if (_wfopen_s(&f, fake.c_str(), L"w") == 0 && f)
    {
        fputs("not a PE file", f);
        fclose(f);
    }
    Check(!IsSignedByMicrosoft(fake), L"Signature: non-PE file is rejected");
    std::error_code ec;
    fs::remove(fake, ec);

    Check(!IsSignedByMicrosoft(L"Z:\\does\\not\\exist.exe"), L"Signature: missing file is rejected");
}

// Opt-in (LE_INSTALLER_NETWORK_TEST=1): downloads the real x86 runtime installer from the
// aka.ms link LEInstaller uses and checks its signature. Nothing is installed.
void TestDownloadOptIn()
{
    wchar_t flag[8] = {};
    if (GetEnvironmentVariableW(L"LE_INSTALLER_NETWORK_TEST", flag, 8) == 0 || flag[0] != L'1')
    {
        wprintf(L"SKIP: Download: set LE_INSTALLER_NETWORK_TEST=1 to run the network test\n");
        return;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    const fs::path file = fs::path(tmp) / (L"LEInstallerTests_runtime_" + std::to_wstring(GetCurrentProcessId()) + L".exe");

    std::atomic<bool> cancel{false};
    int lastPercent = -1;
    const HRESULT hr = DownloadFile(L"https://aka.ms/dotnet/10.0/windowsdesktop-runtime-win-x86.exe", file, cancel,
                                    [&](int p) { lastPercent = p; });
    Check(SUCCEEDED(hr), L"Download: x86 runtime installer downloaded");
    Check(lastPercent == 100, L"Download: progress reached 100%");

    std::error_code ec;
    const auto size = fs::file_size(file, ec);
    wprintf(L"INFO: Download: hr=0x%08X, %llu bytes\n", static_cast<unsigned>(hr), static_cast<unsigned long long>(size));
    Check(!ec && size > 10 * 1024 * 1024, L"Download: file is larger than 10 MB");
    Check(IsSignedByMicrosoft(file), L"Download: runtime installer is signed by Microsoft");
    fs::remove(file, ec);

    // Cancelling before the first progress callback aborts and leaves no file behind.
    std::atomic<bool> cancelled{true};
    const HRESULT hrCancel = DownloadFile(L"https://aka.ms/dotnet/10.0/windowsdesktop-runtime-win-x86.exe", file,
                                          cancelled, nullptr);
    Check(hrCancel == E_ABORT && !fs::exists(file), L"Download: cancel returns E_ABORT and removes the file");

    CoUninitialize();
}

}  // namespace

void RunStringsAndSignatureTests()
{
    TestPickLanguage();
    TestStringTables();
    TestSignature();
    TestDownloadOptIn();
}
