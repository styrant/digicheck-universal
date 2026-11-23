; Inno Setup script for Spectroscope (built by GitHub Actions: iscc /DAppVersion=x.y.z installer\Spectroscope.iss)
; Installs per user (no administrator rights needed) and adds Start menu / optional desktop shortcuts.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif

[Setup]
AppId={{8F3C2A51-6B0E-4D2B-9C1A-5E7D3F2B9A10}
AppName=Spectroscope
AppVersion={#AppVersion}
AppVerName=Spectroscope {#AppVersion}
AppPublisher=DigiCheck Universal
DefaultDirName={autopf}\Spectroscope
DefaultGroupName=Spectroscope
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename=SpectroscopeSetup
SetupIconFile=..\Resources\icon.ico
UninstallDisplayIcon={app}\Spectroscope.exe
UninstallDisplayName=Spectroscope
WizardStyle=modern
Compression=lzma2
SolidCompression=yes
CloseApplications=yes

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "vst3"; Description: "Install the VST3 plugin (DigiCheck Universal.vst3, for your DAW)"; GroupDescription: "Plugin:"

[Files]
Source: "..\build\Spectroscope_artefacts\Release\Spectroscope.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\TargetBuilder_artefacts\Release\TargetBuilder.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion
; the VST3 bundle: Common Files\VST3 for an administrator install, else the per-user VST3 folder
; (%LOCALAPPDATA%\Programs\Common\VST3, the VST3 specification's user location)
Source: "..\build\DigiCheckUniversal_artefacts\Release\VST3\DigiCheck Universal.vst3\*"; DestDir: "{autocf}\VST3\DigiCheck Universal.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Tasks: vst3

[Icons]
Name: "{autoprograms}\Spectroscope"; Filename: "{app}\Spectroscope.exe"
Name: "{autoprograms}\Spectroscope Target Builder"; Filename: "{app}\TargetBuilder.exe"; Comment: "Drag in songs and save them as a tonal balance target"
Name: "{autodesktop}\Spectroscope"; Filename: "{app}\Spectroscope.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Spectroscope.exe"; Description: "{cm:LaunchProgram,Spectroscope}"; Flags: nowait postinstall skipifsilent
