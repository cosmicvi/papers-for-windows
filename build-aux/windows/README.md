# Building and Packaging GNOME Papers on Windows (MSYS2 UCRT64)

This guide describes how to configure, compile, run, and package GNOME Papers natively on Windows using the MSYS2 UCRT64 environment.

---

## 1. Prerequisites & Automated Setup

We provide an automated PowerShell script to install MSYS2 and configure all UCRT64 library dependencies:

```powershell
Set-ExecutionPolicy Bypass -Scope Process
.\build-aux\windows\auto-setup.ps1
```

### Manual Dependency Installation
Alternatively, in an MSYS2 UCRT64 terminal, run:

```bash
pacman -S --noconfirm --needed \
  mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-meson \
  mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-rust \
  mingw-w64-ucrt-x86_64-gtk4 \
  mingw-w64-ucrt-x86_64-libadwaita \
  mingw-w64-ucrt-x86_64-poppler-glib \
  mingw-w64-ucrt-x86_64-libspelling \
  mingw-w64-ucrt-x86_64-blueprint-compiler \
  mingw-w64-ucrt-x86_64-gettext \
  mingw-w64-ucrt-x86_64-appstream \
  mingw-w64-ucrt-x86_64-djvulibre \
  mingw-w64-ucrt-x86_64-libarchive \
  mingw-w64-ucrt-x86_64-libtiff \
  mingw-w64-ucrt-x86_64-pkgconf \
  diffutils
```

---

## 2. Configuration & Compilation

In your UCRT64 shell:

```bash
export PATH=/ucrt64/bin:$PATH

# Setup build directory
meson setup build --buildtype=release --strip -Ddocumentation=false -Duser_doc=false -Dnautilus=false -Dkeyring=disabled --reconfigure

# Compile targets
meson compile -C build
```

This compiles:
- Application binary: `build/shell/src/papers.exe`
- Previewer binary: `build/previewer/papers-previewer.exe`
- Thumbnailer binary: `build/thumbnailer/release/papers-thumbnailer.exe`
- Document backend plugins: `build/libdocument/backend/*.dll`

---

## 3. Packaging & Staging

To stage a standalone portable release directory (`dist/`) containing the executables, DLL dependencies, backends, GSettings schemas, and MIME database:

```powershell
powershell -ExecutionPolicy Bypass -File .\build-aux\windows\bundle.ps1
```

### Creating Windows Installer (.exe)
To generate the NSIS installer:

```bash
makensis build-aux/windows/papers.nsi
```

---

## 4. Running the Application Locally

```bash
export PATH=/ucrt64/bin:build/libdocument:build/libview:$PATH
export GSETTINGS_SCHEMA_DIR=build/data
export PPS_BACKENDS_DIR=build/libdocument/backend

build/shell/src/papers.exe
```
