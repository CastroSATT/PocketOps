# PlatformIO pre-script: data/www/** → include/generated/RemoteHidWwwSeed.h
Import("env")

from pathlib import Path

PROJECT_DIR = Path(env["PROJECT_DIR"])
SRC_DIR = PROJECT_DIR / "data" / "www"
OUT = PROJECT_DIR / "include" / "generated" / "RemoteHidWwwSeed.h"
ALLOW_EXT = {".html", ".css", ".js"}


def collect_files():
    files = []
    for path in sorted(SRC_DIR.rglob("*")):
        if not path.is_file():
            continue
        if path.name.startswith("_"):
            continue
        if path.suffix.lower() not in ALLOW_EXT:
            continue
        rel = path.relative_to(SRC_DIR).as_posix()
        files.append((f"/www/{rel}", path))
    if not files:
        raise SystemExit(f"embed www seed: no html/css/js under {SRC_DIR}")
    return files


files = collect_files()
chunks = []
entries = []
for i, (fs_path, path) in enumerate(files):
    text = path.read_text(encoding="utf-8")
    delim = f"PO{i}"
    closer = f"){delim}"
    if closer in text:
        raise SystemExit(f"embed www seed: {path} contains forbidden {closer}")
    var = f"WWW_SEED_{i}"
    chunks.append(
        f"static const char {var}[] PROGMEM = R\"{delim}(\n{text}){delim}\";\n"
    )
    entries.append(f'  {{"{fs_path}", {var}}}')

OUT.parent.mkdir(parents=True, exist_ok=True)
OUT.write_text(
    "// AUTO-GENERATED from data/www — do not edit\n"
    "#pragma once\n"
    "\n"
    "#include <Arduino.h>\n"
    "\n"
    "struct WwwSeedFile {\n"
    "  const char* path;\n"
    "  const char* data;\n"
    "};\n"
    "\n"
    + "\n".join(chunks)
    + "\n"
    "static const WwwSeedFile WWW_SEED_FILES[] = {\n"
    + ",\n".join(entries)
    + "\n};\n"
    "\n"
    f"static const size_t WWW_SEED_FILE_COUNT = {len(files)};\n",
    encoding="utf-8",
)
print(f"embed www seed: wrote {OUT.relative_to(PROJECT_DIR)} ({len(files)} files)")
