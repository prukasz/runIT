# Kontrakty DISPLAY i TOUCH: szkic

Pomysł, nie specyfikacja: co musi umieć sterownik panelu i sterownik dotyku, żeby konektor LVGL (warstwa 3 w `displays.md` na branchu `display`) nie wiedział, jaki to układ. Konwencja jak w `sys_device`: kontrakt to tabela opów `[contract u8][op u8]`, ten sam sens na każdym sterowniku; to, co umie tylko jeden układ, jest jego własnym opem `0xFFxx`. Tu tylko zarys argumentów, bez wartości rejestrów i bez kodu.

Struktury, enumy i tabele opów są w nagłówkach C w konwencji firmware (X-makra, packed): [display_contract.h](contracts/display_contract.h) i [touch_contract.h](contracts/touch_contract.h). Ten dokument je opisuje; w razie różnicy obowiązuje nagłówek. `contracts/host_stub/` to cienki odpowiednik `sys_device.h`, `sys_event.h` i `sys_io.h` dla hosta (nagłówki kontraktów przechodzą na płytkę bez zmian); `contracts/contracts_check.c` pilnuje w buildzie numerów opów, rozmiarów i wspólnych bitów.

Playground ma te same dwa kontrakty po stronie hosta: `mock_panel` i `mock_touch` to ich „sterowniki”, więc UI rozwijane tu przejdzie na płytkę bez zmian w konektorze.

## Podział

| Warstwa | Co | Przykłady |
|---|---|---|
| Magistrala | przesył bajtów do układu (SPI, I2C), bez znajomości układu | w repo jest `sys_i2c`; **SPI nie ma** |
| Sterownik panelu | init układu, okno, piksele, podświetlenie; implementuje kontrakt DISPLAY | ST7789, ILI9341, GC9A01, ST7735, SSD1306 |
| Sterownik dotyku | odczyt punktów; implementuje kontrakt TOUCH | FT6336U, CST816S, GT911, XPT2046 |
| Konektor LVGL (`sys_lvgl`) | bufory, flush, indev, tick, wątek; zna tylko kontrakty; **moduł z własnym routerem, nie device** ([lvgl_connector.h](contracts/lvgl_connector.h)) | warstwa 3 |

Numery kontraktów: następne wolne po `0x09 HBRIDGE`, czyli `SYS_DEVICE_CONTRACT_DISPLAY = 0x0A`, `SYS_DEVICE_CONTRACT_TOUCH = 0x0B` (`0x0A` i `0x0B` są wolne w `sys_device.h`). Sterownik panelu i sterownik dotyku to dwa osobne devices (dotyk bywa na innej magistrali i w innym układzie niż panel).

## DISPLAY: opy

| Op | Strona | Args | Odpowiedź | Uwagi |
|---|---|---|---|---|
| `0x0A00` GEOMETRY | R | – | patrz niżej | co konektor musi wiedzieć, żeby ustawić LVGL |
| `0x0A01` FLUSH | W | `{u16 x1,y1,x2,y2}` + piksele | – | okno włącznie z krawędziami (w trybie partial LVGL daje zmieniony fragment, więc okno jest konieczne); **asynchroniczne**: wraca po przyjęciu, koniec to zdarzenie `FLUSH_DONE`; **tylko lokalny** (niesie wskaźnik na bufor, nie idzie po drucie) |
| `0x0A02` BACKLIGHT | RW | `{u16 brightness_duty}` 12 bit, 0..4095 | to samo | układ bez podświetlenia: `ERR_DEV_BAD_ARG`; wartość trafia wprost do `IO_PWM_DUTY`, bez floatów (jak PWM w HBRIDGE) |
| `0xFFxx` własne | | | | inwersja, gamma, kontrast OLED, napięcia, **surowy zapis rejestru** `{u8 cmd, bytes}` do bring-upu nowego panelu |

**Zdalnie.** Wyświetlacz na innej płytce wołany jest przez `sys_remote` komendą z argumentami do 64 B (`SYS_DEVICE_REMOTE_ARGS_MAX`), bez wskaźników. GEOMETRY i BACKLIGHT mają packed argumenty, więc działają zdalnie bez dodatkowego kodu, a w aplikacji `brightness_duty` może być suwakiem (`control: slider`, 0..4095). FLUSH nie przejdzie: **konektor LVGL leży na tej samej płytce co panel**, zdalnie sterujemy jasnością i czytamy geometrię, ale nie rysujemy. Do `contract_display.json` wchodzą tylko GEOMETRY i BACKLIGHT (FLUSH jest wewnętrzny, rejestr nie ma jeszcze takiego oznaczenia).

