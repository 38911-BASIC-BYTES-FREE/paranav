#!/bin/sh
# End-to-end check of the map pipeline without internet: synthetic OSM file -> make_tiles.py ->
# firmware tile reader (host tests decode every tile and verify the CRC). Needs the Python packages
# from requirements.txt and g++.
set -e
cd "$(dirname "$0")/../.."
T=/tmp/paranav_topo_selftest
rm -rf "$T" && mkdir -p "$T"
python3 tools/topo/make_test_osm.py "$T/test.osm.pbf"
python3 tools/topo/make_tiles.py --pbf "$T/test.osm.pbf" --name test --title "Test (fikcyjna)" \
  --center 52.09,20.85 --radius-km 30 --out "$T/out" --preview
PARANAV_TOPO_DIR="$T/out/maps" ./tools/run_tests.sh
echo "Mapa testowa: $T/out/maps  (podgląd ekranu: PARANAV_TOPO_DIR=$T/out/maps tools/preview/build.sh)"
