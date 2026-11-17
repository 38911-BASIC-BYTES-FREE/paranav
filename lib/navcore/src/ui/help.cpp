#include "help.h"

namespace ui {

// Note: use only characters present in the bitmap fonts (see tools/gen_fonts.py).
static const std::vector<HelpTopic> kTopics = {
    {"Przyciski i dotyk",
     "KLAWISZ – przycisk na boku obudowy. BOOT – drugi przycisk. PWR – tylko włącza urządzenie (program go nie widzi).\n"
     "– KLAWISZ krótko: następny ekran.\n"
     "– KLAWISZ długo (0,7 s): menu.\n"
     "– BOOT krótko: poprzedni ekran.\n"
     "– BOOT długo: Go Home – cel = start, przejście na ekran NAWIGACJA.\n"
     "– Oba naraz krótko: podświetlenie wł./wył.\n"
     "– Oba naraz 3 s: wyłączenie. Po 1 s pojawia się napis WYŁĄCZANIE, puszczenie anuluje. Przy podłączonym USB "
     "wyłączenie nie działa.\n"
     "– Gdy widać alarm: dowolny krótki przycisk go potwierdza.\n"
     "W menu: KLAWISZ krótko – następna pozycja, długo – wybierz / zapisz. BOOT krótko – poprzednia pozycja, "
     "długo – poziom wyżej / wyjście. Przy zmianie liczby: KLAWISZ krótko = plus, BOOT krótko = minus.\n"
     "W pomocy: KLAWISZ – następna strona, BOOT – poprzednia, długie naciśnięcie – powrót do listy.\n"
     "Dotyk (bez rękawic): pasek u góry – menu; lewa krawędź – poprzedni ekran; prawa krawędź – następny. "
     "MAPA: dotyk mapy zmienia skalę. TOPO: dotyk górnej połowy mapy – przybliż, dolnej – oddal. NAWIGACJA: dotyk zmienia cel (start, trasa, lądowisko). "
     "PALIWO: przyciski na dole ekranu. Alarm: dotknięcie górnej części ekranu go potwierdza."},

    {"Pasek stanu",
     "Od lewej:\n"
     "– Nazwa ekranu i kropki – który to ekran (pełna kropka = bieżący).\n"
     "– Zegar – czas lokalny z GPS (strefa czasowa w menu Ekran i mapa). --:-- = brak czasu z GPS.\n"
     "– ▲ h:mm – czas lotu, tylko w powietrzu.\n"
     "– Czarny kwadrat z ! – jest aktywny alarm (również już potwierdzony).\n"
     "– Czarny kwadrat ze słońcem – podświetlenie włączone. W dzień wyłącz je, zużywa baterię.\n"
     "– TEL – połączony telefon z aplikacją ParaNav.\n"
     "– BF – połączony BlueFly. W ramce: połączony, ale wysokość liczona z barometru na kablu. Białe na czarnym: "
     "wysokość i vario z BlueFly.\n"
     "– ● REC – trwa zapis lotu (IGC + GPX).\n"
     "– GPS n – liczba satelitów. Napis GPS w negatywie = brak pozycji.\n"
     "– ⚡ – bateria się ładuje.\n"
     "– Procent i ikona baterii. ? = brak odczytu baterii (szczegóły na ekranie SYSTEM)."},

    {"Ekran LOT",
     "Główny ekran w locie.\n"
     "– VARIO: pasek po lewej (+/- 5 m/s, wznoszenie pełne, opadanie kreskowane), wartość chwilowa i średnia z 30 s.\n"
     "– WYSOKOŚĆ MSL: z barometru. Dopisek BlueFly = z BlueFly przez Bluetooth, (GPS) = brak barometru, wysokość "
     "i vario z GPS (mniej dokładne, vario opóźnione).\n"
     "– nad startem; nad terenem – wymaga mapy terenu na karcie.\n"
     "– PRĘDKOŚĆ GS – względem ziemi; KURS – kierunek lotu nad ziemią.\n"
     "– START: odległość, strzałka do startu względem kursu (góra = przed Tobą), czas dolotu na silniku z wiatrem "
     "i namiar.\n"
     "– WIATR: siła i kierunek, z którego wieje. Pusta strzałka pokazuje, dokąd wieje, względem Twojego kursu. "
     "Pomiar wymaga zakrętu o co najmniej 200°. Liczba minut w nawiasie = wiek pomiaru.\n"
     "– PALIWO: zapas czasu, litry i zapas na powrót. Czarny pasek WRACAJ = paliwa nie starczy na powrót z rezerwą.\n"
     "– DOSKONAŁOŚĆ L/D: zmierzona tylko przy stałym opadaniu, inaczej ––. biegun. = wartość z ustawień. "
     "dolot do startu = ile metrów zapasu zostanie nad startem po ślizgu bez silnika (z wiatrem i zapasem bezpieczeństwa). "
     "✓ = dolecisz; czarny pasek = start poza zasięgiem ślizgu."},

    {"Ekran MAPA",
     "Mapa ruchoma: Twój kurs u góry (albo północ – Menu > Ekran i mapa). Strzałka w środku to Ty.\n"
     "– Strefy: gruby kontur – zakazane, ograniczone, niebezpieczne i CTR; cienki – pozostałe; przerywany – nieaktywne.\n"
     "– Kreskowanie = strefy aktywne i istotne na Twojej wysokości: kratka – P, R, Q; gęste ukośne – CTR, TRA, TSA; "
     "rzadkie ukośne – pozostałe. Strefy wysoko nad Tobą lub pod Tobą nie są kreskowane.\n"
     "– Linia ciągła – ślad lotu. Przerywany okrąg – zasięg ślizgu bez silnika (wydłużony z wiatrem).\n"
     "– Domek – start. Linia przerywana – kierunek do celu. Kółko – punkt, kwadrat – lądowisko.\n"
     "– N w kółku – gdzie jest północ. Pusta strzałka z liczbą – wiatr (km/h). Pasek w rogu – skala.\n"
     "Panel z prawej: wysokość, vario, prędkość i NAJBLIŻSZA STREFA – nazwa, klasa, dół – góra, odległość ze "
     "strzałką, W STREFIE!, pod/nad strefą (ile metrów), czas do wlotu i status aktywacji. "
     "Dotyk mapy zmienia skalę: 1, 2, 5, 10, 20, 50 km."},

    {"Ekran TOPO",
     "Mapa topograficzna z karty SD (katalog /maps), zawsze północą do góry. Strzałka w kółku – Ty i Twój kurs.\n"
     "– Mapa przesuwa się skokami: gdy odlecisz od środka o ok. 1/3 ekranu, widok ustawia się od nowa, z większym "
     "kawałkiem mapy przed Tobą. Dzięki temu e-papier nie przerysowuje całej mapy co sekundę.\n"
     "– Znaki mapy: poziome kreski – woda i morze; kropki – las; kratka – zabudowa; linia z poprzecznymi kreskami "
     "i kropkami – linia wysokiego napięcia (cieńsza, bez kropek – średnie napięcie); trójnóg w kółku – wiatrak; "
     "daszek z kropką – maszt, wieża, komin (liczba = wysokość w m); kółko z kreską – lotnisko lub lądowisko; "
     "czarne pasy – pasy startowe; czarne linie – drogi; podwójna linia – rzeka; czarno-biała linia – kolej.\n"
     "– Na mapie: kontury stref (krótkie kreski, przerywane cienkie – nieaktywne), punkty i lądowiska, ślad, "
     "domek – START, długie kreski – linia do celu.\n"
     "– Rogi: wysokość i AGL, prędkość i kurs, vario i wiatr (pusta strzałka – dokąd wieje), najbliższa strefa "
     "(strzałka – kierunek, północ u góry).\n"
     "– Na dole skala i poziom mapy z10 (ok. 90 km szerokości ekranu) … z14 (ok. 6 km). Dotyk górnej połowy mapy "
     "– przybliż, dolnej – oddal. Kropki w regularnej siatce = brak mapy dla tego miejsca.\n"
     "Mapa jest pomocą orientacyjną, nie mapą lotniczą – przeszkody mogą być niekompletne."},

    {"Ekran NAWIGACJA",
     "Prowadzenie do celu. Cel wybierasz w Menu > Nawigacja albo dotykiem tego ekranu. BOOT długo = cel START.\n"
     "– Róża kompasu obraca się z kursem; trójkąt u góry to Twój kurs.\n"
     "– Duża strzałka – kierunek do celu. W lewym dolnym rogu: o ile stopni skręcić (◄ w lewo, ► w prawo).\n"
     "– ODLEGŁOŚĆ, KIERUNEK (namiar), CZAS DOLOTU (na silniku, z wiatrem), PRZYLOT (czas lokalny).\n"
     "– PALIWO U CELU – ile zostanie po dolocie.\n"
     "– WYMAGANA L/D – doskonałość potrzebna, żeby dolecieć ślizgiem; DOLOT ŚLIZGIEM – zapas (+) lub brak (-) wysokości "
     "nad celem bez silnika.\n"
     "Trasa: punkty przełączają się same, gdy podlecisz bliżej niż 200 m."},

    {"Ekran TERMIKA",
     "Do lotu z wyłączonym silnikiem.\n"
     "– Duże vario, średnia z 30 s i pasek.\n"
     "– KOMIN: zysk wysokości i średnie wznoszenie od początku krążenia (pojawia się, gdy krążysz).\n"
     "– Wysokość i wysokość nad terenem, wiatr.\n"
     "– Z prawej ślad ostatnich 90 s (kurs u góry): czarne kropki – wznoszenie, większa kropka = silniejsze noszenie; "
     "puste – opadanie. Ø – średnica widoku. Pusta strzałka – kierunek wiatru."},

    {"Ekran STREFY",
     "Tabela do 7 najbliższych stref w promieniu 20 km.\n"
     "– Kolumny: nazwa, klasa, dół – góra, odległość ze strzałką (albo pod/nad i ile metrów, gdy jesteś w jej obrysie), "
     "status.\n"
     "– Status: STAŁA, AKTYWNA, NIEAKT., AKT.? – brak informacji o aktywacji (traktowana jak aktywna), "
     "za m:ss – przewidywany czas do wlotu.\n"
     "– Wiersz w ramce – ostrzeżenie. Wiersz w negatywie – jesteś w środku strefy.\n"
     "Profil u dołu: 10 km przed Tobą wzdłuż kursu. Kropkowane pole – teren, kreskowane prostokąty – strefy, "
     "trójkąt – Twoja wysokość, linia przerywana – ślizg bez silnika."},

    {"Ekran PALIWO",
     "– Duża liczba – zapas czasu lotu (h:mm). Pod nią litry i pasek z zaznaczoną rezerwą.\n"
     "– Z prawej: zużycie, rezerwa, prędkość przelotowa, paliwo potrzebne na powrót z rezerwą, czas powrotu i "
     "margines. WRACAJ! = margines ujemny.\n"
     "– Przyciski dotykowe: -0,5 l / +0,5 l – paliwo na starcie; PEŁNY BAK – zeruje zużycie; SILNIK – gdy wyłączony, "
     "licznik paliwa stoi.\n"
     "Zużycie liczone jest z czasu pracy silnika i ustawionego zużycia l/h – nie ma czujnika paliwa. "
     "Przed lotem ustaw paliwo w Menu > Paliwo."},

    {"Ekran SYSTEM",
     "Stan urządzenia, przydatny przy problemach.\n"
     "– Bateria, prąd, czas pracy, źródło pomiaru baterii.\n"
     "– GPS: moduł, fix, satelity, pozycja, liczba odebranych zdań NMEA.\n"
     "– Barometr: skąd jest wysokość (kabel, BlueFly, brak – GPS) i ciśnienie. BlueFly: stan połączenia, bateria "
     "BlueFly i siła sygnału (dBm, im bliżej zera, tym lepiej).\n"
     "– QNH (skalibr. = ustawione), wysokość z GPS i z barometru, teren pod Tobą.\n"
     "– Telefon: wyłączony / czeka / połączony / odbiór (procent). Z telefonu: wynik ostatniego przesłania.\n"
     "– Karta SD, liczba stref, aktywacji, punktów, kafli terenu i mapa TOPO dla Twojej okolicy, plik zapisu lotu, "
     "wolna pamięć.\n"
     "– Dotyk (test) – współrzędne ostatniego dotknięcia. I2C – adresy wykrytych układów.\n"
     "– Na dole błędy wczytywania plików."},

    {"Alarmy",
     "Alarm to czarny pasek u góry ekranu. Od najważniejszego:\n"
     "– W STREFIE – jesteś w aktywnej strefie.\n"
     "– WRACAJ – PALIWO! – paliwa nie starczy na powrót z rezerwą.\n"
     "– STREFA – zbliżasz się do strefy (w poziomie lub w pionie).\n"
     "– NISKO! – za nisko nad terenem (gdy włączone w ustawieniach).\n"
     "– MAŁO PALIWA – zapas na powrót mniejszy niż 10 min.\n"
     "– BRAK GPS, BRAK BAROMETRU (barometr odłączony w locie – wysokość z GPS), SŁABA BATERIA.\n"
     "Krótkie naciśnięcie przycisku potwierdza alarm. Wraca, gdy sytuacja minie i się powtórzy; W STREFIE i WRACAJ "
     "przypominają się co 2 minuty. Alarmy działają w locie (bateria – zawsze). Progi: Menu > Alarmy."},

    {"Barometr i BlueFly",
     "Wysokość i vario urządzenie bierze automatycznie z pierwszego działającego źródła:\n"
     "– 1. Barometr MS5611 na kablu QWIIC. Można go podłączyć także po włączeniu – urządzenie sprawdza go co 10 s.\n"
     "– 2. BlueFlyVario przez Bluetooth (BLE). Wystarczy włączyć BlueFly – urządzenie samo go znajdzie i połączy, "
     "także po zerwaniu połączenia. Pozycja GPS z BlueFly nie jest używana, tylko wewnętrzny GPS.\n"
     "– 3. Bez barometru: wysokość i vario z GPS (mniej dokładne).\n"
     "Przy zmianie źródła vario startuje od nowa, bez fałszywego skoku. Jeśli barometr zniknie w locie, pojawia się "
     "alarm BRAK BAROMETRU.\n"
     "BlueFly połączony z tym urządzeniem nie połączy się w tym samym czasie z telefonem (np. XCTrack). Dźwięk vario "
     "BlueFly działa zawsze, niezależnie od połączenia.\n"
     "Menu > System: BlueFly wł./wył. (wyłączony Bluetooth oszczędza baterię) i BlueFly: połącz ponownie."},

    {"Telefon – wysyłanie plików",
     "Aplikacja ParaNav na Androida wysyła pliki przez Bluetooth – bez wyjmowania karty SD.\n"
     "– W aplikacji: Połącz, potem wybierz plik z telefonu: strefy (OpenAir .txt), punkty lub trasę (.cup). "
     "Aktywacje stref na dziś wpisujesz w aplikacji.\n"
     "– Opcja Zastąp usuwa poprzednie pliki tego rodzaju (strefy: wszystkie .txt oprócz aktywacji).\n"
     "– Urządzenie sprawdza plik (suma kontrolna, czy są w nim strefy lub punkty) i dopiero wtedy go zapisuje. "
     "Zły plik nie nadpisze dobrego.\n"
     "– W czasie odbioru u dołu ekranu widać pasek postępu, na pasku stanu napis TEL.\n"
     "– Wyłączenie: Menu > System > Telefon (Bluetooth). Każdy telefon z aplikacją w pobliżu może się połączyć, "
     "więc jeśli nie używasz, wyłącz."},

    {"Pliki na karcie SD",
     "Karta microSD w formacie FAT32. Katalogi:\n"
     "– /airspace – strefy w formacie OpenAir, pliki .txt (wczytywane są wszystkie).\n"
     "– /airspace/activations.txt – aktywacje stref na dziś. Pierwsza linia date=RRRR-MM-DD, potem np. "
     "EPTR12 0800-1500 (czas UTC, można podać kilka przedziałów).\n"
     "– /waypoints – punkty, pliki .cup (SeeYou). Styl 2-5 = lądowisko.\n"
     "– /route – trasa, plik .cup; kolejność punktów w pliku = kolejność trasy.\n"
     "– /terrain – mapa terenu, kafle .hgt (mogą być w podkatalogach). N52E021.hgt = 52-53°N, 21-22°E.\n"
     "– /maps – mapy podkładowe dla ekranu TOPO, każdy obszar w osobnym katalogu (info.txt i pliki z10.pak … z14.pak). "
     "Kopiuj cały katalog maps z programu do przygotowania map.\n"
     "– /logs – tu urządzenie zapisuje loty (.igc i .gpx).\n"
     "– /config.ini – ustawienia, zapisywane przez urządzenie.\n"
     "Po wymianie plików: Menu > System > Wczytaj ponownie dane z SD (albo uruchom ponownie)."},

    {"Mapy i tryb USB",
     "Mapy podkładowe przygotowuje się na komputerze z danych OpenStreetMap (szczegóły w README). Na kartę można je "
     "wgrać na dwa sposoby:\n"
     "– Wyjmij kartę i skopiuj katalog maps czytnikiem kart.\n"
     "– Albo bez wyjmowania: Menu > System > Tryb USB – karta SD jako dysk. Podłącz kabel USB-C do komputera – "
     "karta pojawi się jako dysk. Skopiuj pliki, potem wysuń (odmontuj) dysk w komputerze. Urządzenie samo "
     "wróci do pracy i wczyta dane od nowa. Powrót bez wysuwania: KLAWISZ lub BOOT długo – ale najpierw "
     "poczekaj, aż komputer skończy kopiowanie.\n"
     "W trybie USB zapis lotu jest zamknięty, a telefon nie może wysyłać plików. Tryb nie działa w locie.\n"
     "Kilka obszarów na karcie: urządzenie samo wybiera ten, który obejmuje Twoją pozycję (najdokładniejszy)."},

    {"Start, lądowanie, zapis",
     "– Start wykrywany jest sam: prędkość powyżej 15 km/h przez 10 s. Miejsce postoju przed startem staje się "
     "punktem START. Rusza zapis lotu, czas lotu i licznik paliwa.\n"
     "– START możesz ustawić ręcznie: Menu > Lot > Ustaw START tutaj.\n"
     "– Lądowanie: prędkość poniżej 5 km/h przez 60 s. Zapis jest zamykany i pokazuje się podsumowanie lotu.\n"
     "– Wysokościomierz kalibruje się sam na ziemi po złapaniu co najmniej 6 satelitów (Menu > Wysokościomierz).\n"
     "– Wyłączenie (oba przyciski 3 s albo menu) również zamyka i zapisuje lot."},
};

const std::vector<HelpTopic>& helpTopics() { return kTopics; }

}  // namespace ui
