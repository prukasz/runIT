# Moduł wyświetlacza: dekompozycja

Wyświetlacz z edycją zawartości w czasie działania i interakcją z językiem programowania projektu (VM). Renderowanie i gotowe komponenty: [LVGL](https://github.com/lvgl/lvgl). Ekran jest tworzony z bytestreamu, tak jak program VM.

## Założenia

- **System błędów:** używamy istniejącego łańcucha błędów projektu. Moduł ma własną rodzinę błędów z tagami, błędy raportowane tą samą drogą co w reszcie firmware.
- **Wszystko przez bytestream:** konfiguracja sterowniki, budowa ekranu, powiązania, zdarzenia i diagnostyka to komendy i strumienie w tym samym formacie ramek co reszta projektu.
- **Bez kompilacji przez użytkownika:** użytkownik końcowy nigdy nie kompiluje ani nie flashuje. Wszystko, co może zmienić (panel, układ, style, teksty, powiązania, zdarzenia), jest danymi wgrywanymi w czasie działania.

## Warstwy

### 1. Sterowniki sprzętu
Panel i dotyk jako urządzenia. Wiele sterowników wkompilowanych naraz, wybór w ramce create.

### 2. Kontrakty
Interfejs wyświetlacza i dotyku niezależny od sterownika: (parametry panelu, zapis okna, podświetlenie, odczyt punktu dotyku). Hardware niezależny od reszty warstw

### 3. Konektor LVGL
Spina kontrakty z LVGL: flush, dotyk, zegar. zarządza pamięcią dla buforow i obiektów wyświetlacza + praca wielowątkowa.

### 4. Kompozycja ekranu z bytestreamu
Słownik (jakie widgety, właściwości i zdarzenia istnieją, ze stałymi numerami, razem z zasobami: fonty, ikony, style) oraz format ramek i loader, który z niego buduje ekran: styl, drzewo widgetów, właściwości, pokaż / usuń. Słownik jest statyczny, ekran tworzony dynamicznie z elementów - weryfikacja bytestreamu.

### 5. Adapter VM
Powiązania obiekt → widget (wyświetlanie, odświeżanie przy zmianie) i zdarzenie widgetu → akcja (zapis do obiektu, zdarzenie dla VM, zmiana ekranu). Ekran należy do programu VM.

## Zależności

1. Sterowniki sprzętu: rejestr urządzeń, magistrale, router, system błędów
2. Kontrakty: warstwa 1, `sys_event`, system błędów
3. Konektor LVGL: warstwa 2, LVGL, FreeRTOS, system błędów
4. Kompozycja ekranu: warstwa 3, router, system błędów
5. Adapter VM: warstwa 4, `vm_core`, `sys_event`, system błędów

## Efekty

1. Możliwość podlączenia jednego z wybranych wyświetlaczy dynamicznie 
2. Możliwość skomponowania prostego ekranu z dostępnych komponentów przekazując ramki bytecode
3. Możliwość połączenia ekranu i interakcji z kodem z VM

## Efekt końcowy (Prezentacja)

Wgranie prostego kodu na płytkę, następnie wyświetlenie prostego ekranu ("narysowanego w aplikacji") wraz z telemetrią z wybranego urządzenia na płytce - np. poborem prądu.