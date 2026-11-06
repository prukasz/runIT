# Jak uruchomić runIT

Działa na dowolnym ESP32-S3. Firmware jest jeden: aplikacja zapisuje na płytce jej profil (urządzenia, magistrale I2C, układ zasilania).

Komendy firmware w `runit-esp/` (profil ESP-IDF załadowany), aplikacji w `runit-app/`.

## Czysty devkit

Board = ESP32-S3 devkit N16R8 (ten sam build co PCB):

```
idf.py build
idf.py -p <PORT> flash
```

Aplikacja: `npm run app:devkit`

## Płytka runIT (PCB)

Board = runIT PCB (domyślne w `sdkconfig`):

```
idf.py build
idf.py -p <PORT> flash
```

Aplikacja: `npm run app`

## Urządzenia podpinane dynamicznie

Firmware i aplikacja jak dla czystego devkita. Urządzenia tylko w aplikacji: Devices → dodaj, ustaw piny, zapisz projekt na płytkę.

## Własna płytka z urządzeniami (jeśli są sterowniki)

- Urządzenia płytki: profil płytki (`runit-esp/components/sys/sys_board/SYS_BOARD.MD`), czyli opis `board_<nazwa>.json` i jego wariant w aplikacji (`templates/generate_bundle.py`, `runit-app/src/backend/definitions.ts`).
