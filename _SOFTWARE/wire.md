Architektura - założenia
funkcja nie będąca funkcją statyczną (wewnętrzną) może być wywołana na dwa (nie rozróżnialne dla niej sposoby):
- Przez dodanie wywołania do niej jako elementu kodu
- Przez przekazanie pakietu
Argument zwrotny musi zostać przekazany w sposób możliwy do interpretacji przez kod wbudowany oraz przez podmiot interpretujący pakiet.
Caller - nie jest świadomy jak działa wewnętrzna funkcja - jedyne, czego oczekuje to kodu błędu i odpowiedzi w postaci argumentu.

Jak realizacja powinna wyglądać: przykladowa

foo1(in x, out y){
    process(x)
    return(y) or error
}

--> foo2 wywołuje foo1
foo3(in x, out y){
    return foo1(process(x)) or error 
}

foo-remote(in x, out y){or error
    process(x)
    return(y) or error
}

foo2(in x, out y){
    return foo-remote(process(x)) or error
}

Funkcja foo jest wywoływana - poprzez przez przekazanie do niej parametrów - przy czym odpowiedz jak i parametry są strumieniem danych - co pozwoli na wywołanie dowolnego foo w innym "miejscu"
np jest urządzenie a, które jest na płytce 1 i urządzenie b na płytce 2 - oraz wersja, gdzie oba urządzenia są na jednej płytce.

niech urządzenie a, korzysta z urządzenia b
w przypadku, gdy są na jednej płytce - nie sprawia to problemu; natomiast musi być tak, gdy urządzenie b jest na płytce 2 - dla urządzenia a - nie zmienia to działania - istnieje mechanizm - ktory przekaże "stream parametrów" do płytki 2, a jako, ze urządzenie b zwraca strumien argumentów - to również urzadzenie a - powinno dostać ten sam rezultat - jakby urządzenie działało na płytce 1

taki mechanizm powinien istnieć pomiędzy każdym z elementów implementujących zachowanie tego routera.
w ten sam spób aplikacja jest w stanie zrealizować połączenie z płytką 1 i 2 i urzadzeniami a oraz b. 

jedyny problem sprawiają błędy, które pomiędzy płytką a i b nie stanowią struktury linked list tak jak w przypadku a i b na jednej płytce - jak to zrobić?

Dodatkowo, kto jest odpowiedzialny za zarządzanie tym, gdzie jest funkcja - czy dodajemy bajt adresu płytki.
uznajemy topologię mesh, gdzie plytka 1 ma np adres 0 - zawsze, i jest świadoma połączenia z innymi płytkami - przy czym płytki w topologii światome są swojego miejsca i źródła żądania, jak i odpowiedzi - czy ta architektura - jest możliwa? 

W ten sposób można utworzyć na jedny oprogramowaniu - zdalne io, urządzenie itd - pozwalając na rozłożenie obciążenia itd - tutaj problemem - wydaje się wąskie gardło - synchronizacja oraz latencja - w przypadku BLE - RTT może wynosic np50ms + czas na płytce - co w tedy ? W przypadku UART - sytuacja wygląda lepiej bo przy wysokim baudrate - nie sprawia to problemu aż takiego.
Drugim pytaniem z tej dziedziny jest to, czy część operacji można wykonać nie czekając na odpowiedź - robimy fire and forget (np zapis) - przy czym odczyt jest problemem - bo zawsze trzeba czekać - w przypadku fire and forget - problemem zdaje się odbiór potwierdzenia i odpowiedzi - co z nim wtedy ? 
