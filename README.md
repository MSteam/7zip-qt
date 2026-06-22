# 7-Zip Qt — Linux GUI port

A faithful **Qt 6 / Linux port of the 7-Zip File Manager and archiver**, built on top of
upstream **7-Zip 26.01** by Igor Pavlov ([7-zip.org](https://www.7-zip.org/)).

7-Zip's compression engine, command-line tool (`7zz`) and the `IFolder` abstraction were
already cross-platform; only the GUI was bound to the Win32 API. This fork adds a native
Qt GUI on Linux while keeping the engine essentially untouched.

> **Status:** feature-complete and usable on desktop Linux. The GUI mirrors the original
> Windows File Manager closely (panels, archive browsing, in-place archive editing,
> compress/extract dialogs, drag & drop, i18n, …). Windows-only features (NTFS alternate
> streams/ACLs, drive letters, shell context menus, SFX creation) are intentionally
> excluded.

---

## What you get

| Binary      | Description                                                                 |
|-------------|-----------------------------------------------------------------------------|
| `7zqt_fm`   | **File Manager** — two-panel browser for the filesystem *and* archives, with compress / extract / test / hash, in-place archive add/delete/rename, drag & drop in and out, per-type icons, and translations. |
| `7zqt`      | **Standalone archiver** — the compress / extract / test / hash dialogs without the file manager. |
| `7zz`       | The upstream **console 7-Zip** (unchanged), built from the same tree.        |

## File Manager features

- Two-panel navigation across the filesystem and **inside archives** (7z, zip, rar,
  tar, gz, xz, …) — including **nested archives** and **multi-volume** sets.
- **In-place archive editing**: add, delete, rename and **edit-writeback** of files
  inside an updatable archive; CRC / hash of in-archive items without extracting.
- **Compress** (level / method / dictionary / parameters / encryption / split / delete-after)
  and **Extract** (path mode / overwrite mode / split destination) dialogs.
- **Drag & drop**: drag files *out* of an archive to the desktop or another file manager
  (e.g. Dolphin) and *into* an archive; rubber-band selection; "add to archive" on drop.
- Encrypted archives end-to-end (open, extract, add into) with password prompts.
- View modes (Details / List / Large / Small), column chooser, flat view, folder history,
  auto-refresh, inline rename, properties (incl. multi-select aggregates and raw hashes).
- **Internationalisation** via 7-Zip's own language files (93 translations in `Lang/`),
  with OS-locale auto-detection.
- Benchmark, About, options, XDG Trash integration, dark-theme aware.

## How it is built (architecture)

The port is **additive**: all new code lives under [`CPP/7zip/UI/Qt/`](CPP/7zip/UI/Qt).
The engine is reused as-is, with the **only** source changes being **6 write-path files**
carrying minimal `#ifdef _WIN32`-guarded edits — the Windows build stays byte-identical:

```
CPP/7zip/UI/Agent/Agent.cpp
CPP/7zip/UI/Agent/AgentOut.cpp
CPP/7zip/UI/Agent/ArchiveFolderOut.cpp
CPP/7zip/UI/FileManager/FSFolder.cpp
CPP/7zip/UI/FileManager/FSFolder.h
CPP/7zip/UI/FileManager/FSFolderCopy.cpp
```

The build is driven by a root [`CMakeLists.txt`](CMakeLists.txt) (the upstream tree has no
CMake build of its own on Linux).

---

## Quick start

### Prebuilt binary (x86-64)

See the [**Releases**](https://github.com/MSteam/7zip-qt/releases) page for a ready-to-run
x86-64 Linux tarball and step-by-step install / file-association instructions.

> Only **64-bit (x86-64)** binaries are provided: Qt 6 no longer targets 32-bit Linux.

### Build from source

```sh
# Prerequisites: CMake, Ninja, a C++17 compiler, and Qt 6 base (Widgets).
cmake -S . -B build -G Ninja
cmake --build build

# Run the file manager:
./build/CPP/7zip/UI/Qt/7zqt_fm
```

Full per-distribution instructions are in [**BUILDING.md**](BUILDING.md).

---

## Requirements

- **Build:** CMake ≥ 3.16, Ninja, a C++17 compiler (GCC tested), and **Qt 6** base
  development packages (Core / Gui / Widgets / DBus). Built and tested with Qt 6.11.
- **Runtime:** the **Qt 6 base** shared libraries (`libQt6Widgets`, `libQt6Gui`,
  `libQt6Core`, `libQt6DBus`) and a runtime icon theme (e.g. Breeze, Adwaita).

---

## License

This project keeps the **same license as upstream 7-Zip**: the bulk is **GNU LGPL**, with
some BSD 3-clause code and the **unRAR license restriction** on the RAR decoder. Nothing
about the licensing is changed by this port. See:

- [`DOC/License.txt`](DOC/License.txt)
- [`DOC/copying.txt`](DOC/copying.txt) (LGPL)
- [`DOC/unRarLicense.txt`](DOC/unRarLicense.txt)

## Credits

- **7-Zip** © 1999–2026 **Igor Pavlov** — <https://www.7-zip.org/>. All compression code,
  the engine, and `7zz` are his work.
- **Qt/Linux GUI port** — this fork. The port adds the Qt presentation layer and a CMake
  build; it does not alter the archive formats or compression behaviour.
