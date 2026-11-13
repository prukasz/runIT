# Rejestr urządzeń (rework) — propozycja zmian po przeglądzie

Przegląd z czytania kodu rejestru i komponentów wokół niego, dziś w `runit-esp-rework/components` (`core/router`,
`core/router_queue`, `core/timer`, `sys/sys_device` z satelitami, `sys/sys_event`, `sys/bus/sys_i2c`, `sys/sys_io`,
`devices/*`). Rework buduje się (`build.ps1`); na płytce nic nie było uruchamiane.

## Założenia

- Rejestr ma jeden rodzaj blokady: mutex per urządzenie (slot), nierekursywny. Op może go pominąć flagą
  `SYS_DEVICE_OP_NOLOCK`. Blokady rekursywnej nie ma i nie będzie.
- Brak elementów compile time per konfiguracja płytki: każda płytka ma ten sam firmware.
- Sterownik jest ostatnim ogniwem: nikt go nie includuje, sam rejestruje typ, opy i eventy.
- Wstecznej zgodności nie ma: zmiana może wymagać przebudowy reszty.

## Kolejność prac

| # | Zmiana | Stan |
|---|---|---|
| 1 | Eventy z opa dostarczane po zwolnieniu locka | Wdrożone |
| 2 | `sys_device_sweep` | Wdrożone |
| 3 | Flaga `removing` w slocie | Wdrożone |
| 4 | Kolejność CREATED / REMOVED | Wdrożone (generacja slotu w evencie) |
| 5 | Kontrola cykli zależności | Wdrożone |
| 6 | Tożsamość wywołującego | Wdrożone (`sys_device_caller()`, właściciel pinu i adresu, egzekwowanie) |
| 7 | Zasoby zwalniane przez rejestr | Wdrożone |
| — | Lock slotu z timeoutem, własne tagi błędów | Wdrożone |
| — | Unikalność ID kontraktów bez centralnej listy | Wdrożone |
| 8 | Izolacja modułów (typ GPIO, `TAIL`, moduły eventów, interceptor) | Wdrożone; moduły błędów i klucze routera do zrobienia |
| 9 | Podział `devices/`, wspólne taski | Wspólne taski wdrożone (krok urządzenia, sprawiedliwy task timerów); podział `devices/` do zrobienia |
| 12 | Automatyzacja wzorców sterownika i kontraktu | 12.1–12.4, 12.7 i 12.6 bez `sys_device_self()` wdrożone; reszta przy migracji sterowników |

„Wdrożone” znaczy: napisane w `runit-esp-rework` i zbudowane na cel, nieuruchomione na płytce (wyjątek: testy hosta
timera, `core/timer/host`). Opis stanu obecnego jest w `sys_device/SYS_DEVICE.MD`; ten plik zostaje jako uzasadnienie
zmian i lista rzeczy otwartych.

**Następne kroki:** `sys_device_self()` z 12.6 razem z 12.9; 12.8 przy migracji pierwszego sterownika z eventem pinu
(INA3221, DRV8962, TPS55289, TCA6424A).

Co trzeba sprawdzić po pierwszym buildzie: sekcja 11.

---

## 1. Eventy a lock per urządzenie

**Problem.** Wywołania biorą locki w kolejności user → dep (PCA9685 trzyma swój lock i woła szynę). Event opublikowany
inline z wnętrza opa bierze je odwrotnie: publikujący dep trzyma swój lock, a listener (`ON_EVENT` usera, subskrybowany
jako `SYS_EVENT_RUN_INLINE` w `sys_io_claim.c`) bierze lock usera.

- Dwa taski: task 1 trzyma U i czeka na D, task 2 trzyma D i czeka na U. Deadlock.
- Jeden task: U trzyma swój lock, woła D, D publikuje inline, listener woła `ON_EVENT` na U. `held_by_me` zwraca błąd,
  event przepada.

**Stan: wdrożone.** Publikujący wybiera inline albo `SYS_EVENT_DEFER` według obciążalności
swojego taska (task routera: inline; task, na który op może czekać, albo task o twardym czasie: defer). Niezależnie od
tego wyboru listener inline nigdy nie biegnie, gdy publikujący task trzyma lock slotu.

- `sys_event` ma wstrzymanie per task: `sys_event_hold(&hold, buf, cap)` i `sys_event_release(&hold)`. W TLS leży tylko
  wskaźnik; miejsce na eventy daje wołający ze swojego stosu. Publikacja bez `DEFER` w trakcie wstrzymania trafia do
  bufora, a gdy się nie mieści, do taska eventów.
- `sys_device_call` liczy zagnieżdżenie wywołań taska. Najbardziej zewnętrzne wywołanie (`call_outermost`) otwiera
  wstrzymanie z buforem `CONFIG_SYS_DEVICE_EVENT_HOLD_BYTES` (256 B) na własnej ramce i zwalnia je po powrocie, także
  gdy op zwrócił błąd. Nie po każdym opie: gdy U woła D, event z D wysłany po odblokowaniu D trafiłby na lock U.
- `publish_change()` rejestru idzie tą samą drogą bez zmian w kodzie: publikuje wewnątrz wywołania.
- Poza opem inline działa jak wcześniej, od razu.

Skutki dla sterownika:

- „Inline” znaczy „w tasku publikującego, po powrocie z jego zewnętrznego wywołania”, nie „zanim op się skończy”. Op,
  który potrzebuje reakcji przed własnym końcem, woła tamto urządzenie wprost.
- Wstrzymana publikacja zwraca sukces; błąd listenera i pełna kolejka wychodzą przy dostarczaniu, przez `err_report`.
- Op `NOLOCK` biegnie bez locka, ale wewnątrz wywołania: długo blokujący op przetrzyma swoje eventy do powrotu, więc
  publikuje z `DEFER`.

`sys_device_sweep` też otwiera wstrzymanie, na cały przebieg. Opis mechanizmu: `SYS_EVENT.MD` („Holding”) i
`AGENTS.md` w `sys_event`.

## 2. `sys_device_sweep` i safe state

**Problem.** `sys_device_sweep` jest zadeklarowany w `sys_device.h` i używany przez `sys_device_policy` oraz power
managera, ale w pierwszej wersji rejestru nie miał implementacji. Przy pisaniu trzeba rozstrzygnąć trzy rzeczy:

- **Blokowanie.** `sys_device_slot_lock()` czeka z `portMAX_DELAY`. Sweep SUSPEND stanie za każdym długim opem.
  Przykład: `SCAN` trzyma lock szyny przez 112 prób × `CONFIG_ESP32S3_I2C_PROBE_TIMEOUT_MS`, a hooki `suspend()` chipów
  potrzebują tej szyny.
- **Reentrancja.** Jeśli `err_report()` z błędem CRITICAL padnie na tasku, który trzyma lock urządzenia,
  `sys_device_policy_handle()` → sweep dostanie błąd reentrancji na urządzeniu, które zawiodło.
- **Kolejność.** Komentarz mówi „newest first”, ale sloty są indeksowane po ID i nie pamiętają kolejności tworzenia.

**Stan: wdrożone.** `sys_device_sweep` jest w `call/sys_device_call.c`.

- **Kolejność:** topologiczna po maskach `used_by` (user przed depem, RESUME odwrotnie).
- **Lock z timeoutem:** `CONFIG_SYS_DEVICE_LOCK_TIMEOUT_MS` (5000) dla każdego wywołania. Po timeoucie
  `ERR_DEV_BUSY{id, op}` z opem, który trzyma lock, a sweep idzie dalej. Reentrancja ma własny tag `ERR_DEV_REENTRY`.
