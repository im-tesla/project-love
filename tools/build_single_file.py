#!/usr/bin/env python3
"""Flatten web/ into one self-contained HTML file.

    python tools/build_single_file.py [output.html]

The deployed site is a directory of ES modules, which is the right shape for
nginx. But a single file is handy for sharing a preview, opening straight off
disk with no server, or emailing to someone who just wants to look at it.

The output runs against the mock matrix, since there is no device on the other
end of a preview link.

This is a flattener, not a bundler: modules are concatenated in dependency
order with their import/export syntax stripped, which only works because the
sources deliberately avoid name collisions across files. If you add a module,
add it to MODULES below in the order it needs to load.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WEB = ROOT / "web"

CSS = ["css/paper.css", "css/components.css", "css/intro.css"]

# Dependency order. font.js and protocol.js define things the rest reach for.
MODULES = [
    "js/font.js",
    "js/protocol.js",
    "js/animations.js",
    "js/preview.js",
    "js/ble.js",
    "js/mock-ble.js",
    "js/ui/intro.js",
    "js/ui/message.js",
    "js/ui/anim.js",
    "js/ui/draw.js",
    "js/ui/playlist.js",
    "js/ui/dials.js",
    "js/ui/night.js",
    "js/main.js",
]

IMPORT_RE = re.compile(r"^\s*import\b[\s\S]*?;\s*$", re.M)
REEXPORT_RE = re.compile(r"^\s*export\s*\{[^}]*\}\s*;\s*$", re.M)
EXPORT_PREFIX_RE = re.compile(r"^(\s*)export\s+(?=(const|let|var|function|class|async)\b)", re.M)


def strip_module_syntax(source: str, name: str) -> str:
    source = IMPORT_RE.sub("", source)
    source = REEXPORT_RE.sub("", source)
    source = EXPORT_PREFIX_RE.sub(r"\1", source)

    leftover = re.search(r"^\s*(import|export)\b.*$", source, re.M)
    if leftover:
        raise SystemExit(
            f"{name}: could not strip module syntax from:\n  {leftover.group(0).strip()}\n"
            f"The flattener handles plain imports and `export <decl>` only."
        )
    return source.strip()


def extract_body(html: str) -> str:
    match = re.search(r"<body[^>]*>([\s\S]*)</body>", html)
    body = match.group(1) if match else html
    # The module script is replaced by the inlined bundle.
    body = re.sub(r'<script[^>]*type="module"[^>]*></script>', "", body)
    return body.strip()


def main() -> int:
    out_path = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "dist" / "dla-mileny.html"

    css = "\n\n".join(
        f"/* ===== {name} ===== */\n{(WEB / name).read_text(encoding='utf-8').strip()}"
        for name in CSS
    )

    parts = []
    for name in MODULES:
        source = (WEB / name).read_text(encoding="utf-8")
        parts.append(f"// ===== {name} =====\n{strip_module_syntax(source, name)}")
    script = "\n\n".join(parts)

    body = extract_body((WEB / "index.html").read_text(encoding="utf-8"))

    # The charset declaration has to come first and stay inside the first 1024
    # bytes. Without it a server that omits the header leaves every "ę" and "ł"
    # as mojibake -- which is a particularly bad look for this project.
    page = f"""<meta charset="utf-8">
<title>Dla Mileny</title>

<style>
{css}
</style>

{body}

<script>
  // No query string on a shared link, so the mock matrix is selected here.
  window.LOVE_FORCE_MOCK = true;
</script>

<script type="module">
{script}
</script>
"""

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(page, encoding="utf-8", newline="\n")
    print(f"{out_path}  ({len(page) / 1024:.0f} kB)")
    print(f"  {len(CSS)} stylesheets, {len(MODULES)} modules")
    return 0


if __name__ == "__main__":
    sys.exit(main())
