# GNOME Papers Windows Bundle Script
# Collects papers.exe, dlls, backends, GSettings schemas, and assets into dist/ folder.

$ErrorActionPreference = "Stop"

# 1. Setup paths
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if (Test-Path (Join-Path $scriptDir "..\..\meson.build")) {
    $projectRoot = (Get-Item (Join-Path $scriptDir "..\..")).FullName
} else {
    $projectRoot = (Get-Item .).FullName
}
Set-Location $projectRoot

$distDir = Join-Path $projectRoot "dist"
$binDir = $distDir
$backendsDir = Join-Path $distDir "lib\papers\6\backends"
$schemasDir = Join-Path $distDir "share\glib-2.0\schemas"
$iconsDir = Join-Path $distDir "share\icons"
$pixbufDir = Join-Path $distDir "lib\gdk-pixbuf-2.0"

# Find MSYS2 dynamically
$bashCmd = Get-Command "bash.exe" -ErrorAction SilentlyContinue
if ($bashCmd) {
    $bashExe = $bashCmd.Source
    $binDirObj = (Get-Item $bashExe).Directory
    if ($binDirObj.Name -eq "bin" -and $binDirObj.Parent.Name -eq "usr") {
        $msysPath = $binDirObj.Parent.Parent.FullName
    } elseif ($binDirObj.Name -eq "bin") {
        $msysPath = $binDirObj.Parent.FullName
    } else {
        $msysPath = "C:\msys64"
    }
} elseif ($env:MSYSTEM_PREFIX -and (Test-Path "$env:MSYSTEM_PREFIX\..\usr\bin\bash.exe")) {
    $msysPath = (Get-Item "$env:MSYSTEM_PREFIX\..").FullName
    $bashExe = "$msysPath\usr\bin\bash.exe"
} elseif (Test-Path "C:\msys64\usr\bin\bash.exe") {
    $msysPath = "C:\msys64"
    $bashExe = "$msysPath\usr\bin\bash.exe"
} elseif (Test-Path "C:\nbin\msys64\usr\bin\bash.exe") {
    $msysPath = "C:\nbin\msys64"
    $bashExe = "$msysPath\usr\bin\bash.exe"
} else {
    throw "MSYS2 bash.exe was not found."
}

if ($env:MSYSTEM_PREFIX -and (Test-Path $env:MSYSTEM_PREFIX)) {
    $ucrtBin = Join-Path $env:MSYSTEM_PREFIX "bin"
    $ucrtShare = Join-Path $env:MSYSTEM_PREFIX "share"
    $ucrtLib = Join-Path $env:MSYSTEM_PREFIX "lib"
} else {
    $ucrtBin = Join-Path $msysPath "ucrt64\bin"
    $ucrtShare = Join-Path $msysPath "ucrt64\share"
    $ucrtLib = Join-Path $msysPath "ucrt64\lib"
}

Write-Host "Creating staging directories in $distDir..." -ForegroundColor Cyan
New-Item -ItemType Directory -Force -Path $distDir | Out-Null
New-Item -ItemType Directory -Force -Path $backendsDir | Out-Null
New-Item -ItemType Directory -Force -Path $schemasDir | Out-Null
New-Item -ItemType Directory -Force -Path $iconsDir | Out-Null
New-Item -ItemType Directory -Force -Path $pixbufDir | Out-Null

Write-Host "Copying compiled binaries and backends..." -ForegroundColor Cyan
Copy-Item "build\shell\src\papers.exe" $binDir -Force
Copy-Item "build\libdocument\libppsdocument-4.0-6.dll" $binDir -Force
Copy-Item "build\libview\libppsview-4.0-5.dll" $binDir -Force
Copy-Item "build\libdocument\backend\*.dll" $backendsDir -Force
Copy-Item "build\libdocument\backend\*.papers-backend" $backendsDir -Force
Copy-Item "$ucrtBin\libstdc++-6.dll" $binDir -Force -ErrorAction SilentlyContinue
Copy-Item "$ucrtBin\libgcc_s_seh-1.dll" $binDir -Force -ErrorAction SilentlyContinue
Copy-Item "$ucrtBin\libwinpthread-1.dll" $binDir -Force -ErrorAction SilentlyContinue
Write-Host "Copying and compiling GSettings schemas..." -ForegroundColor Cyan
if (Test-Path (Join-Path $ucrtShare "glib-2.0\schemas")) {
    Copy-Item (Join-Path $ucrtShare "glib-2.0\schemas\*.xml") $schemasDir -Force -ErrorAction SilentlyContinue
}
if (Test-Path "data\org.gnome.Papers.gschema.xml") {
    Copy-Item "data\org.gnome.Papers.gschema.xml" $schemasDir -Force
}
$glibCompileSchemas = Join-Path $ucrtBin "glib-compile-schemas.exe"
if (Test-Path $glibCompileSchemas) {
    & $glibCompileSchemas $schemasDir
} else {
    Copy-Item "build\data\gschemas.compiled" $schemasDir -Force
}

