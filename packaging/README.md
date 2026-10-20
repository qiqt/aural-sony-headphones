# Packaging Aural for Windows

For the current verification results and remaining gaps, see
[`VERIFICATION.md`](../VERIFICATION.md). The inventory and launch evidence
below describe the September package; the October minimal Qt installation
does not include the software-OpenGL runtime.

`aural_package` stages a self-contained folder that runs on a machine with
no Qt installed. It stages; it does not publish. Producing a GitHub release,
signing anything, or writing an installer are separate, deliberate steps.

## Building the staging folder

```powershell
cmake -S Client/qt -B build/aural-release -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64"
cmake --build build/aural-release --config Release --target aural_package
```

Result: `build/aural-release/package/Aural`.

The target copies `appAural.exe` next to a fresh, empty folder and then runs
`windeployqt --qmldir Client/qt/qml --no-translations` against it, stages the
MSVC runtime, and drops in `BUILD-INFO.txt`, `LICENSE.txt` and
`THIRD-PARTY-NOTICES.md`. Both the copy and the deploy run from scratch every
time, so the folder never carries stale DLLs from an earlier Qt.

The MSVC runtime comes from CMake's `InstallRequiredSystemLibraries`, not from
`windeployqt --compiler-runtime`. That flag only finds the redistributable when
`VCINSTALLDIR` / `VCToolsRedistDir` are set - i.e. from a Visual Studio
developer prompt. Run from a plain shell it silently copies nothing, which is
how an earlier version of this folder ended up quietly requiring the VC++
redistributable on the target machine. CMake already knows where the DLLs for
its own compiler live, so it is asked instead, and the target warns loudly if
it finds none.

`windeployqt` is invoked directly rather than through
`qt_generate_deploy_qml_app_script()`: that helper writes the executable path
into a generated CMake script without quoting it, which fails on any source
tree whose path contains spaces.

## Build identity

The configure step reads the current commit, branch and working-tree state out
of git and bakes them into the binary (`AURAL_BUILD_REVISION` and friends).
They show up in three places:

- the About screen, under the version number
- `qDebug()` on startup: `[Aural] build: 2.1.0 (d280814daf on multi-device, modified)`
- `BUILD-INFO.txt` in the staged folder

`modified` means the working tree had uncommitted changes when CMake ran, so
the revision does not describe the binary exactly - a release candidate should
be built from a clean tree. CMake re-runs automatically when `.git/HEAD` or
`.git/index` changes, so the stamp does not go stale between commits. A build
from a source archive with no `.git` honestly reports `unknown`.

## What ends up in the folder

Roughly 89 MB, 152 files:

- `appAural.exe` - Release build, `WIN32_EXECUTABLE` (no console window;
  Debug builds keep the console so `qDebug()` stays visible)
- Qt 6 runtime DLLs: Core, Gui, Widgets, Qml, Quick, QuickControls2 (+ Basic
  style and impl), QuickLayouts, QuickShapes, QuickTemplates2, Svg, Network,
  OpenGL
- `platforms/qwindows.dll`, `styles/`, `imageformats/`, `iconengines/`,
  `tls/`, `networkinformation/`, `generic/`
- `qml/` - the Qt QML modules the app imports
- `opengl32sw.dll`, `D3Dcompiler_47.dll`, `dxcompiler.dll`, `dxil.dll` -
  software/OpenGL fallback for machines without a usable GPU driver
- MSVC runtime: `concrt140.dll`, `msvcp140.dll`, `msvcp140_1.dll`,
  `msvcp140_2.dll`, `msvcp140_atomic_wait.dll`, `msvcp140_codecvt_ids.dll`,
  `vcruntime140.dll`, `vcruntime140_1.dll` - so the folder does not silently
  require the Visual C++ 2015-2022 x64 redistributable
- `BUILD-INFO.txt`, `LICENSE.txt`, `THIRD-PARTY-NOTICES.md`

Fonts are *not* in this list on purpose: Manrope, Space Grotesk and Material
Icons Outlined are compiled into the executable as Qt resources
(`qt_add_resources` in `Client/qt/CMakeLists.txt`), so the UI cannot pick up
a system substitute. They are still covered by `THIRD-PARTY-NOTICES.md`.

`qmltooling/` is the QML debugger plugin set. It is harmless and unused at
runtime; strip it if folder size matters.

## What has been verified

On this machine, from the staged folder copied outside the build tree
(`%TEMP%\...\pkgrun`) and launched with `PATH` scrubbed to
`C:\Windows\system32;C:\Windows;...` and `QT_PLUGIN_PATH` /
`QML_IMPORT_PATH` / `QML2_IMPORT_PATH` removed from the environment:

- the app starts, no missing-DLL dialog
- the QML window loads and renders with the bundled fonts
- demo mode (`AURAL_DEMO=1`) drives every screen

That proves the folder does not depend on the Qt install or the build tree
*on a machine that has already built Qt applications*.

## What still needs a clean machine

Not yet done - it needs a second Windows box, or a fresh VM:

1. Copy the folder to a Windows 10/11 machine that has never had Qt, Visual
   Studio, or the Windows SDK installed, and launch it.
2. Confirm no missing-DLL dialog. The MSVC runtime is now bundled, so this
   should pass without the redistributable installed - but bundling it is not
   the same as having tested it on a machine that never had it.
3. Confirm the app renders on a machine with no discrete GPU driver (the
   `opengl32sw.dll` fallback path).
4. Confirm SmartScreen behaviour for an unsigned binary and decide whether
   signing is required before any release.
5. Run discovery, connect and disconnect once on that machine, with real
   headphones - see below.

Until 1-5 are done this is a candidate, not a release.

## Hardware validation still outstanding

Everything in this repository has been exercised against a simulated
transport or demo mode. No part of the packaged build has been run against
real headphones. Pairing, connecting, and every device command remain
unverified on hardware.
