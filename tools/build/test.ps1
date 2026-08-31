param(
    [string]$Preset = 'local-test',
    [string]$Regex = '',
    [string]$LogFile = ''
)
# Test helper: runs ctest with failure output, tees the full log, prints a compact summary.
# Per-test verbose output lives in the log and in build/<preset>/Testing/Temporary/LastTest.log.
# Repo root is found by walking up to CMakeLists.txt, so this works from any checkout.
$ErrorActionPreference = 'Continue'

$repoRoot = $PSScriptRoot
while ($repoRoot -and -not (Test-Path (Join-Path $repoRoot 'CMakeLists.txt'))) {
    $repoRoot = Split-Path $repoRoot -Parent
}
if (-not $repoRoot) { Write-Host 'repo root (CMakeLists.txt) not found above script location'; exit 2 }
if (-not $LogFile) { $LogFile = Join-Path $repoRoot "build/$Preset/ctest.log" }

$vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsInstallPath = & $vswherePath -latest -products '*' -property installationPath
$devShellModule = Join-Path $vsInstallPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
Import-Module $devShellModule
Enter-VsDevShell -VsInstallPath $vsInstallPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
Set-Location $repoRoot

# --no-tests=error: a typo'd -Regex must fail loudly instead of passing silently.
$testArgs = @('--preset', $Preset, '--output-on-failure', '--no-tests=error')
if ($Regex) { $testArgs += @('-R', $Regex) }
ctest @testArgs 2>&1 | Tee-Object -FilePath $LogFile | Out-Null
$code = $LASTEXITCODE

Get-Content $LogFile | Select-String -Pattern 'Test #|tests passed|tests failed|\*\*\*Failed|\*\*\*Timeout|\*\*\*Exception|\*\*\*SegFault|Errors while running|No tests were found' | ForEach-Object { $_.Line }
if ($code -ne 0) {
    Write-Host '=== failing test output (from ctest log): ==='
    # FAIL! lines carry the assertion details; post-context captures Qt Test's
    # Actual/Expected/Loc: lines that locate the failing source line.
    $hits = @(Get-Content $LogFile | Select-String -Pattern 'FAIL!|QFATAL|ASSERT|Critical:|No tests were found' -Context 0, 3)
    foreach ($h in ($hits | Select-Object -First 40)) {
        Write-Host $h.Line
        foreach ($c in $h.Context.PostContext) { Write-Host "    $c" }
    }
    if ($hits.Count -gt 40) {
        Write-Host "... and $($hits.Count - 40) more failing assertions; see $LogFile"
    }
    Get-Content $LogFile | Select-String -Pattern '^Totals:' | Select-Object -Last 5 | ForEach-Object { $_.Line }
}
Write-Host "exit=$code log=$LogFile lasttest=$repoRoot/build/$Preset/Testing/Temporary/LastTest.log"
exit $code