Z lifecycle `sys_device`: **SUSPEND** gasi podświetlenie i usypia panel (bezpieczny stan), **RESUME** budzi i przywraca podświetlenie, **RESET** to sprzętowy reset z pełnym init. Zapisy w stanie SUSPENDED są odrzucane przez rejestr, odczyty (GEOMETRY) działają.

### GEOMETRY: odpowiedź

| Pole | Typ | Po co |
|---|---|---|
| `width`, `height` | u16 | rozdzielczość po bieżącej orientacji → `lv_display_create` |
| `color_format` | u8 | RGB565 (LCD; na magistrali starszy bajt pierwszy, więc konektor zawsze zamienia bajty) albo MONO1 (OLED, strony po 8 wierszy) → `lv_display_set_color_format`; bufor I1 zaczyna się 8 bajtami palety LVGL (flush_cb je pomija: `px_map += 8`), a bajty są mapowane wierszami, więc SSD1306 (strony pionowe) wymaga konwersji |
| `flags` | u16 | bit: DMA (magistrala wysyła przez DMA: bufor rysowania musi być w pamięci zdolnej do DMA; gdzie ona leży, nie zależy od wyświetlacza). Panel bez podświetlenia nie ma osobnej flagi: BACKLIGHT zwraca `ERR_DEV_BAD_ARG` |
| `align_y` | u8 | okno ma zaczynać i kończyć się na wielokrotności tylu wierszy (SSD1306 stronami po 8); 1 = bez ograniczeń. Kolumn nie ogranicza żaden panel hobbystyczny |

## DISPLAY: zdarzenia

| Zdarzenie | Źródło | Wartość | Uwagi |
|---|---|---|---|
| `0x0A00` FLUSH_DONE | sterownik | licznik flushy | z przerwania „DMA gotowe”, **publikowane deferred** (SYS_DEVICE.MD, Concurrency); flaga `LOCAL` (szybkie źródło, tylko słuchacz konektora, nie idzie do aplikacji ani innych płytek); konektor woła z niego `lv_display_flush_ready` |

**Kto słucha.** LVGL nie subskrybuje niczego: robi to konektor `sys_lvgl`. Jest modułem, który żyje cały czas, więc używa słuchacza funkcyjnego z `ctx`: `sys_event_subscribe_fn(display_id, DISPLAY_EV_FLUSH_DONE, SYS_EVENT_ANY_CHANNEL, on_flush_done, c, &c->flush_sub)` w INSTALL, a w RESET `sys_event_unsubscribe`. Sterownik publikuje przez `sys_event_publish_deferred`, więc handler biegnie w zadaniu zdarzeń, nigdy w przerwaniu. Subskrypcja ze `source` = `display_id` filtruje panele. Handler tylko woła `lv_display_flush_ready` (ustawia flagę, nie bierze blokady LVGL) i musi znieść zdarzenie, które dotarło już po `unsubscribe` (sprawdza, czy konektor jest jeszcze zainstalowany). **Nieudany transfer też kończy się `FLUSH_DONE`** (inaczej LVGL zawiśnie), a jego błąd wraca **przy następnym FLUSH**, jak błąd asynchronicznego zapisu w `sys_remote`: sterownik zgłasza go do łańcucha błędów od razu, zapamiętuje, a kolejny `FLUSH` zwraca go raz (i go zeruje) **bez wysyłania nowego okna**. Konektor w `flush_cb` na każdy błąd robi to samo: `lv_display_flush_ready` i unieważnia cały ekran (`lv_obj_invalidate(lv_screen_active())`), więc zarówno okno, które się nie udało, jak i to odrzucone, rysują się od nowa. Błąd widać o jedną klatkę później, co dla panelu SPI i OLED-a wystarcza. RESET i RESUME zerują zapamiętany błąd.

## DISPLAY: create (zarys)

Packed, forma po drucie jak `pca9685_cfg_t`. Piny to `sys_io_pin_t` (urządzenie + numer), więc reset albo podświetlenie mogą siedzieć na ekspanderze. **Wyjątek:** `dc_pin` i `cs_pin` muszą być natywnymi GPIO (przełączane w każdej transakcji, ekspander jest za wolny): sterownik to sprawdza (`ERR_DEV_BAD_PIN`).

