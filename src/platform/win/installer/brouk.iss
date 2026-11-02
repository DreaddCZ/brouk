; Brouk's installer for Windows (024, W4), built with Inno Setup by
; scripts/package_windows. It installs for the signed-in user only, into
; %LOCALAPPDATA%\Programs\<AppName>, so no administrator rights are needed
; (many colleagues have none, 017). The user's data stays in its own folder
; (%LOCALAPPDATA%\<AppName>) and survives updates and uninstalling.
;
; Defines given by scripts/package_windows (from CMakeLists.txt):
;   AppName, AppExe (without .exe), AppVersion, SourceDir (the built app
;   folder), OutputDir, AppIcon (the app's .ico, for Setup itself).
;
; Updates (Brouk's updater) run this installer with /VERYSILENT; /RELAUNCH=1
; starts Brouk again afterwards ("Restart to update").

#ifndef AppName
  #error AppName is not defined (run scripts/package_windows)
#endif

[Setup]
; Never change: Windows knows an installed Brouk by it.
AppId={{DE9B09C1-014E-44EE-89A8-AEB2A3DD23D0}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppName}
AppPublisherURL=https://github.com/DreaddCZ/brouk
AppSupportURL=https://github.com/DreaddCZ/brouk/issues
AppUpdatesURL=https://github.com/DreaddCZ/brouk-releases/releases
VersionInfoVersion={#AppVersion}
PrivilegesRequired=lowest
DefaultDirName={autopf}\{#AppName}
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename={#AppName}-{#AppVersion}-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupIconFile={#AppIcon}
UninstallDisplayIcon={app}\{#AppExe}.exe
UninstallDisplayName={#AppName}
; Brouk is closed by the user or by its updater; Setup waits for it below.
CloseApplications=no
RestartApplications=no

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[InstallDelete]
; An update replaces the whole app: nothing of an older version stays.
Type: filesandordirs; Name: "{app}\locales"
Type: filesandordirs; Name: "{app}\web"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "*.pdb,*.lib,*.exp,*.ilk,crash_reporter.cfg"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}.exe"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}.exe"; Tasks: desktopicon

[Run]
; Brouk can then be chosen as the default browser in Windows' Settings.
Filename: "{app}\{#AppExe}.exe"; Parameters: "--register-browser"; Flags: runhidden waituntilterminated
Filename: "{app}\{#AppExe}.exe"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
Filename: "{app}\{#AppExe}.exe"; Flags: nowait; Check: Relaunch

[UninstallRun]
Filename: "{app}\{#AppExe}.exe"; Parameters: "--unregister-browser"; Flags: runhidden waituntilterminated; RunOnceId: "UnregisterBrowser"

[Code]
const
  RunningMutex = '{#AppName}Running';

function Relaunch: Boolean;
begin
  Result := ExpandConstant('{param:RELAUNCH|0}') = '1';
end;

// Brouk holds RunningMutex while it runs. Its updater starts this installer
// just before Brouk ends, so a silent install waits for it; otherwise the
// user is asked to close it.
function WaitForBrouk(Silent: Boolean): Boolean;
var
  Waited: Integer;
begin
  Waited := 0;
  while CheckForMutexes(RunningMutex) do
  begin
    if Silent then
    begin
      if Waited >= 60000 then
      begin
        Result := False;
        Exit;
      end;
      Sleep(500);
      Waited := Waited + 500;
    end
    else if MsgBox('Close ' + '{#AppName}' + ' to continue.', mbInformation, MB_OKCANCEL) = IDCANCEL then
    begin
      Result := False;
      Exit;
    end;
  end;
  Result := True;
end;

function InitializeSetup: Boolean;
begin
  Result := WaitForBrouk(WizardSilent);
end;

function InitializeUninstall: Boolean;
begin
  Result := WaitForBrouk(UninstallSilent);
end;
