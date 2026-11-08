; Corridor Leveler - Windows installer (Inno Setup 6).
; Built by CI: iscc /DSrcDir=<staging folder> /DAppVersion=<x.y.z> installer\corridor.iss
#ifndef SrcDir
  #define SrcDir "..\stage"
#endif
#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif

[Setup]
AppId={{6E1C2F43-8C0B-4B8E-9C1A-4C6F2B0D9A11}
AppName=Corridor Leveler
AppVersion={#AppVersion}
AppPublisher=Corridor
DefaultDirName={autopf}\Corridor Leveler
DefaultGroupName=Corridor Leveler
DisableProgramGroupPage=yes
LicenseFile={#SrcDir}\LICENSE.txt
OutputBaseFilename=CorridorLeveler-{#AppVersion}-Windows-Setup
OutputDir=..\dist
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Corridor Leveler

[Types]
Name: "full"; Description: "Everything"
Name: "custom"; Description: "Choose what to install"; Flags: iscustom

[Components]
Name: "vst2"; Description: "2.x plugin (OBS's built-in VST 2.x filter, older DAWs)"; Types: full custom
Name: "vst3"; Description: "VST3 plugin (DAWs)"; Types: full custom

[InstallDelete]
; parts that earlier versions installed and this one no longer ships
Type: filesandordirs; Name: "{commonappdata}\obs-studio\plugins\corridor-leveler"
Type: files; Name: "{app}\Corridor Leveler.exe"

[Files]
Source: "{#SrcDir}\vst2\Corridor Leveler.dll"; DestDir: "{commonpf64}\VSTPlugins"; Components: vst2; Flags: ignoreversion
Source: "{#SrcDir}\vst3\Corridor Leveler.vst3\*"; DestDir: "{commoncf64}\VST3\Corridor Leveler.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SrcDir}\LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\README.txt"; DestDir: "{app}"; Flags: ignoreversion isreadme

[Icons]
Name: "{group}\Read me"; Filename: "{app}\README.txt"
Name: "{group}\Uninstall Corridor Leveler"; Filename: "{uninstallexe}"
