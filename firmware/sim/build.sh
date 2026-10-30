#!/usr/bin/env bash
# Builds the desktop version of the UI and renders every screen to PNG.
#   sim/build.sh [output dir]      (run from the firmware folder; needs gcc, g++, python3 + Pillow)
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=${1:-sim/out}
WORK=.sim
mkdir -p "$WORK/obj" "$OUT"
[ -d "$WORK/lvgl" ] || git clone -q --depth 1 -b v8.3.11 https://github.com/lvgl/lvgl.git "$WORK/lvgl"

FLAGS="-O1 -g -DLV_CONF_INCLUDE_SIMPLE -Isim -Iinclude -Isrc -I$WORK/lvgl -I$WORK"
OBJS=()
for f in $(find "$WORK/lvgl/src" -name '*.c') src/fonts/*.c; do
  o="$WORK/obj/$(echo "$f" | tr '/' '_').o"
  [ "$o" -nt "$f" ] || gcc $FLAGS -c "$f" -o "$o"
  OBJS+=("$o")
done
for f in src/theme.cpp src/ui*.cpp sim/sim_stubs.cpp sim/sim_main.cpp; do
  o="$WORK/obj/$(echo "$f" | tr '/' '_').o"
  g++ -std=gnu++17 -Wall -Wno-missing-field-initializers $FLAGS -c "$f" -o "$o" || FAIL=1
  OBJS+=("$o")
done
[ -z "${FAIL:-}" ] || exit 1
g++ -o "$WORK/sim" "${OBJS[@]}"
"$WORK/sim" "$OUT"
python3 - "$OUT" <<'PY'
import glob, os, sys
from PIL import Image
for p in glob.glob(os.path.join(sys.argv[1], '*.ppm')):
    Image.open(p).save(p[:-4] + '.png')
    os.remove(p)
PY
