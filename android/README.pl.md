# ParaNav – aplikacja na Androida (wysyłanie plików przez Bluetooth)

🇬🇧 [English version](README.md)

Język aplikacji: polski, gdy telefon jest ustawiony na polski, w pozostałych przypadkach angielski.

Prosta aplikacja (Java, bez dodatkowych bibliotek, Android 8.0+), która łączy się z ParaNav przez Bluetooth LE i wysyła:

* strefy – plik OpenAir `.txt` wybrany z telefonu (opcja „Zastąp poprzednie strefy”),
* aktywacje stref na dziś – wpisywane w aplikacji (`EPTR12 0800-1500`, czas UTC),
* punkty i trasę – pliki SeeYou `.cup`,
* oraz pokazuje / usuwa pliki zapisane w urządzeniu.

ParaNav sprawdza plik (suma kontrolna CRC32 i czy zawiera strefy/punkty) zanim go zapisze – zły plik nie nadpisze dobrego. Po zapisaniu dane są od razu wczytywane.

## Jak zbudować APK

**A. Android Studio (najprościej):** File → Open → katalog `android/` → poczekaj na synchronizację Gradle → Build → Build App Bundle(s) / APK(s) → Build APK(s). Plik: `android/app/build/outputs/apk/debug/app-debug.apk`. Skopiuj na telefon i zainstaluj (zezwól na instalację z nieznanych źródeł).

**B. GitHub Actions (bez instalowania niczego):** wrzuć cały projekt do repozytorium na GitHubie → zakładka Actions → „Android APK” → Run workflow → po kilku minutach pobierz artefakt `paranav-apk` (plik `.apk`).

**C. Wiersz poleceń** (zainstalowany Android SDK, JDK 17): `cd android && ./gradlew assembleDebug`.

Wydanie `release` jest podpisane kluczem debug – nadaje się do instalacji na własnym telefonie, nie do Google Play.

## Użycie
1. W ParaNav: Menu → System → **Telefon (Bluetooth)** = TAK (domyślnie włączone).
2. W aplikacji: **Połącz z ParaNav** (za pierwszym razem zgoda na „Urządzenia w pobliżu”/Bluetooth).
3. Wybierz rodzaj pliku i plik z telefonu (np. pobrany wcześniej przeglądarką do „Pobrane”).
4. Postęp widać w aplikacji i na dole ekranu ParaNav. Duży plik stref (~800 kB) przesyła się zwykle w kilkadziesiąt sekund – zależy od telefonu.

## Protokół (dla ciekawych)
Usługa BLE `7e2a0001-…`, charakterystyki: sterowanie `…0002` (zapis + powiadomienia, linie tekstu) i dane `…0003` (zapis bez odpowiedzi: 4 bajty przesunięcia + dane). Opis komend w `lib/navcore/src/filexfer.h`. Test obu stron razem: `tools/android_proto_test/run.sh` (aplikacyjny `Uploader.java` ↔ firmware'owy `FileReceiver` z celowo gubionymi pakietami).
