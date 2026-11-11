# src/LECommonLibrary/AGENTS.md

LECommonLibrary（.NET 10，AnyCPU）是 LEProc 與 LEGUI 共用的程式庫：設定檔讀寫、Profile 資料模型、可模擬地區的判斷與 OS 工具函式。

跨模組鐵則見根目錄 [`AGENTS.md`](../../AGENTS.md)。

## 鐵則

1. **`RegionSupport` 是「這個地區能不能模擬」的唯一判斷入口**（`IsSupported`、`GetSupportedCultures`、`TryGetSupportedCulture`）。LEGUI 的地區清單與 LEProc 的解析都要經過它。
2. **設定檔 schema 有兩個解析端**：這裡的 `LEConfig` / `LEProfile`（C#）與 ShellExtension 的 `ConfigReader`（C++，pugixml）。改 schema 要兩邊一起改，並考慮既有使用者的設定檔（目前只讀 `<Region>`，上游的 `<Location>` 沒有相容處理）。
3. **AnyCPU**：會被 x86 的 LEProc 與 AnyCPU 的 LEGUI 載入，不要加入特定架構的原生相依。

## 設定檔格式

`LEConfig.xml`（全域）與 `<程式路徑>.le.config`（單一程式）共用同一個 schema：

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

## 結構

| 檔案 | 內容 |
|---|---|
| `LEConfig.cs`、`LEProfile.cs` | 設定檔讀寫與 Profile 模型 |
| `RegionSupport.cs` | 可模擬地區判斷 |
| `PEFileReader.cs` | 讀 PE header：`GetPEType` 回傳 `X32` / `X64` / `Unknown`，x64 分派（Phase 5）可以從這裡延伸 |
| `AssociationReader.cs` | 檔案關聯（非 exe 檔案用關聯程式啟動） |
| `SystemHelper.cs` | OS 工具函式（系統管理員判斷等） |
| `GlobalHelper.cs` | 版本字串、錯誤訊息框、`CheckCoreDLLs`（檢查 Core DLL 是否存在） |

## 測試

```powershell
dotnet test tests/LECommonLibrary.Tests/
```

`LEConfigTests`、`LEProfileTests`、`RegionSupportTests`、`PEFileReaderTests`、`SystemHelperTests`。
