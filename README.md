# Aural — Sony headphones for Windows

Control Sony Bluetooth headphones from your PC: noise cancelling, ambient
sound, equalizer, Speak-to-Chat, multipoint connections and device settings.
Available controls depend on the connected headphones.

Aural is a Windows desktop app built with C++ and Qt Quick. Pair your
headphones in Windows Bluetooth settings, open Aural and select **Connect**.

## Device support

- **WH-1000XM4:** existing controls have been manually tested by the owner.
- **Other Sony models:** support is in development. XM3, XM5 and XM6 have
  not been validated on hardware; protocol tests alone do not confirm support.

## Build and run

Requires Windows, Visual Studio 2022 C++ build tools, CMake 3.22 or later,
and Qt 6.5 or later with Widgets, Quick, QuickControls2, Svg and Test.
Qt 6.8.3 is used in CI.

```powershell
git clone https://github.com/qiqt/aural-sony-headphones.git
cd aural-sony-headphones
cmake -S Client/qt -B build/aural -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64"
cmake --build build/aural --config Release
$env:PATH = "C:/Qt/6.8.3/msvc2022_64/bin;$env:PATH"
./build/aural/Release/appAural.exe
```

Change the Qt path to match your installation. See [BUILD.md](BUILD.md)
for tests, diagnostics and demo mode, and [packaging](packaging/README.md)
for a portable build.

For bug reports, include the headphone model, firmware, Aural build and
steps to reproduce. **System → Export diagnostics** saves a log; remove
Bluetooth names and addresses before sharing it.

[MIT license](LICENSE). [Third-party notices](packaging/THIRD-PARTY-NOTICES.md).
Aural is not affiliated with Sony.

## Credits

Inspired by [SonyHeadphonesClient](https://github.com/Plutoberth/SonyHeadphonesClient).
