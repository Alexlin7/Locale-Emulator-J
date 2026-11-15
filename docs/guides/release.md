# 發佈 Release

`.github/workflows/release.yml` 手動觸發，只能在 `master` 上跑。流程：

1. **prepare**：檢查版本號格式（`3.0.0`、`3.0.0-beta.1`，不加 `v`）、主版本部分與 `LEProc` / `LEGUI` / `LECommonLibrary` 三個 csproj 的 `<Version>` 相同、`v<版本>` 的 Release 與 tag 都還不存在。
2. **ci**：呼叫 `ci.yml`（`workflow_call`），跑完整的建置、.NET / C++ 測試與 smoke test。
3. **release**：從 CI 上傳的 `build-output` 打包，建立**草稿** Release。版本號含 `-` 時標成 pre-release。

## 步驟

1. 確認要發佈的內容都已合併進 `master`。主版本要變時，先改三個 csproj 的 `<Version>` 並合併。
2. 觸發：

   ```powershell
   gh workflow run Release --ref master -f version=3.0.0-beta.1
   gh run watch   # 或到 Actions 頁面看
   ```

3. 到 GitHub 的 Releases 頁面打開草稿，檢查附件與說明，再按 **Publish release**。tag（`v3.0.0-beta.1`）在 Publish 時才建立，指向觸發時的 `master` commit。
4. 發佈後更新 `docs/roadmap.md`。

不要的草稿直接在 GitHub 上刪掉即可（還沒有 tag，不會留下痕跡）。

## 附件內容

| 檔案 | 內容 |
|---|---|
| `LocaleEmulator-J-v<版本>.zip` | `LEInstaller.exe`、`x86/`（LEProc、LEGUI、LECommonLibrary、LoaderDll、LocaleEmulator、ShellExtension、`Lang/`）、`x64/ShellExtension.dll`、README 與授權 |
| `LocaleEmulator-J-v<版本>-symbols.zip` | LEInstaller、LEProc、LEGUI、LECommonLibrary、LoaderDll、LocaleEmulator、ShellExtension（x86 / x64）的 `.pdb`，查 dump 用；符號必須與同一次建置的檔案對應，事後重建的 pdb 通常對不上 |
| `SHA256SUMS.txt` | 兩個 zip 的 SHA-256 |

- **目錄結構必須是 `x86/` 與 `x64/` 並列**：LEGUI 以 `LEGUI.exe` 的上兩層找 `x64\ShellExtension.dll`（`ShellExtensionRegistrar.GetBuildOutputRoot`）。
- **不放 `LEConfig.xml`**：使用者的全域 profile 在這個檔，解壓覆蓋舊版時才不會被蓋掉。smoke test 會在 `Build/Release/x86/` 產生它，所以打包時是挑檔案複製，而不是整個目錄複製。
- 不放測試程式（`*Tests*.exe`、`LocaleTestApp.exe`）與 `.ilk` / `.exp` / `.lib`。
- Release 說明 = `.github/release-notes.md`（安裝、升級、限制）+ GitHub 自動產生的變更清單。安裝步驟或限制改變時一起更新。

## 尚未處理

- 沒有程式碼簽章：SmartScreen 與防毒可能警告，說明裡有寫。
- `.NET 10 Desktop Runtime`（x86 + x64）沒有打包進 zip：`LEInstaller.exe` 會檢查並可代為下載安裝（framework-dependent 部署的取捨見 `docs/roadmap.md`）。
