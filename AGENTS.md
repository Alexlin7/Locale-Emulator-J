# AGENTS.md

本檔給 coding agent（Claude Code / Codex / 其他）使用，是專案的大方向、不可違反的鐵則與開發約定的單一真相源。現況與使用說明見根目錄 [`README.md`](README.md)；Core 的建置細節見 [`src/Core/README.md`](src/Core/README.md)。

## 專案

Locale Emulator（LE）是 Windows 工具：不改系統地區設定，就能讓指定程式以另一個地區（code page、LCID、時區）執行。做法是把 DLL 注入目標程序、hook Windows API。

本 repo 是 [xupefei/Locale-Emulator](https://github.com/xupefei/Locale-Emulator)（2022-04-15 封存）的延續，經 [ooxxTaiwan/Locale-Emulator-J](https://github.com/ooxxTaiwan/Locale-Emulator-J) fork 而來，目前由本人自行修改維護。包含三個 .NET 10 專案、兩個 C++ native 元件（Core）與一個 C++ COM Shell Extension。Core 源自 [Locale-Emulator-Core](https://github.com/xupefei/Locale-Emulator-Core)（commit `ae7160d`，2022-04-15 封存），直接整合在 `src/Core/`。

## 架構大方向

```
使用者在 exe 上按右鍵
    ↓
ShellExtension（C++，x64）           ← COM Shell Extension，由 Explorer.exe 載入
    │ 讀 LEConfig.xml
    │ 呼叫 LEProc.exe -runas {guid} {path}
    ↓
LEProc（.NET 10，x86）               ← 啟動器
    │ 參照 LECommonLibrary
    │ P/Invoke 呼叫 LoaderDll.dll
    ↓
Core/LoaderDll（C++，x86）           ← 建立目標程序並注入 LocaleEmulator.dll
    │ 把 LEB 寫入共享記憶體
    ↓
Core/LocaleEmulator（C++，x86）      ← 在目標程序內 hook Windows API
    │ 讀 LEB、改寫 PEB 的 NLS 表、裝 syscall hook + EAT hook、hook ntdll 轉換函式
    ↓
目標程序                              ← 以為自己跑在目標地區

LEGUI（.NET 10，AnyCPU）             ← Profile 管理介面 + Shell Extension 安裝
    │ 參照 LECommonLibrary；ShellExtensionRegistrar 以 Registry API 註冊/解除
    ↓
LECommonLibrary（.NET 10，AnyCPU）   ← 共用：LEConfig、LEProfile、RegionSupport、SystemHelper
```

| 專案 | 語言 | 平台 | 職責 |
|---|---|---|---|
| LECommonLibrary | C# | AnyCPU | XML 設定讀寫、Profile 資料模型、可模擬地區判斷（`RegionSupport`）、OS 工具函式 |
| LEProc | C# | x86 | CLI 進入點、由 `CultureInfo` 取 code page/LCID、組 LEB 結構、P/Invoke 呼叫 Core |
| LEGUI | C#（WPF） | AnyCPU | Profile 管理介面、Shell Extension 安裝/解除（`ShellExtensionRegistrar`） |
| Core/LoaderDll | C++ | x86 | 匯出 `LeCreateProcess`：建立目標程序並注入 `LocaleEmulator.dll` |
| Core/LocaleEmulator | C++ | x86 | 注入用 DLL：hook syscall/EAT、改寫 PEB NLS 表、攔截地區相關 API |
| ShellExtension | C++ | x86 + x64 | COM Shell Extension：右鍵選單、讀設定、啟動 LEProc |

**資料流：** 右鍵 exe → ShellExtension 讀 `LEConfig.xml` 並呼叫 `LEProc.exe -runas {guid} {path}` → LEProc 由 XML 載入 profile、用 `CultureInfo` 解析 code page/地區/時區 → `LoaderWrapper` 把 `LEB` 結構與 registry 轉向項目整理進 unmanaged 記憶體 → `LoaderDll.dll` 的 `LeCreateProcess` 建立目標程序並注入 `LocaleEmulator.dll` → `LocaleEmulator.dll` 裝好 hook，目標程序看到的就是模擬的地區。

## 不可違反的鐵則（改動前必讀）

1. **LEProc 與 Core 必須維持 x86。** Core 的 syscall hook 引擎（`HookSysCall_x86`）只有 x86 實作，靠比對 `KiFastSystemCall` 的 opcode 運作。
2. **Shell Extension 必須同時建 x86 與 x64。** x64 給 64 位元 Explorer，x86 給 32 位元 Windows 的 Explorer。
3. **Core 不使用 C runtime，靜態 import 只有 `ntdll.dll`**（LoaderDll 另有 kernel32 的 `CreateProcessInternalW`）。CRT 函式來自 ntdll 匯出（`src/Core/ImportLibs/*.def`），執行期支援在 `src/Core/Runtime/CoreRuntime.cpp`，共用設定在 `src/Core/Core.props`。不要把 CRT 或 Delayimp.lib 加回來。
4. **`LocaleEmulator.dll` 絕不能靜態 import kernel32。** 它是在目標程序第一次呼叫 `LdrLoadDll`（也就是 ntdll 正要載入 kernel32）時被注入的，當下 kernel32 還不存在。要用 kernel32 的函式就在執行時查找（`Hooks/User32Hook.cpp` 的 `KERNEL32_API`）；建置時有檢查，出現靜態 kernel32 import 會直接失敗。
5. **Core 的 hook 只能依賴 ntdll。** 理由同上，hook 裡不能呼叫 kernelbase 的 `MultiByteToWideChar` 之類函式；code page 轉換一律用 ntdll 的 `RtlCustomCPToUnicodeN` / `RtlUnicodeToCustomCPN` 搭配 LE 自己載入的 NLS 表。
6. **新版 Windows 上換 ntdll NLS 表的做法已經失效**：`RtlInitNlsTables` / `RtlResetRtlTranslations` 是空函式（build 26300 實測）。ANSI/OEM 轉換靠 `Hooks/NtdllHook.cpp` 的 `LeRtl*` hook，不要拿掉。
7. **.NET 端全球化後端固定用 NLS**（`Directory.Build.props` 的 `System.Globalization.UseNls=true`）。ICU 模式下地區清單會變成 `zh-Hans-CN` / `zh-Hant-TW`（LCID 0x1000），Core 無法模擬、程式完全沒反應。地區是否可用一律經 `LECommonLibrary.RegionSupport` 判斷。
8. **`src/Core/` 底下的修改，commit 訊息一律加 `[Core]` 前綴**，用來和上游程式碼區分。
9. **改 Core 之後一定要跑 smoke test**（見下方指令）。`Core.Tests` 只驗證結構佈局，測不到 hook 是否真的生效。

## 工具鏈

| 工具 | 版本 | 用途 |
|---|---|---|
| Visual Studio / MSVC Build Tools | 2026（v18），PlatformToolset `v145` | C++ 建置（Core、ShellExtension、C++ 測試） |
| Windows SDK | 10.0（目前用 10.0.26100） | Core 的標頭與 import lib；**不需要 WDK** |
| .NET SDK | 10 | LECommonLibrary / LEProc / LEGUI 與 .NET 測試 |
| .NET 10 Windows Desktop Runtime x86 | 10 | 執行 x86 的 LEProc 與 `LEProc.Tests --arch x86` |

- 方案檔：`LocaleEmulator.sln`（C# 與 C++ 混合）。共用 .NET 建置屬性在 `Directory.Build.props`，所有組件以 `key.snk` 強式名稱簽章。
- 最低支援系統：Windows 10 1607 以上（.NET 10 的要求）。
- 授權：LGPL-3.0（Core 為 LGPL-3.0 / GPL-3.0）。

## 建置與指令

```powershell
# 完整建置（兩個平台）
msbuild LocaleEmulator.sln /p:Configuration=Release /p:Platform=Win32
msbuild LocaleEmulator.sln /p:Configuration=Release /p:Platform=x64

# 只建 .NET 專案
dotnet build src/LECommonLibrary/
dotnet build src/LEProc/
dotnet build src/LEGUI/

# .NET 測試
dotnet test tests/LECommonLibrary.Tests/
dotnet test tests/LEGUI.Tests/
dotnet test tests/LEProc.Tests/ --arch x86     # LEProc 是 x86，需要 x86 test host

# C++ 測試：執行 Build/Release/x86/ 與 Build/Release/x64/ 底下的 *Tests*.exe

# Smoke test（需要先組好 Build/Release/x86/，步驟同 CI）
.\tests\SmokeTest\Invoke-SmokeTest.ps1
```

CI（`.github/workflows/ci.yml`）用 `dotnet build` 建 .NET 專案、用 `msbuild` 逐一建 C++ vcxproj，再把 LEProc/LEGUI publish 與 Core DLL 組進 `Build/Release/x86/`。

**建置產出：**

```
Build/Release/
  x86/
    LEProc.exe              # .NET 10（x86）
    LECommonLibrary.dll     # .NET 10（AnyCPU）
    LEGUI.exe               # .NET 10（AnyCPU）
    LoaderDll.dll           # Core（x86）
    LocaleEmulator.dll      # Core（x86）
    ShellExtension.dll      # Shell Extension（x86）
    Lang/                   # 語系檔
  x64/
    ShellExtension.dll      # Shell Extension（x64）
```

**C++ 專案的建置組態：**

| C++ 專案 | Debug\|Win32 | Release\|Win32 | Debug\|x64 | Release\|x64 |
|---|:---:|:---:|:---:|:---:|
| Core/LoaderDll | - | ✓ | - | - |
| Core/LocaleEmulator | - | ✓ | - | - |
| Core/Loader | - | ✓ | - | - |
| ShellExtension | ✓ | ✓ | ✓ | ✓ |
| LocaleTestApp | ✓ | ✓ | - | - |
| Core.Tests | ✓ | ✓ | - | - |
| ShellExtension.Tests | - | - | ✓ | ✓ |

Core 只有 Release 組態。

## 設定檔格式

`LEConfig.xml` 與 `*.le.config` 共用同一個 schema：

```xml
<LEConfig>
  <Profiles>
    <Profile Name="..." Guid="..." MainMenu="true|false">
      <Parameter>...</Parameter>
      <Region>ja-JP</Region>
      <Timezone>Tokyo Standard Time</Timezone>
      <RunAsAdmin>false</RunAsAdmin>
      <RedirectRegistry>true</RedirectRegistry>
      <IsAdvancedRedirection>false</IsAdvancedRedirection>
      <RunWithSuspend>false</RunWithSuspend>
    </Profile>
  </Profiles>
</LEConfig>
```

## 測試

| 層級 | 框架 | 指令 |
|---|---|---|
| .NET 單元測試（LECommonLibrary、LEGUI） | xUnit + NSubstitute | `dotnet test` |
| .NET 單元測試（LEProc） | xUnit + NSubstitute | `dotnet test --arch x86` |
| C++ 單元測試 | 獨立 exe（無框架） | 執行 `Build/Release/x86/`、`x64/` 的 `*Tests*.exe` |
| Smoke test | PowerShell + LocaleTestApp | `.\tests\SmokeTest\Invoke-SmokeTest.ps1` |

- `tests/LECommonLibrary.Tests/`：XML 設定解析、profile 模型、系統工具函式、可模擬地區（`RegionSupportTests`）
- `tests/LEProc.Tests/`：code page 對應、`CultureInfo` 查詢（與 .NET Framework 4.0 基準值比對）、CLI 參數、registry 轉向
- `tests/LEGUI.Tests/`：Shell Extension 註冊邏輯、i18n 載入、ViewModel 邏輯
- `tests/Core.Tests/`：C++ 與 C# 結構佈局契約（LEB、RTL_TIME_ZONE_INFORMATION、REGISTRY_REDIRECTION_ENTRY64 的大小與欄位位移，x86 獨立 exe）
- `tests/ShellExtension.Tests/`：COM 匯出驗證、DllRegisterServer/Unregister 來回測試（x64）
- **Smoke test**：用不同地區的 profile 經 LEProc 啟動 LocaleTestApp，檢查 `GetACP()`、`GetOEMCP()`、`GetUserDefaultLCID()`；LocaleTestApp 另外輸出 `CHECK_*=OK|FAIL`，驗證 ntdll `Rtl*` 轉換、kernelbase `CP_ACP`、ANSI 檔案 API、user32 ANSI 視窗標題都使用 profile 的 code page。所有 `CHECK_*` 都必須是 `OK`。

## 開發約定

**工作流程（動手前必讀）：**
- **計畫先行（review-first）：動任何 code 前，先提出計畫或設計，等使用者確認再實作**，尤其是重構、新功能、相依或設定變更。
- **小切片、逐步 commit**：大改動切成連續的小切片，每片建置與測試都綠燈後再 commit。
- **commit / merge 前先驗證**：改 .NET 後跑對應的 `dotnet build` + `dotnet test`；改 C++ 後跑 C++ 建置與測試；改 Core 後一定要再跑 smoke test。確認綠燈才 commit。
- **先寫測試**：修 bug 時先寫出能重現問題的測試（例如 LocaleTestApp 的 `CHECK_*`、`RegionSupportTests`），確認它會失敗，再修到通過。
- **commit 前先問**：除非使用者明確要求，否則不要自行 commit 或 push；推到遠端、開 PR 前一定先確認。

**慣例：**
- 全程以**繁體中文（台灣用語）**溝通；程式碼識別子與技術名詞保留原文。
- commit 訊息用中文，格式 `type(scope): 摘要`（例如 `fix:`、`test(smoke-test):`、`chore:`）；`src/Core/` 的修改一律用 `[Core]` 前綴。
- 一個功能一個分支（例如 `feature/...`、`fix/...`），一個 commit 只做一件事，方便日後拆開回饋給上游。
- 語系檔：LEGUI 的 i18n 是 `src/LEGUI/Lang/` 的 XAML resource dictionary（22 種語言）；Shell Extension 是 `src/ShellExtension/Lang/` 的 XML（22 種語言）。新增或修改字串時要一併處理所有語系。
- 測試或除錯時**不要擅自執行或掃描使用者的遊戲／應用程式資料夾**；需要時先問，只碰使用者指定的路徑。
- 改動牽涉設計決策時，同步更新本檔與必要的 `README.md` / `src/Core/README.md`。

## 文件整理原則

- `README.md`：專案現況、來源、系統需求、使用方式（公開門面）。
- `AGENTS.md`：大方向、鐵則、開發約定（本檔）。`CLAUDE.md` 只負責匯入本檔，不另外寫內容。
- `src/Core/README.md`：Core 的建置方式與無 CRT 設計的說明。
- `docs/guides/`：操作指南（例如 CI）。
- `docs/superpowers/specs/`、`docs/superpowers/plans/`：過去的設計與計畫紀錄，保留脈絡用；已過時的內容不要回頭改寫，以本檔與程式碼為準。
- 內容衝突時的優先順序：**本檔的鐵則 > 程式碼與測試 > `README.md` > `docs/` 的歷史紀錄**。
