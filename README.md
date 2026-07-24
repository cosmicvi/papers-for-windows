# ![papers-logo] Document Viewer

Papers is a modern document viewer capable of displaying multiple and single-page document formats like PDF, Comic Book Archives (CBR/CBZ), DjVu, and TIFF.

For more general information about Papers and how to get started, please visit [https://welcome.gnome.org/app/Papers](https://welcome.gnome.org/app/Papers).

## Installation

Papers is licensed under the [GPLv2][license].

[![flatpak]](https://flathub.org/apps/details/org.gnome.Papers)

---

## Windows Implementation & Building on Windows

Papers features native cross-platform support for Microsoft Windows (x86_64) using the MSYS2 UCRT64 toolchain and GTK4.

### Windows Architecture Highlights
1. **POSIX & Unix API Gating**: Replaced Unix-specific process execution (`glib::spawn_command_line_async`) with cross-platform process spawning (`std::process::Command`), and gated file descriptor operations behind Windows-safe preprocessor checks.
2. **Automated CI/CD Release Bundling**: Includes automated GitLab CI pipelines (`win32-ps` runners) generating both standalone portable archives and Modern UI setup installers (`.exe`).

### Building on Windows (MSYS2 UCRT64)

#### Automated Setup (Recommended)
An automated PowerShell setup script is provided in the repository root:

1. Open PowerShell as Administrator.
2. Run the environment setup script:
   ```powershell
   Set-ExecutionPolicy Bypass -Scope Process
   .\auto-setup.ps1
   ```
3. The script configures MSYS2 UCRT64, GCC, Meson, Ninja, Rust, GTK4, Libadwaita, Poppler, and all optional document backend libraries.

#### Manual Build Steps
In an MSYS2 UCRT64 terminal:

```bash
# Add UCRT64 binaries to PATH
export PATH=/ucrt64/bin:$PATH

# Setup build directory
meson setup build --buildtype=release --strip --reconfigure

# Compile all targets
meson compile -C build

# (Optional) Create standalone release bundle
powershell -ExecutionPolicy Bypass -File .\bundle.ps1
```

---

## Reporting and Development

If you experience issues with Papers, check out the [reporting tips](TESTING.md).
Developers should make sure to read the [contributing](CONTRIBUTING.md) guidelines before starting to work on any changes.

### Papers Requirements

* [GNOME Platform libraries][gnome]
* [Poppler for PDF viewing][poppler]

### Papers Optional Backend Libraries

* [DjVuLibre for DjVu viewing][djvulibre]
* [Archive library for Comic Book Resources (CBR) viewing][comics]
* [LibTiff for Multipage TIFF viewing][tiff]

[gnome]: https://www.gnome.org/
[poppler]: https://poppler.freedesktop.org/
[djvulibre]: https://djvu.sourceforge.net/
[comics]: https://libarchive.org/
[tiff]: https://libtiff.gitlab.io/libtiff/
[license]: COPYING
[papers-logo]: data/icons/scalable/apps/org.gnome.Papers.svg
[flatpak]: https://flathub.org/api/badge?svg&locale=en

## Documentation

The documentation for the libraries can be found online:

* [libppsview](https://gnome.pages.gitlab.gnome.org/papers/view/)
* [libppsdocument](https://gnome.pages.gitlab.gnome.org/papers/document/)

## Code of Conduct

When interacting with the project, the [GNOME Code Of Conduct](https://conduct.gnome.org/) applies.
