# Chudnovsky π Calculator & Visualizer (`pi_calc` · `Pi-visual` · `pi300k` · `pi100m`)

> 🌐 **Language / Lingua:** [🇬🇧 English](#english) · [🇮🇹 Italiano](#italiano)

---

# English

Two multi-threaded C++17 programs that compute **π** using the **Chudnovsky
series** with **binary splitting**, evaluated with arbitrary-precision
arithmetic from **GMP/MPFR** (wrapped by **Boost.Multiprecision**):

| Program | Digits | Result file |
|---|---|---|
| `pi_calc.cpp` | configurable via `-N` (default 314,159) | `pi_<N>.txt` |
| `pi300k.cpp` | 300,014 (standard, reference) | `pi300k.txt` |
| `pi100m.cpp` | up to 100,000,000+ (maximum, reference) | `pi100m.txt` |

The names share a prefix so the files stay aligned in a terminal listing;
`pi100m.txt` is git-ignored because it is ~100 MB.

---

## `pi_calc.cpp` — the unified calculator

`pi_calc.cpp` is the current tool: it merges `pi300k.cpp` and `pi100m.cpp`
into one program based entirely on the `pi100m.cpp` architecture (exact
`mpz_class` binary splitting, dynamic-precision Newton sqrt, fast
`mpz_get_str` conversion). The two older programs remain in the repository
as reference.

| Property | Value |
|---|---|
| Default | 314,159 digits, 4 threads |
| Output | `pi_<N>.txt` (`"3."` + N digits, no trailing newline) |
| Verification | first 1,000 digits against a hard-coded prefix (on by default) |
| RAM control | hard barrier: no computation beyond the cap, no override flag |

### Usage

```bash
# Build (macOS ARM, Homebrew)
clang++ -std=c++17 -O2 pi_calc.cpp -o pi_calc \
    -I/opt/homebrew/include -L/opt/homebrew/lib -lmpfr -lgmp

./pi_calc                      # 314,159 digits, 4 threads -> pi_314159.txt
./pi_calc -N 10000000 -t 8     # 10M digits, 8 threads
./pi_calc -N 1000 --no-verify  # minimum size, skip the prefix check
./pi_calc -h                   # usage
```

| Option | Meaning |
|---|---|
| `-N <digits>` | decimal places (default 314159, minimum 1000) |
| `-t <threads>` | compute threads (default 4, at most `hardware_concurrency()`) |
| `-o <file>` | output file (default `pi_<N>.txt`) |
| `--no-verify` | skip the 1,000-digit prefix verification |
| `-h`, `--help` | usage |

### The RAM barrier

The program never risks heavy swapping or OOM. Before any large allocation:

- **peak estimate** = `N × 53` bytes — empirical formula measured on this
  machine with the 20% margin included (10M digits → 0.53 GB estimated,
  100M → 5.3 GB estimated vs 4.4 GB measured);
- **cap** = `min(20 GB, system RAM − 4 GB)` — system RAM read from
  `sysctl hw.memsize`;
- estimate and cap are printed at startup (`stima picco: X.X GB /
  disponibile: Y.Y GB`);
- if the estimate exceeds the cap the program exits with a clear error
  (digits requested, estimated peak, effective cap, detected system RAM,
  suggestion to lower `-N`) **without computing anything**. There is
  **no** `--force` / `--max-ram` flag: the barrier is hard by design.

On the 24 GB machine the cap is 20 GB, which admits up to ≈ 345M digits
(≈ 20–25 min of compute). If the 1,000-digit verification fails the program
exits with an error and writes no file.

---

## `pi300k.cpp` — the standard version

| Property | Value |
|---|---|
| Language | C++17 |
| Dependencies | Boost.Multiprecision, MPFR, GMP, C++ threads |
| Digits computed | 300,014 decimal places (after the point) |
| Series terms | 21,157 (~14.18 digits per term) |
| Working precision | ≈ 997,578 bits (~300,300 decimal digits) |
| Threads | 4 (contiguous chunks, sequential merge) |
| Output | `pi300k.txt` |
| Measured runtime | ≈ 4.9 s (Apple Silicon, clang 21, `-O2`) |

### What it does

1. Computes π with the Chudnovsky formula using binary splitting over the
   term interval `[1, N)`.
2. Splits the interval into **4 contiguous chunks** computed in parallel on
   separate threads, then merges the partial results in order.
3. Formats the result to a string with `mpfr_sprintf` and writes it to
   `pi300k.txt` (`"3."` followed by 300,014 decimal digits, no trailing
   newline).
4. **Verifies the first 1,000 decimal digits** against a hard-coded known
   prefix and prints `Prime 1000 cifre: OK` on success.

### The algorithm

The classic Ramanujan-derived series used for record-breaking π computations:

```
π = 426880·√10005 / S ,   S = Σ (-1)ᵏ (6k)! (13591409 + 545140134·k) / ((3k)! (k!)³ 640320^(3k))
```

Each term adds approximately `log10(640320³) ≈ 14.181647462725477` decimal
digits, so about 21,155 terms suffice for 300,014 digits (the code uses
`N = 21,157`).

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
`O(N · M(n))` instead of `O(N² · M(n))` for naive summation. Finally, the
total sum is `S = A + R/Q` and `π = 426880·√10005 / S` (`A = 13591409`).

### Precision & accuracy

| Quantity | Value | Notes |
|---|---|---|
| Terms used | `N = 21,157` | `⌊300014 / 14.181647462725477⌋ + 2` |
| Series truncation error | ≈ 10⁻³⁰⁰⁰⁴¹ | `14.181647462725477 · N`, printed as `Errore stimato` |
| Working precision | ≈ 997,578 bits | `TARGET_DIGITS·log2(10) + 1000` |
| Guard digits | ≈ 300 | extra bits beyond what the 300,014 output digits need |
| Built-in check | first 1,000 decimal digits | compared against a hard-coded known prefix — **passes** |

- The **truncation error** after 21,157 terms (~10⁻³⁰⁰⁰⁴¹) is dozens of
  orders of magnitude below the last printed digit.
- The **rounding error** is kept far below the last digit by the ~1,000 extra
  guard bits of MPFR precision; `mpfr_sprintf` performs correct rounding.
- **Reproducibility**: re-running the program regenerates `pi300k.txt`
  byte-for-byte identical to the committed file.

### Build & run

Requirements: a C++17 compiler, [Boost.Multiprecision](https://www.boost.org/),
[MPFR](https://www.mpfr.org/) and GMP.

```bash
# macOS (Homebrew)
brew install boost mpfr gmp

# Debian / Ubuntu
sudo apt install libboost-dev libmpfr-dev libgmp-dev

# Build (macOS with Homebrew)
g++ -O2 -std=c++17 pi300k.cpp -o pi300k \
    -I/opt/homebrew/include -L/opt/homebrew/lib -lmpfr -lgmp

# Build (Linux)
g++ -O2 -std=c++17 pi300k.cpp -o pi300k -lmpfr -lgmp

./pi300k
```

Sample output (console messages are in Italian):

```
Termini: 21157
Prime 1000 cifre: OK
Tempo calcolo: 4.95486 s
Errore stimato: 10^{-300041}
Cifre complete salvate in pi300k.txt
```

### Configuration

Both knobs are constants at the top of `main()` in `pi300k.cpp`:

- `TARGET_DIGITS = 300014` — number of decimal places written to
  `pi300k.txt`. Beyond ~62M digits the base-case 64-bit arithmetic in `bs()`
  would overflow — see Notes.
- `NUM_THREADS = 4` — number of threads; `[1, N)` is split into this many
  contiguous chunks.

> In `pi100m.cpp` digits and threads are command-line arguments instead of
> constants — see the section below.

---

## `pi100m.cpp` — the maximum version

`pi100m.cpp` is the most powerful of the two implementations. It applies the
techniques used by world-record π codes, following the architecture of
`gmp-chudnovsky.c`, the GMP team's reference implementation by Hanhong Xue:

| Record technique | Applied here |
|---|---|
| 1. Binary splitting | full binary tree of terms |
| 2. Exact integer math | the whole `P, Q, T` tree runs on **GMP integers** (`mpz_class`); MPFR floating point is used **once**, for the final single division |
| 3. FFT multiplication | GMP's Schönhage–Strassen is selected automatically above its FFT threshold; no tuning needed |
| 4. Dynamic-precision Newton sqrt | `sqrt(10005)` via Newton iterations that double the precision each step (a final full-precision step is included, otherwise the last digits are wrong) |
| 5. Fast decimal output | `round(pi·10^D)` as an exact integer, converted with GMP's divide-and-conquer `mpz_get_str`; math and I/O timings are printed separately |

### Usage

```bash
# Build (macOS with Homebrew)
g++ -O2 -std=c++17 pi100m.cpp -o pi100m \
    -I/opt/homebrew/include -L/opt/homebrew/lib -lmpfr -lgmp

# Build (Linux)
g++ -O2 -std=c++17 pi100m.cpp -o pi100m -lmpfr -lgmp

./pi100m              # 1,000,000 digits, all cores
./pi100m 100000000 10 # 100,000,000 digits, 10 of 12 cores (~80% CPU)
# optional: nice -n 10 keeps the machine responsive during long runs
```

### Measured performance

Apple M4 Pro (12 cores, 24 GB RAM, clang 21, `-O2`); both programs write the
same digits (300k verified byte-for-byte, 1M cross-checked against an
independent π-digit service with 80/80 matches at positions 100k, 500k and
999.9k):

| Digits | `pi300k.cpp` (4 threads) | `pi100m.cpp` (10 threads) |
|---|---|---|
| 300,014 | ≈ 4.9 s | ≈ 0.25 s |
| 1,000,000 | — | ≈ 1.1 s |
| 100,000,000 | — | ≈ 322 s (~5.4 min), ≈ 4.4 GB RAM |

### The maximum result (`pi100m.txt`)

`pi100m.txt` contains **100,000,000 decimal digits** (`"3."` + 100,000,000
digits, ≈ 100 MB, git-ignored), computed at ~80% CPU (10 of 12 cores) and
**fully verified**:

- first 300,014 digits **byte-identical to `pi300k.txt`**;
- first 1,000 digits match the hard-coded known prefix;
- digit frequency uniform within 0.01% over all 100M digits;
- series truncation error `10^(-10⁸)`, far below the last digit;
- **last digits verified**: a second independent computation with 5× the
  guard margin (+5000 bits of working precision) produced a **byte-identical
  file** — the result does not depend on the rounding margin, so all
  100,000,000 digits (including the last ones) are correctly rounded.

### Viewing `pi100m.txt`

The file is a **single line of ~100 MB** — that is normal and why
TextEdit/kitty struggle with it (they try to render one huge line). Use the
terminal instead:

```bash
head -c 100 pi100m.txt          # first 100 digits
tail -c 100 pi100m.txt          # last 100 digits
fold -w 100 pi100m.txt | less   # paginate wrapped in lines of 100 digits
split -b 1M pi100m.txt pi_part_ # split into 1 MB chunks
```

### Theoretical limits

1. **Series truncation** — the Chudnovsky error after N terms is
   `10^(-14.1816·N)`, so more terms can always be added: no ceiling here.
2. **64-bit base case** — `pi300k.cpp` overflows at `a ≳ 4.4·10⁶` terms,
   i.e. ≈ 62 million digits. `pi100m.cpp` removes this ceiling with GMP
   integers.
3. **Memory** — peak footprint grows roughly linearly with the digits:
   ≈ 0.5 GB at 10M, ≈ 4.4 GB at 100M. 314M digits would need ≈ 15 GB and
   ≈ 30–45 min; 1 billion digits would need ≈ 45 GB, beyond the 24 GB
   available.
4. **Time** — measured scaling ≈ n^1.25 (1M → 10M → 31.4M → 100M).

### The world record

The current record is **314,000,000,000,000 digits (314 trillion)**, computed
by **StorageReview** (November 2025) with the *same* Chudnovsky algorithm via
y-cruncher — but on a dedicated 2U server with 384 cores, 1.5 TB of RAM,
~2.4 PB of NVMe storage, running for ~110 days.

---

## Pi-visual — the π visualizer

`Pi-visual.cpp` (+ headers in `visual/`) is an SDL2 + OpenGL 3.3 core window
split into two viewports, driven by one clock `t` and one monotonically
increasing rate `rate(t)` (exponential saturating at t ≈ 120 s), so both
panels accelerate together and the significant animation lasts ~2 minutes.
Afterwards the app stays alive with the stabilized animation and the HUD.

- **left — compound pendulum** `Z(θ) = e^(iθ) + e^(iπθ)`, with π hard-coded
  to 17 decimals. The camera works with **events**: at each of the two famous
  near-closures of π — 14π (gap ≈ 0.056) and 226π (gap ≈ 0.0002, sub-pixel),
  the only points where the curve returns to graze its birth point (2,0) —
  the view dollies in and time **slows down** so the near-contact and the
  colour change of the curve are clearly visible; outside the events, wide
  view and full speed. The curve changes colour at every pass.
- **right — chord diagram** of the digits of π read from `pi100m.txt` via
  `mmap` (never copied into memory): 10 colored sectors (0–9) with large
  digit labels around the ring, one **curved** chord (quadratic Bézier) per
  consecutive digit pair with anchor points spread along each sector arc.
  The web **grows from zero**: the static matrix is fed by the live stream
  and rebuilt every 512 chords; recent chords shine in the foreground
  (quadratic 30 s fade, additive blending) then settle into the web. A
  two-stage rate: slow linear ramp (60 digits/s) up to 100k digits, then
  exponential acceleration to saturation.

### Build & run

```bash
make           # builds Pi-visual (see the Makefile or the header comment)
./Pi-visual    # reads pi100m.txt in the current directory
```

### Keys

| Key | Action |
|---|---|
| `0`–`3` | palette: 0 provisional, 1 Botanica, 2 Alba, 3 Notturna |
| `S` | PNG screenshot into `pictures/` (fallback: `$HOME`) |
| `R` | reset the clock and restart from the beginning of the π file |
| `F` | fullscreen on/off |
| `Esc` | quit |

### Options & limits

- `--pi <file>` reads a different digits file (format: `3.` + digits, no
  trailing newline — e.g. `pi300k.txt` or any `pi_<N>.txt` produced by
  `pi_calc`). The file is verified against the known 100-digit prefix at
  startup; on mismatch the program exits with an error.
- Requires macOS ARM + Homebrew (`sdl2`, `sdl2_ttf`; zlib ships with the
  OS), OpenGL 3.3 core, and the system font
  `/System/Library/Fonts/Helvetica.ttc` (no external assets).
- The mmap'd digits file does not count toward any RAM budget: resident
  memory stays ~100 MB even with the 100M-digit file.
- With `PI_VISUAL_CHECK=1` the app runs an automatic framebuffer diagnostic
  at t = 20 s (ring centering/roundness, chord color variety, pendulum
  visibility, big digit labels) and saves control PNGs into `pictures/`.
  `PI_VISUAL_LABEL_COLOR=1` additionally draws the labels in pure magenta
  so the check can count their pixels.

## Project layout

```
pi_calc.cpp       the unified calculator: CLI, RAM barrier, built-in verification
Pi-visual.cpp     the π visualizer: pendulum + chord diagram, two viewports
visual/           its headers: pi_reader, chord, pendulum, hud, camera, screenshot
Makefile          minimal build for Pi-visual (all / run / clean)
memory.md         session log (environment, decisions, steps)
pi100m.cpp        the maximum version (reference): exact mpz tree, Newton sqrt, fast conversion
pi100m.txt        its output: "3." + 100,000,000 digits (git-ignored, ~100 MB)
pi300k.cpp        the standard version (reference, mpfr tree, 300,014 digits)
pi300k.txt        its output: "3." + 300,014 decimal digits (300,016 bytes)
.gitignore        ignores build artifacts, binaries, pi_*.txt outputs and pictures/
```

## Notes & observations

- **File format**: neither output file has a trailing newline; both start
  with `3.1415926535...`.
- **Naming**: files are named by their size (`pi300k` = 300,014 digits,
  `pi100m` = 100,000,000 digits) so that related files share a prefix and
  stay aligned in a terminal listing.

## License

No license file is present in the repository.

---

# Italiano

Due programmi C++17 multi-thread che calcolano **π** usando la **serie di
Chudnovsky** con **binary splitting**, valutata con aritmetica a precisione
arbitraria di **GMP/MPFR** (incapsulata da **Boost.Multiprecision**):

| Programma | Cifre | File risultato |
|---|---|---|
| `pi_calc.cpp` | configurabile con `-N` (default 314.159) | `pi_<N>.txt` |
| `pi300k.cpp` | 300.014 (standard, riferimento) | `pi300k.txt` |
| `pi100m.cpp` | fino a 100.000.000+ (massima, riferimento) | `pi100m.txt` |

I nomi condividono il prefisso così i file restano allineati nel terminale;
`pi100m.txt` è git-ignorato perché pesa ~100 MB.

---

## `pi_calc.cpp` — il calcolatore unificato

`pi_calc.cpp` è lo strumento attuale: unisce `pi300k.cpp` e `pi100m.cpp` in
un solo programma basato integralmente sull'architettura di `pi100m.cpp`
(binary splitting esatto su `mpz_class`, sqrt con Newton a precisione
dinamica, conversione veloce con `mpz_get_str`). I due programmi più vecchi
restano nel repository come riferimento.

| Proprietà | Valore |
|---|---|
| Default | 314.159 cifre, 4 thread |
| Output | `pi_<N>.txt` (`"3."` + N cifre, nessun newline finale) |
| Verifica | prime 1.000 cifre contro un prefisso noto (attiva di default) |
| Controllo RAM | barriera dura: nessun calcolo oltre il cap, nessun flag di override |

### Uso

```bash
# Compilazione (macOS ARM, Homebrew)
clang++ -std=c++17 -O2 pi_calc.cpp -o pi_calc \
    -I/opt/homebrew/include -L/opt/homebrew/lib -lmpfr -lgmp

./pi_calc                      # 314.159 cifre, 4 thread -> pi_314159.txt
./pi_calc -N 10000000 -t 8     # 10M cifre, 8 thread
./pi_calc -N 1000 --no-verify  # minimo, salta la verifica del prefisso
./pi_calc -h                   # guida
```

| Opzione | Significato |
|---|---|
| `-N <cifre>` | decimali dopo la virgola (default 314159, minimo 1000) |
| `-t <thread>` | thread di calcolo (default 4, al massimo `hardware_concurrency()`) |
| `-o <file>` | file di output (default `pi_<N>.txt`) |
| `--no-verify` | salta la verifica delle prime 1.000 cifre |
| `-h`, `--help` | guida |

### La barriera RAM

Il programma non rischia mai swap pesante né OOM. Prima di ogni allocazione
grande:

- **stima picco** = `N × 53` byte — formula empirica misurata su questa
  macchina con margine del 20% incluso (10M cifre → 0,53 GB stimati,
  100M → 5,3 GB stimati contro 4,4 GB misurati);
- **cap** = `min(20 GB, RAM sistema − 4 GB)` — RAM letta da
  `sysctl hw.memsize`;
- stima e cap sono stampati all'avvio (`stima picco: X,X GB /
  disponibile: Y,Y GB`);
- se la stima supera il cap il programma esce con un errore chiaro (cifre
  richieste, picco stimato, cap effettivo, RAM rilevata, suggerimento di
  ridurre `-N`) **senza calcolare nulla**. Non esiste **nessun** flag
  `--force` / `--max-ram`: la barriera è dura per scelta.

Sulla macchina da 24 GB il cap è 20 GB, che ammette fino a ≈ 345M cifre
(≈ 20–25 min di calcolo). Se la verifica delle 1.000 cifre fallisce il
programma esce con errore e non scrive alcun file.

---

## `pi300k.cpp` — la versione standard

| Proprietà | Valore |
|---|---|
| Linguaggio | C++17 |
| Dipendenze | Boost.Multiprecision, MPFR, GMP, thread C++ |
| Cifre calcolate | 300.014 decimali (dopo la virgola) |
| Termini della serie | 21.157 (~14,18 cifre per termine) |
| Precisione di lavoro | ≈ 997.578 bit (~300.300 cifre decimali) |
| Thread | 4 (chunk contigui, merge sequenziale) |
| Output | `pi300k.txt` |
| Tempo misurato | ≈ 4,9 s (Apple Silicon, clang 21, `-O2`) |

### Cosa fa

1. Calcola π con la formula di Chudnovsky usando il binary splitting
   sull'intervallo di termini `[1, N)`.
2. Divide l'intervallo in **4 chunk contigui** calcolati in parallelo su
   thread separati, poi unisce i risultati parziali in ordine.
3. Formatta il risultato in stringa con `mpfr_sprintf` e lo scrive in
   `pi300k.txt` (`"3."` seguito da 300.014 cifre decimali, senza newline
   finale).
4. **Verifica le prime 1.000 cifre decimali** confrontandole con un prefisso
   noto hard-coded e stampa `Prime 1000 cifre: OK` in caso di successo.

### L'algoritmo

La classica serie derivata da Ramanujan usata per i record di calcolo di π:

```
π = 426880·√10005 / S ,   S = Σ (-1)ᵏ (6k)! (13591409 + 545140134·k) / ((3k)! (k!)³ 640320^(3k))
```

Ogni termine aggiunge circa `log10(640320³) ≈ 14,181647462725477` cifre
decimali, quindi bastano circa 21.155 termini per 300.014 cifre (il codice usa
`N = 21.157`).

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
`O(N · M(n))` invece di `O(N² · M(n))` della somma ingenua. Infine la somma
totale è `S = A + R/Q` e `π = 426880·√10005 / S` (`A = 13591409`).

### Precisione e accuratezza

| Grandezza | Valore | Note |
|---|---|---|
| Termini usati | `N = 21.157` | `⌊300014 / 14,181647462725477⌋ + 2` |
| Errore di troncamento della serie | ≈ 10⁻³⁰⁰⁰⁴¹ | `14,181647462725477 · N`, stampato come `Errore stimato` |
| Precisione di lavoro | ≈ 997.578 bit | `TARGET_DIGITS·log2(10) + 1000` |
| Cifre di guardia | ≈ 300 | bit extra oltre quelli necessari alle 300.014 cifre di output |
| Controllo integrato | prime 1.000 cifre decimali | confrontate con un prefisso noto hard-coded — **superato** |

- L'**errore di troncamento** dopo 21.157 termini (~10⁻³⁰⁰⁰⁴¹) è di decine di
  ordini di grandezza al di sotto dell'ultima cifra stampata.
- L'**errore di arrotondamento** è mantenuto molto al di sotto dell'ultima
  cifra dai ~1.000 bit di guardia extra della precisione MPFR;
  `mpfr_sprintf` esegue l'arrotondamento corretto.
- **Riproducibilità**: rieseguire il programma rigenera `pi300k.txt` identico
  byte per byte al file committato.

### Compilazione ed esecuzione

Requisiti: un compilatore C++17, [Boost.Multiprecision](https://www.boost.org/),
[MPFR](https://www.mpfr.org/) e GMP.

```bash
# macOS (Homebrew)
brew install boost mpfr gmp

# Debian / Ubuntu
sudo apt install libboost-dev libmpfr-dev libgmp-dev

# Compilazione (macOS con Homebrew)
g++ -O2 -std=c++17 pi300k.cpp -o pi300k \
    -I/opt/homebrew/include -L/opt/homebrew/lib -lmpfr -lgmp

# Compilazione (Linux)
g++ -O2 -std=c++17 pi300k.cpp -o pi300k -lmpfr -lgmp

./pi300k
```

Output di esempio (i messaggi a console sono in italiano):

```
Termini: 21157
Prime 1000 cifre: OK
Tempo calcolo: 4.95486 s
Errore stimato: 10^{-300041}
Cifre complete salvate in pi300k.txt
```

### Configurazione

Entrambe le manopole sono costanti in cima a `main()` in `pi300k.cpp`:

- `TARGET_DIGITS = 300014` — numero di decimali scritti in `pi300k.txt`.
  Oltre ~62M di cifre l'aritmetica a 64 bit del caso base in `bs()` andrebbe
  in overflow — vedi Note.
- `NUM_THREADS = 4` — numero di thread; `[1, N)` viene diviso in questo numero
  di chunk contigui.

> In `pi100m.cpp` cifre e thread sono argomenti da riga di comando invece
> che costanti — vedi la sezione sotto.

---

## `pi100m.cpp` — la versione massima

`pi100m.cpp` è la più potente delle due implementazioni. Applica le tecniche
usate dai codici da record per π, seguendo l'architettura di
`gmp-chudnovsky.c`, l'implementazione di riferimento del team GMP (di Hanhong
Xue):

| Tecnica da record | Applicazione qui |
|---|---|
| 1. Binary splitting | albero binario completo dei termini |
| 2. Interi esatti | l'intero albero `P, Q, T` gira su **interi GMP** (`mpz_class`); la virgola mobile MPFR è usata **una sola volta**, per la divisione finale |
| 3. Moltiplicazione FFT | la Schönhage–Strassen di GMP viene selezionata da sola oltre la soglia FFT; nessuna configurazione |
| 4. Radice Newton a precisione dinamica | `sqrt(10005)` con iterazioni di Newton che raddoppiano la precisione a ogni passo (serve anche un passo finale a piena precisione, altrimenti le ultime cifre sono sbagliate) |
| 5. Output decimale veloce | `round(pi·10^D)` come intero esatto, convertito con la `mpz_get_str` divide-and-conquer di GMP; i tempi di matematica e I/O sono stampati separatamente |

### Uso

```bash
# Compilazione (macOS con Homebrew)
g++ -O2 -std=c++17 pi100m.cpp -o pi100m \
    -I/opt/homebrew/include -L/opt/homebrew/lib -lmpfr -lgmp

# Compilazione (Linux)
g++ -O2 -std=c++17 pi100m.cpp -o pi100m -lmpfr -lgmp

./pi100m              # 1.000.000 cifre, tutti i core
./pi100m 100000000 10 # 100.000.000 cifre, 10 core su 12 (~80% CPU)
# facoltativo: nice -n 10 mantiene la macchina reattiva nei run lunghi
```

### Prestazioni misurate

Apple M4 Pro (12 core, 24 GB RAM, clang 21, `-O2`); i due programmi scrivono
le stesse cifre (300k verificate byte per byte, 1M verificate contro un
servizio indipendente di cifre di π con 80/80 match alle posizioni 100k,
500k e 999,9k):

| Cifre | `pi300k.cpp` (4 thread) | `pi100m.cpp` (10 thread) |
|---|---|---|
| 300.014 | ≈ 4,9 s | ≈ 0,25 s |
| 1.000.000 | — | ≈ 1,1 s |
| 100.000.000 | — | ≈ 322 s (~5,4 min), ≈ 4,4 GB di RAM |

### Il risultato massimo (`pi100m.txt`)

`pi100m.txt` contiene **100.000.000 cifre decimali** (`"3."` + 100.000.000
cifre, ≈ 100 MB, git-ignorato), calcolato al ~80% di CPU (10 core su 12) e
**completamente verificato**:

- le prime 300.014 cifre sono **byte-identiche a `pi300k.txt`**;
- le prime 1.000 cifre coincidono col prefisso noto hard-coded;
- la frequenza delle cifre è uniforme entro lo 0,01% su tutti i 100M digit;
- errore di troncamento della serie `10^(-10⁸)`, molto al di sotto
  dell'ultima cifra;
- **ultime cifre verificate**: un secondo calcolo indipendente con 5× il
  margine di guardia (+5000 bit di precisione di lavoro) ha prodotto un file
  **byte-identico** — il risultato non dipende dal margine di arrotondamento,
  quindi tutte le 100.000.000 cifre (ultime incluse) sono arrotondate
  correttamente.

### Come visualizzare `pi100m.txt`

Il file è **una singola riga di ~100 MB** — è normale, ed è per questo che
TextEdit/kitty faticano (provano a renderizzare un'unica riga enorme). Usa il
terminale:

```bash
head -c 100 pi100m.txt          # prime 100 cifre
tail -c 100 pi100m.txt          # ultime 100 cifre
fold -w 100 pi100m.txt | less   # paginazione con righe da 100 cifre
split -b 1M pi100m.txt pi_part_ # dividi in blocchi da 1 MB
```

### Limiti teorici

1. **Troncamento della serie** — l'errore di Chudnovsky dopo N termini è
   `10^(-14,1816·N)`, quindi si possono sempre aggiungere termini: nessun
   tetto da questo lato.
2. **Caso base a 64 bit** — `pi300k.cpp` va in overflow per `a ≳ 4,4·10⁶`
   termini, cioè ≈ 62 milioni di cifre. `pi100m.cpp` elimina questo tetto
   con gli interi GMP.
3. **Memoria** — il picco cresce all'incirca in modo lineare con le cifre:
   ≈ 0,5 GB a 10M, ≈ 4,4 GB a 100M. 314M cifre richiederebbero ≈ 15 GB e
   ≈ 30–45 min; 1 miliardo richiederebbe ≈ 45 GB, oltre i 24 GB disponibili.
4. **Tempo** — scalabilità misurata ≈ n^1,25 (1M → 10M → 31,4M → 100M).

### Il record mondiale

Il record attuale è di **314.000.000.000.000 cifre (≈ 314 mila miliardi)**,
calcolato da **StorageReview** (novembre 2025) con lo *stesso* algoritmo di
Chudnovsky tramite y-cruncher — ma su un server 2U dedicato con 384 core,
1,5 TB di RAM, ~2,4 PB di storage NVMe e ~110 giorni di calcolo.

---

## Pi-visual — il visualizzatore di π

`Pi-visual.cpp` (+ header in `visual/`) è una finestra SDL2 + OpenGL 3.3
core divisa in due viewport, guidati da un solo clock `t` e da una sola
rate `rate(t)` monotona crescente (esponenziale che satura a t ≈ 120 s),
così i due pannelli accelerano insieme e l'animazione significativa dura
~2 minuti. Poi il programma resta vivo con l'animazione stabilizzata e l'HUD.

- **sinistro — pendolo composto** `Z(θ) = e^(iθ) + e^(iπθ)`, con π hardcoded
  a 17 decimali. La curva cresce al consumo delle cifre; una coreografia di
  camera scritta a mano lavora a **eventi**: in corrispondenza delle due
  famose quasi-chiusure di π — 14π (gap ≈ 0,056) e 226π (gap ≈ 0,0002,
  sottopixel), gli unici punti in cui la curva torna a sfiorare il suo
  punto di nascita (2,0) — la vista fa dolly-in e il tempo rallenta
  (**slow motion**) per far vedere il quasi-contatto e il cambio colore
  della curva; fuori dagli eventi, vista ampia e velocità piena. La curva
  cambia colore a ogni svolta.
- **destro — chord diagram** delle cifre di π lette da `pi100m.txt` via
  `mmap` (mai copiate in memoria): 10 settori colorati (0–9) con le cifre
  grandi attorno all'anello, una corda **curva** (Bézier quadratica) per
  ogni coppia di cifre consecutive con ancoraggi distribuiti lungo l'arco
  di ogni settore. La ragnatela **cresce da zero**: la matrice statica è
  alimentata dallo streaming e ricontestualizzata ogni 512 corde; le corde
  recenti brillano in primo piano (fade quadratico di 30 s, blending
  additivo) poi si fondono nella ragnatela. Rate a due stadi: rampa
  lineare lenta (60 cifre/s) fino a 100k cifre, poi accelerazione
  esponenziale fino alla saturazione.

### Compilazione ed esecuzione

```bash
make           # compila Pi-visual (vedi il Makefile o il commento in testa)
./Pi-visual    # legge pi100m.txt nella directory corrente
```

### Tasti

| Tasto | Azione |
|---|---|
| `0`–`3` | palette: 0 provvisoria, 1 Botanica, 2 Alba, 3 Notturna |
| `S` | screenshot PNG in `pictures/` (fallback: `$HOME`) |
| `R` | reset del clock e riavvio dall'inizio del file π |
| `F` | fullscreen on/off |
| `Esc` | esci |

### Opzioni e limiti

- `--pi <file>` legge un file di cifre diverso (formato: `3.` + cifre, senza
  newline finale — es. `pi300k.txt` o qualsiasi `pi_<N>.txt` prodotto da
  `pi_calc`). Il file viene verificato contro il prefisso noto di 100 cifre
  all'avvio; in caso di discordanza il programma esce con errore.
- Richiede macOS ARM + Homebrew (`sdl2`, `sdl2_ttf`; zlib è di sistema),
  OpenGL 3.3 core e il font di sistema
  `/System/Library/Fonts/Helvetica.ttc` (nessun asset esterno).
- Il file mmap di cifre non pesa su nessun budget RAM: la memoria residente
  resta ~100 MB anche con il file da 100M cifre.
- Con `PI_VISUAL_CHECK=1` l'app esegue a t = 20 s una diagnostica automatica
  del framebuffer (centratura/rotondità dell'anello, varietà di colore delle
  corde, visibilità del pendolo, cifre grandi) e salva PNG di controllo in
  `pictures/`. Con `PI_VISUAL_LABEL_COLOR=1` le etichette vengono inoltre
  disegnate in magenta puro perché il check possa contarne i pixel.
- Con `PI_VISUAL_CHECK=1` l'app esegue a t = 20 s una diagnostica automatica
  del framebuffer (centratura/rotondità dell'anello, varietà di colore delle
  corde, visibilità del pendolo, cifre grandi) e salva PNG di controllo in
  `pictures/`. Con `PI_VISUAL_LABEL_COLOR=1` le etichette vengono inoltre
  disegnate in magenta puro perché il check possa contarne i pixel.

## Struttura del progetto

```
pi_calc.cpp       il calcolatore unificato: CLI, barriera RAM, verifica integrata
Pi-visual.cpp     il visualizzatore di π: pendolo + chord diagram, due viewport
visual/           i suoi header: pi_reader, chord, pendulum, hud, camera, screenshot
Makefile          build minimale per Pi-visual (all / run / clean)
memory.md         diario di sessione (ambiente, decisioni, step)
pi100m.cpp        la versione massima (riferimento): albero mpz esatto, Newton, conversione veloce
pi100m.txt        il suo output: "3." + 100.000.000 cifre (git-ignorato, ~100 MB)
pi300k.cpp        la versione standard (riferimento, albero mpfr, 300.014 cifre)
pi300k.txt        il suo output: "3." + 300.014 cifre decimali (300.016 byte)
.gitignore        ignora artefatti di build, binari, output pi_*.txt e pictures/
```

## Note e osservazioni

- **Formato file**: nessuno dei due output ha newline finale; entrambi iniziano
  con `3.1415926535...`.
- **Nomi**: i file sono nominati in base alla dimensione (`pi300k` = 300.014
  cifre, `pi100m` = 100.000.000 cifre) così i file correlati condividono il
  prefisso e restano allineati nel terminale.

## Licenza

Nel repository non è presente alcun file di licenza.
