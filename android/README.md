# ParaNav – Android app (file upload via Bluetooth)

🇵🇱 [Wersja polska](README.pl.md)

A simple app (Java, no extra libraries, Android 8.0+) that connects to ParaNav over Bluetooth LE and sends:

* airspace – an OpenAir `.txt` file chosen on the phone ("Replace previous airspace" option),
* today's airspace activations – typed in the app (`EPTR12 0800-1500`, UTC),
* waypoints and route – SeeYou `.cup` files,
* and it lists / deletes the files stored on the device.

App language: Polish when the phone is set to Polish, English otherwise.

ParaNav checks every file (CRC32 checksum and whether it contains airspace/waypoints) before saving it – a bad file never overwrites a good one. Once saved, the data is loaded immediately.

## How to build the APK

**A. Android Studio (easiest):** File → Open → the `android/` folder → wait for the Gradle sync → Build → Build App Bundle(s) / APK(s) → Build APK(s). File: `android/app/build/outputs/apk/debug/app-debug.apk`. Copy it to the phone and install it (allow installing from unknown sources).

**B. GitHub Actions (nothing to install):** push the whole project to a GitHub repository → Actions tab → "Android APK" → Run workflow → after a few minutes download the `paranav-apk` artifact (the `.apk` file).

**C. Command line** (Android SDK installed, JDK 17): `cd android && ./gradlew assembleDebug`.

The `release` build is signed with the debug key – fine for your own phone, not for Google Play.

## Usage
1. On ParaNav: Menu → System → **Phone (Bluetooth)** = YES (on by default).
2. In the app: **Connect to ParaNav** (the first time, allow "Nearby devices"/Bluetooth).
3. Pick the kind of file and the file on the phone (e.g. downloaded with the browser to "Downloads").
4. Progress is shown in the app and at the bottom of the ParaNav screen. A large airspace file (~800 kB) usually takes a few tens of seconds – depends on the phone.

## Protocol (for the curious)
BLE service `7e2a0001-…`, characteristics: control `…0002` (write + notify, text lines) and data `…0003` (write without response: 4-byte offset + data). Commands are described in `lib/navcore/src/filexfer.h`. Test of both sides together: `tools/android_proto_test/run.sh` (the app's `Uploader.java` ↔ the firmware's `FileReceiver`, with deliberately lost packets).