- **Odcięcie zapisów:** inaczej niż w pierwotnej propozycji. Ustawienie `suspended` wszystkim przed hookami zepsułoby
  same hooki: `suspend()` PCA9685 pisze poziom (`LEVEL`) na pin OE, a `suspend()` silnika `COAST` na mostek, i żaden z
  tych opów nie jest `ALWAYS`. Zamiast tego na czas sweepa SUSPEND działa bramka: zapis, który nie pochodzi od
  urządzenia (komenda, VM, moduł), oraz CREATE są odrzucane na każdym urządzeniu (`ERR_DEV_SUSPENDED`), a wywołania
  urządzenie → urządzenie przechodzą. Rozróżnienie daje `sys_device_caller()`.
- **Eventy:** jeden na sweep (źródło `SYS_DEVICE_ID_NONE`), po ostatnim urządzeniu; REMOVE publikuje REMOVED każdego
  urządzenia. Sweep trzyma eventy tak jak zewnętrzne wywołanie.
- **Pomijane:** REMOVE zostawia urządzenia systemowe, RESET urządzenia bez opa RESET.

**Policy na własnym tasku: wdrożone.** `sys_device_policy_handle()` na tasku zgłaszającym nic nie uruchamia: sprawdza,
czy łańcuch wymaga reakcji, i jeśli tak, kopiuje go jako rekord (`err_record`) do kolejki. Task policy (priorytet 14)
odtwarza łańcuch (`err_record_parse`) i robi sweep oraz akcje, nie trzymając żadnego locka urządzenia. Pełna kolejka
daje `ERR_DEV_FAULT_RESPONSE{stage QUEUE}`.

Krok urządzenia (timer, punkt 9) przychodzi spoza urządzenia, więc w trakcie sweepa SUSPEND jest pomijany: rampa
silnika nie woła już mostka między początkiem sweepa a własnym `suspend()`.

`SCAN` szyny I2C jest `NOLOCK` (flaga w tabeli kontraktu): 112 prób, każda do timeoutu próby, trzymałoby lock dłużej
niż jego timeout i blokowało transfery chipów. Bez locka urządzenia każda próba bierze tylko lock szyny w sterowniku
ESP-IDF, transfery chipów wchodzą między próby, a REMOVE czeka na `SCAN` w locie. `SCAN` czyta tylko uchwyt
kontrolera, który żyje do REMOVE. Wybrane zamiast dłuższego timeoutu: ten musiałby rosnąć z timeoutem próby, a
zajęta szyna i tak stałaby przez cały `SCAN`.

## 3. Wyścig REMOVE kontra `sys_device_use`

**Problem.** REMOVE sprawdza `used_by == 0`, potem robi `drain`, `uninstall()` i dopiero `sys_device_slot_uninstall()`
gasi `installed`. W tym oknie inny task w `create()` woła `sys_device_slot_use(user, dep)`: `dep` jest jeszcze
`installed`, więc dostaje sukces. Po chwili `dep` znika, a `user` ma zależność do wolnego slotu i wskaźnik do zwolnionej
pamięci (`has_op(instance)` w `sys_device_require_op`).

**Stan: wdrożone.** `sys_device_slot_begin_remove()` sprawdza `used_by == 0` i ustawia flagę `removing` w tej samej
sekcji krytycznej, w której `sys_device_slot_use()` deklaruje użycie; `use` odrzuca urządzenie z tą flagą
(`ERR_DEV_NOT_FOUND`). Flagę kasuje `sys_device_slot_uninstall()` razem z resztą slotu.

## 4. Stan odtwarzany z eventów

**Problem.** `publish_change()` działa po `unlock`. Task A robi REMOVE(5) i zostaje wywłaszczony przed publikacją.
Task B robi CREATE(5) i publikuje CREATED. Potem A publikuje REMOVED. `sys_device_policy` uzna, że urządzenia 5 nie ma.

**Stan: wdrożone.** Slot ma generację (`gen`, 1..255), która rośnie na początku każdego CREATE (zanim `create()`
ją rozda, np. timerowi) i zostaje po zwolnieniu.
CREATED i REMOVED niosą ją w payloadzie. `sys_device_policy` zapamiętuje generację z CREATED i przyjmuje REMOVED tylko
z tą samą generacją, więc spóźnione REMOVED poprzedniego urządzenia nie kasuje nowego.

Przy okazji poprawione: zmiany stanu były publikowane z flagami 0, więc SUSPENDED / RESUMED / RESET traciły
`SYS_EVENT_NOTIFY`. Teraz idą przez `SYS_EVENT_PUBLISH` z flagami z tabeli.

## 5. Cykle zależności

**Problem.** `sys_device_use()` wymaga tylko locka `user`, więc da się go wywołać z opa. Wtedy A może zacząć używać B,
które już używa A. Żadnego z nich nie da się usunąć, a kolejność sweepa jest nieokreślona.

**Stan: wdrożone.** `sys_device_slot_use()` sprawdza pod `s_table_lock`, czy `user` jest osiągalny z `dep` przez maski
`uses`, i odmawia z `ERR_DEV_CYCLE{id, dep}`. `use` z opa zainstalowanego urządzenia nadal jest dozwolone.

## 6. Tożsamość wywołującego

**Problem.** Handler nie wie, kto woła. Skutki:

- Każdy może zrobić `DETACH` lub `XFER` na adresie cudzego chipa.
- Każdy lokalny kod może zrobić `UNLOCK` lub `LOCKED_LEVEL` na cudzym pinie.
- `ATTACH` i `DETACH` nie są `LOCAL`, więc aplikacja może komendą zająć adres bez właściciela, którego nikt nie zwolni.

**Stan: wdrożone.** Rejestr pamięta per task, którego urządzenia op biegnie i kto je zawołał:
`sys_device_caller()` zwraca urządzenie wołające albo `SYS_DEVICE_ID_NONE` (komenda, moduł, listener). W `create()`
jest to tworzone urządzenie. Sygnatury handlerów się nie zmieniły.

- **`ATTACH` i `DETACH` są `LOCAL`** (jak `XFER`): komenda ich nie wyśle i nie ma ich w `OPS`. Diagnostyka z aplikacji
  zostaje przy `RAW`, `PROBE`, `SCAN`.
- **Właściciel przy zasobie.** Pin: `sys_io_pins_t.owner[64]`, zapisywany w `sys_io_pins_lock()`. Adres I2C:
  `owner[128]` w szynie (`device_esp32s3_i2c`), zapisywany w `ATTACH`. Właścicielem jest `sys_device_caller()` opa,
  który zajął zasób. Błąd następnego chętnego go podaje: `ERR_DEV_PIN_LOCKED{id, pin, owner}`,
  `ERR_I2C_ADDR_TAKEN{bus, addr, owner}` (pole `owner` jest w deskryptorach jako ID urządzenia).
- **Egzekwowanie.** Zapisy IO zablokowanego pinu tylko dla właściciela (`sys_io_pins_check_free`:
  `ERR_DEV_PIN_LOCKED{owner}` dla pozostałych; osobnego opa `LOCKED_LEVEL` nie ma, patrz 12.7), `UNLOCK` tylko dla
  właściciela (`ERR_DEV_PIN_NOT_OWNED{id, pin, owner}`), `XFER` i `DETACH` tylko dla właściciela adresu
  (`ERR_I2C_NOT_OWNER{bus, addr, owner}`). `RAW` zajmuje adres na jeden transfer wewnątrz szyny (właściciel
  `SYS_DEVICE_ID_NONE`) i nie przechodzi przez te opy.

Skutek dla sterownika: do zajętego pinu i adresu sięga tylko z wnętrza opa swojego urządzenia (`create()`, `suspend()`,
`resume()`, op timera, `ON_EVENT`). Własny task sterownika wołający szynę wprost dostanie `ERR_I2C_NOT_OWNER`.