| Grupa | Pola | Uwagi |
|---|---|---|
| Magistrala | `bus_kind` (SPI, I2C), `bus_index`, `bus_freq_hz`, `spi_mode` (CPOL/CPHA), `i2c_addr` | tylko SPI i I2C: panele na magistrali równoległej (i80, RGB) nie są obsługiwane; pola obcej magistrali są ignorowane |
| Piny | `cs_pin`, `dc_pin`, `reset_pin`, `backlight_pin` | każdy opcjonalny (`SYS_IO_PIN_NONE`) poza `dc_pin` przy SPI |
| Podświetlenie | `bl_active_level`, `bl_default_duty` | PWM przez kontrakt IO urządzenia z `backlight_pin`, częstotliwość PWM to stała sterownika |
| Panel | `width`, `height` (natywne), `offset_x`, `offset_y`, `rgb_order` (RGB/BGR), `invert` | offsety, bo RAM kontrolera bywa większy niż szkło (240×240 na RAM 240×320). Format koloru zna sterownik (ST7789 i ILI9341: RGB565, SSD1306: MONO1) i zgłasza go w GEOMETRY |
| Orientacja | `orientation` (flagi: swap_xy, mirror_x, mirror_y) | ustawiana raz w create (rejestr MADCTL / remap OLED); obrót w trakcie pracy: `lv_display_set_rotation` tylko zamienia rozdzielczość i zgłasza `LV_EVENT_SIZE_CHANGED`, nie obraca; faktyczny obrót robi sprzęt panelu (sterownik) albo konektor programowo w `flush_cb` (zob. „Do rozstrzygnięcia”). Panel, który nie umie orientacji: `ERR_DEV_BAD_ARG` |
| Wariant | `variant` u8 | ten sam kontroler z różnymi szkłami ma inne tablice init i gamma; wybór gotowego wariantu zamiast wgrywania tablicy |

Reset jest aktywny stanem niskim, a jego czasy (trzymanie, odczekanie przed pierwszą komendą) to stałe sterownika z karty układu; to jedyne dozwolone „czekania” w create obok startu układu. Inwersja kolorów to `invert` w create albo własny op sterownika.

Rozmiar `display_cfg_t` to 35 B, `touch_cfg_t` ma 31 B.

Kolejność w `create()`: zadeklaruj urządzenia pinów i magistrali (`sys_device_use`) → reset sprzętowy → tablica init kontrolera (`{cmd, dane, opóźnienie}`, w kodzie sterownika) → format koloru, orientacja, offsety → **wyczyść RAM** (żeby po włączeniu nie było śmieci) → sleep-out (czeka układ) → wyświetlanie włączone → dopiero podświetlenie. `uninstall()`: podświetlenie wyłączone, panel uśpiony, magistrala zwolniona (każdy krok chroniony tym, co zbudowano, jak w `SYS_DEVICE_DRIVER.MD`).

## TOUCH: opy

Jeden palec: `lv_indev` typu pointer w LVGL v9 to jeden punkt (gesty LVGL liczy z niego sam), więc kontrakt nie ma wielu punktów, stanów DOWN/MOVE/UP, nacisku ani gestów.

| Op | Strona | Args | Odpowiedź | Uwagi |
|---|---|---|---|---|
| `0x0B00` POINT | R | – | `{u8 pressed, u16 x, u16 y}` (5 B) | jeden tani odczyt, wołany z `read_cb` LVGL co okres indev; `x` i `y` w pikselach panelu (0..`max_x`/`max_y` z create), przy `pressed` = 0 zachowują ostatnią pozycję |
| `0x0B01` CALIBRATION | RW | `{i16 x_min, x_max, y_min, y_max}` (surowe wartości ADC → 0..max) | to samo | tylko rezystancyjny; pojemnościowy: `ERR_DEV_BAD_ARG` |
| `0xFFxx` własne | | | | czułość / próg, tryb gestów, firmware układu |

Orientacja, czułość i reset są ustawiane raz w create (`orientation`) albo są sprawą sterownika; nie ma ich w opach. Oba opy są packed i małe, więc działają zdalnie przez `sys_remote`. SUSPEND wyłącza układ w tryb uśpienia (przerwanie dotyku może go wybudzić tylko w trybie, który sterownik zadeklaruje); RESUME budzi.

Brak zdarzeń: `read_cb` odpytuje POINT co okres indev (domyślnie 30 ms). Jeśli to za dużo ruchu na I2C, można dodać zdarzenie CHANGED z linii IRQ (deferred, `LOCAL`), po którym konektor czyta POINT raz, a indev przechodzi w tryb zdarzeniowy.

