<#
.SYNOPSIS
    以 LEProc -run 連續啟動 LocaleTestApp N 次，統計 LEProc 偶發當機（0xC0000005）的機率。

.DESCRIPTION
    用來重現與量測 docs/roadmap.md「查 LEProc 偶發當機」（計畫見
    docs/superpowers/plans/2026-11-12-leproc-intermittent-crash.md）。

    每次啟動記錄：
      OK       LEProc 結束碼 0，目標產生 locale_result.txt 且 ACP 正確
      CRASH    LEProc 結束碼不是 0（Main 回傳 void，正常結束一律是 0），記錄 hex 值，
               例如 0xC0000005（存取違規）、0xC0000409、0x80131506（CoreCLR fatal error）
      TIMEOUT  LEProc 沒有在 -TimeoutSeconds 內結束。kill 之前先用 createdump 取 full dump，
               並記錄主視窗標題：LeCreateProcess 回傳錯誤時 LEProc 會跳 MessageBox（系統管理員），
               所以 TIMEOUT 可能是卡住，也可能是錯誤對話框
      NORESULT LEProc 正常結束，但目標沒有產生 locale_result.txt
      WRONG    目標的 ACP 不是預期值

    另外：
      - 設定 WER LocalDumps 收集 LEProc.exe 的 full dump（需要系統管理員；不行就略過），結束時還原
      - 收集期間 Application 事件記錄中 LEProc.exe 的 Application Error（Event ID 1000），內含 faulting module 與 offset
      - 找得到 cdb.exe（Windows SDK Debugging Tools x86）時，對每個 dump 跑 !analyze -v 與所有執行緒的堆疊

    本腳本只在 CI 或使用者同意的環境執行，目標程式只有 LocaleTestApp。
    結束碼：0 = 全部 OK，1 = 有失敗（呼叫端自行決定要不要當成錯誤）。

.PARAMETER BuildDir
    含 LEProc.exe、LoaderDll.dll、LocaleEmulator.dll、LocaleTestApp.exe 的目錄（預設 Build\Release\x86）

.PARAMETER Count
    啟動次數（預設 100）

.PARAMETER OutDir
    輸出目錄（預設 Build\LaunchStress）

.EXAMPLE
    .\tests\SmokeTest\Invoke-LaunchStress.ps1 -Count 200
#>

[CmdletBinding()]
param(
    [string]$BuildDir = (Join-Path $PSScriptRoot "..\..\Build\Release\x86"),
    [int]$Count = 100,
    [string]$OutDir = (Join-Path $PSScriptRoot "..\..\Build\LaunchStress"),
    [string]$Region = "ja-JP",
    [int]$ExpectedACP = 932,
    [int]$TimeoutSeconds = 30
)

$ErrorActionPreference = "Stop"

$BuildDir = (Resolve-Path $BuildDir).Path
$leProc = Join-Path $BuildDir "LEProc.exe"
$testApp = Join-Path $BuildDir "LocaleTestApp.exe"
foreach ($f in $leProc, $testApp, (Join-Path $BuildDir "LoaderDll.dll"), (Join-Path $BuildDir "LocaleEmulator.dll")) {
    if (-not (Test-Path $f)) { throw "找不到 $f" }
}

if (Test-Path $OutDir) { Remove-Item $OutDir -Recurse -Force }
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$dumpDir = Join-Path $OutDir "dumps"
New-Item -ItemType Directory -Path $dumpDir -Force | Out-Null

# The target writes locale_result.txt next to itself; LEProc reads <target>.le.config.
# Run against a copy so the build directory's LocaleTestApp is untouched.
$workDir = Join-Path $OutDir "work"
New-Item -ItemType Directory -Path $workDir -Force | Out-Null
Get-ChildItem $BuildDir -File | Copy-Item -Destination $workDir -Force
Get-ChildItem $BuildDir -Directory | Copy-Item -Destination $workDir -Recurse -Force
$leProc = Join-Path $workDir "LEProc.exe"
$app = Join-Path $workDir "LocaleTestApp.exe"
$resultFile = Join-Path $workDir "locale_result.txt"

@"
<?xml version="1.0" encoding="utf-8"?>
<LEConfig>
  <Profiles>
    <Profile Name="LaunchStress_$Region" Guid="{$(New-Guid)}" MainMenu="false">
      <Parameter></Parameter>
      <Region>$Region</Region>
      <Timezone>Tokyo Standard Time</Timezone>
      <RunAsAdmin>false</RunAsAdmin>
      <RedirectRegistry>true</RedirectRegistry>
      <IsAdvancedRedirection>false</IsAdvancedRedirection>
      <RunWithSuspend>false</RunWithSuspend>
    </Profile>
  </Profiles>
