<#
.SYNOPSIS
    Locale Emulator Smoke Test - 驗證 LEProc 對不同 locale 的 hook 效果。

.DESCRIPTION
    使用 LEProc 搭配不同 locale profile 啟動 LocaleTestApp.exe，
    擷取其 GetACP()/GetOEMCP()/GetUserDefaultLCID() 輸出並比對預期值。

.PARAMETER BuildDir
    建置輸出目錄路徑（預設為 repo 根目錄下 Build\Release\x86）

.PARAMETER Verbose
    顯示詳細輸出

.EXAMPLE
    .\Invoke-SmokeTest.ps1
    .\Invoke-SmokeTest.ps1 -BuildDir "C:\MyBuild\Release\x86"
#>

[CmdletBinding()]
param(
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\..\Build\Release\x86")
)

$ErrorActionPreference = "Stop"

# ============================================================
# Smoke Test Matrix
# ============================================================
# 每個 locale profile 定義：
#   - Name:    人類可讀名稱
#   - Region:  locale 名稱（對應 LEConfig.xml <Region>；v3.x 從 <Location> 改名）
#   - ACP:     預期 Active Code Page
#   - OEMCP:   預期 OEM Code Page
#   - LCID:    預期 Locale ID（十六進位字串，不含 0x 前綴）
# ============================================================

$TestMatrix = @(
    @{
        Name   = "Japanese (ja-JP)"
        Region = "ja-JP"
        ACP    = 932
        OEMCP  = 932
        LCID   = "0411"
    },
    @{
        Name   = "Traditional Chinese (zh-TW)"
        Region = "zh-TW"
        ACP    = 950
        OEMCP  = 950
        LCID   = "0404"
    },
    @{
        Name   = "Simplified Chinese (zh-CN)"
        Region = "zh-CN"
        ACP    = 936
        OEMCP  = 936
        LCID   = "0804"
    },
    @{
        # LEGUI 在 ICU 全球化模式下曾存成 zh-Hant-TW（LCID 0x1000，無法模擬）；
        # 啟用 UseNls 後應解析為 zh-TW。
        Name   = "Traditional Chinese (legacy name zh-Hant-TW)"
        Region = "zh-Hant-TW"
        ACP    = 950
        OEMCP  = 950
        LCID   = "0404"
    },
    @{
        Name   = "Korean (ko-KR)"
        Region = "ko-KR"
        ACP    = 949
        OEMCP  = 949
        LCID   = "0412"
    },
    @{
        Name   = "English (en-US)"
        Region = "en-US"
        ACP    = 1252
        OEMCP  = 437
        LCID   = "0409"
    }
)

# ============================================================
# Helper Functions
# ============================================================

function Write-TestHeader {
    Write-Host ""
    Write-Host "============================================================" -ForegroundColor Cyan
    Write-Host " Locale Emulator Smoke Test" -ForegroundColor Cyan
    Write-Host " $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Gray
    Write-Host "============================================================" -ForegroundColor Cyan
    Write-Host ""
}

function Write-TestResult {
    param(
        [string]$TestName,
        [bool]$Passed,
        [string]$Detail = ""
    )
    if ($Passed) {
        Write-Host "  [PASS] $TestName" -ForegroundColor Green
    } else {
        Write-Host "  [FAIL] $TestName" -ForegroundColor Red
        if ($Detail) {
            Write-Host "         $Detail" -ForegroundColor Yellow
        }
    }
}

function Parse-LocaleTestOutput {
    param([string[]]$OutputLines)

    $result = @{}
    foreach ($line in $OutputLines) {
        if ($line -match '^(\w+)=(.+)$') {
            $result[$Matches[1]] = $Matches[2].Trim()
        }
    }
    return $result
}

# ============================================================
# Main
# ============================================================

Write-TestHeader

# 確認必要檔案存在
$leProcPath = Join-Path $BuildDir "LEProc.exe"
$testAppPath = Join-Path $BuildDir "LocaleTestApp.exe"
$loaderDllPath = Join-Path $BuildDir "LoaderDll.dll"
$localeEmulatorDllPath = Join-Path $BuildDir "LocaleEmulator.dll"

$prerequisites = @(
    @{ Path = $leProcPath;             Name = "LEProc.exe" },
    @{ Path = $testAppPath;            Name = "LocaleTestApp.exe" },
    @{ Path = $loaderDllPath;          Name = "LoaderDll.dll" },
    @{ Path = $localeEmulatorDllPath;  Name = "LocaleEmulator.dll" }
)

