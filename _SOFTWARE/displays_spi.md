# Wyświetlacz: tylko SPI

Decyzja dla modułu wyświetlacza (`displays.md`): obsługujemy wyświetlacze na magistrali SPI.

## Dlaczego

- Na płytce wyprowadzone jest SPI.
- Magistrala równoległa (I80) potrzebuje 12–20 GPIO (8–16 linii danych plus WR, DC, CS), za dużo dla tej płytki.
- Panele RGB wymagają ciągłego strumienia pikseli i pełnego bufora w pamięci, czyli PSRAM, który jest dziś wyłączony.
- Wysoka liczba klatek na sekundę nie jest wymagana.

## Konsekwencje

- Bufor rysowania częściowy (np. 2 × 1/10 ekranu), w pamięci wewnętrznej z obsługą DMA.
- PSRAM nie jest potrzebny do wyświetlacza.
- Niska liczba klatek jest akceptowalna, więc nie optymalizujemy przepustowości SPI.
- Transport SPI i sterowniki paneli z `esp_lcd` (panel IO SPI), kontrakty i urządzenia własne.