</LEConfig>
"@ | Out-File -FilePath "$app.le.config" -Encoding UTF8

# ---- WER LocalDumps (best effort) ----
$werKey = "HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\LEProc.exe"
$werSet = $false
try {
    New-Item -Path $werKey -Force | Out-Null
    New-ItemProperty -Path $werKey -Name DumpFolder -Value $dumpDir -PropertyType ExpandString -Force | Out-Null
    New-ItemProperty -Path $werKey -Name DumpType -Value 2 -PropertyType DWord -Force | Out-Null
    New-ItemProperty -Path $werKey -Name DumpCount -Value 5 -PropertyType DWord -Force | Out-Null
    $werSet = $true
} catch {
    Write-Host "[INFO] 無法設定 WER LocalDumps（需要系統管理員），不收集 dump：$($_.Exception.Message)" -ForegroundColor Yellow
}

# createdump.exe ships with the x86 .NET runtime; used to dump a hung LEProc before killing it.
$createDump = Get-ChildItem "${env:ProgramFiles(x86)}\dotnet\shared\Microsoft.NETCore.App" -Filter createdump.exe -Recurse -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
$hangDumps = 0

$startTime = Get-Date
$results = New-Object System.Collections.Generic.List[object]

try {
    for ($i = 1; $i -le $Count; $i++) {
        Remove-Item $resultFile -Force -ErrorAction SilentlyContinue

        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        $p = Start-Process -FilePath $leProc -ArgumentList @("-run", "`"$app`"") -WorkingDirectory $workDir -PassThru
        $null = $p.Handle  # cache the handle so ExitCode stays readable after exit
        $exited = $p.WaitForExit($TimeoutSeconds * 1000)
        $sw.Stop()

        $status = "OK"
        $exitCode = $null
        $detail = ""

        if (-not $exited) {
            $status = "TIMEOUT"
            try { $p.Refresh(); $detail = "window='$($p.MainWindowTitle)'" } catch { }
            if ($createDump -and $hangDumps -lt 3) {
                & $createDump -f (Join-Path $dumpDir "LEProc.hang.$($p.Id).dmp") $p.Id 2>&1 | Out-Null
                $hangDumps++
            }
            try { $p.Kill() } catch { }
            Get-Process LocaleTestApp -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
        } else {
            $p.WaitForExit()
            $exitCode = $p.ExitCode
            $unsigned = [uint32]([int64]$exitCode -band 0xFFFFFFFF)
            if ($exitCode -ne 0) {
                $status = "CRASH"
                $detail = ("0x{0:X8}" -f $unsigned)
            } else {
                # LEProc waits for the target, so the result should already be there.
                $wait = 0
                while (-not (Test-Path $resultFile) -and $wait -lt 10) { Start-Sleep -Milliseconds 200; $wait++ }
                if (-not (Test-Path $resultFile)) {
                    $status = "NORESULT"
                } else {
                    $acpLine = Get-Content $resultFile | Where-Object { $_ -match '^ACP=' } | Select-Object -First 1
                    $acp = if ($acpLine) { [int]($acpLine -replace '^ACP=', '') } else { -1 }
                    if ($acp -ne $ExpectedACP) { $status = "WRONG"; $detail = "ACP=$acp" }
                }
            }
        }

        $results.Add([pscustomobject]@{
            Run      = $i
            Status   = $status
            ExitCode = $exitCode
            Detail   = $detail
            Ms       = $sw.ElapsedMilliseconds
        })
        if ($status -ne "OK") { Write-Host ("  #{0}: {1} {2}" -f $i, $status, $detail) -ForegroundColor Yellow }
        elseif ($i % 25 -eq 0) { Write-Host "  #${i}: OK" }
    }
} finally {
    Remove-Item "$app.le.config" -Force -ErrorAction SilentlyContinue
    if ($werSet) { Remove-Item $werKey -Recurse -Force -ErrorAction SilentlyContinue }
}

# ---- Event log: Application Error for LEProc.exe ----
$events = @()
try {
    $events = @(Get-WinEvent -FilterHashtable @{ LogName = "Application"; Id = 1000; StartTime = $startTime } -ErrorAction SilentlyContinue |
        Where-Object { $_.Message -match 'LEProc\.exe' })
} catch { }

# ---- Dump analysis (best effort) ----
$dumps = @(Get-ChildItem $dumpDir -Filter *.dmp -ErrorAction SilentlyContinue)
$cdb = @(
    "${env:ProgramFiles(x86)}\Windows Kits\10\Debuggers\x86\cdb.exe",
    "$env:ProgramFiles\Windows Kits\10\Debuggers\x86\cdb.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
$analyses = @()
if ($dumps.Count -gt 0 -and $cdb) {
    # Core PDBs (built next to the DLLs) + Microsoft symbol server.
    $symDir = Join-Path $OutDir "symbols"
    New-Item -ItemType Directory -Path $symDir -Force | Out-Null
    Get-ChildItem (Join-Path $PSScriptRoot "..\..\src\Core") -Filter *.pdb -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\Release\\' } | Copy-Item -Destination $symDir -Force
    $env:_NT_SYMBOL_PATH = "$symDir;srv*$(Join-Path $env:TEMP 'LeSymCache')*https://msdl.microsoft.com/download/symbols"
    foreach ($d in ($dumps | Select-Object -First 5)) {
        $txt = Join-Path $OutDir ($d.BaseName + ".analyze.txt")
        & $cdb -z $d.FullName -c ".reload; !analyze -v; .ecxr; r; kb 40; ~*kb 30; lm; q" 2>&1 | Out-File $txt -Encoding UTF8
        $analyses += $txt
    }
}

# ---- Report ----
$groups = $results | Group-Object Status | Sort-Object Name
$failCount = @($results | Where-Object { $_.Status -ne "OK" }).Count

$lines = @()
$lines += "# LEProc launch stress"
$lines += ""
$lines += "- Runs: $Count, Region: $Region (expected ACP $ExpectedACP)"
$lines += "- Failures: $failCount"
foreach ($g in $groups) { $lines += "  - $($g.Name): $($g.Count)" }
$avg = [int](($results | Measure-Object Ms -Average).Average)
$lines += "- Average LEProc wall time: $avg ms"
$lines += "- Dumps: $($dumps.Count) (WER LocalDumps $(if ($werSet) { 'enabled' } else { 'not enabled' }); createdump: $(if ($createDump) { 'found' } else { 'not found' })); cdb: $(if ($cdb) { $cdb } else { 'not found' })"
$lines += "- Application Error events for LEProc.exe: $($events.Count)"
$lines += ""
if ($failCount -gt 0) {
    $lines += "## Failed runs"
    $lines += ""
    $lines += "| Run | Status | Detail | ms |"
    $lines += "|---|---|---|---|"
    foreach ($r in ($results | Where-Object { $_.Status -ne "OK" })) {
        $lines += "| $($r.Run) | $($r.Status) | $($r.Detail) | $($r.Ms) |"
    }
    $lines += ""
}
if ($events.Count -gt 0) {
    $lines += "## Application Error events (first 5)"
    $lines += ""
    foreach ($e in ($events | Select-Object -First 5)) {
        $lines += '```'
        $lines += $e.Message
        $lines += '```'
    }
    $lines += ""
}
foreach ($a in $analyses) {
    $lines += "## $(Split-Path $a -Leaf)"
    $lines += ""
    $content = Get-Content $a
    $start = ($content | Select-String -Pattern 'FAULTING_IP|EXCEPTION_RECORD|STACK_TEXT|FAILURE_BUCKET_ID|MODULE_NAME' | Select-Object -First 1)
    $excerpt = if ($start) { $content[([Math]::Max(0, $start.LineNumber - 1))..([Math]::Min($content.Count - 1, $start.LineNumber + 80))] } else { $content | Select-Object -First 80 }
    $lines += '```'
    $lines += $excerpt
    $lines += '```'
    $lines += ""
}

$report = Join-Path $OutDir "report.md"
$lines | Out-File -FilePath $report -Encoding UTF8
$results | Export-Csv -Path (Join-Path $OutDir "runs.csv") -NoTypeInformation -Encoding UTF8
Remove-Item $workDir -Recurse -Force -ErrorAction SilentlyContinue

Write-Host ""
$lines | Select-Object -First 12 | ForEach-Object { Write-Host $_ }
Write-Host "Report: $report" -ForegroundColor Cyan

if ($failCount -gt 0) { exit 1 } else { exit 0 }
