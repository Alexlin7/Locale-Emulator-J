# 設計文件：Core 模組化重構與 x64 支援

> **日期**：2026-11-11
> **狀態**：設計草稿，待使用者確認（§3 的待確認決策）
> **前置研究**：
> - `docs/superpowers/specs/2026-11-11-x64-core-migration-research.md`（技術研究：語言選型、x64 佈局、hook、linker）
> - `docs/superpowers/specs/2026-11-11-ml-usage-inventory.md`（ml 使用盤點）
> **實作計畫**：`docs/superpowers/plans/2026-11-11-core-phase1-foundation.md`（Phase 1）

---

## 1. 目標與動機

### 1.1 目標

1. **讓 LE 能模擬 64 位元程式的地區設定。** 目前 Core 只有 x86，對 64 位元程式完全無效。
2. **把 Core 重構成有明確邊界的模組**，降低對 ml（4.9 萬行的個人工具庫）的依賴，讓之後的維護者看得懂、改得動。
3. **全程不破壞現有 x86 行為**：每個切片都必須讓 x86 smoke test 維持全綠。

### 1.2 為什麼兩件事一起做

x64 化必須重寫 hook 引擎（x86 的 `KiFastSystemCall` 單點攔截在 x64 不存在），而 hook 引擎正是 ml 依賴最重的部分（盤點分類 A，164 處）。若先做 x64、之後再重構，同一批程式碼要改兩次。所以策略是：**先在 x86 上切出模組邊界（行為不變），再在邊界後面加入 x64 實作。**

### 1.3 範圍

| 包含 | 不包含 |
|---|---|
| LocaleEmulator.dll、LoaderDll 的 x64 版本 | ARM64 / ARM64EC |
| LEProc 依目標架構分派 | Windows 11 新版右鍵選單（`IExplorerCommand`），另案處理 |
| x64 的 NLS（code page）、地區（LCID）、時區模擬 | 第一版 x64 不含 user32 / gdi32 hook（視窗標題、字型），排在後續 phase |
| 逐步以自有模組取代 ml | 完全移除 ml（視重構結果再決定） |
| CI 的 x64 建置與 smoke test | 改用 Rust（研究結論：不採用） |

---

## 2. 現況限制（摘要）

詳細說明見研究報告，這裡只列直接影響設計的事實：

| # | 事實 | 影響 |
|---|---|---|
| F1 | x64 沒有 `KiFastSystemCall`，`HookPort` 的單點 syscall 攔截無法移植 | x64 改為逐一 hook 需要的 `Nt*` stub（約 13 個） |
| F2 | ml 的 `ML_AMD64` 路徑從未建置過，已確認 `Mp` 的 x64 push/ret 跳轉編碼錯誤（高 32 位元遺失、中間夾 4 bytes 垃圾） | 所有 x64 路徑當作新程式碼審查與測試 |
| F3 | 所有 hook 都經過 `Mp::PatchMemory(PATCH_MEMORY_DATA[], count)` | 這是 HookEngine 的天然切點 |
| F4 | `CoreRuntime.cpp` 的 SEH handler 與 `.def` 檔只適用 x86 | x64 需要依架構分段，x64 SEH 改用 ntdll 的 `__C_specific_handler` |
| F5 | `RtlInitNlsTables` / `RtlResetRtlTranslations` 在新版 Windows 是空函式（鐵則 6） | x64 一樣需要 `LeRtl*` hook；另外有一個「resume 前改寫遠端 PEB」的假說待驗證 |
| F6 | x86 程序無法對 x64 程序做 inline patch | 需要 x64 版的 Loader 與啟動器 |
| F7 | `LEB` 結構只有固定大小欄位（`ULONG`、`ULONG64`、`WCHAR[]`），沒有指標 | C# 與 C++ 的結構契約在 x64 不會改變，`LoaderWrapper` 不需要修改 |
| F8 | `HookPort.cpp` / `.h` 有 832 行只在 `ML_KERNEL_MODE` 下編譯的死碼 | 可以直接刪除，降低移植量 |

---

## 3. 決策

### 3.1 已確定（依研究與先前討論）

