#!/bin/sh
# Regenerates the README screenshots in docs/screens/pl and docs/screens/en from the screen preview
# (tools/preview/preview.cpp - the same drawing code as the device, a simulated flight with sd_card/ data).
# Uses a synthetic terrain tile (gentle hills ~110 m, around the simulated flight) so AGL / terrain fields
# show values. Needs g++ and python3 with numpy + Pillow.
# Not covered here (made separately): V_*, W_*, X_*, Y_*, Z_* (USB question, charging, airspace exit,
# route check, inside-airspace alert).
set -e
cd "$(dirname "$0")/../.."
T=/tmp/paranav_screens
rm -rf "$T" && mkdir -p "$T/terrain" "$T/pl" "$T/en"
python3 - "$T/terrain/N52E020.hgt" <<'PY'
import sys
import numpy as np
n = 1201
y, x = np.mgrid[0:n, 0:n] / 1200.0
lat, lon = 53 - y, 20 + x
h = 110 + 25 * np.sin((lat - 52.0) * 40) * np.cos((lon - 20.8) * 30) + 15 * (lon - 20.85) * 10
np.clip(h, 80, 250).astype(">i2").tofile(sys.argv[1])
PY
g++ -std=c++17 -O2 -Ilib/navcore/src tools/preview/preview.cpp lib/navcore/src/*.cpp lib/navcore/src/ui/*.cpp -o "$T/preview"
for l in pl en; do
  PARANAV_LANG=$l "$T/preview" sd_card "$T/$l" "$T/terrain" 900 > /dev/null
  python3 - "$T/$l" "docs/screens/$l" <<'PY'
import os, sys
from PIL import Image
src, dst = sys.argv[1], sys.argv[2]
for f in sorted(os.listdir(src)):
    if f.endswith(".pgm"):
        Image.open(os.path.join(src, f)).convert("L").save(os.path.join(dst, f[:-4] + ".png"))
PY
done
echo "screenshots written to docs/screens/pl and docs/screens/en"
