# Builds x64 and x86 (Win32) Release DLLs and runs the engine tests.
# Usage: pwsh -NoProfile -File tools/build.ps1 [-Config Release|Debug]
# -Generator overrides the CMake generator (default: the newest Visual Studio found by vswhere,
#  e.g. 'Visual Studio 18 2026' locally or 'Visual Studio 17 2022' on a GitHub runner).
param([string]$Config = 'Release', [string]$Generator = '')
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsArgs = @('-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64')
$vs = & $vswhere @vsArgs -property installationPath
if (-not $vs) { throw 'Visual Studio with the C++ x86/x64 tools not found' }
if (-not $Generator) {
    $major = [int](((& $vswhere @vsArgs -property installationVersion) -split '\.')[0])
    $Generator = switch ($major) {
        18 { 'Visual Studio 18 2026' }
        17 { 'Visual Studio 17 2022' }
        default { throw "unsupported Visual Studio version $major" }
    }
}
# Visual Studio's own CMake always knows its generator; a CMake on PATH may be older.
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path $cmake)) { $cmake = (Get-Command cmake -ErrorAction Stop).Source }
Write-Output "Generator: $Generator ($cmake)"

$dataDir = Join-Path $root 'data'
if (-not (Test-Path (Join-Path $dataDir 'packs.txt'))) { & (Join-Path $PSScriptRoot 'build_data.ps1') }
$packFiles = @('packs.txt') + (Get-Content (Join-Path $dataDir 'packs.txt') -Encoding utf8 |
    Where-Object { $_ -and -not $_.StartsWith('#') } | ForEach-Object { ($_ -split "`t")[1] })

foreach ($arch in 'x64', 'Win32') {
    $build = Join-Path $root "build\$arch"
    & $cmake -S $root -B $build -G $Generator -A $arch
    if ($LASTEXITCODE) { throw "configure $arch failed" }
    & $cmake --build $build --config $Config --parallel
    if ($LASTEXITCODE) { throw "build $arch failed" }
    & (Join-Path $build "$Config\engine_test.exe") $dataDir
    if ($LASTEXITCODE) { throw "engine tests failed ($arch)" }
    # The smoke test (and the settings app) look for the packs like the installed DLL does: ..\data
    New-Item -ItemType Directory -Force (Join-Path $build 'data') | Out-Null
    foreach ($f in $packFiles) { Copy-Item (Join-Path $dataDir $f) (Join-Path $build "data\$f") -Force }
    & (Join-Path $build "$Config\tip_smoke.exe")
    if ($LASTEXITCODE) { throw "TIP smoke test failed ($arch)" }
    & (Join-Path $build "$Config\pad_test.exe")  # handwriting; skips if no recognizer is installed
    if ($LASTEXITCODE) { throw "pad test failed ($arch)" }
}
Write-Output "Built: build\x64\$Config\IdealismTip.dll, build\Win32\$Config\IdealismTip.dll, build\x64\$Config\IdealismSettings.exe, build\x64\$Config\IdealismPad.exe"