GPIO ESP32-S3 (`device_esp32s3_gpio`) jest w rework na `sys_io_pins_t`: pin zarezerwowany w ESP-IDF przez natywny
peryferial (flash, PSRAM, USB-Serial-JTAG) jest zablokowany od `CREATE` z właścicielem `SYS_DEVICE_ID_NONE`, a `MODE`
pinu zarezerwowanego później daje `ERR_DEV_PIN_LOCKED{owner SYS_DEVICE_ID_NONE}`. Piny szyny I2C blokuje szyna
(`sys_io_claim_peripheral`), z sobą jako właścicielem. Op `LOCKED_LEVEL` zniknął też tu (12.7).

## 7. Zasoby zwalniane przez rejestr

Wzór: `devm_*` i device links w Linuksie.

**Problem.** Każdy sterownik pisze `uninstall()` ręcznie („każdy krok strzeżony tym, co zbudowano”), a `sys_io_claim_t`
odtwarza to flagami `used / locked / subscribed / keep`. Pominięty krok to wyciek pinu, adresu albo subskrypcji.

**Stan: wdrożone.** Typ nie ma `uninstall()`. Zasób rejestruje się na ID właściciela
(`sys_device_res_add(owner, &node, release, arg)`), węzeł listy leży w strukturze sterownika, bez alokacji.

- Zasób jest dodawany przed zajęciem, a jego `release` jest strzeżony własnym stanem zasobu (`attached`, `locked`,
  `bus != NULL`), więc oddaje tylko to, co naprawdę zajęto.
- Zasobami są: claim pinu (`sys_io_claim_*`, razem z subskrypcją), adres I2C (`sys_i2c_dev_attach`), timer
  (`sys_device_timer_init`: jego wygaśnięcie jest krokiem właściciela, `sys_device_step`) oraz to, co sterownik doda sam (kontroler I2C w
  `device_esp32s3_i2c`).
- REMOVE: `suspend()` na urządzeniu jeszcze niezawieszonym (stan bezpieczny jest stanem końcowym), potem zasoby od
  ostatniego do pierwszego, pod lockiem właściciela.
- Nieudane `create()`: zawsze zasoby od ostatniego do pierwszego. Bez `suspend()`: struktura jest w połowie
  zbudowana, a `suspend()` działa na cudzych urządzeniach (silnik wysłałby COAST na kanał mostka, którego jeszcze nie
  sprawdził). Krok, który trzeba cofnąć na sprzęcie, jest osobnym zasobem dodanym przed tym krokiem.

Do zrobienia: zależność (`sys_device_use`) jako zasób zamiast osobnego mechanizmu masek przy zwalnianiu; zasób
dodawany poza `create()` i oddawany przed REMOVE (dziś nie ma zdjęcia z listy).

Każde roszczenie jest wyłączne: pin i adres I2C mają jednego właściciela, drugi chętny dostaje błąd. Dzielony jest
tylko dostawca (szyna z wieloma chipami, urządzenie z wieloma pinami), a to pokrywa `used_by`. Trybu dzielonego z
głosowaniem o poziom nie ma: żadna linia OE / EN / RESET nie jest wspólna dla kilku chipów, a regulator nie jest
niczyją zależnością (istnieje sam, napięcie ustawia użytkownik).

## 8. Izolacja modułów

- **Typ innego sterownika w sterowniku.** Wdrożone: `device_esp32s3_i2c.c` nie zna typu GPIO i niczego nie sprawdza.
  Konwencja: nóżka = pin, indeks pinu jest numerem GPIO (pin 46 to GPIO 46). To, że SDA / SCL są pinami samego
  ESP32-S3, a nie ekspandera, jest konfiguracją płytki. `ERR_I2C_BAD_PIN` zniknął (nic go nie zgłasza).
- **Jeden numer modułu.** Wdrożone: moduł ma jeden numer (`MODULE_ID_DECLARE(NAME, 0x22)` w jego nagłówku,
  `MODULE_ID_DEFINE` w jednym źródle; `core/utils/module_id.h`), który jest zarazem kluczem routera
  (`ROUTER_MODULE_REGISTER(NAME, fn)`), modułem błędów (`#define ERR_MOD_X MODULE_ID_NAME`), modułem eventów
  (`EVT_TAG(MODULE_ID_NAME, code)`) i, dla kontraktu, jego ID (`SYS_DEVICE_CONTRACT_DECLARE`). Dwa moduły z jednym
  numerem nie linkują się (`module_id__0x01`). Żaden plik nie musi includować wszystkich modułów. Przydział:
  `0x01..0x1F` kontrakty (`0x01` IO, `0x02` I2C, `0x03` EVENT, `0x04` PMON, `0x05` VREG, `0x07` PD, `0x08` SUPPLY,
  `0x09` HBRIDGE), `0x20..0xFE` pozostałe moduły (`0x20` rejestr, `0x21` polityka, `0x22` menedżer zasilania, `0x23`
  router, `0x24` cmd, `0x25` eventy, `0x26` kontrakty zasilania), `0xFF` własne opy urządzenia. Numery eventów
  urządzeń: `0x8000 | typ`. `router_ids.h` zniknął; `err_modules.h` zawiera już tylko moduły bez własnego numeru.
- **Payloady zmiennej długości.** Wdrożone: flaga `SYS_DEVICE_OP_TAIL` w wierszu tabeli i jeden handler,
  `CMD_HANDLER_TAIL` w `cmd_table.h` (`OP_WRITE_TAIL(P, NAME)` dla sterownika): ciało dostaje `args` (głowa, w miejscu),
  `tail`, `tail_len`. Tabela rozstrzyga: `OP_WRITE` na opie z `TAIL` i `OP_WRITE_TAIL` na opie bez niej nie kompilują
  się. `OP_WRITE_I2C_RAW` zniknęło, `OP_WRITE_ON_EVENT` to `OP_WRITE_TAIL` plus sprawdzenie długości eventu, `OPS` jest
  zwykłym `OP_READ` z odpowiedzią składaną przez `cmd_append`.
- **Uchwyty subskrypcji.** Wdrożone: uchwyt to `[generacja u8][slot u8]`, nigdy 0, więc `unsubscribe`
  starym uchwytem nie usuwa cudzej subskrypcji.
- **Interceptor.** Wdrożone: `sys_device_set_interceptor(contract, fn, done)`. `done(id, dir, op, passed, err)` jest
  wołany raz dla każdego wywołania, które `fn` przepuścił, po urządzeniu i bez locka rejestru, także gdy do urządzenia
  nie doszło (brak, zawieszone, lock zajęty). Lock slotu nie rozwiązałby wyścigu (budżet jest wspólny dla wielu
  urządzeń), więc `fn` decyduje i rezerwuje pod własnym lockiem modułu, a `done` oddaje rezerwację nieudanego zapisu.

## 9. Izolacja od sprzętu

- **Podział `devices/`.** Do zrobienia. `soc/` (`esp32s3_*`): jedyne miejsce z `esp_driver_*`. `chips/` i urządzenia
  wirtualne: tylko `core` i kontrakty `sys`. PCA9685 czeka już przez `core/time` (`board_time_delay_us`), nie
  `esp_rom_delay_us`. Regułę sprawdza skrypt po `PRIV_REQUIRES` (albo funkcja CMake z 12.5), więc każdy chip buduje
  się na hoście bez zmian.
- **Wspólne taski robocze.** Task eventów i task timerów wołają opy urządzeń. Z `portMAX_DELAY` jedno zawieszone
  urządzenie zatrzymuje wszystkie eventy (w tym NOTIFY do aplikacji) i wszystkie timery. Dla eventów rozwiązuje to
  timeout locka z pkt 2. Timery: wdrożone. Wygaśnięcie timera urządzenia to krok (`sys_device_step(id, gen, op)`):
  lock brany tylko, gdy wolny, krok pomijany bez błędu, gdy urządzenie zajęte, zawieszone, pod bramką safe state albo
  to już inne urządzenie (generacja). Op kroku nie jest `ALWAYS`, sterownik nie trzyma własnej flagi `suspended`.
  Task timerów wybiera spośród należnych ten, który czeka najdłużej, więc wolny callback nie zagładza pozostałych.
  Rampa silnika liczy czas z zegara (`board_time_uptime_ms`), nie z liczby kroków.

