# 設計文件：Core 模組化重構與 x64 支援

> **日期**：2026-11-11
> **狀態**：§3.1、§3.2 已確認；§3.3（D7）待 §4.5 的實驗結果
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

### 3.3 提案中（待實驗結果）

| ID | 項目 | 提案 | 決定條件 |
|---|---|---|---|
| **D7** | 兩段式 Core（E1、E2 已全 PASS，見 §4.5） | 把注入 DLL 拆成兩段：**第一段**維持無 CRT、只依賴 ntdll，只做一定要在 kernel32 之前完成的事；**第二段**是一般 C++ DLL（靜態 CRT `/MT`、STL、C++23 函式庫），其餘 hook 都放這裡 | §4.5 的時機實驗（E1～E3）。實驗結果決定第一段要留哪些東西，或是否連第一段都不需要 |

背景：
- 現在的無 CRT 限制，來自 LE 選擇在 ntdll 第一次呼叫 `LdrLoadDll`（載入 kernel32）時注入（鐵則 4）。這樣做的理由是 kernelbase 初始化時會讀取 PEB 的 code page / NLS 資料並快取，LE 要在它讀之前改好 PEB。
- 鐵則 6 已確認，新版 Windows 上替換 ntdll NLS 表的做法失效，ANSI/OEM 轉換實際靠 `LeRtl*` hook。「一定要這麼早」的理由已經比當年弱，值得用實驗重新確認。
- 其他工具多半選擇晚一點注入，換取完整 CRT。例如 Microsoft Detours 的 `DetourCreateProcessWithDllEx` 把 DLL 插成主程式匯入表的第一項，DLL 在 kernel32 之後、主程式其他 DLL 與進入點之前載入。Locale Remulator 就是用 Detours 實作；本專案不直接採用它，是因為它的維護太慢。
- D7 只借用「分段、晚段可以用 CRT」的思路，不引入 Detours（hook 引擎依 Q4 使用 Zydis 自寫）。

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

### 4.5 兩段式 Core（D7）與時機實驗

**目標形狀（若 D7 成立）：**

```
LoaderDll ─ 建立暫停的程序，hook 第一次 LdrLoadDll（現有機制）
  ↓
第一段：LocaleEmulator(64).dll        無 CRT、只依賴 ntdll、盡量小
  │ 只做實驗證明「一定要在 kernel32 之前」的事（候選：改寫 PEB、ntdll 的 Rtl* hook）
  │ 原本的 LdrLoadDll(kernel32) 回傳後 → LdrLoadDll(第二段)
  ↓
第二段：LeHooks(64).dll（暫名）        一般 C++：/MT、STL、C++23 函式庫
  │ kernel32 / user32 / gdi32 / 時區等 hook、registry 轉向
  ↓
目標程式的其他 DLL 與進入點
```

- **載入第二段的位置**：放在第一段的 `LdrLoadDll` hook 裡、原呼叫回傳之後。不能放在 `LdrRegisterDllNotification` 的回呼裡，那時持有 loader lock。程序初始化階段這樣呼叫 `LdrLoadDll` 是否安全要實驗確認；LE 現在本來就在同一個時間點載入自己，這是一個正面跡象。
- **第二段的 CRT**：靜態連結（`/MT`），不依賴目標程式帶的 VC++ runtime；每個 DLL 各有一份 CRT 狀態，不與目標程式共用。
- **對 x64 的影響**：如果 D7 成立，x64 的大部分 hook 可以直接寫在第二段，Zydis 也不必用 no-libc 模式。所以實驗要排在 Phase 3（x64 引擎）之前。

**現在的初始化順序**（`LeGlobalData::Initialize`，x86）：

1. 載入 NLS 檔、改寫 PEB 的 `AnsiCodePageData` / `OemCodePageData` / `UnicodeCaseTableData`
2. `InstallHookPort()`：syscall hook
3. `HookNtdllRoutines()`：包含 `LeRtl*`
4. 註冊 DLL 載入通知；kernel32、user32、gdi32 等載入時再各自 hook

**實驗（x86，用 smoke test 的 `CHECK_*` 判定）：**

| 實驗 | 做法 | 回答的問題 |
|---|---|---|
| **E0 基準** | 現行版本 | 每個 `CHECK_*` 目前都是 `OK`（現況） |
| **E1 延後改寫 PEB** | 第 1 步移到原本的 `LdrLoadDll(kernel32)` 回傳之後再做，其他不變 | kernelbase 是否還依賴「初始化前改好 PEB」 |
| **E2 全部延後** | 整個 `Initialize` 都移到 `LdrLoadDll(kernel32)` 回傳之後；這模擬 Detours（匯入表）的注入時機 | 是否連第一段都不需要；結果全 `OK` 代表可以改用單段、完整 CRT 的設計 |
| **E3 逐項延後** | 只在 E2 有 `FAIL` 時才做：一次只把一個步驟（PEB、HookPort、ntdll hook）留在前面，其他延後 | 找出哪些步驟一定要放第一段 |

