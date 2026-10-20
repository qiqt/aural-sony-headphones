# Building Aural (Windows)

`appAural` is the Qt Quick client in `Client/qt`. It reuses the plain C++
engine directly under `Client/` (protocol, framing, listener, Windows
Bluetooth transport). The original ImGui client (`Client/CMakeLists.txt`)
is a separate entry point and is not built by these steps.

## Verified toolchain

These are the versions this build was last configured and compiled with on
Windows 11 (2026-10-20):

| Component | Required | Verified with |
|---|---|---|
| CMake | >= 3.22 with controller tests; 3.21 without | 4.4.4 |
| Compiler | MSVC v143 (x64) | cl 19.44.35229 (VS 2022 Build Tools) |
| Qt | >= 6.5 | 6.8.3 `msvc2022_64` |
| Qt modules | Core, Gui, Qml, Quick, QuickControls2, Svg | same |
| C++ standard | C++20 | same |

`Qt6::Widgets`, `Svg` and `QuickControls2` are hard requirements of the CMake file even
though the current QML uses an icon font rather than SVG assets; a Qt
installation without them fails at `find_package`.
The default controller test build also requires `Qt6::Test`.

The commands below use `C:/Qt` as an example installation path. Substitute your
actual Qt location. The October verification used
`C:/Users/User/.cache/aural-qt/6.8.3/msvc2022_64` and fresh
`build/verify-20261020-engine` / `build/verify-20261020-qt` output directories.

## Configure and build

Run from the repository root. The build directory is deliberately outside
`Client/qt` and is named `build/...` so it is covered by `.gitignore`.

```powershell
cmake -S Client/qt -B build/aural-debug -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64"
cmake --build build/aural-debug --config Debug
```

Result: `build/aural-debug/Debug/appAural.exe`.

For a release build use a separate directory and `--config Release`:

```powershell
cmake -S Client/qt -B build/aural-release -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64"
cmake --build build/aural-release --config Release
```

Debug keeps a console window so `qDebug()` output is visible. Release is a
GUI-subsystem binary, so no terminal appears behind the window.

## Running from the build tree

The executable needs the Qt runtime. Either put the Qt `bin` directory on
`PATH` for that shell:

```powershell
$env:PATH = "C:/Qt/6.8.3/msvc2022_64/bin;$env:PATH"
./build/aural-debug/Debug/appAural.exe
```

or deploy a self-contained copy (see "Packaging" below).

### Demo mode

Setting `AURAL_DEMO` fakes a connected WH-1000XM4 with sample settings so
every screen can be inspected without hardware. Any value enables it,
including `0`.

```powershell
$env:AURAL_DEMO = '1'
./build/aural-debug/Debug/appAural.exe
Remove-Item Env:AURAL_DEMO
```

Demo mode short-circuits the pump: it exercises the QML and the controller's
UI state only. It does not test Bluetooth, the command queue or ACK handling.

`AURAL_DEMO_DEVICE` sets the name the demo pretends to be connected to.
Anything that is not a known product code (e.g. `"Leon's Headphones"`)
previews the conservative control surface an unrecognised device gets - the
only way to see that path without hardware.

```powershell
$env:AURAL_DEMO = "1"; $env:AURAL_DEMO_DEVICE = "Leon's Headphones"
./build/aural-debug/Debug/appAural.exe
```

## Tests

The engine tests build without Qt and run under CTest:

```powershell
cmake -S Client/tests -B build/tests -G "Visual Studio 17 2022" -A x64
cmake --build build/tests --config Debug
ctest --test-dir build/tests -C Debug --output-on-failure
```

They drive the engine through `FakeBluetoothConnector`, a scripted
in-memory transport (`Client/tests/FakeBluetoothConnector.h`), so ACK
timing, remote closure and reconnect are deterministic and do not depend on
wall-clock sleeps.

A second suite covers `HeadphonesController`, which is where sessions, the
teardown order and the capability gating actually live. It needs a Qt event
loop, so it is built as part of the Qt project and run from that build
directory:

```powershell
ctest --test-dir build/aural-debug -C Debug --output-on-failure
```

It builds by default; pass `-DAURAL_BUILD_TESTS=OFF` at configure time to skip
it. The test binary is `controller_tests.exe` and can be run directly with Qt
on `PATH` (CTest prepends it for you).

## QML checks

`qmllint` runs over the whole module. It needs the built module on its import
path, so build first:

```powershell
$files = Get-ChildItem Client/qt/qml/*.qml | ForEach-Object { $_.FullName }
& "C:/Qt/6.8.3/msvc2022_64/bin/qmllint.exe" -I build/aural-debug -W 0 @files
```

`-W 0` means "fail on any warning". The equivalent through CMake is
`--target all_qmllint`, which prints the same diagnostics but does not fail
the build.

`Client/qt/qml/.qmllint.ini` disables the `UnqualifiedAccess` category. `Hp`,
`App` and `Platform` are C++ context properties, which qmllint cannot see at
all, so every use of them was reported - about 160 false positives that hid
the real findings. Everything else is on, and currently clean.

## Packaging

`windeployqt` copies the Qt runtime, QML plugins and platform plugins next
to the executable:

```powershell
cmake --build build/aural-release --config Release --target aural_package
```

The staging directory is `build/aural-release/package/Aural`. See
`packaging/README.md` for what is checked and what still needs a clean
machine.

## Build identity

The configure step stamps the git commit, branch and working-tree state into
the binary. `Aural` prints it at startup, About shows it, and `BUILD-INFO.txt`
in a staged package carries it. A dirty tree is reported as `modified`, and a
build with no `.git` reports `unknown` rather than guessing. See
`packaging/README.md`.

## Moving the checkout

CMake caches absolute source paths. After moving the checkout, configure
a new build directory instead of reusing a cache from its old location.

