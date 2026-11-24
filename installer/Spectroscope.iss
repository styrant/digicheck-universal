; Inno Setup script for Spectroscope (built by GitHub Actions: iscc /DAppVersion=x.y.z installer\Spectroscope.iss)
; Installs for all users by default (Windows asks for administrator rights), so the VST3 plugin goes to
; C:\Program Files\Common Files\VST3, the folder every DAW scans. "Install for me only" is still offered
; (no administrator rights); the plugin then goes to the per-user VST3 folder, which not every DAW scans.

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
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog commandline
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

[Code]
// A per-user install can't write to Common Files\VST3: say where the plugin goes instead
function NextButtonClick (CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = wpSelectTasks) and WizardIsTaskSelected ('vst3') and (not IsAdminInstallMode) then
    Result := MsgBox ('This is an install for you only, so the VST3 plugin goes to your own VST3 folder:' + #13#10 +
                      ExpandConstant ('{localappdata}') + '\Programs\Common\VST3' + #13#10#13#10 +
                      'Some DAWs (for example older Ableton Live or FL Studio versions) only look in' + #13#10 +
                      'C:\Program Files\Common Files\VST3. To put it there, cancel and run the installer again,' + #13#10 +
                      'choosing "Install for all users".' + #13#10#13#10 + 'Continue anyway?',
                      mbConfirmation, MB_YESNO) = IDYES;
end;

[Icons]
Name: "{autoprograms}\Spectroscope"; Filename: "{app}\Spectroscope.exe"
Name: "{autoprograms}\Spectroscope Target Builder"; Filename: "{app}\TargetBuilder.exe"; Comment: "Drag in songs and save them as a tonal balance target"
Name: "{autodesktop}\Spectroscope"; Filename: "{app}\Spectroscope.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Spectroscope.exe"; Description: "{cm:LaunchProgram,Spectroscope}"; Flags: nowait postinstall skipifsilent