實作方式：
- 用編譯期巨集（例如 `LE_EXPERIMENT_TIMING=1/2`）切換，只在 CI 的實驗步驟另外建一份 x86 `LocaleEmulator.dll` 來跑 smoke test。正式產物不受影響。
- 實驗步驟允許失敗（`continue-on-error`），只記錄每個 `CHECK_*` 的結果。
- 結果寫回本節，據此把 D7 移到「已確定」或「否決」。

**結果（2026-11-11，CI run 42964350577，commit `12ff732`，GitHub Actions `windows-2025-vs2026`，系統 ACP 1252 / OEMCP 437 / LCID 0x0409）：**

| 實驗 | 延後路徑確實執行（`LE_EXPERIMENT` 標記） | ja-JP | zh-TW | zh-CN | zh-Hant-TW | ko-KR | en-US |
|---|---|:---:|:---:|:---:|:---:|:---:|:---:|
| E0 基準 | — | PASS | PASS | PASS | PASS | PASS | PASS |
| E1 延後改寫 PEB | `E1:peb-after-kernel32` | PASS | PASS | PASS | PASS | PASS | PASS |
| E2 全部延後 | `E2:init-after-kernel32` | PASS | PASS | PASS | PASS | PASS | PASS |

- 每個語系的 ACP、OEMCP、LCID 與 22 個 `CHECK_*` 全部 `OK`（E1、E2 合計 264 個 `CHECK_*`）。en-US 與 runner 的系統設定相同，分辨不出差異；其他五個語系都和系統設定不同，結果有鑑別力。
- 第一次跑時兩份實驗版都 PASS，但無法證明延後路徑真的執行，所以補了 `LE_EXPERIMENT` 標記：實驗版在延後的時間點設定這個環境變數，LocaleTestApp 輸出，smoke test 顯示。正式版沒有這個標記。
- 實驗沒有用到 E3（E2 沒有 `FAIL`）。

**解讀：**
- 在 smoke test 涵蓋的範圍內，**LE 的初始化不需要早於 kernel32 / kernelbase 的 `DLL_PROCESS_ATTACH`**。kernelbase 的 `CP_ACP` 在 E1、E2 都正確，原因應該是 kernel32 載入時的 `HackAnsiOemCodeHashNodes`（寫入 PEB 的 code page 欄位並重建 kernelbase 的 code page hash 節點）本來就在 kernel32 之後執行，它取代了「在 kernelbase 初始化前改好 PEB」的必要性。這一點是推論，尚未單獨驗證。
- E2 的時機是在 kernel32 的進入點回傳之後、主程式的其他 DLL 與進入點之前，和 Detours 匯入表注入的時機相同或更早。所以「改成 kernel32 之後再初始化」在 smoke test 範圍內可行。
- **但這還不代表可以直接用 CRT。** E2 的 DLL 本身仍然在 kernel32 之前被載入，只是初始化延後，所以它依然不能靜態 import kernel32 或 CRT。要用 CRT，必須讓「用 CRT 的那顆 DLL」在 kernel32 之後才載入。因此 D7 的形狀調整為：
  - **第一段**縮到最小：只負責在 kernel32 初始化完成後載入第二段（現有的注入機制不變）。
  - **第二段**：所有初始化與 hook，可以使用 CRT。
  - 另一個選項是改掉注入機制（例如匯入表注入），讓第一段完全消失。但這會動到 LoaderDll 與 `ml` 的 `CreateProcessWithDll`，範圍大很多，先不採用。

**擴充涵蓋範圍後再跑一次（Task C3，CI run 42965336674，commit `8987222`）：**

新增 10 個 `CHECK_*`，smoke test 的 profile 改為 `RedirectRegistry=true`，並檢查時區：

| 類別 | 新增項目 |
|---|---|
| kernelbase | `GETCPINFO_ACP`、`GETCPINFO_OEMCP`、`CP_THREAD_ACP`、`CP_OEMCP`、`LOCALEINFO_ACP`、`USER_DEFAULT_LOCALE_NAME` |
| registry 轉向 | `REGISTRY_NLS_ACP`、`REGISTRY_NLS_OEMCP`（`HKLM\System\CurrentControlSet\Control\Nls\CodePage`） |
| gdi32 | `GDI32_STOCK_FONT_CHARSET`（`DEFAULT_GUI_FONT`）、`GDI32_COMPATIBLE_DC_CHARSET`（`CreateCompatibleDC`） |
| 時區 | `GetTimeZoneInformation` 的 `Bias` 必須是 -540（Tokyo Standard Time；runner 是 UTC） |