## 10. Drobne

- `sys_power_manager` jest na nowym API: budżet VREG to interceptor z `done` (rezerwacja w `fn`, zwrot po nieudanym
  zapisie).
- Z mapy błędów rejestru wypadły tagi, których nic nie zgłasza: `ERR_DEV_TABLE_FULL`, `ERR_DEV_DEPS_FULL`,
  `ERR_DEV_HELD`. Rejestr definiuje swój moduł błędów (`ERR_MODULE_DEFINE`), którego w pierwszej wersji brakowało.
- `cmd_none_t` ma rozmiar 0 w C, 1 w C++, mimo strażników `extern "C"`.
- Limit 64 urządzeń obejmuje szyny, kanały muxa, urządzenia wirtualne i zdalne.
- `sys_device_slot_drain()` odpytuje co tick; wystarczy, dopóki opy `NOLOCK` mają własny timeout.
- `test_host/run.py` szuka testów w `runit-esp/components` i w `runit-esp-rework/components`. Przechodzą `errors`,
  `router`, `router_queue`, `timer`, `sys_flasher`, `sys_event` (19) i `sys_device` (24).

## 11. Do wytestowania

Większość punktów poniżej pokrywają testy hosta `sys_event` i `sys_device` (co dokładnie: „Tests” w `SYS_EVENT.MD` i
`SYS_DEVICE.MD`). Otwarte na hoście: opy `NOLOCK` i czekanie REMOVE na nie, `has_op`, sweep RESET pomijający typ bez
RESET, `sys_device_timer` (sam krok jest testowany), `sys_device_policy`, właściciel pinu i adresu (sterowniki: IO,
I2C), `TAIL` i `ON_EVENT`. Na płytce: wszystko, co zależy od priorytetów, stosów i czasu (zapas stosu, sweep na
prawdziwym I2C, `SCAN`).

### Build

- Na cel: buduje się (`build.ps1`, bez ostrzeżeń). Na hoście: `sys_event` i `sys_device` budują się i przechodzą.
- `sizeof(sys_device_slot_t)` i rozmiar `s_slots` w pliku `.map` (szacunek: 120–128 B na slot, ok. 8 KB).

### Wstrzymanie eventów (`sys_event`)

- Publikacja inline bez wstrzymania: listener biegnie przed powrotem z `sys_event_publish`.
- Publikacja w trakcie wstrzymania: listener nie biegnie do `sys_event_release`, potem biegnie; kolejność eventów
  jest kolejnością publikacji; `t_ms` jest czasem publikacji, nie dostarczenia.
- Event z payloadem o długości niepodzielnej przez 4: następny event w buforze jest czytany poprawnie.
- Bufor pełny: nadmiarowy event trafia do taska eventów i tam dostają go listenery inline i kolejkowane.
- Wstrzymanie w wstrzymaniu: wewnętrzne nic nie trzyma, eventy wychodzą przy zwolnieniu zewnętrznego.
- Listener, który w trakcie `sys_event_release` sam publikuje: dostarczane od razu, bufor nie jest uszkodzony.
- Listener kolejkowany dostaje kopię eventu wstrzymanego; pełna kolejka jest zgłaszana (`ERR_EVENT_QUEUE_FULL`).
- `SYS_EVENT_DEFER` i publikacja z ISR działają tak samo z wstrzymaniem i bez.
- Dwa taski: wstrzymanie jednego nie zatrzymuje publikacji drugiego.
- Uchwyt subskrypcji: `unsubscribe` uchwytem 0, uchwytem już usuniętym i uchwytem, którego slot zajął nowy listener,
  daje `ERR_EVENT_NOT_FOUND`, a nowy listener zostaje; generacja po 255 subskrypcjach slotu wraca do 1, nie do 0.

### Eventy z opa (`sys_device_call`)

- U woła D, D publikuje inline, listenerem jest `ON_EVENT` urządzenia U: event dochodzi po powrocie z opa U, bez błędu
  reentrancji.
- To samo z dwóch tasków naraz (task 1: op U wołający D; task 2: op D publikujący do U): brak deadlocka.
- Op, który zwrócił błąd, a wcześniej opublikował: event jest dostarczony.
- CREATED / REMOVED / SUSPENDED / RESUMED / RESET dochodzą po wywołaniu, także gdy wywołanie było zagnieżdżone.
- Listener wołający urządzenie w trakcie dostarczania: jego wywołanie jest nowym zewnętrznym, z własnym wstrzymaniem.
- Zapas stosu tasków routera, timerów, eventów i VM z buforem 256 B na zewnętrznym wywołaniu (na płytce).

### Tożsamość wywołującego

- Poza opem i w opie z komendy: `SYS_DEVICE_ID_NONE`.
- W opie D zawołanym z opa U: U; po powrocie do opa U wartość jest jak przed wywołaniem.
- W `create()` urządzenia X, w opie szyny (`ATTACH`): X.
- W listenerze uruchomionym po wywołaniu: `SYS_DEVICE_ID_NONE`.

### Zasoby (`sys_device_res_add`)

- REMOVE: `suspend()` biegnie na urządzeniu niezawieszonym, nie biegnie na zawieszonym; zasoby są oddawane od
  ostatniego do pierwszego.
- Nieudane `create()` na każdym kroku PCA9685 (brak szyny, brak ACK, zajęty pin OE): adres i pin są wolne, drugi
  CREATE z tą samą konfiguracją przechodzi; `suspend()` nie jest wołane.
- Błąd jednego `release`: pozostałe są wołane, REMOVE zwraca pierwszy błąd, slot jest wolny.
- Ten sam węzeł dodany dwa razy: `ERR_DEV_BAD_ARG`, lista nie jest zapętlona.
- `sys_device_res_add` bez locka slotu: błąd, węzeł nadal „niedodany”.
- `device_esp32s3_i2c`: kontroler jest usuwany przed zwolnieniem pinów SDA / SCL.
- `sys_device_timer`: timer stoi po REMOVE i po nieudanym `create()`; krok „w drodze” po REMOVE (także po CREATE
  nowego urządzenia pod tym ID) jest pomijany bez błędu i nie dotyka zwolnionej pamięci.
- Krok na urządzeniu zajętym przez inny task (długi op): pominięty od razu, task timerów nie czeka; pozostałe timery
  biegną. Krok na zawieszonym urządzeniu i w trakcie sweepa SUSPEND: pominięty.
- Rampa silnika: ten sam czas od 0 do pełnej prędkości przy zapisach `SPEED` co 1 ms i bez nich; nieudany krok
  (mostek zawieszony) zgłoszony raz, timer stoi do następnego `SPEED`.
- Sterta po serii CREATE / REMOVE i nieudanych CREATE wraca do stanu początkowego.

### Sloty: lock, usuwanie, cykle

- Lock zajęty dłużej niż timeout: wołający dostaje `ERR_DEV_BUSY{id, op}` z opem, który trzyma lock, a nie czeka dalej.
- Wywołanie urządzenia z jego własnego opa: `ERR_DEV_REENTRY`.
- REMOVE urządzenia używanego: `ERR_DEV_IN_USE` z maską użytkowników, nic się nie zmienia.
- Wyścig: task A w REMOVE(D) zatrzymany po sprawdzeniu użytkowników (hook `suspend()` czeka), task B w `create()`
  deklaruje użycie D: B dostaje `ERR_DEV_NOT_FOUND`, D zostaje usunięte, nikt nie ma zależności do wolnego slotu.
