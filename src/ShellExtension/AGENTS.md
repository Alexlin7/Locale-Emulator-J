# src/ShellExtension/AGENTS.md

ShellExtension 是 C++ 的 COM Shell Extension（`IShellExtInit` + `IContextMenu`），由 Explorer 載入，提供 exe 的右鍵選單：讀 `LEConfig.xml` 列出 profile，選了之後啟動同目錄的 `LEProc.exe -runas {guid} "{path}"`（或 `-run`、`-manage` 等）。

跨模組鐵則見根目錄 [`AGENTS.md`](../../AGENTS.md)。

## 鐵則

1. **x86 與 x64 都要建。** 架構跟著 Explorer：x64 給 64 位元 Windows，x86 給 32 位元 Windows，和目標程式的架構無關。
2. **CLSID**（`dllmain.cpp` 的 `CLSID_ShellExtension`，`{A8B4F5C2-7E3D-4F1A-9C6B-2D8E0F5A3B71}`）必須與 `src/LEGUI/ShellExtensionConstants.cs` 相同。
3. **字串改動要處理全部 22 種語系**（`Lang/` 的 XML，`DefaultLanguage.xml` 加 21 種翻譯）。
4. **它跑在 Explorer 程序裡**：不能崩潰、不能長時間阻塞，例外不能丟出 COM 邊界。設定檔讀不到或格式錯誤時要安靜地退回預設行為。

## 結構

| 檔案 | 內容 |
|---|---|
| `dllmain.cpp`、`ClassFactory.*`、`ShellExtension.def` | COM 匯出（`DllGetClassObject`、`DllCanUnloadNow`、`DllRegisterServer` 只註冊 CLSID / InprocServer32） |
| `ContextMenuHandler.*` | 右鍵選單內容、啟動 LEProc（`LaunchLEProc`） |
| `ConfigReader.*` | 用 pugixml 讀 `LEConfig.xml`（schema 與 `src/LECommonLibrary/` 共用，改 schema 要兩邊一起改） |
| `I18n.*`、`Lang/` | 語系 |
| `pugixml/` | 第三方 XML 函式庫 |

選單的 shell 註冊（`*\shellex\ContextMenuHandlers` 等）由 LEGUI 的 `ShellExtensionRegistrar` 處理，不在這裡。

## 現況與待辦

- 目前是傳統的 `IContextMenu`，在 Windows 11 只出現在「顯示其他選項」裡。要出現在第一層需要 `IExplorerCommand`，通常還要有簽章的 sparse package，會影響安裝方式（見 `docs/roadmap.md`）。

## 建置與測試

```powershell
msbuild src/ShellExtension/ShellExtension.vcxproj /p:Configuration=Release /p:Platform=x64
msbuild src/ShellExtension/ShellExtension.vcxproj /p:Configuration=Release /p:Platform=Win32
```

- C++23（`stdcpp23`），一般 CRT / STL 可用。
- `tests/ShellExtension.Tests/`（x64）：COM 匯出存在、`LoadLibraryW` 成功；`ConfigReaderTests.cpp` 把 `ConfigReader.cpp` 與 pugixml 直接編進測試 exe，測設定檔解析（含舊版 `<Location>`）。註冊（`DllRegisterServer` / `DllUnregisterServer`）會改 HKCR，刻意不自動測。
