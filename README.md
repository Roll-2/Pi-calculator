# Chudnovsky π Calculator — 300,014 digits (`3m14`)

> 🌐 **Language / Lingua:** [🇬🇧 English](#english) · [🇮🇹 Italiano](#italiano)

---

# English

A multi-threaded C++17 program that computes **π to 300,014 decimal places**
using the **Chudnovsky series** with **binary splitting**, evaluated with
arbitrary-precision arithmetic from the **MPFR** library (wrapped by
**Boost.Multiprecision**).

| Property | Value |
|---|---|
| Language | C++17 |
| Dependencies | Boost.Multiprecision, MPFR, GMP, C++ threads |
| Digits computed | 300,014 decimal places (after the point) |
| Series terms | 21,157 (~14.18 digits per term) |
| Working precision | ≈ 997,578 bits (~300,300 decimal digits) |
| Threads | 4 (contiguous chunks, sequential merge) |
| Output | `pi.txt` |
| Measured runtime | ≈ 4.9 s (Apple Silicon, clang 21, `-O2`) |

---

## What it does

1. Computes π with the Chudnovsky formula using binary splitting over the
   term interval `[1, N)`.
2. Splits the interval into **4 contiguous chunks** computed in parallel on
   separate threads, then merges the partial results in order.
3. Formats the result to a string with `mpfr_sprintf` and writes it to
   `pi.txt` (`"3."` followed by 300,014 decimal digits, no trailing newline).
4. **Verifies the first 1,000 decimal digits** against a hard-coded known
   prefix and prints `Prime 1000 cifre: OK` on success.

## The algorithm

### Chudnovsky series

The classic Ramanujan-derived series used for record-breaking π computations:

```
1/π = 12 · Σ (-1)ᵏ (6k)! (13591409 + 545140134·k) / ((3k)! (k!)³ 640320^(3k+3/2))
```

The code uses the equivalent form

```
π = 426880·√10005 / S ,   S = Σ (-1)ᵏ (6k)! (13591409 + 545140134·k) / ((3k)! (k!)³ 640320^(3k))
```

Each term adds approximately `log10(640320³) ≈ 14.181647462725477` decimal
digits, so about 21,155 terms suffice for 300,014 digits (the code uses
`N = 21,157`).

### Binary splitting

Instead of summing the series term by term, the program recursively combines
blocks of terms. For a half-open interval `[a, b)` it builds a triple
`(P, Q, R)`:

- **Base case** (`b - a == 1`):
  - `P = (6a−5)(2a−1)(6a−1)`
  - `Q = a³ · C³/24` where `C³/24 = 640320³/24 = 10,939,058,860,032,000`
  - `R = (−1)ᵃ (13591409 + 545140134·a) · P`
- **Merge step** (`m = (a+b)/2`, left = `[a,m)`, right = `[m,b)`):
  - `P = P_left · P_right`
  - `Q = Q_left · Q_right`
  - `R = R_left · Q_right + P_left · R_right`

The recursion depth is only `log2(N) ≈ 15`, and the total work is
`O(N · M(n))` instead of `O(N² · M(n))` for naive summation.

Finally, the total sum is `S = A + R/Q` and `π = 426880·√10005 / S`
(`A = 13591409`).

### Parallelism

The interval `[1, N)` is divided into 4 contiguous chunks of approximately
equal size. Each thread independently runs `bs()` on its chunk, producing one
`Triple`; the 4 triples are then merged **in order** with the same merge rule,
so the result is exactly the full binary splitting of `[1, N)`.

## Precision & accuracy

| Quantity | Value | Notes |
|---|---|---|
| Terms used | `N = 21,157` | `⌊300014 / 14.181647462725477⌋ + 2` |
| Series truncation error | ≈ 10⁻³⁰⁰⁰⁴¹ | `14.181647462725477 · N`, printed as `Errore stimato` |
| Working precision | ≈ 997,578 bits | `TARGET_DIGITS·log2(10) + 1000` ≈ 300,300 digit-equivalents |
| Guard digits | ≈ 300 | extra bits beyond what the 300,014 output digits need |
| Built-in check | first 1,000 decimal digits | compared against a hard-coded known prefix — **passes** |

- The **truncation error** after 21,157 terms (~10⁻³⁰⁰⁰⁴¹) is dozens of
  orders of magnitude below the last printed digit.
- The **rounding error** is kept far below the last digit by the ~1,000 extra
  guard bits of MPFR precision; `mpfr_sprintf` performs correct rounding.
- Note: intermediate `P, Q, R` values are MPFR floating-point numbers (not
  exact rationals), so each operation rounds at full precision — with the
  guard margin this is negligible at this scale.
- **Reproducibility**: re-running the program regenerates `pi.txt` byte-for-byte
  identical to the committed file.

> The `Errore stimato` line is the bound on the *series truncation*; the true
> error of the printed digits also includes MPFR rounding, which is bounded
> by the guard bits and is many orders below the last digit.

## Build & run

Requirements: a C++17 compiler, [Boost.Multiprecision](https://www.boost.org/),
[MPFR](https://www.mpfr.org/) and GMP.

```bash
# macOS (Homebrew)
brew install boost mpfr gmp

# Debian / Ubuntu
sudo apt install libboost-dev libmpfr-dev libgmp-dev

# Build (macOS with Homebrew)
g++ -O2 -std=c++17 3m14.cpp -o 3m14_chudnovsky \
    -I/opt/homebrew/include -L/opt/homebrew/lib -lmpfr -lgmp

# Build (Linux)
g++ -O2 -std=c++17 3m14.cpp -o 3m14_chudnovsky -lmpfr -lgmp
```

Run:

```bash
./3m14_chudnovsky
```

Sample output (console messages are in Italian):

```
Termini: 21157
Prime 1000 cifre: OK
Tempo calcolo: 4.92788 s
Errore stimato: 10^{-300041}
Cifre complete salvate in pi.txt
```

## Configuration

Both knobs are constants at the top of `main()` in `3m14.cpp`:

- `TARGET_DIGITS = 300014` — number of decimal places written to `pi.txt`.
  **Scaling up**: the series truncation error is `10^(-14.18·N)`, so
  `N = digits / 14.1816 + 2` terms are used automatically; the +1000 guard
  bits are enough for any value of `TARGET_DIGITS` (beyond ~62M digits the
  base-case 64-bit arithmetic in `bs()` would overflow — see Notes).
- `NUM_THREADS = 4` — number of threads; `[1, N)` is split into this many
  contiguous chunks.

## Project layout

```
3m14.cpp          the implementation (algorithm, threading, I/O)
pi.txt            computed output: "3." + 300,014 decimal digits (300,016 bytes)
.gitignore        ignores build artifacts and the binary 3m14_chudnovsky
```

## Notes & observations

- **64-bit base case**: the product `(6a−5)(2a−1)(6a−1)` uses `unsigned long
  long`. It stays within 64 bits up to `a ≈ 4.4·10⁶` terms (≈ 62M digits),
  after which it would overflow.
- **File format**: `pi.txt` has no trailing newline; the first line starts
  with `3.1415926535...`.
- **Name**: `3m14` most likely reads as *"3M14"* — an ambition of 3,000,014
  digits — or as a nod to π ≈ 3.14; the committed target is 300,014 digits.

## License

No license file is present in the repository.

---

# Italiano

Programma C++17 multi-thread che calcola **π fino a 300.014 cifre decimali**
usando la **serie di Chudnovsky** con **binary splitting**, valutata con
aritmetica a precisione arbitraria della libreria **MPFR** (incapsulata da
**Boost.Multiprecision**).

| Proprietà | Valore |
|---|---|
| Linguaggio | C++17 |
| Dipendenze | Boost.Multiprecision, MPFR, GMP, thread C++ |
| Cifre calcolate | 300.014 decimali (dopo la virgola) |
| Termini della serie | 21.157 (~14,18 cifre per termine) |
| Precisione di lavoro | ≈ 997.578 bit (~300.300 cifre decimali) |
| Thread | 4 (chunk contigui, merge sequenziale) |
| Output | `pi.txt` |
| Tempo misurato | ≈ 4,9 s (Apple Silicon, clang 21, `-O2`) |

---

## Cosa fa

1. Calcola π con la formula di Chudnovsky usando il binary splitting
   sull'intervallo di termini `[1, N)`.
2. Divide l'intervallo in **4 chunk contigui** calcolati in parallelo su
   thread separati, poi unisce i risultati parziali in ordine.
3. Formatta il risultato in stringa con `mpfr_sprintf` e lo scrive in
   `pi.txt` (`"3."` seguito da 300.014 cifre decimali, senza newline finale).
4. **Verifica le prime 1.000 cifre decimali** confrontandole con un prefisso
   noto hard-coded e stampa `Prime 1000 cifre: OK` in caso di successo.

## L'algoritmo

### Serie di Chudnovsky

La classica serie derivata da Ramanujan usata per i record di calcolo di π:

```
1/π = 12 · Σ (-1)ᵏ (6k)! (13591409 + 545140134·k) / ((3k)! (k!)³ 640320^(3k+3/2))
```

Il codice usa la forma equivalente

```
π = 426880·√10005 / S ,   S = Σ (-1)ᵏ (6k)! (13591409 + 545140134·k) / ((3k)! (k!)³ 640320^(3k))
```

Ogni termine aggiunge circa `log10(640320³) ≈ 14,181647462725477` cifre
decimali, quindi bastano circa 21.155 termini per 300.014 cifre (il codice usa
`N = 21.157`).

### Binary splitting

Invece di sommare la serie termine per termine, il programma combina
ricorsivamente blocchi di termini. Per un intervallo semi-aperto `[a, b)`
costruisce una tripla `(P, Q, R)`:

- **Caso base** (`b - a == 1`):
  - `P = (6a−5)(2a−1)(6a−1)`
  - `Q = a³ · C³/24` dove `C³/24 = 640320³/24 = 10.939.058.860.032.000`
  - `R = (−1)ᵃ (13591409 + 545140134·a) · P`
- **Passo di merge** (`m = (a+b)/2`, sinistra = `[a,m)`, destra = `[m,b)`):
  - `P = P_sx · P_dx`
  - `Q = Q_sx · Q_dx`
  - `R = R_sx · Q_dx + P_sx · R_dx`

La profondità della ricorsione è solo `log2(N) ≈ 15`, e il lavoro totale è
`O(N · M(n))` invece di `O(N² · M(n))` della somma ingenua.

Infine la somma totale è `S = A + R/Q` e `π = 426880·√10005 / S`
(`A = 13591409`).

### Parallelismo

L'intervallo `[1, N)` viene diviso in 4 chunk contigui di dimensione
approssimativamente uguale. Ogni thread esegue in modo indipendente `bs()` sul
proprio chunk, producendo una `Triple`; le 4 triple vengono poi unite **in
ordine** con la stessa regola di merge, quindi il risultato è esattamente il
binary splitting completo di `[1, N)`.

## Precisione e accuratezza

| Grandezza | Valore | Note |
|---|---|---|
| Termini usati | `N = 21.157` | `⌊300014 / 14,181647462725477⌋ + 2` |
| Errore di troncamento della serie | ≈ 10⁻³⁰⁰⁰⁴¹ | `14,181647462725477 · N`, stampato come `Errore stimato` |
| Precisione di lavoro | ≈ 997.578 bit | `TARGET_DIGITS·log2(10) + 1000` ≈ 300.300 equivalenti decimali |
| Cifre di guardia | ≈ 300 | bit extra oltre quelli necessari alle 300.014 cifre di output |
| Controllo integrato | prime 1.000 cifre decimali | confrontate con un prefisso noto hard-coded — **superato** |

- L'**errore di troncamento** dopo 21.157 termini (~10⁻³⁰⁰⁰⁴¹) è di decine di
  ordini di grandezza al di sotto dell'ultima cifra stampata.
- L'**errore di arrotondamento** è mantenuto molto al di sotto dell'ultima
  cifra dai ~1.000 bit di guardia extra della precisione MPFR;
  `mpfr_sprintf` esegue l'arrotondamento corretto.
- Nota: i valori intermedi `P, Q, R` sono numeri in virgola mobile MPFR (non
  razionali esatti), quindi ogni operazione arrotonda a piena precisione — con
  il margine di guardia questo è trascurabile a questa scala.
- **Riproducibilità**: rieseguire il programma rigenera `pi.txt` identico
  byte per byte al file committato.

> La riga `Errore stimato` è il limite sul *troncamento della serie*; l'errore
> reale delle cifre stampate include anche l'arrotondamento MPFR, limitato dai
> bit di guardia e di molti ordini al di sotto dell'ultima cifra.

## Compilazione ed esecuzione

Requisiti: un compilatore C++17, [Boost.Multiprecision](https://www.boost.org/),
[MPFR](https://www.mpfr.org/) e GMP.

```bash
# macOS (Homebrew)
brew install boost mpfr gmp

# Debian / Ubuntu
sudo apt install libboost-dev libmpfr-dev libgmp-dev

# Compilazione (macOS con Homebrew)
g++ -O2 -std=c++17 3m14.cpp -o 3m14_chudnovsky \
    -I/opt/homebrew/include -L/opt/homebrew/lib -lmpfr -lgmp

# Compilazione (Linux)
g++ -O2 -std=c++17 3m14.cpp -o 3m14_chudnovsky -lmpfr -lgmp
```

Esecuzione:

```bash
./3m14_chudnovsky
```

Output di esempio (i messaggi a console sono in italiano):

```
Termini: 21157
Prime 1000 cifre: OK
Tempo calcolo: 4.92788 s
Errore stimato: 10^{-300041}
Cifre complete salvate in pi.txt
```

## Configurazione

Entrambe le manopole sono costanti in cima a `main()` in `3m14.cpp`:

- `TARGET_DIGITS = 300014` — numero di decimali scritti in `pi.txt`.
  **Scalabilità**: l'errore di troncamento della serie è `10^(-14,18·N)`,
  quindi `N = cifre / 14,1816 + 2` termini vengono usati automaticamente; i
  +1000 bit di guardia bastano per qualsiasi valore di `TARGET_DIGITS`
  (oltre ~62M di cifre l'aritmetica a 64 bit del caso base in `bs()` andrebbe
  in overflow — vedi Note).
- `NUM_THREADS = 4` — numero di thread; `[1, N)` viene diviso in questo numero
  di chunk contigui.

## Struttura del progetto

```
3m14.cpp          l'implementazione (algoritmo, threading, I/O)
pi.txt            output calcolato: "3." + 300.014 cifre decimali (300.016 byte)
.gitignore        ignora gli artefatti di build e il binario 3m14_chudnovsky
```

## Note e osservazioni

- **Caso base a 64 bit**: il prodotto `(6a−5)(2a−1)(6a−1)` usa `unsigned long
  long`. Resta entro 64 bit fino a `a ≈ 4,4·10⁶` termini (≈ 62M di cifre),
  oltre i quali andrebbe in overflow.
- **Formato file**: `pi.txt` non ha newline finale; la prima riga inizia con
  `3.1415926535...`.
- **Nome**: `3m14` si legge molto probabilmente come *"3M14"* — l'ambizione di
  3.000.014 cifre — oppure come un omaggio a π ≈ 3,14; il target committato è
  di 300.014 cifre.

## Licenza

Nel repository non è presente alcun file di licenza.
