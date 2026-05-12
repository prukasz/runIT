# runIT

> **`YOU can run IT`**

---

 # czym jest runIT?

**runIT** to kontroler łączący pogranicze **IoT**, modelarstwa **RC** oraz **automatyki**.

Pozwoli Ci on na podłączenie standardowych elementów RC (takich jak serwa, ESC, różnego rodzaju aktuatory) oraz łatwą integrację popularnych czujników i pasków LED. Dzięki aplikacji mobilnej stworzysz interfejs sterowania, a za pomocą prostego języka graficznego uczynisz swój projekt "SMART" i podłączysz go do sieci.

# Co oferuje sterownik runIT

    * PowerIO 20V
    * Dwie regulowane przetwornice DC-DC
    * Elastyczne zasilanie w tym USB-C PD
    * PWM expander
    * WiFi + BLE
    * Zabezpieczenia 

    Całość na popularnym mikrokontrolerze ESP32-S3

# PowerIO 
    * 4 Konfigurowalne mostki H (8 wyjść)- nawet 20V 5A
    * 8 wejść analogowo - cyfrowych gotowe do bezpośrednej pracy z napięciem do 20V
    * System wyboru źródeł zasilania dla mostków H - Zasil elementy wymagające różne napięcia bezpośrednio z płytki
    * Zasilanie mostków H z przetwornic DC-DC lub bezpośrednio z zasilacza
    * Pomiar poboru prądu i tryb Constant Current

# Regulowane przetwornice DC-DC
    * Swobodna regulacja w zakresie 4.5V - 20V
    * Zabezpieczenia przeciw zwarciowe i przeciążeniowe
    * Limit poboru prądu
    * Wyjścia bezpośrednie z przetwornicy - zasilacz regulowany
# USB-C PD i wejscie uniwersalne zasilania
    * Wsparcie USB-C PD 3.0 20V-5A 
    * Wejście na akumulator lub zasilacz 5V - 20V wraz z pomiarem poboru prądu i napięcia
    * Możliwość koegzystencji obu wejść zasilania
# PWM expander
    * 8 kanałów dedykowanych wraz z wyjściamiami 3 - pinowymi
    * Współpraca z Servo i ESC bez zajmowania innych kanałów
# WiFi + BLE
    * Połączenie WiFi i BLE lub oba na raz
# Zabezpieczenia
    * E - bezpieczniki
    * Zabezpieczenie przeciwko odwrotnej polaryzacji
    * Zabezpieczenia ESD
    * Izolowane zasilanie 3.3V
    * Regulowany limit prądu zasilania 
    * Pomiar napięcia i poboru prądu - alerty

# ESP32-S3
    * Wyprowadzone GPIO w tym SPI i I2C
    * Brak ograniczenia jedynie do wbudowanych peryferiów - podłącz popularne czujniuki 
    * Ekosystem ESP-IDF łątwa modyfikacja do swoich potrzeb

# Co oferuje oprogramowanie 
    * Prosty język blokowy z wbudowanym wsparciem "RC"
    * Tryb smart - pilota RC
    * Wsparcie dla popularnych elementów modelarskich
    * Prosta konfiguracja podpiętych elemntów i WiFi, BLE
    * Ułatwione wykrywanie błędów 
    * Łatwy dostęp do danych telemetrycznych
    * Zabezpieczenia działające w tle

# Język blokowy
    * Drag ' n drop
    * Automatyzacja prostych rozwiązań 
    * Wyślij zebrane dane przez BLE lib WiFi
    * Dedykowane bloki sterujące dla elementów modelarskich 
    * Podgląd zmiennych na żywo 
    * Edycja kodu na żywo i OTA
    * Ułatwiona konfiguracja podpiętych elemtentów i wybranego zasilania - ustaw napięcie a resztą zajmie się oprogramowanie
# Tryb RC 
    * Połacz kod z pilotem na urządzeniu
    * Wykonuj skomplikowane sekwencje niemożliwe w zwykłej aparaturze RC
## Potrzebujesz bardziej skomplikowanego oprogramowania ? 
    * Zaimplementuj je równolegle C i utwórz nowy blok 
    * Uruchom go równolegle z kodem blokowym
    * Wykorzystaj sprzęt oraz sterowniki i napisz własny kod

-------------------------
# STAN PROJEKTU


# Sterowniki w trackie pisania...
## Sterowniki do przetwornic, expandera PWM, ADC i innych elementów przygotowywane są na test gotowej płytki
## Zaimplementowano uniwersalny system wymiany danych - obecnie przez BLE 

# Projekt płytki już w produkcji!

![alt text](/_DOCS/images/PCBTOP.png "PCBTOP")
