# Część sieciowa — analiza krytyczna i propozycja nowej wersji

Przegląd z czytania kodu `runit-esp/components/sys/` (`sys_session`, `sys_watch`, `sys_remote`, `sys_stream`,
`sys_data_provider`, transporty `sys_uart` i `sys_ble`, `sys_flasher`) oraz tego, na czym nowa wersja ma stanąć w
`runit-esp-rework` (`core/cmd`, `core/router`, `core/router_queue`, `core/timer`, `sys_device/remote`, `sys_event`).
Nic nie było uruchamiane ani budowane: każdy błąd poniżej wynika z kodu, żaden nie był odtworzony na płytce.

Założenia: brak zgodności wstecz, jeden firmware na każdej płytce, topologia zostaje (gwiazda: root, slave'y, klienci
roota; dostęp do slave'a przez roota; flashowanie płytki na linku; OTA slave'a przez roota).

## W skrócie

Rdzeń jest dobry i zostaje: nagłówek 5 B, cztery rodzaje ramek, routing po `dst` bez stanu, hello z `boot_id`,
COBS + CRC16 na UART, fragmentacja na BLE, wywołania z odpowiedzią i timeoutem. Złożoność siedzi wyżej: trzy osobne
mechanizmy subskrypcji, cztery kształty ciała PUSH, pięć rodzajów celu strumienia i heurystyki cache'u odczytów pisane
dla jednego wywołującego.

| # | Zmiana | Co daje | Waga |
|---|---|---|---|
| B1 | `sys_session_call_wait`: pętla na fladze zamiast jednej notyfikacji | usuwa zapis po zwolnionym stosie | błąd, pilne |
| B2 | Licznik `seq` PUSH osobno dla ramek pilnych | usuwa fałszywe `ERR_REMOTE_LOST` | błąd |
| B3 | Kopia odczytu przyjmowana dopiero po odpowiedzi na ADD | usuwa wartość cudzego odczytu w cache'u | błąd |
| B4 | Odpowiedź ma pierwszeństwo przed PUSH w kolejce providera | zapis nie traci odpowiedzi przez telemetrię | błąd projektu |
| P1 | Wysyłka dwuczęściowa (nagłówek + ciało) | znikają bufory 512 B na stosie i sztuczka z `CMD_HEAD` | upraszcza |
| P2 | `seq` PUSH liczy sesja, nie moduły | znikają trzy liczniki i ich obsługa po obu stronach | upraszcza |
| P3 | Ciało PUSH: wpis watcha bez `owner`, id urządzenia 1 B; kanał DATA zostaje | krótsze ramki, zgodne z rejestrem reworku | porządek |
| P4 | Cele strumienia: SOURCE / SINK / PEER / CALLBACK | 5 rodzajów → 4, znaczenie nie zależy od kierunku | upraszcza |
| P5 | Klient watchy: tylko cache odczytów, bez heurystyk | ok. 500 → ok. 200 linii, mniej stanów | upraszcza |
| P6 | `sys_remote`: bez tabeli aliasów, bez własnej kolejki i taska | jedno źródło prawdy (rejestr) | upraszcza |
| P7 | Sesja: przyłącze UP / DOWN, powiadomienie PEER zamiast GONE | mniej stanów roli; aplikacja wie o restarcie slave'a | upraszcza |
| P8 | Timery sesji i watchy na `board_timer` | nic nie blokuje taska `esp_timer` | porządek |
| R1 | Policy kończy przejście łańcucha na `ERR_REMOTE` | błąd zdalnej płytki nie rusza lokalnych urządzeń o tych samych id | warunek proxy |
| R2 | Most zdarzeń nie przenosi `CREATED` / `REMOVED` rejestru | policy nie zapomina ustawień istniejącego aliasu | warunek proxy |
| R3 | Safe state płytki jako zdarzenie płytki przez most | master wie, że urządzenia slave'a są wstrzymane | warunek proxy |
| R4 | Rejestr: wyszukanie i iteracja urządzeń zdalnych; UNSUBSCRIBE z ramki bez aliasu | P6 bez tabeli aliasów jest wykonalne | warunek proxy |
| R5 | Firmware PENDING bez `CONFIRM` restartuje się sam | zdalne OTA slave'a nie kończy się wyjazdem do płytki | warunek OTA |

Trzy rzeczy, które każda zmiana musi zachować, są opisane w sekcji 4 („Wymagania”): czysty tunel aplikacja → master →
slave, flashowanie slave'a przez mastera (OTA i ROM po UART), proxy urządzeń z błędami i mostem zdarzeń.

---

## 1. Błędy

### B1. `sys_session_call_wait` budzi się od cudzej notyfikacji

`call/sys_session_call.c`: czekający zostawia `waiter_t` na stosie i śpi w `ulTaskNotifyTake(pdTRUE, portMAX_DELAY)`,
czyli na domyślnej notyfikacji taska (`CONFIG_FREERTOS_TASK_NOTIFICATION_ARRAY_ENTRIES=1`). Tę samą notyfikację dostaje
task urgent routera (`wake_urgent` w `router_queue.c`: każda nowa pilna komenda i `router_run_urgent`), task power
managera, LVGL, flashera.

