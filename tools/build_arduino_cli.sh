#!/bin/sh
# Alternative build with arduino-cli (PlatformIO is the primary way, see platformio.ini).
# Usage: ARDUINO_CLI="arduino-cli --config-file cli.yaml" FQBN=espressif:esp32:esp32s3 tools/build_arduino_cli.sh
# Needs the NimBLE-Arduino 1.4.3 library: install it with "arduino-cli lib install NimBLE-Arduino@1.4.3"
# or point NIMBLE_DIR to a checkout (https://github.com/h2zero/NimBLE-Arduino, tag 1.4.3).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CLI="${ARDUINO_CLI:-arduino-cli}"
FQBN="${FQBN:-esp32:esp32:esp32s3}"
SK=/tmp/paranav_sketch/paranav
NIMBLE_FLAGS="-DCONFIG_BT_NIMBLE_MAX_CONNECTIONS=2 -DCONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=1 -DCONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=6144"
rm -rf /tmp/paranav_sketch && mkdir -p "$SK"
cp "$ROOT"/src/*.cpp "$ROOT"/src/*.h "$SK"/
mv "$SK/main.cpp" "$SK/paranav_main.cpp"
echo "// see paranav_main.cpp" > "$SK/paranav.ino"
$CLI compile --fqbn "$FQBN:PSRAM=opi,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc,USBMode=default" \
  ${NIMBLE_DIR:+--library "$NIMBLE_DIR"} --library "$ROOT/lib/epdiy" --library "$ROOT/lib/SensorLib" --library "$ROOT/lib/BQ27220" --library "$ROOT/lib/navcore" \
  --build-property "compiler.cpp.extra_flags=-std=gnu++17 -DBOARD_HAS_PSRAM $NIMBLE_FLAGS" \
  --build-property "compiler.c.extra_flags=$NIMBLE_FLAGS" \
  --output-dir "$ROOT/build" "$SK" "$@"
