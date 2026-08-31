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

# Prints matching log lines with a few lines of post-context (MSVC note:/see-reference
# lines), capped by match count and total lines, and reports how many matches were
# truncated.
function Show-LogErrors {
    param(
        [string]$Log,
        [string]$Pattern,
        [int]$MaxMatches = 40,
        [int]$ContextLines = 3,
        [int]$MaxLines = 160
    )
    $hits = @(Get-Content $Log | Select-String -Pattern $Pattern -Context 0, $ContextLines)
    $shownMatches = 0
    $shownLines = 0
    foreach ($h in $hits) {
        if ($shownMatches -ge $MaxMatches -or $shownLines -ge $MaxLines) { break }
        Write-Host $h.Line
        $shownLines++
        foreach ($c in $h.Context.PostContext) {
            if ($shownLines -ge $MaxLines) { break }
            Write-Host "    $c"
            $shownLines++
        }
        $shownMatches++
    }
    $remaining = $hits.Count - $shownMatches
    if ($remaining -gt 0) {
        Write-Host "... and $remaining more matching errors; see $Log"
    }
}

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
        Show-LogErrors -Log $LogFile -Pattern 'CMake Error' -MaxMatches 5 -ContextLines 10
        Write-Host "full log: $LogFile"
        exit $LASTEXITCODE
    }
}

$buildArgs = @('--build', '--preset', $Preset, '--parallel')
if ($Target) { $buildArgs += @('--target', $Target) }
cmake @buildArgs 2>&1 | Tee-Object -FilePath $LogFile | Out-Null
$code = $LASTEXITCODE
if ($code -ne 0) {
    Write-Host '=== build failed, errors (first 40, context preserved): ==='
    Show-LogErrors -Log $LogFile -Pattern 'error C\d+|error LNK\d+|error MSB\d+|fatal error|FAILED:|CMake Error|ninja: build stopped'
} else {
    Get-Content $LogFile | Select-String -Pattern 'Built target' | ForEach-Object { $_.Line }
    $warnings = @(Get-Content $LogFile | Select-String -Pattern 'warning C\d+|warning LNK\d+')
    if ($warnings.Count -gt 0) {
        Write-Host "=== $($warnings.Count) compiler/linker warnings (first 20): ==="
        $warnings | Select-Object -First 20 | ForEach-Object { $_.Line }
    }
}
Write-Host "exit=$code log=$LogFile"
exit $code
