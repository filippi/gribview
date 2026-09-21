#!/usr/bin/env python3
"""Use the public bottle filenames recorded by Homebrew, not local filenames."""
import hashlib
import json
from pathlib import Path

for manifest in Path.cwd().glob("*.bottle.json"):
    for formula in json.loads(manifest.read_text()).values():
        for tag in formula["bottle"]["tags"].values():
            source = Path(tag["local_filename"])
            target = Path(tag["filename"])
            if source.name != str(source) or target.name != str(target):
                raise RuntimeError("Bottle filenames must not contain directories")
            if hashlib.sha256(source.read_bytes()).hexdigest() != tag["sha256"]:
                raise RuntimeError(f"Checksum mismatch: {source}")
            if source != target:
                if target.exists():
                    raise RuntimeError(f"Refusing to overwrite {target}")
                source.rename(target)
