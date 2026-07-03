# setup-devtools.ps1
# ============================================================================
# BG3 Cinematic Combat - Entwicklungsumgebung Setup
# MUSS ALS ADMINISTRATOR ausgefuehrt werden!
# ============================================================================
# Rechtsklick -> "Als Administrator ausfuehren" in PowerShell
# Oder: Start-Process powershell -Verb RunAs -ArgumentList "-File setup-devtools.ps1"

$ErrorActionPreference = "Stop"

Write-Host "=== BG3 Cinematic Combat - Dev Environment Setup ===" -ForegroundColor Cyan
Write-Host ""

# Check admin
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "FEHLER: Dieses Skript muss als Administrator ausgefuehrt werden!" -ForegroundColor Red
    Write-Host "Rechtsklick auf PowerShell -> 'Als Administrator ausfuehren'" -ForegroundColor Yellow
    Write-Host ""
    pause
    exit 1
}

Write-Host "[1/4] Visual Studio 2022 Build Tools installieren..." -ForegroundColor Green
winget install Microsoft.VisualStudio.2022.BuildTools `
    --accept-package-agreements --accept-source-agreements `
    --override "--add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.Windows11SDK.22621 --passive --wait"
if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne -1978335189) {
    Write-Host "  WARNUNG: VS Build Tools Installation gab Exit Code $LASTEXITCODE" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "[2/4] CMake installieren..." -ForegroundColor Green
winget install Kitware.CMake --accept-package-agreements --accept-source-agreements
if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne -1978335189) {
    Write-Host "  WARNUNG: CMake Installation gab Exit Code $LASTEXITCODE" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "[3/4] Git installieren (fuer vcpkg)..." -ForegroundColor Green
winget install Git.Git --accept-package-agreements --accept-source-agreements
if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne -1978335189) {
    Write-Host "  WARNUNG: Git Installation gab Exit Code $LASTEXITCODE" -ForegroundColor Yellow
}

# Refresh PATH
$env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path", "User")

Write-Host ""
Write-Host "[4/4] vcpkg installieren..." -ForegroundColor Green
$VcpkgPath = "C:\vcpkg"
if (-not (Test-Path $VcpkgPath)) {
    git clone https://github.com/microsoft/vcpkg.git $VcpkgPath
    & "$VcpkgPath\bootstrap-vcpkg.bat" -disableMetrics
} else {
    Write-Host "  vcpkg bereits vorhanden in $VcpkgPath" -ForegroundColor Yellow
}

# Set VCPKG_ROOT environment variable
[System.Environment]::SetEnvironmentVariable("VCPKG_ROOT", $VcpkgPath, "User")
$env:VCPKG_ROOT = $VcpkgPath

Write-Host ""
Write-Host "============================================" -ForegroundColor Cyan
Write-Host "  Setup abgeschlossen!" -ForegroundColor Green
Write-Host "============================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Installiert:"
Write-Host "  - Visual Studio 2022 Build Tools (C++ Compiler)"
Write-Host "  - CMake (Build System)"
Write-Host "  - Git"
Write-Host "  - vcpkg (C++ Package Manager) in $VcpkgPath"
Write-Host ""
Write-Host "WICHTIG: Starte VS Code neu damit die PATH-Aenderungen wirksam werden!" -ForegroundColor Yellow
Write-Host ""
Write-Host "Dann zum Bauen:" -ForegroundColor Cyan
Write-Host '  cd BG3CinematicCombat'
Write-Host '  .\build-release.ps1 -Deploy'
Write-Host ""
pause