| ID | 項目 | 決策 | 理由 |
|---|---|---|---|
| D1 | 語言 | Core 全部維持 MSVC C++（v145、No-CRT）；x64 組語用 MASM（ml64） | Rust 需要重寫 5 萬行以上，而且沒有 SEH；效能不是瓶頸 |
| D2 | 架構分派點 | **LEProc** 判斷目標 exe 的架構；ShellExtension 不改 | ShellExtension 的架構取決於 Explorer，與目標程式無關 |
| D3 | 重構策略 | 先切模組邊界（x86 行為不變）→ 再加 x64 實作 | 避免同一批程式碼改兩次 |
| D4 | 跳轉方式 | x64 只用 `jmp` 形式（`E9` / `FF 25`），不用 push/ret | CET shadow stack 相容；避免 RSP 對齊偏移 |
| D5 | x64 syscall hook | 逐一 hook 需要的 `Nt*` stub；呼叫原函式時使用自建的 syscall stub | HookPort 架構無法移植（F1） |
| D6 | 驗證方式 | 每個切片都經 CI（Windows runner）建置 + C++ 測試 + smoke test；本機 Windows 有空時再補測 | 開發環境可能沒有 Windows；CI 已具備完整流程與 `workflow_dispatch` |

### 3.2 待確認（附建議預設值）

| ID | 項目 | 建議預設 | 其他選項 |
|---|---|---|---|
| **Q1** | 修改 AGENTS.md 鐵則 1 | 改為「LEProc 維持 x86；Core 同時提供 x86 與 x64 版本，x64 版只用於 64 位元目標」 | 暫不修改，x64 先放在實驗分支 |
| **Q2** | x64 啟動器形式 | **同一個 `LEProc.csproj` 再建一次 x64 版，輸出 `LEProc64.exe`**。程式碼完全共用，LEProc（x86）偵測到 x64 目標時，以相同參數啟動 LEProc64 | 新寫一支 C++ 小程式；或 LEProc 改成 AnyCPU |
| **Q3** | x64 DLL 的命名與位置 | 與 x86 放在同一個資料夾，加 `64` 後綴：`LoaderDll64.dll`、`LocaleEmulator64.dll` | 放在 `x64\` 子資料夾、沿用相同檔名 |
| **Q4** | x64 指令解碼器 | Zydis（MIT 授權），以原始碼方式放進 `src/Core/ThirdParty/zydis/`，用 `ZYAN_NO_LIBC` 編譯，只使用 decoder | 修好並沿用 ml 的 `LdeGetOpCodeSize64`（只能回報長度） |
| **Q5** | 跨架構子程序（x86 → x64、x64 → x86） | 第一版不支援，維持現行行為（直接放棄注入，`NtdllHook.cpp:270`） | 交給對應架構的 Loader 處理（需要 IPC） |
| **Q6** | x64 第一版的功能範圍 | NLS（ANSI / OEM code page）+ 地區 API（`NtQueryDefault*`）+ 時區 | 一次做到與 x86 相同（含 user32 / gdi32） |

---

## 4. 目標架構

### 4.1 啟動鏈

```
ShellExtension（x64，Explorer 載入）
  │ LEProc.exe -runas {guid} {path}
  ↓
LEProc.exe（.NET 10，x86）
  │ TargetArchitectureDetector：讀 PE header + CLR header
  ├─ x86 目標 ─→ LoaderDll.dll ─→ LocaleEmulator.dll          ← 現有路徑，不變
  └─ x64 目標 ─→ LEProc64.exe（同一份程式碼的 x64 建置）
                   └─→ LoaderDll64.dll ─→ LocaleEmulator64.dll