```c
/* konektor, w create: dotyk jako lv_indev */
lv_indev_t* in = lv_indev_create();
lv_indev_set_type(in, LV_INDEV_TYPE_POINTER);
lv_indev_set_read_cb(in, read_cb);
lv_indev_set_user_data(in, c);

static void read_cb(lv_indev_t* in, lv_indev_data_t* data) {
  conn_t* c = lv_indev_get_user_data(in);
  touch_point_t p;
  if (sys_touch_get_point(c->touch, &p) != OK) { data->state = LV_INDEV_STATE_RELEASED; return; }
  data->point.x = p.x;
  data->point.y = p.y;
  data->state = p.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
```

## TOUCH: create (zarys)

| Grupa | Pola | Uwagi |
|---|---|---|
| Magistrala | `bus_kind` (I2C, SPI), `bus_index`, `freq_hz`, `i2c_addr` albo `cs_pin` | I2C: układy mają stałe adresy, ale kolizje zdarzają się (adres z pinu) |
| Piny | `irq_pin`, `reset_pin` | `irq_pin` natywne GPIO, opcjonalny: linia aktywna stanem niskim, z nią POINT nie czyta magistrali, gdy nikt nie dotyka; `reset_pin` dowolny. Czasy resetu to stałe sterownika |
| Obszar | `max_x`, `max_y` | rozmiar, w którym POINT odpowiada: szerokość i wysokość panelu w orientacji 0. Sterownik skaluje do niego natywny zakres układu, więc konektor nie potrzebuje geometrii dotyku |
| Orientacja | `orientation` (flagi: swap_xy, mirror_x, mirror_y) | ustawiana raz, ta sama wartość co w panelu, żeby (0,0) dotyku był tam, gdzie (0,0) panelu; przy obrocie ekranu współrzędne dotyku dopasowuje konektor (LVGL obrotu nie wykonuje; do sprawdzenia, czy indev dostaje punkt już po obrocie) |
| Rezystancyjny | `cal` jak CALIBRATION, `pressure_min` | pozostałe układy to pole ignorują |

Kolejność w `create()`: `sys_device_use` dla magistrali i pinów → reset układu → odczyt identyfikatora (jeśli układ ma: szybko wykrywa zły adres albo kabel) → tryb pracy → przerwanie podłączone na końcu.

## Konektor LVGL: opy

`sys_lvgl` nie jest urządzeniem (nie ma go w rejestrze, nie ma create struct ani lifecycle SUSPEND/RESUME). To moduł z własnym kluczem i routerem, jak `sys_power_manager`: [lvgl_connector.h](contracts/lvgl_connector.h). Ops to komendy `[ROUTER_KEY_LVGL][op][args]`, ustawienie czyta to, co zapis przyjmuje, a ramki są zapisywalne jako rekordy, więc projekt odtwarza INSTALL przy starcie.

Przy starcie płytki LVGL jest zainicjowany i **pusty**: `lv_init` z małą stertą startową, bez wyświetlacza. INSTALL podłącza panel, RESET wraca do stanu pustego.

| Op | Strona | Args | Uwagi |
|---|---|---|---|
| `0x01` INSTALL | RW | `{u16 display, u16 touch, u32 heap_bytes}` | zapis buduje wszystko (sekcja niżej); `touch` = `SYS_DEVICE_ID_NONE`: bez dotyku; `heap_bytes` to sterta obiektów w bajtach, 0 = domyślna; zapis przy zainstalowanym jest odrzucony (najpierw RESET). Odczyt: co jest zainstalowane (NONE i 0, gdy nic) |
| `0x02` RESET | W | – | zdejmuje wyświetlacz i dotyk, zwalnia bufory i obiekty, wraca do stanu pustego (gotowy na następny INSTALL); bez zainstalowanego nic nie robi |
| `0x03` ROTATION | RW | `{u8 0..3}` | `lv_display_set_rotation` zamienia rozdzielczość, sam obrót robi sprzęt panelu albo konektor w `flush_cb` (do rozstrzygnięcia); w każdej chwili |
| `0x04` STATUS | R | `{installed, running, width, height, buffer_bytes, heap_total, heap_free, flushes, flush_errors}` | do diagnozy pamięci i flushy w aplikacji |
| `0x05` START | W | – | wznawia renderowanie i odczyty dotyku, unieważnia cały ekran (panel mógł stracić zawartość) |
| `0x06` STOP | W | – | czeka na ostatni `FLUSH_DONE`, zatrzymuje renderowanie; `read_cb` zwraca „puszczony”. Obiekty, bufory i subskrypcja zostają. Po INSTALL konektor jest uruchomiony, RESET zatrzymuje i rozbiera |