- Cykl: A używa B, potem B z własnego opa deklaruje użycie A: `ERR_DEV_CYCLE`; to samo przez trzecie urządzenie.
- Generacja: po REMOVE i CREATE tego samego ID `gen` jest inne; po 255 urządzeniach wraca do 1, nie do 0.
- `sys_device_slot_uninstall` nie kasuje locka ani generacji.

### Sweep

- Kolejność: dla łańcucha silnik → mostek → PCA9685 → szyna SUSPEND idzie w tej kolejności, RESUME w odwrotnej.
- SUSPEND: `suspend()` PCA9685 dociera do pinu OE i do szyny (żadne z tych wywołań nie dostaje `ERR_DEV_SUSPENDED`).
- Bramka: w trakcie sweepa SUSPEND zapis z komendy na urządzenie jeszcze niezawieszone i CREATE dostają
  `ERR_DEV_SUSPENDED`; odczyt i op `ALWAYS` przechodzą; po sweepie bramka jest zdjęta (RESUME i zapis działają).
- Dwa sweepy SUSPEND naraz z dwóch tasków: bramka schodzi dopiero po drugim.
- Urządzenie z zajętym lockiem: sweep zwraca `ERR_DEV_BUSY` dla niego i zawiesza pozostałe.
- Sweep wywołany z opa urządzenia X: X dostaje `ERR_DEV_REENTRY`, pozostałe są zawieszone.
- REMOVE: urządzenia systemowe zostają, pozostałe znikają w kolejności user przed depem, bez `ERR_DEV_IN_USE`.
- RESET: urządzenie bez opa RESET jest pomijane bez błędu.
- Eventy: jeden SUSPENDED / RESUMED / RESET ze źródłem `SYS_DEVICE_ID_NONE`, po ostatnim urządzeniu, z flagą NOTIFY;
  żadnego per urządzenie. Sweep REMOVE: REMOVED dla każdego usuniętego.

### Eventy CREATED / REMOVED

- CREATED i REMOVED niosą generację slotu; SUSPENDED / RESUMED / RESET mają `SYS_EVENT_NOTIFY`.
- `sys_device_policy`: REMOVED(gen 1) dostarczone po CREATED(gen 2) tego samego ID zostawia politykę nowego
  urządzenia; w zwykłej kolejności urządzenie traci politykę, a nowe dostaje domyślną.

### Właściciel zasobu

- `ATTACH`, `DETACH`, `LOCK`, `UNLOCK` jako komenda: `ERR_DEV_LOCAL_OP`; nie ma ich w `OPS`.
- Drugi `LOCK` pinu: `ERR_DEV_PIN_LOCKED` z `owner` = urządzenie, które zajęło pin; zapis IO na zajęty pin to samo.
- `UNLOCK` z opa innego urządzenia i spoza opa: `ERR_DEV_PIN_NOT_OWNED{owner}`, pin zostaje zajęty. `UNLOCK` pinu
  niezajętego: sukces.
- Zapis IO (`LEVEL`, `TOGGLE`, `PWM_DUTY`) zablokowanego pinu: z opa właściciela przechodzi; z opa innego urządzenia,
  z komendy i z VM daje `ERR_DEV_PIN_LOCKED{owner}`. Pin z właścicielem `SYS_DEVICE_ID_NONE`: odrzuca każdy zapis,
  także z kodu bez urządzenia.
- `LOCK` pinu, który woła sam właściciel drugi raz: `ERR_DEV_PIN_LOCKED`.
- Drugi `ATTACH` adresu: `ERR_I2C_ADDR_TAKEN` z `owner`. `XFER` i `DETACH` z opa innego urządzenia:
  `ERR_I2C_NOT_OWNER{owner}`, adres zostaje.
- REMOVE i nieudane `create()` PCA9685: zwolnienie pinu OE i adresu przechodzi (wołającym jest właściciel);
  `suspend()` PCA9685 w sweepie i przy REMOVE dociera do OE (`LEVEL` właściciela) i do szyny (`XFER`).
- Transfer I2C z opa timera i z `ON_EVENT` właściciela przechodzi.
- `RAW` na adresie chipa: `ERR_I2C_ADDR_TAKEN` z właścicielem; na wolnym adresie działa i zostawia go wolnym.
- `sys_io_pins_init`: wszyscy właściciele `SYS_DEVICE_ID_NONE`; po `UNLOCK` właściciel wraca do `SYS_DEVICE_ID_NONE`.

### `TAIL`, moduły eventów, interceptor

- `device_esp32s3_i2c` na pinach GPIO ESP32-S3: szyna działa na GPIO = indeks pinu.
- `OP_WRITE(I2C, RAW)` i `OP_WRITE_TAIL(IO, LEVEL)` nie kompilują się (asercje flagi `TAIL`).
- `RAW` zapis: payload krótszy niż głowa i sama głowa dają `ERR_CMD_BAD_LEN`; głowa + n bajtów pisze n bajtów.
- `ON_EVENT`: payload krótszy niż 20 B i `len` inne niż długość ogona dają `ERR_CMD_BAD_LEN`; payload eventu jest
  czytany poprawnie przez `SYS_EVENT_DATA`.
- `OPS` zwraca tę samą listę co przed zmianą (bez opów `LOCAL`).
- Dwa moduły eventów z tym samym ID: błąd linkowania (`sys_event__module_0x....`); `SYS_EVENT_MODULE_DEFINE` z innym
  ID niż w deklaracji: błąd kompilacji; ID spoza `0x0100..0x7FFF`: błąd kompilacji.
- Interceptor: `done` wołany raz po udanym opie (`err` NULL), po błędzie opa, po `ERR_DEV_NOT_FOUND`,
  `ERR_DEV_SUSPENDED`, `ERR_DEV_BUSY`, po opie `NOLOCK` i po wywołaniu urządzenia zdalnego; nie jest wołany, gdy `fn`
  odmówił. `passed` to payload z `fn` (albo `in`, gdy `fn` zostawił `len` 0). Interceptor bez `done` działa jak dotąd.

### Szkielet sterownika i własne opy

- Build: trzy sterowniki i rejestr kompilują się na cel (na hoście sprawdzona tylko składnia, ze stubami ESP-IDF).
- `OPS` silnika zwraca te same opy i strony co przed zmianą; `TICK` nie ma w liście (`LOCAL`) i jako komenda daje
  `ERR_DEV_LOCAL_OP`; `TICK` nie biegnie na zawieszonym silniku (krok pominięty).
- Każdy własny op silnika z payloadem o złej długości: `ERR_CMD_BAD_LEN`; odczyty `SPEED`, `OUTPUT`, `SETTINGS`,
  `CURRENT`, `STALLED` zwracają te same bajty co przed zmianą.
- CREATE z ramki pod nieparzystym adresem (PCA9685, silnik): `cfg` jest czytane poprawnie.
- Typ jest zarejestrowany pod ID z `SYS_DEVICE_TYPE_DECLARE` (CREATE działa, `INFO` zwraca to ID); dwa sterowniki z
  jednym ID: błąd linkowania (`sys_device__type_0x....`).
- Asercje kompilacji: `create` / `suspend` / `resume` o innej sygnaturze, niespakowana struktura create, plik z
  handlerem bez `SYS_DEVICE_CTX` (na hoście sprawdzone: zła sygnatura `create` i obie asercje `TAIL`).
- Eventy `DC_MOTOR_EV_*` mają moduł `0x8000 | 0x004B` jak przed zmianą.

### Kontrakty

- Dwa kontrakty z tym samym ID w jednym buildzie: błąd linkowania (`sys_device__contract_0x..`), także gdy żaden plik
  nie includuje obu nagłówków.
