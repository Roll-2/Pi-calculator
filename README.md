# Chudnovsky π Calculator & Visualizer (`pi_calc` · `Pi-visual`)

> 🌐 **Language / Lingua:** [🇬🇧 English](#english) · [🇮🇹 Italiano](#italiano)

---

# English

A multi-threaded C++17 program that computes **π** using the **Chudnovsky
series** with **binary splitting**, evaluated with arbitrary-precision
arithmetic from **GMP/MPFR** (wrapped by **Boost.Multiprecision**), plus an
SDL2/OpenGL **visualizer**:

| Program | Digits | Result file |
|---|---|---|
| `pi_calc.cpp` | configurable via `-N` (default 314,159) | `pi_<N>.txt` |

The digit files produced by the two earlier standalone programs are kept as
data: `pi300k.txt` (300,014 digits) and `pi100m.txt` (100,000,000 digits,
~100 MB, git-ignored). The sources `pi300k.cpp` and `pi100m.cpp` have been
removed — `pi_calc.cpp` replaces them (same algorithm, same file format).

---

## `pi_calc.cpp` — the unified calculator

`pi_calc.cpp` is the only calculator: it replaces the two earlier standalone
programs (`pi300k.cpp` and `pi100m.cpp`, removed from the repository) and
keeps the more robust architecture of the two (exact `mpz_class` binary
splitting, dynamic-precision Newton sqrt, fast `mpz_get_str` conversion).

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

## The algorithm

`pi_calc.cpp` computes π with the classic Ramanujan-derived series used for
record-breaking computations:

The classic Ramanujan-derived series used for record-breaking π computations:

```
π = 426880·√10005 / S ,   S = Σ (-1)ᵏ (6k)! (13591409 + 545140134·k) / ((3k)! (k!)³ 640320^(3k))
```

Each term adds approximately `log10(640320³) ≈ 14.181647462725477` decimal
digits: ~21,155 terms for 300,014 digits, ~7.05M terms for 100,000,000.

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

The implementation follows the world-record techniques of the GMP reference
`gmp-chudnovsky.c`: the whole `P, Q, R` tree runs on GMP integers
(`mpz_class`) with MPFR used once for the final division, `sqrt(10005)` via
dynamic-precision Newton iterations, decimal output via GMP's
divide-and-conquer `mpz_get_str`, and parallelism over contiguous term
chunks.

### Viewing the digit files

Each output file is a **single line of `3.` + digits** — that is why
editors struggle with `pi100m.txt` (they try to render one huge line).
Use the terminal instead:

```bash
head -c 100 pi100m.txt          # first 100 digits
tail -c 100 pi100m.txt          # last 100 digits
fold -w 100 pi100m.txt | less   # paginate wrapped in lines of 100 digits
split -b 1M pi100m.txt pi_part_ # split into 1 MB chunks
```

---

## Pi-visual — the π visualizer

`Pi-visual.cpp` (+ headers in `visual/`) is an SDL2 + OpenGL 3.3 core window
split into two viewports, driven by one clock `t` and one monotonically
increasing rate `rate(t)`, so both panels accelerate together; a global
slow-motion kicks in at every pendulum close-up and the significant
animation lasts ~4 minutes.

- **left — compound pendulum** `Z(θ) = e^(iθ) + e^(iπθ)`, with π hard-coded
  to 17 decimals. The camera works with **events** at the three near-closures
  of π — 14π, 226π and 66204π, where the curve grazes its birth point (2,0)
  at distance `miss` = δ*² ≈ 1.8e-4 / 2.1e-9 / 8.4e-10: at each event the
  view zooms in logarithmically (zoom = 0.72/miss, up to ~8.6e8×) and time
  **slows down** (global slow-motion shared with the chord panel), so the
  close-up shows two clearly separated near-vertical lines — the returning
  curve vs the birth branch, ~220 px apart; they never touch. The curve
  changes colour **only at a near-contact** (four HSL tints), not per period,
  so the eye can count the close calls.
- **right — chord diagram** of the digits of π read from the digits file
  (default `pi_314159.txt`) via `mmap` (never copied into memory): 10 colored sectors (0–9) with large
  digit labels around the ring, one **curved** chord (quadratic Bézier) per
  consecutive digit pair with anchor points spread along each sector arc.
  The web **grows from zero**: the static matrix is fed by the live stream
  and rebuilt every 512 chords; recent chords shine in the foreground
  (quadratic 30 s fade, standard alpha blending — colours never saturate
  toward white) and each new chord is fainter than the previous one (alpha
  decays with the chord index, with a floor), so the web stays colourful
  while only the newest chords stand out. Rate: gentle but not sluggish
  start at 2.5 s per digit (~32 digits in the first 80 s), then
  exponential acceleration (≈100 digits/s at 500; all 314,159 digits
  consumed by t ≈ 153 s).

### Build & run

```bash
make           # builds Pi-visual (see the Makefile or the header comment)
./Pi-visual    # reads pi_314159.txt in the current directory (default)
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

- `--pi <file>` reads a different digits file (default `pi_314159.txt`;
  format: `3.` + digits, no trailing newline — e.g. `pi300k.txt` or any
  `pi_<N>.txt` produced by `pi_calc`). The file is verified against the
  known 100-digit prefix at startup; on mismatch the program exits with an
  error.
- Requires macOS ARM + Homebrew (`sdl2`, `sdl2_ttf`; zlib ships with the
  OS), OpenGL 3.3 core, and the system font
  `/System/Library/Fonts/Helvetica.ttc` (no external assets).
- The mmap'd digits file does not count toward any RAM budget: resident
  memory stays ~100 MB even with the 100M-digit file.
- With `PI_VISUAL_CHECK=1` the app runs a three-phase automatic
  framebuffer diagnostic (ring centering + pendulum visibility at t = 20 s,
  chord colour variety at ~400 digits, then a control screenshot at each of
  the three pendulum close-ups) and saves control PNGs into `pictures/`.
  `PI_VISUAL_LABEL_COLOR=1` additionally draws the labels in pure magenta
  so the check can count their pixels.

## Project layout

```
pi_calc.cpp       the unified calculator: CLI, RAM barrier, built-in verification
Pi-visual.cpp     the π visualizer: pendulum + chord diagram, two viewports
visual/           its headers: pi_reader, chord, pendulum, hud, camera, screenshot
Makefile          minimal build for Pi-visual (all / run / clean)
memory.md         session log (environment, decisions, steps)
pi100m.txt        100,000,000 digits kept as data (git-ignored, ~100 MB)
pi300k.txt        300,014 digits kept as data (300,016 bytes)
.gitignore        ignores build artifacts, binaries, pi_*.txt outputs and pictures/
```

## Notes & observations

- **File format**: no output file has a trailing newline; they all start
  with `3.1415926535...`.
- **Kept data files**: `pi300k.txt` and `pi100m.txt` are named after their
  size (300,014 and 100,000,000 digits) and remain on disk even though the
  programs that produced them were removed.

## License

No license file is present in the repository.

---

# Italiano

Un programma C++17 multi-thread che calcola **π** usando la **serie di
Chudnovsky** con **binary splitting**, valutata con aritmetica a precisione
arbitraria di **GMP/MPFR** (incapsulata da **Boost.Multiprecision**), più un
**visualizzatore** SDL2/OpenGL:

| Programma | Cifre | File risultato |
|---|---|---|
| `pi_calc.cpp` | configurabile con `-N` (default 314.159) | `pi_<N>.txt` |

I file di cifre prodotti dai due programmi precedenti restano come dati:
`pi300k.txt` (300.014 cifre) e `pi100m.txt` (100.000.000 cifre, ~100 MB,
git-ignorato). I sorgenti `pi300k.cpp` e `pi100m.cpp` sono stati rimossi —
`pi_calc.cpp` li sostituisce (stesso algoritmo, stesso formato).

---

## `pi_calc.cpp` — il calcolatore unificato

`pi_calc.cpp` è l'unico calcolatore: sostituisce i due programmi
precedenti (`pi300k.cpp` e `pi100m.cpp`, rimossi dal repository) e
mantiene l'architettura più solida dei due (binary splitting esatto su
`mpz_class`, sqrt con Newton a precisione dinamica, conversione veloce con
`mpz_get_str`).

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

## L'algoritmo

`pi_calc.cpp` calcola π con la classica serie derivata da Ramanujan usata
nei record di calcolo:

La classica serie derivata da Ramanujan usata per i record di calcolo di π:

```
π = 426880·√10005 / S ,   S = Σ (-1)ᵏ (6k)! (13591409 + 545140134·k) / ((3k)! (k!)³ 640320^(3k))
```

Ogni termine aggiunge circa `log10(640320³) ≈ 14,181647462725477` cifre
decimali: ~21.155 termini per 300.014 cifre, ~7,05M termini per 100.000.000.

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

L'implementazione segue le tecniche da record dell'implementazione di
riferimento GMP `gmp-chudnovsky.c`: l'intero albero `P, Q, R` gira su interi
GMP (`mpz_class`) con MPFR usato una sola volta per la divisione finale,
`sqrt(10005)` con Newton a precisione dinamica, output decimale con la
`mpz_get_str` divide-and-conquer di GMP e parallelismo su chunk contigui di
termini.

### Come visualizzare i file di cifre

Ogni file di output è **una singola riga di `3.` + cifre** — ed è per questo
che gli editor faticano con `pi100m.txt` (provano a renderizzare un'unica
riga enorme). Usa il terminale:

```bash
head -c 100 pi100m.txt          # prime 100 cifre
tail -c 100 pi100m.txt          # ultime 100 cifre
fold -w 100 pi100m.txt | less   # paginazione con righe da 100 cifre
split -b 1M pi100m.txt pi_part_ # dividi in blocchi da 1 MB
```

---

## Pi-visual — il visualizzatore di π

`Pi-visual.cpp` (+ header in `visual/`) è una finestra SDL2 + OpenGL 3.3
core divisa in due viewport, guidati da un solo clock `t` e da una sola
rate `rate(t)` monotona crescente, così i due pannelli accelerano insieme;
una slow motion globale scatta a ogni close-up del pendolo e
l'animazione significativa dura ~4 minuti.

- **sinistro — pendolo composto** `Z(θ) = e^(iθ) + e^(iπθ)`, con π hardcoded
  a 17 decimali. La curva cresce al consumo delle cifre; una coreografia di
  camera scritta a mano lavora a **eventi**: alle tre quasi-chiusure di π —
  14π, 226π e 66204π, dove la curva sfiora il suo punto di nascita (2,0) a
  distanza `miss` = δ*² ≈ 1,8e-4 / 2,1e-9 / 8,4e-10 — la vista zooma in modo
  logaritmico (zoom = 0,72/miss, fino a ~8,6e8×) e il tempo rallenta (**slow
  motion globale**, condivisa col chord): il close-up mostra due linee
  quasi-verticali ben separate — la curva di ritorno e il ramo di nascita,
  ~220 px di distanza — che non si toccano mai. La curva cambia colore
  **solo al quasi-contatto** (quattro tinte HSL), non a ogni periodo, così
  l'occhio conta i passaggi ravvicinati.
- **destro — chord diagram** delle cifre di π lette dal file di cifre
  (default `pi_314159.txt`) via `mmap` (mai copiate in memoria): 10 settori colorati (0–9) con le cifre
  grandi attorno all'anello, una corda **curva** (Bézier quadratica) per
  ogni coppia di cifre consecutive con ancoraggi distribuiti lungo l'arco
  di ogni settore.  La ragnatela **cresce da zero**: la matrice statica è alimentata dallo
  streaming e ricontestualizzata ogni 512 corde; le corde recenti brillano
  in primo piano (fade quadratico di 30 s, blending alpha standard — i
  colori non schizzano mai verso il bianco) e ogni corda nuova è più
  flebile della precedente (alpha decrescente con indice, con floor), così
  la ragnatela resta colorata e solo le ultime spiccano. Rate: partenza
  delicata ma non rallentata (2,5 s a cifra, ~32 cifre nei primi 80 s),
  poi accelerazione esponenziale (~100 cifre/s a 500; file da 314.159
  cifre esaurito a t ≈ 153 s).

### Compilazione ed esecuzione

```bash
make           # compila Pi-visual (vedi il Makefile o il commento in testa)
./Pi-visual    # legge pi_314159.txt nella directory corrente (default)
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

- `--pi <file>` legge un file di cifre diverso (default `pi_314159.txt`;
  formato: `3.` + cifre, senza newline finale — es. `pi300k.txt` o
  qualsiasi `pi_<N>.txt` prodotto da `pi_calc`). Il file viene verificato
  contro il prefisso noto di 100 cifre all'avvio; in caso di discordanza il
  programma esce con errore.
- Richiede macOS ARM + Homebrew (`sdl2`, `sdl2_ttf`; zlib è di sistema),
  OpenGL 3.3 core e il font di sistema
  `/System/Library/Fonts/Helvetica.ttc` (nessun asset esterno).
- Il file mmap di cifre non pesa su nessun budget RAM: la memoria residente
  resta ~100 MB anche con il file da 100M cifre.
- Con `PI_VISUAL_CHECK=1` l'app esegue una diagnostica automatica del
  framebuffer a tre fasi (centratura anello + visibilità del pendolo a
  t = 20 s, varietà di colore delle corde a ~400 cifre, poi uno screenshot
  di controllo a ogni uno dei tre close-up del pendolo) e salva PNG di
  controllo in `pictures/`. Con `PI_VISUAL_LABEL_COLOR=1` le etichette
  vengono inoltre disegnate in magenta puro perché il check possa contarne
  i pixel.

## Struttura del progetto

```
pi_calc.cpp       il calcolatore unificato: CLI, barriera RAM, verifica integrata
Pi-visual.cpp     il visualizzatore di π: pendolo + chord diagram, due viewport
visual/           i suoi header: pi_reader, chord, pendulum, hud, camera, screenshot
Makefile          build minimale per Pi-visual (all / run / clean)
memory.md         diario di sessione (ambiente, decisioni, step)
pi100m.txt        100.000.000 cifre conservate come dati (git-ignorato, ~100 MB)
pi300k.txt        300.014 cifre conservate come dati (300.016 byte)
.gitignore        ignora artefatti di build, binari, output pi_*.txt e pictures/
```

## Note e osservazioni

- **Formato file**: nessun file di output ha newline finale; tutti iniziano
  con `3.1415926535...`.
- **File dati conservati**: `pi300k.txt` e `pi100m.txt` sono nominati in
  base alla dimensione (300.014 e 100.000.000 cifre) e restano su disco
  anche se i programmi che li producevano sono stati rimossi.

## Licenza

Nel repository non è presente alcun file di licenza.
