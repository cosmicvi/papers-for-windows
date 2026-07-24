# Windows Porting Documentation: GNOME Papers

This document provides a comprehensive overview of the porting design, architecture, build requirements, and runtime instructions for running the GNOME Papers application natively on Windows.

---

## 1. Architectural Strategy

The Windows port is achieved through standard cross-platform GTK4 libraries and conditional compilation, minimizing direct changes to the platform-independent core logic.

### 1.1 Gating Unix-Specific Features
- **Keyring/Secrets Service**: Disabled on Windows since the D-Bus secret service provider (`oo7`) is Linux-only.
- **Nautilus Extension**: Disabled on Windows since Nautilus is a Linux-only file manager.
- **File Descriptors**: Unix file descriptor APIs (`fcntl` / `F_DUPFD_CLOEXEC` / `poppler_document_new_from_fd`) are gated behind `#ifndef G_OS_WIN32` blocks in:
  - `previewer/pps-previewer-window.c`
  - `previewer/pps-previewer.c`
  - `libview/pps-jobs.c`
  - `libdocument/backend/pdf/pps-poppler.c`

---

## 2. Build Requirements & Environment

The Windows version is built using **MSYS2** under the **UCRT64** environment.

### 2.1 MSYS2 Package Installation
Install the necessary compilation tools and Rust package manager:
```bash
# Install rust-gnu toolchain targeting UCRT64
pacman -S --noconfirm mingw-w64-ucrt-x86_64-rust

# Install standard UNIX diffutils (required by gettext-sys build scripts)
pacman -S --noconfirm diffutils
```

---

## 3. Compilation Guide

To configure and compile the project under MSYS2 UCRT64, execute the following commands:

```bash
# Prepends ucrt64 path for dependencies resolution
export PATH=/ucrt64/bin:$PATH

# Setup or reconfigure the Meson build directory
meson setup build --reconfigure

# Compile all targets (C backends, Rust shell, thumbnailer, and previewer)
meson compile -C build
```

The compilation generates the following binaries in the `build/` directory:
- `build/shell/src/papers.exe` (Main GUI Application)
- `build/thumbnailer/release/papers-thumbnailer.exe` (Thumbnailer)
- Backend libraries: `build/libdocument/backend/libpdfdocument.dll`, etc.

---

## 4. Execution & Environment Variables

To run the compiled `papers.exe` binary directly from the build directory, specify the runtime environment paths for schemas, backends, and DLL libraries:

```bash
# 1. Add DLL library dependencies to system PATH
export PATH=/ucrt64/bin:build/libdocument:build/libview:$PATH

# 2. Configure GSettings compiled schemas path
export GSETTINGS_SCHEMA_DIR=build/data

# 3. Configure Papers Document Backends path
export PPS_BACKENDS_DIR=build/libdocument/backend

# 4. Launch the application
build/shell/src/papers.exe
```
