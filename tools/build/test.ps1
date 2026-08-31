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

$testArgs = @('--preset', $Preset, '--output-on-failure')
if ($Regex) { $testArgs += @('-R', $Regex) }
ctest @testArgs 2>&1 | Tee-Object -FilePath $LogFile | Out-Null
$code = $LASTEXITCODE

Get-Content $LogFile | Select-String -Pattern 'Test #|tests passed|tests failed|\*\*\*Failed|\*\*\*Timeout|Errors while running' | ForEach-Object { $_.Line }
if ($code -ne 0) {
    Write-Host '=== failing test output (from ctest log): ==='
    Get-Content $LogFile | Select-String -Pattern 'FAIL!|ASSERT|QFATAL|QWARN|Critical|Error' | Select-Object -First 40 | ForEach-Object { $_.Line }
}
Write-Host "exit=$code log=$LogFile lasttest=$repoRoot/build/$Preset/Testing/Temporary/LastTest.log"
exit $code
