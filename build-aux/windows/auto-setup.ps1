# GNOME Papers Dependency Auto-Setup Script for Windows
# This script ensures MSYS2 is installed and installs all UCRT64 compiler/library dependencies.

$ErrorActionPreference = "Stop"

# 1. Ask for installation location
$defaultInstallDir = "C:\nbin"
Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "        GNOME Papers Windows dependency setup" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host ""
$installPath = Read-Host "Enter dependency installation directory [Default: $defaultInstallDir]"
if ([string]::IsNullOrWhitespace($installPath)) {
    $installPath = $defaultInstallDir
}

# Resolve full path
$installPath = [System.IO.Path]::GetFullPath($installPath)
$msysPath = Join-Path $installPath "msys64"
$bashExe = Join-Path $msysPath "usr\bin\bash.exe"

# 2. Check if MSYS2 is already installed
$needMsysInstall = $true
if (Test-Path $bashExe) {
    Write-Host "MSYS2 was found at: $msysPath" -ForegroundColor Green
    $needMsysInstall = $false
} else {
    # Check fallback standard path C:\msys64
    if (Test-Path "C:\msys64\usr\bin\bash.exe") {
        Write-Host "Existing MSYS2 installation found at C:\msys64." -ForegroundColor Yellow
        $useDefault = Read-Host "Use this existing MSYS2 installation? (Y/N) [Default: Y]"
        if ([string]::IsNullOrWhitespace($useDefault) -or $useDefault.ToUpper() -eq "Y") {
            $msysPath = "C:\msys64"
            $bashExe = "C:\msys64\usr\bin\bash.exe"
            $needMsysInstall = $false
        }
    }
}

# 3. Download and Install MSYS2 if required
if ($needMsysInstall) {
    if (!(Test-Path $installPath)) {
        New-Item -ItemType Directory -Force -Path $installPath | Out-Null
    }

    $tempInstaller = Join-Path $env:TEMP "msys2-installer.exe"
    Write-Host "Downloading MSYS2 installer..." -ForegroundColor Cyan
    $downloadUrl = "https://github.com/msys2/msys2-installer/releases/download/nightly-x86_64/msys2-x86_64-latest.exe"

    Invoke-WebRequest -Uri $downloadUrl -OutFile $tempInstaller

    Write-Host "Installing MSYS2 silently to $msysPath (this may take a few minutes)..." -ForegroundColor Cyan
    # Silent install arguments: dir, unchecked icons, automatic execution, etc.
    $installArgs = @(
        "--mode", "script",
        "--unattendedmodeui", "none",
        "default_installation_directory=$msysPath"
    )
    $process = Start-Process -FilePath $tempInstaller -ArgumentList $installArgs -Wait -NoNewWindow -PassThru

    if ($process.ExitCode -ne 0) {
        throw "MSYS2 installation failed with exit code $($process.ExitCode)."
    }

    # Remove installer temp file
    if (Test-Path $tempInstaller) {
        Remove-Item $tempInstaller -Force
    }

    Write-Host "MSYS2 successfully installed!" -ForegroundColor Green
}

# 4. Install Pacman Dependencies
Write-Host "Installing required UCRT64 toolchain and libraries..." -ForegroundColor Cyan

$packages = @(
    "mingw-w64-ucrt-x86_64-gcc",
    "mingw-w64-ucrt-x86_64-meson",
    "mingw-w64-ucrt-x86_64-ninja",
    "mingw-w64-ucrt-x86_64-rust",
    "mingw-w64-ucrt-x86_64-gtk4",
    "mingw-w64-ucrt-x86_64-libadwaita",
    "mingw-w64-ucrt-x86_64-poppler-glib",
    "mingw-w64-ucrt-x86_64-libspelling",
    "mingw-w64-ucrt-x86_64-djvulibre",
    "mingw-w64-ucrt-x86_64-libarchive",
    "mingw-w64-ucrt-x86_64-libtiff",
    "mingw-w64-ucrt-x86_64-pkg-config",
    "diffutils"
)

$packageString = $packages -join " "

# Run pacman inside MSYS2 bash environment
$bashArgs = "-lc `"pacman -Sy --noconfirm --needed $packageString`""
$process = Start-Process -FilePath $bashExe -ArgumentList $bashArgs -Wait -NoNewWindow -PassThru

if ($process.ExitCode -ne 0) {
    throw "Failed to install dependencies via MSYS2 pacman."
}

Write-Host ""
Write-Host "==========================================================" -ForegroundColor Green
Write-Host "   Setup complete! All dependencies are installed." -ForegroundColor Green
Write-Host "   To build the project, run the following commands:" -ForegroundColor Green
Write-Host "   MSYS2 Path: $bashExe" -ForegroundColor Yellow
Write-Host "   Commands:" -ForegroundColor Yellow
Write-Host "     1. export PATH=/ucrt64/bin:`$PATH" -ForegroundColor Yellow
Write-Host "     2. meson setup build --buildtype=release --strip --reconfigure" -ForegroundColor Yellow
Write-Host "     3. meson compile -C build" -ForegroundColor Yellow
Write-Host "==========================================================" -ForegroundColor Green
