# Building 7-Zip Qt on Linux

## Prerequisites

You need a C++17 compiler, **CMake**, **Ninja**, and the **Qt 6 base** development
packages (Core, Gui, Widgets, DBus).

| Distribution        | Install command                                                                 |
|---------------------|---------------------------------------------------------------------------------|
| Arch / Manjaro      | `sudo pacman -S --needed base-devel cmake ninja qt6-base`                        |
| Debian / Ubuntu     | `sudo apt install build-essential cmake ninja-build qt6-base-dev`                |
| Fedora              | `sudo dnf install gcc-c++ cmake ninja-build qt6-qtbase-devel`                    |
| openSUSE            | `sudo zypper install gcc-c++ cmake ninja qt6-base-devel`                         |

The project is built and tested with **GCC** and **Qt 6.11**; Qt 6.5 or newer should work.

## Build

From the repository root:

```sh
cmake -S . -B build -G Ninja
cmake --build build
```

This produces, under `build/`:

| Path                                  | What it is              |
|---------------------------------------|-------------------------|
| `build/CPP/7zip/UI/Qt/7zqt_fm`        | File Manager GUI        |
| `build/CPP/7zip/UI/Qt/7zqt`           | Standalone archiver GUI |
| `build/7zz`                           | Console 7-Zip (CLI)     |
| `build/CPP/7zip/UI/Qt/Lang/`          | Translation files (`*.txt`) staged next to the GUI |

## Run

```sh
./build/CPP/7zip/UI/Qt/7zqt_fm        # file manager
./build/CPP/7zip/UI/Qt/7zqt           # standalone archiver
./build/7zz                           # console tool
```

### Translations at runtime

The GUI looks for its language files in **`Lang/` next to the executable**
(`<exe-dir>/Lang/*.txt`), or in the directory given by the `7ZIP_LANG_DIR`
environment variable. The CMake build stages the `Lang/` corpus next to `7zqt_fm`
automatically. If you move the binary, copy the `Lang/` folder along with it (or set
`7ZIP_LANG_DIR`). With no language files found, the GUI falls back to English.

The interface language can be changed in the GUI under **Tools → Options → Language**;
on first run it auto-detects the OS locale.

## Notes

- The Qt GUI is **additive** and lives entirely under `CPP/7zip/UI/Qt/`. The only engine
  source changes are 6 `#ifdef _WIN32`-guarded write-path files (see the README); the
  rest of the upstream 7-Zip tree is compiled unmodified.
- Only **x86-64** is supported for the GUI: Qt 6 does not target 32-bit Linux.
