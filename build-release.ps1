# build-release.ps1
# Build script for BG3 Cinematic Combat
# Requires: Visual Studio 2022, CMake, vcpkg

param(
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [string]$BG3Path = "C:\Spiele\Steam\steamapps\common\Baldurs Gate 3",
    [switch]$Deploy,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$ProjectRoot = $PSScriptRoot
$BuildDir = Join-Path $ProjectRoot "build"

Write-Host "=== BG3 Cinematic Combat - Build ===" -ForegroundColor Cyan

# Validate vcpkg
if (-not $VcpkgRoot) {
    Write-Host "ERROR: VCPKG_ROOT not set. Install vcpkg and set the environment variable." -ForegroundColor Red
    Write-Host "  git clone https://github.com/microsoft/vcpkg.git C:\vcpkg" -ForegroundColor Yellow
    Write-Host "  C:\vcpkg\bootstrap-vcpkg.bat" -ForegroundColor Yellow
    Write-Host '  [Environment]::SetEnvironmentVariable("VCPKG_ROOT", "C:\vcpkg", "User")' -ForegroundColor Yellow
    exit 1
}

$VcpkgToolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
if (-not (Test-Path $VcpkgToolchain)) {
    Write-Host "ERROR: vcpkg toolchain not found at $VcpkgToolchain" -ForegroundColor Red
    exit 1
}

# Clean build directory if requested
if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "Cleaning build directory..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $BuildDir
}

# Create build directory
if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}

# Configure
Write-Host "`nConfiguring CMake..." -ForegroundColor Green
cmake -S $ProjectRoot -B $BuildDir `
    -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_TOOLCHAIN_FILE="$VcpkgToolchain" `
    -DVCPKG_TARGET_TRIPLET="x64-windows-static" `
    -DCMAKE_BUILD_TYPE=Release

if ($LASTEXITCODE -ne 0) {
    Write-Host "CMake configure failed!" -ForegroundColor Red
    exit 1
}

# Build
Write-Host "`nBuilding Release..." -ForegroundColor Green
cmake --build $BuildDir --config Release --parallel

if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed!" -ForegroundColor Red
    exit 1
}

$DllPath = Join-Path $BuildDir "bin\Release\BG3CinematicCombat.dll"
if (-not (Test-Path $DllPath)) {
    # Try alternate output location
    $DllPath = Get-ChildItem -Path $BuildDir -Recurse -Filter "BG3CinematicCombat.dll" | Select-Object -First 1 -ExpandProperty FullName
}

if ($DllPath -and (Test-Path $DllPath)) {
    Write-Host "`nBuild successful: $DllPath" -ForegroundColor Green
    Write-Host "  Size: $((Get-Item $DllPath).Length / 1KB) KB"
} else {
    Write-Host "WARNING: DLL not found at expected location" -ForegroundColor Yellow
}

# Deploy
if ($Deploy) {
    Write-Host "`nDeploying to BG3..." -ForegroundColor Cyan

    $NativeModsDir = Join-Path $BG3Path "bin\NativeMods"
    if (-not (Test-Path $NativeModsDir)) {
        New-Item -ItemType Directory -Path $NativeModsDir | Out-Null
    }

    # Copy DLL and config
    Copy-Item $DllPath (Join-Path $NativeModsDir "BG3CinematicCombat.dll") -Force
    Copy-Item (Join-Path $ProjectRoot "config\BG3CinematicCombat.toml") (Join-Path $NativeModsDir "BG3CinematicCombat.toml") -Force

    # Copy SE Lua mod
    $SEModSrc = Join-Path $ProjectRoot "ScriptExtender\Mods\BG3CinematicCombat"
    $SEModDst = Join-Path $BG3Path "Data\Mods\BG3CinematicCombat"
    if (Test-Path $SEModSrc) {
        if (-not (Test-Path $SEModDst)) {
            New-Item -ItemType Directory -Path $SEModDst | Out-Null
        }
        Copy-Item -Path "$SEModSrc\*" -Destination $SEModDst -Recurse -Force
    }

    Write-Host "Deployed:" -ForegroundColor Green
    Write-Host "  DLL    -> $NativeModsDir\BG3CinematicCombat.dll"
    Write-Host "  Config -> $NativeModsDir\BG3CinematicCombat.toml"
    Write-Host "  SE Mod -> $SEModDst"
}

Write-Host "`n=== Done ===" -ForegroundColor Cyan
