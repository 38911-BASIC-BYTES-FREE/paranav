#!/usr/bin/env python3
"""One-off helper used to introduce TR("pl", "en") into the firmware sources.
Replaces quoted Polish literals (exact match, outside comments, not already inside TR(...))
with TR("pl", "en") using the dictionaries below. Kept in the repository as a record of the
translation; new texts are simply written as TR("...", "...") by hand."""
import re
import sys

SCREENS = {
    "LOT": "FLIGHT", "MAPA": "MAP", "RUCH": "TRAFFIC", "NAWIGACJA": "NAV", "TERMIKA": "THERMAL",
    "STREFY": "AIRSPACE", "PALIWO": "FUEL",
    "średnio 30 s": "30 s average", "WYSOKOŚĆ MSL": "ALTITUDE MSL", "nad startem": "above takeoff",
    "nad terenem": "AGL", "PRĘDKOŚĆ GS": "GND SPEED", "KURS": "TRACK", "START": "HOME", "czas ": "ETE ",
    "brak": "none", "WIATR": "WIND", "brak danych": "no data", "wykonaj zakręt 360°": "fly a 360° turn",
    "PALIWO – zapas czasu": "FUEL – endurance", "PALIWO – SILNIK WYŁ.": "FUEL – ENGINE OFF",
    "WRACAJ – brak paliwa na powrót": "TURN BACK – not enough fuel", "powrót: OK  zapas +%s": "return: OK  margin +%s",
    "DOSKONAŁOŚĆ L/D": "GLIDE RATIO L/D", "biegun. %.1f": "polar %.1f", "dolot do startu": "arrival at home",
    "✓ DOLECISZ DO STARTU BEZ SILNIKA": "✓ HOME WITHIN GLIDE, ENGINE OFF",
    "START POZA ZASIĘGIEM ŚLIZGU": "HOME OUT OF GLIDE RANGE",
    "ODL.": "DIST", "KIER.": "BRG", "WYS.": "ALT", "Brak innych pilotów": "No other pilots",
    "w zasięgu FANET": "in FANET range", "Widać tylko urządzenia FANET": "Only FANET devices are shown",
    "Samoloty i szybowce z samym FLARM": "Aircraft and gliders with FLARM only", "nie są widoczne.": "are not visible.",
    "inny": "other", "paralotnia": "paraglider", "lotnia": "hang glider", "balon": "balloon", "szybowiec": "glider",
    "samolot": "aircraft", "śmigłowiec": "helicopter", "dron": "drone", "na ziemi": "on ground",
    "FANET wyłączony (Menu > FANET)": "FANET off (Menu > FANET)", "błąd radia": "radio error",
    "nadaję": "transmitting", "tylko odbiór": "receive only", "TAK": "YES", "NIE": "NO",
    "KURS↑": "TRK↑", "RUCH  godz. %d": "TFC  %d o'clock",
    "Czekam na GPS…": "Waiting for GPS…", "WYS. MSL": "ALT MSL", "NAJBLIŻSZA STREFA": "NEAREST AIRSPACE",
    "W STREFIE!": "INSIDE!", "pod strefą": "below", "nad strefą": "above",
    "wlot za %s": "entry in %s", "brak danych stref": "no airspace data", "brak stref": "no airspace",
    "(karta SD /airspace)": "(SD card /airspace)", "w promieniu 20 km": "within 20 km", "skala %g km": "scale %g km",
    "błąd odczytu mapy": "map read error", "brak mapy dla tej okolicy": "no map for this area",
    "brak map (karta SD /maps)": "no maps (SD card /maps)", "WYS": "ALT", "pod": "below", "nad": "above",
    "brak w pobliżu": "none nearby",
    "TRYB USB": "USB MODE", "Karta SD jako dysk USB": "SD card as a USB drive",
    "Komputer połączony – kopiuj pliki": "Computer connected – copy the files",
    "Podłącz kabel USB-C do komputera": "Connect a USB-C cable to a computer",
    "mapy podkładowe:  maps/<obszar>/…": "base maps:  maps/<area>/…",
    "strefy: airspace/    punkty: waypoints/": "airspace: airspace/    waypoints: waypoints/",
    "trasa: route/    teren: terrain/": "route: route/    terrain: terrain/",
    "Po skopiowaniu: wysuń (odmontuj) dysk w PC,": "When done: eject the drive on the computer,",
    "potem przytrzymaj KEY – powrót i wczytanie danych.": "or hold KEY – back to normal, data reloaded.",
    "Zapis lotu wstrzymany – nie używaj tego trybu w locie": "Flight log closed – do not use this mode in flight",
    "▲ na kursie": "▲ on course", "brak celu": "no target", "brak GPS": "no GPS",
    "CEL: START (Go Home)": "TARGET: HOME (Go Home)", "CEL: PUNKT": "TARGET: WAYPOINT",
    "CEL: NAJBLIŻSZE LĄDOWISKO": "TARGET: NEAREST LANDING", "CEL: PILOT (FANET)": "TARGET: PILOT (FANET)",
    "TRASA: punkt %d / %d": "ROUTE: point %d / %d", "ODLEGŁOŚĆ": "DISTANCE", "KIERUNEK": "BEARING",
    "CZAS DOLOTU": "ETE", "PRZYLOT (lokal.)": "ETA (local)", "PALIWO U CELU": "FUEL AT TARGET",
    "WYMAGANA L/D": "REQUIRED L/D", "DOLOT ŚLIZGIEM": "FINAL GLIDE", "BOOT długo = Go Home": "BOOT long = Go Home",
    "KOMIN: zysk / średnio": "THERMAL: gain / avg", "KOMIN (brak krążenia)": "THERMAL (not circling)",
    "WYS. / NAD TERENEM": "ALT / AGL", "wiatr %d km/h z %s": "wind %d km/h from %s", "wiatr": "wind",
    "● = noszenie": "● = lift", "Brak danych stref": "No airspace data",
    "Wgraj plik OpenAir (*.txt) do katalogu /airspace na karcie SD": "Copy an OpenAir file (*.txt) to /airspace on the SD card",
    "STREFA": "AIRSPACE", "KL.": "CL.", "DÓŁ – GÓRA": "FLOOR – CEILING", "W ŚRODKU": "INSIDE", "za ": "in ",
    "Brak stref w promieniu 20 km": "No airspace within 20 km", "Profil: brak GPS": "Profile: no GPS",
    "brak mapy terenu (/terrain/*.hgt)": "no terrain data (/terrain/*.hgt)",
    "ZAPAS CZASU LOTU": "ENDURANCE", "rezerwa": "reserve", "Zużyto: %.2f l": "Used: %.2f l",
    "licznik stoi": "counter stopped", "Zużycie": "Fuel flow", "Rezerwa": "Reserve", "Prędkość przelotowa": "Cruise speed",
    "Paliwo na powrót + rez.": "Fuel to home + res.", "Czas powrotu": "Time to home", "WRACAJ!": "TURN BACK!",
    "Margines powrotu": "Return margin", "PEŁNY BAK": "FULL TANK", "SILNIK: PRACUJE": "ENGINE: ON",
    "SILNIK: WYŁ.": "ENGINE: OFF",
    "BATERIA / GPS / BARO": "BATTERY / GPS / BARO", "Bateria": "Battery", "(ładowanie)": "(charging)",
    "Prąd": "Current", "Czas pracy (szac.)": "Runtime (est.)", "Pomiar baterii": "Battery gauge",
    "Moduł GPS": "GPS module", "Pozycja": "Position", "%u / błędy %u": "%u / errors %u", "Zdania NMEA": "NMEA sentences",
    "kabel": "cable", "brak – GPS": "none – GPS", "Barometr": "Barometer", "wyłączony": "off", "szukam…": "scanning…",
    "łączę…": "connecting…", "połączony": "connected", "błąd": "error", "czeka": "waiting", "odbiór": "receiving",
    "Telefon": "Phone", "(skalibr.)": "(calibr.)", "(domyśl.)": "(default)", "Wysokość": "Altitude",
    "Teren pod Tobą": "Terrain below you", "KARTA SD / DANE": "SD CARD / DATA", "%s  wolne %.0f MB": "%s  free %.0f MB",
    "BRAK!": "MISSING!", "Karta SD": "SD card", "%d (%d pkt)": "%d (%d pts)", "Strefy": "Airspace",
    "%d wpisów, %u": "%d entries, %u", "brak pliku": "no file", "Aktywacje": "Activations",
    "Punkty / trasa": "Waypoints / route", "brak map": "no maps", " obsz.": " areas", "Teren / mapa": "Terrain / map",
    "Zapis lotu": "Flight log", "nie": "no", "RAM / PSRAM wolne": "RAM / PSRAM free", "Wersja": "Version",
    "Z telefonu": "From phone", "KLAWISZ": "KEY", "= potwierdź": "= acknowledge", "PODSUMOWANIE": "SUMMARY",
    "LOT ZAKOŃCZONY": "FLIGHT COMPLETED", "Czas lotu": "Flight time", "Maks. wysokość": "Max altitude",
    "Maks. wznoszenie / opadanie": "Max climb / sink", "Przebyta trasa": "Distance flown",
    "Maks. odległość od startu": "Max distance from takeoff", "Zużyte paliwo (szac.)": "Fuel used (est.)",
    "Naciśnij klawisz, aby wrócić": "Press a key to return", "Odbieranie z telefonu:": "Receiving from phone:",
    "Odbieranie z telefonu: ": "Receiving from phone: ", "WYŁĄCZANIE…": "POWERING OFF…",
    "trzymaj oba przyciski jeszcze 2 s": "keep holding both buttons for 2 s", "puść, aby anulować": "release to cancel",
    "komputer motoparalotniowy  v": "paramotor flight computer  v",
}

