# Windows portable build

From the repository root, using your Qt installation path:

```powershell
cmake -S Client/qt -B build/aural-release -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64"
cmake --build build/aural-release --config Release --target aural_package
```

Output: `build/aural-release/package/Aural`. Run `appAural.exe` from that
folder. The target rebuilds the folder and copies the app, Qt libraries and
plugins, MSVC runtime, build information and license notices. Fonts are
embedded in the executable.

The `licenses` folder includes the full LGPLv3 and GPLv3 texts for Qt.

To create a ZIP containing that folder:

```powershell
cmake --build build/aural-release --config Release --target aural_archive
```

The ZIP is saved in `build/aural-release`. Its name includes the app version
and source revision. Extract it before running `Aural/appAural.exe`.

The build uses `windeployqt --no-translations` for Qt and CMake's
`InstallRequiredSystemLibraries` for the MSVC runtime. `windeployqt` is
called directly to support source paths containing spaces.

## Before distributing

Build from a clean commit. Check the revision in `BUILD-INFO.txt` against
that commit; `modified` indicates uncommitted changes at configure time.
An archive without Git metadata reports `unknown`.

The Aural CI workflow checks the required executable, DLLs, plugins and
notices. Also test the folder on a Windows machine without Qt or Visual
Studio installed:

1. Start the app and check for missing DLLs, fonts or rendering problems.
2. Pair headphones, connect, read settings and disconnect.
3. Test the controls intended for the release; record model and firmware.

The portable build passed a startup check on a Windows 11 VM without Qt or
Visual Studio installed, using the Qt Quick software backend. Interactive
layout and Bluetooth checks on that VM are still pending.
See [PROTOCOL.md](../PROTOCOL.md) for the XM4 test results.

The output is an unsigned portable folder, ready to zip and share.

## Versions

Aural starts at 0.1.0. Set the version in `Client/qt/CMakeLists.txt`; the app,
Windows file properties, build information and ZIP name all use that value.
Release tags use `v` followed by the same version, for example `v0.1.0`.

Use 0.1.1 for a fix to 0.1.0, and 0.2.0 for the next feature release.
Development builds keep the version and include their Git revision. Deleted
tags don't change the version compiled into the app.
