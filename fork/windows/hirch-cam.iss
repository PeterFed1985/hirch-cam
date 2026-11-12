; Installationsprogramm für Hirch CAM (bis 03.11.2026 «Hirch CNC») unter Windows 10/11 (64 Bit)
;
;   ISCC.exe /DVERSION=2026.10.28-abc1234 /DQUELLE=C:\hirch\arbeit\dist /OC:\hirch\arbeit\aus hirch-cam.iss
;
; Installiert ohne Administratorrechte nach %LOCALAPPDATA%\Programs\Hirch CAM
; (wer will, wählt im Dialog «für alle Benutzer»). Ein Update ist einfach das
; neuere Installationsprogramm. Einstellungen liegen in %APPDATA%\hirch-cam und
; bleiben beim Update und beim Entfernen erhalten.
;
; Umzug Stufe 2 (Peter 04.11.2026): Programmordner bis dahin «Hirch CNC». Das Update installiert in den neuen
; Ordner und entfernt den alten; die Einstellungen kopiert das Programm beim ersten Start (hirch_cam.orte.umzug),
; %APPDATA%\hirch-cnc bleibt als Sicherung stehen.

#ifndef VERSION
  #define VERSION "0"
#endif
#ifndef QUELLE
  #define QUELLE "..\..\..\arbeit\dist"
#endif