Write-Host "Resolving and copying DLL dependencies via ldd..." -ForegroundColor Cyan
$env:CHERE_INVOKING = "1"
$posixRoot = (& $bashExe -c "cygpath -u '$projectRoot'").Trim()
$lddCmd = "export PATH=/ucrt64/bin:/usr/bin:$posixRoot/build/libdocument:$posixRoot/build/libview:`$PATH && shopt -s nullglob && ldd '$posixRoot/build/shell/src/papers.exe' '$posixRoot'/build/libdocument/backend/*.dll"
$lddOutput = & $bashExe -c $lddCmd

$copiedCount = 0
foreach ($line in $lddOutput) {
    if ($line -match '(?i)([a-z0-9_\-\.]+\.dll)') {
        $dllName = $Matches[1]
        $srcPath = Join-Path $ucrtBin $dllName
        $dstPath = Join-Path $binDir $dllName
        if ((Test-Path $srcPath) -and !(Test-Path $dstPath)) {
            Copy-Item $srcPath $binDir -Force
            $copiedCount++
        }
    }
}

# Copy delay-loaded GTK4 runtime DLLs missed by static ldd
$extraDlls = @("vulkan-1.dll", "libvulkan-1.dll")
foreach ($dll in $extraDlls) {
    $srcPath = Join-Path $ucrtBin $dll
    $dstPath = Join-Path $binDir $dll
    if ((Test-Path $srcPath) -and !(Test-Path $dstPath)) {
        Copy-Item $srcPath $binDir -Force
        Write-Host "  + Copied extra runtime DLL: $dll" -ForegroundColor Green
    }
}

$totalDlls = (Get-ChildItem $binDir -Filter "*.dll").Count
if ($totalDlls -lt 10) {
    throw "Fatal error during bundling: No DLL dependencies were resolved by ldd! (Only $totalDlls DLLs in bin)"
} else {
    Write-Host "Successfully resolved and bundled $totalDlls total DLL dependencies in bin." -ForegroundColor Green
}

Write-Host "Copying translation catalogs (.mo) and UI assets..." -ForegroundColor Cyan
$targetLocaleDir = Join-Path $distDir "share\locale"
New-Item -ItemType Directory -Force -Path $targetLocaleDir | Out-Null
if (Test-Path "build\po") {
    Get-ChildItem -Path "build\po" -Filter "*.mo" -Recurse | ForEach-Object {
        $lang = $_.Directory.Parent.Name
        if ($lang -and $lang -ne "po") {
            $langDir = Join-Path $targetLocaleDir "$lang\LC_MESSAGES"
            New-Item -ItemType Directory -Force -Path $langDir | Out-Null
            Copy-Item $_.FullName (Join-Path $langDir "papers.mo") -Force
        }
    }
}
if (Test-Path (Join-Path $ucrtShare "locale")) {
    Copy-Item -Path (Join-Path $ucrtShare "locale\*") -Destination $targetLocaleDir -Recurse -Container -Force -ErrorAction SilentlyContinue
}
if (Test-Path (Join-Path $ucrtShare "icons\Adwaita")) {
    Copy-Item -Path (Join-Path $ucrtShare "icons\Adwaita") -Destination $iconsDir -Recurse -Container -Force
}
if (Test-Path (Join-Path $ucrtShare "icons\hicolor")) {
    Copy-Item -Path (Join-Path $ucrtShare "icons\hicolor") -Destination $iconsDir -Recurse -Container -Force
}
if (Test-Path (Join-Path $ucrtShare "mime")) {
    Copy-Item -Path (Join-Path $ucrtShare "mime") -Destination (Join-Path $distDir "share") -Recurse -Container -Force
}
if (Test-Path (Join-Path $ucrtShare "fontconfig")) {
    Copy-Item -Path (Join-Path $ucrtShare "fontconfig") -Destination (Join-Path $distDir "share") -Recurse -Container -Force
}
if (Test-Path (Join-Path $ucrtShare "poppler")) {
    Copy-Item -Path (Join-Path $ucrtShare "poppler") -Destination (Join-Path $distDir "share") -Recurse -Container -Force
}
if (Test-Path (Join-Path $ucrtLib "gdk-pixbuf-2.0")) {
    Copy-Item -Path (Join-Path $ucrtLib "gdk-pixbuf-2.0\*") -Destination $pixbufDir -Recurse -Container -Force
}

Write-Host ""
Write-Host "==========================================================" -ForegroundColor Green
Write-Host "   Bundling complete! Staged application is in:" -ForegroundColor Green
Write-Host "   $distDir" -ForegroundColor Yellow
Write-Host "==========================================================" -ForegroundColor Green
