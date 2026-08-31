param(
    [string]$Preset = 'local-test',
    [string]$Regex = '',
    [switch]$List,
    [switch]$RerunFailed,
    [switch]$StopOnFailure,
    [string]$ShowTest = '',
    [string]$LogFile = ''
)
# Test helper: runs ctest with failure output, tees the full log, prints a compact summary.
# Per-test verbose output lives in the log and in build/<preset>/Testing/Temporary/LastTest.log.
# Repo root is found by walking up to CMakeLists.txt, so this works from any checkout.
#
# Modes:
#   (default)        run ctest; -Regex filters, -RerunFailed re-runs only previously
#                    failed tests, -StopOnFailure aborts at the first failing test.
#   -List            list matching tests without running them (ctest -N).
#   -ShowTest <name> do not run ctest; print the full output block of one test from
#                    the previous run's LastTest.log.
$ErrorActionPreference = 'Continue'

$repoRoot = $PSScriptRoot
while ($repoRoot -and -not (Test-Path (Join-Path $repoRoot 'CMakeLists.txt'))) {
    $repoRoot = Split-Path $repoRoot -Parent
}
if (-not $repoRoot) { Write-Host 'repo root (CMakeLists.txt) not found above script location'; exit 2 }
if (-not $LogFile) { $LogFile = Join-Path $repoRoot "build/$Preset/ctest.log" }
$lastTestLog = Join-Path $repoRoot "build/$Preset/Testing/Temporary/LastTest.log"

# -ShowTest works without the VS dev shell; handle it before the slow shell setup.
if ($ShowTest) {
    if (-not (Test-Path $lastTestLog)) { Write-Host "no LastTest.log at $lastTestLog; run tests first"; exit 2 }
    $lines = @(Get-Content $lastTestLog)
    $escaped = [regex]::Escape($ShowTest)
    $start = -1
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match "^\d+/\d+ Test:\s*$escaped\s*$") { $start = $i; break }
    }
    if ($start -lt 0) {
        Write-Host "test '$ShowTest' not found in $lastTestLog; available tests:"
        $lines | Select-String -Pattern '^\d+/\d+ Test:' | ForEach-Object { $_.Line }
        exit 1
    }
    for ($i = $start; $i -lt $lines.Count; $i++) {
        if ($i -gt $start -and $lines[$i] -match '^\d+/\d+ Test:') { break }
        Write-Host $lines[$i]
    }
    exit 0
}

$vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsInstallPath = & $vswherePath -latest -products '*' -property installationPath
$devShellModule = Join-Path $vsInstallPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
Import-Module $devShellModule
Enter-VsDevShell -VsInstallPath $vsInstallPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
Set-Location $repoRoot

if ($List) {
    $listArgs = @('--preset', $Preset, '-N')
    if ($Regex) { $listArgs += @('-R', $Regex) }
    ctest @listArgs 2>&1 | ForEach-Object { "$_" }
    exit $LASTEXITCODE
}

# --no-tests=error: a typo'd -Regex must fail loudly instead of passing silently.
$testArgs = @('--preset', $Preset, '--output-on-failure', '--no-tests=error')
if ($Regex) { $testArgs += @('-R', $Regex) }
if ($RerunFailed) { $testArgs += '--rerun-failed' }
if ($StopOnFailure) { $testArgs += '--stop-on-failure' }
ctest @testArgs 2>&1 | Tee-Object -FilePath $LogFile | Out-Null
$code = $LASTEXITCODE

Get-Content $LogFile | Select-String -Pattern 'Test #|tests passed|tests failed|\*\*\*Failed|\*\*\*Timeout|\*\*\*Exception|\*\*\*SegFault|\*\*\*Not Run|Errors while running|No tests were found|Unable to find executable' | ForEach-Object { $_.Line }
if ($code -ne 0) {
    Write-Host '=== failing test output (from ctest log): ==='
    # FAIL! lines carry the assertion details; post-context captures Qt Test's
    # Actual/Expected/Loc: lines that locate the failing source line.
    $hits = @(Get-Content $LogFile | Select-String -Pattern 'FAIL!|QFATAL|ASSERT|Critical:|Unable to find executable|No tests were found' -Context 0, 3)
    foreach ($h in ($hits | Select-Object -First 40)) {
        Write-Host $h.Line
        foreach ($c in $h.Context.PostContext) { Write-Host "    $c" }
    }
    if ($hits.Count -gt 40) {
        Write-Host "... and $($hits.Count - 40) more failing assertions; see $LogFile"
    }
    Get-Content $LogFile | Select-String -Pattern '^Totals:' | Select-Object -Last 5 | ForEach-Object { $_.Line }
    Write-Host "hint: full output of one test: test.ps1 -Preset $Preset -ShowTest <name>"
}
Write-Host "exit=$code log=$LogFile lasttest=$lastTestLog"
exit $code
