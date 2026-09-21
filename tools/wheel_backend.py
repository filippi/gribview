"""PEP 517 backend for already-built, audited native application runtimes."""
import base64
import csv
import hashlib
import io
from pathlib import Path
import platform
import re
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def get_requires_for_build_wheel(config_settings=None):
    return []


def build_wheel(wheel_directory, config_settings=None, metadata_directory=None):
    settings = config_settings or {}
    if "runtime" not in settings:
        raise RuntimeError("Build a native runtime first, then pass --config-settings runtime=/absolute/staging/path")
    runtime = Path(settings["runtime"]).resolve()
    binary = runtime / "bin/gribview"
    if not binary.is_file() or not (runtime / "runtime-manifest.json").is_file():
        raise RuntimeError("runtime must be produced by tools/bundle_runtime.py")
    version = subprocess.check_output([str(binary), "--version"], text=True).split()[1]
    arch = platform.machine()
    if platform.system() == "Darwin":
        if arch != "arm64":
            raise RuntimeError("Only Apple Silicon macOS wheels are supported")
        tag = "macosx_15_0_arm64"
        for path in [binary, *(runtime / "lib").iterdir()]:
            output = subprocess.check_output(["otool", "-L", str(path)], text=True)
            for line in output.splitlines()[1:]:
                dep = line.strip().split(" (", 1)[0]
                if not dep.startswith(("@executable_path/../lib/", "/usr/lib/", "/System/")):
                    raise RuntimeError(f"Unbundled dependency: {dep}")
    elif platform.system() == "Linux":
        if arch not in ("x86_64", "aarch64"):
            raise RuntimeError(f"Unsupported Linux architecture: {arch}")
        # Every copied ELF is checked; do not infer compatibility from the host alone.
        for path in [binary, *(runtime / "lib").iterdir()]:
            versions = subprocess.check_output(["readelf", "--version-info", str(path)], text=True)
            required = [tuple(map(int, v.split("."))) for v in re.findall(r"GLIBC_(\d+\.\d+)", versions)]
            if required and max(required) > (2, 35):
                raise RuntimeError(f"{path.name} requires glibc newer than 2.35")
            output = subprocess.check_output(["ldd", str(path)], text=True)
            if "not found" in output:
                raise RuntimeError(output)
            for dep in re.findall(r"=>\s+(/\S+)", output):
                if not Path(dep).resolve().is_relative_to(runtime) and not re.match(r"lib(c|m|pthread|dl|rt|resolv|util|stdc\+\+|gcc_s)\.so", Path(dep).name):
                    raise RuntimeError(f"Unbundled dependency: {dep}")
        tag = f"manylinux_2_35_{arch}"
    else:
        raise RuntimeError("Wheel packaging currently supports macOS arm64 and Linux")
    filename = f"gribview-{version}-py3-none-{tag}.whl"
    info = f"gribview-{version}.dist-info"
    metadata = f"Metadata-Version: 2.1\nName: gribview\nVersion: {version}\nSummary: Desktop GRIB weather-data viewer\nRequires-Python: >=3.9\nLicense: Apache-2.0\nHome-page: https://github.com/filippi/gribview\n\nIncludes the native desktop app. Linux requires a graphical desktop and OpenGL 3.2.\n"
    records = []
    target = Path(wheel_directory) / filename
    target.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as wheel:
        def add(name, data, executable=False):
            entry = zipfile.ZipInfo(name)
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = (0o100755 if executable else 0o100644) << 16
            wheel.writestr(entry, data)
            digest = base64.urlsafe_b64encode(hashlib.sha256(data).digest()).rstrip(b"=").decode()
            records.append((name, f"sha256={digest}", len(data)))
        for path in sorted(runtime.rglob("*")):
            if path.is_file():
                add("gribview/runtime/" + path.relative_to(runtime).as_posix(), path.read_bytes(), bool(path.stat().st_mode & 0o111))
        for path in (ROOT / "python/gribview").glob("*.py"):
            add("gribview/" + path.name, path.read_bytes())
        add(f"{info}/METADATA", metadata.encode())
        add(f"{info}/WHEEL", f"Wheel-Version: 1.0\nGenerator: gribview\nRoot-Is-Purelib: false\nTag: py3-none-{tag}\n".encode())
        add(f"{info}/entry_points.txt", b"[console_scripts]\ngribview = gribview:main\n")
        add(f"{info}/LICENSE", (ROOT / "LICENSE").read_bytes())
        record = io.StringIO()
        writer = csv.writer(record, lineterminator="\n")
        writer.writerows([*records, (f"{info}/RECORD", "", "")])
        wheel.writestr(f"{info}/RECORD", record.getvalue())
    return filename


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    print(build_wheel(args.output, {"runtime": args.runtime}))
