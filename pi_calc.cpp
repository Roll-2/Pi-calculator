// ============================================================
// pi_calc.cpp — calcolatore di π unificato (sostituisce i vecchi pi300k e
// pi100m, rimossi dal repository)
//
// Architettura (le 5 tecniche "da record" di gmp-chudnovsky.c):
// Chudnovsky con binary splitting su interi esatti mpz_class, sqrt(10005)
// con Newton a precisione dinamica, conversione decimale con mpz_get_str,
// parallelismo su chunk contigui di termini.
// (Il vecchio pi300k usava mpfr_float nell'albero ed andava in overflow a
// ~62M cifre col caso base a 64 bit: da qui la scelta dell'albero mpz.)
//
// CLI:
//   ./pi_calc [-N cifre] [-t thread] [-o file] [--no-verify] [-h]
//     -N <cifre>    decimali dopo la virgola (default 314159, minimo 1000)
//     -t <thread>   thread di calcolo (default 4, massimo hardware_concurrency)
//     -o <file>     file di output (default pi_<N>.txt)
//     --no-verify   salta la verifica delle prime 1000 cifre
//     -h, --help    questa guida
//
// Controllo RAM (barriera dura, NESSUN flag di override):
//   stima picco = N x 53 byte (margine del 20% incluso; formula empirica
//   misurata su questa macchina: 100M cifre -> 4,4 GB reali -> 5,3 GB stimati).
//   cap = min(20 GB, RAM_sistema - 4 GB). Se la stima supera il cap il
//   programma esce con errore PRIMA di qualsiasi allocazione grande.
//
// Output: "3." + N cifre decimali, nessun newline finale, nessuna intestazione
// (stesso formato di pi100m.txt / pi300k.txt).
//
// Compilazione (macOS ARM, Homebrew):
//   clang++ -std=c++17 -O2 pi_calc.cpp -o pi_calc \
//     -I/opt/homebrew/include -L/opt/homebrew/lib -lmpfr -lgmp
//
// Esempi:
//   ./pi_calc                          # 314.159 cifre, 4 thread -> pi_314159.txt
//   ./pi_calc -N 10000000 -t 8         # 10M cifre, 8 thread
//   ./pi_calc -N 1000000000            # rifiutato dal controllo RAM
// ============================================================
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <sys/sysctl.h>

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

// sqrt(10005) con Newton-Raphson a precisione dinamica.
// Ogni iterazione raddoppia la precisione; Newton converge quadraticamente.
// Serve la catena FINO A COPIRE la precisione finale PIU' un ultimo passo a
// piena precisione: con la sola catena si otterrebbero ~0.83·prec bit
// corretti, insufficienti; il passo finale li eleva al quadrato.
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

// ---------------------- Controllo RAM (barriera dura) ----------------------
// RAM di sistema su macOS ARM. Fallback 0.0 => il chiamante esce con errore:
// il controllo è obbligatorio, non si prosegue senza sapere quanta RAM c'è.
static double system_ram_gb() {
    uint64_t memsize = 0;
    size_t size = sizeof(memsize);
    if (sysctlbyname("hw.memsize", &memsize, &size, nullptr, 0) != 0) return 0.0;
    return (double)memsize / (1024.0 * 1024.0 * 1024.0);
}

// Formula empirica misurata su questa macchina, margine del 20% incluso:
// 100M cifre -> 4,4 GB reali -> 5,3 GB stimati. (10M cifre -> 0,53 GB.)
static constexpr double BYTES_PER_DIGIT = 53.0;
static constexpr double HARD_CAP_GB   = 20.0;  // limite hard, mai superabile
static constexpr double OS_MARGIN_GB  = 4.0;   // margine per il sistema operativo

static void print_usage(std::FILE* out, const char* prog) {
    std::fprintf(out,
        "Uso: %s [opzioni]\n"
        "  -N <cifre>    numero di cifre decimali dopo la virgola (default 314159, min 1000)\n"
        "  -t <thread>   numero di thread (default 4, massimo hardware_concurrency)\n"
        "  -o <file>     file di output (default pi_<N>.txt)\n"
        "  --no-verify   salta la verifica delle prime 1000 cifre\n"
        "  -h, --help    mostra questa guida\n",
        prog);
}

