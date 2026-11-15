; LinkVault installer (Inno Setup 6)
; Built by build_setup.bat  (or open this file in Inno Setup and press Compile after running build.bat)

#define AppName    "LinkVault"
#define AppVersion "4.1.0"
#define AppExe     "LinkVault.exe"
#ifndef DistDir
  #define DistDir  "..\dist"
#endif

[Setup]
AppId={{8F3C2A71-5B4D-4E2A-9C61-7A1D3E5B9F20}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=maniXpc
VersionInfoVersion={#AppVersion}.0
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
; installs per-user without admin; the user can pick "all users" on the first page
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
OutputDir=..\Setup
OutputBaseFilename=LinkVault-Setup-{#AppVersion}
SetupIconFile=..\app.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
MinVersion=10.0
; closes a running LinkVault before updating its files
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "autostart"; Description: "Run when I sign in"; GroupDescription: "Other:"; Flags: unchecked

[Files]
Source: "{#DistDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "LinkVault"; ValueData: """{app}\{#AppExe}"" --background"; Flags: uninsdeletevalue; Tasks: autostart

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[Code]
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
  begin
    { the app can add itself to startup from its settings page; always clean that up }
    RegDeleteValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'LinkVault');
    if (not UninstallSilent) and
       (MsgBox('Also delete your saved links, thumbnails and settings?',
               mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES) then
    begin
      DelTree(ExpandConstant('{userappdata}\LinkVault'), True, True, True);
      RegDeleteKeyIncludingSubkeys(HKCU, 'Software\LinkVault');
    end;
  end;
end;
