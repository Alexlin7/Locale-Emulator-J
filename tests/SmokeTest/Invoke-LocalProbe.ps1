<#
.SYNOPSIS
    在本機跑 LocaleTestApp 兩次（不經 LE、經 LE），產生 PROBE_* / CHECK_* 的比對報告。

.DESCRIPTION
    用來在使用者自己的電腦上找出 LE 漏攔的 API（例如系統 ACP 65001 / UTF-8 beta 下的亂碼）。
    給本機的 coding agent 或使用者執行；步驟與注意事項見 tests/SmokeTest/AGENTS.md。

    1. 把 LocaleTestApp.exe 複製到 -OutDir 的工作資料夾（不動建置目錄與任何遊戲資料夾）
    2. 直接執行一次，存成 baseline.txt
    3. 在旁邊寫 .le.config，經 -LeDir 的 LEProc.exe -run 執行一次，存成 le.txt
    4. 產生 report.md：環境、ACP 等基本值、所有非 OK 的 CHECK_* / PROBE_*、INFO_* 對照

    本腳本不改系統設定。UTF-8 beta 的開關要改系統設定並重新開機，請使用者自己切換。

.PARAMETER LeDir
    含 LEProc.exe、LoaderDll.dll、LocaleEmulator.dll 的目錄：使用者安裝的 LE，或本機建置的 Build\Release\x86

.PARAMETER TestApp
    LocaleTestApp.exe 的路徑（預設 Build\Release\x86\LocaleTestApp.exe）

.PARAMETER OutDir
    工作與輸出資料夾（預設 %TEMP%\LEProbe_<時間>）

.EXAMPLE
    .\tests\SmokeTest\Invoke-LocalProbe.ps1 -LeDir "C:\Tools\Locale Emulator"
    .\tests\SmokeTest\Invoke-LocalProbe.ps1 -LeDir .\Build\Release\x86 -Region ja-JP -RedirectRegistry $true
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$LeDir,
    [string]$TestApp = (Join-Path $PSScriptRoot "..\..\Build\Release\x86\LocaleTestApp.exe"),
    [string]$OutDir = (Join-Path $env:TEMP ("LEProbe_" + (Get-Date -Format "yyyyMMdd_HHmmss"))),
    [string]$Region = "ja-JP",
    [string]$Timezone = "Tokyo Standard Time",
    [bool]$RedirectRegistry = $true,
    [bool]$AdvancedRedirection = $false,
    [int]$TimeoutSeconds = 30
)

$ErrorActionPreference = "Stop"

function Read-Result([string]$Path) {
    $result = [ordered]@{}
    foreach ($line in Get-Content $Path -Encoding UTF8) {
        if ($line -match '^(\w+)=(.*)$') { $result[$Matches[1]] = $Matches[2].Trim() }
    }
    return $result
}

