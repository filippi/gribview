"""Launch the bundled Gribview desktop application."""
from pathlib import Path
import subprocess
import sys


def main():
    binary = Path(__file__).resolve().parent / "runtime/bin/gribview"
    if sys.platform == "win32":
        binary = binary.with_suffix(".exe")
    try:
        return subprocess.call([str(binary), *sys.argv[1:]])
    except OSError as error:
        print(f"Cannot start Gribview: {error}", file=sys.stderr)
        return 1
