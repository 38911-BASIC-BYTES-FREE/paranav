<<<<<<< HEAD
# ParaNav – komputer lotu dla motoparalotni

Firmware dla **LilyGO T5 E-Paper S3 Pro** (ESP32-S3, e-papier 4,7″ 960×540, GPS MIA-M10Q/L76K) z dodatkowym barometrem **MS5611** na złączu QWIIC. Ekran w poziomie, dane ładowane z karty microSD.

> ⚠️ Projekt eksperymentalny. Dane stref i obliczenia **nie są certyfikowane** – to pomoc, nie podstawa nawigacji. Zawsze sprawdzaj AIP/AUP przed lotem.

## Funkcje (wersja 0.2)

| Obszar | Co działa |
|---|---|
| Wysokość | baro (QNH, auto-kalibracja z GPS lub mapy terenu), nad startem, nad terenem (SRTM) |
| Wariometr | filtr Kalmana ~90 Hz, średnia 30 s, wykrywanie krążenia i zysku w kominie (tylko wizualnie – brak brzęczyka) |
| Wiatr | z dopasowania okręgu do wektorów prędkości GPS podczas zakrętów (≥200° w ciągu 90 s) |
| Doskonałość | zmierzona (GS / opadanie) przy locie ślizgowym, zasięg ślizgu z biegunowej + wiatr, „dolecisz do startu” |
| Paliwo | zapas czasu, paliwo na powrót (z wiatrem) + rezerwa, alarm WRACAJ, licznik zatrzymywany przy wyłączonym silniku |
| Strefy | OpenAir z SD, test w poziomie i pionie, przewidywany czas wlotu, plik aktywacji AUP, profil pionowy |
| Nawigacja | Go Home, punkt z listy, najbliższe lądowisko, trasa (.cup) z auto-przełączaniem, ETE/ETA, wymagana L/D |
| Zapis lotu | IGC + GPX w `/logs`, automatyczny start/stop (wykrywanie startu i lądowania), podsumowanie lotu |
| Mapa TOPO | czarno-biała mapa topograficzna z OpenStreetMap (woda, las, zabudowa, linie energetyczne, wiatraki, maszty, lotniska, drogi), północ u góry, z10–z14 |
| Wgrywanie | karta SD w czytniku, **tryb USB** (karta jako dysk w komputerze), telefon przez Bluetooth (strefy, punkty) |
| System | bateria (BQ27220), stan GPS, karta SD, test dotyku |

## Sprzęt

### Podłączenie MS5611
Złącze QWIIC (JST-SH 4 pin): **czarny = GND, czerwony = 3V3, niebieski = SDA (GPIO39), żółty = SCL (GPIO40)**.
Moduł GY-63: VCC→3V3, GND→GND, SCL→SCL, SDA→SDA, PS zostaw (na module podciągnięty do VCC = tryb I²C).
Adres 0x77 (CSB=0) lub 0x76 – oba są wykrywane automatycznie.

Wskazówki montażu: czujnik jest **wrażliwy na światło** – przykryj go kawałkiem gąbki/pianki, nie wystawiaj na strumień powietrza ze śmigła, a obudowa powinna mieć mały otwór wyrównujący ciśnienie.

### Barometr: kabel, BlueFly albo bez
Jeden firmware obsługuje automatycznie trzy przypadki – źródło wybierane jest na bieżąco:
1. **MS5611 na kablu QWIIC** – jeśli odpowiada (sprawdzany co 10 s, można go podłączyć po włączeniu),
2. **BlueFlyVario v25 przez Bluetooth LE** – urządzenie samo wyszukuje „BlueFly…” (usługa Microchip Transparent UART), łączy się i wznawia połączenie po zerwaniu. Obsługiwane formaty wyjścia BlueFly: `PRS`/`_PRS` (domyślny), `LK8EX1`, `LXWP0`, `$XCTOD` (bateria). Pozycja GPS z BlueFly jest ignorowana – używany jest wewnętrzny GPS,
3. **bez barometru** – wysokość i vario z GPS (mniej dokładne, vario opóźnione o kilka sekund).

Zmiana źródła restartuje filtr vario (bez fałszywego skoku), a utrata barometru w locie daje alarm BRAK BAROMETRU. Bluetooth można wyłączyć w Menu → System (oszczędza baterię). Uwaga: BlueFly połączony z ParaNav nie połączy się w tym czasie z telefonem.

### Przyciski (uwaga, poprawka do wcześniejszej propozycji)
Na schemacie IO48 to linia wyświetlacza, a przycisk użytkownika **S3 jest podłączony przez ekspander PCA9535 (IO1_2)**. Drugim przyciskiem jest **BOOT (GPIO0)**.

