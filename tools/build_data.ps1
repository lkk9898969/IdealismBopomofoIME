# Builds the dictionary from McBopomofo's sources with its own Python pipeline (data/data.txt),
# then splits it into phrase packs (data/packs.txt + base/phrases/symbols.txt).
# Needs Python 3.9+ (standard library only).
# Usage: pwsh -NoProfile -File tools/build_data.ps1
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')
$src = Join-Path $root 'third_party\McBopomofo\Source\Data'
$outDir = Join-Path $root 'data'

$py = @('py', '-3')
if (-not (Get-Command py -ErrorAction SilentlyContinue)) { $py = @('python') }
$env:PYTHONUTF8 = '1'

Push-Location $src
try {
    function Run([string[]]$cmdArgs) {
        & $py[0] @($py | Select-Object -Skip 1) @cmdArgs
        if ($LASTEXITCODE) { throw "failed: $cmdArgs" }
    }
    Run @('-m', 'curation.builders.frequency_builder')
    Run @('-m', 'curation.compilers.main_compiler',
        '--heterophony1', 'heterophony1.list', '--heterophony2', 'heterophony2.list',
        '--heterophony3', 'heterophony3.list', '--phrase_freq', 'PhraseFreq.txt',
        '--bpmf_mappings', 'BPMFMappings.txt', '--bpmf_base', 'BPMFBase.txt',
        '--punctuations', 'BPMFPunctuations.txt', '--symbols', 'Symbols.txt',
        '--macros', 'Macros.txt', '--output', 'data-raw.txt')
    Run @('-m', 'curation.compilers.postprocess', '--input', 'data-raw.txt',
        '--directive', 'Postprocess.txt', '--output', 'data.txt')
} finally {
    Pop-Location
}

New-Item -ItemType Directory -Force $outDir | Out-Null
# Python on Windows writes CRLF; McBopomofo's parser requires LF (it checks the "# format" header
# byte-for-byte and binary-searches rows by '\n').
$text = [System.IO.File]::ReadAllText((Join-Path $src 'data.txt'), [System.Text.Encoding]::UTF8)
$text = $text.Replace("`r`n", "`n")
[System.IO.File]::WriteAllText((Join-Path $outDir 'data.txt'), $text, (New-Object System.Text.UTF8Encoding $false))
Write-Output "Wrote $(Join-Path $outDir 'data.txt')"

& $py[0] @($py | Select-Object -Skip 1) (Join-Path $PSScriptRoot 'split_packs.py') (Join-Path $outDir 'data.txt') `
    (Join-Path $src 'Symbols.txt') $outDir
if ($LASTEXITCODE) { throw 'split_packs failed' }
