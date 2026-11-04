# Builds the playground (configures on the first run) and starts it.
#   .\run.ps1                 the window, scale 2
#   .\run.ps1 -Scale 3        the window, bigger
#   .\run.ps1 -Headless       the scenario without a window: frames in out\*.bmp, statistics on the console
#   .\run.ps1 -Clean          delete build\ first (a new compiler, changed CMake options)
#   .\run.ps1 -NoWindow       build without SDL2 (implies -Headless)
param(
    [int]$Scale = 2,
    [switch]$Headless,
    [switch]$Clean,
    [switch]$NoWindow
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$build = Join-Path $root 'build'

# Put gcc, cmake and ninja on PATH when the shell does not have them yet.
function Add-ToolDir($name, [string[]]$globs) {
    if (Get-Command $name -ErrorAction SilentlyContinue) { return }
    foreach ($g in $globs) {
        $hit = Get-ChildItem $g -Filter "$name.exe" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($hit) { $env:Path += ";$($hit.DirectoryName)"; return }
    }
    throw "$name not found: install it or add it to PATH"
}
Add-ToolDir 'gcc'   @("$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs*")
Add-ToolDir 'cmake' @('C:\Espressif\tools\cmake', 'C:\Program Files\CMake\bin')
Add-ToolDir 'ninja' @('C:\Espressif\tools\ninja')

if ($NoWindow) { $Headless = $true }
if ($Clean -and (Test-Path $build)) { Remove-Item $build -Recurse -Force }

if (-not (Test-Path (Join-Path $build 'build.ninja')) -or $Clean -or $NoWindow) {
    $window = if ($NoWindow) { 'OFF' } else { 'ON' }
    cmake -S $root -B $build -G Ninja -DCMAKE_C_COMPILER=gcc "-DPLAYGROUND_WINDOW=$window"
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
}

cmake --build $build --target playground
if ($LASTEXITCODE) { exit $LASTEXITCODE }

$exe = Join-Path $build 'playground.exe'
if ($Headless) {
    $out = Join-Path $root 'out'
    New-Item -ItemType Directory -Force $out | Out-Null
    & $exe $out
} else {
    & $exe --window $Scale
}
exit $LASTEXITCODE
