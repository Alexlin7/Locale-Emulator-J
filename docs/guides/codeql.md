# CodeQL 靜態分析

## 概要

`.github/workflows/codeql.yml` 在 push / PR 到 `master`、每週三，以及手動觸發時執行：

| Job | 內容 |
|---|---|
| `Custom query tests`（ubuntu） | 下載 CodeQL bundle，跑 `.github/codeql/le-queries-tests` 的查詢單元測試 |
| `Analyze (csharp)` | C#，`build-mode: none`（不需建置），查詢組合 `security-extended` |
| `Analyze (c-cpp)` | C/C++，以 msbuild 建 Core x86（LoaderDll、LocaleEmulator）與 ShellExtension x64；`security-extended` 加上專案自訂查詢 |

結果在 repo 的 **Security → Code scanning**，PR 上會直接標出新增的問題。

- 上游的 `ml.h` / `ml.cpp` 與第三方的 `pugixml` 會在上傳前從 SARIF 濾掉（`codeql-config.yml` 的 `paths-ignore` 對需要編譯的語言不一定生效）。
- C/C++ job 會檢查 SARIF 裡確實有自訂查詢的規則，沒有就失敗，避免自訂查詢沒跑卻顯示「沒有問題」。

## 檔案

```
.github/codeql/
  codeql-config.yml            共用設定（查詢組合、paths-ignore）
  codeql-workspace.yml         讓本地的兩個 pack 互相找到
  le-queries/                  自訂查詢 pack（le/core-queries）
    CoreNoKernelbaseNls.ql     le/core/kernelbase-nls-call
  le-queries-tests/            查詢單元測試（le/core-queries-tests）
    Core/NoKernelbaseNls/            在 Core 範圍內：應報與不應報的案例
    ShellExtension/NoKernelbaseNls/  在 Core 範圍外：不應報
```

## 自訂查詢

### `le/core/kernelbase-nls-call`

對應 `src/Core/AGENTS.md` 的鐵則 3：Core（排除 ml）不得使用 kernel32 / kernelbase 的 code page、地區、時區 API。會抓：

- 直接呼叫（例如 `MultiByteToWideChar(...)`）
- 執行時查找：`KERNEL32_API(Name)`、`LookupExportTable(..., KERNEL32_Name)`、以名稱字串傳給 `GetProcAddress` / `GetRoutineAddress` 等

連結階段的檢查只能擋靜態 import，後兩種只有這條查詢抓得到。

**豁免**：確實需要時，在同一行或上一行加 `// LE-ALLOW-KERNELBASE: <理由>`。目前只有 `src/Core/LocaleEmulator/Hooks/Kernel32Hook.cpp` 的三處（刻意操作 kernelbase 的地區快取）。

## 在本機跑查詢測試

不需要 Windows。下載 bundle（約 700 MB）後：

```bash
curl -sSL -o codeql.tgz https://github.com/github/codeql-action/releases/latest/download/codeql-bundle-linux64.tar.gz
```

```bash
tar -xzf codeql.tgz
```

```bash
cd .github/codeql && /path/to/codeql/codeql test run le-queries-tests
```

## 新增自訂查詢的流程

1. 在 `le-queries-tests/` 下新增測試目錄（C++ 測試只會擷取目錄**第一層**的 `.cpp`），寫應報（行尾加 `// $ Alert`）與不應報的案例，`.qlref` 寫：
   ```
   query: <查詢檔名>.ql
   postprocess: utils/test/InlineExpectationsTestQuery.ql
   ```
2. 先用空殼查詢跑一次，確認測試會失敗（`Missing result: Alert`）。注意「extractor found nothing to extract」代表測試是空跑，結果不可信。
3. 寫查詢到測試通過，`codeql test run --learn` 產生 `.expected`，檢查內容後一起 commit。
4. 反向驗證：暫時拿掉查詢的某個條件，確認反例測試會失敗。
5. CI 的 C/C++ job 若要檢查新規則確實執行，在 `Filter SARIF` 步驟加上它的 id。
