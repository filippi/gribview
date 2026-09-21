#!/usr/bin/env python3
"""Stage a relocatable runtime from a CMake build, failing on missing inputs."""
import argparse
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess


def run(*args):
    return subprocess.check_output(args, text=True).strip()


def copy_tree(source, destination):
    shutil.copytree(source, destination, dirs_exist_ok=True)


def linux_dependencies(binary):
    output = run("ldd", str(binary))
    if "not found" in output:
        raise RuntimeError(f"Unresolved libraries in {binary}:\n{output}")
    return [Path(match) for match in re.findall(r"(?:=>\s+|^\s*)(/[^\s]+)", output, re.M)]


def mac_dependencies(binary):
    return [line.strip().split(" (compatibility", 1)[0]
            for line in run("otool", "-L", str(binary)).splitlines()[1:]]


def mac_resolve(dep, original, executable):
    if dep.startswith("@loader_path/"):
        return original.parent / dep.removeprefix("@loader_path/")
    if dep.startswith("@executable_path/"):
        return executable.parent / dep.removeprefix("@executable_path/")
    if dep.startswith("@rpath/"):
        for owner in (original, executable):
            paths = re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset", run("otool", "-l", str(owner)))
            for path in paths:
                base = path.replace("@loader_path", str(owner.parent)).replace("@executable_path", str(executable.parent))
                candidate = Path(base) / dep.removeprefix("@rpath/")
                if candidate.is_file():
                    return candidate
        raise RuntimeError(f"Cannot resolve {dep} from {original}")
    return Path(dep)


def stage(build, output):
    if output.exists():
        raise RuntimeError(f"Output already exists: {output}; choose a new staging directory")
    executable = build / "bin/gribview"
    diagnostics = dict(line.split("=", 1) for line in run(str(executable), "--diagnostics").splitlines() if "=" in line)
    subprocess.run(["cmake", "--install", str(build), "--prefix", str(output)], check=True)
    libraries = output / "lib"
    resources = output / "share/gribview"
    libraries.mkdir()
    for kind in ("definitions", "samples"):
        locations = diagnostics[kind].strip('"').split(os.pathsep)
        sources = [Path(location) for location in locations if Path(location).is_dir()]
        if sources:
            for source in reversed(sources):
                copy_tree(source, resources / "eccodes" / kind)
        elif not any(location.startswith("/MEMFS/") for location in locations):
            raise RuntimeError(f"ecCodes {kind} is neither embedded nor available: {locations}")
    root = Path(__file__).resolve().parents[1]
    notices = output / "share/licenses/gribview"
    copy_tree(root / "packaging/licenses", notices)
    shutil.copy2(root / "external/imgui/LICENSE.txt", notices / "imgui.txt")
    shutil.copy2(root / "external/tinyfiledialogs/tinyfiledialogs.h", notices / "tinyfiledialogs.txt")
    shutil.copy2(root / "src/stb_image_write.h", notices / "stb_image_write.txt")
    # Traverse using original loader paths before modifying each staged binary.
    pending = [(executable, output / "bin/gribview")]
    copied = {}
    manifest = []
    mac = platform.system() == "Darwin"
    while pending:
        original, target = pending.pop()
        if mac:
            minimums = re.findall(r"\bminos (\d+)\.(\d+)", run("otool", "-l", str(original)))
            if any(tuple(map(int, version)) > (15, 0) for version in minimums):
                raise RuntimeError(f"{original} requires macOS newer than the release baseline 15.0")
            deps = []
            for dep in mac_dependencies(original):
                if dep.startswith(("/System/", "/usr/lib/")):
                    continue
                source = mac_resolve(dep, original, executable).resolve()
                if source == original.resolve():
                    continue
                deps.append((dep, source))
        else:
            deps = [(str(p), p.resolve()) for p in linux_dependencies(original)
                    if not re.match(r"(?:ld-linux|lib(?:c|m|pthread|dl|rt|resolv|util|stdc\+\+|gcc_s)\.so)", p.name)]
        for dep, source in deps:
            name = Path(dep).name
            if name in copied and copied[name] != source:
                raise RuntimeError(f"Conflicting libraries named {name}")
            dest = libraries / name
            if name not in copied:
                copied[name] = source
                shutil.copy2(source, dest)
                dest.chmod(0o755)
                pending.append((source, dest))
                manifest.append({"library": name, "source": str(source)})
                if mac:
                    prefix = source.parent.parent
                    for pattern in ("LICENSE*", "COPYING*", "share/doc/*/LICENSE*", "share/doc/*/COPYING*"):
                        for license_file in prefix.glob(pattern):
                            if license_file.is_file():
                                shutil.copy2(license_file, notices / f"{name}-{license_file.name}")
                if not mac:
                    result = subprocess.run(["dpkg-query", "-S", str(source)], text=True, capture_output=True)
                    if result.returncode:
                        result = subprocess.run(["dpkg-query", "-S", f"*/{source.name}"], text=True, capture_output=True)
                    for line in result.stdout.splitlines():
                        package = line.split(": ", 1)[0].split(":", 1)[0]
                        copyright_file = Path("/usr/share/doc") / package / "copyright"
                        if copyright_file.is_file():
                            shutil.copy2(copyright_file, notices / f"{package}.copyright")
            if mac:
                subprocess.run(["install_name_tool", "-change", dep, f"@executable_path/../lib/{name}", str(target)], check=True)
        if mac:
            if target.parent == libraries:
                subprocess.run(["install_name_tool", "-id", f"@executable_path/../lib/{target.name}", str(target)], check=True)
            paths = re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset", run("otool", "-l", str(target)))
            for path in paths:
                subprocess.run(["install_name_tool", "-delete_rpath", path, str(target)], check=True)
        else:
            subprocess.run(["patchelf", "--set-rpath", "$ORIGIN" if target.parent == libraries else "$ORIGIN/../lib", str(target)], check=True)
    if mac:
        for binary in [*libraries.iterdir(), output / "bin/gribview"]:
            subprocess.run(["codesign", "--force", "--sign", "-", str(binary)], check=True)
    (output / "runtime-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    subprocess.run([str(output / "bin/gribview"), "--check", str(resources / "sample.grib")], check=True, cwd="/")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    stage(args.build.resolve(), args.output.resolve())