Write-Host "Checking prerequisites..." -ForegroundColor White
$allPresent = $true
foreach ($prereq in $prerequisites) {
    if (Test-Path $prereq.Path) {
        Write-Host "  [OK] $($prereq.Name)" -ForegroundColor Green
    } else {
        Write-Host "  [MISSING] $($prereq.Name) - $($prereq.Path)" -ForegroundColor Red
        $allPresent = $false
    }
}

if (-not $allPresent) {
    Write-Host ""
    Write-Host "ERROR: Missing prerequisite files. Cannot proceed." -ForegroundColor Red
    Write-Host "Ensure all projects are built and Core DLLs are in place." -ForegroundColor Yellow
    exit 1
}

# 先不透過 LEProc 直接跑一次，記錄 baseline
Write-Host ""
Write-Host "Running baseline (no locale emulation)..." -ForegroundColor White
try {
    $baselineResultFile = Join-Path (Split-Path $testAppPath) "locale_result.txt"
    if (Test-Path $baselineResultFile) { Remove-Item $baselineResultFile -Force }
    & $testAppPath 2>&1 | Out-Null
    # 等待結果檔案產生
    $waitCount = 0
    while (-not (Test-Path $baselineResultFile) -and $waitCount -lt 20) {
        Start-Sleep -Milliseconds 500
        $waitCount++
    }
    if (Test-Path $baselineResultFile) {
        $baselineLines = Get-Content $baselineResultFile
        $baseline = Parse-LocaleTestOutput $baselineLines
        Write-Host "  System ACP=$($baseline.ACP), OEMCP=$($baseline.OEMCP), LCID=$($baseline.LCID)" -ForegroundColor Gray
        Remove-Item $baselineResultFile -Force
    } else {
        Write-Host "  [WARN] Baseline locale_result.txt not produced within timeout" -ForegroundColor Yellow
    }
} catch {
    Write-Host "  [ERROR] Failed to run baseline: $_" -ForegroundColor Red
}

# 執行各 locale 測試
$totalTests = 0
$passedTests = 0
$failedTests = 0
# PROBE_* 只回報不判定（尋找 LE 漏攔的 API 用，見 tests/SmokeTest/AGENTS.md）
$probeFailures = @()

Write-Host ""
Write-Host "Running locale smoke tests..." -ForegroundColor White

# 診斷 log（LE_DIAG_LOG）預設關閉：主測試期間把 TEMP / TMP 指到空目錄、清掉 LE_DIAG_LOG，
# 跑完後檢查這個目錄沒有產生 log（見 Test-DiagLog）。
$savedEnv = @{ TEMP = $env:TEMP; TMP = $env:TMP; LE_DIAG_LOG = $env:LE_DIAG_LOG }
$diagRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("LeDiagSmoke_" + [guid]::NewGuid().ToString('N'))
$offTemp = Join-Path $diagRoot "off-temp"
New-Item -ItemType Directory -Path $offTemp -Force | Out-Null
$env:TEMP = $offTemp
$env:TMP = $offTemp
Remove-Item Env:LE_DIAG_LOG -ErrorAction SilentlyContinue

