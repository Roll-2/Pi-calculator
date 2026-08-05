// ============================================================
// pi100m.cpp  —  "la versione massima"
//
// Implementazione stile gmp-chudnovsky.c (il riferimento ufficiale
// del team GMP, di Hanhong Xue): stesso algoritmo di Chudnovsky con
// binary splitting, ma con l'architettura usata dai codici da record.
//
// Le 5 tecniche dei record, e come sono applicate qui:
//
//   1. BINARY SPLITTING puro           — l'albero P, Q, T e' calcolato
//      con INTERI ESATTI (mpz_class), senza mai toccare i float durante
//      il calcolo. Una sola divisione in virgola mobile a fine lavoro.
//      Complessita' O(n log^3 n) invece di O(n^2) della somma ingenua.
//
//   2. INTERI GMP invece di float      — niente mpf/mpfr nel tree:
//      addizioni e moltiplicazioni intere sono molto piu' veloci e,
//      soprattutto, ESATTE (zero errori di arrotondamento intermedi).
//
//   3. FFT (Schonhage-Strassen)        — GMP lo seleziona in automatico
//      per operandi grandi (soglia FFT interna); MPFR usa lo stesso
//      strato mpn di GMP. Nessun intervento richiesto.
//
//   4. RADICE QUADRATA con Newton      — sqrt(10005) calcolata con
//      iterazione Newton-Raphson a precisione DINAMICA: si parte da
//      ~64 bit e si raddoppia a ogni passo, toccando la precisione
//      piena solo nell'ultima iterazione (~2x il costo di una radice
//      singola a piena precisione).
//
//   5. CONVERSIONE DECIMALE veloce     — invece di mpfr_sprintf (lento
//      a grandi dimensioni), si calcola pi*10^D come intero esatto e lo
//      si converte con mpz_get_str, che in GMP usa la conversione
//      divide-and-conquer. I tempi di matematica e di I/O sono
//      misurati separatamente.
//
// Uso:  ./pi100m [cifre >= 1000] [thread]
// Default: 1.000.000 cifre, tutti i core. Output: pi100m.txt
//
// Riferimenti: https://gmplib.org/pi-with-gmp  (gmp-chudnovsky.c)
// ============================================================
#define GL_SILENCE_DEPRECATION
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <cmath>
#include <fstream>
#include <string>
#include <cstdlib>
#include <algorithm>
#include <gmpxx.h>
#include <mpfr.h>
#include <boost/multiprecision/mpfr.hpp>

using namespace boost::multiprecision;
using mpfr_float = number<mpfr_float_backend<0>>;

// Costanti di Chudnovsky
constexpr long long A = 13591409;
constexpr long long B = 545140134;
// Versioni come interi esatti GMP
const mpz_class A_Z(13591409);
const mpz_class B_Z(545140134);
const mpz_class C3_OVER_24_Z("10939058860032000"); // 640320^3 / 24

// Termini del binary splitting: tre interi esatti (stile gmp-chudnovsky)
struct Triple {
    mpz_class P, Q, T;
};

// Calcolo ricorsivo per l'intervallo [a, b) (con a >= 1), interi esatti.
// T = termine con segno; a fine albero S = A + T/Q (A = termine k=0).
void bs(unsigned long a, unsigned long b, Triple& out) {
    if (b - a == 1) {
        mpz_class pa(a);
        // P(a) = (6a-5)(2a-1)(6a-1)  — prodotto esatto, nessun overflow
        out.P = (6 * pa - 5) * (2 * pa - 1) * (6 * pa - 1);
        // Q(a) = a^3 * (640320^3 / 24)
        out.Q = pa * pa * pa * C3_OVER_24_Z;
        // T(a) = (-1)^a * (A + B*a) * P(a)
        out.T = (A_Z + B_Z * pa) * out.P;
        if (a % 2 == 1) out.T = -out.T;   // (-1)^a per a dispari
        return;
    }
    unsigned long m = (a + b) / 2;
    Triple left, right;
    bs(a, m, left);
    bs(m, b, right);
    out.P = left.P * right.P;
    out.Q = left.Q * right.Q;
    // T = T_left * Q_right + P_left * T_right
    out.T = left.T * right.Q + left.P * right.T;
}

