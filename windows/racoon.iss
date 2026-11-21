; Instalador de Racoon para Windows (Inno Setup 6).
; Lo arma GitHub Actions con lo que compila: Racoon.exe, los 16 plugins VST3 y la carpeta de muestras.
; Para armarlo a mano: iscc /DFuente=<carpeta con Racoon.exe, VST3 y Muestras Racoon> /DVersion=1.0.0 racoon.iss

#ifndef Fuente
  #define Fuente "..\dist\Racoon-Windows"
#endif
#ifndef Version
  #define Version "1.0.0"
#endif

[Setup]
AppId={{6B1E3C52-8A44-4F0B-9E21-52A0C0D1F7A3}
AppName=Racoon
AppVersion={#Version}
AppVerName=Racoon {#Version}
AppPublisher=Racoon
AppPublisherURL=https://github.com/theracoonsocietymx-lang/Racoon.beta
DefaultDirName={autopf}\Racoon
DefaultGroupName=Racoon
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
OutputDir=..\dist
OutputBaseFilename=Racoon-{#Version}-Windows
SetupIconFile=racoon.ico
UninstallDisplayIcon={app}\Racoon.exe
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
MinVersion=10.0

[Languages]
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "escritorio"; Description: "Crear un acceso directo en el escritorio"; GroupDescription: "Accesos directos:"

[Files]
; el estudio
Source: "{#Fuente}\Racoon.exe"; DestDir: "{app}"; Flags: ignoreversion
; los 16 plugins, en la carpeta donde los busca cualquier programa de audio
Source: "{#Fuente}\VST3\*"; DestDir: "{commoncf64}\VST3"; Flags: ignoreversion recursesubdirs createallsubdirs
; las muestras libres, compartidas por Racoon y los plugins (C:\ProgramData\Racoon)
Source: "{#Fuente}\Muestras Racoon\*"; DestDir: "{commonappdata}\Racoon\Muestras Racoon"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\LEEME.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Racoon"; Filename: "{app}\Racoon.exe"
Name: "{group}\Desinstalar Racoon"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Racoon"; Filename: "{app}\Racoon.exe"; Tasks: escritorio

[Run]
Filename: "{app}\Racoon.exe"; Description: "Abrir Racoon"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{commonappdata}\Racoon\Muestras Racoon"