foreach ($test in $TestMatrix) {
    Write-Host ""
    Write-Host "--- $($test.Name) ---" -ForegroundColor Cyan

    $totalTests++

    try {
        # 透過 LEProc -run 啟動 LocaleTestApp
        # LEProc 會根據 Region 參數設定 locale 並建立程序
        # 注意：實際參數格式可能因遷移後 LEProc CLI 變化而需調整
        $tempConfigPath = $testAppPath + ".le.config"

        # 產生臨時 .le.config 檔案（必須放在目標程式旁邊，LEProc -run 會讀取 <target-path>.le.config）
        # Note: <Region> 是 v3.x 後的 XML schema（從 <Location> 重新命名）
        $configXml = @"
<?xml version="1.0" encoding="utf-8"?>
<LEConfig>
  <Profiles>
    <Profile Name="SmokeTest_$($test.Region)" Guid="{$(New-Guid)}" MainMenu="false">
      <Parameter></Parameter>
      <Region>$($test.Region)</Region>
      <Timezone>Tokyo Standard Time</Timezone>
      <RunAsAdmin>false</RunAsAdmin>
      <RedirectRegistry>true</RedirectRegistry>
      <IsAdvancedRedirection>false</IsAdvancedRedirection>
      <RunWithSuspend>false</RunWithSuspend>
    </Profile>
  </Profiles>
</LEConfig>
"@
        $configXml | Out-File -FilePath $tempConfigPath -Encoding UTF8

        # 使用 LEProc -run 執行目標程式
        # 重要：LEProc 透過 LeCreateProcess 建立新程序，目標 app 的 stdout
        # 不會回傳到 LEProc。因此 LocaleTestApp 將結果寫入檔案。
        $resultFile = Join-Path (Split-Path $testAppPath) "locale_result.txt"
        if (Test-Path $resultFile) { Remove-Item $resultFile -Force }

        & $leProcPath -run $testAppPath 2>&1 | Out-Null

        # 等待目標程式完成並寫入結果檔
        $waitCount = 0
        while (-not (Test-Path $resultFile) -and $waitCount -lt 30) {
            Start-Sleep -Milliseconds 500
            $waitCount++
        }

        if (-not (Test-Path $resultFile)) {
            throw "LocaleTestApp 未產出 locale_result.txt（等待 15 秒後逾時）"
        }

        $outputLines = Get-Content $resultFile
        $parsed = Parse-LocaleTestOutput $outputLines

        # 比對結果
        $testPassed = $true
        $details = @()

        # 比對 ACP
        $actualACP = [int]($parsed.ACP)
        if ($actualACP -ne $test.ACP) {
            $testPassed = $false
            $details += "ACP: expected=$($test.ACP), actual=$actualACP"
        } else {
            Write-Verbose "  ACP=$actualACP (expected $($test.ACP)) OK"
        }

        # 比對 OEMCP
        $actualOEMCP = [int]($parsed.OEMCP)
        if ($actualOEMCP -ne $test.OEMCP) {
            $testPassed = $false
            $details += "OEMCP: expected=$($test.OEMCP), actual=$actualOEMCP"
        } else {
            Write-Verbose "  OEMCP=$actualOEMCP (expected $($test.OEMCP)) OK"
        }

        # 比對 LCID（十六進位比對）
        $actualLCID = $parsed.LCID -replace '^0x', ''
        $expectedLCID = $test.LCID
        if ($actualLCID -ne $expectedLCID) {
            $testPassed = $false
            $details += "LCID: expected=0x$expectedLCID, actual=0x$actualLCID"
        } else {
            Write-Verbose "  LCID=0x$actualLCID (expected 0x$expectedLCID) OK"
        }

        # 時區：profile 固定為 Tokyo Standard Time（UTC+9，Bias=-540，無日光節約）
        if ($parsed.TZBIAS -ne '-540') {
            $testPassed = $false
            $details += "TZBIAS: expected=-540, actual=$($parsed.TZBIAS)"
        } else {
            Write-Verbose "  TZBIAS=-540 OK"
        }

        # 時機實驗版的 LocaleEmulator.dll 會留下 LE_EXPERIMENT 標記（正式版沒有）
        if ($parsed.ContainsKey('LE_EXPERIMENT')) {
            Write-Host "  LE_EXPERIMENT=$($parsed.LE_EXPERIMENT)" -ForegroundColor Gray
        }

        # 比對 CHECK_*：各層 ANSI/OEM <-> Unicode 轉換必須使用 profile 的 code page
        $checkKeys = @($parsed.Keys | Where-Object { $_ -like 'CHECK_*' } | Sort-Object)
        if ($checkKeys.Count -eq 0) {
            $testPassed = $false
            $details += "no CHECK_* lines in output"
        }
        foreach ($key in $checkKeys) {
            if ($parsed[$key] -ne 'OK') {
                $testPassed = $false
                $details += "$($key -replace '^CHECK_', ''): $($parsed[$key])"
            } else {
                Write-Verbose "  $key OK"
            }
        }

        # PROBE_*：列出失敗項目但不影響結果；-Verbose 時也列出 OK 與 INFO_*
        $probeKeys = @($parsed.Keys | Where-Object { $_ -like 'PROBE_*' } | Sort-Object)
        $probeFailed = @($probeKeys | Where-Object { $parsed[$_] -ne 'OK' })
        Write-Host "  PROBE: $($probeKeys.Count - $probeFailed.Count)/$($probeKeys.Count) OK" -ForegroundColor Gray
        foreach ($key in $probeFailed) {
            Write-Host "  [PROBE FAIL] $($key -replace '^PROBE_', ''): $($parsed[$key])" -ForegroundColor Yellow
            $probeFailures += "$($test.Region) $($key -replace '^PROBE_', '')"
        }
        foreach ($key in @($parsed.Keys | Where-Object { $_ -like 'INFO_*' } | Sort-Object)) {
            Write-Verbose "  $key=$($parsed[$key])"
        }

        if ($testPassed) {
            $passedTests++
            Write-TestResult -TestName $test.Name -Passed $true
        } else {
            $failedTests++
            Write-TestResult -TestName $test.Name -Passed $false -Detail ($details -join "; ")
        }

        # 清理臨時檔案
        if (Test-Path $tempConfigPath) { Remove-Item $tempConfigPath -Force }
        if (Test-Path $resultFile) { Remove-Item $resultFile -Force }

    } catch {
        $failedTests++
        Write-TestResult -TestName $test.Name -Passed $false -Detail "Exception: $_"
    }
}

