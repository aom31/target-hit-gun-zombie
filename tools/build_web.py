"""PlatformIO pre-script: gzip web/ into data/www/ before the filesystem image is built.

Only runs for `buildfs` / `uploadfs`, so normal firmware builds stay fast. The
hub's static handler serves `index.html.gz` when `index.html` is requested.
"""

import gzip
import shutil
from pathlib import Path

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

from SCons.Script import COMMAND_LINE_TARGETS  # noqa: E402

FS_TARGETS = {"buildfs", "uploadfs", "uploadfsota"}
WEB_FILES = {".html", ".css", ".js", ".svg", ".json", ".ico", ".webmanifest"}
BINARY_FILES = {".mp3", ".wav", ".ogg", ".m4a", ".woff2", ".png", ".jpg", ".webp"}  # already compressed: copied as-is
MAX_AUDIO_BYTES = 400 * 1024
SKIP = {"demo.js"}  # browser-only simulator, not needed on the hub


def build_web():
    project = Path(env.subst("$PROJECT_DIR"))  # noqa: F821
    src = project / "web"
    out = project / "data" / "www"
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)

    total = 0
    for path in sorted(src.rglob("*")):
        if not path.is_file() or path.name in SKIP:
            continue
        rel = path.relative_to(src)
        if path.suffix.lower() in BINARY_FILES:
            size = path.stat().st_size
            if size > MAX_AUDIO_BYTES:
                raise SystemExit(f"web: {rel} is {size} B; keep binary assets under {MAX_AUDIO_BYTES} B (mono, 64-96 kbps)")
            target = out / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target)
            total += size
            print(f"  web: {rel} -> {size} B (copied)")
            continue
        if path.suffix not in WEB_FILES:
            continue
        target = out / (str(rel) + ".gz")
        target.parent.mkdir(parents=True, exist_ok=True)
        # mtime=0 keeps the output byte-identical between builds.
        with path.open("rb") as fin, target.open("wb") as raw:
            with gzip.GzipFile(fileobj=raw, mode="wb", compresslevel=9, mtime=0) as fout:
                shutil.copyfileobj(fin, fout)
        total += target.stat().st_size
        print(f"  web: {rel} -> {target.stat().st_size} B")
    print(f"  web: {total} B total in data/www")


if FS_TARGETS.intersection(COMMAND_LINE_TARGETS):
    build_web()
