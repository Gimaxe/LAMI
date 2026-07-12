; Installeur Windows de LAMI (Inno Setup).
; Installation PAR UTILISATEUR (%LOCALAPPDATA%\Programs\LAMI) : aucun droit
; administrateur requis, et l'auto-mise à jour (qui remplace les fichiers en
; place) continue de fonctionner puisque le dossier est accessible en écriture.
; Chemins relatifs au dossier de ce script (installer/), d'où les "..\".

#define MyAppName "LAMI"
#ifndef MyAppVersion
  #define MyAppVersion "0.0.0"
#endif

[Setup]
AppId={{B7E5B1A0-1C2D-4E3F-9A8B-0C1D2E3F4A5B}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher=Gimaxe
AppPublisherURL=https://github.com/Gimaxe/LAMI
DefaultDirName={localappdata}\Programs\LAMI
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir=..\installer-out
OutputBaseFilename=LAMI-Setup-v{#MyAppVersion}
SetupIconFile=..\web\assets\lami-icon.ico
UninstallDisplayIcon={app}\lami_shell.exe
UninstallDisplayName={#MyAppName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "french"; MessagesFile: "compiler:Languages\French.isl"

[Tasks]
Name: "desktopicon"; Description: "Créer un raccourci sur le Bureau"; GroupDescription: "Raccourcis :"

[Files]
Source: "..\dist\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\lami_shell.exe"
Name: "{autodesktop}\{#MyAppName}";  Filename: "{app}\lami_shell.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\lami_shell.exe"; Description: "Lancer {#MyAppName}"; Flags: nowait postinstall skipifsilent
