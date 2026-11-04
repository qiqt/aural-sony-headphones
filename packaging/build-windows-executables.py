"""Wrap an existing portable ZIP without rebuilding or changing its app files."""

import argparse
import hashlib
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--makensis", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    compiler = args.makensis.resolve(strict=True)
    archive = args.archive.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    source = Path(__file__).resolve().parent
    version = subprocess.check_output([str(compiler), "/VERSION"], text=True).strip()
    if version != "v3.13":
        raise ValueError(f"Expected NSIS v3.13, got {version}")

    with tempfile.TemporaryDirectory(prefix="aural-package-", dir=output) as temp:
        stage = Path(temp)
        with zipfile.ZipFile(archive) as bundle:
            for entry in bundle.infolist():
                target = (stage / entry.filename).resolve()
                if not target.is_relative_to(stage) or not entry.filename.startswith("Aural/"):
                    raise ValueError(f"Unexpected archive path: {entry.filename}")
            if bundle.testzip() is not None:
                raise ValueError("Corrupt ZIP")
            bundle.extractall(stage)
        package = stage / "Aural"
        info = (package / "BUILD-INFO.txt").read_text()
        match = re.search(r"^Aural (\d+\.\d+\.\d+)\s*$", info, re.MULTILINE)
        if not match or not re.search(r"^Working tree\s*:\s*clean\s*$", info, re.MULTILINE):
            raise ValueError("The ZIP must contain a versioned build from a clean commit")
        app_version = match[1]
        for name in ("appAural.exe", "LICENSE.txt", "THIRD-PARTY-NOTICES.md",
                     "licenses/LGPL-3.0-only.txt", "licenses/GPL-3.0-only.txt"):
            if not (package / name).is_file():
                raise ValueError(f"Missing package file: {name}")
        shutil.copyfile(source / "licenses/NSIS-zlib.txt", package / "licenses/NSIS-zlib.txt")

        # Delete only packaged files on uninstall; leave user-created files in place.
        def quoted_path(path):
            relative = str(path.relative_to(package)).replace("/", "\\")
            return relative.replace("$", "$$").replace('"', '$\\"')

        lines = [f'Delete "$INSTDIR\\{quoted_path(p)}"'
                 for p in sorted(package.rglob("*")) if p.is_file()]
        directories = sorted((p for p in package.rglob("*") if p.is_dir()),
                             key=lambda p: len(p.parts), reverse=True)
        lines += [f'RMDir "$INSTDIR\\{quoted_path(p)}"' for p in directories]
        manifest = stage / "uninstall-files.nsh"
        manifest.write_text("\n".join(lines) + "\n", encoding="utf-8-sig")

        for kind in ("Setup", "Portable"):
            exe = output / f"Aural-{app_version}-{kind}.exe"
            command = [str(compiler), "/V2", f"/DAPP_VERSION={app_version}",
                       f"/DPACKAGE_DIR={package}", f"/DOUTPUT_FILE={exe}",
                       f"/DUNINSTALL_MANIFEST={manifest}"]
            if kind == "Portable":
                command.append("/DPORTABLE")
            command.append(str(source / "windows-executable.nsi"))
            subprocess.run(command, check=True)
            digest = hashlib.sha256(exe.read_bytes()).hexdigest()
            exe.with_suffix(".exe.sha256").write_text(f"{digest}  {exe.name}\n")
            print(f"Built {exe.name}: {exe.stat().st_size} bytes, SHA256 {digest}")


if __name__ == "__main__":
    main()
