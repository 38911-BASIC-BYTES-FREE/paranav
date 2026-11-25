#include "help.h"

#include "../i18n.h"

namespace ui {

// Note: use only characters present in the bitmap fonts (see tools/gen_fonts.py).
static const std::vector<HelpTopic> kTopicsPl = {
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
     "Język / Language: Menu > Ekran i mapa.\n"
     "Dotyk (bez rękawic): pasek u góry – menu; lewa krawędź – poprzedni ekran; prawa krawędź – następny. "
     "MAPA: dotyk mapy zmienia skalę. TOPO: dotyk górnej połowy mapy – przybliż, dolnej – oddal. RUCH: dotyk pilota na liście – leć do niego. NAWIGACJA: dotyk zmienia cel (start, trasa, lądowisko). "
     "PALIWO: przyciski na dole ekranu. Alarm: dotknięcie górnej części ekranu go potwierdza.\n"
     "Menu dotykiem: dotknij pozycji, aby ją wybrać. Długie listy przewijasz dużymi przyciskami ▲ / ▼ po prawej "
     "(o stronę). Pasek tytułu – poziom wyżej."},

    {"Pasek stanu",
     "Od lewej:\n"
     "– Nazwa ekranu i kropki – który to ekran (pełna kropka = bieżący).\n"
     "– Zegar – czas lokalny z GPS (strefa czasowa w menu Ekran i mapa). --:-- = brak czasu z GPS.\n"
     "– ▲ h:mm – czas lotu, tylko w powietrzu.\n"
     "– Czarny kwadrat z ! – jest aktywny alarm (również już potwierdzony).\n"
     "– Czarny kwadrat ze słońcem – podświetlenie włączone. W dzień wyłącz je, zużywa baterię.\n"
     "– TEL – połączony telefon z aplikacją ParaNav.\n"
     "– FN n – tylu innych pilotów słychać przez FANET. Białe na czarnym – ktoś jest niebezpiecznie blisko.\n"
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
     "– Bez sygnału GPS mapa pokazuje okolicę ostatniej znanej pozycji (zapamiętanej także po wyłączeniu), z "
     "czarnym napisem BRAK SYGNAŁU GPS i przerywanym kółkiem zamiast strzałki. To nie jest aktualna pozycja; strefy "
     "nie są wtedy rysowane.\n"
     "Mapa jest pomocą orientacyjną, nie mapą lotniczą – przeszkody mogą być niekompletne."},
    {"Zapisane loty",
     "Menu > Zapisane loty: lista lotów z karty SD (pliki IGC w katalogu logs/), najnowsze na górze – data, godzina "
     "startu i czas lotu. Wybierz lot, aby go obejrzeć (po lądowaniu; w locie lista się nie otwiera).\n"
     "– Po lewej dane lotu: czas, długość śladu, średnia prędkość, przewyższenie i wartości maksymalne (wysokość, "
     "odległość od startu, wznoszenie i opadanie z 10 s, prędkość).\n"
     "– Mapa: ślad na mapie z karty (białe tło, gdzie mapy nie ma). Kropka – start, kwadrat – lądowanie. "
     "Przyciski + i – albo KLAWISZ / BOOT krótko: przybliż / oddal; CAŁY – cały lot. Dotyk mapy przesuwa ten punkt "
     "na środek.\n"
     "– Na dole profil wysokości (kreskowanie – teren, gdy są pliki terenu). Dotyk profilu zaznacza chwilę lotu: "
     "kółko na mapie i pod profilem godzina, wysokość, wysokość nad terenem, vario i prędkość.\n"
     "– USUŃ (albo przytrzymaj BOOT) – pytanie, potem usunięcie plików IGC i GPX z karty. ZAMKNIJ (albo "
     "przytrzymaj KLAWISZ) – powrót.\n"
     "Wysokość pochodzi z barometru wyrównanego do GPS; bez barometru – z GPS."},

