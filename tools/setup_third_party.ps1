# Fetches McBopomofo (git submodule, MIT) after a fresh clone and applies our MSVC fixes.
# Only Source/Engine and Source/Data are needed, so the checkout is sparse.
# Usage: pwsh -NoProfile -File tools/setup_third_party.ps1
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')
$sub = Join-Path $root 'third_party\McBopomofo'
$patch = Join-Path $root 'patches\mcbopomofo-msvc.patch'

Push-Location $root
try {
    git submodule init third_party/McBopomofo
    if (-not (Test-Path (Join-Path $sub '.git'))) {
        git -c submodule.third_party/McBopomofo.update=checkout submodule update --filter=blob:none --no-checkout third_party/McBopomofo
        git -C $sub sparse-checkout set Source/Engine Source/Data
        git -C $sub checkout
    }
    # Apply the patch unless it is already there.
    git -C $sub apply --check --reverse $patch 2>$null
    if ($LASTEXITCODE -ne 0) {
        git -C $sub apply $patch
        if ($LASTEXITCODE) { throw 'applying patches/mcbopomofo-msvc.patch failed' }
        Write-Output 'Applied patches/mcbopomofo-msvc.patch'
    } else {
        Write-Output 'McBopomofo is already patched'
    }
} finally {
    Pop-Location
}