// sqrt(10005) con Newton-Raphson a precisione dinamica (tecnica 4).
// Ogni iterazione raddoppia la precisione; Newton converge quadraticamente
// (le cifre corrette raddoppiano a ogni passo: ~53·2^k bit dopo k passi).
// Serve la catena FINO A COPIRE la precisione finale (passi a 128, 256, ...,
// fino a >= final_prec) PIU' un ultimo passo a piena precisione: con la
// sola catena si otterrebbero ~0.83·prec bit corretti, insufficienti; il
// passo finale li eleva al quadrato, ben oltre la precisione di lavoro.
mpfr_float sqrt10005_newton(unsigned long final_prec) {
    mpfr_float guess = 100.024996875781031;  // sqrt(10005) come double (~53 bit)
    unsigned long p = 64;                    // precisione iniziale (bit)
    while (p < final_prec) {
        p = std::min(final_prec, p * 2);     // raddoppia, clamp finale
        guess.precision(p);
        mpfr_float x(10005);
        x.precision(p);
        guess = (guess + x / guess) / 2;     // passo di Newton
    }
    // Passo FINALE a piena precisione: raddoppia ancora le cifre corrette.
    guess.precision(final_prec);
    mpfr_float xf(10005);
    xf.precision(final_prec);
    guess = (guess + xf / guess) / 2;
    return guess;
}