    {"FANET – inni piloci",
     "Wbudowane radio LoRa 868 MHz nadaje Twoją pozycję w otwartym systemie FANET i odbiera innych pilotów. "
     "Widzisz urządzenia FANET (Skytraxx, XC Tracer, Naviter, Burnair, Leaf, GXAirCom…). Samolotów i szybowców "
     "z samym FLARM nie widać – i one nie widzą Ciebie.\n"
     "– Nadajesz jako paralotnia, co 5 s (w tłumie rzadziej). Stacje OGN mogą pokazać Cię w internecie "
     "(np. glidernet.org) – wyłączysz to w Menu > FANET > Widoczny online.\n"
     "– MAPA i TOPO: strzałka = pilot i jego kierunek, liczba = różnica wysokości (+ nad Tobą), ↑↓ = wznosi się / "
     "opada. Pusta strzałka – brak danych od 20 s. Kółko z kwadracikiem – ktoś na ziemi. Gruby okrąg – zagrożenie.\n"
     "– Ekran RUCH: lista (odległość, kierunek względem kursu, różnica wysokości, vario) i radar kursem do góry. "
     "Dotknij pilota – staje się celem nawigacji (leć do kolegi).\n"
     "– Na innych ekranach: ramka RUCH z prawej u góry – ktoś bliżej niż próg ostrzeżenia albo zbliża się "
     "(do 45 s). Pasek RUCH! – połowa progu albo za mniej niż 20 s. Progi: Menu > FANET.\n"
     "To pomoc, nie system antykolizyjny: pozycje przychodzą co ok. 5 s, a antena jest wewnętrzna – zasięg zależy "
     "od położenia urządzenia. Zawsze patrz w powietrze."},

