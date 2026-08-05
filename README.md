# Chudnovsky π Calculator — 300,014 digits (`3m14`)

> 🇬🇧 English · [🇮🇹 Italiano](README.it.md)

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
