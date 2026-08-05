#define GL_SILENCE_DEPRECATION
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <cmath>
#include <fstream>
#include <boost/multiprecision/mpfr.hpp>
#include <mpfr.h>

using namespace boost::multiprecision;
using mpfr_float = number<mpfr_float_backend<0>>;

// Costanti di Chudnovsky
constexpr long long A = 13591409;
constexpr long long B = 545140134;
constexpr unsigned long long C3_OVER_24_VAL = 10939058860032000ULL; // 640320^3 / 24

// Struttura per i tre valori del binary splitting
struct Triple {
    mpfr_float P, Q, R;
};

mpfr_float C3_OVER_24;  // inizializzata dopo aver impostato la precisione

// Calcolo ricorsivo per l'intervallo [a, b) (con a >= 1)
Triple bs(unsigned long a, unsigned long b) {
    if (b - a == 1) {
        Triple t;
        // P(a) = (6a-5)(2a-1)(6a-1)
        unsigned long long p = (6ULL * a - 5) * (2ULL * a - 1) * (6ULL * a - 1);
        t.P = mpfr_float(p);
        t.Q = mpfr_float(a) * a * a * C3_OVER_24;
        // R(a) = (-1)^a * (A + B*a) * P(a)
        mpfr_float term = A + B * a;
        if (a % 2 == 1) term = -term;   // (-1)^a per a dispari
        t.R = term * t.P;
        return t;
    } else {
        unsigned long m = (a + b) / 2;
        Triple left = bs(a, m);
        Triple right = bs(m, b);
        Triple res;
        res.P = left.P * right.P;
        res.Q = left.Q * right.Q;
        // R = R_left * Q_right + P_left * R_right
        res.R = left.R * right.Q + left.P * right.R;
        return res;
    }
}

int main() {
    // ------------------------- Configurazione -------------------------
    constexpr long TARGET_DIGITS = 300014;   // cifre totali (incluso "3.")
    constexpr int NUM_THREADS = 4;          // carico moderato

    // Precisione in bit sufficiente (3.322 bit per cifra + margine)
    unsigned long prec = static_cast<unsigned long>(TARGET_DIGITS * std::log2(10.0)) + 1000;
    mpfr_float::default_precision(prec);
    C3_OVER_24 = mpfr_float(C3_OVER_24_VAL);

    // Numero di termini: ogni termine aggiunge ~14.181647 cifre
    const double digits_per_term = 14.181647462725477;
    unsigned long N = static_cast<unsigned long>(TARGET_DIGITS / digits_per_term) + 2;
    std::cout << "Termini: " << N << std::endl;

    // ------------------ Calcolo parallelo ------------------
    // Intervallo [1, N) diviso in blocchi (k parte da 1)
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

    auto start_time = std::chrono::steady_clock::now();

    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([i, &starts, &ends, &results]() {
            results[i] = bs(starts[i], ends[i]);
        });
    }
    for (auto& t : threads) t.join();

    // Combinazione in ordine
    Triple total = results[0];
    for (int i = 1; i < NUM_THREADS; ++i) {
        Triple tmp;
        tmp.P = total.P * results[i].P;
        tmp.Q = total.Q * results[i].Q;
        tmp.R = total.R * results[i].Q + total.P * results[i].R;
        total = tmp;
    }

    // Somma totale S = A + R/Q  →  π = 426880·√10005 / S
    mpfr_float sqrt10005 = sqrt(mpfr_float(10005));
    mpfr_float num = 426880 * sqrt10005 * total.Q;
    mpfr_float den = A * total.Q + total.R;
    mpfr_float pi = num / den;

    auto end_time = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed = end_time - start_time;

    // Estrazione cifre
    std::string pi_str;
    {
        char* buf = new char[TARGET_DIGITS + 10];
        mpfr_sprintf(buf, "%.*Rf", TARGET_DIGITS, pi.backend().data());
        pi_str = buf;
        delete[] buf;
    }

    // Verifica prime 1000 cifre
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
    std::cout << "Tempo calcolo: " << elapsed.count() << " s" << std::endl;

    double err_exp = digits_per_term * N;
    std::cout << "Errore stimato: 10^{-" << err_exp << "}" << std::endl;

    // Salvataggio su file
    std::ofstream fout("pi300k.txt");
    fout << pi_str;
    fout.close();
    std::cout << "Cifre complete salvate in pi300k.txt" << std::endl;

    return 0;
}