- `SYS_DEVICE_CONTRACT_DEFINE` z innym ID niż w `SYS_DEVICE_CONTRACT_DECLARE`: błąd kompilacji.
- `sys_hbridge.c` (tylko zajęcie ID) trafia do obrazu: symbol `sys_device__contract_0x09` jest w pliku `.map`.

### Policy na własnym tasku

- Błąd CRITICAL zgłoszony z wnętrza opa urządzenia X: sweep zawiesza wszystkie urządzenia, X też (bez
  `ERR_DEV_REENTRY`); task zgłaszający nie czeka na sweep.
- Łańcuch, który nie wymaga reakcji (za niska ważność, urządzenie z importance NONE): nic nie trafia do kolejki.
- Łańcuch po przejściu przez rekord: te same urządzenia, ta sama ważność i ta sama akcja co przed kolejką.
- Łańcuch dłuższy niż `CONFIG_SYS_DEVICE_POLICY_RECORD_MAX`: co zostaje obcięte i czy ważność CRITICAL z korzenia
  przetrwa (jeśli nie, rekord trzeba powiększyć albo nieść ważność osobno).
- Pełna kolejka: `sys_device_policy_handle` zwraca `ERR_DEV_FAULT_RESPONSE{stage QUEUE}`, nic nie blokuje.
- Błąd samej reakcji (nieudany sweep, nieudana akcja): zgłoszony raz, nie wraca do policy.
- Błąd zgłoszony przez task policy w trakcie reakcji: nie jest obsługiwany ponownie.
- Zapas stosu taska policy przy sweepie przez wszystkie urządzenia (na płytce).
- Router polityki: odczyt `POLICY` odpowiada `{id, policy}` (ta sama ramka co zapis); zapis tej odpowiedzi z powrotem
  nic nie zmienia; ramka o złej długości daje `ERR_CMD_BAD_LEN{got, need}` z długością samego payloadu.

### Szyna I2C: `SCAN` bez locka

- `SCAN` na szynie z chipem, który w tym czasie robi transfery (timer PCA9685): transfery chipu przechodzą między
  próbami, nikt nie dostaje `ERR_DEV_BUSY`; wynik `SCAN` taki sam jak bez ruchu.
- REMOVE szyny w trakcie `SCAN` (bez chipów): czeka na koniec `SCAN`, potem usuwa kontroler.
- `SCAN` na zawieszonej szynie działa (odczyt); `PROBE` adresu bez chipa zwraca 0 bez błędu, `SCAN` z odłączoną
  linią SDA kończy się `ERR_I2C_BUS_STUCK` przy pierwszym adresie.

### GPIO ESP32-S3 (`device_esp32s3_gpio`)

- `AVAILABLE` to `SOC_GPIO_VALID_GPIO_MASK` (bez GPIO 22-25); `LOCKED` po `CREATE` to piny flash / PSRAM / USB, a
  `MODE` na takim pinie daje `ERR_DEV_PIN_LOCKED{owner 0xFF}`. Drugi `CREATE`: `ERR_DEV_ID_TAKEN` z ID pierwszego.
- PCA9685 z OE na GPIO: `create()` ustawia wyjście, poziom i `LOCK` (właściciel PCA9685); `LEVEL` z komendy na OE
  daje `ERR_DEV_PIN_LOCKED{owner}`, `suspend()` PCA9685 w sweepie podnosi OE (zapis właściciela), a `suspend()` GPIO
  zostawia ten pin. REMOVE PCA9685 odblokowuje OE i zostawia je wyjściem (`keep`).
- Szyna I2C na GPIO: SDA / SCL zablokowane przez szynę, `MODE` na nich odmawia; po REMOVE szyny są wolne i
  niezarezerwowane.
- Zbocze wejścia uzbrojonego przez `INTR`: `IO_EV_RISING` / `FALLING` bez payloadu, źródło = ID GPIO, kanał = pin;
  debounce 50 ms tylko z `debounce = 1`.
- Pin ADC z oknem: `IO_EV_ADC_OUTSIDE` / `INSIDE` z `mV`, z taska ADC (odroczone); progi `RISING` / `FALLING` pinu ADC
  bez payloadu.
- PWM: dwa piny na jednej częstotliwości dzielą timer, piąta częstotliwość daje `ERR_IO_PWM_TIMERS_EXHAUSTED`;
  `suspend()` / `resume()` zdejmują i przywracają wypełnienie i poziomy wyjść.
- REMOVE GPIO (urządzenie nie płytki, bez użytkowników): wszystkie piny wracają do stanu po resecie, kolejny `CREATE`
  działa (serwis ISR i task ADC zostają).

## 12. Automatyzacja wzorców

Miejsca, gdzie ten sam wzorzec był pisany ręcznie w każdym sterowniku albo kontrakcie. 12.1–12.4, 12.7 i z 12.6 wszystko
poza `sys_device_self()` są wdrożone i zbudowane; 12.5, `sys_device_self()` i 12.8–12.13 czekają. 12.7–12.13 pochodzą
z przeglądu starych sterowników (`runit-esp/components/devices/device_*`) i routerów w `sys`. Kolejność: 12.9 z
`sys_device_self()` i 12.8 przy migracji pierwszego sterownika, który ich używa, bo zmieniają kształt każdego
migrowanego sterownika i bez niego nie ma ich na czym sprawdzić; 12.10–12.12 przy migracji.

### 12.1 Tabela własnych opów urządzenia (`0xFFxx`)

**Problem.** Kontrakt ma `P_OPERATIONS` i z niej typy, flagi i wiersze. Własne opy w `device_dc_motor.c` były ręczne:
`#define DC_MOTOR_OP_*`, handlery `SYS_DEVICE_HANDLER(get_speed_h, cmd_none_t)`, wiersze
`{.op = ..., .read = ..., .flags = ...}` i `cmd_reply(out, &v, sizeof(v))`. Nic nie pilnowało, że `TICK` ma `LOCAL`,
ani że handler bierze typ zgodny z opem.

**Stan: wdrożone.** Tabela w formacie tabeli kontraktu w źródle sterownika, `DC_MOTOR_OPERATIONS(X, P)`, i
`SYS_DEVICE_OWN_DECLARE(DC_MOTOR)` (kontrakt `0xFF`). Sterownik pisze `OP_READ(DC_MOTOR, SPEED)`,
`OP_ENTRY(DC_MOTOR, SPEED)` i `CMD_REPLY(DC_MOTOR, SPEED, ...)` jak dla kontraktu; flagi (`LOCAL`, `ALWAYS`, `TAIL`)
idą z tabeli. `SYS_DEVICE_HANDLER` przestało być publiczne (`SYS_DEVICE__HANDLER`): handler pisze się tylko przez
`OP_READ` / `OP_WRITE` / `OP_WRITE_TAIL`. Numery opów silnika się nie zmieniły.

### 12.2 Szkielet sterownika

**Problem.** Każdy sterownik powtarzał:

- `k_type` z `sizeof(cfg)`, `sizeof(ctx)`, `ARRAY_LEN(k_ops)`, `.has_op = NULL`;
- rzutowanie `cfg_in` w `create()` (PCA9685 rzutował, silnik robił `memcpy`);
- `(void)args; (void)out; pca_t* c = ctx;` na początku każdego handlera.

**Stan: wdrożone.**

- `#define SYS_DEVICE_CTX pca_t` raz na plik, po strukturze: ciało każdego handlera (`OP_READ`, `OP_WRITE`,
  `OP_WRITE_TAIL`, `OP_WRITE_ON_EVENT`, `SYS_IO_PINS_HANDLERS`) dostaje `c` już typowane. Plik bez tej definicji nie
  kompiluje się. Rejestr definiuje `void` (jego ctx to ID urządzenia).
