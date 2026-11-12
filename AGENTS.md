# AGENTS.md

本檔給 coding agent（Claude Code / Codex / 其他）使用，內容是全專案共用的大方向、跨模組鐵則與開發約定。**各模組的細節寫在該模組自己的 `AGENTS.md`**（見下方「模組說明檔」），改哪個模組就讀哪一份；本檔只保留跨模組、每次都必須知道的內容。

現況與使用說明見 [`README.md`](README.md)；目前的待辦、取捨與風險見 [`docs/roadmap.md`](docs/roadmap.md)。

## 專案

Locale Emulator（LE）是 Windows 工具：不改系統地區設定，就能讓指定程式以另一個地區（code page、LCID、時區）執行。做法是把 DLL 注入目標程序、hook Windows API。

本 repo 是 [xupefei/Locale-Emulator](https://github.com/xupefei/Locale-Emulator)（2022-04-15 封存）的延續，經 [ooxxTaiwan/Locale-Emulator-J](https://github.com/ooxxTaiwan/Locale-Emulator-J) fork 而來，目前由本人自行修改維護。Core 源自 [Locale-Emulator-Core](https://github.com/xupefei/Locale-Emulator-Core)（commit `ae7160d`，2022-04-15 封存），直接整合在 `src/Core/`。

## 架構

```
使用者在 exe 上按右鍵
    ↓
ShellExtension（C++，x86 + x64）     ← COM Shell Extension，由 Explorer.exe 載入
    │ 讀 LEConfig.xml，呼叫 LEProc.exe -runas {guid} {path}
    ↓
LEProc（.NET 10，x86）               ← 啟動器：由 CultureInfo 組出 LEB，P/Invoke 呼叫 LoaderDll
    ↓
Core/LoaderDll（C++，x86）           ← 建立目標程序、注入 LocaleEmulator.dll、把 LEB 寫入共享記憶體
    ↓
Core/LocaleEmulator（C++，x86）      ← 在目標程序內 hook Windows API
    ↓
目標程序                              ← 以為自己跑在目標地區

LEGUI（.NET 10，AnyCPU）             ← Profile 管理介面 + Shell Extension 安裝
LECommonLibrary（.NET 10，AnyCPU）   ← 共用：LEConfig、LEProfile、RegionSupport、SystemHelper
```

## 模組說明檔

| 模組 | 語言 / 平台 | 職責 | 說明檔 |
|---|---|---|---|
| `src/Core/`（LoaderDll、LocaleEmulator、Loader） | C++，x86（x64 開發中） | 注入與 hook；無 CRT | [`src/Core/AGENTS.md`](src/Core/AGENTS.md) |
| `src/LEProc/` | C#，x86 | CLI 啟動器、組 LEB、呼叫 Core | [`src/LEProc/AGENTS.md`](src/LEProc/AGENTS.md) |
| `src/LECommonLibrary/` | C#，AnyCPU | 設定檔、Profile、`RegionSupport` | [`src/LECommonLibrary/AGENTS.md`](src/LECommonLibrary/AGENTS.md) |
| `src/LEGUI/` | C#（WPF），AnyCPU | Profile 管理、Shell Extension 安裝 | [`src/LEGUI/AGENTS.md`](src/LEGUI/AGENTS.md) |
| `src/ShellExtension/` | C++，x86 + x64 | 右鍵選單 | [`src/ShellExtension/AGENTS.md`](src/ShellExtension/AGENTS.md) |
| `tests/SmokeTest/` | PowerShell + C++ | 端對端驗證 hook 是否生效 | [`tests/SmokeTest/AGENTS.md`](tests/SmokeTest/AGENTS.md) |

每個模組的 `CLAUDE.md` 只負責匯入同目錄的 `AGENTS.md`，不另外寫內容。其他測試專案（`tests/*.Tests`）的說明併在對應模組的檔案裡。

## 跨模組鐵則（改動前必讀）

1. **架構分工固定。** LEProc 與 x86 版 Core 必須維持 x86；64 位元目標程式由 x64 版 Core（`LoaderDll64.dll` + `LocaleEmulator64.dll`）處理，x64 版尚在開發中、未納入發佈產物。ShellExtension 必須同時建 x86 與 x64（架構跟著 Explorer，與目標程式無關）。設計見 `docs/superpowers/specs/2026-11-11-core-modularization-and-x64-design.md`。
2. **跨模組的資料契約要一起改。**
   - **LEB 結構**：C#（`src/LEProc/LoaderWrapper.cs`）、C++（`src/Core/LocaleEmulator/LocaleEmulator.h`）、`tests/Core.Tests/main.cpp`、`tests/LEProc.Tests/LEBStructLayoutTests.cs` 四處必須一致，改任何一處都要同步其他三處，並跑 Core.Tests 與 LEProc.Tests。
   - **Shell Extension 的 CLSID**：`src/ShellExtension/dllmain.cpp` 與 `src/LEGUI/ShellExtensionConstants.cs` 必須相同。
   - **`LEConfig.xml` schema**：LECommonLibrary（C#）與 ShellExtension 的 `ConfigReader`（C++）各自解析，改 schema 要兩邊一起改。
3. **.NET 端全球化後端固定用 NLS**（`Directory.Build.props` 的 `System.Globalization.UseNls=true`）。ICU 模式下地區會變成 LCID 0x1000，Core 無法模擬。地區是否可用一律經 `LECommonLibrary.RegionSupport` 判斷。
4. **`src/Core/` 底下的修改，commit 訊息一律加 `[Core]` 前綴**，用來和上游程式碼區分。
5. **改 Core 之後一定要跑 smoke test。** Core.Tests 只驗證結構佈局，測不到 hook 是否真的生效。

各模組自己的鐵則摘要（細節見各模組說明檔）：
- Core：不使用 C runtime，靜態 import 只有 ntdll；`LocaleEmulator.dll` 絕不能靜態 import kernel32；hook 只能依賴 ntdll；`LeRtl*` hook 不能拿掉。
- ShellExtension：x86、x64 都要建；字串改動要處理 22 種語系。
- LEGUI：字串改動要處理 22 種語系。

## 工具鏈

| 工具 | 版本 | 用途 |
|---|---|---|
| Visual Studio / MSVC Build Tools | 2026（v18），PlatformToolset `v145` | C++ 建置 |
| Windows SDK | 10.0（目前用 10.0.26100） | Core 的標頭與 import lib；**不需要 WDK** |
| .NET SDK | 10 | .NET 專案與測試 |
| .NET 10 Windows Desktop Runtime x86 | 10 | 執行 x86 的 LEProc 與 `LEProc.Tests --arch x86` |

- C++ 標準：Core 為 C++17；ShellExtension 與 C++ 測試專案為 C++23（`stdcpp23`）。
- 方案檔：`LocaleEmulator.sln`（C# 與 C++ 混合）。共用 .NET 建置屬性在 `Directory.Build.props`，所有組件以 `key.snk` 強式名稱簽章。
- 最低支援系統：Windows 10 1607 以上（.NET 10 的要求）。
- 授權：LGPL-3.0（Core 為 LGPL-3.0 / GPL-3.0）。

## 建置、測試與 CI

```powershell
# 完整建置（兩個平台）
msbuild LocaleEmulator.sln /p:Configuration=Release /p:Platform=Win32
msbuild LocaleEmulator.sln /p:Configuration=Release /p:Platform=x64

# .NET 測試
dotnet test tests/LECommonLibrary.Tests/
dotnet test tests/LEGUI.Tests/
dotnet test tests/LEProc.Tests/ --arch x86     # LEProc 是 x86，需要 x86 test host

# C++ 測試：執行 Build/Release/x86/ 與 Build/Release/x64/ 底下的 *Tests*.exe

# Smoke test（需要先組好 Build/Release/x86/，步驟同 CI）
.\tests\SmokeTest\Invoke-SmokeTest.ps1
```

| 層級 | 框架 | 位置 |
|---|---|---|
| .NET 單元測試 | xUnit + NSubstitute | `tests/LECommonLibrary.Tests/`、`tests/LEGUI.Tests/`、`tests/LEProc.Tests/` |
| C++ 測試 | 獨立 exe（無框架） | `tests/Core.Tests/`（結構佈局，x86 + x64）、`tests/ShellExtension.Tests/`（COM，x64） |
| Smoke test | PowerShell + LocaleTestApp | `tests/SmokeTest/` |
| 靜態分析 | CodeQL（`security-extended` + 自訂查詢） | `.github/workflows/codeql.yml`、`.github/codeql/` |

CI（`.github/workflows/ci.yml`，`workflow_dispatch` 觸發）用 `dotnet build` 建 .NET、用 `msbuild` 逐一建 C++ vcxproj（每個 msbuild 後都檢查結束碼），組出 `Build/Release/x86/` 後跑 C++ 測試與 smoke test，另外跑時機實驗（僅供參考）。操作說明見 `docs/guides/github-actions-ci.md`。

CodeQL（`.github/workflows/codeql.yml`）分析 C#（不建置）與 C/C++（Core x86、ShellExtension x64），結果在 GitHub 的 Security → Code scanning 與 PR 上；`ml.h` / `ml.cpp`、`pugixml` 的結果會被濾掉。另有專案自訂查詢（`.github/codeql/le-queries/`），把鐵則寫成自動檢查。說明見 `docs/guides/codeql.md`。

**建置產出：**

```
Build/Release/
  x86/   LEProc.exe、LECommonLibrary.dll、LEGUI.exe、LoaderDll.dll、LocaleEmulator.dll、ShellExtension.dll、Lang/
  x64/   ShellExtension.dll
```

**C++ 專案的建置組態：**

| C++ 專案 | Debug\|Win32 | Release\|Win32 | Debug\|x64 | Release\|x64 |
|---|:---:|:---:|:---:|:---:|
| Core/LoaderDll | - | ✓ | - | ✓（開發中，未發佈） |
| Core/LocaleEmulator | - | ✓ | - | ✓（開發中，hook 停用） |
| Core/Loader | - | ✓ | - | - |
| ShellExtension | ✓ | ✓ | ✓ | ✓ |
| LocaleTestApp | ✓ | ✓ | - | - |
| Core.Tests | ✓ | ✓ | - | ✓ |
| ShellExtension.Tests | - | - | ✓ | ✓ |

Core 三個專案沒有 Debug 組態；`LocaleEmulator.sln` 的 Debug 方案組態把它們對應到同平台的 Release（Loader 只有 Win32），所以用 Debug 開方案也能建出 Core。

## 開發約定

**工作流程（動手前必讀）：**
- **計畫先行（review-first）：動任何 code 前，先提出計畫或設計，等使用者確認再實作**，尤其是重構、新功能、相依或設定變更。
- **小切片、逐步 commit**：大改動切成連續的小切片，每片建置與測試都綠燈後再 commit。
- **commit / merge 前先驗證**：改 .NET 後跑對應的 `dotnet build` + `dotnet test`；改 C++ 後跑 C++ 建置與測試；改 Core 後一定要再跑 smoke test。確認綠燈才 commit。
- **先寫測試**：修 bug 時先寫出能重現問題的測試（例如 LocaleTestApp 的 `CHECK_*`、`RegionSupportTests`），確認它會失敗，再修到通過。
- **commit 前先問**：除非使用者明確要求，否則不要自行 commit 或 push；推到遠端、開 PR 前一定先確認。
- **本機沒有 Windows 時以 CI 驗證**：結論要附 CI run 編號；「全部通過」的結果要確認測試確實測到了東西（例如時機實驗的 `LE_EXPERIMENT` 標記）。

**慣例：**
- 全程以**繁體中文（台灣用語）**溝通；程式碼識別子與技術名詞保留原文。
- commit 訊息用中文，格式 `type(scope): 摘要`（例如 `fix:`、`test(smoke-test):`、`chore:`）；`src/Core/` 的修改一律用 `[Core]` 前綴。
- 一個功能一個分支（例如 `feature/...`、`fix/...`），一個 commit 只做一件事，方便日後拆開回饋給上游。
- **對外連結只放可信來源**（GitHub、微軟、gnu.org 等）。原作者的 `pooi.moe` 與圖示來源 `graphicex.com` 已成為停放網域、會轉址到不明網站，不得再加回 README、程式或語系檔；新增連結前先確認網域現況。
- 測試或除錯時**不要擅自執行或掃描使用者的遊戲／應用程式資料夾**；需要時先問，只碰使用者指定的路徑。
- 改動牽涉設計決策時，同步更新相關的 `AGENTS.md`（本檔或模組的）、`docs/roadmap.md` 與必要的 `README.md`。

## 文件整理原則

| 文件 | 內容 | 維護方式 |
|---|---|---|
| `README.md` | 專案現況、來源、系統需求、使用方式（公開門面） | 隨功能更新 |
| `AGENTS.md`（本檔） | 跨模組的大方向、鐵則、開發約定 | 隨決策更新 |
| `<模組>/AGENTS.md` | 該模組的鐵則、結構、常見陷阱、測試方式 | 改該模組時一起更新 |
| `CLAUDE.md`（各層） | 只匯入同目錄的 `AGENTS.md` | 不寫內容 |
| `src/Core/README.md` | Core 的建置方式與無 CRT 設計（給人看） | 隨 Core 建置變更 |
| `docs/roadmap.md` | 待辦、做得好的地方、取捨、不做會後悔的事 | **活文件**，每完成或新增一項就更新 |
| `docs/guides/` | 操作指南（例如 CI） | 隨流程更新 |
| `docs/superpowers/specs/`、`plans/` | 設計與計畫紀錄 | 保留脈絡；實驗結果與定案狀態可以補記，已過時的內容不回頭改寫 |

內容衝突時的優先順序：**本檔的鐵則 > 模組 `AGENTS.md` 的鐵則 > 程式碼與測試 > `README.md` > `docs/` 的紀錄**。
