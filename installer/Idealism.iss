; 微新注音 installer (Inno Setup 6). Built by tools/package.ps1, which passes /DAppVersion=x.y.z.
;
; Every build installs into its own folder, {app}\<build time>: programs that still have the previous
; DLL loaded (3ds Max, the taskbar...) keep using it until they restart, so updating never needs
; them closed. Older folders are removed after installing, or at the next restart if still in use.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#define Stamp GetDateTimeString('yyyymmdd-hhnnss', '', '')
#define Root ".."
#define TipProfile "0404:{1C2FCFCE-B688-41D6-9CA8-FFC3F808F0B8}{84EF98CE-13EF-4565-8250-EA8A42727F5D}"

[Setup]
AppId={{CC4E7BC5-291F-441E-9ABC-31CED387C508}
AppName=微新注音
AppVersion={#AppVersion}
AppVerName=微新注音 {#AppVersion}
AppPublisher=微新注音
DefaultDirName={autopf}\IdealismIME
; 1.0.0 and the development builds lived in {autopf}\ZhuyinIME; never reuse that folder.
UsePreviousAppDir=no
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableWelcomePage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#Root}\dist
OutputBaseFilename=IdealismBopomofo-Setup-{#AppVersion}
SetupIconFile={#Root}\src\tip\zhuyin.ico
UninstallDisplayName=微新注音
UninstallDisplayIcon={app}\{#Stamp}\IdealismSettings.exe
VersionInfoVersion={#AppVersion}
VersionInfoProductName=微新注音
VersionInfoDescription=微新注音安裝程式
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; Never ask to close the programs using the IME (3ds Max!): the new version goes in a new folder.
CloseApplications=no
RestartApplications=no

[Languages]
Name: "cht"; MessagesFile: "Languages\ChineseTraditional.isl"

[Messages]
cht.ReadyLabel1=即將安裝微新注音 {#AppVersion}。
cht.ReadyLabel2a=按「安裝」繼續。已經開著的程式要重新開啟才會使用新版。

[Files]
Source: "{#Root}\build\x64\Release\IdealismTip.dll"; DestDir: "{app}\{#Stamp}\x64"; Flags: ignoreversion regserver 64bit uninsrestartdelete
Source: "{#Root}\build\Win32\Release\IdealismTip.dll"; DestDir: "{app}\{#Stamp}\x86"; Flags: ignoreversion regserver 32bit uninsrestartdelete
Source: "{#Root}\build\x64\Release\IdealismSettings.exe"; DestDir: "{app}\{#Stamp}"; Flags: ignoreversion uninsrestartdelete
Source: "{#Root}\build\x64\Release\IdealismPad.exe"; DestDir: "{app}\{#Stamp}"; Flags: ignoreversion uninsrestartdelete
Source: "{#Root}\data\packs.txt"; DestDir: "{app}\{#Stamp}\data"; Flags: ignoreversion uninsrestartdelete
Source: "{#Root}\data\base.txt"; DestDir: "{app}\{#Stamp}\data"; Flags: ignoreversion uninsrestartdelete
Source: "{#Root}\data\phrases.txt"; DestDir: "{app}\{#Stamp}\data"; Flags: ignoreversion uninsrestartdelete
Source: "{#Root}\data\symbols.txt"; DestDir: "{app}\{#Stamp}\data"; Flags: ignoreversion uninsrestartdelete
Source: "{#Root}\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "{#Root}\THIRD-PARTY-NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{commonprograms}\微新注音設定"; Filename: "{app}\{#Stamp}\IdealismSettings.exe"; Comment: "詞庫包、自訂詞、排除詞、學習記錄"
Name: "{commonprograms}\微新注音整合器"; Filename: "{app}\{#Stamp}\IdealismPad.exe"; Comment: "手寫辨識（顯示讀音）、符號表"

[InstallDelete]
; Shortcuts of the development builds (tools/install.ps1)
Type: files; Name: "{commonprograms}\注音輸入法設定.lnk"
Type: files; Name: "{commonprograms}\注音輸入法整合器.lnk"

[Run]
; Store apps and other AppContainer processes need explicit read/execute on the IME's files.
Filename: "{sys}\icacls.exe"; Parameters: """{app}\{#Stamp}"" /grant *S-1-15-2-1:(OI)(CI)(RX) /T /Q"; Flags: runhidden

[Code]
function InstallLayoutOrTip(psz: String; dwFlags: DWORD): BOOL;
  external 'InstallLayoutOrTip@input.dll stdcall delayload';

// Deletes a folder; whatever is still in use (a DLL loaded by a running program) is deleted at the
// next restart instead.
procedure RemoveTree(const Dir: String);
var
  FindRec: TFindRec;
  Path: String;
begin
  if FindFirst(Dir + '\*', FindRec) then
  begin
    try
      repeat
        if (FindRec.Name <> '.') and (FindRec.Name <> '..') then
        begin
          Path := Dir + '\' + FindRec.Name;
          if FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY <> 0 then
            RemoveTree(Path)
          else if not DeleteFile(Path) then
            RestartReplace(Path, '');
        end;
      until not FindNext(FindRec);
    finally
      FindClose(FindRec);
    end;
  end;
  if not RemoveDir(Dir) and DirExists(Dir) then
    RestartReplace(Dir, '');
end;

// Folders of older versions (and of the development install script).
procedure RemoveOldVersions;
var
  FindRec: TFindRec;
  Root: String;
begin
  Root := ExpandConstant('{app}');
  if FindFirst(Root + '\*', FindRec) then
  begin
    try
      repeat
        if (FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY <> 0) and (FindRec.Name <> '.') and
           (FindRec.Name <> '..') and (FindRec.Name <> '{#Stamp}') then
          RemoveTree(Root + '\' + FindRec.Name);
      until not FindNext(FindRec);
    finally
      FindClose(FindRec);
    end;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    // Add 微新注音 to the user's input methods (Win+Space).
    try
      InstallLayoutOrTip('{#TipProfile}', 0);
    except
    end;
    RemoveOldVersions;
    // Before the rename to Idealism (the new DLLs are registered by now, so nothing loads these again).
    if DirExists(ExpandConstant('{autopf}\ZhuyinIME')) then
      RemoveTree(ExpandConstant('{autopf}\ZhuyinIME'));
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    try
      InstallLayoutOrTip('{#TipProfile}', 1);  // ILOT_UNINSTALL
    except
    end;
  end;
  if CurUninstallStep = usPostUninstall then
  begin
    RemoveTree(ExpandConstant('{app}'));
    if DirExists(ExpandConstant('{userappdata}\IdealismIME')) and
       (SuppressibleMsgBox('要同時刪除你的學習記錄、自訂詞和設定嗎？' + #13#10 + #13#10 +
         '選「否」會保留在 ' + ExpandConstant('{userappdata}\IdealismIME') + '，重新安裝後會繼續使用。',
         mbConfirmation, MB_YESNO or MB_DEFBUTTON2, IDNO) = IDYES) then
      DelTree(ExpandConstant('{userappdata}\IdealismIME'), True, True, True);
  end;
end;
