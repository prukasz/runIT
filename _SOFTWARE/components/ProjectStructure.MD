# Podział koncepcyjny

Na wersję aktualną składają się odpowiednie komponenty:

* runit_kernel
* drivers (+mocks)
* providers
* vm (bez implementacji)
* managers
* status
* packet_interface

## runit_kernel

Odpowiada za inicjalizację wszystkich innych komponentów oraz orkiestrację działań i obsługę błędów.

## managers

Udostępniają uniwersalną warstwę abstakcji do wykorzystania w ramach vm oraz runit_kernel

manager_ble -> wysylanie i odbieranie danych za pomocą ring-bufferów

manager_i2c -> warstwa abstakcji dołaczająca drivery do szyn i2c wraz z opcją zadań cyklicznych i jednorazowych ułatwiających współdzielenie szyn i2c

manager_io -> warstwa abstakcji pozwalająca na dołączenie dodatkowych peryferiów wramach ustandaryzowanych funkji. Pozwala na konfiguranie oraz użytkowanie podpietych urzadzeń z użyciem jednolitej logiki.

providers -> warstwa abstakcji ukrywająca sterowniki, oraz udostępniająca potrzebne funkcje dla innych komponentów, w ramach providers można dołączać do manager_io kolejne urządzenia

vm-> maszyna wirtualna uruchamiająca język blokowy (opierająca się na managerach oraz run_it kernel)

## drivers

Sterowniki - poszczególnych urządzeń:

Zalecana koncepcja sterownika:

* Brak wewnętrznej inicjalizacji i podpięcia do szyny np i2c -> to obsługuje manager
* Wewnętrzny task udostępniający opcję opóźnionej aktualizacji oraz obsługi przerwań (wentętrznie) wraz z możliwością przypisana callbacków użytkownika
  
  Koncepcja  opoźnionej aktualizacji oraz odczytu boforowanych danych (dereffered/ suspended mode) związana jest z możliwością wywołania w ISR oraz z brakiem data tearingu w przypadku korzystania w maszynie wirtualnej
* Wewnętrzy task w przypadku i2c może być wywoływany cyklicznie przez manager i2c lub jednorazowo, w takim przypadku musi przyjmować musi TaskHandle_t taksa powiadamiającego  i powiadamiać ten task po zakończeniu działań
* W przypadku sterowników do elemetów polegających na innych peryferiach, zalecane jest korzystanie z funcji udostępnionych przez managery, lub w przypadku braku takiej możliwości, odpowiedniej konfiguracji managerów, tak aby nie zachodziły na siebie impleentowane funckje np przerwanie z GPIO

## status wraz z integracją z runit_kernel

* Udostępnia zunifikowaną strukturę błędów
* Błędy typu Critical udostępniane są do runit_kernel w momencie zastosowania "PUSH" wszystkie struktury typu status dodane do bufora zostaną wysłane przez interfejs bezprzewodowy
* Dodawanie nowych kodów błędów oraz "właścicieli opiera się na "X macro"
* Błędu typu critical powinny być obsługiwane przez kernel jeśli zostały dodane do kolejki
* Możliwe jest zdalne wyłaczenie kolejkowania błędów i statusów danego typu

## packet inteface

Łączy ustandaryzowane funckje konfiguracyjne z managerów oraz VM

## features

Udogodnienia i obiekty zbudowane na już istniejących rozwiązaniach

# Złożenie w  całość

providers powinny być dodawane w rik_devices
rik_modules służy do inicjalizacji managerów
rik_scheduler obsługuje odpowiednie zdarzenia z managerów oraz taska obługi błędów

