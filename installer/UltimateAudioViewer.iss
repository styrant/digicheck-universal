; Inno Setup script for Ultimate Audio Viewer (built by GitHub Actions: iscc /DAppVersion=x.y.z installer\UltimateAudioViewer.iss)
; Earlier builds were "Spectroscope" / "DigiCheck Universal": same AppId, so this upgrades them, and their old
; files (exe, shortcuts, the old VST3 bundle) are removed below.
; Installs for all users by default (Windows asks for administrator rights), so the VST3 plugin goes to
; C:\Program Files\Common Files\VST3, the folder every DAW scans. Setup always asks (all users / me only).
; "For me only" needs no administrator rights; the plugin then goes to the per-user VST3 folder, which not
; every DAW scans, so Setup offers to restart itself for all users. An install for all users removes an
; earlier "for me only" install.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif

[Setup]
AppId={{8F3C2A51-6B0E-4D2B-9C1A-5E7D3F2B9A10}
AppName=Ultimate Audio Viewer
AppVersion={#AppVersion}
AppVerName=Ultimate Audio Viewer {#AppVersion}
AppPublisher=Ultimate Audio Viewer
DefaultDirName={autopf}\Ultimate Audio Viewer
DefaultGroupName=Ultimate Audio Viewer
DisableProgramGroupPage=yes
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog commandline
; always ask (all users / me only): otherwise Setup silently reuses the mode of an earlier install, and
; earlier builds installed "for me only"
UsePreviousPrivileges=no
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename=Ultimate-Audio-Viewer-Setup
SetupIconFile=..\Resources\icon.ico
UninstallDisplayIcon={app}\Ultimate Audio Viewer.exe
UninstallDisplayName=Ultimate Audio Viewer
WizardStyle=modern
Compression=lzma2
SolidCompression=yes
CloseApplications=yes

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "vst3"; Description: "Install the VST3 plugin (Ultimate Audio Viewer.vst3, for your DAW)"; GroupDescription: "Plugin:"
Name: "bridge"; Description: "Install UAV Bridge (Ultimate Audio Viewer Bridge.vst3: a tiny plugin that only sends your DAW's audio to the app)"; GroupDescription: "Plugin:"

[InstallDelete]
; an earlier copy of the plugin: a whole old bundle, or a single file of the same name (the older one-file
; VST3 layout, e.g. copied in by hand), which would otherwise block the bundle folder (error 183)
Type: filesandordirs; Name: "{autocf}\VST3\Ultimate Audio Viewer.vst3"; Tasks: vst3
Type: filesandordirs; Name: "{autocf}\VST3\Ultimate Audio Viewer Bridge.vst3"; Tasks: bridge
; the plugin's earlier name: two bundles with the same plugin ID would confuse DAWs
Type: filesandordirs; Name: "{autocf}\VST3\DigiCheck Universal.vst3"
; the app's earlier names and shortcuts
Type: files; Name: "{app}\Spectroscope.exe"
Type: files; Name: "{app}\TargetBuilder.exe"
Type: files; Name: "{autoprograms}\Spectroscope.lnk"
Type: files; Name: "{autoprograms}\Spectroscope Target Builder.lnk"
Type: files; Name: "{autodesktop}\Spectroscope.lnk"

[Files]
Source: "..\build\Spectroscope_artefacts\Release\Ultimate Audio Viewer.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\TargetBuilder_artefacts\Release\Ultimate Audio Viewer Target Builder.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion
; the VST3 bundle: Common Files\VST3 for an administrator install, else the per-user VST3 folder
; (%LOCALAPPDATA%\Programs\Common\VST3, the VST3 specification's user location)
Source: "..\build\DigiCheckUniversal_artefacts\Release\VST3\Ultimate Audio Viewer.vst3\*"; DestDir: "{autocf}\VST3\Ultimate Audio Viewer.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Tasks: vst3
Source: "..\build\UAVBridge_artefacts\Release\VST3\Ultimate Audio Viewer Bridge.vst3\*"; DestDir: "{autocf}\VST3\Ultimate Audio Viewer Bridge.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Tasks: bridge

[Code]
const
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{8F3C2A51-6B0E-4D2B-9C1A-5E7D3F2B9A10}_is1';

var
  Relaunched: Boolean;

// A per-user install can't write to Common Files\VST3: offer to restart Setup for all users (Windows then
// asks for administrator rights), or carry on with the per-user VST3 folder
function NextButtonClick (CurPageID: Integer): Boolean;
var
  Choice, Code: Integer;
begin
  Result := True;
  if (CurPageID = wpSelectTasks) and (WizardIsTaskSelected ('vst3') or WizardIsTaskSelected ('bridge')) and (not IsAdminInstallMode) then
  begin
    Choice := TaskDialogMsgBox ('Install the plugin where every DAW finds it?',
      'This install is for you only, so the VST3 plugin would go to your own VST3 folder:' + #13#10 +
      ExpandConstant ('{localappdata}') + '\Programs\Common\VST3' + #13#10#13#10 +
      'Some DAWs (for example older Ableton Live or FL Studio versions) only look in' + #13#10 +
      'C:\Program Files\Common Files\VST3, which needs an install for all users.',
      mbConfirmation, MB_YESNO, ['Install for all users (recommended)', 'Keep the install for me only'], IDYES);
    if Choice = IDYES then
    begin
      // the same Setup again, elevated, in all-users mode; this one closes
      if ShellExec ('runas', ExpandConstant ('{srcexe}'), '/ALLUSERS', '', SW_SHOWNORMAL, ewNoWait, Code) then
      begin
        Relaunched := True;
        WizardForm.Close;
      end
      else
        MsgBox ('Setup couldn''t restart with administrator rights (' + SysErrorMessage (Code) + ').' + #13#10 +
                'The plugin will go to your own VST3 folder.', mbInformation, MB_OK);
      Result := not Relaunched;
    end;
  end;
end;

procedure CancelButtonClick (CurPageID: Integer; var Cancel, Confirm: Boolean);
begin
  if Relaunched then Confirm := False;          // closing because Setup restarted for all users
end;

// An earlier install somewhere else (the old "Spectroscope" folder, or "for me only" when this one is for all
// users) is uninstalled first, quietly, so there's one copy and nothing left behind
procedure RemoveEarlier (Root: Integer);
var
  Path, Uninstaller: String;
  Code: Integer;
begin
  if RegQueryStringValue (Root, UninstallKey, 'Inno Setup: App Path', Path) and
     (CompareText (RemoveBackslashUnlessRoot (Path), RemoveBackslashUnlessRoot (ExpandConstant ('{app}'))) <> 0) and
     RegQueryStringValue (Root, UninstallKey, 'UninstallString', Uninstaller) then
    Exec (RemoveQuotes (Uninstaller), '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART', '', SW_HIDE, ewWaitUntilTerminated, Code);
end;

procedure CurStepChanged (CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
  begin
    if IsAdminInstallMode then RemoveEarlier (HKLM64);
    RemoveEarlier (HKCU);
  end;
end;

[Icons]
Name: "{autoprograms}\Ultimate Audio Viewer"; Filename: "{app}\Ultimate Audio Viewer.exe"
Name: "{autoprograms}\Ultimate Audio Viewer Target Builder"; Filename: "{app}\Ultimate Audio Viewer Target Builder.exe"; Comment: "Drag in songs and save them as a tonal balance target"
Name: "{autodesktop}\Ultimate Audio Viewer"; Filename: "{app}\Ultimate Audio Viewer.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Ultimate Audio Viewer.exe"; Description: "{cm:LaunchProgram,Ultimate Audio Viewer}"; Flags: nowait postinstall skipifsilent
