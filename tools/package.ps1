# Builds everything and makes the installer dist\Idealism-Setup-<version>.exe (Inno Setup 6).
# Usage: pwsh -NoProfile -File tools/package.ps1 [-SkipBuild]
param([switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build.ps1') -Config Release
    if ($LASTEXITCODE) { throw 'build failed' }
}

$header = Get-Content (Join-Path $root 'src\Version.h') -Raw
if ($header -notmatch '#define ZY_VERSION_STRING "([0-9.]+)"') { throw 'version not found in src\Version.h' }
$version = $Matches[1]

$iscc = @(
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw 'Inno Setup 6 (ISCC.exe) not found' }

& $iscc /Qp "/DAppVersion=$version" (Join-Path $root 'installer\Idealism.iss')
if ($LASTEXITCODE) { throw 'ISCC failed' }
Write-Output "Built: dist\Idealism-Setup-$version.exe"
