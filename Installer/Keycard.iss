; Keycard Windows installer (Inno Setup 6). Built by Scripts\BuildInstaller.bat, which first stages the
; release folder with Scripts\MakePortableZip.ps1 so the installer and the portable zip ship identical files.
; Defines passed by the build script: AppVersion (e.g. 0.5.0-alpha), NumericVersion (e.g. 0.5.0.0), SourceDir.
; Saves live in %LOCALAPPDATA%\Keycard and are never touched by install or uninstall.

#ifndef AppVersion
  #define AppVersion "0.0.0-dev"
#endif
#ifndef NumericVersion
  #define NumericVersion "0.0.0.0"
#endif
#ifndef SourceDir
  #error SourceDir must point at the staged release folder (Release\Portable\Keycard-<version>-Win64-Portable)
#endif

[Setup]
AppId={{3046A0CB-4FAD-4595-B1E1-1F6AFA5A892C}
AppName=Keycard
AppVersion={#AppVersion}
AppVerName=Keycard {#AppVersion}
AppPublisher=M A Almahlaki
AppPublisherURL=https://www.linkedin.com/in/mohammed-almahlaki001/
AppSupportURL=https://www.linkedin.com/in/mohammed-almahlaki001/
AppContact=m.awwadh.k@gmail.com
AppCopyright=Copyright (c) 2026 M A Almahlaki
VersionInfoVersion={#NumericVersion}
VersionInfoDescription=Keycard installer
DefaultDirName={autopf}\Keycard
DefaultGroupName=Keycard
DisableProgramGroupPage=yes
; Installs per user without administrator rights by default; an administrator can choose all users.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
LicenseFile=..\Docs\Release\Licence-and-credits.txt
InfoBeforeFile=..\Docs\Release\UNSIGNED-BUILD.txt
OutputDir=..\Release\Installer
OutputBaseFilename=Keycard-Setup-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\Keycard.exe
UninstallDisplayName=Keycard {#AppVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Keycard"; Filename: "{app}\Keycard.exe"; WorkingDir: "{app}"
Name: "{group}\Read me"; Filename: "{app}\README.txt"
Name: "{group}\Licences and credits"; Filename: "{app}\Licences"
Name: "{group}\{cm:UninstallProgram,Keycard}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Keycard"; Filename: "{app}\Keycard.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\Keycard.exe"; Description: "{cm:LaunchProgram,Keycard}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Anything the game wrote inside its own folder (it normally writes only to %LOCALAPPDATA%).
Type: filesandordirs; Name: "{app}\Keycard\Saved"

[Code]
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if (CurUninstallStep = usPostUninstall) and (not UninstallSilent) then
    MsgBox('Keycard has been removed. Your saves were kept in ' + ExpandConstant('{localappdata}') +
      '\Keycard. Delete that folder too if you want to remove them.', mbInformation, MB_OK);
end;