Jeden wyświetlacz na razie. Router odpowiada tylko za te sześć ops: ekran z ramek (słownik widgetów, loader, pokaż/usuń) to osobna warstwa 4 z własnym routerem, a powiązania obiekt → widget to warstwa 5 (adapter VM).

Obok komend jest interfejs C bez drutu (ten sam nagłówek): `sys_lvgl_init` (krok bootu), `sys_lvgl_lock` i `sys_lvgl_unlock` (blokada LVGL dla warstwy 4 i adaptera VM; obiektów `lv_*` dotyka się tylko pod nią) oraz funkcje `sys_lvgl_install`, `_reset`, `_start`, `_stop`, `_set_rotation`, `_get_status`, które wołają ops routera i `runit` (stan bezpieczny: STOP, potem START).

## Kto alokuje bufory

**Bufor rysowania (to, co idzie do FLUSH) należy do konektora**, nie do sterownika panelu, i konektor **sam go wylicza**: w INSTALL nie ma żadnych parametrów bufora, tylko `heap_bytes` dla obiektów. Alokuje go **raz, przy INSTALL** (nie przy tworzeniu panelu i nie w czasie pracy), bo dopiero konektor zna LVGL, a sterownik zna tylko swoje ograniczenia (GEOMETRY); zwalnia go dopiero RESET. Obiektów konektor nie alokuje: robi to **własny alokator LVGL** (niżej), a konektor ustawia mu tylko budżet.

Kolejność przy starcie:

1. **CREATE panelu** (sterownik DISPLAY): magistrala, reset, init układu. Nie alokuje żadnego bufora na obraz.
2. **INSTALL** (zapis `{display, touch, heap_bytes}`): konektor sprawdza, że to urządzenia właściwych kontraktów, czyta DISPLAY GEOMETRY i wylicza bufor:
   - tryb: zawsze partial (mało RAM-u, wszystkie panele przyjmują okno);
   - rozmiar: 1/10 wysokości ekranu w wierszach, zaokrąglone w górę do wielokrotności `align_y` (SSD1306: do pełnej strony po 8 wierszy), jeden bufor to `width * wiersze * bajty na piksel` (według `color_format`);
   - liczba: 2 (jeden wysyłany, drugi renderowany), a 1, gdy wolnej wewnętrznej pamięci zabraknie na drugi z zapasem dla reszty systemu;
   - pamięć: wybiera ją konektor (konfiguracja płytki), nie wyświetlacz: RAM wewnętrzny albo PSRAM. Wyświetlacz zgłasza tylko flagę `DISPLAY_FLAG_DMA`: z nią bufor ma być zdolny do DMA (`MALLOC_CAP_DMA`, także w PSRAM), bez niej może leżeć gdziekolwiek. Adres wyrównany do `LV_DRAW_BUF_ALIGN` (domyślnie 4; inaczej asercja LVGL);
   - sterta obiektów: limit ustawiony na `heap_bytes` (zob. niżej).
3. Konektor woła `lv_display_create(width, height)`, `lv_display_set_buffers(...)`, `lv_display_set_flush_cb(...)`, tworzy `lv_indev`, subskrybuje `FLUSH_DONE` i dopiero wtedy włącza podświetlenie. Nieudana alokacja to błąd INSTALL z wycofaniem (to, co zbudowano, jest zwalniane, LVGL zostaje pusty).

