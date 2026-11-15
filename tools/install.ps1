#Requires -RunAsAdministrator
# Installs the built DLLs into a fresh, versioned folder under Program Files and registers them
# (x64 + x86). A versioned folder means re-installing never fights with a DLL that a running
# app (e.g. 3ds Max) still has loaded: old processes keep the old copy until restarted.
# Usage (elevated): pwsh -NoProfile -File tools/install.ps1 [-Config Release|Debug]
param([string]$Config = 'Release')
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$dest = Join-Path $env:ProgramFiles "IdealismIME\$stamp"

$targets = @(
    @{ Src = "build\x64\$Config\IdealismTip.dll";   Dir = 'x64'; RegSvr = "$env:WINDIR\System32\regsvr32.exe" },
    @{ Src = "build\Win32\$Config\IdealismTip.dll"; Dir = 'x86'; RegSvr = "$env:WINDIR\SysWOW64\regsvr32.exe" }
)

foreach ($t in $targets) {
    $src = Join-Path $root $t.Src
    if (-not (Test-Path $src)) { throw "Missing $src - run tools/build.ps1 first" }
}
$dataSrc = Join-Path $root 'data'
$manifest = Join-Path $dataSrc 'packs.txt'
if (-not (Test-Path $manifest)) { throw "Missing $manifest - run tools/build.ps1 first" }
$settingsSrc = Join-Path $root "build\x64\$Config\IdealismSettings.exe"
$padSrc = Join-Path $root "build\x64\$Config\IdealismPad.exe"
foreach ($f in $settingsSrc, $padSrc) { if (-not (Test-Path $f)) { throw "Missing $f - run tools/build.ps1 first" } }

# Phrase packs, shared by both architectures: <dest>\data\packs.txt + one file per pack
# (found via ..\data from each DLL, and data\ from the settings app).
$dataDir = Join-Path $dest 'data'
New-Item -ItemType Directory -Force $dataDir | Out-Null
$packFiles = @('packs.txt') + (Get-Content $manifest -Encoding utf8 |
    Where-Object { $_ -and -not $_.StartsWith('#') } | ForEach-Object { ($_ -split "`t")[1] })
foreach ($f in $packFiles) { Copy-Item (Join-Path $dataSrc $f) (Join-Path $dataDir $f) }
icacls $dataDir /grant '*S-1-15-2-1:(OI)(CI)(RX)' /T | Out-Null

# Settings app (right-click the 中/英 tray button, or the Start menu).
$settingsExe = Join-Path $dest 'IdealismSettings.exe'
Copy-Item $settingsSrc $settingsExe
$shortcut = Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs\微新注音設定.lnk'
$shell = New-Object -ComObject WScript.Shell
$link = $shell.CreateShortcut($shortcut)
$link.TargetPath = $settingsExe
$link.Description = '微新注音設定：詞庫包、自訂詞、排除詞、學習記錄'
$link.Save()

# IME pad: handwriting and symbols (right-click the 中/英 tray button, or the Start menu).
$padExe = Join-Path $dest 'IdealismPad.exe'
Copy-Item $padSrc $padExe
$link = $shell.CreateShortcut((Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs\微新注音整合器.lnk'))
$link.TargetPath = $padExe
$link.Description = '微新注音整合器：手寫辨識（顯示讀音）、符號表'
$link.Save()

foreach ($t in $targets) {
    $dir = Join-Path $dest $t.Dir
    New-Item -ItemType Directory -Force $dir | Out-Null
    $dll = Join-Path $dir 'IdealismTip.dll'
    Copy-Item (Join-Path $root $t.Src) $dll
    # Store apps / AppContainer processes need explicit read+execute (ALL APPLICATION PACKAGES).
    icacls $dll /grant '*S-1-15-2-1:(RX)' | Out-Null
    $p = Start-Process $t.RegSvr -ArgumentList '/s', "`"$dll`"" -Wait -PassThru
    if ($p.ExitCode -ne 0) { throw "regsvr32 failed for $dll (exit $($p.ExitCode))" }
    Write-Output "Registered $dll"
}

Write-Output ''
Write-Output 'Done. If "微新注音" does not show in the input switcher (Win+Space),'
Write-Output 'add it under Settings > Time & language > Language > 中文(台灣) > Options > Add a keyboard.'
