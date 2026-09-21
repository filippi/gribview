#!/usr/bin/env python3
"""Generate a pip-compatible wheel index for the GitHub Pages download site."""
import argparse
import hashlib
import html
from pathlib import Path
from urllib.parse import quote

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--version", required=True)
parser.add_argument("--artifacts", required=True, type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1] / "docs/wheels"
package = root / "gribview"
package.mkdir(parents=True, exist_ok=True)
wheels = sorted(args.artifacts.glob(f"gribview-{args.version}-*.whl"))
if not wheels:
    raise SystemExit("No release wheels found")
links = []
for wheel in wheels:
    digest = hashlib.sha256(wheel.read_bytes()).hexdigest()
    url = f"https://github.com/filippi/gribview/releases/download/v{args.version}/{quote(wheel.name)}#sha256={digest}"
    links.append(f'<a href="{html.escape(url)}" data-requires-python="&gt;=3.9">{html.escape(wheel.name)}</a><br>')
(package / "index.html").write_text('<!doctype html>\n<html><head><title>gribview wheels</title></head><body>\n' + '\n'.join(links) + '\n</body></html>\n')
(root / "index.html").write_text('<!doctype html>\n<html><head><title>Gribview packages</title></head><body><a href="gribview/">gribview</a></body></html>\n')
