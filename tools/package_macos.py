#!/usr/bin/env python3
"""Package an Apple Silicon app, optionally Developer-ID sign and notarize it."""
import argparse
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import tempfile
from bundle_runtime import stage, run


def package(build, dist):
    version = (build / "version.txt").read_text().strip()
    if run("lipo", "-archs", str(build / "bin/gribview")) != "arm64":
        raise RuntimeError("The macOS release must be Apple Silicon (arm64) only")
    dist.mkdir(parents=True, exist_ok=True)
    app = dist / "Gribview.app"
    if app.exists():
        raise RuntimeError(f"{app} already exists; use a fresh DIST_DIR")
    identity = os.environ.get("CODESIGN_IDENTITY", "-")
    notary = os.environ.get("NOTARY_PROFILE")
    with tempfile.TemporaryDirectory(prefix="gribview-macos-") as directory:
        temp = Path(directory)
        runtime = temp / "runtime"
        stage(build, runtime)
        contents = app / "Contents"
        contents.mkdir(parents=True)
        shutil.move(runtime / "bin", contents / "MacOS")
        shutil.move(runtime / "lib", contents / "lib")
        shutil.move(runtime / "share/gribview", contents / "Resources")
        shutil.move(runtime / "share/licenses", contents / "Resources/licenses")
        shutil.copy2(runtime / "runtime-manifest.json", contents / "Resources/runtime-manifest.json")
        iconset = temp / "Gribview.iconset"
        iconset.mkdir()
        for size in (16, 32, 128, 256, 512):
            for scale in (1, 2):
                name = f"icon_{size}x{size}" + ("@2x" if scale == 2 else "") + ".png"
                subprocess.run(["sips", "-z", str(size * scale), str(size * scale), str(contents / "Resources/icon.png"), "--out", str(iconset / name)], check=True, stdout=subprocess.DEVNULL)
        subprocess.run(["iconutil", "-c", "icns", str(iconset), "-o", str(contents / "Resources/Gribview.icns")], check=True)
        info = {"CFBundleName": "Gribview", "CFBundleDisplayName": "Gribview",
                "CFBundleExecutable": "gribview", "CFBundleIdentifier": "org.firecaster.gribview",
                "CFBundleVersion": version, "CFBundleShortVersionString": version,
                "CFBundlePackageType": "APPL", "NSHighResolutionCapable": True,
                "LSMinimumSystemVersion": "15.0", "CFBundleIconFile": "Gribview",
                "LSApplicationCategoryType": "public.app-category.weather",
                "CFBundleDocumentTypes": [{"CFBundleTypeName": "GRIB weather data", "CFBundleTypeRole": "Viewer",
                    "LSHandlerRank": "Alternate", "CFBundleTypeExtensions": ["grib", "grib2", "grb", "grb2"]}]}
        with (contents / "Info.plist").open("wb") as stream:
            plistlib.dump(info, stream)
        for binary in [*(contents / "lib").iterdir(), contents / "MacOS/gribview"]:
            if run("lipo", "-archs", str(binary)) != "arm64":
                raise RuntimeError(f"Unexpected architecture: {binary}")
            command = ["codesign", "--force", "--sign", identity]
            if identity != "-":
                command += ["--options", "runtime", "--timestamp"]
            subprocess.run([*command, str(binary)], check=True)
        subprocess.run([*command, str(app)], check=True)
        subprocess.run(["codesign", "--verify", "--deep", "--strict", str(app)], check=True)
        subprocess.run([str(contents / "MacOS/gribview"), "--check", str(contents / "Resources/sample.grib")], check=True, cwd="/")
        image_root = temp / "dmg"
        image_root.mkdir()
        shutil.copytree(app, image_root / app.name)
        (image_root / "Applications").symlink_to("/Applications")
        suffix = "" if identity != "-" and notary else "-unsigned"
        dmg = dist / f"Gribview-{version}-macOS-arm64{suffix}.dmg"
        subprocess.run(["hdiutil", "create", "-quiet", "-fs", "HFS+", "-format", "UDZO", "-volname", "Gribview", "-srcfolder", str(image_root), str(dmg)], check=True)
        if notary:
            if identity == "-":
                raise RuntimeError("Notarization requires CODESIGN_IDENTITY")
            subprocess.run(["xcrun", "notarytool", "submit", str(dmg), "--keychain-profile", notary, "--wait"], check=True)
            subprocess.run(["xcrun", "stapler", "staple", str(dmg)], check=True)
        print(dmg)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--dist", type=Path, required=True)
    args = parser.parse_args()
    package(args.build.resolve(), args.dist.resolve())
