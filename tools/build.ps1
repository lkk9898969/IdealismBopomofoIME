# Builds x64 and x86 (Win32) Release DLLs and runs the engine tests.
# Usage: pwsh -NoProfile -File tools/build.ps1 [-Config Release|Debug]
param([string]$Config = 'Release')
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vs = & $vswhere -latest -products * -property installationPath
    $cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}

$dataDir = Join-Path $root 'data'
if (-not (Test-Path (Join-Path $dataDir 'packs.txt'))) { & (Join-Path $PSScriptRoot 'build_data.ps1') }
$packFiles = @('packs.txt') + (Get-Content (Join-Path $dataDir 'packs.txt') -Encoding utf8 |
    Where-Object { $_ -and -not $_.StartsWith('#') } | ForEach-Object { ($_ -split "`t")[1] })

foreach ($arch in 'x64', 'Win32') {
    $build = Join-Path $root "build\$arch"
    & $cmake -S $root -B $build -G 'Visual Studio 18 2026' -A $arch
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