    {"Ekran NAWIGACJA",
     "Prowadzenie do celu. Cel wybierasz w Menu > Nawigacja albo dotykiem tego ekranu. BOOT długo = cel START.\n"
     "– Róża kompasu obraca się z kursem; trójkąt u góry to Twój kurs.\n"
     "– Duża strzałka – kierunek do celu. W lewym dolnym rogu: o ile stopni skręcić (◄ w lewo, ► w prawo).\n"
     "– ODLEGŁOŚĆ, KIERUNEK (namiar), CZAS DOLOTU (na silniku, z wiatrem), PRZYLOT (czas lokalny).\n"
     "– PALIWO U CELU – ile zostanie po dolocie.\n"
     "– WYMAGANA L/D – doskonałość potrzebna, żeby dolecieć ślizgiem; DOLOT ŚLIZGIEM – zapas (+) lub brak (-) wysokości "
     "nad celem bez silnika.\n"
     "Trasa: punkty przełączają się same, gdy podlecisz bliżej niż 200 m.\n"
     "Wybór trasy: Menu > Nawigacja > Wybierz trasę – wszystkie trasy z karty z liczbą punktów i długością "
     "(● = aktywna). Wybrana trasa zostaje zapamiętana. Na MAPIE i TOPO trasa to linia z numerowanymi kółkami "
     "(kwadrat = lądowisko, czarny = punkt, do którego lecisz). Trasy planujesz na mapie w aplikacji na telefon."},

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
     "– Barometr: skąd jest wysokość (kabel, BlueFly, brak – GPS) i ciśnienie; przy kablu F a/b = ile pełnych "
     "odświeżeń ekranu filtr przeczekał / ile odczytów odrzucił jako skoki. BlueFly: stan połączenia, bateria "
     "BlueFly i siła sygnału (dBm, im bliżej zera, tym lepiej).\n"
     "– QNH (skalibr. = ustawione), wysokość z GPS i z barometru, teren pod Tobą.\n"
     "– Telefon: wyłączony / czeka / połączony / odbiór (procent). Z telefonu: wynik ostatniego przesłania.\n"
     "– Karta SD, liczba stref, aktywacji, punktów, kafli terenu i mapa TOPO dla Twojej okolicy, plik zapisu lotu, "
     "wolna pamięć.\n"
     "– FANET: Twój numer (ID), liczba wysłanych (tx) i odebranych (rx) pakietów albo błąd radia. "
     "I2C – adresy wykrytych układów.\n"
     "– Na dole błędy wczytywania plików."},

    {"Alarmy",
     "Alarm to czarny pasek u góry ekranu. Od najważniejszego:\n"
     "– RUCH! – inny pilot FANET bardzo blisko albo za chwilę będzie (kierunek jak na zegarze: godz. 12 = przed "
     "Tobą, 3 = z prawej).\n"
     "– W STREFIE – jesteś w aktywnej strefie.\n"
     "– WRACAJ – PALIWO! – paliwa nie starczy na powrót z rezerwą.\n"
     "– STREFA – zbliżasz się do strefy (w poziomie lub w pionie).\n"
     "– NISKO! – za nisko nad terenem (gdy włączone w ustawieniach).\n"
     "– MAŁO PALIWA – zapas na powrót mniejszy niż 10 min.\n"
     "– BRAK GPS, BRAK BAROMETRU (barometr odłączony w locie – wysokość z GPS), SŁABA BATERIA.\n"
     "Krótkie naciśnięcie przycisku potwierdza alarm. Wraca, gdy sytuacja minie i się powtórzy; W STREFIE i WRACAJ "
     "przypominają się co 2 minuty. Alarmy działają w locie (bateria – zawsze). Progi: Menu > Alarmy."},

    {"Wyjście ze strefy",
     "Gdy wlecisz w aktywną strefę (te same, które dają alarm W STREFIE), alarm ma dwa przyciski:\n"
     "– WYPROWADŹ ZE STREFY (KLAWISZ albo dotknięcie lewego przycisku) – ekran WYJŚCIE pokazuje najszybszą drogę "
     "na zewnątrz, niezależnie od celu, trasy i Go Home.\n"
     "– IGNORUJ (BOOT albo prawy przycisk) – alarm wróci za 2 min, jeśli nadal jesteś w strefie.\n"
     "Urządzenie porównuje trzy drogi i wybiera najszybszą: w poziomie (kierunek co 5°, przez sąsiednie strefy aż "
     "do wolnej przestrzeni, z czasem zakrętu), w górę ponad pułap (także nad strefę leżącą wyżej) albo w dół pod "
     "podstawę (nie bliżej niż 60 m nad terenem; strefa od ziemi – w dół się nie da).\n"
     "Strzałka pokazuje kierunek względem Twojego kursu; przy wyjściu pionowym – duża strzałka WZNOŚ SIĘ / ZNIŻAJ "
     "SIĘ z wysokością docelową. Po prawej czas i dystans wszystkich trzech możliwości.\n"
     "10 s po opuszczeniu strefy ekran sam się zamyka. Zamknięcie ręczne: przytrzymaj KLAWISZ. Bez alarmu: Menu > "
     "Nawigacja > Wyprowadź ze strefy.\n"
     "Wznoszenie i opadanie do obliczeń: Menu > Alarmy (domyślnie 1,5 i 2 m/s).\n"
     "Limit wysokości: Menu > Alarmy > Maks. wysokość FL (domyślnie FL95, 0 = bez limitu). Wyjście w górę jest "
     "proponowane tylko wtedy, gdy kończy się co najmniej 30 m poniżej limitu – inaczej przy \"w górę\" widać "
     "\"ponad FL95\". Limit jest liczony z aktualnego QNH.\n"
     "Kontrola trasy: po wybraniu trasy (w urządzeniu lub z telefonu) ParaNav sprawdza, czy przecina strefy z "
     "podstawą poniżej wysokości lotu z Menu > Alarmy (domyślnie 1000 m nad terenem, nie wyżej niż limit FL) "
     "i pokazuje listę. "
     "Ponownie: Menu > Nawigacja > Sprawdź trasę."},

    {"Barometr i BlueFly",
     "Wysokość i vario urządzenie bierze automatycznie z pierwszego działającego źródła:\n"
     "– 1. Barometr MS5611 na kablu QWIIC. Można go podłączyć także po włączeniu – urządzenie sprawdza go co 10 s.\n"
     "– 2. BlueFlyVario przez Bluetooth (BLE). Wystarczy włączyć BlueFly – urządzenie samo go znajdzie i połączy, "
     "także po zerwaniu połączenia. Pozycja GPS z BlueFly nie jest używana, tylko wewnętrzny GPS.\n"
     "– 3. Bez barometru: wysokość i vario z GPS (mniej dokładne).\n"
     "Przy zmianie źródła vario startuje od nowa, bez fałszywego skoku. Jeśli barometr zniknie w locie, pojawia się "
     "alarm BRAK BAROMETRU.\n"
     "Barometr na kablu: pełne odświeżenie ekranu zakłóca jego odczyt (skok ok. 2 m). Na ten czas i 1 s po nim vario "
     "nie bierze odczytów i liczy dalej z ostatniego wznoszenia. Pojedyncze skoki o ponad 1,5 m też są odrzucane.\n"
     "BlueFly połączony z tym urządzeniem nie połączy się w tym samym czasie z telefonem (np. XCTrack). Dźwięk vario "
     "BlueFly działa zawsze, niezależnie od połączenia.\n"
     "Menu > System: BlueFly wł./wył. (wyłączony Bluetooth oszczędza baterię) i BlueFly: połącz ponownie."},

    {"Telefon – wysyłanie plików",
     "Aplikacja ParaNav na Androida wysyła pliki przez Bluetooth – bez wyjmowania karty SD.\n"
     "– W aplikacji: Połącz, potem wybierz plik z telefonu: strefy (OpenAir .txt), punkty lub trasę (.cup). "
     "Aktywacje stref na dziś wpisujesz w aplikacji.\n"
     "– Trasy można planować w aplikacji na mapie (OpenTopoMap, ze strefami i punktami) i wysłać jako aktywne.\n"
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
     "– /logs – tu urządzenie zapisuje loty (.igc i .gpx) oraz diagnostykę: restarts.txt (każde uruchomienie "
     "z powodem) i crash-NN.txt (raport każdego nieoczekiwanego restartu).\n"
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

    {"Zasilanie USB i ładowanie",
     "Po podłączeniu USB urządzenie pyta, w jakim trybie pracować:\n"
     "– NAWIGACJA – normalna praca, np. z powerbankiem.\n"
     "– ŁADOWANIE – tylko ekran baterii; nawigacja wstrzymana, FANET i BlueFly wyłączone, ekran odświeża się "
     "tylko przy zmianie procentów.\n"
     "– PLIKI – karta SD jako dysk (tylko gdy po drugiej stronie kabla jest komputer).\n"
     "Wybór: dotknij przycisku albo KLAWISZ krótko – następny, długo – wybierz. Bez odpowiedzi po 15 s: "
     "ŁADOWANIE, jeśli urządzenie włączyło się od podłączenia USB; NAWIGACJA, jeśli już pracowało.\n"
     "W locie pytanie się nie pojawia – nawigacja działa dalej. Po restarcie z powodu błędu urządzenie od razu "
     "wraca do nawigacji.\n"
     "Wyjście z ładowania: dotknij ekranu, przytrzymaj KLAWISZ albo odłącz USB – wtedy rusza nawigacja. "
     "Ręcznie: Menu > System > Tryb ładowania."},

    {"Start, lądowanie, zapis",
     "– Start wykrywany jest sam: prędkość powyżej 15 km/h przez 10 s. Miejsce postoju przed startem staje się "
     "punktem START. Rusza zapis lotu, czas lotu i licznik paliwa.\n"
     "– START możesz ustawić ręcznie: Menu > Lot > Ustaw START tutaj.\n"
     "– Lądowanie: po 60 s bez ruchu (poniżej 5 km/h, vario do 0,5 m/s) pojawia się pytanie LĄDOWANIE?. KLAWISZ – nadal lecę "
     "(nie pytam przez 5 min), BOOT – wylądowałem. Bez odpowiedzi lot trwa; jeśli bezruch trwa dalej, lot kończy się "
     "sam po 4 min. Wyżej niż 50 m nad terenem pytanie się nie pokazuje (wiszenie pod wiatr). Po lądowaniu zapis jest "
     "zamykany i pokazuje się podsumowanie; czas lotu liczony jest do chwili zatrzymania.\n"
     "– Wysokościomierz kalibruje się sam na ziemi po złapaniu co najmniej 6 satelitów (Menu > Wysokościomierz).\n"
     "– Wyłączenie (oba przyciski 3 s albo menu) również zamyka i zapisuje lot."},
};