MENU = {
    "POMOC › ": "HELP › ", "KLAWISZ: następna strona   BOOT: poprzednia   przytrzymaj: powrót":
        "KEY: next page   BOOT: previous   hold: back",
    " edycja": " edit", " pomoc str.": " help p.", "◄ wstecz": "◄ back", "ANULUJ (BOOT długo)": "CANCEL (BOOT long)",
    "ZAPISZ (KLAWISZ długo)": "SAVE (KEY long)",
    "KLAWISZ: dalej / przytrzymaj = wybierz     BOOT: wstecz / przytrzymaj = wyjdź":
        "KEY: next / hold = select     BOOT: back / hold = exit",
}

AIRSPACE = {"STAŁA": "PERM.", "AKTYWNA": "ACTIVE", "NIEAKT.": "INACT.", "AKT.?": "ACT.?"}

FILEXFER = {
    "przerwano (rozłączono)": "aborted (disconnected)", "błąd sumy kontrolnej": "checksum error",
    "nieznany typ pliku": "unknown file type",
    "brak stref w pliku (czy to format OpenAir?)": "no airspace in the file (is it OpenAir?)",
    "brak punktów w pliku (czy to SeeYou .cup?)": "no waypoints in the file (is it SeeYou .cup?)",
    "brak punktów trasy w pliku": "no route points in the file",
    "nie mogę zapisać pliku na karcie": "cannot write the file to the card",
}

