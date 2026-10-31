; Osplay installer for Windows (DEVPLAN Phase 14). scripts/windows/Build-Installer.ps1 stages the
; files and compiles this script; see docs/BUILDING.md. It installs the app, the OsplayHelper
; service, the Virtual Display Driver, UsbDk and the Android accessory driver, and a firewall rule,
; and the uninstaller removes all of them (UsbDk only when this installer added it).

#ifndef AppVersion
  #error Define AppVersion, for example /DAppVersion=0.1.0
#endif
#ifndef StageDir
  #error Define StageDir: the folder Build-Installer.ps1 stages the files in
#endif
#ifndef UsbDkProductCode
  #error Define UsbDkProductCode: the bundled UsbDk MSI's ProductCode, from Build-Installer.ps1
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
Source: "{#StageDir}\accessory\*"; DestDir: "{app}\accessory"; Flags: ignoreversion
Source: "{#StageDir}\usbdk\UsbDk.msi"; DestDir: "{tmp}"; Flags: deleteafterinstall

[Icons]
Name: "{autoprograms}\Osplay"; Filename: "{app}\Osplay.exe"

[Run]
Filename: "{app}\Osplay.exe"; Description: "{cm:LaunchProgram,Osplay}"; Flags: nowait postinstall skipifsilent runasoriginaluser

[UninstallDelete]
; The helper's logs.
Type: filesandordirs; Name: "{commonappdata}\Osplay"
; Made on this PC by AccessoryDriver.ps1.
Type: files; Name: "{app}\accessory\OsplayAccessory.cat"

[Code]
const
  FirewallRuleName = 'Osplay';
  RunKey = 'Software\Microsoft\Windows\CurrentVersion\Run';
  { pnputil: the package is already installed and up to date, and a restart finishes the install. }
  PnpUtilUpToDate = 259;
  PnpUtilRestartNeeded = 3010;
  { msiexec: a restart finishes the install or removal. }
  MsiRestartNeeded = 3010;
  MsiFatalError = 1603;
  UsbDkServiceKey = 'SYSTEM\CurrentControlSet\Services\UsbDk';
  { Remembers that this installer added UsbDk, so the uninstaller removes only its own copy. }
  OsplayKey = 'Software\Osplay';
  InstalledUsbDkValue = 'InstalledUsbDk';

var
  RestartNeeded: Boolean;
  UsbDkNeedsRestart: Boolean;
  UsbDkRemovalPending: Boolean;
  UninstallRestartNeeded: Boolean;

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

function MsiExecPath: String;
begin
  Result := ExpandConstant('{sysnative}\msiexec.exe');
end;

{ Runs AccessoryDriver.ps1 with Params; see that script for its exit codes. PowerShell is started
  from System32, which is the 64-bit one in this install mode: started through Sysnative it gets a
  home folder that does not exist for a 64-bit process, and Get-WindowsDriver fails on it. }
function RunAccessoryScript(const Params: String): Integer;
begin
  Result := RunHidden(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
    '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' +
    ExpandConstant('{app}\accessory\AccessoryDriver.ps1') + '"' + Params);
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

{ UsbDk sends Android devices on the USB cable the switch to Osplay's accessory mode. A UsbDk that
  another app installed is used as it is and left in place by the uninstaller. Checked by the DLL
  the engine loads: removing UsbDk leaves its service until a restart. }
procedure InstallUsbDk;
var
  Code: Integer;
begin
  if FileExists(ExpandConstant('{sys}\UsbDkHelper.dll')) then
  begin
    Log('UsbDk is already installed');
    Exit;
  end;
  { MSGBOX=n makes UsbDk's driver helper run as "in", which keeps its own "Please restart your
    computer" box away even with /qn (UsbDkInstaller.wxs and UsbDkInstHelper.cpp in UsbDk 1.0.22);
    the finished page says it instead. }
  Code := RunHidden(MsiExecPath,
    '/i "' + ExpandConstant('{tmp}\UsbDk.msi') + '" /qn /norestart MSGBOX=n');
  { UsbDk removed since the last restart (by an Osplay uninstall, say) leaves its driver running,
    and its MSI fails until Windows restarts. Its uninstaller keeps the service registered either
    way, so only the failure tells the two apart. The finished page asks for a restart and another
    run of this installer instead of reporting an error (user decision). }
  if (Code = MsiFatalError) and RegKeyExists(HKLM, UsbDkServiceKey) then
  begin
    Log('UsbDk is being removed until Windows restarts');
    UsbDkRemovalPending := True;
    Exit;
  end;
  if (Code = 0) or (Code = MsiRestartNeeded) then
  begin
    RegWriteDWordValue(HKLM, OsplayKey, InstalledUsbDkValue, 1);
    if Code = MsiRestartNeeded then
      RestartNeeded := True;
    { UsbDk attaches to USB controllers only when they start, so until the next restart it sees
      almost no device. Everything else works at once, so the finished page only says so (user
      decision) instead of asking for a restart. }
    UsbDkNeedsRestart := True;
  end
  else
    ReportFailure('UsbDkSetupFailed', Code);
