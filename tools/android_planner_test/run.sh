#!/bin/sh
# Route planner helpers (Android app, pure Java) + cross-check: a .cup written by the app is read by the firmware.
set -e
cd "$(dirname "$0")/../.."
rm -rf /tmp/planner_test && mkdir -p /tmp/planner_test
J=android/app/src/main/java/pl/paranav/uploader
javac -d /tmp/planner_test $J/Geo.java $J/CupFile.java $J/OpenAir.java $J/Tr.java $J/RouteCheck.java tools/android_planner_test/PlannerTest.java
java -cp /tmp/planner_test pl.paranav.uploader.PlannerTest sd_card
g++ -std=c++17 -O1 -Ilib/navcore/src tools/android_planner_test/route_check.cpp lib/navcore/src/waypoints.cpp \
  lib/navcore/src/geo.cpp -o /tmp/planner_test/route_check
/tmp/planner_test/route_check
