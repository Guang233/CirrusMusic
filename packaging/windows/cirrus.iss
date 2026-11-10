; The Windows installer (docs/tasks/release.md §5). Built by the release workflow:
;   iscc /DAppVersion=0.1.0 /DStageDir=<installed tree> /DOutputDir=<dir> packaging\windows\cirrus.iss
; Installs for the current user by default (no administrator rights); the first page
; offers installing for all users instead.

#ifndef AppVersion
  #error Pass /DAppVersion=x.y.z
#endif
#ifndef StageDir
  #error Pass /DStageDir=<the cmake --install tree>
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
; Never change AppId: Windows recognises upgrades and the uninstaller by it.
AppId={{6E0C2B7A-5D3F-4C1E-9A8B-2F4D7C9E1A35}
AppName=Cirrus
AppVersion={#AppVersion}
AppVerName=Cirrus {#AppVersion}
AppPublisher=guang233
AppCopyright=Cirrus is not affiliated with NetEase.
DefaultDirName={autopf}\Cirrus
DefaultGroupName=Cirrus
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir={#OutputDir}
OutputBaseFilename=Cirrus-{#AppVersion}-windows-x64-setup
SetupIconFile={#SourcePath}\..\..\ui\app\icons\cirrus.ico
UninstallDisplayIcon={app}\Cirrus.exe
UninstallDisplayName=Cirrus
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes

[Languages]
#if FileExists(CompilerPath + "Languages\ChineseSimplified.isl")
Name: "zh"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
#endif
Name: "en"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
; An upgrade replaces the whole tree, so files a newer version dropped do not linger.
Type: filesandordirs; Name: "{app}\qml"
Type: filesandordirs; Name: "{app}\plugins"

[Icons]
Name: "{autoprograms}\Cirrus"; Filename: "{app}\Cirrus.exe"
Name: "{autodesktop}\Cirrus"; Filename: "{app}\Cirrus.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Cirrus.exe"; Description: "{cm:LaunchProgram,Cirrus}"; Flags: nowait postinstall skipifsilent