Zasady:
- **Bufor żyje, dopóki trwa FLUSH.** Wskaźnik w `display_flush_args_t` ma być ważny i niezmieniany do `FLUSH_DONE`; konektor nie zwalnia i nie zmienia bufora, który jest w locie, a w trybie dwóch buforów renderuje do drugiego.
- **RESET.** Czeka na ostatni `FLUSH_DONE`, odsubskrybowuje zdarzenie, usuwa `lv_indev` i `lv_display` (a z nimi wszystkie obiekty ekranu), zwalnia bufory i wraca ze stertą obiektów do startowej. `lv_deinit` nie jest wołane: LVGL zostaje zainicjowany. Panel nie powinien dać się usunąć, dopóki jest zainstalowany (otwarte pytanie niżej).
- **Obrót.** Bufor częściowy ma rozmiar w bajtach, więc zmiana rotacji w LVGL go nie zmienia (szerokość z wysokością się zamieniają, bajtów tyle samo). Obrót programowy w `flush_cb` potrzebuje jeszcze drugiego bufora na wynik (`lv_draw_sw_rotate` i `lv_display_rotate_area`, rozmiar jednego bufora rysowania).
- **Wyrównanie i kolejność.** Bufor wyrównany do `LV_DRAW_BUF_ALIGN` (domyślnie 4), inaczej asercja. Rozdzielczość i format koloru ustawia się przed `lv_display_set_buffers`, bo w trybie partial wysokość bufora to `buf_size / stride`.
- **Sterownik alokuje tylko to, co jego.** Gdy magistrala nie może wysłać całego okna naraz albo bufor jest w PSRAM, a DMA go nie widzi, sterownik w `create()` bierze mały bufor pośredni (rozmiaru jednej transakcji) z RAM-u DMA, kopiuje do niego kawałkami i zwalnia go w `uninstall()`. `FLUSH_DONE` publikuje po ostatnim kawałku. Do tego ma prawo także konwersja (zamiana bajtów, format MONO1). Bufora na cały ekran sterownik nigdy nie trzyma.
- **Sterta obiektów.** Widgety, style i cache alokuje **własny alokator LVGL** (w playgroundzie wbudowany: `LV_STDLIB_BUILTIN`, pula `LV_MEM_SIZE` 64 kB), osobna pula od buforów rysowania, tworzona i zwalniana w czasie pracy; w firmware sterowana własnymi hookami malloc w `lv_conf.h` (`LV_STDLIB_CUSTOM`) nad `heap_caps_malloc` z budżetem (licznikiem), który odrzuca alokację ponad limit; z niego `STATUS` liczy `heap_total` i `heap_free`. Na starcie limit to mała sterta startowa (globalne struktury `lv_init`), INSTALL ustawia go na `heap_bytes` (0: wartość domyślna z Kconfig), RESET przywraca startowy. Rozmiaru wbudowanego alokatora (`LV_MEM_SIZE`) nie da się zmienić w czasie działania, dlatego hooki. Sprawdzone w źródłach LVGL v9.3.0: `LV_STDLIB_CUSTOM` wymaga dziewięciu funkcji (`lv_mem_init`, `lv_mem_deinit`, `lv_mem_add_pool`, `lv_mem_remove_pool`, `lv_malloc_core`, `lv_realloc_core`, `lv_free_core`, `lv_mem_monitor_core`, `lv_mem_test_core`); `lv_init` woła `lv_mem_init`, a `lv_deinit` `lv_mem_deinit`; `lv_mem_monitor` czyta z `lv_mem_monitor_core`, więc `STATUS` może się na nim opierać. Alternatywa bez własnych hooków: wbudowany alokator (TLSF) z małą pulą startową, `lv_mem_add_pool` w INSTALL i `lv_mem_remove_pool` w RESET (obie istnieją; usunięcie puli jest bezpieczne tylko wtedy, gdy nic w niej nie zostało, czego LVGL nie sprawdza). **Alokacja ponad limit:** `lv_malloc` zwraca NULL, ale kod LVGL sprawdza to asercją (`LV_ASSERT_MALLOC`), a domyślny `LV_ASSERT_HANDLER` to `while(1);`, więc bez własnego handlera przekroczony budżet zawiesza system.

## Jak sterownik jest podpięty do bufora

**Nie jest.** Sterownik nie zna bufora i niczego nie rejestruje: konektor podaje wskaźnik w każdym wywołaniu FLUSH (`display_flush_args_t.pixels`), a sterownik czyta z niego dokładnie okno i oddaje je magistrali. Bufor zna tylko LVGL (`lv_display_set_buffers`) i konektor. Łącznikiem są dwie rzeczy: `flush_cb` w jedną stronę i zdarzenie `FLUSH_DONE` w drugą.

```c
/* konektor, w INSTALL: bufory i wyświetlacz LVGL */
disp = lv_display_create(geo.width, geo.height);
lv_display_set_color_format(disp, lv_format(geo.color_format));
lv_display_set_buffers(disp, buf[0], buf[1], buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
lv_display_set_flush_cb(disp, flush_cb);
lv_display_set_user_data(disp, c);              /* c: display_id, disp, swap */
sys_event_subscribe_fn(display_id, DISPLAY_EV_FLUSH_DONE, SYS_EVENT_ANY_CHANNEL, on_flush_done, c, &c->flush_sub);

/* LVGL -> panel: z zadania LVGL, wewnątrz lv_timer_handler() */
static void flush_cb(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
  conn_t* c = lv_display_get_user_data(d);
  display_window_t w = {a->x1, a->y1, a->x2, a->y2};
  if (c->swap) lv_draw_sw_rgb565_swap(px, lv_area_get_size(a));   /* c->swap = (color_format == RGB565) */
  if (sys_display_flush(c->display, &w, px) != OK) {              /* wraca od razu, bez czekania */
    report(...); lv_display_flush_ready(d);                        /* inaczej LVGL zawiśnie */
  }
}

/* panel -> LVGL: słuchacz konektora (zadanie zdarzeń), tylko to */
static err_h on_flush_done(const sys_event_t* ev, void* ctx) { conn_t* c = ctx; if (c->attached) lv_display_flush_ready(c->disp); return OK; }
```

