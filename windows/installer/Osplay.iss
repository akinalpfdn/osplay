; Osplay installer for Windows (DEVPLAN Phase 14). scripts/windows/Build-Installer.ps1 stages the
; files and compiles this script; see docs/BUILDING.md. It installs the app, the OsplayHelper
; service, the Virtual Display Driver and a firewall rule, and the uninstaller removes all of them.

#ifndef AppVersion
  #error Define AppVersion, for example /DAppVersion=0.1.0
#endif
#ifndef StageDir
  #error Define StageDir: the folder Build-Installer.ps1 stages the files in
#endif
#ifndef OutputDir
  #define OutputDir AddBackslash(SourcePath) + "..\build\installer"
#endif
#define RepoDir AddBackslash(SourcePath) + "..\.."

[Setup]
; Never change AppId: upgrades and the uninstaller find an installed Osplay by it.
AppId={{E6A6020B-BBB0-4C02-A8F0-5C134954A530}
AppName=Osplay
AppVersion={#AppVersion}
AppVerName=Osplay {#AppVersion}
AppPublisher=Akinalp Fidan
AppPublisherURL=https://github.com/akinalpfdn/osplay
AppSupportURL=https://github.com/akinalpfdn/osplay/issues
AppUpdatesURL=https://github.com/akinalpfdn/osplay/releases
AppCopyright=Copyright (c) 2026 Akinalp Fidan
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\Osplay
DisableProgramGroupPage=yes
PrivilegesRequired=admin
; The bundled driver is x64 only, so ARM64 Windows is not offered the install.
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
; Windows 10 22H2 or later.
MinVersion=10.0.19045
LicenseFile={#StageDir}\licenses\LICENSE.txt
SetupIconFile={#RepoDir}\windows\app\Osplay.App\Assets\Osplay.ico
UninstallDisplayIcon={app}\Osplay.exe
UninstallDisplayName=Osplay
OutputDir={#OutputDir}
; No version in the name, so releases/latest/download/Osplay-Setup.exe always works.
OutputBaseFilename=Osplay-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; Osplay has no way to be asked to quit, so a running copy is closed when its files are replaced.
CloseApplications=force
RestartApplications=no
SetupLogging=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl,Osplay.en.isl"
Name: "turkish"; MessagesFile: "compiler:Languages\Turkish.isl,Osplay.tr.isl"

[Files]
Source: "{#StageDir}\app\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StageDir}\driver\*"; DestDir: "{app}\driver"; Flags: ignoreversion
Source: "{#StageDir}\licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Osplay"; Filename: "{app}\Osplay.exe"

[Run]
Filename: "{app}\Osplay.exe"; Description: "{cm:LaunchProgram,Osplay}"; Flags: nowait postinstall skipifsilent runasoriginaluser

[UninstallDelete]
; The helper's logs.
Type: filesandordirs; Name: "{commonappdata}\Osplay"

[Code]
const
  FirewallRuleName = 'Osplay';
  RunKey = 'Software\Microsoft\Windows\CurrentVersion\Run';
  { pnputil: the package is already installed and up to date, and a restart finishes the install. }
  PnpUtilUpToDate = 259;
  PnpUtilRestartNeeded = 3010;

var
  RestartNeeded: Boolean;

{ Runs a program without a window and waits for it. Returns its exit code, or -1 when it could not
  be started. Both are written to the setup log. }
function RunHidden(const FileName, Params: String): Integer;
var
  ResultCode: Integer;
begin
  Log('Running: ' + FileName + ' ' + Params);
  if Exec(FileName, Params, '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
  begin
    Result := ResultCode;
    Log('Exit code: ' + IntToStr(Result));
  end
  else
  begin
    Result := -1;
    Log('Could not start: ' + SysErrorMessage(ResultCode));
  end;
end;

function HelperPath: String;
begin
  Result := ExpandConstant('{app}\OsplayHelper.exe');
end;

function NetshPath: String;
begin
  Result := ExpandConstant('{sysnative}\netsh.exe');
end;

procedure ReportFailure(const MessageName: String; Code: Integer);
begin
  SuppressibleMsgBox(FmtMessage(CustomMessage(MessageName), [IntToStr(Code)]), mbError, MB_OK, IDOK);
end;

{ The driver package goes into the driver store; the helper then adds the first device and
  registers its service, which adds one more device per virtual monitor when it needs them. }
procedure InstallDriverAndHelper;
var
  Code: Integer;
begin
  Code := RunHidden(ExpandConstant('{sysnative}\pnputil.exe'),
    '/add-driver "' + ExpandConstant('{app}\driver\MttVDD.inf') + '" /install');
  if Code = PnpUtilRestartNeeded then
    RestartNeeded := True
  else if (Code <> 0) and (Code <> PnpUtilUpToDate) then
  begin
    ReportFailure('DriverSetupFailed', Code);
    Exit;
  end;
  Code := RunHidden(HelperPath, '--install');
  if Code <> 0 then
    ReportFailure('HelperSetupFailed', Code);
end;

procedure AddFirewallRule;
var
  Code: Integer;
begin
  { Deleting first keeps a single rule across upgrades; it fails harmlessly when there is none. }
  RunHidden(NetshPath, 'advfirewall firewall delete rule name="' + FirewallRuleName + '"');
  Code := RunHidden(NetshPath, 'advfirewall firewall add rule name="' + FirewallRuleName +
    '" dir=in action=allow enable=yes profile=private program="' +
    ExpandConstant('{app}\Osplay.exe') + '"');
  if Code <> 0 then
    ReportFailure('FirewallSetupFailed', Code);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  { An upgrade: stop the old service so its executable can be replaced. The driver devices stay,
    so Windows keeps the virtual monitors' arrangement. }
  if FileExists(HelperPath) then
    RunHidden(HelperPath, '--stop');
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    InstallDriverAndHelper;
    AddFirewallRule;
  end;
end;

function NeedRestart: Boolean;
begin
  Result := RestartNeeded;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep <> usUninstall then
    Exit;
  { A running Osplay holds its files and the helper connection. }
  RunHidden(ExpandConstant('{sysnative}\taskkill.exe'), '/F /IM Osplay.exe');
  { The service, every driver device (one per virtual monitor) and the driver settings. }
  RunHidden(HelperPath, '--uninstall');
  if UninstallSilent or
     (MsgBox(CustomMessage('RemoveDriverQuestion'), mbConfirmation, MB_YESNO) = IDYES) then
    RunHidden(HelperPath, '--remove-driver');
  RunHidden(NetshPath, 'advfirewall firewall delete rule name="' + FirewallRuleName + '"');
  { "Start with Windows" in Osplay's settings. }
  RegDeleteValue(HKEY_CURRENT_USER, RunKey, 'Osplay');
end;