| | Krótko | Długo (>0,7 s) |
|---|---|---|
| **KLAWISZ (S3)** | następny ekran / w menu: dalej / w edycji: + | otwórz menu / wybierz / zapisz |
| **BOOT** | poprzedni ekran / w menu: wstecz / w edycji: − | **Go Home** (cel = start, ekran NAWIGACJA) / wyjdź z menu |
| dowolny | potwierdza aktywny alarm | |
| **oba naraz** | krótko (<1 s) = **podświetlenie wł./wył.** | 3 s = **wyłącz** (działa też w menu) |

**Podświetlenie:** naciśnij krótko oba przyciski jednocześnie (albo Menu → Ekran i mapa → Podświetlenie). Gdy świeci, na pasku stanu jest czarny kwadrat ze słoneczkiem – w pełnym słońcu od razu widać, że niepotrzebnie zużywa baterię. Jasność i automatyczne wyłączanie po N minutach ustawisz w tym samym menu. Przycisku PWR nie da się do tego użyć: jest podłączony tylko do układu ładowania (wejście QON układu BQ25896), a nie do procesora, więc program nie widzi jego naciśnięć.

**Włączanie / wyłączanie:** włącza przycisk PWR na płytce albo podłączenie USB. Wyłączanie: **przytrzymaj oba przyciski (KLAWISZ + BOOT) przez 3 s** (po 1 s pojawia się napis „WYŁĄCZANIE…”, puszczenie anuluje) albo Menu → *Wyłącz urządzenie* → *TAK*. Lot zostaje zapisany i zamknięty, a na ekranie zostaje napis „WYŁĄCZONY”. Układ ładowania BQ25896 odcina wtedy baterię. **Przy podłączonym USB wyłączenie nie działa**, bo tak działa ten układ, a nie firmware.

Dotyk (gdy masz gołe palce): pasek u góry = menu, lewa krawędź = poprzedni ekran, prawa = następny, na MAPIE środek = zmiana skali, na NAWIGACJI = zmiana celu, na PALIWIE = przyciski ±0,5 l / pełny bak / silnik.

## Pomoc w urządzeniu
Menu → **Pomoc**: przyciski i dotyk, pasek stanu, opis każdego ekranu, alarmy, pliki na karcie SD, automatyczny start/lądowanie/zapis. Treść jest w `lib/navcore/src/ui/help.cpp` – łatwo ją poprawić; `tools/preview/layout_check.sh` sprawdza, czy każda strona mieści się na ekranie i czy font ma wszystkie znaki.

## Ekrany

`LOT → MAPA → TOPO → NAWIGACJA → TERMIKA → STREFY → PALIWO → SYSTEM` (podglądy PNG w `docs/screens/`; TOPO na podglądzie pokazuje fikcyjną mapę testową).

### Strefy na mapie
Wnętrza stref są kreskowane, a nie szare. Szybkie odświeżanie e-papieru (co 1 s) obsługuje tylko czerń i biel, więc szarość „pływałaby” między odświeżeniami.

* **gęsta kratka** – strefy zakazane, ograniczone i niebezpieczne (P, R, Q, EPP/EPR/EPD),
* **ukośne linie co 10 px** – CTR oraz TRA/TSA (EPTR/EPTS),
* **rzadkie ukośne linie** – pozostałe (TMA, ATZ, klasy C/D...).

Kreskowane są tylko strefy aktywne i istotne na Twojej wysokości: jesteś w ich zakresie pionowym albo bliżej niż 2× bufor pionowy, min. 150 m. Strefy wysoko nad Tobą lub pod Tobą mają sam kontur, a nieaktywne – kontur przerywany.

## Wysyłanie plików z telefonu (Android)
Aplikacja w katalogu `android/` (instrukcja budowania: `android/README.md`) wysyła przez Bluetooth strefy (OpenAir), aktywacje na dziś, punkty i trasę (.cup) – bez wyjmowania karty SD. ParaNav sprawdza każdy plik przed zapisaniem i od razu go wczytuje; w czasie odbioru na dole ekranu jest pasek postępu, a na pasku stanu „TEL”. Włączanie/wyłączanie: Menu → System → Telefon (Bluetooth).

## Karta SD (FAT32)