```

**目標架構判斷規則（`TargetArchitectureDetector`，C#）：**

| PE `Machine` | CLR header | 判斷結果 |
|---|---|---|
| `AMD64` | — | x64 |
| `I386` | 沒有 CLR header（原生程式） | x86 |
| `I386` | 有 CLR header，且有 `32BITREQUIRED` 或 `32BITPREFERRED` | x86 |
| `I386` | 有 CLR header，沒有上述旗標（AnyCPU） | **x64**（64 位元 Windows 上） |
| `ARM64` 或其他 | — | 不支援，顯示錯誤 |

### 4.2 Core 模組切分

```
src/Core/
  Runtime/                  無 CRT 執行期支援（依 _M_IX86 / _M_X64 分段）
  ImportLibs/
    x86/*.def               現有檔案
    x64/*.def               新增：無 @N 修飾，含 __C_specific_handler、__chkstk
  LocaleEmulator/
    Arch/
      LeArch.h              TEB / PEB / NLS 欄位定位、佈局 static_assert
    HookEngine/
      HookEngine.h          介面（以 PATCH_MEMORY_DATA 的形狀為準）
      HookEngineX86.cpp     包裝現有的 Mp::PatchMemory + HookPort
      HookEngineX64.cpp     新寫：near relay + FF25 + Zydis + syscall stub 複製
      LeX64Thunks.asm       MASM（只有 x64 編譯）
    Native/                 （Phase 7）NT 宣告，取代 ml 的分類 I
    Pe/                     （Phase 7）EAT / 模組查找，取代 ml 的分類 B
    Hooks/                  只描述「攔截什麼、改成什麼」，不碰 patch 細節
    ...
  ThirdParty/zydis/         （Q4 確認後）
```

### 4.3 HookEngine 介面

所有 hook 檔案目前都是這種寫法：

```cpp
Mp::PATCH_MEMORY_DATA p[] =
{
    Mp::FunctionJumpVa(::RtlCustomCPToUnicodeN, LeCustomCPToUnicodeN, &HookStub.StubRtlCustomCPToUnicodeN),
    ...
};
Mp::PatchMemory(p, countof(p));
```

介面設計原則是**保留這個形狀**，讓 hook 檔案只需要把 `Mp::` 換成 `Le::Hook::`：

```cpp
namespace Le::Hook {

struct HookSpec
{
    PVOID   Target;      // 要攔截的函式
    PVOID   Detour;      // LE 的取代函式
    PVOID*  Original;    // 輸出：呼叫原函式用的指標（可為 nullptr）
};

HookSpec Jump(PVOID Target, PVOID Detour, PVOID* Original = nullptr);

NTSTATUS Install(const HookSpec* Specs, ULONG_PTR Count);
NTSTATUS Uninstall(PVOID* Original);   // 對應現有的 Mp::RestoreMemory

// 只有 x64 實作使用；x86 走 HookPort
NTSTATUS InstallSyscall(PCSTR NtName, PVOID Detour, PVOID* Original);

} // namespace Le::Hook
```

- **x86**：`Install` 內部轉成 `Mp::PATCH_MEMORY_DATA` 並呼叫 `Mp::PatchMemory`，行為完全不變
- **x64**：`Install` 使用新的引擎
- `Mp::FunctionCallVa`（以 `call` 方式 patch，目前只用在 `LdrInitNtContinue`）**不放進共用介面**。x64 需要時，以 MASM thunk 個別處理（研究報告 §3.4）

### 4.4 NLS 模擬策略（x64）

1. **先做實驗（Phase 3）**：Loader 在 resume 前改寫遠端 PEB 的 `AnsiCodePageData` / `OemCodePageData` / `UnicodeCaseTableData`，**不裝任何 hook**，觀察 LocaleTestApp 的哪些 `CHECK_*` 變成 OK。
2. 無論實驗結果如何，都移植 `LeRtl*` hook（鐵則 6）。實驗只影響 kernelbase 是否需要額外處理。

---

## 5. 分階段計畫

每個 phase 都由數個「可獨立 commit、CI 全綠」的切片組成。`src/Core/` 的 commit 一律加 `[Core]` 前綴。

| Phase | 名稱 | 內容 | 完成條件 | 計畫文件 |
|---|---|---|---|---|
| **0** | 決策 | 確認 §3.2；修改 AGENTS.md 鐵則 | 使用者確認 | 本文件 |
| **1** | 地基 | 刪 HookPort 死碼、Core.props 依平台參數化、拆分 .def、CoreRuntime 依架構分段、LeArch.h + 佈局 assert、修正 ml 的 x64 錯誤、**x64 能編譯（hook 暫時停用）**、Core.Tests 加 x64 | CI：x86 全綠 + x64 建置成功 | `plans/2026-11-11-core-phase1-foundation.md` |
| **2** | HookEngine 切點（x86） | 建立 `Le::Hook` 介面與 x86 實作；四個 hook 檔改用新介面 | x86 smoke test 全綠，行為不變 | Phase 1 完成後撰寫 |
| **3** | x64 引擎 + NLS 實驗 | Zydis、near relay、syscall stub 複製；C++ 單元測試（hook 測試程式自己的函式）；LocaleTestApp x64；PEB 改寫實驗 | x64 hook 單元測試全綠；實驗結果記錄 | Phase 2 完成後撰寫 |
| **4** | x64 功能移植 | `LeRtl*`、`NtQueryDefault*`、時區 hook | x64 smoke test 的 NLS / LCID `CHECK_*` 全綠 | 依 Phase 3 結果撰寫 |
| **5** | 啟動鏈 | `TargetArchitectureDetector`（C#，含單元測試）、LEProc64 建置、組裝 x64 產物 | 右鍵 64 位元程式能正確模擬 | — |
| **6** | 進階功能 | user32 / gdi32 / win32u hook、x64 → x64 子程序注入 | 對應的 `CHECK_*` 全綠 | — |
| **7** | 減少 ml 依賴 | 依盤點報告的順序：巨集、NT 宣告、程序／記憶體、PE、容器 | 每一步 x86 / x64 都全綠 | 可與 Phase 3～6 穿插 |

### 5.1 為什麼 Phase 1 要讓 x64「能編譯」

x64 的編譯錯誤數量現在無法預估（ml 的 x64 路徑從未建置過）。讓它先編譯過、hook 先停用，可以：
- 把「編譯問題」和「hook 正確性問題」分開處理
- 讓 CI 從此持續保護 x64 建置，避免之後的修改再弄壞
- 及早發現 ml 還有哪些 x64 錯誤

---

## 6. 測試策略

| 層級 | 現有 | 新增 |
|---|---|---|
| C# 單元測試 | LEProc / LECommonLibrary / LEGUI | `TargetArchitectureDetector`（以小型測試 PE 檔案作為 fixture）、LEProc64 分派 |
| 結構佈局 | Core.Tests（x86，手抄結構） | Core.Tests x64 組態；`LeArch.h` 的編譯期 `static_assert`（PEB / TEB 位移） |
| Hook 引擎 | 無 | `HookEngine.Tests`（x86 + x64）：hook 測試程式自己的函式，驗證 detour 被呼叫、原函式可呼叫、解除後恢復；覆蓋 RIP-relative、短函式、syscall stub 等情況 |
| 端對端 | Smoke test（x86） | LocaleTestApp x64 組態；smoke test 同時跑 x86 與 x64 |
| 行為不變性 | — | Phase 1、2 的重構切片：x86 smoke test 全綠是必要條件 |

CI 調整：
- 新增 `Build Core (Release|x64)` step
- C++ 測試已經會跑 `Build/Release/x64/*Tests*.exe`，新增的 x64 測試只要輸出到該目錄即可
- smoke test 在 Phase 4 擴充成兩種架構

---

## 7. 風險

| 風險 | 機率 | 影響 | 對策 |
|---|---|---|---|
| ml 的 x64 編譯錯誤遠多於預期 | 中 | Phase 1 延長 | 以 CI log 迭代；必要時把 x64 不需要的 ml 區段用 `#if` 排除 |
| PEB 改寫實驗失敗 | 中 | 只能靠 hook，kernelbase 可能需要額外處理 | 實驗只是加分項；`LeRtl*` hook 是既定做法 |
| 某些 x64 遊戲啟用 CET / ACG / 反作弊 | 中 | 這些程式無法模擬 | 偵測並給出明確錯誤；列為已知限制 |
| 重構切片意外改變 x86 行為 | 低 | 回歸 | 每個切片都跑 smoke test；切片要小 |
| 開發環境沒有 Windows | 高 | 回饋慢 | 一律以 CI 驗證；可以加速時再用 Remote Control 連到使用者的 Windows 電腦 |

---

## 8. 文件更新

| 時機 | 檔案 | 內容 |
|---|---|---|
| Phase 0 | `AGENTS.md` | 鐵則 1、2；C++ 建置組態表（新增 `Release\|x64` 欄位的預告） |
| Phase 1 結束 | `AGENTS.md`、`src/Core/README.md` | Core 的 x64 組態、.def 目錄結構、CoreRuntime 分段說明 |
| Phase 5 結束 | `README.md` | 支援 64 位元程式、產物清單 |
