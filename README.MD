# runIT

> **`YOU can run IT`**

![Płytka runIT, widok z góry](/_DOCS/images/PCBTOP.png "PCBTOP")

---

## Czym jest runIT?

**runIT** to kontroler łączący pogranicze **IoT**, modelarstwa **RC** oraz **automatyki**.

Pozwoli Ci on na podłączenie standardowych elementów RC (takich jak serwa, ESC, różnego rodzaju aktuatory) oraz łatwą integrację popularnych czujników i pasków LED. Dzięki aplikacji stworzysz interfejs sterowania, a za pomocą prostego języka graficznego uczynisz swój projekt "SMART" i podłączysz go do sieci.

## Co oferuje sterownik runIT

- PowerIO 20V
- Dwie regulowane przetwornice DC-DC
- Elastyczne zasilanie, w tym USB-C PD
- PWM expander
- WiFi + BLE
- Zabezpieczenia

Całość na popularnym mikrokontrolerze ESP32-S3.

### PowerIO

- 4 konfigurowalne mostki H (8 wyjść) - nawet 20V 5A
- 8 wejść analogowo-cyfrowych gotowych do bezpośredniej pracy z napięciem do 20V
- System wyboru źródeł zasilania dla mostków H - zasil elementy wymagające różnych napięć bezpośrednio z płytki
- Zasilanie mostków H z przetwornic DC-DC lub bezpośrednio z zasilacza
- Pomiar poboru prądu i tryb Constant Current

### Regulowane przetwornice DC-DC

- Swobodna regulacja w zakresie 4.5V - 20V
- Zabezpieczenia przeciwzwarciowe i przeciążeniowe
- Limit poboru prądu
- Wyjścia bezpośrednio z przetwornicy - zasilacz regulowany

### USB-C PD i wejście uniwersalne zasilania

- Wsparcie USB-C PD 3.0 20V-5A
- Wejście na akumulator lub zasilacz 5V - 20V wraz z pomiarem poboru prądu i napięcia
- Możliwość koegzystencji obu wejść zasilania

### PWM expander

- 8 kanałów dedykowanych wraz z wyjściami 3-pinowymi
- Współpraca z Servo i ESC bez zajmowania innych kanałów

### WiFi + BLE

- Połączenie WiFi i BLE lub oba na raz

### Zabezpieczenia

- E-bezpieczniki
- Zabezpieczenie przeciwko odwrotnej polaryzacji
- Zabezpieczenia ESD
- Izolowane zasilanie 3.3V
- Regulowany limit prądu zasilania
- Pomiar napięcia i poboru prądu - alerty

### ESP32-S3

- Wyprowadzone GPIO, w tym SPI i I2C
- Brak ograniczenia jedynie do wbudowanych peryferiów - podłącz popularne czujniki
- Ekosystem ESP-IDF - łatwa modyfikacja do swoich potrzeb

## Co oferuje oprogramowanie

- Prosty język blokowy z wbudowanym wsparciem "RC"
- Tryb smart - pilot RC
- Wsparcie dla popularnych elementów modelarskich
- Prosta konfiguracja podpiętych elementów oraz WiFi i BLE
- Ułatwione wykrywanie błędów
- Łatwy dostęp do danych telemetrycznych
- Zabezpieczenia działające w tle

### Język blokowy

- Drag 'n drop
- Automatyzacja prostych rozwiązań
- Wyślij zebrane dane przez BLE lub WiFi
- Dedykowane bloki sterujące dla elementów modelarskich
- Podgląd zmiennych na żywo
- Edycja kodu na żywo i OTA
- Ułatwiona konfiguracja podpiętych elementów i wybranego zasilania - ustaw napięcie, a resztą zajmie się oprogramowanie

### Tryb RC

- Połącz kod z pilotem na urządzeniu
- Wykonuj skomplikowane sekwencje niemożliwe w zwykłej aparaturze RC

### Potrzebujesz bardziej skomplikowanego oprogramowania?

- Zaimplementuj je równolegle w C i utwórz nowy blok
- Uruchom go równolegle z kodem blokowym
- Wykorzystaj sprzęt oraz sterowniki i napisz własny kod

---

## Stan projektu

**Płytka jest już wyprodukowana i uruchamiana.** Firmware i aplikacja powstają równolegle.

### Działa (sprawdzone na płytce)

- Uniwersalny system wymiany danych z aplikacją - przez BLE i UART
- Sterowniki: GPIO ESP, ekspander TCA6424A, ADC ADS7128, PWM PCA9685, USB-C PD AP33772S i kolejne układy zasilania
- Komendy, odpowiedzi, błędy i logi dekodowane w aplikacji
- Zdarzenia na urządzeniach i akcje uruchamiane zdarzeniami
- Maszyna wirtualna (VM) z 15 typami bloków, m.in. wyrażenia, warunki, pętle, timery, zatrzask, detektor zbocza, wyjścia IO
- Zapis programu w pamięci płytki i jego wczytanie po restarcie
- Aplikacja (przeglądarka, Web Bluetooth): widok płytki i urządzeń, edytor bloków z płótnem (canvas), kompilator programu, tryb debugowania z podglądem zmiennych na żywo

### W trakcie

- Wysyłanie programu z edytora blokowego na płytkę w pełnym przepływie z poziomu aplikacji
- Wybór źródła zasilania mostków H (z blokadą przed zwarciem źródeł)
- Zasilanie: potwierdzenie kolejności kanałów INA3221, wskaźniki źródła, estymaty
- Testy VM na sprzęcie dla bloków Latch, Periodic, Action i On Event

### Planowane

- Tryb RC - pilot w aplikacji połączony z kodem blokowym
- Telemetria i rejestrator danych
- Strona zasilania i subskrypcji zdarzeń w aplikacji
- WiFi (w tym MQTT) jako kolejny kanał danych
- Aplikacja mobilna (Android)

## Struktura repozytorium

| Folder | Zawartość |
|---|---|
| `_SOFTWARE` | Firmware ESP32-S3 (ESP-IDF), deskryptory danych i aplikacja webowa |
| `_HARDWARE` | Projekt płytki: schematy i PCB (Altium) |
| `_DOCS` | Obrazy i dokumentacja do README |