# ============================================================
# 診斷 log（LE_DIAG_LOG）
# ============================================================
# 以 ja-JP 再跑兩次：LE_DIAG_LOG=1（預設目錄 %TEMP%\LocaleEmulator）與 LE_DIAG_LOG=<絕對路徑>，
# 檢查 LEProc（loader）與 LocaleTestApp（target）各有一份 log、內容含初始化與注入結果。
# 另外檢查上面 6 個地區（未設定 LE_DIAG_LOG）沒有產生任何 log。

function Invoke-DiagLogRun {
    param([string]$LogDir)

    $tempConfigPath = $testAppPath + ".le.config"
    $resultFile = Join-Path (Split-Path $testAppPath) "locale_result.txt"
    @"
<?xml version="1.0" encoding="utf-8"?>
<LEConfig>
  <Profiles>
    <Profile Name="SmokeTest_DiagLog" Guid="{$(New-Guid)}" MainMenu="false">
      <Parameter></Parameter>
      <Region>ja-JP</Region>
      <Timezone>Tokyo Standard Time</Timezone>
      <RunAsAdmin>false</RunAsAdmin>
      <RedirectRegistry>true</RedirectRegistry>
      <IsAdvancedRedirection>false</IsAdvancedRedirection>
      <RunWithSuspend>false</RunWithSuspend>
    </Profile>
  </Profiles>
</LEConfig>
"@ | Out-File -FilePath $tempConfigPath -Encoding UTF8
    if (Test-Path $resultFile) { Remove-Item $resultFile -Force }

    try {
        & $leProcPath -run $testAppPath 2>&1 | Out-Null
        # LEProc 會等目標結束；保險起見再等 LocaleTestApp 結束，讓 log 寫完、關檔
        Get-Process LocaleTestApp -ErrorAction SilentlyContinue | Wait-Process -Timeout 15 -ErrorAction SilentlyContinue
    } finally {
        if (Test-Path $tempConfigPath) { Remove-Item $tempConfigPath -Force }
        if (Test-Path $resultFile) { Remove-Item $resultFile -Force }
    }

    $problems = @()
    $targetLogs = @(Get-ChildItem -Path $LogDir -Filter "LocaleTestApp.exe.*.log" -ErrorAction SilentlyContinue)
    $loaderLogs = @(Get-ChildItem -Path $LogDir -Filter "LEProc.exe.*.log" -ErrorAction SilentlyContinue)
    if ($targetLogs.Count -ne 1) { $problems += "expected 1 LocaleTestApp log in $LogDir, found $($targetLogs.Count)" }
    if ($loaderLogs.Count -ne 1) { $problems += "expected 1 LEProc log in $LogDir, found $($loaderLogs.Count)" }

    $expect = @()
    if ($targetLogs.Count -eq 1) {
        $text = Get-Content $targetLogs[0].FullName -Raw -Encoding Unicode
        Write-Verbose "---- $($targetLogs[0].Name) ----`n$text"
        $expect += @(
            @{ Log = $targetLogs[0].Name; Text = $text; Pattern = 'mode: target' },
            @{ Log = $targetLogs[0].Name; Text = $text; Pattern = 'leb: from launcher' },
            @{ Log = $targetLogs[0].Name; Text = $text; Pattern = 'emulate: ACP=932 OEMCP=932 LCID=0x0411' },
            @{ Log = $targetLogs[0].Name; Text = $text; Pattern = 'InstallHookPort: status=0x00000000' },
            @{ Log = $targetLogs[0].Name; Text = $text; Pattern = 'init ok' },
            @{ Log = $targetLogs[0].Name; Text = $text; Pattern = 'hook (?i:USER32\.dll): status=0x00000000' },
            @{ Log = $targetLogs[0].Name; Text = $text; Pattern = 'dll load (?i:user32\.dll)' }
        )
    }
    if ($loaderLogs.Count -eq 1) {
        $text = Get-Content $loaderLogs[0].FullName -Raw -Encoding Unicode
        Write-Verbose "---- $($loaderLogs[0].Name) ----`n$text"
        $expect += @(
            @{ Log = $loaderLogs[0].Name; Text = $text; Pattern = 'mode: loader' },
            @{ Log = $loaderLogs[0].Name; Text = $text; Pattern = 'create process: .*LocaleTestApp\.exe' },
            @{ Log = $loaderLogs[0].Name; Text = $text; Pattern = 'inject: pid=\d+ status=0x00000000' }
        )
    }
    foreach ($e in $expect) {
        if ($e.Text -notmatch $e.Pattern) { $problems += "$($e.Log): missing /$($e.Pattern)/" }
    }
    return $problems
}

