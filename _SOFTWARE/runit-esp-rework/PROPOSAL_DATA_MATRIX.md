# data_matrix: jedno miejsce na wszystkie przepływy danych

Plan modułu, który ustawia w jednym miejscu, dokąd idzie każdy bajt, który nie jest komendą. Kroki 1–5 poniżej są zrobione (`components/sys/data_matrix/`, opis tego, co jest: `DATA_MATRIX.MD`), zostały rekord płytki, aplikacja i polecenie `CALL`. Ten plik opisuje całość pomysłu i to, co zostało.

## 1. Po co

Dziś ten sam rodzaj decyzji („te dane idą tu”) jest rozproszony w czterech miejscach, każde z własną komendą i własnym formatem:

| Miejsce | Co ustawia |
|---|---|
| `sys_stream` | cele strumieni: `SOURCE`, `SINK`, `PEER`, `CALLBACK` |
| `sys_data_provider` | `BIND_COMMANDS`: provider jako port komend |
| `sys_session` | `ATTACH` / `DETACH`: provider jako sesja |
| `sys_session` | `on_push`: kto odbiera PUSH-e (watch, remote) |

Cel: **jedna tabela i jeden format**, ustawiane zdalnie po id, zapisywane jako ramki rekordu płytki i odtwarzane przy starcie. Aplikacja ma jeden edytor „macierzy połączeń”, a master zapisuje taki rekord slave'owi przez sesję.

## 2. Model

**Rola providera** (jedna na provider, jawna): provider bez roli jest odpięty, jego dane są odrzucane i żadna trasa nie może go mieć na żadnym końcu.

| Rola | Znaczenie |
|---|---|
| `RAW` | bajty wysyłane i odbierane tak jak są, bez ramki i tagu |
| `SESSION` | działa na nim sesja (`sys_session`): REQUEST, ANSWER i CONTROL są sesji, PUSH jest danymi z tagiem. Parametry: dead-man (czas) i flaga `safety`. |
| `COMMANDS` | każda wiadomość to jedna komenda dla routera, odpowiedź wraca na ten provider |

**Końcówka** (3 B: rodzaj, `a`, `b`):

| Końcówka | Jako źródło | Jako cel |
|---|---|---|
| `P(provider)` | surowe bajty z `rx` | `sys_data_provider_send` |
| `T(provider, tag)` | PUSH z tagiem z sesji na tym providerze | PUSH przez sesję; na `RAW` same dane, tag nie jest wysyłany |
| `S(strumień)` | to, co kod wysłał `data_matrix_send` | wstrzyknięcie do strumienia |
| `C(callback)` | n/d | kod płytki; tylko w C, nigdy komendą |

**Trasa** `{z, do}` (6 B). Dozwolone pary:

| z \ do | `P` | `T` | `S` | `C` |
|---|---|---|---|---|
| `P` | tak | tak | tak | tak |
| `T` | tak | tak | tak | tak |
| `S` | tak | tak | nie | tak |

`P` jako cel na providerze z sesją jest odrzucone (surowe bajty zepsułyby ramki), `T` jest PUSH-em. Jeden koniec może mieć wiele tras i do jednego końca może prowadzić wiele tras, dane są kopiowane do każdej, NOWAIT. To, czego cel nie przyjął (zajęty lub wyłączony provider), ginie bez raportu, jak zawsze w strumieniach.

**Moduł to tabela i dostarczanie.** Nie zna protokołów: hello, ping, wywołania i żądania zostają w `sys_session`, komendy w routerze. Nie ma własnych stanów, liczników ani limitów poza rozmiarem tablicy.

## 3. Komendy (router `MODULE_ID_DATA_MATRIX` = `0x30`, do zadeklarowania z modułem)

| Komenda | Argumenty | Znaczenie |
|---|---|---|
| `ROLE` | provider, rola, `deadman_ms`, flagi | nadaje rolę; ta sama jeszcze raz: nic; inna: błąd, najpierw `CLEAR_ROLE` |
| `CLEAR_ROLE` | provider | zdejmuje trasy providera, kończy sesję; provider jest znów odpięty |
| `ROUTE` / `UNROUTE` | trasa | dodaje / usuwa; to samo jeszcze raz: nic |
| `CLEAR` | końcówka | usuwa każdą trasę, która ją ma po którejkolwiek stronie |
| `LIST` | n/d | role i trasy |

Wszystkie są ramkami rekordu płytki (SYS_BOARD.MD): najpierw `ROLE`, potem `ROUTE`, odtwarzane przy każdym starcie bez błędu.

