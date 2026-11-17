#!/bin/sh
# Builds and runs the desktop preview (Linux/macOS, needs g++ and python3+Pillow for PNG conversion)
set -e
cd "$(dirname "$0")/../.."
g++ -std=c++17 -O2 -Ilib/navcore/src tools/preview/preview.cpp lib/navcore/src/*.cpp lib/navcore/src/ui/*.cpp -o /tmp/paranav_preview
/tmp/paranav_preview sd_card /tmp/preview "${TERRAIN_DIR:-/tmp}" "${SIM_SECONDS:-900}"
python3 -c "
import glob
from PIL import Image
for p in sorted(glob.glob('/tmp/preview/*.pgm')):
    Image.open(p).save(p[:-4]+'.png')
print('PNG written to /tmp/preview')
"
