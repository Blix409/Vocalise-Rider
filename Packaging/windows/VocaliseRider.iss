; Inno Setup script: builds Vocalise-Rider-<version>-Windows-Setup.exe
; Build the plugin first (see README.txt), then run make_installer.bat in this folder.
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif

[Setup]
AppId={{0F7A7449-D126-4C28-819C-843B5CA66C5F}
AppName=Vocalise-Rider
AppVersion={#AppVersion}
AppPublisher=Daemonj Multimedia
DefaultDirName={autopf}\Daemonj Multimedia\Vocalise-Rider
DefaultGroupName=Daemonj Multimedia
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
OutputDir=..\..
OutputBaseFilename=Vocalise-Rider-{#AppVersion}-Windows-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupIconFile=icon.ico
UninstallDisplayIcon={app}\Documents\icon.ico
InfoBeforeFile=wizard-info.txt

[Files]
; the VST3 plugin (a folder bundle) goes to the standard VST3 folder
Source: "..\..\build\VocalRider_artefacts\Release\VST3\Vocalise-Rider.vst3\*"; DestDir: "{commoncf64}\VST3\Vocalise-Rider.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
; guides are always installed
Source: "docs\*"; DestDir: "{app}\Documents"; Flags: ignoreversion
Source: "icon.ico"; DestDir: "{app}\Documents"; Flags: ignoreversion

[Icons]
Name: "{group}\Vocalise-Rider guides"; Filename: "{app}\Documents"

[Run]
Filename: "{app}\Documents\HOW-TO-USE.txt"; Description: "Open the how-to guide"; Flags: postinstall shellexec skipifsilent unchecked
