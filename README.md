# Router-implementation
A small implementation of a router in C , ARP, ICMP and routing using LCPM
### Copyright Raducanu George-Cristian 2022-2023 321CAb

----
# Tema 1 PCOM - Dataplane Router
----
Tema 1 a constat in implementarea unui **Dataplane Router**
 
A fost necesar sa implementez trei protocoale pentru o functionare completa:<br />
* IPV4 - rutarea pachetelor <br />
* ICMP <br />
* ARP <br />
---
In cele ce voi urmeaza voi descrie "flow-ul" programului, apoi voi intra in 
detalii legate de fiecare dintre cele 3 protocoale si in final
 probleme intalnite si feedback legat de enunt.

---
---

## Structura, algoritm de speed-up cu putina memorie & flow program

Am pornit de la scheletul oferit in router.c impreuna cu functiile 
auxiliare puse la dispozitie in celelalte fisiere.

Primul pas in main este sa imi aloc spatiul necesar pentru cele 2 tabele
 si de asemenea si coada ce o voi folosi pentru a retine pachetele pe care
  nu le-am trimis inca. Intrucat coada oferita in schelet nu retine decat o 
  adresa, a fost nevoie sa imi fac o structura ajutatoare in care retineam 
  pachetul efectiv si lungimea acestuia.

  Inainte de a incepe efectiv rutarea pachetelor, de asemenea la pornirea
  routerului este necesar ca sa sortam tabela de rutare, intrucat se doreste
  gasirea eficienta a next->hop. O abordare cu trie ar fi fost un pic mai rapida
  dar proabil mai costisitoare din punct de vedere al memoriei.

  Faptul ca sortez tabela de rutare este un pic costisitor dar in principal
  qsort -ul are O(n * log n) si este cu atat mai aproape de aceasta complezitate
   cu cat n este mai mare si se indeparteaza de O(n ^ 2)

   Cautarea de realizeaza binar O(log n). La 80000 de intrari se simte un speed up
   semnificativ. log 80000 = 16.28 => reducere semnificativa.
   
   Dificultatea a constat in gasirea criteriului de sortare.

   Ideea a fost sa sortez crescator dupa prefixul "real", chiar prefix & masca proprie.

   In caz de egalitate se sorteaza crescator dupa dimensiunea mastii.

   Astfel, sortarea va arata in felul urmator(ca exemplu):
<br />

________________________________________________________________<br />
index:         |    0         |   1           |  2              ...<br />
prefix real:   | 192.168.0.0  |  192.168.0.0  |  192.168.0.0    ...<br />
lg masca reala:|   24         |      25       |  27             ...<br />
________________________________________________________________<br />
   
   
   Astfel la o cautare binara se cauta cel mai la dr element ce respecta proprietatea cu
   si pe masti.

In continuare incepe efectiv sa munceasca routerul.

Se primeste un pachet. Se obtine de asemenea si lungimea acestuia.

Se verifica tipul acestuia, daca este de tip IPv4 sau de tip ARP. Se urmeaza pasii
 specifici fiecarui format, urmand ca in cazul unor erori la IPv4 sa se trimita mesaje
  de tip ICMP. Se va detalia in sectiunile destinate fiecarui protocol in parte.

---
---

## Protocolul IPv4


* In cazul primirii unui pachet de tip IPv4 se verifica mai intai integritatea 
pachetului utilizand checksum-ul oferit si in cazul coruperii se afiseaza acest mesaj.

* In continuare, daca ttl ul pachetului primit este <= 1 se trimite mesaj de tip ICMP
(a se vedea sectiunea de **Protocol ICMP**)

* In caz contrar, se continua, decrementandu-se ttl-ul. 
* Se recalculeaza suma de control pt pachetul cu ttl-ul modificat.

* Se face verificarea de a fi un pachet trimis catre mine. (Routerul e destinatarul).
In cazul de fata este un mesaj ICMP de tip **echo Hello** si este necesar reply si ne oprim aici.

* Urmatorul pas a fost sa caut pe unde trebuie sa o iau, ruta cea mai 
buna. Aici intervine tabela/
vectorul sortat si cautarea binara.

* In cazul negasirii unei rute corespunzatoare se trimite mesaj de tip ICMP 
(vezi seztiunea ICMP)