| 實驗 | 6 個語系 | `CHECK_*`（每語系 32 個） | 時區 |
|---|---|---|---|
| E0 正式版 | 6/6 PASS | 192/192 OK | 6/6 |
| E1 | 6/6 PASS | 192/192 OK | 6/6 |
| E2 | 6/6 PASS | 192/192 OK | 6/6 |

新項目在 E0 也全部 `OK`，代表它們反映的是 LE 現有的行為，不是新發現的缺口；E2 同樣全部 `OK`，結論不變。

**D7 定案前還要做的事：**
1. ~~擴充 smoke test 的涵蓋範圍，再跑一次 E2~~：已完成，見上表。仍未涵蓋：user32 的訊息轉換（`SendMessageA` 等）、gdi32 的 `EnumFontFamilies*`、`IsAdvancedRedirection` 的 HKCU 項目與 UI 語言 API。
2. 由使用者挑幾個實際遊戲，在自己的電腦上用 E2 版測試（不擅自掃描使用者的遊戲資料夾）。
3. 兩項都沒問題，就把 D7 移到「已確定」，並另寫兩段式的實作計畫。

**實驗的限制：**
- smoke test 只涵蓋 LocaleTestApp 會檢查的 API（`GetACP`、`GetOEMCP`、`GetUserDefaultLCID`、`Rtl*` 轉換、kernelbase `CP_ACP`、ANSI 檔案 API、user32 視窗標題）。kernelbase 也許還快取了別的東西，LocaleTestApp 沒有檢查到。
- 遊戲可能在自己的 DLL 的 DllMain 就讀 code page。E2 的時機仍然早於主程式其他 DLL，理論上沒問題，但實際遊戲要由使用者在自己的電腦上驗證（不擅自掃描或執行使用者的遊戲資料夾）。
- 需要時可以為 LocaleTestApp 新增 `CHECK_*`，例如 `GetCPInfo`、`MultiByteToWideChar(CP_THREAD_ACP)`、`GetLocaleInfo`，讓實驗涵蓋更多。

---

## 5. 分階段計畫

每個 phase 都由數個「可獨立 commit、CI 全綠」的切片組成。`src/Core/` 的 commit 一律加 `[Core]` 前綴。

| Phase | 名稱 | 內容 | 完成條件 | 計畫文件 |
|---|---|---|---|---|
| **0** | 決策 | 確認 §3.2；修改 AGENTS.md 鐵則 | 使用者確認 | 本文件 |
| **1** | 地基 | 刪 HookPort 死碼、Core.props 依平台參數化、拆分 .def、CoreRuntime 依架構分段、LeArch.h + 佈局 assert、修正 ml 的 x64 錯誤、**x64 能編譯（hook 暫時停用）**、Core.Tests 加 x64 | CI：x86 全綠 + x64 建置成功 | `plans/2026-11-11-core-phase1-foundation.md` |
| **2** | HookEngine 切點（x86） | 建立 `Le::Hook` 介面與 x86 實作；四個 hook 檔改用新介面 | x86 smoke test 全綠，行為不變 | Phase 1 完成後撰寫 |
| **2E** | 時機實驗（D7） | §4.5 的 E0～E3（x86）；必要時補 LocaleTestApp 的 `CHECK_*` | 實驗結果寫回 §4.5；D7 定案 | Phase 2 計畫 Part C（排在 Part B 之前） |
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
| D7 實驗結果模稜兩可（smoke test 涵蓋不足） | 中 | D7 無法定案 | 先補 `CHECK_*`；仍不確定就維持單段無 CRT，D7 延後 |
| 兩段式在程序初始化期間載入第二段失敗或造成死結 | 低 | D7 不可行 | E2 會直接暴露；失敗就維持現況 |
| 開發環境沒有 Windows | 高 | 回饋慢 | 一律以 CI 驗證；可以加速時再用 Remote Control 連到使用者的 Windows 電腦 |

---

## 8. 文件更新

| 時機 | 檔案 | 內容 |
|---|---|---|
| Phase 0 | `AGENTS.md` | 鐵則 1、2；C++ 建置組態表（新增 `Release\|x64` 欄位的預告） |
| Phase 1 結束 | `AGENTS.md`、`src/Core/README.md` | Core 的 x64 組態、.def 目錄結構、CoreRuntime 分段說明 |
| Phase 5 結束 | `README.md` | 支援 64 位元程式、產物清單 |