function Wait-ForFile([string]$Path, [int]$Seconds) {
    $deadline = (Get-Date).AddSeconds($Seconds)
    while (-not (Test-Path $Path) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
    if (-not (Test-Path $Path)) { return $false }
    # LocaleTestApp writes the file in one go; give it a moment to close it.
    Start-Sleep -Milliseconds 500
    return $true
}

# ---- Prerequisites ----
$LeDir = (Resolve-Path $LeDir).Path
$leProc = Join-Path $LeDir "LEProc.exe"
foreach ($f in "LEProc.exe", "LoaderDll.dll", "LocaleEmulator.dll") {
    if (-not (Test-Path (Join-Path $LeDir $f))) { throw "找不到 $f（-LeDir $LeDir）" }
}
if (-not (Test-Path $TestApp)) { throw "找不到 LocaleTestApp.exe（-TestApp $TestApp）；先建置或從 CI artifact 取得" }

New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$workDir = Join-Path $OutDir "work"
New-Item -ItemType Directory -Path $workDir -Force | Out-Null
$app = Join-Path $workDir "LocaleTestApp.exe"
Copy-Item $TestApp $app -Force
$resultFile = Join-Path $workDir "locale_result.txt"
$configFile = "$app.le.config"

# ---- System information (read outside LE, so not redirected) ----
$nls = Get-ItemProperty "HKLM:\SYSTEM\CurrentControlSet\Control\Nls\CodePage"
$os = Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion"
$systemInfo = [ordered]@{
    "OS"               = "$($os.ProductName) $($os.DisplayVersion) build $($os.CurrentBuild).$($os.UBR)"
    "System ACP"       = $nls.ACP
    "System OEMCP"     = $nls.OEMCP
    "UTF-8 beta"       = $(if ($nls.ACP -eq "65001") { "on" } else { "off" })
    "LE directory"     = $LeDir
    "LE version"       = (Get-Item $leProc).VersionInfo.FileVersion
}

# ---- 1. Baseline (no LE) ----
Write-Host "Running baseline (no LE)..." -ForegroundColor Cyan
Remove-Item $resultFile, $configFile -Force -ErrorAction SilentlyContinue
Start-Process -FilePath $app -WorkingDirectory $workDir -WindowStyle Hidden -Wait
if (-not (Wait-ForFile $resultFile 5)) { throw "baseline：LocaleTestApp 沒有產生 locale_result.txt" }
Move-Item $resultFile (Join-Path $OutDir "baseline.txt") -Force

# ---- 2. Through LE ----
Write-Host "Running through LE ($Region)..." -ForegroundColor Cyan
@"
<?xml version="1.0" encoding="utf-8"?>
<LEConfig>
  <Profiles>
    <Profile Name="LocalProbe_$Region" Guid="{$(New-Guid)}" MainMenu="false">
      <Parameter></Parameter>
      <Region>$Region</Region>
      <Timezone>$Timezone</Timezone>
      <RunAsAdmin>false</RunAsAdmin>
      <RedirectRegistry>$($RedirectRegistry.ToString().ToLower())</RedirectRegistry>
      <IsAdvancedRedirection>$($AdvancedRedirection.ToString().ToLower())</IsAdvancedRedirection>
      <RunWithSuspend>false</RunWithSuspend>
    </Profile>
  </Profiles>
</LEConfig>
"@ | Out-File -FilePath $configFile -Encoding UTF8

try {
    & $leProc -run $app 2>&1 | Out-Null
    if (-not (Wait-ForFile $resultFile $TimeoutSeconds)) {
        throw "經 LE 執行：LocaleTestApp 沒有在 $TimeoutSeconds 秒內產生 locale_result.txt（LE 可能沒有成功啟動它）"
    }
    Move-Item $resultFile (Join-Path $OutDir "le.txt") -Force
} finally {
    Remove-Item $configFile -Force -ErrorAction SilentlyContinue
}

# ---- 3. Report ----
$base = Read-Result (Join-Path $OutDir "baseline.txt")
$le = Read-Result (Join-Path $OutDir "le.txt")

$lines = @()
$lines += "# LE local probe report"
$lines += ""
$lines += "Generated: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
$lines += ""
$lines += "## Environment"
$lines += ""
foreach ($k in $systemInfo.Keys) { $lines += "- ${k}: $($systemInfo[$k])" }
$lines += "- Profile: Region=$Region, Timezone=$Timezone, RedirectRegistry=$RedirectRegistry, AdvancedRedirection=$AdvancedRedirection"
$lines += ""
$lines += "## Basic values"
$lines += ""
$lines += "| Key | Baseline (no LE) | Through LE |"
$lines += "|---|---|---|"
foreach ($k in "ACP", "OEMCP", "LCID", "UILANG", "THREADLOCALE", "TZBIAS", "LE_EXPERIMENT") {
    if ($base.Contains($k) -or $le.Contains($k)) { $lines += "| $k | $($base[$k]) | $($le[$k]) |" }
}

$failKeys = @($le.Keys | Where-Object { ($_ -like 'CHECK_*' -or $_ -like 'PROBE_*') -and $le[$_] -ne 'OK' })
$okCount = @($le.Keys | Where-Object { ($_ -like 'CHECK_*' -or $_ -like 'PROBE_*') -and $le[$_] -eq 'OK' }).Count
$lines += ""
$lines += "## Failures through LE ($($failKeys.Count) failed, $okCount OK)"
$lines += ""
if ($failKeys.Count -eq 0) {
    $lines += "None."
} else {
    $lines += "| Check | Through LE | Baseline (no LE) |"
    $lines += "|---|---|---|"
    foreach ($k in $failKeys) { $lines += "| $k | $($le[$k]) | $($base[$k]) |" }
}

$lines += ""
$lines += "## INFO"
$lines += ""
$lines += "| Key | Baseline (no LE) | Through LE |"
$lines += "|---|---|---|"
$infoKeys = @(@($base.Keys) + @($le.Keys) | Where-Object { $_ -like 'INFO_*' } | Sort-Object -Unique)
foreach ($k in $infoKeys) { $lines += "| $k | $($base[$k]) | $($le[$k]) |" }

$report = Join-Path $OutDir "report.md"
$lines | Out-File -FilePath $report -Encoding UTF8
Remove-Item $workDir -Recurse -Force -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "System ACP: $($systemInfo['System ACP'])  (UTF-8 beta: $($systemInfo['UTF-8 beta']))" -ForegroundColor White
Write-Host "Through LE: ACP=$($le['ACP']) OEMCP=$($le['OEMCP']) LCID=$($le['LCID'])" -ForegroundColor White
Write-Host "Failures through LE: $($failKeys.Count)" -ForegroundColor $(if ($failKeys.Count -gt 0) { "Yellow" } else { "Green" })
foreach ($k in $failKeys) { Write-Host "  $k = $($le[$k])" -ForegroundColor Yellow }
Write-Host ""
Write-Host "Report: $report" -ForegroundColor Cyan
Write-Host "Raw results: $(Join-Path $OutDir 'baseline.txt'), $(Join-Path $OutDir 'le.txt')" -ForegroundColor Gray