Scenariusz: pilna komenda na urządzeniu zdalnym `WIRED` (awaryjne zatrzymanie silnika na slave'ie) czeka na odpowiedź;
w tym czasie przychodzi druga pilna komenda. `wake_urgent` kończy czekanie, funkcja zwraca `NULL` z `len` 0
(„sukces” bez danych), a wywołanie nadal jest w tabeli. Gdy odpowiedź przyjdzie albo timer je zakończy, `wait_done`
pisze przez `w->buf`, `w->len`, `w->err` w ramkę stosu, której już nie ma.

Poprawka (kilka linii): `waiter_t` dostaje `bool done`, ustawiane w `wait_done` przed `xTaskNotifyGive`; czekanie to
`while (!w.done) ulTaskNotifyTake(...)`. Zjedzona cudza notyfikacja jest nieszkodliwa dla taska urgent (po komendzie i
tak opróżnia kolejkę i sprawdza `urgent_fn`); pozostałym opóźnia jeden obieg.

### B2. Fałszywe `ERR_REMOTE_LOST` po wyprzedzeniu przez ramkę pilną

`sys_remote.c`, `send_out`: zdarzenia i błędy jednej subskrypcji mają wspólny `seq`, ale błąd idzie z
`ROUTER_FLAG_URGENT`, a UART ma dla pilnych osobną kolejkę, pisaną pierwszą. Błąd wyprzedza więc zdarzenia jeszcze
czekające w zwykłej kolejce. Odbiorca (`take_in`) liczy `lost = seq - a->seq - 1` w `uint16_t`: najpierw widzi dziurę,
a przy pierwszym spóźnionym zdarzeniu wynik zawija się do ok. 65 tysięcy. To idzie przez `err_report_board` do policy
płytki. Na BLE nie wystąpi (kolejka zachowuje kolejność).

Poprawka: licznik osobny dla klasy pilnej i zwykłej (P2 robi to w jednym miejscu dla wszystkich PUSH).

### B3. Wpis starego watcha staje się kopią nowego odczytu

`sys_watch_client.c`: `on_idle_tick` zwalnia slot i wysyła REMOVE; `take_locked` daje następnemu odczytowi najniższe
wolne id, czyli zwykle to samo. Wpisy starego watcha, które tamta płytka wysłała przed przetworzeniem REMOVE, mają to
samo `(board, id)` i `on_push` zapisuje je jako kopię nowego odczytu (`active = true` przy dowolnym wpisie). VM dostaje
przez jeden okres wartość innego odczytu, np. innego pinu.

Poprawka: slot przyjmuje wpisy dopiero po odpowiedzi OK na swoje ADD. Kolejność na łączu gwarantuje, że wszystko, co
przyjdzie po tej odpowiedzi, należy już do nowego watcha.

### B4. Odpowiedzi giną przy przekazywaniu, gdy kolejkę zajmują PUSH

Root przekazuje ramki z `NOWAIT`; odpowiedź, która się nie zmieści, jest porzucana bez śladu („czytający ponowi”).
Kolejka runIT TX na BLE ma 4 ramki i dzieli ją wszystko: odpowiedzi roota, jego strumienie, przekazywane odpowiedzi i
wpisy watchy slave'ów. Dla zapisu utrata odpowiedzi znaczy, że aplikacja nie wie, czy komenda się wykonała, a
przegrała z telemetrią.

Poprawka: jedna flaga wysyłki providera (np. `SYS_DATA_PROVIDER_SEND_SPARE`), z którą sterownik odmawia, gdy kolejka
jest zajęta ponad połowę. Dostaje ją każda ramka PUSH (własna i przekazywana). Odpowiedzi i ramki kontrolne mają wtedy
zawsze zarezerwowane miejsce. Po kilka linii w `sys_uart` i `sys_ble`.

### Mniejsze

- **Tabela tasków „nie czekaj” jest pełna.** `NO_WAIT_TASKS` = 6, a zapisują się: RX konsoli, RX linku, host NimBLE,
  `esp_timer`, task strumieni, task `sys_remote`. Siódmy (sesja na porcie z UART CREATE, link BLE central) nie zapisze
  się po cichu, a `call_wait` na nim zawiśnie na `portMAX_DELAY`. Po B1 wystarczy czekać z własnym limitem
  (timeout wywołania + zapas) i samemu zdjąć wywołanie z tabeli; wtedy rejestr tasków można usunąć, zostaje tylko
  odmowa `router_is_task()` dla wywołania do siebie.
- **Task `esp_timer` (stos 3584 B)** wykonuje: koniec sesji ze wszystkimi listenerami `gone` (trzy mutexy z
  `portMAX_DELAY`, broadcast STOP z buforem 512 B), timeouty wywołań z callbackami i rekordem błędu (165 B), tick
  watchy (`refused` 132 B, potem `sys_session_push` 512 B). Przepełnienia nie stwierdziłem, ale zapas jest nieznany, a
  każdy mutex wstrzymuje wszystkie timery firmware'u. → P1 i P8.
- **PING na zamkniętej sesji jest ignorowany.** Strona, która przegapiła hello po restarcie drugiej, dowiaduje się o
  tym dopiero z własnego timeoutu (sekundy). Odpowiedź `NO_SESSION` + hello także na PING naprawia to od razu.
- **`ERR_SESSION_NONE` raportowane dla każdej ramki** na zamkniętej sesji (`sys_session_hello_start_over`): ramki w
  locie po restarcie dają serię rekordów błędów. Raz na zamknięcie wystarczy.
- **Zapis `WIRED` ma bufor odpowiedzi 16 B** (`ANSWER_MAX`): poprawny zapis z dłuższą odpowiedzią kończy się
  `ERR_CMD_BAD_LEN`, choć się wykonał. Odpowiedź zapisu należy po prostu odrzucić.
- **Hello:** `version` nigdy się nie zmieni (brak wersjonowania), `fw_version` jest zawsze 0. Jedno pole z prawdziwym
  identyfikatorem buildu przyda się rootowi (czy slave wymaga OTA); `version` i `ERR_SESSION_VERSION` do usunięcia.
- **Id 0.** Root z id 0 nie otworzy sesji ze świeżym slave'em (też 0), a dwóch świeżych slave'ów nie da się
  skonfigurować jednocześnie. Zostawiam bez zmiany w firmware: kolejność (najpierw id roota) to sprawa aplikacji, ale
  warto, by `ERR_SESSION_ID_TAKEN` trafiał do klienta roota jako czytelny stan, nie tylko na strumień błędów.

---

## 2. Co komplikuje architekturę

1. **Trzy mechanizmy „peer chce coś dostawać”**, każdy z własną tabelą, listenerem `gone`, ponowieniem po `reached` i
   własnym licznikiem `seq`: subskrybenci strumienia, watche, subskrypcje urządzeń w `sys_remote`. Po stronie
   odbiorcy trzy osobne śledzenia dziur.
2. **Cztery kształty ciała PUSH**: `[stream][payload]`, `[0x80][owner][id][status][data]`, `[0x81][id u16][kind][data]`,
   `[0x82][to][from][data]`. Bajt `owner` we wpisie watcha powtarza `dst` nagłówka.
3. **Pięć rodzajów celu strumienia**, z czego `PROVIDER` znaczy co innego dla IN (źródło) i OUT (ujście), `OUTPUT`
   istnieje tylko dla IN, a `REMOTE` to to samo ujście na innej płytce.
4. **Cache odczytów zbudowany na zapas.** `sys_watch_read` ma jednego wywołującego (`sys_remote`), zawsze ze stałym
   `max_age` i `wait = false`. Mimo to klient obsługuje zmienne `max_age` z ponownym ADD, wariant czekający, licznik
   odczytów, okna 2× i 10×, `retry_ms`, oraz całe API właściciela (`sys_watch_remote_add` / `_remove`, callback,
   ponowne ADD po `reached`), którego nikt nie woła.
5. **Sztuczka z miejscem przed danymi.** Callback odpowiedzi pisze nagłówek w `CMD_HEAD` bajtów przed `data`
   (`data[-1..-4]` w `emit`, rzut `const` w `sys_session_call_answer`). Każdy, kto woła `router_done_f`, musi to
   miejsce zapewnić; a `sys_session_push` i tak kopiuje całość do bufora 512 B na stosie, żeby dokleić 5 bajtów.
6. **Kontekst wykonania.** Sesja nie ma taska, więc wszystko dzieje się na tasku RX sterownika albo na `esp_timer`.
   Stąd reguła „skopiuj i wróć” dla każdego callbacka, rejestr tasków, które nie mogą czekać, i własna kolejka z
   taskiem w `sys_remote`.
7. **Role sesji mają dwa pola** (`peer`, `attached`), osobny `s_state.master`, `admits`, `take_master`, a rodzaj
   `MASTER` jako argument ATTACH nie jest używany nigdzie.

---

## 3. Propozycja

### Co zostaje bez zmian

- Nagłówek `[frame][dst][src][seq u16]`, rodzaje REQUEST / ANSWER / PUSH / CONTROL, flagi URGENT i NOREPLY.
- Gwiazda o głębokości 1, routing po `dst` na tasku RX, bez stanu przekazywanej ramki. Slave ↔ slave zostaje
  zabronione (patrz „Rozważone i odrzucone”).
- Hello z `boot_id`, `NO_SESSION`, timeout mówiony w hello, dead-man jako cisza sesji. Sprawdziłem wariant ze stałym
  keepalive: root musi znać timeout slave'a, więc pole w hello jest potrzebne.
- Wywołania: slot do odpowiedzi, timeout zależny od łącza, limit w locie na płytkę, `ERR_REMOTE` nad łańcuchem.
- Transporty (COBS + CRC16, fragmenty BLE), command binding, flasher (link pożyczony jako DIRECT), OTA przez roota.
  Żadna z poniższych zmian nie dotyka ścieżki przekazywania, więc flashowanie i dostęp przez roota działają jak dziś.
- Watche po stronie płytki, która je wykonuje (`sys_watch.c`): model jest prosty i dobry.

### P1. Wysyłka dwuczęściowa

```c
err_h sys_data_provider_send(uint8_t id, const void* head, uint32_t head_len,
                             const void* body, uint32_t body_len, uint8_t flags);
```

Sterownik i tak kopiuje ramkę do swojej kolejki: rezerwuje `head_len + body_len` i robi dwa `memcpy`. Sesja trzyma
na stosie tylko 5 bajtów nagłówka. Znika: bufor `CMD_FRAME_MAX` w `sys_session_push`, `_data`, `call_frame`,
`call_broadcast`; reguła „`CMD_HEAD` bajtów przed danymi należy do callbacka” w `router_done_f` (dane stają się
`const`); zapis pod ujemnym indeksem w `sys_watch`. Stos taska strumieni i `esp_timer` przestaje zależeć od rozmiaru
ramki.

### P2. `seq` ramek PUSH liczy sesja

Dziś `seq` PUSH ma trzy znaczenia (licznik strumienia, watcha, subskrypcji) i trzy implementacje. Propozycja:
`seq` PUSH to licznik nadawcy **dla pary (dst, klasa)**, klasa = pilna albo zwykła (bit URGENT jest w nagłówku).

```c
typedef struct {            // jedna mała tabela w sesji, CONFIG_SYS_SESSION_PUSH_PEERS wpisów
  uint8_t  peer;            // adresat (dst) albo nadawca (src); SYS_SESSION_ID_NONE: wolny
  uint16_t tx[2];           // wysłane do niego: zwykłe, pilne (liczone także, gdy kolejka odmówiła)
  uint16_t rx[2];           // ostatnie od niego
  uint8_t  have;            // bit na klasę: rx jest ważne
} session_push_t;
```

- `sys_session_push(peer, data, len, flags)` traci argument `seq`. Listener PUSH dostaje gotowe `lost`.
- Znikają: `stream_t.seq`, `watch_t.seq`, `seq` i `have_seq` w kliencie watchy, `sub_t.seq` z `count_lost_locked`,
  `alias_t.seq`. Wpis zwalnia `gone` danego peera.
- Naprawia B2 z definicji.
- **Tabela jest liczona po adresatach, nie po sesjach.** Slave ma jedną sesję (z masterem), a wysyła PUSH do roota i
  do każdego klienta roota (watch aplikacji, subskrypcja strumienia). Rozmiar to własna opcja
  (`CONFIG_SYS_SESSION_PUSH_PEERS`: root + jego klienci na slave'ie, slave'y + klienci na roocie), nie
  `CONFIG_SYS_SESSION_MAX`. Wpis klienta roota slave zwalnia po `PEER {id, up = 0}` (P7), wszystkie po końcu sesji
  mastera. Pełna tabela: PUSH idzie z `seq` 0 i bitem „bez licznika” w ramce, zamiast odmowy (tunel nie może zależeć
  od limitu po stronie slave'a).
- **Koszt:** odbiorca wie, że zgubił N ramek od płytki X, ale nie wie, którego strumienia. Aplikacja po dziurze
  odświeża wszystko, co śledzi z tej płytki, zamiast jednego strumienia. `dropped` na celu strumienia zostaje do
  diagnostyki. Jeśli aplikacja naprawdę potrzebuje dziury per strumień (resynchronizacja samej telemetrii VM), tę
  zmianę należy pominąć i zrobić tylko poprawkę B2.

### P3. Ciało PUSH

| Pierwszy bajt | Ciało | Uwagi |
|---|---|---|
| `0x01..0x7F` | `[stream][payload]` | bez zmian |
| `0x80` | `[0x80][id][status][data]` | wpis watcha; bez bajtu `owner` (to `dst`) |
| `0x81` | `[0x81][id u8][kind][data]` | zdarzenie / błąd urządzenia; id ma 1 B jak w rejestrze reworku |
| `0x82` | `[0x82][to][from][data]` | dane do providera płytki `dst`; bez zmian |

**Kanał DATA zostaje** (decyzja). Dane do providera innej płytki idą prosto z taska RX na provider, z pominięciem
routera, więc tunel nie czeka za wolną komendą (transakcja I2C, `UART RAW` do 1 s). Zastąpienie go komendą
`PROVIDER SEND` z NOREPLY usunęłoby jeden kształt ramki, ale kosztem tej własności.

Z P1 `sys_session_data` składa `[nagłówek sesji][0x82][to][from]` w 8 bajtach na stosie i podaje je jako `head`, a
dane jako `body`: bufor 512 B znika także tutaj.

### P4. Cele strumienia

| Dziś | Po zmianie |
|---|---|
| `PROVIDER` na strumieniu IN | `SOURCE {provider}`: strumień bierze hook RX providera |
| `PROVIDER` na OUT, `OUTPUT`, `REMOTE` | `SINK {board, provider}`: `board` = ta płytka → wysyłka wprost; inna → ramka DATA (`sys_session_data`) |
| `PEER` | `PEER` |
| `CALLBACK` | `CALLBACK` |

Komendy: `SOURCE`, `SINK`, `UNBIND`, `SUBSCRIBE` zamiast `BIND` (dwa znaczenia), `OUTPUT`, `REMOTE`. Pole `dir` w
`sys_stream_cfg_t` staje się zbędne: strumień „IN” to strumień, który ma źródło, i taki odmawia `sys_stream_send`
(`ERR_STREAM_DIRECTION`). Tunel to nadal dwa strumienie, każdy z jednym SOURCE i jednym SINK.

### P5. Klient watchy: sam cache

```c
err_h sys_watch_read(uint8_t board, cmd_in_t read, void* buf, uint32_t cap, uint32_t* len);
void  sys_watch_stale(uint8_t board, cmd_in_t write, uint32_t target_len);
```

```c
typedef struct {
  uint8_t  state;         // FREE, ADDING (ADD wysłane), ACTIVE (jest kopia), OLD (po zapisie, czeka na odczyt)
  uint8_t  board, id, req_len, copy_len;
  uint16_t gen;
  uint32_t copy_ms, last_read_ms;
  uint8_t  req[SYS_WATCH_READ_MAX];
  uint8_t  copy[SYS_WATCH_COPY_MAX];
} slot_t;
```

- Pierwszy odczyt `(board, req)` zajmuje slot i wysyła ADD `ON_CHANGE` z okresem z Kconfig (`CONFIG_SYS_REMOTE_WATCH_MS`,
  cisza 2×). Do pierwszego wpisu `ERR_WATCH_NO_VALUE`, kopia za stara `ERR_WATCH_STALE`.
- Wpis przyjmowany tylko w stanach ACTIVE i po odpowiedzi na ADD (B3).
- Jedna reguła usuwania: nieczytany przez stały czas → REMOVE. Płytka zgubiona → wszystkie jej sloty do ADDING przy
  następnym odczycie.
- `sys_watch_stale` zostaje (gwarancja „odczyt po zapisie nie jest sprzed zapisu” jest warta swoich 40 linii).
- Znika: `wait`, `max_age_ms` jako argument i jego renegocjacja, `reads`, `prev_read_ms`, `retry_ms`, `added_age_ms`,
  `cached`, `cb` / `ctx` / `head`, `sys_watch_remote_add` / `_remove`, `on_reached`, `on_add_done` z callbackiem.

### P6. `sys_remote`

- **Tabela aliasów znika.** Rejestr reworku sam pilnuje, że urządzenie zdalne ma jedno lokalne id (`find_remote`,
  `ERR_DEV_ID_TAKEN`), więc `(board, remote_id) → local` to jedno zapytanie do rejestru. Znika `alias_t`,
  `CONFIG_SYS_REMOTE_ALIASES`, `ERR_REMOTE_ALIASES_FULL`. W reworku hooki `added` / `removed` już nie istnieją, więc
  `sys_remote` słucha zdarzeń CREATED / REMOVED. Czego potrzeba od rejestru i jak znika UNSUBSCRIBE bez adresu w
  REMOVED: R4.
- **Tabela subskrypcji zostaje** (`{peer, id, listens, handle}`), bez `seq` (P2). Filtr u źródła jest potrzebny:
  strumień zdarzeń niesie tylko NOTIFY, a alias potrzebuje wszystkich zdarzeń urządzenia (zbocza pinu).
- **Własna kolejka i task prawdopodobnie zbędne.** Powody z kodu to „`sys_session_push` bierze całą ramkę na stos”
  (znika z P1) i „raportujący może być taskiem timera” (`push` z `NOWAIT` nie blokuje). Na wejściu zdarzenie i tak
  idzie przez `sys_event_publish_deferred`. Do potwierdzenia: czy `err_report` błędu zdalnego urządzenia może biec na
  tasku RX (policy urządzeń ma w reworku własny task, więc powinno).
  **Ryzyko:** kolejka 1 KB wygładza dziś serie zdarzeń (zbocza pinu). Bez niej seria trafia wprost do kolejki
  sterownika, a na BLE to 4 ramki, z których po B4 połowa jest zarezerwowana dla odpowiedzi. Kolejkę usuwać dopiero po
  pomiarze `ERR_REMOTE_LOST` na BLE (sekcja 7); jeśli gubi, zostaje sama kolejka wyjściowa (bez taska na wejściu).
- Bit `not_wired` na płytkę i `wired_now` zostają.

### P7. Sesja: przyłącze i powiadomienie o peerach

- **ATTACH ma dwa rodzaje: `UP`** (konsola, para BLE: klient albo master) **i `DOWN`** (link, BLE central: slave).
  Rola `MASTER` wynika z hello (id płytki na przyłączu UP), nie jest polem konfiguracji. Znika `attached` obok
  `peer`, gałąź `clash` w attach i `MASTER` jako argument komendy.
- **`PEER {id, up}` zamiast `GONE {id}`**: root wysyła je na wszystkie pozostałe otwarte sesje (slave'y i klienci)
  przy otwarciu i zakończeniu sesji. Zamyka punkt „Open” z SYS_SESSION.MD: aplikacja dowiaduje się o restarcie
  slave'a od razu, a nie z ciszy watchy, i dodaje je ponownie. Koszt: jedna ramka kontrolna na zdarzenie.
- PING i każda inna ramka na zamkniętej sesji → `NO_SESSION` + hello; raport `ERR_SESSION_NONE` raz.

### P8. Timery

Tick sesji (pingi, timeouty sesji i wywołań) i tick watchy na `board_timer` (własny task 4 KB, może krótko czekać na
lock), zgodnie z punktem „Open” w `TIMER.MD`. Listenery `gone` przestają blokować `esp_timer`.

**Warunek: czekanie z własnym limitem wchodzi razem z P8.** Krok sterownika biegnie na tasku `board_timer`
(`sys_device_step`) i może wołać urządzenie zdalne `WIRED` (TICK silnika na mostku slave'a): `call_wait` śpi wtedy na
tym samym tasku, który po P8 odpala timeout wywołania, więc bez odpowiedzi nikt go nie obudzi. `call_wait` czeka więc
zawsze z własnym limitem (timeout wywołania + zapas) i sam zdejmuje wywołanie z tabeli (punkt „Tabela tasków” w
sekcji 1); to nie jest uproszczenie do zrobienia później. Koszt, który zostaje: przez czas takiego czekania stoją
wszystkie timery płytki, więc krok sterownika na urządzeniu zdalnym ma w dokumencie sterowników być wyjątkiem.

### Kontrakty po zmianach

```
REQUEST  [frame][dst][src][seq]  [key][dir][code | id + op][args]
ANSWER   [frame|status][dst][src][seq żądania]  dane | rekord błędu
PUSH     [frame][dst][src][seq pary (dst, klasa)]  [stream][payload] | [0x80][id][status][data] | [0x81][id][kind][data]
                                                   | [0x82][to][from][data]
CONTROL  [frame][dst][src][0]  HELLO {id, boot_id, fw, timeout_ms} | PING | NO_SESSION | PEER {id, up}
```

Wywołania providera, które zostają: `open` / `close` (jeden właściciel RX), `send(head, body, flags)` z `NOWAIT`,
`URGENT`, `SPARE`.

---

## 4. Wymagania, które plan zachowuje

Sprawdzone z czytania kodu i dokumentów, nie na płytce.

### Czysty tunel: aplikacja → master → slave

Slave nie wie, że komenda przyszła przez roota: root przekazuje ramkę po `dst` z nagłówka, bez stanu, a slave
wykonuje ją jak od każdego nadawcy i odpowiada na `src`. Żadna zmiana tego nie dotyka:

- P1, P3, P4 zmieniają ciało ramek i wysyłkę, nie trasę. Przekazanie to ta sama ramka podana jako `body` bez `head`.
- B4: root przy przekazywaniu czyta rodzaj ramki (bajt nagłówka), nigdy ciało.
- P7: `PEER {id, up = 0}` daje slave'owi to, co dziś `GONE`: klient roota zniknął, jego watche i subskrypcje się
  kończą. `PEER {id, up = 1}` slave ignoruje.
- Kanał DATA zostaje (P3): zapis do providera slave'a bez żadnej konfiguracji po jego stronie.
- P2: tabela liczników po adresatach, pełna tabela nie odmawia PUSH (patrz P2).

Reguła dla każdej następnej zmiany: nic po stronie slave'a nie zależy od tego, czy nadawca jest jego sąsiadem.

### Flash slave'a przez mastera

- **Zdalnie (OTA):** `BEGIN` / `WRITE` / `COMMIT` z `dst` = slave idą tunelem. B4 chroni odpowiedź na `WRITE` przed
  telemetrią, P7 mówi aplikacji od razu, że slave wrócił (kiedy wysłać `CONFIRM`), pole buildu w hello mówi, czy OTA
  jest potrzebne.
- **Po UART (ROM bootloader):** flasher nie używa sesji ani `sys_data_provider_send`; pożycza port
  (`sys_uart_link_direct`) i pisze po nim sam, więc P1 go nie dotyka. Przy przenoszeniu `sys_uart` do reworku zostają:
  tryb DIRECT (`sys_uart_write`, `_read`, `_flush_input`, `sys_uart_set_baud`) oraz to, że sesja linku kończy się i
  otwiera sama (provider w dół, potem w górę). Przyłącze linku to `DOWN` (P7); hello świeżej płytki ma id 0, więc root
  ma inne (bez zmiany).
- **Zostaje otwarte (jak dziś):** flasher kopiuje firmware mastera, nie obraz z aplikacji. Przy jednym buildzie
  kolejność to OTA mastera, potem flasher na slave'a.

**R5. Firmware PENDING bez `CONFIRM` restartuje się sam.** Dziś slave po OTA z firmware'em, w którym link nie działa,
wraca do starego slotu dopiero po restarcie, a zdalnie nikt go nie zrestartuje; slave na BLE nie ma drogi przez ROM.
Firmware w stanie `SYS_OTA_STATE_PENDING`, który nie dostał `CONFIRM` w `CONFIG_SYS_OTA_CONFIRM_S` od startu,
restartuje się (bootloader wraca do starego slotu). Jeden `board_timer` w `sys_ota`, bez zmiany formatu.

### Proxy device: wywołania, błędy, most zdarzeń

Rejestr reworku ma fundament: `CREATE_REMOTE`, slot zdalny (typ + `sys_device_remote_t`), op sprawdzony pod lockiem
(tabela typu, nie `LOCAL`, nie zapis w safe state) i hook wołany po zwolnieniu locka. Wywołanie `WIRED` wymaga B1.
Do tego cztery warunki:

**R1. Policy kończy przejście łańcucha na `ERR_REMOTE`.** `walk` w `sys_device_policy.c` reaguje na każdy węzeł
`ERR_DEV_ID`. Błąd urządzenia zdalnego to `ERR_DEV_ID{lokalne id}` nad `ERR_REMOTE{board, seq}` nad łańcuchem tamtej
płytki, a ten niesie jej id urządzeń: bez zatrzymania policy zadziała na lokalne urządzenia o tych samych numerach.
`ERR_REMOTE` wchodzi do `core/errors` (zna go policy i `sys_remote`), przejście kończy się na nim; severity łańcucha
liczy się nadal z całości (`err_chain_sev`), więc krytyczny błąd na slave'ie jest krytyczny dla aliasu.

**R2. Most nie przenosi zdarzeń cyklu życia rejestru jako zdarzeń aliasu.** Subskrypcja urządzenia bierze wszystko
o danym źródle, także `SYS_DEVICE_EV_CREATED` / `REMOVED` z `gen` tamtej płytki. Opublikowane lokalnie trafiają do
policy, która porównuje `gen` ze swoim: przy zgodnej liczbie zapomina ustawienia aliasu, który istnieje. Na płytce
urządzenia most pomija `CREATED` i `REMOVED`; `SUSPENDED`, `RESUMED`, `RESET` przechodzą (alias pokazuje stan
urządzenia, lokalna flaga wstrzymania aliasu pozostaje osobna: „ta płytka do niego nie pisze”). Usunięcie urządzenia
na jego płytce alias widzi jako `ERR_DEV_NOT_FOUND` w odpowiedzi.

**R3. Safe state płytki dochodzi do mastera.** Sweep publikuje jedno `SUSPENDED` / `RESUMED` ze źródłem
`SYS_DEVICE_ID_NONE`, którego nie łapie żadna subskrypcja urządzenia. `sys_remote` na płytce urządzeń wysyła je każdemu
subskrybentowi raz, jako `[0x81][0xFF][kind][event]` (id `0xFF`: płytka); odbiorca publikuje je dla każdego swojego
aliasu tej płytki. Bez tego master dowiaduje się z błędu pierwszego zapisu.

**R4. Rejestr: urządzenia zdalne do odczytu.** Dziś `find_remote` jest `static`, a REMOVED niesie tylko `gen`.
Dochodzi (sys_device_hooks.h):

```c
uint8_t sys_device_find_remote(uint8_t board, uint8_t remote_id);        // lokalne id albo SYS_DEVICE_ID_NONE
bool    sys_device_remote_of(uint8_t id, sys_device_remote_t* where);    // kopia rekordu; false: nie zdalne
```

- CREATED → `sys_device_remote_of(source)`: zdalne → SUBSCRIBE do jego płytki.
- Płytka osiągnięta ponownie → pętla po id z `sys_device_remote_of` (64 sloty), SUBSCRIBE każdego jej aliasu.
- REMOVED nie niesie adresu i nie musi: ramka `0x81` dla `(board, remote_id)` bez lokalnego aliasu
  (`sys_device_find_remote` = NONE) powoduje UNSUBSCRIBE do nadawcy. Subskrypcja usuniętego aliasu kończy się przy
  pierwszym zdarzeniu albo z końcem sesji; zdarzenie REMOVED zostaje bez zmian.

---

## 5. Rozważone i odrzucone

- **Wpis watcha jako kolejny ANSWER na ADD** (id watcha = `seq` żądania). Usuwa kanał 0x80, ale łamie regułę
  „wywołanie kończy się raz”, gubi zastępowanie watcha po id przy powrocie klienta i myli pierwszą odpowiedź (OK bez
  danych) z wpisami.
- **Jedna wspólna tabela subskrypcji w sesji** dla strumieni, watchy i urządzeń. Usuwa trzy listenery `gone`, ale
  watch i tak trzyma własny stan, a strumień liczy `dropped` per cel; przenosi złożoność zamiast ją usuwać.
- **Zdarzenia urządzeń zdalnych przez strumień zdarzeń** (bez subskrypcji per urządzenie). Strumień niesie tylko
  NOTIFY; rozszerzenie go o wszystkie zdarzenia zalałoby łącze zboczami pinów.
- **Slave ↔ slave przez roota.** W routingu to usunięcie trzech linii, ale slave musiałby wiedzieć o pojawieniu się
  i zniknięciu innych slave'ów, żeby odnawiać subskrypcje. Z `PEER` (P7) da się to dodać później bez zmiany formatu.
- **Stały keepalive zamiast timeoutu w hello.** Root musi pingować slave'a wg jego dead-mana, którego bez hello nie zna.
- **Drzewo głębsze niż 1.** Poza zakresem; wymaga tablicy tras i TTL.

---

## 6. Przeniesienie do reworku

Część sieciowa wchodzi do `runit-esp-rework` etapami. Każdy etap kończy się firmware'em reworku, który się buduje
(`.\build.ps1`), i bramką: testem, bez którego następny etap nie rusza. Poprawki i zmiany (B, P, R) wchodzą w etapie,
w którym komponent jest przenoszony, nie osobno. Stary `runit-esp` jest tylko źródłem części sieciowej: nic się w nim
nie poprawia i nic na nim nie testuje.

### Zasady

- **Źródło to `runit-esp/components/sys/`.** Folder komponentu trafia do `runit-esp-rework/components/sys/` i tam jest
  zmieniany; błędy z sekcji 1 są poprawiane dopiero tutaj, w etapie swojego komponentu.
- **Styl reworku przy kopii:** `MODULE_ID_DECLARE`, tabela opów routera (`P_OPERATIONS`, ramki nad tabelą), timery
  przez `board_timer`, zdarzenia przez `sys_event` reworku, urządzenia przez rejestr reworku (id 1 B).
- **Z komponentem idą:** jego `*.MD` i `AGENTS.md` (opis stanu po zmianie), `contract_*.json`, `errors_*.json`,
  wiersz w tabeli „In the build” w REWORK.md. `templates/sync.sh` szuka dziś w `runit-esp/`: deskryptory reworku
  podłącza etap 9.
- **Testy hosta przy kopii.** Żaden z przenoszonych komponentów poza `sys_flasher` ich nie ma. Sesja, klient watchy i
  `sys_remote` dostają je na fałszywym providerze (ramki podawane i odbierane w teście), bo ich błędy (B1, B3) to
  wyścigi i stany, których nie widać na płytce.
- **Klient testowy to skrypty hosta** (`router_link.py`, `router_tests.py`, `tree_tests.py`, `remote_tests.py`),
  zmieniane w etapie, który zmienia format. Aplikacja przechodzi na nowy format raz, w etapie 9.
- **`main` reworku rośnie w kompozycję** (`app/runit`): każdy etap dopisuje swój krok startu i klucz routera.

### Etapy

| Etap | Do reworku | Zmiany przy kopii | Zależy od | Bramka |
|---|---|---|---|---|
| 1 | `sys_nvs`, `sys_data_provider` | P1 (`send(head, body, flags)`), B4 (flaga `SPARE`); `router_done_f` z `const` danymi w `core/router` | — | test hosta providera na fałszywym sterowniku: dwie części, `NOWAIT`, `SPARE` |
| 2 | `bus/sys_uart` | P1, B4; piny przez `sys_io` reworku; tryb DIRECT bez zmian (flasher) | 1 | płytka: konsola przyjmuje i oddaje ramkę (COBS + CRC16); `sys_uart_link_direct` tam i z powrotem |
| 3 | `sys_session`, `sys_system` | B1, P7, P8 z czekaniem z własnym limitem, PING na zamkniętej sesji, pole buildu w hello, bez `version`, raport `ERR_SESSION_NONE` raz; `sys_system`: safe state to `sys_device_sweep(SUSPEND)` | 1, 2 | testy hosta sesji (hello, restart peera, timeout, B1); płytka: `router_tests.py`; dwie płytki: `tree_tests.py` (tunel) |
| 4 | — | P2 w sesji, tabela po adresatach (albo sama poprawka B2) | 3, decyzja o dziurach per strumień | test hosta: dziury per (peer, klasa), pełna tabela; `tree_tests.py` |
| 5 | `sys_stream`, `sys_log` | P4, P3 | 3 | płytka: strumień błędów i logów u klienta; dwie płytki: tunel z dwóch strumieni, ramka DATA do providera slave'a |
| 6 | `sys_ota`, `sys_flasher` | R5; strażnik OTA bez VM: `NULL` | 2, 3 | OTA slave'a przez roota, potem bez `CONFIRM`; flasher: nowa płytka otwiera sesję z id 0 |
| 7 | `sys_watch` | serwer: wpis bez `owner` (P3), tick na `board_timer`; klient: P5 napisany od nowa (B3 z definicji) | 3, 4 | test hosta klienta (stany FREE / ADDING / ACTIVE / OLD, B3); `tree_tests.py`: watch na slave'ie przez roota |
| 8 | `sys_remote` | najpierw R1 i R4 w rejestrze i policy reworku; potem P6, R2, R3; odpowiedź zapisu `WIRED` odrzucana zamiast bufora 16 B | 7 | test hosta mostu (R2, R3, UNSUBSCRIBE bez aliasu); `remote_tests.py`; pomiar kolejki na BLE (sekcja 7) |
| 9 | `sys_ble`; aplikacja | P1, B4, poza tym bez zmian; `runit-app/src/backend/session` na nowy format; `sync.sh` i deskryptory z reworku | 3..8 | `npm run test:hw` na firmware reworku; OTA i flasher z aplikacji |

- Etapy 5, 6 i 7 nie zależą od siebie: kolejność między nimi jest dowolna.
- R1 i R4 (etap 8) dotyczą komponentów, które już są w reworku, więc można je zrobić wcześniej, w dowolnym momencie.
- `sys_ble` jest na końcu, bo to 2000 linii bez zmian projektu, a wszystko przed nim testuje się po UART. Jeśli
  pomiar kolejki `sys_remote` na BLE (etap 8) ma być zrobiony przed decyzją o kolejce, `sys_ble` wchodzi przed
  etapem 8.
- Bramki z dwiema płytkami (etapy 3, 5, 6, 7, 8) sprawdzają wymagania z sekcji 4: tunel od etapu 3, flash w etapie 6,
  proxy w etapie 8.

### Poza zakresem tego planu

- `sys_actions`, `sys_project`, `sys_board`: rekord `SYS_ACTIONS_ID_LINKS` przyłącza link BLE central przy starcie.
  Do ich przeniesienia przyłącze robi komenda ATTACH (ręcznie albo ze skryptu).
- VM: strażnik OTA (`vm_exec_mode`) i bloki urządzeń zdalnych; do tego czasu proxy testują skrypty hosta przez
  komendy urządzeń.

### Decyzje przed startem

- Przed etapem 4: czy aplikacja potrzebuje dziur per strumień (P2). Kanał DATA zostaje (P3).
- Przed etapem 9: aplikacja przechodzi na nowy format jednym krokiem. Alternatywa (aplikacja zmieniana z każdym
  etapem) wymaga, żeby rework miał BLE już w etapie 3.

Uwaga do P2 przy zostającym kanale DATA: `seq` ramki DATA to dziś licznik strumienia, z którego pochodzi. Po P2 jest
to licznik pary (dst, klasa) jak dla każdego PUSH, a `(src, from id)` nadal mówi, kto nadał.

## 7. Do sprawdzenia na płytce

- Tunel po P2: slave z watchem aplikacji i subskrypcją strumienia drugiego klienta; dziury liczone osobno dla każdego.
- OTA slave'a przez roota przy włączonej telemetrii VM; potem celowo bez `CONFIRM`: powrót do starego slotu (R5).
- Flasher po przeniesieniu `sys_uart`: `n-flasher.hw.test.ts` kończy DONE, nowa płytka otwiera sesję z id 0.
- R1: błąd krytyczny urządzenia 7 na slave'ie przy lokalnym urządzeniu 7 na masterze; reaguje tylko policy aliasu.
- R2: usunięcie i ponowne utworzenie urządzenia na slave'ie; ustawienia policy aliasu na masterze zostają.
- R3: safe state slave'a (jego policy); alias na masterze pokazuje SUSPENDED bez próby zapisu.
- R4: usunięty alias; po pierwszym zdarzeniu jego urządzenia subskrypcja znika z tabeli slave'a.
- P6: seria zboczy pinu (1 kHz przez 100 ms) na urządzeniu zdalnym przez BLE i przez UART; `ERR_REMOTE_LOST` z kolejką
  `sys_remote` i bez niej.
- P8: krok sterownika na urządzeniu `WIRED`, slave odłączony w trakcie; krok kończy się błędem po własnym limicie,
  timery płytki ruszają dalej.
- B1: pilna komenda na urządzeniu `WIRED` plus druga pilna w trakcie; przed poprawką spodziewany pusty „sukces”.
- B2: slave publikuje zdarzenia gęsto (zbocza) i w tym czasie raportuje błąd urządzenia; licznik `ERR_REMOTE_LOST`.
- B3: dwa różne odczyty zdalne na przemian z przerwą dłuższą niż czas bezczynności.
- B4: telemetria VM na BLE przy pełnym obciążeniu i seria zapisów do slave'a; liczba zapisów bez odpowiedzi.
- Zapas stosu `esp_timer` (`uxTaskGetStackHighWaterMark`) przy końcu sesji z uzbrojonym dead-manem i 24 watchami.
- Po P7: restart slave'a widziany przez aplikację jako `PEER` w czasie krótszym niż timeout sesji.
