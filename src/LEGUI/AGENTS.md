# src/LEGUI/AGENTS.md

LEGUI（.NET 10，WPF，AnyCPU）是 Profile 管理介面，也負責安裝與解除 Shell Extension。

跨模組鐵則見根目錄 [`AGENTS.md`](../../AGENTS.md)。

## 鐵則

1. **字串改動要處理全部 22 種語系**：i18n 是 `Lang/` 底下的 XAML resource dictionary（`DefaultLanguage.xaml` 加 21 種翻譯）。`tests/LEGUI.Tests/LocaleCompletenessTests.cs` 會檢查各語系的鍵是否齊全。
2. **Shell Extension 的 CLSID**（`ShellExtensionConstants.cs`）必須與 `src/ShellExtension/dllmain.cpp` 相同。
3. **地區清單一律來自 `LECommonLibrary.RegionSupport`**，不要自己列舉 `CultureInfo`。

## 結構

| 檔案 | 內容 |
|---|---|
| `GlobalConfig.xaml`、`AppConfig.xaml` | 全域設定（`LEConfig.xml`）與單一程式設定（`*.le.config`）的視窗 |
| `ProfileEditorControl.xaml` | Profile 編輯元件（兩個視窗共用） |
| `ShellExtensionPanel.xaml`、`ShellExtensionRegistrar.cs` | Shell Extension 的安裝 / 解除（以 Registry API 註冊，經 `IRegistryOperations` 抽象以便測試） |
| `ShellExtCliCommand.cs` | 以命令列參數執行安裝 / 解除（`--shell-ext ...`），以及 `--installer` 的判斷 |
| `InstallerWindow.xaml` | `LEGUI.exe --installer`（由 LEInstaller.exe 開啟）：只有 `ShellExtensionPanel` 的小視窗；不檢查目錄寫入權限、不建立 `LEConfig.xml` |
| `LegacyInstallation.cs` | 從原版 LE 2.x 的右鍵選單註冊（舊 CLSID 的 `InprocServer32` 與版本子機碼的 `CodeBase`，HKCU / HKLM × 64 / 32 位元 view）找出舊版資料夾的 `LEConfig.xml` |
| `InputBox.xaml` | 單行輸入對話框（另存全域 profile 的名稱）：依內容排版、套用 `UIFont`；Enter / Esc 用按鈕的 `IsDefault` / `IsCancel`；空白時確認鈕停用 |
| `I18n.cs`、`Lang/` | 語系載入 |
| `AboutPanel.xaml` | 關於頁；**對外連結只放可信來源**（根目錄開發約定） |

## 常見陷阱

- **匯入舊版設定要在清除舊版註冊之前**：舊版資料夾只能從那筆註冊得知，清掉就找不到了（`ShellExtensionPanel.bCleanupOld_Click`）。匯入在目前（未提權）的程序內進行，因為寫的是本安裝目錄的 `LEConfig.xml`；清除 HKLM 才經 `--shell-ext` 子程序提權。
- **建立 WPF 控制項的測試類別要加 `[Collection(WpfUiCollection.Name)]`**：xUnit 會平行跑不同類別，`InitializeComponent` 同時在多個 STA 執行緒呼叫會偶發 `ArgumentOutOfRangeException`（`PackagePart`）。
- 測試裡沒有 WPF `Application`，`I18n.GetString` 只會回傳鍵名；要驗證訊息內容時，用面板的 `FormatMessage` / `ShowMessage` 替換。
- 全域設定的按鈕列每欄約 82px 文字寬，喬治亞文、印尼文、法文、德文、俄文、日文、韓文的「另存為」都接近或超過；加按鈕前先量各語系的字串寬度。

## 測試

```powershell
dotnet test tests/LEGUI.Tests/
```

`ShellExtensionRegistrarTests`、`ShellExtCliCommandTests`、`ShellExtensionPanelTests`、`ShellExtensionPanelImportTests`、`LegacyInstallationTests`、`InputBoxTests`、`ProfileEditorControlTests`、`I18nTests`、`LocaleCompletenessTests`。