Wynikają z tego reguły dla sterownika:
- **Jeden FLUSH w locie.** LVGL nie woła `flush_cb` ponownie, dopóki nie dostanie `flush_ready`, więc sterownik obsługuje jedno okno naraz; drugi FLUSH przed FLUSH_DONE to błąd „zajęty”, nie kolejka.
- **FLUSH wraca po przyjęciu** (DMA ruszyło), nie po wysłaniu. Czeka tylko własne I/O; resztę zgłasza przerwanie.
- **Sterownik nie trzyma wskaźnika po FLUSH_DONE** i niczego nie zakłada o czasie życia bufora poza tym oknem.
- **Zamiana bajtów** robi konektor na buforze przed FLUSH (dla RGB565), sterownik dostaje piksele już w kolejności, której chce.
- **Blokady.** `flush_cb` jest pod blokadą LVGL i bierze blokadę rejestru (przez `sys_device_call`); słuchacz FLUSH_DONE woła tylko `lv_display_flush_ready` i nie bierze żadnej z nich, więc kolejność blokad się nie odwraca.

Gdy sterownik będzie potrzebował znać bufor wcześniej (panel RGB z własnym framebufferem i deskryptorami DMA), dojdzie osobny op rejestrujący bufory przy create konektora. Przy SPI i I2C nie jest potrzebny.

## DMA

DMA jest sprawą sterownika i magistrali; konektor widzi z niego tylko flagę DMA z GEOMETRY oraz zdarzenie FLUSH_DONE. Schemat dla SPI (ESP32-S3):

1. **Konektor alokuje bufor tak, żeby DMA go widziało** (reguły wyżej): pamięć zdolna do DMA (RAM wewnętrzny albo PSRAM, wedle konfiguracji konektora). LVGL renderuje do niego; nic nie jest kopiowane do osobnego bufora DMA.
2. **FLUSH** (w zadaniu LVGL) sprawdza okno (`ERR_DEV_RANGE`, wyrównanie) i wskaźnik (`ERR_DEV_BAD_ARG`, gdy źle wyrównany albo w pamięci, której DMA nie widzi), po czym:
   - wysyła krótkie komendy ustawienia okna i początku zapisu pamięci bez DMA (D/C przełączane w callbacku transakcji: dlatego `dc_pin` musi być natywnym GPIO);
   - kolejkuje transakcje z pikselami **kawałkami nie większymi niż limit jednej transakcji** magistrali (ustawiony przy otwarciu magistrali), wszystkie naraz, żeby DMA nie czekało na zadanie między kawałkami;
   - wraca, nie czekając.
3. **Callback „ostatnia transakcja skończona”** (z przerwania) publikuje `FLUSH_DONE` deferred; nie woła nic, co blokuje.
4. **Czas na magistrali:** `bajty * 8 / częstotliwość`. Pełny ekran 240×320 RGB565 przy 40 MHz to ok. 15 ms, więc LVGL renderuje następny fragment, gdy poprzedni jeszcze leci: stąd dwa bufory.

Bez DMA (I2C OLED, a na ESP32-S3 I2C nie ma DMA) sterownik wysyła w kawałkach z przerwania albo zadania transakcji, a FLUSH_DONE publikuje po ostatnim; flaga `DMA` nie jest ustawiona i bufor może być gdziekolwiek.

Sterownik nigdy nie kopiuje całego okna do własnego bufora. Mały bufor pośredni z RAM-u DMA bierze tylko wtedy, gdy konektor mimo wszystko dał bufor, którego DMA nie widzi (np. konwersja formatu); to wyjątek, nie droga domyślna.

## Co konektor bierze z kontraktów

