# LVGL playground

LVGL w zwykłym C na PC, bez płytki, ESP-IDF i projektu runIT. Zamiast wyświetlacza jest mock
(`mock/`): framebuffer RGB565 z interfejsem jak prawdziwy kontroler LCD (okno + strumień pikseli)
i mock dotyku. Scenariusz działa na wirtualnym zegarze (bez czekania) i zapisuje ekran do `out/*.bmp`.

## Co LVGL potrzebuje od platformy (blok „platform glue” w `main.c`)

| Element | Po co | W playgroundzie | Na prawdziwej płytce |
|---|---|---|---|
| `lv_conf.h` | wybór głębi kolorów, pamięci, widgetów, fontów, logów | `lv_conf.h` | ten sam plik |
| Pamięć | sterta LVGL (obiekty, style, cache) | `LV_MEM_SIZE` 64 kB, wbudowany alokator | pula w RAM lub PSRAM |
| Tick | milisekundowy zegar do animacji i timerów | `lv_tick_set_cb` na wirtualnym zegarze | `esp_timer_get_time()/1000` |
| Bufory rysowania | tu LVGL renderuje | 2 × 1/10 ekranu, tryb `PARTIAL` | w pamięci zdolnej do DMA (RAM wewnętrzny albo PSRAM, wybór płytki) |
| `flush_cb` | wysłanie wyrenderowanego prostokąta do panelu, potem `lv_display_flush_ready` | `mock_panel_set_window` + `mock_panel_write` | okno + RAMWR po SPI albo I2C, `flush_ready` z przerwania DMA |
| Wejście | `lv_indev` z `read_cb` (dotyk, enkoder, przyciski) | `mock_touch` | sterownik dotyku (I2C/SPI) |
| Pętla | `lv_timer_handler()` co kilka ms z jednego wątku | funkcja `run()` | zadanie FreeRTOS, reszta pod mutexem (`lvgl_port_lock`) |
| Log | `LV_USE_LOG` | `printf` | `ESP_LOG` |

Opcjonalne, jeszcze nie użyte: system plików (obrazy, fonty z pliku), dekodery obrazów, przyspieszenie
rysowania (DMA2D, PPA), własne fonty (`lv_font_conv`), zamiana bajtów RGB565 gdy panel tego chce.

Szkic kontraktów sterownika panelu i dotyku (DISPLAY, TOUCH) pod płytkę: [CONTRACTS.md](CONTRACTS.md); jego structy są w `contracts/*.h` i kompilują się razem z playgroundem (`contracts_check.c`).

## Budowanie

Potrzebne: kompilator C (gcc/clang/MSVC), CMake ≥ 3.28, Ninja lub Make, dostęp do GitHuba (LVGL v9.3.0 i SDL2 pobiera `FetchContent`).
Na Windows: gcc z winget (`BrechtSanders.WinLibs.POSIX.UCRT`).

    .\run.ps1                # buduje (przy pierwszym razie konfiguruje) i otwiera okno z podglądem ekranu
    .\run.ps1 -Scale 3       # większe okno
    .\run.ps1 -Headless      # scenariusz bez okna, klatki w out/*.bmp
    .\run.ps1 -Clean         # najpierw kasuje build/
    .\run.ps1 -NoWindow      # build bez SDL2 (od razu -Headless)

Skrypt sam dodaje do `PATH` gcc (winget), cmake i ninja (Espressif), jeśli shell ich nie ma. Ręcznie to samo robią:

    cmake -S . -B build -G Ninja
    cmake --build build --target window   # okno
    cmake --build build --target run      # scenariusz bez okna

`window` otwiera okno SDL2 z mockiem panelu w ramce „obudowy”: lewy przycisk myszy to dotyk (też przeciąganie),
`S` zapisuje zrzut `screenshot_N.bmp`, `Esc` zamyka, a tytuł okna pokazuje flushe i kB na sekundę przez „magistralę”.
Skala okna: `playground --window 3`. Bez okna (`-DPLAYGROUND_WINDOW=OFF`) SDL2 się nie pobiera.
