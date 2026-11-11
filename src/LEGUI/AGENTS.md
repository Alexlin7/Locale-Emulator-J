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
| `ShellExtCliCommand.cs` | 以命令列參數執行安裝 / 解除 |
| `I18n.cs`、`Lang/` | 語系載入 |
| `AboutPanel.xaml` | 關於頁；**對外連結只放可信來源**（根目錄開發約定） |

## 測試

```powershell
dotnet test tests/LEGUI.Tests/
```

`ShellExtensionRegistrarTests`、`ShellExtCliCommandTests`、`ShellExtensionPanelTests`、`ProfileEditorControlTests`、`I18nTests`、`LocaleCompletenessTests`。