TOPO = {
    ": zły info.txt": ": bad info.txt", ": brak lub zły plik": ": missing or bad file",
    "odczyt indeksu mapy": "map index read", "uszkodzony indeks mapy": "damaged map index",
    "odczyt kafelka mapy": "map tile read", "uszkodzony kafelek mapy (CRC)": "damaged map tile (CRC)",
}

FLIGHT = {"START": "HOME"}

BLUEFLY = {"za mało RAM (%u kB)": "not enough RAM (%u kB)",
           "błąd startu Bluetooth – wyłączony": "Bluetooth start failed – disabled"}

BOARD = {"brak odpowiedzi": "no response", "brak": "none", "BQ25896 (napięcie)": "BQ25896 (voltage)",
         "brak (55:%s 6B:%s)": "none (55:%s 6B:%s)", "jest": "yes", "nie": "no", "nic": "nothing"}

GPS = {"brak odpowiedzi": "no response"}

FANET = {"brak modułu LoRa": "no LoRa module", "błąd radia LoRa (%d)": "LoRa radio error (%d)",
         "błąd startu radia – FANET wyłączony": "radio start failed – FANET disabled"}

USB = {"brak karty SD": "no SD card", "USB: błąd startu": "USB: start error",
       "niedostępne (USB Mode = hwcdc)": "not available (USB Mode = hwcdc)"}

