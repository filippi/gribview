#!/usr/bin/env python3
"""Check an installed binary, GRIB decoding, error codes and rendered pixels."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def verify(binary, sample, screenshot=None):
    binary, sample = str(binary.resolve()), str(sample.resolve())
    def run(*args, success=True):
        result = subprocess.run([binary, *args], cwd="/", capture_output=True, text=True, timeout=60)
        if (result.returncode == 0) != success:
            raise RuntimeError(f"{args}: exit {result.returncode}\n{result.stdout}\n{result.stderr}")
        return result.stdout
    assert "gribview " in run("--version")
    assert "Usage:" in run("--help")
    assert "messages=2" in run("--check", sample)
    with tempfile.TemporaryDirectory(prefix="gribview test ") as directory:
        root = Path(directory)
        invalid = root / "invalid.grib"
        invalid.write_text("not GRIB")
        empty = root / "empty.grib"
        empty.touch()
        truncated = root / "truncated.grib"
        truncated.write_bytes(Path(sample).read_bytes()[:32])
        for file in (invalid, empty, truncated, root / "missing.grib"):
            run("--check", str(file), success=False)
        copied = root / "sample with spaces.grib"
        copied.write_bytes(Path(sample).read_bytes())
        assert "messages=2" in run("--check", str(copied))
    run("--unknown-option", success=False)
    if screenshot:
        from PIL import Image
        screenshot = screenshot.resolve()
        result = run("--smoke-test", sample, "--screenshot", str(screenshot))
        assert "messages=2" in result
        with Image.open(screenshot) as image:
            # The field occupies the canvas to the right of the controls.
            canvas = image.crop((image.width // 2, image.height // 4, image.width, image.height * 3 // 4)).convert("RGB")
            colorful = sum(max(pixel) - min(pixel) > 40 for pixel in canvas.getdata())
            assert colorful > canvas.width * canvas.height * 0.01, "No colored GRIB field in screenshot"
    print("Artifact checks passed:", binary)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("sample", type=Path)
    parser.add_argument("--screenshot", type=Path)
    args = parser.parse_args()
    verify(args.binary, args.sample, args.screenshot)
