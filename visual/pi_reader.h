// ============================================================
// pi_reader.h — lettura del file delle cifre di π via mmap
//
// Header-only (il modulo è piccolo). Layout atteso del file:
//   data[0] = '3', data[1] = '.', decimali da data[2] in poi,
//   nessun newline finale (formato di pi100m.txt / pi_calc).
//
// Il file NON viene copiato in memoria: accesso a byte diretti sul mmap,
// così 100 MB di cifre non pesano sul heap.
//
// All'open() verifica le prime 100 cifre contro il prefisso noto
// (prime 100 cifre del prefisso di pi100m.cpp): se non corrispondono
// ritorna errore — chi chiama deve uscire.
//
// Self-test temporaneo (non è un binario ufficiale):
//   clang++ -x c++ -std=c++17 -DPI_READER_TEST visual/pi_reader.h -o pi_reader_test
//   ./pi_reader_test [file_pi]
// ============================================================
#pragma once

#include <cstddef>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

struct PiReader {
    const char* data = nullptr;   // mappa completa del file
    size_t      size = 0;         // byte mappati
    bool        ok   = false;

    // Cifra decimale i (0-based) dopo la virgola: data[2 + i] - '0'.
    // Il chiamante garantisce i < decimal_digits().
    int digit(size_t i) const { return data[2 + i] - '0'; }

    // Numero di cifre decimali disponibili nel file (byte - 2: "3.")
    size_t decimal_digits() const { return size >= 2 ? size - 2 : 0; }

    bool open(const char* path) {
        ok = false;
        int fd = ::open(path, O_RDONLY);
        if (fd < 0) {
            std::fprintf(stderr, "pi_reader: impossibile aprire %s\n", path);
            return false;
        }
        struct stat st;
        if (fstat(fd, &st) != 0 || st.st_size < 102) {
            std::fprintf(stderr, "pi_reader: file %s troppo piccolo o fstat fallito "
                                 "(atteso \"3.\" + cifre)\n", path);
            ::close(fd);
            return false;
        }
        size = (size_t)st.st_size;
        void* mapped = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        ::close(fd);
        if (mapped == MAP_FAILED) {
            std::fprintf(stderr, "pi_reader: mmap fallito su %s\n", path);
            size = 0;
            return false;
        }
        data = (const char*)mapped;

        // Verifica del prefisso: prime 100 cifre (il prefisso noto di
        // pi100m.cpp è di 1000 cifre; qui bastano le prime 100 per essere
        // sicuri di aver aperto il file giusto).
        static const char KNOWN_100[] =
            "3.1415926535897932384626433832795028841971693993751058209749445923078"
            "1640628620899862803482534211706";
        if (std::memcmp(data, KNOWN_100, sizeof(KNOWN_100) - 1) != 0) {
            std::fprintf(stderr,
                "pi_reader: ERRORE, il file %s non inizia con il prefisso noto "
                "di π (3.14159265358979...): file sbagliato o corrotto?\n", path);
            close();
            return false;
        }
        ok = true;
        return true;
    }

    void close() {
        if (data) munmap((void*)data, size);
        data = nullptr;
        size = 0;
        ok = false;
    }

    ~PiReader() { close(); }
};

#ifdef PI_READER_TEST
// Self-test: stampa le prime 100 cifre e verifica il prefisso.
int main(int argc, char* argv[]) {
    const char* path = (argc > 1) ? argv[1] : "pi100m.txt";
    PiReader r;
    if (!r.open(path)) return 1;
    std::printf("file: %s, byte: %zu, cifre decimali: %zu\n",
                path, r.size, r.decimal_digits());
    std::printf("prime 100 cifre: %.100s\n", r.data);
    // controllo manuale del layout: data[0]=='3', data[1]=='.', decimali da data[2]
    std::printf("layout: data[0]='%c', data[1]='%c', digit(0)=%d digit(97)=%d\n",
                r.data[0], r.data[1], r.digit(0), r.digit(97));
    std::printf("verifica prefisso: OK\n");
    return 0;
}
#endif