static const std::vector<HelpTopic> kTopicsEn = {
    {"Buttons and touch",
     "KEY – the button on the side of the case. BOOT – the second button. PWR – power-on only (not visible to the software).\n"
     "– KEY short: next screen.\n"
     "– KEY long (0.7 s): menu.\n"
     "– BOOT short: previous screen.\n"
     "– BOOT long: Go Home – target = HOME, switches to the NAV screen.\n"
     "– Both short: front light on/off.\n"
     "– Both for 3 s: power off. After 1 s POWERING OFF appears, releasing cancels. Power off does not work while "
     "USB is connected.\n"
     "– When an alert is shown: any short press acknowledges it.\n"
     "In the menu: KEY short – next item, long – select / save. BOOT short – previous item, long – up one level / "
     "exit. When editing a number: KEY short = plus, BOOT short = minus.\n"
     "In help: KEY – next page, BOOT – previous, long press – back to the list.\n"
     "Language / Język: Menu > Display and map.\n"
     "Touch (no gloves): top bar – menu; left edge – previous screen; right edge – next. MAP: tap the map to change "
     "the scale. TOPO: tap the upper half to zoom in, the lower half to zoom out. TRAFFIC: tap a pilot to fly to him. "
     "NAV: tap to change the target (home, route, landing). FUEL: buttons at the bottom. Alert: tap the upper part "
     "of the screen to acknowledge.\n"
     "Menu by touch: tap an item to choose it. Scroll long lists with the big ▲ / ▼ buttons on the right (a page "
     "at a time). Title bar – up one level."},

    {"Status bar",
     "From the left:\n"
     "– Screen name and dots – which screen this is (filled dot = current).\n"
     "– Clock – local time from GPS (time zone in Menu > Display and map). --:-- = no GPS time.\n"
     "– ▲ h:mm – flight time, in the air only.\n"
     "– Black box with ! – an alert is active (also if already acknowledged).\n"
     "– Black box with a sun – front light on. Switch it off in daylight, it drains the battery.\n"
     "– TEL – phone with the ParaNav app connected.\n"
     "– FN n – this many other pilots heard via FANET. White on black – someone is dangerously close.\n"
     "– BF – BlueFly connected. Framed: connected, but altitude comes from the wired barometer. White on black: "
     "altitude and vario from BlueFly.\n"
     "– ● REC – flight is being logged (IGC + GPX).\n"
     "– GPS n – satellites in use. GPS in reverse video = no position.\n"
     "– ⚡ – battery charging.\n"
     "– Battery percentage and icon. ? = no battery reading (details on the SYSTEM screen)."},

    {"FLIGHT screen",
     "Main in-flight screen.\n"
     "– VARIO: bar on the left (+/- 5 m/s, climb filled, sink hatched), instant value and 30 s average.\n"
     "– ALTITUDE MSL: barometric. BlueFly = from BlueFly via Bluetooth, (GPS) = no barometer, altitude and vario "
     "from GPS (less accurate, vario lags).\n"
     "– above takeoff; AGL – needs terrain data on the card.\n"
     "– GROUND SPEED; TRACK – direction over the ground.\n"
     "– HOME: distance, arrow to home relative to your track (up = ahead), ETE under power with wind, and bearing.\n"
     "– WIND: speed and the direction it blows from. The hollow arrow shows where it blows to, relative to your "
     "track. Needs a turn of at least 200°. Minutes in brackets = age of the measurement.\n"
     "– FUEL: endurance, litres and return margin. Black TURN BACK bar = not enough fuel to get home with reserve.\n"
     "– GLIDE RATIO L/D: measured only in a steady glide, otherwise ––. polar = value from the settings. "
     "arrival at home = height margin over home after an engine-off glide (wind and safety height included). "
     "✓ = within glide; black bar = home out of glide range."},

    {"MAP screen",
     "Moving map: track up (or north up – Menu > Display and map). The arrow in the centre is you.\n"
     "– Airspace: thick outline – prohibited, restricted, danger and CTR; thin – others; dashed – inactive.\n"
     "– Hatching = active airspace relevant at your altitude: cross-hatch – P, R, Q; dense diagonal – CTR, TRA, TSA; "
     "sparse diagonal – others. Airspace far above or below you is not hatched.\n"
     "– Solid line – flight track. Dashed circle – engine-off glide range (stretched downwind).\n"
     "– House – HOME. Dashed line – course to the target. Circle – waypoint, square – landing field.\n"
     "– Arrows with numbers – other pilots (FANET), number = height difference.\n"
     "– N in a circle – where north is. Hollow arrow with a number – wind (km/h). Bar in the corner – scale.\n"
     "Panel on the right: altitude, vario, ground speed and NEAREST AIRSPACE – name, class, floor – ceiling, "
     "distance with arrow, INSIDE!, below/above (metres), time to entry and activation status. "
     "Tap the map to change the scale: 1, 2, 5, 10, 20, 50 km."},

    {"TOPO screen",
     "Topographic map from the SD card (/maps folder), always north up. Arrow in a circle – you and your track.\n"
     "– The map moves in steps: once you are about 1/3 of the screen away from the centre, the view is re-centred "
     "with more map ahead of you. This way the e-paper does not redraw the whole map every second.\n"
     "– Map symbols: horizontal lines – water and sea; dots – forest; cross-hatch – built-up area; line with cross "
     "ticks and dots – high-voltage power line (thinner, no dots – medium voltage); tripod in a circle – wind "
     "turbine; caret with a dot – mast, tower, chimney (number = height in m); circle with a bar – aerodrome or "
     "airstrip; black bars – runways; black lines – roads; double line – river; black and white line – railway.\n"
     "– Overlays: airspace outlines (short dashes, thin dashed – inactive), waypoints and landings, track, house – "
     "HOME, long dashes – line to the target, arrows – other pilots (FANET).\n"
     "– Corners: altitude and AGL, ground speed and track, vario and wind (hollow arrow – where it blows to), "
     "nearest airspace (arrow – direction, north up).\n"
     "– At the bottom: scale and map level z10 (screen ~90 km wide) … z14 (~6 km). Tap the upper half of the map "
     "to zoom in, the lower half to zoom out. Dots in a regular grid = no map for this place.\n"
     "– Without a GPS signal the map shows the area of the last known position (kept also over power-off), with "
     "a black NO GPS SIGNAL banner and a dashed ring instead of the arrow. It is not the current position; airspace "
     "is not drawn then.\n"
     "The map is an orientation aid, not an aeronautical chart – obstacles may be incomplete."},
    {"Saved flights",
     "Menu > Saved flights: flights on the SD card (IGC files in logs/), newest first – date, takeoff time and "
     "airtime. Choose one to view it (after landing; the list does not open in flight).\n"
     "– On the left: airtime, track length, average speed, max gain and the maximum values (altitude, distance "
     "from takeoff, climb and sink over 10 s, speed).\n"
     "– Map: the track over the map from the card (white where there is no map). Dot – takeoff, square – landing. "
     "Buttons + and – or KEY / BOOT short: zoom in / out; ALL – whole flight. Tap the map to move that point to the "
     "centre.\n"
     "– At the bottom: altitude profile (hatched – ground, with terrain files). Tap the profile to select a moment: "
     "a ring on the map and, below the profile, time, altitude, height above ground, vario and speed.\n"
     "– DELETE (or hold BOOT) – a question, then the IGC and GPX files are removed. CLOSE (or hold KEY) – back.\n"
     "Altitude comes from the barometer levelled to GPS; without a barometer from GPS."},

    {"FANET – other pilots",
     "The built-in 868 MHz LoRa radio transmits your position on the open FANET network and receives other pilots. "
     "You see FANET devices (Skytraxx, XC Tracer, Naviter, Burnair, Leaf, GXAirCom…). Aircraft and gliders with "
     "FLARM only are not shown – and they do not see you.\n"
     "– You transmit as a paraglider every 5 s (less often in a crowd). OGN ground stations may show you on the "
     "internet (e.g. glidernet.org) – switch off in Menu > FANET > Visible online.\n"
     "– MAP and TOPO: arrow = pilot and his track, number = height difference (+ above you), ↑↓ = climbing / "
     "sinking. Hollow arrow – no data for 20 s. Circle with a square – someone on the ground. Thick circle – threat.\n"
     "– TRAFFIC screen: list (distance, bearing relative to your track, height difference, vario) and a track-up "
     "radar. Tap a pilot – he becomes the navigation target (fly to a friend).\n"
     "– On other screens: TRAFFIC box top right – someone closer than the warning limit or closing in (within 45 s). "
     "TRAFFIC! bar – half the limits or within 20 s. Limits: Menu > FANET.\n"
     "This is an aid, not a collision avoidance system: positions arrive every ~5 s and the antenna is internal – "
     "range depends on where the device sits. Always keep a lookout."},

    {"NAV screen",
     "Guidance to the target. Choose the target in Menu > Navigation or by tapping this screen. BOOT long = HOME.\n"
     "– The compass rose turns with your track; the triangle at the top is your track.\n"
     "– Big arrow – direction to the target. Bottom left: how many degrees to turn (◄ left, ► right).\n"
     "– DISTANCE, BEARING, ETE (under power, with wind), ETA (local time).\n"
     "– FUEL AT TARGET – fuel left on arrival.\n"
     "– REQUIRED L/D – glide ratio needed to reach the target engine-off; FINAL GLIDE – height margin (+) or "
     "shortfall (-) over the target without engine.\n"
     "Route: points switch automatically when you get closer than 200 m.\n"
     "Choosing a route: Menu > Navigation > Choose route – all routes on the card with their points and length "
     "(● = active). The chosen route is remembered. On MAP and TOPO the route is a line with numbered circles "
     "(square = landing field, black = the point you are flying to). Routes are planned on a map in the phone app."},

    {"THERMAL screen",
     "For engine-off soaring.\n"
     "– Large vario, 30 s average and bar.\n"
     "– THERMAL: height gain and average climb since circling started (shown while circling).\n"
     "– Altitude and AGL, wind.\n"
     "– On the right the track of the last 90 s (track up): black dots – climb, bigger dot = stronger lift; "
     "hollow – sink. Ø – view diameter. Hollow arrow – wind direction."},

    {"AIRSPACE screen",
     "Table of up to 7 nearest airspaces within 20 km.\n"
     "– Columns: name, class, floor – ceiling, distance with arrow (or below/above and metres when you are within "
     "its outline), status.\n"
     "– Status: PERM. (permanent), ACTIVE, INACT. (inactive), ACT.? – no activation data (treated as active), in m:ss – predicted "
     "time to entry.\n"
     "– Framed row – warning. Row in reverse video – you are inside.\n"
     "Profile at the bottom: 10 km ahead along your track. Dotted area – terrain, hatched boxes – airspace, "
     "triangle – your altitude, dashed line – engine-off glide."},

    {"FUEL screen",
     "– Big number – endurance (h:mm). Below: litres and a bar with the reserve marked.\n"
     "– On the right: fuel flow, reserve, cruise speed, fuel needed to get home with reserve, time to home and "
     "margin. TURN BACK! = negative margin.\n"
     "– Touch buttons: -0.5 l / +0.5 l – fuel at takeoff; FULL TANK – resets fuel used; ENGINE – when off, the fuel "
     "counter stops.\n"
     "Consumption is computed from engine running time and the set flow in l/h – there is no fuel sensor. "
     "Set the fuel before the flight in Menu > Fuel."},

    {"SYSTEM screen",
     "Device status, useful when troubleshooting.\n"
     "– Battery, current, runtime, battery gauge source.\n"
     "– GPS: module, fix, satellites, position, NMEA sentences received.\n"
     "– Barometer: altitude source (cable, BlueFly, none – GPS) and pressure; with the cable F a/b = full screen "
     "refreshes the filter waited out / samples rejected as jumps. BlueFly: connection state, BlueFly "
     "battery and signal strength (dBm, closer to zero is better).\n"
     "– QNH (calibr. = set), GPS and barometric altitude, terrain below you.\n"
     "– Phone: off / waiting / connected / receiving (percent). From phone: result of the last upload.\n"
     "– SD card, airspace, activations, waypoints, terrain tiles and the TOPO map for your area, flight log file, "
     "free memory.\n"
     "– FANET: your ID, packets sent (tx) and received (rx) or a radio error. I2C – addresses of detected chips.\n"
     "– At the bottom: file loading errors."},

    {"Alerts",
     "An alert is a black bar at the top of the screen. Highest priority first:\n"
     "– TRAFFIC! – another FANET pilot very close or about to be (clock direction: 12 o'clock = ahead, 3 = right).\n"
     "– INSIDE – you are inside active airspace.\n"
     "– TURN BACK – FUEL! – not enough fuel to get home with reserve.\n"
     "– AIRSPACE – approaching airspace (horizontally or vertically).\n"
     "– LOW! – too low above terrain (when enabled in the settings).\n"
     "– LOW FUEL – return margin below 10 min.\n"
     "– NO GPS, NO BAROMETER (barometer lost in flight – altitude from GPS), LOW BATTERY.\n"
     "A short button press acknowledges the alert. It comes back when the situation clears and happens again; INSIDE "
     "and TURN BACK repeat every 2 minutes. Alerts work in flight (battery – always). Limits: Menu > Alarms."},

    {"Airspace exit",
     "When you enter active airspace (the zones that raise the INSIDE alert), the alert has two buttons:\n"
     "– LEAD ME OUT (KEY or tap the left button) – the EXIT screen shows the fastest way out, regardless of the "
     "target, the route or Go Home.\n"
     "– IGNORE (BOOT or the right button) – the alert comes back in 2 min if you are still inside.\n"
     "Three ways are compared and the fastest is shown: level (every 5°, through neighbouring zones to free "
     "airspace, including the turn), up above the ceiling (also above a zone stacked on top) or down below the floor "
     "(not lower than 60 m above the ground; a zone from the ground cannot be left downwards).\n"
     "The arrow is relative to your track; for a vertical exit a big CLIMB / DESCEND arrow with the target "
     "altitude. On the right: time and distance of all three options.\n"
     "The screen closes 10 s after you are out. Close it by hand: hold KEY. Without the alert: Menu > Navigation "
     "> Lead me out of airspace.\n"
     "Climb and sink rates used: Menu > Alarms (default 1.5 and 2 m/s).\n"
     "Altitude limit: Menu > Alarms > Max altitude FL (default FL95, 0 = no limit). Climbing out is offered only "
     "when it ends at least 30 m below the limit – otherwise \"up\" shows \"above FL95\". The limit follows the "
     "current QNH.\n"
     "Route check: when a route is chosen (on the device or from the phone), ParaNav checks whether it crosses "
     "zones with a floor below the flying height set in Menu > Alarms (default 1000 m AGL, never above the FL "
     "limit) and lists them. "
     "Again: Menu > Navigation > Check route."},

    {"Barometer and BlueFly",
     "Altitude and vario come automatically from the first working source:\n"
     "– 1. MS5611 barometer on the QWIIC cable. It can be plugged in later – it is checked every 10 s.\n"
     "– 2. BlueFlyVario via Bluetooth (BLE). Just switch the BlueFly on – it is found and connected automatically, "
     "also after a lost link. The BlueFly GPS position is not used, only the internal GPS.\n"
     "– 3. No barometer: altitude and vario from GPS (less accurate).\n"
     "When the source changes, the vario restarts without a false jump. If the barometer is lost in flight, the "
     "NO BAROMETER alert appears.\n"
     "Wired barometer: a full screen refresh disturbs its reading (about 2 m jump). During it and 1 s after, the "
     "vario ignores the readings and carries on with the last climb rate. Single jumps of more than 1.5 m are "
     "rejected too.\n"
     "A BlueFly connected to this device cannot connect to a phone (e.g. XCTrack) at the same time. The BlueFly "
     "vario sound always works, regardless of the link.\n"
     "Menu > System: BlueFly on/off (Bluetooth off saves battery) and BlueFly: reconnect."},

    {"Phone – file upload",
     "The ParaNav Android app sends files via Bluetooth – no need to remove the SD card.\n"
     "– In the app: Connect, then pick a file on the phone: airspace (OpenAir .txt), waypoints or route (.cup). "
     "Today's airspace activations are typed in the app.\n"
     "– Routes can be planned on a map in the app (OpenTopoMap, with airspace and waypoints) and sent as active.\n"
     "– The Replace option deletes previous files of that kind (airspace: all .txt except activations).\n"
     "– The device checks the file (checksum, whether it contains airspace or waypoints) before saving it. A bad "
     "file never overwrites a good one.\n"
     "– During the upload a progress bar is shown at the bottom and TEL in the status bar.\n"
     "– Switch off: Menu > System > Phone (Bluetooth). Any phone with the app nearby can connect, so switch it off "
     "when not in use."},

    {"Files on the SD card",
     "microSD card, FAT32. Folders:\n"
     "– /airspace – airspace in OpenAir format, .txt files (all are loaded).\n"
     "– /airspace/activations.txt – today's activations. First line date=YYYY-MM-DD, then e.g. EPTR12 0800-1500 "
     "(UTC, several intervals allowed).\n"
     "– /waypoints – waypoints, .cup files (SeeYou). Style 2-5 = landing field.\n"
     "– /route – route, a .cup file; order in the file = order of the route.\n"
     "– /terrain – terrain data, .hgt tiles (may be in subfolders). N52E021.hgt = 52-53°N, 21-22°E.\n"
     "– /maps – base maps for the TOPO screen, one folder per area (info.txt and z10.pak … z14.pak). Copy the whole "
     "maps folder produced by the map tool.\n"
     "– /logs – flights are saved here (.igc and .gpx), and diagnostics: restarts.txt (every start with its "
     "reason) and crash-NN.txt (a report for every unexpected restart).\n"
     "– /config.ini – settings, written by the device.\n"
     "After changing files: Menu > System > Reload data from SD (or restart)."},

    {"Maps and USB mode",
     "Base maps are made on a computer from OpenStreetMap data (see README). Two ways to put them on the card:\n"
     "– Remove the card and copy the maps folder with a card reader.\n"
     "– Or without removing it: Menu > System > USB mode – SD card as a drive. Connect a USB-C cable to the "
     "computer – the card appears as a drive. Copy the files, then eject the drive on the computer. The device "
     "returns to normal operation and reloads the data. Return without ejecting: KEY or BOOT long – but first wait "
     "until the computer has finished copying.\n"
     "In USB mode the flight log is closed and the phone cannot upload files. Not available in flight.\n"
     "Several areas on the card: the device picks the one that covers your position (the most detailed)."},

    {"USB power and charging",
     "When USB is plugged in, the device asks which mode to use:\n"
     "– NAVIGATION – normal operation, e.g. with a power bank.\n"
     "– CHARGING – battery screen only; navigation paused, FANET and BlueFly off, the screen is redrawn only when "
     "the percentage changes.\n"
     "– FILES – SD card as a drive (only when a computer is on the other end of the cable).\n"
     "Choose: tap a button, or KEY short – next, long – choose. No answer within 15 s: CHARGING if the device was "
     "switched on by plugging USB in; NAVIGATION if it was already running.\n"
     "Never asked in flight – navigation simply continues. After a restart caused by an error the device goes "
     "straight back to navigation.\n"
     "Leave charging: tap the screen, hold KEY or unplug USB – navigation starts. "
     "Manually: Menu > System > Charging mode."},

    {"Takeoff, landing, logging",
     "– Takeoff is detected automatically: ground speed above 15 km/h for 10 s. The position before takeoff becomes "
     "HOME. Flight logging, flight time and the fuel counter start.\n"
     "– HOME can be set manually: Menu > Flight > Set HOME here.\n"
     "– Landing: after 60 s without movement (below 5 km/h, vario under 0.5 m/s) the LANDED? question appears. KEY – still "
     "flying (no question for 5 min), BOOT – landed. Without an answer the flight goes on; if you still do not move, "
     "it ends by itself after 4 min. Higher than 50 m above the terrain there is no question (hovering into the wind). "
     "After landing the log is closed and a summary is shown; the flight time ends when you stopped.\n"
     "– The altimeter calibrates itself on the ground once at least 6 satellites are received (Menu > Altimeter).\n"
     "– Power off (both buttons 3 s or menu) also closes and saves the flight."},
};

const std::vector<HelpTopic>& helpTopics() { return nav::langEn() ? kTopicsEn : kTopicsPl; }

}  // namespace ui
