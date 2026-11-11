; Inno Setup script for the Windows installer (Diorascii-<version>-Windows-Setup.exe).
;
; Build it from a staged install tree:
;   cmake --install build/release --config Release --prefix staging/root
;   iscc /DAppVersion=1.2.3 /DSourceDir=staging/root /DOutputDir=staging packaging/windows/diorascii.iss
;
; The installer copies diorascii.exe to Program Files (or, when the user declines elevation in the
; privilege dialog, to the user's own programs folder), puts its bin folder on the PATH so that
; cmd and PowerShell find `diorascii`, and registers an uninstaller. A newer version installed over
; an older one replaces it in place: the AppId below is what ties the installs together, so it must
; never change.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\staging\root"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\staging"
#endif

[Setup]
; Never change this GUID: it identifies the install for upgrades and for the uninstaller.
AppId={{6F0B7E52-3C1D-4D8A-9B64-2A5E8C7D1F40}
AppName=Diorascii
AppVersion={#AppVersion}
AppVerName=Diorascii {#AppVersion}
AppPublisher=Christian Ahrens
AppPublisherURL=https://github.com/ChristianAhrens/Diorascii
AppSupportURL=https://github.com/ChristianAhrens/Diorascii/issues
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\Diorascii
DisableProgramGroupPage=yes
DisableDirPage=auto
LicenseFile={#SourceDir}\share\doc\diorascii\LICENSE
OutputDir={#OutputDir}
OutputBaseFilename=Diorascii-{#AppVersion}-Windows-Setup
; System-wide by default (Program Files, machine PATH); the dialog lets a user without
; administrator rights choose a per-user install (user PATH) instead.
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Compression=lzma2
SolidCompression=yes
; Tells running programs (new terminals) that the environment changed, so the PATH entry
; takes effect without logging out.
ChangesEnvironment=yes
; A running diorascii.exe would block the replacement during an upgrade.
CloseApplications=yes
UninstallDisplayIcon={app}\bin\diorascii.exe
UninstallDisplayName=Diorascii

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "addtopath"; Description: "Add Diorascii to the PATH (use it as ""diorascii"" in cmd and PowerShell)"; GroupDescription: "Command line:"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion

[Code]
const
  MachineEnvKey = 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment';
  UserEnvKey = 'Environment';

{ Reads the PATH of the environment this install targets (machine for an administrator install,
  the user's otherwise). }
function ReadPath(var Value: string): Boolean;
begin
  if IsAdminInstallMode then
    Result := RegQueryStringValue(HKLM, MachineEnvKey, 'Path', Value)
  else
    Result := RegQueryStringValue(HKCU, UserEnvKey, 'Path', Value);
  if not Result then
    Value := '';
end;

{ Writes the PATH back as REG_EXPAND_SZ, the type Windows itself uses for it. }
procedure WritePath(const Value: string);
begin
  if IsAdminInstallMode then
    RegWriteExpandStringValue(HKLM, MachineEnvKey, 'Path', Value)
  else
    RegWriteExpandStringValue(HKCU, UserEnvKey, 'Path', Value);
end;

{ True if Dir is already one of the entries of Path (case-insensitive, ignoring a trailing
  backslash), so that an upgrade over the same folder does not add it twice. }
function PathContains(const Path, Dir: string): Boolean;
begin
  Result := Pos(';' + Lowercase(Dir) + ';', ';' + Lowercase(Path) + ';') > 0;
end;

procedure AddToPath(const Dir: string);
var
  Path: string;
begin
  ReadPath(Path);
  if PathContains(Path, Dir) then
    exit;
  if (Path <> '') and (Path[Length(Path)] <> ';') then
    Path := Path + ';';
  WritePath(Path + Dir);
end;

procedure RemoveFromPath(const Dir: string);
var
  Path: string;
  Needle: string;
  P: Integer;
begin
  if not ReadPath(Path) then
    exit;
  { Wrap in semicolons so that only whole entries match, then strip them again. }
  Path := ';' + Path + ';';
  Needle := ';' + Lowercase(Dir) + ';';
  P := Pos(Needle, Lowercase(Path));
  while P > 0 do
  begin
    Delete(Path, P, Length(Needle) - 1);
    P := Pos(Needle, Lowercase(Path));
  end;
  while (Length(Path) > 0) and (Path[1] = ';') do
    Delete(Path, 1, 1);
  while (Length(Path) > 0) and (Path[Length(Path)] = ';') do
    Delete(Path, Length(Path), 1);
  WritePath(Path);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and WizardIsTaskSelected('addtopath') then
    AddToPath(ExpandConstant('{app}\bin'));
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    RemoveFromPath(ExpandConstant('{app}\bin'));
end;