[Setup]
; gleiche AppId wie bisher = derselbe Eintrag unter «Apps»
AppId={{6F1C2B0E-5A43-4C8B-9E0D-48A1C3B7D2F5}
AppName=Hirch CAM
AppVersion={#VERSION}
AppVerName=Hirch CAM {#VERSION}
AppPublisher=Hirch
DefaultDirName={autopf}\Hirch CAM
; nicht in den Ordner der alten Installation («Hirch CNC»), sondern in den neuen
UsePreviousAppDir=no
DefaultGroupName=Hirch CAM
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputBaseFilename=hirch-cam-{#VERSION}-windows-setup
SetupIconFile={#QUELLE}\hirch-cam.ico
UninstallDisplayIcon={app}\hirch-cam.ico
Compression=lzma2/max
SolidCompression=yes
LZMANumBlockThreads=4
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
ShowLanguageDialog=no
ChangesAssociations=yes

[Languages]
Name: "de"; MessagesFile: "compiler:Languages\German.isl"

[Tasks]
Name: "desktop"; Description: "Symbol auf dem Desktop"; GroupDescription: "Zusätzlich:"

[InstallDelete]
; alte Programmdateien vor dem Update entfernen
Type: filesandordirs; Name: "{app}\bin"
Type: filesandordirs; Name: "{app}\lib"
Type: filesandordirs; Name: "{app}\share"
; Verknüpfungen unter dem alten Namen (bis 03.11.2026 «Hirch CNC»)
Type: files; Name: "{autoprograms}\Hirch CNC.lnk"
Type: files; Name: "{autodesktop}\Hirch CNC.lnk"

[Files]
Source: "{#QUELLE}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Hirch CAM"; Filename: "{app}\bin\inkscape.exe"; WorkingDir: "{userdocs}"; IconFilename: "{app}\hirch-cam.ico"; Comment: "Zeichnen und G-Code für die CNC-Fräse erstellen"
Name: "{autodesktop}\Hirch CAM"; Filename: "{app}\bin\inkscape.exe"; WorkingDir: "{userdocs}"; IconFilename: "{app}\hirch-cam.ico"; Tasks: desktop

[Registry]
; Kennung bis 04.11.2026 HirchCNC.Zeichnung: Schlüssel und Verweise darauf entfernen (zeigten auf den alten Ordner)
Root: HKA; Subkey: "Software\Classes\HirchCNC.Zeichnung"; Flags: deletekey
Root: HKA; Subkey: "Software\Classes\.hir\OpenWithProgids"; ValueType: none; ValueName: "HirchCNC.Zeichnung"; Flags: deletevalue
Root: HKA; Subkey: "Software\Classes\.hirch\OpenWithProgids"; ValueType: none; ValueName: "HirchCNC.Zeichnung"; Flags: deletevalue
Root: HKA; Subkey: "Software\Classes\.svg\OpenWithProgids"; ValueType: none; ValueName: "HirchCNC.Zeichnung"; Flags: deletevalue
; Dateiendung .hir (auch .hirch der ersten Fassung): öffnet Hirch CAM, eigenes Symbol.
; HKA = HKCU bei Installation ohne Adminrechte, sonst HKLM.
Root: HKA; Subkey: "Software\Classes\.hir"; ValueType: string; ValueName: ""; ValueData: "HirchCAM.Zeichnung"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.hir"; ValueType: string; ValueName: "Content Type"; ValueData: "application/x-hirch-cam"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.hir\OpenWithProgids"; ValueType: string; ValueName: "HirchCAM.Zeichnung"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.hirch"; ValueType: string; ValueName: ""; ValueData: "HirchCAM.Zeichnung"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.hirch\OpenWithProgids"; ValueType: string; ValueName: "HirchCAM.Zeichnung"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\HirchCAM.Zeichnung"; ValueType: string; ValueName: ""; ValueData: "Hirch-CAM-Zeichnung"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\HirchCAM.Zeichnung\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\hirch-cam.ico,0"
Root: HKA; Subkey: "Software\Classes\HirchCAM.Zeichnung\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\bin\inkscape.exe"" ""%1"""
; SVG-Dateien bietet Windows unter «Öffnen mit» an, ohne die Standardzuordnung zu ändern
Root: HKA; Subkey: "Software\Classes\.svg\OpenWithProgids"; ValueType: string; ValueName: "HirchCAM.Zeichnung"; ValueData: ""; Flags: uninsdeletevalue

[Run]
Filename: "{app}\bin\inkscape.exe"; Description: "Hirch CAM starten"; Flags: nowait postinstall skipifsilent

[Code]
// Programmordner bis 04.11.2026
function AlterOrdner(): String;
begin
  Result := ExpandConstant('{autopf}\Hirch CNC');
end;

// gdbus.exe (D-Bus-Hilfe von GLib, startet mit dem Ordnerfenster) läuft nach «Aktualisieren» weiter und
// sperrt bin\ – das Schliessen über CloseApplications klappt dabei nicht (04.11.2026). Vorher beenden,
// nur die aus unserem alten oder neuen Programmordner. Läuft das alte Programm noch, sieht CloseApplications
// es nicht (es prüft nur den neuen Ordner) – dann bitten, es zu schliessen.
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Ergebnis: Integer;
begin
  Exec('powershell.exe', '-NoProfile -NonInteractive -Command "Get-Process gdbus -ErrorAction SilentlyContinue | ' +
       'Where-Object { $_.Path -like ''' + AddBackslash(ExpandConstant('{app}')) + 'bin\*'' -or ' +
       '$_.Path -like ''' + AddBackslash(AlterOrdner()) + 'bin\*'' } | Stop-Process -Force; ' +
       'if (Get-Process inkscape -ErrorAction SilentlyContinue | Where-Object { $_.Path -like ''' +
       AddBackslash(AlterOrdner()) + 'bin\*'' }) { exit 3 }"',
       '', SW_HIDE, ewWaitUntilTerminated, Ergebnis);
  if Ergebnis = 3 then
    Result := 'Hirch CAM läuft noch. Bitte schliessen und das Installationsprogramm nochmals starten.'
  else
    Result := '';
end;

// Nach dem Update aus «Hirch CNC» den alten Programmordner entfernen (nur wenn er wirklich unser Programm enthält)
procedure CurStepChanged(CurStep: TSetupStep);
var
  Alt: String;
begin
  if CurStep = ssPostInstall then begin
    Alt := AlterOrdner();
    if (CompareText(Alt, ExpandConstant('{app}')) <> 0) and FileExists(Alt + '\bin\inkscape.exe') and
       FileExists(Alt + '\VERSION') then
      DelTree(Alt, True, True, True);
  end;
end;