## 4. Jak wyglądają przypadki

| Przypadek | Konfiguracja |
|---|---|
| logi na provider 1 (surowy) i 2 (sesja) | `ROLE 1 RAW`, `ROLE 2 SESSION`, `ROUTE S(logs)→T(1,logs)`, `ROUTE S(logs)→T(2,logs)` |
| watche z providerów x i y | `ROLE x SESSION`, `ROLE y SESSION`, `ROUTE T(x,watch)→C(w)`, `ROUTE T(y,watch)→C(w)` (callback dostaje `from`) |
| tunel k–j | `ROUTE P(k)→P(j)` i `ROUTE P(j)→P(k)` |
| provider z sesją: dane do routera | `ROLE s SESSION`: REQUEST idzie do routera sam, to nie jest trasa |
| provider m: surowe dane do routera | `ROLE m COMMANDS` |
| telemetria slave'a do aplikacji innym kanałem BLE | `forward`: `ROUTE T(uart_slave, telemetria)→T(ble_tel, telemetria)` |
| aplikacja do slave'a | komenda `CALL {board, request}` na masterze (osobno, nie matryca) |

## 5. Co z reszty wypada

| Dziś | Po zmianie |
|---|---|
| `sys_stream` (cele, router `0x29`, `sys_stream_send`) | cały moduł wchodzi do `data_matrix` (`data_matrix_send`, trasy) |
| `sys_data_provider`: `BIND_COMMANDS` / `UNBIND_COMMANDS` | rola `COMMANDS` |
| `sys_session`: `ATTACH` / `DETACH` w routerze, `on_push` | rola `SESSION`; sesja zostaje przy hello, wywołaniach, odpowiedziach, dead-manie i opakowaniu PUSH-a |
| `sys_watch`, `sys_remote`: `sys_session_push`, `sys_session_on_push` | `data_matrix_send_to` i końcówka `C` |
| `sys_log`, `runit`: tworzenie strumieni i sinków | domyślne `ROLE` i `ROUTE` w `runit`, potem w rekordzie |

## 6. Prymityw pod spodem: sesja jako warstwa

Sesja jest przezroczysta dla danych modułów, bez osobnej warstwy w providerze: macierz zna sesję (`sys_session_push(provider, [tag][dane])` wysyła PUSH z licznikiem sesji i flagami urgent/spare; `sys_session_set_push` oddaje jej każdy odebrany PUSH razem z providerem). `T(provider, tag)` na providerze z sesją to PUSH, na surowym same dane. Odbiorcy są trasami po `(provider, tag)`; forward i przetwarzanie przez system to kolejne trasy tego samego tagu.

Po zdjęciu nagłówka zostaje to samo: `REQUEST` to `[key][dir][op][args]` jak komenda surowego portu, `PUSH` to `[tag][dane]` jak surowe dane, `ANSWER` to dane ze statusem w bajcie ramki. Sesja dokłada tylko rodzaj ramki, `seq` do parowania odpowiedzi, flagi urgent/noreply, hello i ping.

**Odpowiedź komendy:** `router_submit(&cmd, on_answer, ctx)` z ctx = provider żądania. `seq`, `src` i flagi niesie `cmd`, więc odpowiedź wraca dokładnie tam, skąd przyszło żądanie, a router niczego o sesjach nie wie.

## 7. Sesja po przerobieniu (to już zrobione)

- punkt-punkt: bez trasowania, bez ról master/slave/client, bez `PEER`, bez kanału DATA; nagłówek 3 B (`[bajt ramki][seq]`),
- sesja siedzi na providerze do `DETACH`; wygaśnięcie ją zeruje (provider zostaje, sesja jest pusta do następnego hello),
- hello bez czasu: `{id, boot_id, build_id}`,
- dead-man **własny dla sesji**: czas i flaga `safety`. Bez czasu cisza niczego nie kończy. Z czasem: koniec po ciszy, a keepalive to 1/3 czasu. Flaga `safety` decyduje, czy koniec jest zdarzeniem bezpieczeństwa (safe state),
- `sys_session_broadcast` idzie na każdą otwartą sesję (STOP, czas),
- `REACHED` i `GONE` jako eventy (`sys_session_events.h`) zamiast hooków; PUSH zostaje hookiem (do 509 B, event ma 64 B).

## 8. Standard dla modułów, które mają strumienie

Tak samo jak eventy i błędy, tabela we własnym nagłówku modułu:

