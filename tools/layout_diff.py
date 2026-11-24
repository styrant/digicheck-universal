"""Compares two layout.json files from `"Ultimate Audio Viewer.exe" --layout-check` (previous build, this build) and
prints a Markdown summary of the controls that moved, appeared or disappeared, per page and window size.
Usage: python tools/layout_diff.py previous.json current.json"""
import json, sys
from collections import defaultdict

def load(path):
    try:
        with open(path, encoding="utf-8") as f:
            items = json.load(f)
    except (OSError, ValueError):
        return None
    out = {}
    count = defaultdict(int)
    for it in items:
        key = (it["page"], it["path"])
        count[key] += 1
        out[(it["page"], it["path"], count[key])] = (it["x"], it["y"], it["w"], it["h"])
    return out

prev, cur = load(sys.argv[1]), load(sys.argv[2])
if cur is None:
    print("No layout check result in this build."); sys.exit(0)
if prev is None:
    print("No previous layout to compare with (first run)."); sys.exit(0)

changes = defaultdict(list)
for k, b in cur.items():
    if k not in prev: changes[k[0]].append(f"+ {k[1]} at {b}")
    elif prev[k] != b: changes[k[0]].append(f"~ {k[1]}: {prev[k]} -> {b}")
for k, b in prev.items():
    if k not in cur: changes[k[0]].append(f"- {k[1]} (was at {b})")

if not changes:
    print("Layout unchanged from the previous build."); sys.exit(0)
print(f"### Layout changes since the previous build ({sum(len(v) for v in changes.values())} controls on {len(changes)} page views)\n")
for page in sorted(changes):
    print(f"<details><summary>{page} ({len(changes[page])})</summary>\n\n```")
    for line in changes[page][:200]: print(line)
    print("```\n</details>\n")
