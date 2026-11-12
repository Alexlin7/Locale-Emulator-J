// tests/ShellExtension.Tests/ConfigReaderTests.cpp
// Unit tests for ShellExtension's ConfigReader (LEConfig.xml parsing).
// ConfigReader.cpp and pugixml.cpp are compiled into this test exe directly,
// so these tests do not go through ShellExtension.dll.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <fstream>
#include <string>

#include "ConfigReader.h"

// ConfigReader.cpp references the DLL's module handle (dllmain.cpp). The tests
// only call LoadProfiles(path), which does not use it.
HMODULE g_hModule = nullptr;

namespace
{

int g_failures = 0;

void Check(bool condition, const wchar_t* name)
{
    if (condition)
    {
        wprintf(L"PASS: %s\n", name);
    }
    else
    {
        wprintf(L"FAIL: %s\n", name);
        ++g_failures;
    }
}

// Writes the UTF-8 XML to a unique temp file and returns its path.
std::wstring WriteTempConfig(const char* xml)
{
    wchar_t dir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, dir);
    wchar_t path[MAX_PATH] = {};
    GetTempFileNameW(dir, L"lec", 0, path);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << xml;
    return path;
}

// Old LE versions (upstream, and this fork before f29f84f) wrote <Location>
// instead of <Region>. Must stay in sync with LECommonLibrary's LEConfig.
void LegacyLocationElement_ReadsAsRegion()
{
    auto path = WriteTempConfig(R"(<?xml version="1.0" encoding="utf-8"?>
<LEConfig>
  <Profiles>
    <Profile Name="Legacy" Guid="guid-legacy" MainMenu="false">
      <Parameter></Parameter>
      <Location>ja-JP</Location>
      <Timezone>Tokyo Standard Time</Timezone>
    </Profile>
  </Profiles>
</LEConfig>)");

    auto profiles = ConfigReader::LoadProfiles(path);
    DeleteFileW(path.c_str());

    Check(profiles.size() == 1 && profiles[0].region == L"ja-JP",
          L"ConfigReader: legacy <Location> is read as region");
}

void BothRegionAndLocation_RegionWins()
{
    auto path = WriteTempConfig(R"(<?xml version="1.0" encoding="utf-8"?>
<LEConfig>
  <Profiles>
    <Profile Name="Both" Guid="guid-both" MainMenu="false">
      <Parameter></Parameter>
      <Location>ja-JP</Location>
      <Region>zh-TW</Region>
      <Timezone>Taipei Standard Time</Timezone>
    </Profile>
  </Profiles>
</LEConfig>)");

    auto profiles = ConfigReader::LoadProfiles(path);
    DeleteFileW(path.c_str());

    Check(profiles.size() == 1 && profiles[0].region == L"zh-TW",
          L"ConfigReader: <Region> wins over <Location>");
}

void RegionElement_IsRead()
{
    auto path = WriteTempConfig(R"(<?xml version="1.0" encoding="utf-8"?>
<LEConfig>
  <Profiles>
    <Profile Name="Current" Guid="guid-current" MainMenu="true">
      <Parameter>-x</Parameter>
      <Region>ko-KR</Region>
      <Timezone>Korea Standard Time</Timezone>
    </Profile>
  </Profiles>
</LEConfig>)");

    auto profiles = ConfigReader::LoadProfiles(path);
    DeleteFileW(path.c_str());

    Check(profiles.size() == 1 && profiles[0].region == L"ko-KR" &&
              profiles[0].name == L"Current" && profiles[0].showInMainMenu,
          L"ConfigReader: <Region> is read");
}

} // namespace

// Returns the number of failed checks.
int RunConfigReaderTests()
{
    g_failures = 0;
    RegionElement_IsRead();
    LegacyLocationElement_ReadsAsRegion();
    BothRegionAndLocation_RegionWins();
    return g_failures;
}
