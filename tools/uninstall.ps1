#Requires -RunAsAdministrator
# Unregisters the IME (x64 + x86) and removes installed copies that are no longer in use.
# Usage (elevated): pwsh -NoProfile -File tools/uninstall.ps1
$ErrorActionPreference = 'Continue'
$clsid = '{1C2FCFCE-B688-41D6-9CA8-FFC3F808F0B8}'

$views = @(
    @{ Key = "Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\$clsid\InprocServer32";             RegSvr = "$env:WINDIR\System32\regsvr32.exe" },
    @{ Key = "Registry::HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Classes\CLSID\$clsid\InprocServer32"; RegSvr = "$env:WINDIR\SysWOW64\regsvr32.exe" }
)
foreach ($v in $views) {
    if (Test-Path $v.Key) {
        $dll = (Get-ItemProperty $v.Key).'(default)'
        if ($dll -and (Test-Path $dll)) {
            Start-Process $v.RegSvr -ArgumentList '/u', '/s', "`"$dll`"" -Wait | Out-Null
            Write-Output "Unregistered $dll"
        }
    }
}

foreach ($name in '微新注音設定.lnk', '微新注音整合器.lnk', '注音輸入法設定.lnk', '注音輸入法整合器.lnk') {
    $shortcut = Join-Path $env:ProgramData "Microsoft\Windows\Start Menu\Programs\$name"
    if (Test-Path $shortcut) { Remove-Item $shortcut -Force; Write-Output "Removed $shortcut" }
}

foreach ($base in (Join-Path $env:ProgramFiles 'IdealismIME'), (Join-Path $env:ProgramFiles 'ZhuyinIME')) {
    if (Test-Path $base) {
        Get-ChildItem $base -Directory | ForEach-Object {
            try { Remove-Item $_.FullName -Recurse -Force -ErrorAction Stop; Write-Output "Removed $($_.FullName)" }
            catch { Write-Output "Still in use (remove after restarting apps / signing out): $($_.FullName)" }
        }
    }
}