MAIN = {
    "Za mało pamięci na wszystkie dane z karty SD": "Not enough memory for all data on the SD card",
    "Strefy: brak danych w ": "Airspace: no data in ", "Mapy: ": "Maps: ",
    "Tryb USB niedostępny": "USB mode not available", "Tryb USB niedostępny w locie": "USB mode not available in flight",
    "wyląduj i zakończ lot": "land and end the flight", "Brak karty SD": "No SD card",
    "włóż kartę i uruchom ponownie": "insert a card and restart", "Trwa odbiór z telefonu": "Receiving from the phone",
    "poczekaj na koniec przesyłania": "wait until the transfer ends", "tryb USB: start": "USB mode: start",
    "Tryb USB: błąd": "USB mode: error", "tryb USB: koniec, ponowne wczytanie": "USB mode: end, reloading",
    "Karta SD nie odpowiada po trybie USB": "SD card not responding after USB mode",
    "TAK": "YES", "NIE": "NO", "Ustaw START tutaj": "Set HOME here", "Silnik pracuje": "Engine running",
    "Zakończ lot (zamknij IGC)": "End flight (close IGC)", "Lot": "Flight", "Paliwo na starcie": "Fuel at takeoff",
    "Zużycie": "Fuel flow", "Rezerwa": "Reserve", "Prędkość przelotowa": "Cruise speed",
    "Zatankowano do pełna": "Refuelled (full tank)", "Paliwo": "Fuel", "Wysokość teraz": "Altitude now",
    "Auto QNH (GPS/teren)": "Auto QNH (GPS/terrain)", "Kalibruj ponownie z GPS": "Recalibrate from GPS",
    "Wysokościomierz": "Altimeter", "Cel: START (Go Home)": "Target: HOME (Go Home)",
    "Cel: najbliższe lądowisko": "Target: nearest landing", "Trasa ": "Route ", "Trasa: następny punkt": "Route: next point",
    "Trasa: poprzedni punkt": "Route: previous point", "Zmień plik trasy": "Change route file",
    "Cel: punkt z listy": "Target: waypoint from list", "Nawigacja": "Navigation",
    "Doskonałość (bez silnika)": "Glide ratio (engine off)", "Prędkość ślizgu": "Glide speed",
    "Zapas wysokości": "Safety height", "Szybowanie": "Glide", "Strefa: ostrzeż przed": "Airspace: warn before",
    "Strefa: odległość": "Airspace: distance", "Strefa: bufor pionowy": "Airspace: vertical buffer",
    "Niska wysokość (0=wył.)": "Low altitude (0=off)", "Bateria": "Battery", "Alarmy": "Alarms",
    "Mapa: północ u góry": "Map: north up", "Mapa: skala (0-5)": "Map: scale (0-5)",
    "TOPO: skala (10-14)": "TOPO: zoom (10-14)", "Obróć ekran o 180°": "Rotate screen 180°",
    "Pełne odświeżanie co": "Full refresh every", "Strefa czasowa (UTC+)": "Time zone (UTC+)",
    "Podświetlenie": "Front light", "Jasność podświetlenia": "Front light brightness",
    "Podświetl. wyłącz po (0=nigdy)": "Light off after (0=never)", "Ekran i mapa": "Display and map",
    "FANET (radio LoRa)": "FANET (LoRa radio)", "Nadawaj moją pozycję": "Transmit my position",
    "Widoczny online (OGN)": "Visible online (OGN)", "Ostrzeżenia o ruchu": "Traffic warnings",
    "Ostrzeż: odległość": "Warn: distance", "Ostrzeż: różnica wysokości": "Warn: height difference",
    "FANET – inni piloci": "FANET – other pilots", "BlueFly: połącz ponownie": "BlueFly: reconnect",
    "Telefon (Bluetooth)": "Phone (Bluetooth)", "Wczytaj ponownie dane z SD": "Reload data from SD",
    "Tryb USB – karta SD jako dysk": "USB mode – SD card as a drive", "VCOM e-papieru (restart)": "E-paper VCOM (restart)",
    "TAK – zapisz lot i wyłącz": "YES – save flight and power off", "Wyłącz urządzenie": "Power off",
    "Pomoc": "Help", "RUCH! godz. %d, %d m": "TRAFFIC! %d o'clock, %d m", "  – zbliża się": "  – closing",
    "W STREFIE: ": "INSIDE: ", "do podstawy": "to floor", "do pułapu": "to ceiling",
    "wlot za %s  (%.1f km)": "entry in %s  (%.1f km)", "odległość %.1f km": "distance %.1f km",
    "STREFA: ": "AIRSPACE: ", "paliwo %.1f l, na powrót potrzeba %.1f l": "fuel %.1f l, return needs %.1f l",
    "WRACAJ – PALIWO!": "TURN BACK – FUEL!", "zapas na powrót tylko %d min": "return margin only %d min",
    "MAŁO PALIWA": "LOW FUEL", "%d m nad terenem": "%d m AGL", "NISKO!": "LOW!", "BRAK GPS": "NO GPS",
    "nawigacja i strefy nieaktualne": "navigation and airspace not updated", "BRAK BAROMETRU": "NO BAROMETER",
    "wysokość i vario z GPS (mniej dokładne)": "altitude and vario from GPS (less accurate)",
    "SŁABA BATERIA": "LOW BATTERY", "obliczenia 1 Hz": "1 Hz computation", " (w menu)": " (in menu)",
    "Nie mogę utworzyć pliku IGC": "Cannot create the IGC file", "KLAWISZ krótko": "KEY short",
    "KLAWISZ długo": "KEY long", "BOOT krótko": "BOOT short", "BOOT długo": "BOOT long", "dotyk": "touch",
    "oba krótko": "both short", "oba długo": "both long", "ekran ": "screen ", "po: ": "after: ",
    "rysowanie: ": "drawing: ", " (pełne)": " (full)", "Odłącz kabel USB": "Unplug the USB cable",
    "przy zasilaniu z USB wyłączenie nie jest możliwe": "the device cannot power off while on USB power",
    "WYŁĄCZONY": "POWERED OFF", "Włączanie: przycisk PWR lub podłączenie USB": "Power on: PWR button or USB",
    "Nie udało się wyłączyć": "Power off failed", "spróbuj ponownie lub odłącz baterię": "try again or disconnect the battery",
    "błąd programu (panic)": "software error (panic)", "zawieszenie (watchdog)": "hang (watchdog)",
    "spadek napięcia (bateria?)": "brownout (battery?)", "[!!] Poprzedni restart: ": "[!!] Last restart: ",
    "[!!] Ostatnio: ": "[!!] Last action: ", "Karta SD": "SD card",
    " – brak, dane i zapis lotu niedostępne": " – missing, no data and no flight log",
    "start: barometr": "start: barometer", "[OK] Barometr na kablu MS5611 (adres 0x%02X)": "[OK] Wired barometer MS5611 (address 0x%02X)",
    "[--] Barometr na kablu: brak (użyję BlueFly albo GPS)": "[--] Wired barometer: none (BlueFly or GPS will be used)",
    "start: skan I2C": "start: I2C scan", "start: bateria": "start: battery", "%s Bateria: %s %.2f V": "%s Battery: %s %.2f V",
    "start: wczytywanie danych z SD": "start: loading data from SD",
    "[OK] Strefy %d, punkty %d, trasa %d, teren %d, mapy %d": "[OK] Airspace %d, waypoints %d, route %d, terrain %d, maps %d",
    "Raport: ": "Report: ", "[..] GPS – wykrywanie modułu": "[..] GPS – detecting module", "praca": "running",
    "instalacja pliku z telefonu: ": "installing file from phone: ", "usunięto ": "deleted ",
    "stref": "airspaces", "aktywacji": "activations", "punktów": "waypoints", "punktów trasy": "route points",
    "BŁĄD: ": "ERROR: ",
}