- Parametry ciała handlera są oznaczone `CMD_UNUSED`: żadnego `(void)args; (void)out;`.
- `SYS_DEVICE_TYPE_DEFINE(NAME, cfg_t, create, suspend, resume, k_ops)` jako ostatnia linia pliku: wypełnia
  `sys_device_type_t` (rozmiary z typów, liczba opów z tabeli) i rejestruje typ. Haki są typowane:
  `create(uint8_t id, pca_t* c, const cfg_t* cfg)`, `suspend(pca_t* c)`, `resume(pca_t* c)`; hak o innej sygnaturze
  nie kompiluje się. `cfg` jest czytane w miejscu (asercja: struktura create jest spakowana), więc `memcpy` zniknęło.
  `SYS_DEVICE_TYPE_DEFINE_HAS_OP` dodaje `has_op(c, op)`.
- Przepisane: `device_pca9685.c`, `device_esp32s3_i2c.c`, `device_dc_motor.c`, `device_esp32s3_gpio.c`, opy
  rejestru.

`c->id = id` w `create()` zostaje (patrz 12.6, `sys_device_self()`).

### 12.3 ID typu pisane raz

**Problem.** Silnik miał `#define DC_MOTOR_TYPE 0x004B`, potem `_Static_assert` i
`SYS_DEVICE_TYPE_REGISTER(0x004B, k_type)`, bo moduł własnych eventów potrzebuje ID przed tabelą eventów.

**Stan: wdrożone.** `SYS_DEVICE_TYPE_DECLARE(DC_MOTOR, 0x004B)` na górze pliku: daje `DC_MOTOR_TYPE` (dla
`SYS_EVENT_MODULE_TYPE(DC_MOTOR_TYPE)` i dla `SYS_DEVICE_TYPE_DEFINE`) oraz symbol linkera `sys_device__type_0x004B`.
`SYS_DEVICE_TYPE_REGISTER` zniknęło.

### 12.4 Eventy kontraktu z modułem z kontraktu

**Problem.** Wiersze tabeli eventów podają moduł ręcznie: `EVT_TAG(VREG_CONTRACT, 0x01)`, a rejestr literałem
`EVT_TAG(0x00, 0x01)`. Da się wpisać cudzy moduł.

**Stan: wdrożone.** Tabela eventów w formacie tabeli opów, `P_EVENTS(X, P)` z wierszami
`X(P, NAME, kod, flagi, { payload })`; moduł bierze się z prefiksu, nie z wiersza. `SYS_EVENTS_DECLARE(P)` daje
moduł `MODULE_ID_<P>` (moduł albo kontrakt), `SYS_DEVICE_OWN_EVENTS(P)` moduł własnych eventów typu
`SYS_EVENT_MODULE_TYPE(P_TYPE)`. Nazwy (`P_EV_<NAME>`, `evt_data_P_EV_<NAME>_t`) są takie jak wcześniej, więc
publikacja i odczyt payloadu się nie zmieniły. `EVENTS_DECLARE` zniknęło. Eventy rejestru mają teraz moduł `0x20`
(numer rejestru) zamiast `0x00`.

### 12.5 CMake sterownika

**Problem.** Każdy `CMakeLists.txt` w `devices/` jest taki sam. Zapomniane `WHOLE_ARCHIVE` daje cichy błąd: typ nie
istnieje w runtime (`ERR_DEV_TYPE_UNKNOWN`).

**Propozycja.** Funkcja `runit_device(SRCS ... PRIV_REQUIRES ...)` we wspólnym pliku cmake: zawsze `WHOLE_ARCHIVE`,
bez `INCLUDE_DIRS`. Może też odrzucić `esp_driver_*` poza `soc/`, czyli reguła z sekcji 9 bez osobnego skryptu.

### 12.6 Mniejsze

- **ID urządzenia w strukturze sterownika.** Każdy trzyma `c->id`, a `SYS_IO_PINS_HANDLERS` wymaga pól o nazwach `id`
  i `pins`. Rejestr zna urządzenie, którego op biegnie (`s_running`, obok `sys_device_caller()`): funkcja
  `sys_device_self()` usuwa pole i konwencję nazw. Sama funkcja to kilka linii; pole `c->id` znika dopiero z
  przepisaniem sterowników (zakresy, źródło eventów). Robić razem z 12.9, przy migracji pierwszego sterownika, który
  czyta stan zawieszenia.
- **Zakresy ustawień.** Zastąpione przez 12.10.
- **„Zawołaj op urządzenia później”.** Timer: wdrożone. `sys_device_timer` pakuje `[op u16][gen u8][owner u8]`, a
  wygaśnięcie to krok `sys_device_step(id, gen, op)` (punkt 9). Subskrypcja (`sys_device_event`) nadal pakuje samo ID
  i idzie do `ON_EVENT`: to 12.8.
- **Ignorowanie jednego tagu błędu.** Wdrożone: `bool err_drop(err_h* err, uint32_t tag)` w `core/errors` (wspólny z
  `runit-esp`). Gdy przyczyną (root tag) łańcucha jest `tag`, łańcuch jest zwalniany, `*err` staje się NULL, wynik
  `true`; inny błąd zostaje. Forma z `bool`, a nie `err_unless(err, tag)`, bo `PROBE` i `SCAN` muszą wiedzieć, czy to
  był ten tag (adres bez chipa), a `sys_io_prepare_input` po nim ponawia `MODE`. Użyte w `sys_io.c` (`ignore_unknown_op`
  zniknęło), trzech miejscach `sys_power_manager.c` (`ERR_DEV_NOT_FOUND`, `ERR_DEV_SUSPENDED`) i w `PROBE` / `SCAN`
  szyny. Test hosta w `errors`.
- **Router polityki.** Wdrożone: tabela `POLICY_OPERATIONS` w `sys_device_policy_router.h`, handlery `ROUTER_READ` /
  `ROUTER_WRITE`. Odczyt `POLICY` odpowiada teraz `{id, policy}` (`sys_device_policy_entry_t`, ta sama ramka co zapis,
  lustro), a nie samą polityką. Z `router.h` zniknęły `ROUTER_OP*`, `ROUTER_R/W/RW`, `router_args*`: handler o stałej
  ramce to `CMD_HANDLER` w `cmd_table.h` (wspólny z `OP_READ` / `OP_WRITE` urządzeń), z ogonem `CMD_HANDLER_TAIL`
  (`ROUTER_WRITE_TAIL`). `router_call_op` podaje handlerowi sam payload, jak rejestr. `main/table_check.c` zniknął.
- **`SYS_IO.MD` w `sys_io`.** Przepisane razem z przeniesieniem GPIO: opisuje obecne API (`sys_io_pins_t`,
  `sys_io_claim_*`, opy `0x010A..0x010D`, eventy z payloadem).

### 12.7 Właściciel pinu pisze zwykłymi opami

**Problem.** Właściciel zablokowanego pinu miał osobny op `LOCKED_LEVEL`, tylko dla poziomu. Każda inna zmiana wymaga
ręcznego „odblokuj, zmień, zablokuj”: INA3221 przy zmianie zbocza alertu (`set_pin_edge`), DRV8962 przy oknie IPROPI
(`arm_ipropi`). W oknie między odblokowaniem a zablokowaniem pin jest niczyj.

**Stan: wdrożone.** Pin zna właściciela (punkt 6), więc
urządzenie z pinami przepuszcza zapisy właściciela i odrzuca cudze.

- `sys_io_pins_check_free` znaczy „wolny albo mój”: przechodzi pin niezablokowany i pin, którego właścicielem jest
  `sys_device_caller()`. Pin z właścicielem `SYS_DEVICE_ID_NONE` (natywny peryferial, moduł) odrzuca każdy zapis.
- Op `LOCKED_LEVEL` zniknął z kontraktu, z PCA9685 i z `contract_io.json` (`0x0E` zostaje nieużywane), razem z
  `sys_io_pins_check_owner`.
