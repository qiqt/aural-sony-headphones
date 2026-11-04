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

The portable build still needs testing on a clean Windows machine and with
software rendering. The local Qt installation lacks `opengl32sw.dll`.
See [PROTOCOL.md](../PROTOCOL.md) for the XM4 test results.

The output is an unsigned portable folder, ready to zip and share.
