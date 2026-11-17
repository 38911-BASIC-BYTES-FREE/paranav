#!/bin/sh
# End-to-end protocol test: Android app's Uploader.java <-> firmware FileReceiver (desktop build).
set -e
cd "$(dirname "$0")/../.."
g++ -std=c++17 -O1 -Ilib/navcore/src tools/xfer_sim/xfer_sim.cpp lib/navcore/src/filexfer.cpp lib/navcore/src/airspace.cpp \
  lib/navcore/src/waypoints.cpp lib/navcore/src/geo.cpp lib/navcore/src/settings.cpp -o /tmp/xfer_sim
rm -rf /tmp/xfer_root /tmp/proto_test && mkdir -p /tmp/xfer_root /tmp/proto_test
python3 - <<'PY'
# synthetic but realistic-size OpenAir file: 800 zones
out=[]
for i in range(800):
    out.append(f"AC {'RCQPD'[i%5]}\nAN ZONE {i}\nAL GND\nAH FL95")
    if i%3==0: out.append(f"V X=52:{i%60:02d}:00 N 020:{i%60:02d}:00 E\nDC {1+i%5}")
    else:
        for k in range(60): out.append(f"DP 5{i%3}:{k%60:02d}:00 N 02{k%9}:{(k*7)%60:02d}:00 E")
open('/tmp/proto_test/big.txt','w').write("\n".join(out)+"\n")
PY
javac -d /tmp/proto_test android/app/src/main/java/pl/paranav/uploader/Link.java \
  android/app/src/main/java/pl/paranav/uploader/Proto.java android/app/src/main/java/pl/paranav/uploader/Uploader.java \
  tools/android_proto_test/ProtoTest.java
java -cp /tmp/proto_test ProtoTest /tmp/xfer_sim /tmp/xfer_root /tmp/proto_test/big.txt sd_card