- `sys_io_claim_drive` zostaje jako skrót: pisze zwykłe `LEVEL` i nic nie robi dla pinu nieużywanego
  (`SYS_IO_PIN_NONE`), więc pin opcjonalny nie wymaga sprawdzenia w sterowniku. Każdy inny zapis zajętego pinu to
  sam op IO: `SYS_IO_WRITE(p->pin, INTR, ...)`.
- `LOCK` pinu już zablokowanego dalej zwraca `ERR_DEV_PIN_LOCKED`, także właścicielowi (blokadę bierze się raz).

Przy migracji INA3221 i DRV8962 miejsca z odblokowaniem zamieniają się w jeden zapis `INTR`.

### 12.8 Zdarzenie pinu do konkretnego opa

**Problem.** Timer urządzenia dostarcza wygaśnięcie do wskazanego własnego opa. Subskrypcja pinu idzie do jednego
`ON_EVENT`, więc sterownik sam rozpoznaje źródło: DRV8962 i INA3221 porównują `source` i `channel` z każdym pinem,
a TPS55289 i TCA6424A zakładają, że mają jedną subskrypcję.

**Propozycja.** `sys_io_claim_input` (i `sys_io_claim` z eventem) biorą op docelowy właściciela (`NFAULT`,
`IPROPI_OVER`: własne, `LOCAL`, `TAIL` z eventem). Kontrakt EVENT z `ON_EVENT` zostaje dla urządzeń, które słuchają
dowolnych eventów.

- **Wiązanie.** `ctx` subskrypcji to `[op u16][gen u8][owner u8]`, jak w timerze, więc event w drodze nie trafi do
  nowego urządzenia pod tym samym ID (dziś `sys_device_event` pakuje samo ID; ryzyko jest małe, bo zasób pinu
  odsubskrybowuje przed REMOVE, ale dostarczenie w toku może się z tym minąć).
- **Dostarczenie to nie krok.** Eventu nie wolno pominąć, gdy urządzenie jest zajęte (krok timera jest pomijany): czeka
  na lock jak dzisiejsze `ON_EVENT` i biegnie także na zawieszonym urządzeniu (`ALWAYS`). Wspólne z timerem jest więc
  tylko pakowanie `ctx` i sprawdzenie generacji pod lockiem: jedna funkcja rejestru, np. `sys_device_deliver(id, gen,
  op, payload)` obok `sys_device_step`.
- **Kiedy.** Dziś żaden sterownik w rework nie subskrybuje eventu pinu (PCA9685 bierze pin jako wyjście, szyna I2C
  jako peryferial). Wdrożyć przy migracji pierwszego z INA3221, DRV8962, TPS55289, TCA6424A, żeby projekt sprawdzić na
  prawdziwym przypadku.

### 12.9 Stan urządzenia dla sterownika

**Problem.** Siedem z dwunastu starych sterowników czyta `c->base.suspended` (odczyt z kopii, pominięcie eventu,
stan zasilania chipa). W nowym API tego pola nie ma. Silnik już jej nie potrzebuje: jego krok timera nie biegnie na
zawieszonym urządzeniu (punkt 9). Pozostałe przypadki (odczyt z kopii, pominięcie eventu w `ON_EVENT`, który jest
`ALWAYS`) zostają.

**Propozycja.** Razem z `sys_device_self()` z 12.6: ID i stan urządzenia, którego op biegnie
(`sys_device_self()`, `sys_device_self_suspended()`), z TLS rejestru. Znika `c->id = id`, kopia flagi i wymóg pól
`id` / `pins` o ustalonych nazwach w `SYS_IO_PINS_HANDLERS`.

### 12.10 Tabela pól ustawień z zakresami

**Problem.** W starych sterownikach jest 77 `SYS_DEV_CHECK_IN_RANGE` i 19 ręcznych
`ERR_CHECK(x <= 1, ERR_CMD_BAD_ARG)` dla pól 0/1 (inny tag niż dla zakresu). Te same sprawdzenia są powtórzone w
`create()` i w opie `SETTINGS` (DRV8962: `check_cfg` i `set_settings_h` słowo w słowo), a trzeci raz jako min / max w
`device_*.json`.

**Propozycja.** Tabela `X(pole, min, max)` obok struktury ustawień: z niej funkcja sprawdzająca, wołana w `create()`
i w `SETTINGS`; pole 0/1 to zakres 0..1 (jeden tag, `ERR_DEV_RANGE`). Ta sama tabela jako źródło min / max dla
deskryptora (albo sprawdzenie zgodności w `crosscheck.py`). Zastępuje wpis „Zakresy ustawień” z 12.6.

### 12.11 Sprawdzenie kanału

**Problem.** Każdy sterownik z kanałami pisze własne `check_channel`, z różnymi błędami: INA3221 zwraca
`ERR_POWER_BAD_CHANNEL`, DRV8962 dla tego samego błędu `ERR_DEV_BAD_PIN`.

**Propozycja.** `SYS_DEV_CHECK_CHANNEL(dev, ch, count)` i jeden tag `ERR_DEV_BAD_CHANNEL{id, channel}` w mapie
rejestru, jak `SYS_DEV_CHECK_IN_RANGE`.

### 12.12 Rejestry chipa z kopią

**Problem.** Chip traci rejestry po resecie albo po zdjęciu EN, więc sterownik trzyma ich kopie i pisze je od nowa:
TCA6424A (`config`, `output`, `polarity`, `chip_restore()`), TPS55289 (`tps_apply_config()`), INA3221 (`mask`,
`config`). Każdy po swojemu.

**Propozycja.** Mały typ w kliencie I2C: rejestr z kopią (zapis i `update` przez kopię, bez odczytu z chipa) i
`restore` wszystkich rejestrów urządzenia naraz. Pokrywa trzy sterowniki, do sprawdzenia PCA9685, ADS7128, DAC53202.

### 12.13 Mniejsze z przeglądu starych sterowników

- **Publikacja eventu z tagiem znanym w runtime.** `SYS_EVENT_PUBLISH` bierze nazwę w czasie kompilacji: DRV8962 ma
  `switch` po przyczynie błędu, INA3221 `if / else`, choć payload jest ten sam; task ADC GPIO (w rework) ma `switch`
  po czterech eventach IO. Wariant z tagiem jako wartością dla
  eventów o wspólnym typie payloadu.
- **Routery modułów.** 108 handlerów w 17 plikach `sys` / `vm` starego firmware jest pisanych przez `ROUTER_OP`. W
  rework tych makr już nie ma (12.6, „Router polityki”), więc każdy przenoszony moduł dostaje tabelę `P_OPERATIONS` i
  handlery z `router_table.h`.
- **Opóźnienia w opach.** 11 miejsc z `vTaskDelay` / `esp_rom_delay_us` pod lockiem urządzenia (wybudzenie chipa):
  reguła z sekcji 9 (opóźnienie przez `core/time`), nie automatyzacja.
- **Bloki VM a sterownik bez nagłówka.** Cztery bloki VM includują `device_x.h` (silnik, DRV8962, serwo, TPS55289)
  dla struktur i własnych opów. Sterownik w rework nie ma nagłówka. Do rozstrzygnięcia: blok woła op ramką z
  deskryptora, albo tabela własnych opów (12.1) idzie do małego nagłówka sterownika. Wiąże się z generowaniem
  deskryptorów z tabel (numery opów, struktury, offsety dziś przepisywane ręcznie do `contract_*.json` i
  `device_*.json`).
- **`uninstall()`** było w każdym z dwunastu sterowników (30 wystąpień „zwolnij tylko to, co zbudowano”): zasoby z
  punktu 7 usuwają je w całości przy migracji, bez dodatkowej pracy.