FILES = {
    "lib/navcore/src/ui/screens.cpp": SCREENS, "lib/navcore/src/ui/menu.cpp": MENU,
    "lib/navcore/src/airspace.cpp": AIRSPACE, "lib/navcore/src/filexfer.cpp": FILEXFER,
    "lib/navcore/src/topo.cpp": TOPO, "lib/navcore/src/flight.cpp": FLIGHT, "src/bluefly_ble.cpp": BLUEFLY,
    "src/board.cpp": BOARD, "src/gps.cpp": GPS, "src/fanet_radio.cpp": FANET, "src/usb_disk.cpp": USB,
    "src/main.cpp": MAIN,
}

LIT = re.compile(r'"((?:[^"\\\n]|\\.)*)"')


def process(path, d):
    src = open(path, encoding="utf-8").read()
    out, i, n = [], 0, 0
    # walk the source, skipping comments and existing TR(...) calls
    pos = 0
    res = []
    while pos < len(src):
        if src.startswith("//", pos):
            e = src.find("\n", pos)
            e = len(src) if e < 0 else e
            res.append(src[pos:e])
            pos = e
            continue
        if src.startswith("/*", pos):
            e = src.find("*/", pos) + 2
            res.append(src[pos:e])
            pos = e
            continue
        if src.startswith("TR(", pos):
            # copy the whole TR( "a", "b" ) call
            m = re.compile(r'TR\(\s*"(?:[^"\\]|\\.)*"\s*,\s*"(?:[^"\\]|\\.)*"\s*\)').match(src, pos)
            if m:
                res.append(m.group(0))
                pos = m.end()
                continue
        if src[pos] == "'":
            m = re.compile(r"'(?:[^'\\]|\\.)*'").match(src, pos)
            if m:
                res.append(m.group(0))
                pos = m.end()
                continue
        if src[pos] == '"':
            m = LIT.match(src, pos)
            if m:
                s = m.group(1)
                line_start = src.rfind("\n", 0, pos) + 1
                if s in d and not src[line_start:pos].lstrip().startswith("#"):
                    en = d[s].replace('"', '\\"')
                    res.append(f'TR("{s}", "{en}")')
                    n += 1
                else:
                    res.append(m.group(0))
                pos = m.end()
                continue
        res.append(src[pos])
        pos += 1
    new = "".join(res)
    if "TR(" in new and '#include "i18n.h"' not in new and "i18n.h" not in new:
        rel = "../i18n.h" if "/ui/" in path else "i18n.h"
        # after the first #include line
        k = new.find("#include")
        k = new.find("\n", k) + 1
        new = new[:k] + f'#include "{rel}"\n' + new[k:]
    open(path, "w", encoding="utf-8").write(new)
    print(f"{path}: {n} texts")


if __name__ == "__main__":
    for p, d in FILES.items():
        process(p, d)