* In cazul gasirii, se continua si se cauta in tabela mac/arp translatarea 
next hop in adresa mac.

* Daca translatarea nu este cunoscuta se trimite mesaj de tip ARP pentru a 
se afla, iar pachetul pentru a
nu fi pierdut se insereaza intr-o coada. Atunci cand voi primi mesaj de tip 
ICMP se va analiza toata coada
(vezi sectiunea ARP)

* In cazul pozitiv al succesului in toate situatiile se trimite in final 
pachetul cu toate headerele actualizate


---
---

## Protocolul ICMP

Protocolul ICMP consta in transmiterea in caz de eroare a unor mesaje catre sursa.

In cazul ICMP pachetul are urmatoarele headere in ordine de la stanga la dreapta:

Header eth | Header IPv4 | Header ICMP

In cazul temei au fost necesare 3 cazuri din ICMP:

* Pachet "expirat" cu ttl <= 1

* Pachet caruia nu ii gasesc ruta, nu stiu pe unde sa il trimit ("Destination unreacheable")

* Mesaj chiar pentru routerul meu, de tip *echo Hello*, si raspund cu *echo Reply*

---

* In cazul primelor 2 tipuri de mesaje se trimite in payload-ul vechi
 chiar headerul ip vechi +
inca 8 octeti din pachetul vechi din payload-ul dupa IPv4 ("deasupra
 headeului IPv4 in stiva OSI"). Cele 2 cazuri nu difera in implementare 
 decat prin un camp al header-ului ICMP necesare pentru a face distinctia (setare corespunzatoare).

* In cazul mesajelor de tip echo reply in payload-ul ICMP se pune 
intreg pachetul vechi. Si campurile din header-ul ICMP curent sa fie
 setate corespunzator pentru acest protocol. 


---
---

## Protocolul ARP

In cazul mesajelor de tip ARP, pachetele arata de forma:

Header eth | Header ARP | ...

Acest protocol se aplica in urmatoarele situatii:

* Intrarea IPv4 ce se doreste a fi "translatata" in MAC nu este gasita in tabela, 
astfel trebuie sa fac un mesaj de tip broadcast in urma caruia sa se obtina 
translatarea. Pana le primirea unui raspuns, pachetul se pune intr-o coada de 
pachete, ce urmeaza a fi procesata in cazul primirii unui raspuns.

* Routerul primeste el o cerere de tip ARP si raspunde cu adresa sa MAC, 
completand headerele si apoi trimitand mesajul corespunzator.

* Routerul primeste raspuns de tip ARP.
Isi actualizeaza tabela. Urmatorul pas este sa parcurga **intreaga** coada de 
pachete pentru a verifica din nou daca se cunoaste "translatarea". In caz afirmativ,
 se trimite pachetul respectiv intrucat avem acum toate informatiile necesare. 
 In caz contrar, se insereaza din nou pachetul, de data asta intr-o coada auxiliara.
  La finalul iteratiei prin coada se atribuie coada auxiliara celei initiale 
  pentru pastrarea pachetelor ramase.


---
---

## Dificultati intalite si feedback legat de cerinta

Am intalnit cateva dificultati tehnice, atat legate de checker cat si legate de 
propria implementare. A durat mult pana am inteles ce trebuie sa pun in pachete, 
mai ales in pachetele de ICMP unde enuntul a fost corectat de cateva ori pe website.
 O alta dificultate a constat in gasirea unui algoritm eficient de rutare care sa nu
  foloseasca trie-uri intrucat memoria folosita ar fi crescut enorm si ar fi fost si
   mai greu de inteles si implementat. Cautarea binara este o solutie eficienta si usor de inteles.

Ca feedback as spune ca enunturile trebuie explicate mai clar la PCOM, si sa nu fim
 pusi sa cautam chiar totul in RFC-uri si standardele IEEE. Trebuie si enuntul sa fie
  mai clar din punct de vedere al pasilor ce trebuie urmati.

---

## Resurse utilizate:

* https://vinesmsuic.github.io/notes-networkingIP-L3/#Example-Binary-search-based-on-prefix-length-8-bit
* https://www.rfc-editor.org/rfc/rfc5342.html#appendix-B
* https://www.rfc-editor.org/rfc/rfc792
