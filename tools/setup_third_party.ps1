# Fetches McBopomofo (git submodule, MIT) after a fresh clone and applies our MSVC fixes.
# Only Source/Engine and Source/Data are needed, so the checkout is sparse (and blobless).
# Usage: pwsh -NoProfile -File tools/setup_third_party.ps1
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$name = 'third_party/McBopomofo'
$sub = Join-Path $root 'third_party\McBopomofo'
$patch = Join-Path $root 'patches\mcbopomofo-msvc.patch'

function Invoke-Git([string[]]$cmdArgs) {
    & git @cmdArgs
    if ($LASTEXITCODE) { throw "git $cmdArgs failed" }
}

# True only when $dir is the top of its own repository: "git -C <dir>" on a folder that is not
# would silently act on this repository instead.
function IsOwnRepo([string]$dir) {
    if (-not (Test-Path (Join-Path $dir '.git'))) { return $false }
    $top = & git -C $dir rev-parse --show-toplevel 2>$null
    return $LASTEXITCODE -eq 0 -and (Resolve-Path $top).Path -eq (Resolve-Path $dir).Path
}

Push-Location $root
try {
    Invoke-Git @('submodule', 'init', $name)
    if (-not (IsOwnRepo $sub)) {
        $url = (& git config -f .gitmodules "submodule.$name.url").Trim()
        $commit = ((& git ls-tree HEAD $name) -split '\s+')[2]
        if (-not $url -or -not $commit) { throw "submodule $name not found in .gitmodules / HEAD" }
        if (Test-Path $sub) {
            if (Get-ChildItem $sub -Force) { throw "$sub is not a git checkout but is not empty; move it away first" }
            Remove-Item $sub -Force  # the empty placeholder a clone leaves
        }
        # LF exactly as upstream (core.autocrlf is often true on Windows): the patch applies byte for byte.
        Invoke-Git @('clone', '--quiet', '--filter=blob:none', '--no-checkout', '-c', 'core.autocrlf=false', $url, $sub)
        Invoke-Git @('-C', $sub, 'sparse-checkout', 'set', 'Source/Engine', 'Source/Data')
        Invoke-Git @('-C', $sub, 'checkout', '--quiet', $commit)
        Write-Output "Fetched $name at $commit"
    }
    # Apply the patch unless it is already there.
    & git -C $sub apply --check --reverse $patch 2>$null
    if ($LASTEXITCODE -ne 0) {
        Invoke-Git @('-C', $sub, 'apply', $patch)
        Write-Output 'Applied patches/mcbopomofo-msvc.patch'
    } else {
        Write-Output 'McBopomofo is already patched'
    }
} finally {
    Pop-Location
}