| LVGL / playground (`main.c`, „platform glue”) | Skąd na płytce |
|---|---|
| `lv_display_create(w, h)` i format koloru | DISPLAY GEOMETRY |
| rozmiar i miejsce buforów | GEOMETRY `align_y`, flaga DMA; **alokuje konektor** (sekcja wyżej) |
| `flush_cb` | DISPLAY FLUSH; `flush_ready` z FLUSH_DONE |
| zamiana bajtów | GEOMETRY `color_format` (konektor zamienia zawsze dla RGB565) |
| `lv_indev` `read_cb` | TOUCH POINT (jeden palec; sekcja „TOUCH: opy”) |
| `lv_tick_set_cb` | zegar płytki (`core/time`), nie kontrakt |
| podświetlenie, usypianie | BACKLIGHT, SUSPEND i RESUME |
| wątek LVGL i blokada | konektor: jedno zadanie, reszta pod mutexem (zasada „op nie czeka” z `SYS_DEVICE.MD`); LVGL ma wbudowane `lv_lock` i `lv_unlock` (przy `LV_USE_OS` = `LV_OS_FREERTOS`), które `lv_timer_handler` bierze sam, więc `sys_lvgl_lock` może je opakować |

## Do rozstrzygnięcia

- **Usunięcie zainstalowanego panelu.** Konektor nie jest urządzeniem, więc nie wiadomo, czy może zadeklarować `sys_device_use` na panel i dotyk (zablokować ich REMOVE). Albo rejestr to przyjmie od modułu, albo konektor dostaje powiadomienie o usunięciu i sam się odłącza, albo panel jest oznaczony jako systemowy (`ERR_DEV_SYSTEM`) jak urządzenia płytki.
- **Klucz routera.** Następny wolny w `router_ids.h` (0x09 się nie przydziela), razem z `contract_lvgl.json` i modułem błędów (`ERR_LVGL_*`: brak pamięci, już zainstalowany, zły kontrakt, za mała sterta).
- **Błąd transferu.** Wraca z następnego FLUSH (sekcja „Kto słucha”). Gdyby konektor miał reagować od razu, bez czekania na kolejny flush, błąd musiałby jechać w zdarzeniu: wartość `FLUSH_DONE` jako `{licznik, kod błędu}`.
- **Callback zamiast zdarzenia.** Gdyby przeskok przez zadanie zdarzeń okazał się za wolny: `done_cb` i `done_ctx` w `display_flush_args_t` (FLUSH jest i tak lokalny), wołane z przerwania w IRAM.
- **Znaczniki publikacji.** Do `enums.json` mają trafić `display_bus_kind_e`, `display_rgb_order_e`, `DISPLAY_ORIENTATION_*` i `display_color_format_e` (`//#ref-enum` / `//#ref-defines`, `@alias` przy każdym członku); w nagłówku są na razie tylko przy magistrali.
- **Piksele w FLUSH.** Rama po drucie nie unosi bufora ekranu, a `sys_device_call` zna argumenty „w miejscu”. W nagłówku: `display_flush_args_t` ma wskaźnik do bufora LVGL (bez kopii) i nie jest packed, więc FLUSH jest tylko wewnętrzny i z drutu ma być odrzucany (jeszcze nie ma na to mechanizmu w rejestrze: op oznaczony „internal”); do testów z aplikacji osobny op z małym oknem w ramce.
- **Magistrala jako urządzenie.** Czy `sys_spi` to osobny moduł jak `sys_i2c`, czy sterownik otwiera magistralę sam z `bus_kind`.
- **Wiele punktów dotyku.** Kontrakt ma jeden punkt, bo `indev` pointer w LVGL v9 jest jednopunktowy. Multitouch (GT911 ma 5 punktów) to osobny indev na palec albo gesty układu: wtedy własny op `0xFFxx` albo nowa wersja POINT.
- **Kolor i bajty.** Kto zamienia bajty RGB565: sterownik (przy zapisie), konektor (przed FLUSH), czy LVGL (`lv_draw_sw_rgb565_swap`). Propozycja: konektor, dla każdego RGB565.
- **Obrót w czasie pracy.** `lv_display_set_rotation` niczego nie obraca. Albo sterownik panelu dostaje op zmiany orientacji (sprzęt, bez dodatkowego bufora), albo konektor obraca programowo w `flush_cb` (drugi bufor na wynik). Kontrakt ma dziś orientację tylko w create.
- **Asercja LVGL przy braku pamięci.** Własny `LV_ASSERT_HANDLER` (zgłoszenie błędu, STOP) zamiast `while(1);`, albo budżet z zapasem, żeby przekroczenie nie zatrzymywało systemu.
- **Dane PCB.** Pustych miejsc w tabelach nie da się domknąć bez konkretnego panelu i dotyku: rozdzielczość, kontroler, interfejs, piny.