int main(int argc, char* argv[]) {
    // ------------------------- Configurazione / CLI -------------------------
    long long TARGET_DIGITS = 314159;   // default: 314.159 cifre decimali
    int NUM_THREADS = 4;                // default: 4 thread
    std::string out_file;               // vuoto => pi_<N>.txt
    bool verify = true;                 // --no-verify la disattiva

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next_value = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "Errore: l'opzione %s richiede un valore.\n", name);
                std::exit(1);
            }
            return argv[++i];
        };
        if (arg == "-h" || arg == "--help") {
            print_usage(stdout, argv[0]);
            return 0;
        } else if (arg == "-N") {
            TARGET_DIGITS = std::atoll(next_value("-N"));
        } else if (arg == "-t") {
            NUM_THREADS = std::atoi(next_value("-t"));
        } else if (arg == "-o") {
            out_file = next_value("-o");
        } else if (arg == "--no-verify") {
            verify = false;
        } else {
            std::fprintf(stderr, "Errore: opzione non riconosciuta: %s\n", arg.c_str());
            print_usage(stderr, argv[0]);
            return 1;
        }
    }

    if (TARGET_DIGITS < 1000) {
        std::fprintf(stderr, "Errore: -N deve essere >= 1000 (ricevuto %lld).\n", TARGET_DIGITS);
        return 1;
    }
    const unsigned int hw = std::thread::hardware_concurrency();
    if (NUM_THREADS < 1) {
        std::fprintf(stderr, "Errore: -t deve essere >= 1 (ricevuto %d).\n", NUM_THREADS);
        return 1;
    }
    if (hw > 0 && NUM_THREADS > static_cast<int>(hw)) {
        std::fprintf(stderr, "Nota: -t %d ridotto a %u (hardware_concurrency).\n", NUM_THREADS, hw);
        NUM_THREADS = static_cast<int>(hw);
    }
    if (out_file.empty()) {
        out_file = "pi_" + std::to_string(TARGET_DIGITS) + ".txt";
    }

    // --------------------- Controllo RAM (prima di allocare) ---------------------
    const double ram_gb = system_ram_gb();
    if (ram_gb <= 0.0) {
        std::fprintf(stderr,
            "Errore: impossibile rilevare la RAM di sistema (sysctl hw.memsize).\n"
            "Il controllo RAM è obbligatorio: esco senza calcolare.\n");
        return 1;
    }
    const double cap_gb  = std::min(HARD_CAP_GB, ram_gb - OS_MARGIN_GB);
    const double peak_gb = (double)TARGET_DIGITS * BYTES_PER_DIGIT / 1e9;

    std::printf("Cifre:   %lld\n", TARGET_DIGITS);
    std::printf("Thread:  %d\n", NUM_THREADS);
    std::printf("Output:  %s\n", out_file.c_str());
    std::printf("stima picco: %.1f GB / disponibile: %.1f GB\n", peak_gb, cap_gb);

    if (peak_gb > cap_gb) {
        const long long suggerite = (long long)(cap_gb * 0.9 * 1e9 / BYTES_PER_DIGIT);
        std::fprintf(stderr,
            "\nERRORE: RAM insufficiente per %lld cifre, il calcolo non parte.\n"
            "  cifre richieste : %lld\n"
            "  picco stimato   : %.1f GB  (N x 53 byte, margine 20%% incluso)\n"
            "  cap effettivo   : %.1f GB  (min(20 GB, RAM - 4 GB))\n"
            "  RAM sistema     : %.1f GB\n"
            "Suggerimento: ridurre -N (es. -N %lld).\n"
            "Nessun flag di override: il limite non è aggirabile.\n",
            TARGET_DIGITS, TARGET_DIGITS, peak_gb, cap_gb, ram_gb, suggerite);
        return 1;
    }
    if (TARGET_DIGITS >= 10000000) {
        std::printf("Attenzione: %lld cifre -> picco stimato %.1f GB; il calcolo può durare "
                    "da decine di secondi a minuti.\n", TARGET_DIGITS, peak_gb);
    }

    // Precisione di lavoro in bit (~3.322 bit per cifra decimale + margine)
    unsigned long prec = static_cast<unsigned long>((double)TARGET_DIGITS * std::log2(10.0)) + 1000;
    mpfr_float::default_precision(prec);

    // Numero di termini: ogni termine aggiunge ~14.181647 cifre
    const double digits_per_term = 14.181647462725477;
    unsigned long N = static_cast<unsigned long>((double)TARGET_DIGITS / digits_per_term) + 2;
    std::cout << "Termini: " << N << std::endl;

    // Il numero di thread non può superare i termini disponibili
    // (altrimenti si creerebbero intervalli vuoti e ricorsione infinita).
    if (NUM_THREADS > static_cast<int>(N - 1)) {
        NUM_THREADS = static_cast<int>(N - 1);
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
    // (divide-and-conquer interna di GMP).
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

    // ------------------------- Verifica delle prime 1000 cifre -------------------------
    if (verify) {
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

        bool ok = (pi_str.compare(0, known_prefix.length(), known_prefix) == 0);
        std::cout << "Prime 1000 cifre: " << (ok ? "OK" : "ERRORE!") << std::endl;
        if (!ok) {
            // Verifica fallita: errore e NESSUN file scritto.
            std::cerr << "ERRORE: la verifica delle prime 1000 cifre è fallita: "
                         "il risultato non corrisponde al prefisso noto. Nessun file scritto.\n";
            return 1;
        }
    } else {
        std::cout << "Verifica prime 1000 cifre: SALTATA (--no-verify)" << std::endl;
    }

    // Tempi separati: matematica vs conversione
    std::cout << "Binary splitting:    " << split_t.count() << " s" << std::endl;
    std::cout << "Radice + divisione:  " << sqrt_div_t.count() << " s" << std::endl;
    std::cout << "Conversione decimale:" << conv_t.count() << " s" << std::endl;
    std::cout << "Tempo totale:        " << total_t.count() << " s" << std::endl;

    double err_exp = digits_per_term * N;
    std::cout << "Errore stimato: 10^{-" << err_exp << "}" << std::endl;

    std::ofstream fout(out_file);
    if (!fout) {
        std::cerr << "ERRORE: impossibile aprire il file di output " << out_file << std::endl;
        return 1;
    }
    fout << pi_str;
    fout.close();
    std::cout << "Cifre complete salvate in " << out_file << std::endl;

    return 0;
}