```c
#define LOG_STREAMS(X, P)                                          \
  X(P, LOGS,   0x03, DM_OUT, 0,         "log lines, text")         \
  X(P, ERRORS, 0x04, DM_OUT, DM_URGENT, "error records")
DATA_MATRIX_STREAMS_DECLARE(LOG)  // LOG_STREAM_LOGS = 0x03, flagi, opis
```

- id 1..0x7F, sprawdzane w czasie kompilacji i **zajmowane symbolem linkera** (dwa moduły z jednym id się nie zlinkują),
- `DM_OUT`: moduł woła `data_matrix_send(P_STREAM_x, …)` i nie wie, dokąd to idzie,
- `DM_IN`: moduł w `init` woła `data_matrix_serve(P_STREAM_x, fn, ctx)`, co tworzy końcówkę `C` i **stałą trasę `S(id)→C`**; z zewnątrz dane wchodzą zwykłą trasą `P/T→S(id)`,
- kanały modułów (watch, remote) to ten sam mechanizm z tagiem `0x80 | MODULE_ID`, bez własnej deklaracji.

## 9. Kolejność

Zrobione (host: 21 testów `data_matrix`, 22 `sys_session`, 7 `sys_watch`; oraz build firmware):

1. **Prymityw:** sesja ma jeden odbiornik PUSH-ów (`sys_session_set_push`, z providerem) i wysyła PUSH po providerze (`sys_session_push(provider, [tag][dane])`); odbiorcy po `(provider, tag)` to trasy macierzy; odpowiedź komendy wraca przez `router_submit(..., ctx = provider)`. Osobnej „warstwy” w providerze nie ma: macierz woła sesję.
2. **`data_matrix` zamiast `sys_stream`:** końcówki `P`, `T`, `S`, `C`, role, trasy, komendy `ROLE`, `CLEAR_ROLE`, `ROUTE`, `UNROUTE`, `CLEAR`, `LIST` (moduł `0x30`); `sys_stream` usunięty; `sys_log` i `runit` na `data_matrix_send` i domyślnych trasach.
3. **Role `SESSION` i `COMMANDS`:** `ATTACH` / `DETACH` zniknęły z routera sesji, `BIND_COMMANDS` z providera.
4. **Watch i remote jako zwykli odbiorcy:** kanały modułów (`SYS_WATCH_CHANNEL`, `SYS_REMOTE_CHANNEL`) to callback `C` z trasą `T(ANY, tag)`; wysyłka do sesji przez `data_matrix_send_to(T(provider, tag))`.
5. **Standard strumieni modułu:** tabela `X(P, NAZWA, id, flagi, opis)` (`LOG_STREAMS`, `RUNIT_STREAMS`), `DATA_MATRIX_STREAMS_DECLARE` / `_DEFINE`, id zajmowane symbolem linkera. Strona `DM_IN` (`data_matrix_serve`) nie istnieje: pierwszy konsument strumienia wejściowego (sloty VM) ją doda.

Zostało:

6. **`CALL {board, request}`** na masterze (aplikacja → komenda slave'a) i `forward` strumienia slave'a do BLE (`ROUTE T(uart_slave, telemetria) → T(ble, telemetria)` działa już jako trasa, brakuje komendy aplikacji).
7. **Rekord płytki:** ramki `ROLE` i `ROUTE` w `sys_board` / `SYS_ACTIONS_ID_DATA_MATRIX`, zapis master → slave przez sesję. Do tego czasu role i trasy płytki są tymi z `runit_ways_init`.
8. **Aplikacja:** migracja na deskryptory reworku (nagłówek 3 B, `PEERS`, kontrakt `data_matrix`, tag PUSH-a zamiast kanału), czytanie logów z osobnej charakterystyki.

## 10. Otwarte

- Publikacja tabel strumieni do aplikacji (nazwa, kierunek, opis): generator jeszcze ich nie zbiera, tak jak eventów, więc id `LOGS`, `ERRORS`, `EVENTS` nie są jeszcze w `enums.json`.
- Jedno `DATA_MATRIX_ANY` na wszystkie sesje (kanały modułów) to wyjątek od „trasa per provider”: lista tras pokazuje go jako provider `0xFF`.
- Tag PUSH-a nie niesie flagi `urgent` do odbiorcy: dane wchodzące z sesji do trasy mają flagi 0.
- Cały model nie był na płytce. Na sprzęcie trzeba sprawdzić BLE z dwoma service (`0xFFE0`, `0xFFE8`), sesję przez UART między płytkami, keepalive i to, że `ERRORS` / `EVENTS` dochodzą do aplikacji jako PUSH.
