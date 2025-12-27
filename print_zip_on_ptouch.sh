#!/usr/bin/env bash
# print-zip-pngs.sh
# Usage: ./print-zip-pngs.sh /path/to/images.zip
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 /path/to/images.zip" >&2
  exit 1
fi

ZIP="$1"
[[ -f "$ZIP" ]] || { echo "Zip not found: $ZIP" >&2; exit 1; }

command -v ptouch-print >/dev/null 2>&1 || {
  echo "Error: ptouch-print not found in PATH." >&2
  exit 1
}

TMPDIR="$(mktemp -d -t ptpngs.XXXXXXXX)"
trap 'rm -rf "$TMPDIR"' EXIT

if command -v unzip >/dev/null 2>&1; then
  unzip -qq -- "$ZIP" -d "$TMPDIR"
elif command -v 7z >/dev/null 2>&1; then
  7z x -y -o"$TMPDIR" -- "$ZIP" >/dev/null
else
  echo "Error: need unzip or 7z to extract." >&2
  exit 1
fi

mapfile -t IMGS < <(find "$TMPDIR" -type f -iname '*.png' | LC_ALL=C sort)
(( ${#IMGS[@]} > 0 )) || { echo "No PNGs found in archive." >&2; exit 1; }

CMD=(ptouch-print)
for i in "${!IMGS[@]}"; do
  img="${IMGS[$i]}"
  CMD+=(--image "$img")
  if (( i < ${#IMGS[@]} - 1 )); then
    CMD+=(--cut)
  fi
done

exec "${CMD[@]}"