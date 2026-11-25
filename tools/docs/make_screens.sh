#!/bin/sh
# Regenerates the README screenshots in docs/screens/pl and docs/screens/en from the screen preview
# (tools/preview/preview.cpp - the same drawing code as the device, a simulated flight with sd_card/ data).
# Uses a synthetic terrain tile (gentle hills ~110 m, around the simulated flight) so AGL / terrain fields
# show values. Needs g++ and python3 with numpy + Pillow.
# V_*, W_*, X_*, Y_*, Z_* (USB question, charging, airspace exit, route check, inside-airspace alert)
# come from the layout test (tools/preview/layout_check.cpp, scenarios "normalny" and "skrajne").
# D_power_off_map, T_topo_bez_gps and G_lot* need a real background map: run with
# PARANAV_DEMO_MAPS=<make_tiles.py output>/maps (README images were made with a 40 km map of Gdansk);
# without it they are not regenerated.
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
# around the demo map (Gdansk) for the ground under the saved flight profile
lat, lon = 55 - y, 18 + x
h = 40 + 70 * np.sin(lat * 40) * np.cos(lon * 30) + 35 * np.sin(lon * 90 + lat * 20)
np.clip(h, 0, 250).astype(">i2").tofile(sys.argv[1].replace("N52E020", "N54E018"))
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
# screens from the layout test, in both languages
python3 - <<'PY'
import re
src = open('src/main.cpp', encoding='utf-8').read()
# same menu label list as tools/preview/layout_check.sh (both languages)
labels = re.findall(r'M::(?:num|act|toggle|sub)\(\s*"([^"]+)"', src)
for pl, en in re.findall(r'M::(?:num|act|toggle|sub)\(\s*TR\("([^"]+)",\s*"([^"]+)"\)', src):
    labels += [pl, en]
open('/tmp/paranav_screens/menu_labels.txt', 'w', encoding='utf-8').write("\n".join(labels) + "\n")
PY
g++ -std=c++17 -O1 -Ilib/navcore/src tools/preview/layout_check.cpp lib/navcore/src/*.cpp lib/navcore/src/ui/*.cpp -o "$T/layout"
for l in pl en; do
  rm -rf /tmp/layout && mkdir -p /tmp/layout
  PARANAV_LANG=$l "$T/layout" sd_card "$T/menu_labels.txt" > /dev/null || true
  python3 - "$l" <<'PY'
import sys
from PIL import Image
lang = sys.argv[1]
names = {"normalny_ask1": ("V_usb_pytanie", "V_usb_question"), "normalny_charging": ("W_ladowanie", "W_charging"),
         "normalny_esc_h": ("X_wyjscie_poziom", "X_exit_level"), "normalny_esc_up": ("X_wyjscie_gora", "X_exit_up"),
         "normalny_routecheck": ("Y_kontrola_trasy", "Y_route_check"),
         "skrajne_alert_asp": ("Z_alarm_w_strefie", "Z_alert_inside"),
         "normalny_landing": ("U_pytanie_ladowanie", "U_landing_question")}
for src, (pl, en) in names.items():
    Image.open(f"/tmp/layout/{src}.pgm").convert("L").save(f"docs/screens/{lang}/{pl if lang == 'pl' else en}.png")
PY
done
echo "screenshots written to docs/screens/pl and docs/screens/en"