```
/config.ini                 ustawienia (tworzone/aktualizowane przez urządzenie)
/airspace/*.txt             strefy w formacie OpenAir (wczytywane są wszystkie pliki .txt)
/airspace/activations.txt   aktywacje stref na dziś (format poniżej)
/waypoints/*.cup            punkty (SeeYou .cup); styl 2–5 = lądowisko
/route/*.cup                trasa – kolejność punktów w pliku = kolejność trasy
/terrain/N52E021.hgt ...    mapa terenu SRTM (.hgt, 3″ lub 1″)
/maps/<obszar>/info.txt     mapa podkładowa dla ekranu TOPO (+ z10.pak … z14.pak)
/logs/                      zapisane loty .igc + .gpx
```
Przykładowe (fikcyjne!) pliki są w katalogu `sd_card/`.

### Skąd brać dane
* **Strefy (OpenAir)**: [openAIP](https://www.openaip.net) – eksport dla Polski w formacie OpenAir; albo plik Ryszarda Lewandowskiego z lotnik.org (link na [soaringweb.org/SUA/PL](https://soaringweb.org/SUA/PL)). Aktualizuj co cykl AIRAC (28 dni).
* **Aktywacje (AUP/UUP)**: strefy TRA/TSA/EPR/EPD/MRT są aktywowane dziennie. Na razie przepisujesz je do `activations.txt` (np. z aplikacji „AUP w Polsce” lub serwisu PANSA). Bez tego pliku takie strefy mają status „AKT.?” i są traktowane jak aktywne (bezpieczniej).
  ```
  date=2026-11-16
  EPTR12 0800-1500
  EPTS05 0900-1100 1300-1600
  ```
  Czas w **UTC**; nazwa dopasowuje się do początku nazwy strefy (`EPTR12` pasuje do „EPTR12 KRZESINY”, ale nie do „EPTR121”).
* **Teren**: kafle SRTM `.hgt` w katalogu `/terrain` (szczegóły w sekcji „Mapa terenu” niżej).
* **Punkty**: dowolny edytor .cup (SeeYou, XCSoar, WeGlide).

## Mapa terenu (.hgt)

Teren daje: wysokość **nad terenem**, automatyczne QNH, profil pionowy na ekranie STREFY i zasięg ślizgu względem ziemi. Bez niego te pola pokazują „––”, a reszta działa.

**Skąd:**
1. **viewfinderpanoramas.org** (polecane – SRTM z uzupełnionymi dziurami): strona [dem3.html](https://viewfinderpanoramas.org/dem3.html) → mapa pokrycia 3″ → kliknij arkusze nad Polską. Arkusze mapy świata 1:1 mln nazywają się **M33, M34** (48–52°N) i **N33, N34** (52–56°N), wschodni skraj Polski jest w **M35/N35**. Każda paczka ZIP to kilkadziesiąt kafli.
2. **NASA Earthdata**, zbiór SRTMGL3 (wymaga darmowego konta): pliki `N52E021.SRTMGL3.hgt.zip` – po rozpakowaniu `N52E021.hgt`.

**Dokąd:** do katalogu **`/terrain`** na karcie SD. Kafle mogą leżeć bezpośrednio tam albo w podkatalogach, np. tak, jak rozpakuje się ZIP: `/terrain/M34/N52E021.hgt`. Wielkość liter nie ma znaczenia.

**Które:** nazwa kafla to jego południowo-zachodni róg, np. `N52E021.hgt` = 52–53°N, 21–22°E. Cała Polska to N49–N54 × E014–E024 (~60 kafli z lądem, ok. 170 MB w 3″). Wystarczą kafle wokół miejsc, w których latasz. Obsługiwane są 3″ (2,9 MB/kafel) i 1″ (26 MB/kafel; dokładniejsze, ale wolniejsze na karcie).

**Sprawdzenie:** ekran SYSTEM → „Kafle terenu” (liczba znalezionych plików) i „Teren pod Tobą” (wysokość terenu w Twoim miejscu, po złapaniu GPS).

## Mapy podkładowe (ekran TOPO)

Ekran **TOPO** (po MAPIE) pokazuje mapę topograficzną przygotowaną specjalnie dla e-papieru: czarno-białą, z grubymi znakami tego, co groźne dla pilota.

| Znak | Co to |
|---|---|
| poziome kreski, ciągły brzeg | woda, morze |
| kropki | las |
| kratka | zabudowa (osiedla, przemysł) |
| linia z poprzecznymi kreskami i kropkami | linia wysokiego napięcia (cienka bez kropek – średnie napięcie, od z12) |
| trójnóg w kółku | wiatrak (od z13 z wysokością, jeśli jest w OSM) |
| daszek z kropką | maszt, wieża, komin |
| kółko z poziomą kreską | lotnisko / lądowisko (`aeroway=aerodrome/airstrip`), od z12 pasy startowe |
| czarne linie | drogi (grubość = ranga) |
| podwójna linia | rzeka; czarno-biała linia – kolej |

Na mapę nakładane są: Twoja pozycja i kurs, ślad, START, linia do celu, punkty i lądowiska z `.cup` oraz **tylko kontury** stref (szczegóły – ekran STREFY i alarmy). W rogach: wysokość/AGL, prędkość/kurs, vario/wiatr, najbliższa strefa. Dotyk górnej połowy mapy – przybliż, dolnej – oddal (z10 ≈ 90 km szerokości ekranu, z14 ≈ 6 km). Mapa przesuwa się skokami (gdy odlecisz od środka o ok. 1/3 ekranu), żeby e-papier nie przerysowywał wszystkiego co sekundę.

> Mapa z OSM jest pomocą orientacyjną, **nie mapą lotniczą** – linie energetyczne i przeszkody mogą być niekompletne.

### 1. Przygotowanie mapy

Dane: [Geofabrik](https://download.geofabrik.de/europe/poland.html) (OpenStreetMap, licencja ODbL – pobieranie do użytku offline dozwolone). Program: `tools/topo/make_tiles.py`.

**Sposób A – na GitHubie, bez instalowania czegokolwiek (polecany):**
1. Projekt w Twoim repozytorium na GitHubie (to samo, co do budowania APK).
2. **Actions → „Zbuduj mapę” → Run workflow.** Domyślnie: Gdańsk, promień 100 km, poziomy 10–14, pliki pomorskie + kujawsko-pomorskie + warmińsko-mazurskie + obwód kaliningradzki (żeby linia brzegowa Zalewu i Mierzei była ciągła).
3. Po ok. 10–30 min: otwórz zakończone uruchomienie → pobierz artefakt **mapa-gdansk** (zip) → rozpakuj. W środku katalog `maps` i podglądy `podglad_*.png`.

Inny obszar: zmień nazwę, środek, promień i listę plików Geofabrik (muszą razem pokryć cały okrąg – np. dla Warszawy `europe/poland/mazowieckie`). Cała Polska: `europe/poland` (duży plik, długo).

**Sposób B – na własnym komputerze** (Python 3.9+):
```
pip install -r tools/topo/requirements.txt
python3 tools/topo/make_tiles.py --pbf pomorskie-latest.osm.pbf --pbf kujawsko-pomorskie-latest.osm.pbf \
   --pbf warminsko-mazurskie-latest.osm.pbf --pbf kaliningrad-latest.osm.pbf \
   --name gdansk --title "Gdańsk 100 km" --center 54.35,18.65 --radius-km 100 --zooms 10-14 --out out --preview
```

Rozmiar: Gdańsk 100 km, z10–z14 – szacunkowo 20–60 MB (morze i jednolite lasy zajmują prawie nic – identyczne kafelki są zapisane raz).

### 2. Wgrywanie na kartę

* **Czytnik kart:** skopiuj katalog `maps` do katalogu głównego karty.
* **Tryb USB (bez wyjmowania karty):** Menu → System → **Tryb USB – karta SD jako dysk**, podłącz USB-C do komputera. Karta pojawi się jako dysk „ParaNav”. Skopiuj `maps`, potem **wysuń dysk w komputerze** – ParaNav sam wróci do pracy i wczyta wszystko od nowa (albo KLAWISZ/BOOT długo). Działa też dla stref, punktów, terenu i lotów (`/logs` – zgrywanie IGC). Tryb USB jest niedostępny w locie; zapis lotu zostaje zamknięty.

Kilka obszarów na karcie (np. `maps/gdansk`, `maps/wyjazd`) – urządzenie samo wybiera ten, który obejmuje Twoją pozycję. SYSTEM → „Teren / mapa” pokazuje kafle terenu i nazwę aktywnej mapy.

## Budowanie

1. Zainstaluj [PlatformIO](https://platformio.org) (VS Code + rozszerzenie lub `pip install platformio`).
2. `pio run -t upload` w katalogu projektu. Jeśli płytka nie wchodzi w tryb programowania: przytrzymaj BOOT, krótko RST, puść BOOT.
3. Biblioteki LilyGO (epdiy, SensorLib, BQ27220) są dołączone w `lib/` – to te same wersje co w repozytorium producenta. NimBLE-Arduino 1.4.3 (Bluetooth) PlatformIO pobierze sam (`lib_deps`).

Alternatywnie arduino-cli (rdzeń esp32 2.0.14): `tools/build_arduino_cli.sh`.

**USB od wersji z mapami:** firmware używa trybu USB-OTG (TinyUSB, `ARDUINO_USB_MODE=0`), bo tylko w nim karta SD może być dyskiem USB. Port szeregowy nadal działa (logi, wgrywanie przez `pio run -t upload`), ale w systemie to inne urządzenie COM niż wcześniej. Jeśli wgrywanie się nie uda (np. program zawiesił się przy starcie): przytrzymaj BOOT, krótko RST, puść BOOT i wgraj ponownie.

### Testy bez sprzętu
* `tools/run_tests.sh` – testy jednostkowe (NMEA, Kalman, wiatr, parser OpenAir, aktywacje, kolizje ze strefami, IGC, .cup, teren, logika lotu, alarmy, przesyłanie plików, dekoder mapy TOPO).
* `tools/topo/selftest.sh` – cała ścieżka mapy bez internetu: fikcyjny plik OSM → `make_tiles.py` → czytnik kafelków z firmware (każdy kafelek dekodowany i sprawdzany sumą CRC).
* `tools/android_proto_test/run.sh` – test protokołu wysyłania plików: kod aplikacji (`Uploader.java`) rozmawia z kodem firmware (`FileReceiver`), z celowo gubionymi pakietami.
* `tools/preview/layout_check.sh` – renderuje wszystkie ekrany, menu, alarmy i ekran startowy w sytuacjach normalnych, „nic nie działa” i ze skrajnymi wartościami (9999 m, -12.3 m/s, 999 km, długie nazwy stref i punktów...). Zgłasza każdy napis, który wychodzi poza ekran, nachodzi na inny napis albo przecina linię podziału.
* `tools/preview/build.sh` – symuluje lot i renderuje wszystkie ekrany do PNG (`/tmp/preview`), ten sam kod UI co na urządzeniu.

## Do sprawdzenia na sprzęcie (pierwsze uruchomienie)
1. **Orientacja** – jeśli obraz jest do góry nogami: Menu → Ekran i mapa → Obróć ekran o 180°.
2. **Dotyk** – SYSTEM → „Dotyk (test)” pokazuje współrzędne; jeśli nie zgadzają się z miejscem dotknięcia, popraw mapowanie w `board.cpp: toScreen()`.
3. **VCOM** – wartość z naklejki na taśmie wyświetlacza (domyślnie 1560 mV jak w firmware LilyGO).
4. **Duchy na e-papierze** – szybkie odświeżanie (DU) co 1 s, pełne co `fullRefreshS` (domyślnie 60 s, 0 = wyłączone).
5. **Barometr** – na ziemi wario powinno pokazywać ±0,1 m/s; QNH kalibruje się samo po złapaniu ≥6 satelitów.

## Ograniczenia / dalsze kroki
* Brak dźwięku wariometru (brak brzęczyka) – przygotowane ustawienie `varioSound`.
* Plik IGC nie ma podpisu G (nie jest „walidowany” jak z certyfikowanego rejestratora). Do XContest zwykle wystarcza, na zawody z wymogiem walidacji – nie.
* Wiatr wymaga zakrętów; w locie prostym pokazywana jest ostatnia wartość z wiekiem pomiaru.
* Mapa TOPO: tylko północ u góry; mapy przez Wi-Fi z telefonu – następny etap (dziś: czytnik kart lub tryb USB).

## Struktura kodu
```
src/            sprzęt: main.cpp (zadania, logika 1 Hz, menu), board.cpp (e-papier, przyciski, dotyk, bateria, SD),
                ms5611.cpp, gps.cpp, config.h (piny)
lib/navcore/    przenośny rdzeń (kompiluje się też na PC): geo, nmea, vario (Kalman), wind, airspace (OpenAir),
                terrain (HGT), topo (mapa TOPO), waypoints (.cup), logger (IGC/GPX), flight (logika lotu), alerts, settings,
                ui/ (canvas, fonty, ekrany, menu)
test/           testy jednostkowe (host)
tools/          generator fontów, podgląd ekranów, skrypty budowania, topo/ (generator mapy TOPO)
sd_card/        przykładowa zawartość karty
```
Licencje: dane mapy TOPO – © OpenStreetMap contributors, ODbL; epdiy – LGPL-3.0, SensorLib – MIT, fonty DejaVu – licencja Bitstream Vera/DejaVu.
=======
# paranav
>>>>>>> 27bfddb68599795da26ee635efa77e9a471d3a94
