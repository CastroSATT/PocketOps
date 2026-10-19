#!/usr/bin/env bash
# Copy sdcard-layout/{Mac,PC,Linux,Logs} onto a mounted POCKETOPS volume (macOS).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/sdcard-layout"
FOLDERS=(Mac PC Linux Logs Audio)

find_volume() {
  local vol
  for vol in /Volumes/*; do
    [[ -d "$vol" ]] || continue
    # FAT label is POCKETOPS; Finder may show that name.
    if [[ "$(basename "$vol")" == "POCKETOPS" ]]; then
      printf '%s\n' "$vol"
      return 0
    fi
  done
  return 1
}

if [[ ! -d "$SRC" ]]; then
  echo "Missing source: $SRC" >&2
  exit 1
fi

DEST="$(find_volume)" || {
  echo "No POCKETOPS drive mounted under /Volumes." >&2
  echo "Expose the card on the stick, then run again." >&2
  exit 1
}

echo "Source: $SRC"
echo "Dest:   $DEST"

for name in "${FOLDERS[@]}"; do
  mkdir -p "$DEST/$name"
  # Only copy new/changed files. FAT mtimes are coarse (2s), so allow that skew
  # instead of re-copying everything that already matches by size+time.
  rsync -a --update --modify-window=2 --exclude '.gitkeep' \
    "$SRC/$name/" "$DEST/$name/"
  echo "  synced $name/"
done

echo "Done."
# Nudge Finder / FAT to flush
sync