Write-Host ""
Write-Host "--- Diagnostic log (LE_DIAG_LOG) ---" -ForegroundColor Cyan
$totalTests++
try {
    $problems = @()

    # 1. 預設關閉：上面的地區測試沒有在 TEMP 底下產生任何東西
    $leaked = @(Get-ChildItem -Path $offTemp -Recurse -Filter "*.log" -ErrorAction SilentlyContinue)
    if ($leaked.Count -ne 0) { $problems += "LE_DIAG_LOG unset but logs were written: $($leaked.Name -join ', ')" }

    # 2. LE_DIAG_LOG=1 → %TEMP%\LocaleEmulator
    $onTemp = Join-Path $diagRoot "on-temp"
    New-Item -ItemType Directory -Path $onTemp -Force | Out-Null
    $env:TEMP = $onTemp
    $env:TMP = $onTemp
    $env:LE_DIAG_LOG = "1"
    $problems += @(Invoke-DiagLogRun -LogDir (Join-Path $onTemp "LocaleEmulator") | ForEach-Object { "[LE_DIAG_LOG=1] $_" })

    # 3. LE_DIAG_LOG=<絕對路徑>
    $explicitDir = Join-Path $diagRoot "explicit"
    $env:LE_DIAG_LOG = $explicitDir
    $problems += @(Invoke-DiagLogRun -LogDir $explicitDir | ForEach-Object { "[LE_DIAG_LOG=<dir>] $_" })

    if ($problems.Count -eq 0) {
        $passedTests++
        Write-TestResult -TestName "Diagnostic log" -Passed $true
    } else {
        $failedTests++
        Write-TestResult -TestName "Diagnostic log" -Passed $false -Detail ($problems -join "; ")
    }
} catch {
    $failedTests++
    Write-TestResult -TestName "Diagnostic log" -Passed $false -Detail "Exception: $_"
} finally {
    foreach ($name in $savedEnv.Keys) {
        if ($null -eq $savedEnv[$name]) { Remove-Item "Env:$name" -ErrorAction SilentlyContinue }
        else { Set-Item "Env:$name" $savedEnv[$name] }
    }
    Remove-Item $diagRoot -Recurse -Force -ErrorAction SilentlyContinue
}

# ============================================================
# Summary
# ============================================================
Write-Host ""
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host " Summary" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  Total:  $totalTests" -ForegroundColor White
Write-Host "  Passed: $passedTests" -ForegroundColor Green
Write-Host "  Failed: $failedTests" -ForegroundColor $(if ($failedTests -gt 0) { "Red" } else { "Green" })
Write-Host "  PROBE failures (informational): $($probeFailures.Count)" -ForegroundColor $(if ($probeFailures.Count -gt 0) { "Yellow" } else { "Gray" })
foreach ($f in $probeFailures) { Write-Host "    $f" -ForegroundColor Yellow }
Write-Host ""

if ($failedTests -gt 0) {
    Write-Host "SMOKE TEST FAILED" -ForegroundColor Red
    exit 1
} else {
    Write-Host "ALL SMOKE TESTS PASSED" -ForegroundColor Green
    exit 0
}