end;

{ The WinUSB driver for Android devices once they are in Osplay's accessory mode. }
procedure InstallAccessoryDriver;
var
  Code: Integer;
begin
  Code := RunAccessoryScript('');
  if Code = PnpUtilRestartNeeded then
    RestartNeeded := True
  else if (Code <> 0) and (Code <> PnpUtilUpToDate) then
    ReportFailure('AccessoryDriverSetupFailed', Code);
end;

{ Every profile: Windows marks new networks Public, and pairing, not the network, decides who may
  connect. }
procedure AddFirewallRule;
var
  Code: Integer;
begin
  { Deleting first keeps a single rule across upgrades; it fails harmlessly when there is none. }
  RunHidden(NetshPath, 'advfirewall firewall delete rule name="' + FirewallRuleName + '"');
  Code := RunHidden(NetshPath, 'advfirewall firewall add rule name="' + FirewallRuleName +
    '" dir=in action=allow enable=yes profile=any program="' +
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

{ "Start with Windows" names the Osplay.exe that turned it on. Pointing an existing entry at this
  install keeps the user's choice and stops a copy from another folder (an older build, a moved
  install) starting after every sign-in instead of this one. }
procedure RepointStartup;
var
  Current, Wanted: String;
begin
  Wanted := '"' + ExpandConstant('{app}\Osplay.exe') + '"';
  if RegQueryStringValue(HKEY_CURRENT_USER, RunKey, 'Osplay', Current) and
     (CompareText(Current, Wanted) <> 0) then
  begin
    Log('Start with Windows pointed at ' + Current);
    RegWriteStringValue(HKEY_CURRENT_USER, RunKey, 'Osplay', Wanted);
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    RepointStartup;
    InstallDriverAndHelper;
    InstallUsbDk;
    InstallAccessoryDriver;
    AddFirewallRule;
  end;
end;

function NeedRestart: Boolean;
begin
  Result := RestartNeeded;
end;

{ Adds a paragraph to the finished page. The label keeps its height unless told, which hides the
  new text, so it grows and the restart choice and the launch box below move down with it. }
procedure AppendFinishedNote(const MessageName: String);
var
  Grown: Integer;
begin
  WizardForm.FinishedLabel.Caption := WizardForm.FinishedLabel.Caption + #13#10#13#10 +
    CustomMessage(MessageName);
  Grown := WizardForm.FinishedLabel.AdjustHeight + ScaleY(8);
  WizardForm.YesRadio.Top := WizardForm.YesRadio.Top + Grown;
  WizardForm.NoRadio.Top := WizardForm.NoRadio.Top + Grown;
  WizardForm.RunList.Top := WizardForm.RunList.Top + Grown;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID <> wpFinished then
    Exit;
  if UsbDkRemovalPending then
    AppendFinishedNote('UsbDkRemovalPendingNote')
  else if UsbDkNeedsRestart and not RestartNeeded then
    AppendFinishedNote('UsbDkRestartNote');
end;

function UninstallNeedRestart: Boolean;
begin
  Result := UninstallRestartNeeded;
end;

procedure UninstallUsbDk;
var
  Installed: Cardinal;
begin
  if not RegQueryDWordValue(HKLM, OsplayKey, InstalledUsbDkValue, Installed) or
     (Installed <> 1) then
    Exit;
  if RunHidden(MsiExecPath, '/x {#UsbDkProductCode} /qn /norestart') = MsiRestartNeeded then
    UninstallRestartNeeded := True;
  RegDeleteValue(HKLM, OsplayKey, InstalledUsbDkValue);
  RegDeleteKeyIfEmpty(HKLM, OsplayKey);
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
  RunAccessoryScript(' -Remove');
  UninstallUsbDk;
  RunHidden(NetshPath, 'advfirewall firewall delete rule name="' + FirewallRuleName + '"');
  { "Start with Windows" in Osplay's settings. }
  RegDeleteValue(HKEY_CURRENT_USER, RunKey, 'Osplay');
end;
