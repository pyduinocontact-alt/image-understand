[Setup]

AppId={{E0C5D6B6-7A8D-4C8F-9A71-7F6B6E5F77A1}
AppName=PyDuino Image Understand
AppVersion=1.0.0
AppPublisher=PyDuino
DefaultDirName={autopf}\PyDuino\ImageUnderstand
DefaultGroupName=PyDuino Image Understand
UninstallDisplayIcon={app}\image-understand.exe
SetupIconFile=C:\Users\bismi\Downloads\image-understand\ico.ico
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
CloseApplications=no
OutputDir=dist
OutputBaseFilename=PyDuino-Image-Understand-Setup
Compression=lzma
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "arabic"; MessagesFile: "compiler:Languages\Arabic.isl"
Name: "armenian"; MessagesFile: "compiler:Languages\Armenian.isl"
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "bulgarian"; MessagesFile: "compiler:Languages\Bulgarian.isl"
Name: "catalan"; MessagesFile: "compiler:Languages\Catalan.isl"
Name: "corsican"; MessagesFile: "compiler:Languages\Corsican.isl"
Name: "czech"; MessagesFile: "compiler:Languages\Czech.isl"
Name: "danish"; MessagesFile: "compiler:Languages\Danish.isl"
Name: "dutch"; MessagesFile: "compiler:Languages\Dutch.isl"
Name: "finnish"; MessagesFile: "compiler:Languages\Finnish.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "hebrew"; MessagesFile: "compiler:Languages\Hebrew.isl"
Name: "hungarian"; MessagesFile: "compiler:Languages\Hungarian.isl"
Name: "italian"; MessagesFile: "compiler:Languages\Italian.isl"
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "korean"; MessagesFile: "compiler:Languages\Korean.isl"
Name: "norwegian"; MessagesFile: "compiler:Languages\Norwegian.isl"
Name: "polish"; MessagesFile: "compiler:Languages\Polish.isl"
Name: "portuguese"; MessagesFile: "compiler:Languages\Portuguese.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "slovak"; MessagesFile: "compiler:Languages\Slovak.isl"
Name: "slovenian"; MessagesFile: "compiler:Languages\Slovenian.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "swedish"; MessagesFile: "compiler:Languages\Swedish.isl"
Name: "tamil"; MessagesFile: "compiler:Languages\Tamil.isl"
Name: "turkish"; MessagesFile: "compiler:Languages\Turkish.isl"
Name: "ukrainian"; MessagesFile: "compiler:Languages\Ukrainian.isl"

[Tasks]
Name: "addpath"; Description: "Add Image Understand to PATH"; Flags: unchecked
Name: "desktopicon"; Description: "Create a desktop icon"; Flags: unchecked

[Files]
; Main executable and supporting files (adjust Source path if your build output differs)
Source: "build\image-understand.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "build\image_understand_backend.py"; DestDir: "{app}"; Flags: ignoreversion
Source: "build\*.dll"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "build\platforms\*"; DestDir: "{app}\platforms"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "build\styles\*"; DestDir: "{app}\styles"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "build\image-understand.pdb"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{group}\PyDuino Image Understand"; Filename: "{app}\image-understand.exe"
Name: "{commondesktop}\PyDuino Image Understand"; Filename: "{app}\image-understand.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\image-understand.exe"; Description: "Launch PyDuino Image Understand"; Flags: nowait postinstall skipifsilent

[Code]
const
  EnvKeySystem = 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment';
  EnvKeyUser = 'Environment';

function SendMessageTimeout(hWnd: Integer; Msg: Integer; wParam: Integer; lParam: PAnsiChar;
  fuFlags: Integer; uTimeout: Integer; var lpdwResult: Integer): Integer;
  external 'SendMessageTimeoutW@user32.dll stdcall';

const
  MY_HWND_BROADCAST = $FFFF;
  MY_WM_SETTINGCHANGE = $001A;
  MY_SMTO_ABORTIFHUNG = $0002;

function GetEnvKey(): string;
begin
  if IsAdminInstallMode then
    Result := EnvKeySystem
  else
    Result := EnvKeyUser;
end;

procedure AddToPath(Param: string);
var
  PathValue: string;
  RootKey: Integer;
begin
  if IsAdminInstallMode then
    RootKey := HKEY_LOCAL_MACHINE
  else
    RootKey := HKEY_CURRENT_USER;

  if not RegQueryStringValue(RootKey, GetEnvKey(), 'Path', PathValue) then
    PathValue := '';
  if Pos(';' + Param + ';', ';' + PathValue + ';') = 0 then begin
    PathValue := PathValue + ';' + Param;
    RegWriteStringValue(RootKey, GetEnvKey(), 'Path', PathValue);
  end;
end;

procedure RemoveFromPath(Param: string);
var
  PathValue: string;
  Padded: string;
  Target: string;
  RootKey: Integer;
begin
  if IsAdminInstallMode then
    RootKey := HKEY_LOCAL_MACHINE
  else
    RootKey := HKEY_CURRENT_USER;

  if not RegQueryStringValue(RootKey, GetEnvKey(), 'Path', PathValue) then
    exit;
  Padded := ';' + PathValue + ';';
  Target := ';' + Param + ';';
  while Pos(Target, Padded) > 0 do
    StringChangeEx(Padded, Target, ';', True);

  // Trim leading/trailing semicolons.
  while (Length(Padded) > 0) and (Padded[1] = ';') do
    Delete(Padded, 1, 1);
  while (Length(Padded) > 0) and (Padded[Length(Padded)] = ';') do
    Delete(Padded, Length(Padded), 1);

  RegWriteStringValue(RootKey, GetEnvKey(), 'Path', Padded);
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
begin
  if (CurStep = ssPostInstall) and WizardIsTaskSelected('addpath') then begin
    AddToPath(ExpandConstant('{app}'));
    // Notify Windows that environment has changed
    SendMessageTimeout(MY_HWND_BROADCAST, MY_WM_SETTINGCHANGE, 0,
      'Environment', MY_SMTO_ABORTIFHUNG, 2000, ResultCode);
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ResultCode: Integer;
begin
  if CurUninstallStep = usUninstall then begin
    RemoveFromPath(ExpandConstant('{app}'));
    SendMessageTimeout(MY_HWND_BROADCAST, MY_WM_SETTINGCHANGE, 0,
      'Environment', MY_SMTO_ABORTIFHUNG, 2000, ResultCode);
  end;
end;
