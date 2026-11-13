; Luckgram Desktop installer (D1). Built by .github/workflows/build.yml:
;   iscc /DMyAppVersion=7.4.2 /DMyBuildNumber=12 /DSourceDir=<stage> /DOutputDir=<dist> luckgram.iss
;
; Per-user install, no admin rights: %LOCALAPPDATA%\Programs\Luckgram.
; Chat data lives in %APPDATA%\Luckgram (see Luckgram::IsPortable); the
; uninstaller asks whether to delete it and keeps it by default.

#ifndef MyAppVersion
  #define MyAppVersion "0.0.0"
#endif
#ifndef MyBuildNumber
  #define MyBuildNumber "0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\out\Release\stage"
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

#define MyAppName "Luckgram"
#define MyAppExeName "Luckgram.exe"
; Must match AppId in Telegram/SourceFiles/core/version.h.
#define MyAppId "{{6F1C2B7E-4A3D-4C59-9E21-8B5D0A7C3E14}"
; Must match AppUserModelIdBase / ToastActivator in platform/win.
#define MyAppUserModelId "Luckgram.LuckgramDesktop"
#define MyToastActivatorClsid "8C3D5E21-7B4A-4F06-A1D9-2E6B7C4F9A35"

[Setup]
AppId={#MyAppId}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion} ({#MyBuildNumber})
AppPublisher=Luckgram
AppPublisherURL=https://luckgram.cc
AppSupportURL=https://github.com/limeng9088/luckgram-desktop
VersionInfoVersion={#MyAppVersion}.{#MyBuildNumber}
VersionInfoProductName={#MyAppName}
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=
DefaultDirName={localappdata}\Programs\{#MyAppName}
DisableDirPage=yes
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#MyAppExeName}
UninstallDisplayName={#MyAppName}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename=Luckgram-Setup-{#MyAppVersion}-{#MyBuildNumber}-x64
SetupIconFile=..\..\Telegram\Resources\art\icon256.ico
WizardSmallImageFile=wizard-small-55.bmp,wizard-small-69.bmp,wizard-small-83.bmp,wizard-small-110.bmp,wizard-small-138.bmp
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=force
RestartApplications=no
ShowLanguageDialog=no
LanguageDetectionMethod=uilanguage

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
; Copy of jrsoftware/issrc Files/Languages/ChineseSimplified.isl (Inno Setup 6.5+).
Name: "zh"; MessagesFile: "ChineseSimplified.isl"

[CustomMessages]
en.DeleteData=Also delete Luckgram chat data and settings on this computer?%n%n%1%n%nChoose "No" to keep them (recommended if you will reinstall).
zh.DeleteData=是否同时删除这台电脑上的 Luckgram 聊天数据和设置？%n%n%1%n%n选「否」会保留（以后重新安装可以继续使用）。
en.DesktopIcon=Create a &desktop shortcut
zh.DesktopIcon=创建桌面快捷方式(&D)
en.LaunchApp=Launch Luckgram
zh.LaunchApp=启动 Luckgram

[Tasks]
Name: "desktopicon"; Description: "{cm:DesktopIcon}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
; D12: the built-in Xray core (Xray-core, MPL-2.0) and its license.
Source: "{#SourceDir}\xray.exe"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#SourceDir}\xray-LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#SourceDir}\modules\*"; DestDir: "{app}\modules"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{userprograms}\{#MyAppName}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; AppUserModelID: "{#MyAppUserModelId}"; AppUserModelToastActivatorCLSID: "{#MyToastActivatorClsid}"
Name: "{userdesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; AppUserModelID: "{#MyAppUserModelId}"; AppUserModelToastActivatorCLSID: "{#MyToastActivatorClsid}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchApp}"; Flags: nowait postinstall skipifsilent
; D4 in-app update: Luckgram runs this installer with /VERYSILENT
; /luckgramupdate=1 and quits; start the new version afterwards.
Filename: "{app}\{#MyAppExeName}"; Flags: nowait postinstall skipifnotsilent; Check: IsLuckgramUpdate

[UninstallDelete]
; The whole program folder goes; chat data is outside it (see [Code]).
Type: filesandordirs; Name: "{app}"

[Code]
function IsLuckgramUpdate: Boolean;
begin
  Result := ExpandConstant('{param:luckgramupdate|0}') = '1';
end;

function DataDir(): String;
begin
  Result := ExpandConstant('{userappdata}\Luckgram');
end;

// Removes the tg:// association only when it points at this Luckgram, so an
// association owned by official Telegram Desktop is never touched.
procedure RemoveTgScheme();
var
  Command: String;
begin
  if RegQueryStringValue(HKCU, 'Software\Classes\tg\shell\open\command', '', Command) then
  begin
    if Pos(Lowercase(ExpandConstant('{app}')), Lowercase(Command)) > 0 then
      RegDeleteKeyIncludingSubkeys(HKCU, 'Software\Classes\tg');
  end;
  RegDeleteKeyIncludingSubkeys(HKCU, 'Software\Classes\luckgram.tg');
  RegDeleteKeyIncludingSubkeys(HKCU, 'Software\Luckgram');
  RegDeleteValue(HKCU, 'Software\RegisteredApplications', 'Luckgram');
end;

// D2: the device token is in Windows Credential Manager (generic credential
// "Luckgram/device_token", see luckgram_secure_store.cpp); it goes together
// with the data, kept when the user keeps the data.
procedure RemoveLuckgramToken();
var
  ResultCode: Integer;
begin
  Exec(ExpandConstant('{sys}\cmdkey.exe'), '/delete:Luckgram/device_token', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    RemoveTgScheme();
  if (CurUninstallStep = usPostUninstall) and DirExists(DataDir()) and not UninstallSilent() then
  begin
    // Default button is "No": keep chat data.
    if MsgBox(FmtMessage(CustomMessage('DeleteData'), [DataDir()]), mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
    begin
      DelTree(DataDir(), True, True, True);
      RemoveLuckgramToken();
    end;
  end;
end;
