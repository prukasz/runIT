# Jak uruchomić runIT

Działa na dowolnym ESP32-S3. Wybór płytki: `menuconfig` → runIT → Board (`CONFIG_RUNIT_BOARD`).

Komendy firmware w `runit-esp/` (profil ESP-IDF załadowany), aplikacji w `runit-app/`.

## Czysty devkit

Board = ESP32-S3 devkit (gotowe w `sdkconfig.devkit.defaults`):

```
idf.py -B build_devkit -D SDKCONFIG=build_devkit/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig;sdkconfig.devkit.defaults" build
idf.py -B build_devkit -p <PORT> flash
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

- Urządzenia tworzone przy starcie: nowa opcja w `RUNIT_BOARD` (`runit-esp/components/app/runit/Kconfig`), ich tworzenie w `runit_board_cfg.c`, opis `board_<nazwa>.json` i jego wariant w aplikacji (`templates/generate_bundle.py`, `runit-app/src/backend/definitions.ts`).
