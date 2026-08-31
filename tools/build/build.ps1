param(
    [string]$Preset = 'local-test',
    [string]$Target = '',
    [switch]$Configure,
    [string]$LogFile = ''
)
# Build helper: enters the VS dev shell, optionally configures, builds, tees the full
# log to a file, and prints only the failure-relevant lines on error.
# Works from any checkout location (main tree, worktree, or the .agents skill copy):
# the repo root is found by walking up to the directory containing CMakeLists.txt.
$ErrorActionPreference = 'Continue'

$repoRoot = $PSScriptRoot
while ($repoRoot -and -not (Test-Path (Join-Path $repoRoot 'CMakeLists.txt'))) {
    $repoRoot = Split-Path $repoRoot -Parent
}
if (-not $repoRoot) { Write-Host 'repo root (CMakeLists.txt) not found above script location'; exit 2 }
if (-not $LogFile) { $LogFile = Join-Path $repoRoot "build/$Preset/build.log" }

# Shared dependency sources across worktrees: set FETCHCONTENT_BASE_DIR once per machine
# (e.g. F:\cache\fetchcontent) to avoid re-downloading FluentQt/QWindowKit per checkout.
$configureArgs = @('--preset', $Preset)
if ($env:FETCHCONTENT_BASE_DIR) {
    $configureArgs += "-DFETCHCONTENT_BASE_DIR=$env:FETCHCONTENT_BASE_DIR"
}

$vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsInstallPath = & $vswherePath -latest -products '*' -property installationPath
$devShellModule = Join-Path $vsInstallPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
Import-Module $devShellModule
Enter-VsDevShell -VsInstallPath $vsInstallPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
Set-Location $repoRoot

if ($Configure) {
    cmake @configureArgs 2>&1 | Tee-Object -FilePath $LogFile | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Host '=== configure failed, errors: ==='
        Get-Content $LogFile | Select-String -Pattern 'CMake Error|CMake Warning \(dev\)|error' | Select-Object -First 40 | ForEach-Object { $_.Line }
        Write-Host "full log: $LogFile"
        exit $LASTEXITCODE
    }
}

$buildArgs = @('--build', '--preset', $Preset, '--parallel')
if ($Target) { $buildArgs += @('--target', $Target) }
cmake @buildArgs 2>&1 | Tee-Object -FilePath $LogFile | Out-Null
$code = $LASTEXITCODE
if ($code -ne 0) {
    Write-Host '=== build failed, errors (first 40): ==='
    Get-Content $LogFile | Select-String -Pattern 'error C\d+|error LNK\d+|fatal error|FAILED:|CMake Error' | Select-Object -First 40 | ForEach-Object { $_.Line }
} else {
    Get-Content $LogFile | Select-String -Pattern 'Built target|warning C\d+' | Select-Object -First 20 | ForEach-Object { $_.Line }
}
Write-Host "exit=$code log=$LogFile"
exit $code