int main(int argc, char* argv[]) {
    // ------------------------- Configurazione -------------------------
    long TARGET_DIGITS = 1000000;                       // default: 1.000.000 cifre
    int NUM_THREADS = std::max(1u, std::thread::hardware_concurrency());

    if (argc > 1) {
        long d = std::atol(argv[1]);
        if (d >= 1000) {
            TARGET_DIGITS = d;
        } else {
            std::cerr << "Uso: " << argv[0] << " [cifre >= 1000] [thread]" << std::endl;
            return 1;
        }
    }
    if (argc > 2) {
        int t = std::atoi(argv[2]);
        if (t >= 1) NUM_THREADS = t;
    }

    // Precisione di lavoro in bit (~3.322 bit per cifra decimale + margine)
    unsigned long prec = static_cast<unsigned long>(TARGET_DIGITS * std::log2(10.0)) + 1000;
    mpfr_float::default_precision(prec);

    // Numero di termini: ogni termine aggiunge ~14.181647 cifre
    const double digits_per_term = 14.181647462725477;
    unsigned long N = static_cast<unsigned long>(TARGET_DIGITS / digits_per_term) + 2;
    std::cout << "Termini: " << N << std::endl;

    // Il numero di thread non puo' superare i termini disponibili
    // (altrimenti si creerebbero intervalli vuoti e ricorsione infinita).
    if (NUM_THREADS > static_cast<int>(N - 1)) {
        NUM_THREADS = static_cast<int>(N - 1);
    }

    if (TARGET_DIGITS > 50000000) {
        std::cout << "Attenzione: oltre ~50 milioni di cifre servono molti GB di RAM "
                     "e il calcolo puo' durare ore." << std::endl;
    }

    // ============ 1. BINARY SPLITTING INTERO (parallelo) ============
    std::vector<unsigned long> starts, ends;
    unsigned long chunk = (N - 1) / NUM_THREADS;
    unsigned long cur = 1;
    for (int i = 0; i < NUM_THREADS; ++i) {
        starts.push_back(cur);
        unsigned long next = (i == NUM_THREADS - 1) ? N : cur + chunk;
        ends.push_back(next);
        cur = next;
    }

    std::vector<Triple> results(NUM_THREADS);
    std::vector<std::thread> threads;

    auto t0 = std::chrono::steady_clock::now();

    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([i, &starts, &ends, &results]() {
            bs(starts[i], ends[i], results[i]);
        });
    }
    for (auto& t : threads) t.join();

    // Combinazione in ordine (identico al binary splitting completo)
    Triple total = results[0];
    for (int i = 1; i < NUM_THREADS; ++i) {
        Triple tmp;
        tmp.P = total.P * results[i].P;
        tmp.Q = total.Q * results[i].Q;
        tmp.T = total.T * results[i].Q + total.P * results[i].T;
        total = tmp;
    }

    auto t1 = std::chrono::steady_clock::now();

    // ============ 2-4. RADICE (Newton dinamico) + DIVISIONE FINALE ============
    // pi = 426880 * sqrt(10005) * Q / (A*Q + T)
    mpfr_float sqrtC = sqrt10005_newton(prec);

    mpfr_float Qm, Tm, Am;   // precisione = default (prec)
    mpfr_set_z(Qm.backend().data(), total.Q.get_mpz_t(), MPFR_RNDN);
    mpfr_set_z(Tm.backend().data(), total.T.get_mpz_t(), MPFR_RNDN);
    mpfr_set_ui(Am.backend().data(), static_cast<unsigned long>(A), MPFR_RNDN);

    mpfr_float num = 426880 * sqrtC * Qm;
    mpfr_float den = Am * Qm + Tm;
    mpfr_float pi = num / den;

    auto t2 = std::chrono::steady_clock::now();

    // ============ 5. CONVERSIONE DECIMALE VELOCE (intera) ============
    // pi10 = round(pi * 10^D) come intero esatto, poi mpz_get_str
    // (divide-and-conquer interna di GMP, molto piu' veloce di sprintf).
    // Nota: TARGET_DIGITS e' gia' il numero di decimali DOPO la virgola
    // (pi.txt = "3." + 300.014 cifre), quindi D = TARGET_DIGITS.
    unsigned long D = static_cast<unsigned long>(TARGET_DIGITS); // decimali
    mpz_class ten_d;
    mpz_ui_pow_ui(ten_d.get_mpz_t(), 10, D);

    mpfr_float p10;   // precisione = default (prec)
    mpfr_mul_z(p10.backend().data(), pi.backend().data(), ten_d.get_mpz_t(), MPFR_RNDN);

    mpz_class pi10;
    mpfr_get_z(pi10.get_mpz_t(), p10.backend().data(), MPFR_RNDN);
    std::string s = pi10.get_str();   // "3" + D cifre

    // Caso (impossibile per pi: servirebbero D+1 nove consecutivi) di
    // riporto: se pi*10^D arrotondasse esattamente a 10^D, la stringa
    // avrebbe D+2 caratteri e la cifra giusta sarebbe "4.000...0".
    std::string pi_str;
    if (s.length() == D + 2) {
        pi_str = "4." + std::string(D, '0');
    } else {
        pi_str = "3." + s.substr(1, D);            // s[0] == '3'
    }

    auto t3 = std::chrono::steady_clock::now();

    std::chrono::duration<double> split_t = t1 - t0;
    std::chrono::duration<double> sqrt_div_t = t2 - t1;
    std::chrono::duration<double> conv_t = t3 - t2;
    std::chrono::duration<double> total_t = t3 - t0;

    // ------------------------- Verifica e output -------------------------
    const std::string known_prefix =
        "3.1415926535897932384626433832795028841971693993751058209749445923078164062862089986280348253421170679"
        "8214808651328230664709384460955058223172535940812848111745028410270193852110555964462294895493038196"
        "4428810975665933446128475648233786783165271201909145648566923460348610454326648213393607260249141273"
        "7245870066063155881748815209209628292540917153643678925903600113305305488204665213841469519415116094"
        "3305727036575959195309218611738193261179310511854807446237996274956735188575272489122793818301194912"
        "9833673362440656643086021394946395224737190702179860943702770539217176293176752384674818467669405132"
        "0005681271452635608277857713427577896091736371787214684409012249534301465495853710507922796892589235"
        "4201995611212902196086403441815981362977477130996051870721134999999837297804995105973173281609631859"
        "5024459455346908302642522308253344685035261931188171010003137838752886587533208381420617177669147303"
        "5982534904287554687311595628638823537875937519577818577805321712268066130019278766111959092164201989";

    bool ok = (pi_str.substr(0, known_prefix.length()) == known_prefix);
    std::cout << "Prime 1000 cifre: " << (ok ? "OK" : "ERRORE!") << std::endl;

    // Tempi separati: matematica vs conversione (tecnica 5: isolare l'I/O)
    std::cout << "Binary splitting:    " << split_t.count() << " s" << std::endl;
    std::cout << "Radice + divisione:  " << sqrt_div_t.count() << " s" << std::endl;
    std::cout << "Conversione decimale:" << conv_t.count() << " s" << std::endl;
    std::cout << "Tempo totale:        " << total_t.count() << " s" << std::endl;

    double err_exp = digits_per_term * N;
    std::cout << "Errore stimato: 10^{-" << err_exp << "}" << std::endl;

    std::ofstream fout("pi100m.txt");
    fout << pi_str;
    fout.close();
    std::cout << "Cifre complete salvate in pi100m.txt" << std::endl;

    return 0;
}
