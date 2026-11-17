#!/bin/sh
# Checks that no text leaves the screen, overlaps other text or crosses separator lines.
set -e
cd "$(dirname "$0")/../.."
python3 - <<'PY'
import re
src = open('src/main.cpp', encoding='utf-8').read()
labels = re.findall(r'M::(?:num|act|toggle|sub)\(\s*"([^"]+)"', src)
labels += ["Trasa przelot_dlugi_2026.cup: start", "Lądowisko Zakrzewo Świerczynówek  (123.4 km)"]
open('/tmp/menu_labels.txt', 'w', encoding='utf-8').write("\n".join(labels) + "\n")
PY
g++ -std=c++17 -O1 -Ilib/navcore/src tools/preview/layout_check.cpp lib/navcore/src/*.cpp lib/navcore/src/ui/*.cpp -o /tmp/paranav_layout
/tmp/paranav_layout sd_card /tmp/menu_labels.txt
