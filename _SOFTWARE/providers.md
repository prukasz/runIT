Providery, sesja, konektory - decyzje

## Zrobione
- Z `state` providera zniknął `FRAMED`; sesja nie sprawdza już typu providera (`ERR_SESSION_NOT_FRAMED` usunięty).
- `SYSTEM` ustawia driver przy tworzeniu providera; `sys_data_provider_mark_system` usunięte.

## Do zrobienia

1. **`connector_raw`**: osobny konektor, który bierze providera na własność, bez sesji i subskrybentów.
   - bind `RX` / `TX`, rx `LATEST` / `FIFO`
   - `FORWARD` = tunel (co wejdzie na jednym providerze, wychodzi na drugim)
   - zna tylko providerów
2. **Broadcast sesji**: ramka STREAM z `dst = 0xFF`, bez zestawiania sesji (bez HELLO, SUBSCRIBE, stanu peera); liczy się tylko nagłówek (`kind`, `dst`, `src`, `seq`).
   - dla strumieni stratnych, na początek logi (`LOGS`)
3. **Drzewo to tylko dzieci**: płytka zna rodzica i bezpośrednie dzieci, głębokość 2 (gwiazda).
   - HELLO bez podrzewa i bez ponownego HELLO przy zmianie
   - routing: `dst` = dziecko, to do niego; `dst` = klient, to do klienta (korzeń); reszta do rodzica, a nieznane: `ERR_SESSION_NO_ROUTE`
   - `sys_session_below(board)` = czy to bezpośrednie dziecko
   - `sys_session_attach` odmawia `CHILD` płytce, która ma rodzica
4. **`connector` (obecny)**: zostają strumienie do peerów (subskrybenci, mirrory) przez sesję; bindingi do providerów przechodzą do `connector_raw`.
