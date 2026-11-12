# Baut das Windows-Paket von Hirch CAM (bis 03.11.2026 «Hirch CNC») lokal auf dem PC: übersetzen (bauen.sh in
# ink168), Tests ohne Oberfläche, Installationsprogramm mit Inno Setup 6.
#
#   powershell -ExecutionPolicy Bypass -File fork\windows\paket.ps1 [-Arbeit C:\hirch\arbeit] [-Ablage <Ordner>] [-OhneTests] [-AuchAlterName]
#
# Ergebnis: <Arbeit>\aus\hirch-cam-<datum>-<commit>-windows-setup.exe, zusätzlich in
# den Ordner -Ablage kopiert (Standard: Umgebungsvariable HIRCH_ABLAGE; auf dem Bau-PC dort einmal eintragen).
# -AuchAlterName legt ihn dort zusätzlich als hirch-cnc-… ab: Versionen vor 59a5427 suchen beim
# «Aktualisieren» nur diesen Namen (einmal für den Übergang).
param(
    [string]$Arbeit = (Join-Path (Split-Path (Split-Path (Split-Path $PSScriptRoot))) "arbeit"),
    [string]$Ablage = $env:HIRCH_ABLAGE,   # Ordner für die fertigen Dateien; leer = nicht kopieren
    [string]$Ink = "C:\ink168",
    [string]$Iscc = "",
    [switch]$OhneTests,
    [switch]$AuchAlterName
)
$ErrorActionPreference = "Stop"
$repo = Split-Path (Split-Path $PSScriptRoot)
New-Item -ItemType Directory -Force $Arbeit | Out-Null
$Arbeit = (Resolve-Path $Arbeit).Path
if (-not $Iscc) {
    $Iscc = @("$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe", "C:\werkzeuge\innosetup6\ISCC.exe",
              "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $Iscc) { throw "Inno Setup 6 (ISCC.exe) nicht gefunden" }

function Msys([string]$befehl) {
    & "$Ink\msys2_shell.cmd" -defterm -no-start -ucrt64 -here -c $befehl
    if ($LASTEXITCODE -ne 0) { throw "Fehlgeschlagen: $befehl" }
}

# 1. Übersetzen und Programmordner zusammenstellen
$env:CCACHE_DIR = "$Arbeit\ccache"
$env:CCACHE_MAXSIZE = "10G"
$env:CCACHE_COMPILERCHECK = "content"
$env:PYTHONUTF8 = "1"
$a = $Arbeit -replace '\\', '/'
Push-Location $repo
try { Msys "fork/windows/bauen.sh '$a'" } finally { Pop-Location }
$dist = "$Arbeit\dist"
$version = (Get-Content "$Arbeit\VERSION").Trim()

# 2. Tests ohne Oberfläche (die brauchen Xvfb) mit dem Python aus dem Paket: es findet
#    die DLLs des 3D-Kerns neben sich in bin\ und bringt inkex mit. pytest liegt daneben
#    in <Arbeit>\testpakete, nicht im Paket.
if (-not $OhneTests) {
    # die eben gebauten Module dorthin, wo die Tests sie suchen (in .gitignore)
    Copy-Item "$dist\share\inkscape\hirch-cam\hirch_cam\hirch_*.pyd" "$repo\hirch_cam\" -Force
    $tp = "$Arbeit\testpakete"
    if (-not (Test-Path "$tp\pytest")) {
        Msys "python -m pip install --quiet --break-system-packages --target '$($tp -replace '\\', '/')' pytest==8.4.2"
    }
    $env:PYTHONPATH = "$tp;$repo;$dist\share\inkscape\extensions"
    $env:HIRCH_OHNE_OBERFLAECHE = "1"
    Push-Location $repo
    try {
        & "$dist\bin\python.exe" -m pytest -q -p no:cacheprovider tests --ignore=tests/test_oberflaeche.py
        if ($LASTEXITCODE -ne 0) { throw "Tests fehlgeschlagen" }
    } finally { Pop-Location; Remove-Item Env:HIRCH_OHNE_OBERFLAECHE, Env:PYTHONPATH }
}

# 3. Installationsprogramm
$aus = "$Arbeit\aus"
New-Item -ItemType Directory -Force $aus | Out-Null
& $Iscc "/DVERSION=$version" "/DQUELLE=$dist" "/O$aus" "$PSScriptRoot\hirch-cam.iss"
if ($LASTEXITCODE -ne 0) { throw "Inno Setup fehlgeschlagen" }
$setup = Get-Item "$aus\hirch-cam-$version-windows-setup.exe"
Write-Host ("Fertig: {0} ({1:N0} MB)" -f $setup.FullName, ($setup.Length / 1MB))
if ($Ablage -and (Test-Path $Ablage)) {
    Copy-Item $setup.FullName $Ablage -Force
    Write-Host "Kopiert nach $Ablage"
    if ($AuchAlterName) {
        Copy-Item $setup.FullName (Join-Path $Ablage "hirch-cnc-$version-windows-setup.exe") -Force
        Write-Host "Auch als hirch-cnc-$version-windows-setup.exe abgelegt"
    }
}
