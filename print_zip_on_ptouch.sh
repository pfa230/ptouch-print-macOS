#!/usr/bin/env bash
# print_zip_on_ptouch.sh
# Usage: ./print_zip_on_ptouch.sh /path/to/images.zip
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: print_zip_on_ptouch.sh /path/to/images.zip

Print all PNG files in the zip (sorted by path), cutting between each image.

Environment:
  PTOUCH_PRINT  Path to ptouch-print binary (default: ptouch-print in PATH,
                or ./build/Release/ptouch-print if present).
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

if [[ $# -ne 1 ]]; then
  usage >&2
  exit 1
fi

ZIP="$1"
[[ -f "$ZIP" ]] || { echo "Zip not found: $ZIP" >&2; exit 1; }

PTOUCH_BIN=""
if [[ -n "${PTOUCH_PRINT:-}" ]]; then
  PTOUCH_BIN="$PTOUCH_PRINT"
elif command -v ptouch-print >/dev/null 2>&1; then
  PTOUCH_BIN="$(command -v ptouch-print)"
elif [[ -x "./build/Release/ptouch-print" ]]; then
  PTOUCH_BIN="./build/Release/ptouch-print"
else
  echo "Error: ptouch-print not found. Set PTOUCH_PRINT or add to PATH." >&2
  exit 1
fi
[[ -x "$PTOUCH_BIN" ]] || { echo "Error: ptouch-print not executable: $PTOUCH_BIN" >&2; exit 1; }
echo "Running ptouch-print from $PTOUCH_BIN"

TMPDIR="$(mktemp -d -t ptpngs.XXXXXXXX)"
trap 'rm -rf "$TMPDIR"' EXIT

if command -v unzip >/dev/null 2>&1; then
  unzip -qq -- "$ZIP" -d "$TMPDIR"
elif command -v 7z >/dev/null 2>&1; then
  7z x -y -o"$TMPDIR" -- "$ZIP" >/dev/null
elif command -v ditto >/dev/null 2>&1; then
  ditto -xk "$ZIP" "$TMPDIR"
else
  echo "Error: need unzip, 7z, or ditto to extract." >&2
  exit 1
fi

IMGS=()
while IFS= read -r img; do
  IMGS+=("$img")
done < <(find "$TMPDIR" -type f -iname '*.png' | LC_ALL=C sort)
(( ${#IMGS[@]} > 0 )) || { echo "No PNGs found in archive." >&2; exit 1; }

CMD=("$PTOUCH_BIN")
for i in "${!IMGS[@]}"; do
  img="${IMGS[$i]}"
  CMD+=(--image "$img")
  if (( i < ${#IMGS[@]} - 1 )); then
    CMD+=(--cut)
  fi
done

exec "${CMD[@]}"
