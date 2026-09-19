// ============================================================
// chord.h — chord diagram delle cifre di π
//
// 10 settori (uno per cifra 0–9) disposti in cerchio dal PUBBLICO in senso
// orario partendo dall'alto; ogni coppia di cifre consecutive (d[k], d[k+1])
// è una corda tra il settore di d[k] e quello di d[k+1].
//
// La geometria è pre-costruita come mesh di triangoli (su macOS GL 3.3 core
// glLineWidth è limitato a 1, quindi lo spessore delle corde viene dalla
// geometria: quad per corda con larghezza proporzionale a sqrt(conteggio)).
// Il colore della corda è quello della cifra di partenza; l'alpha cresce con
// il conteggio normalizzato (usato con blending additivo).
//
// Self-test temporaneo (non è un binario ufficiale):
//   clang++ -x c++ -std=c++17 -DCHORD_TEST visual/chord.h -o chord_test \
//     && ./chord_test [file_pi]
// ============================================================
#pragma once

#include <cmath>
#include <cstdio>
#include <vector>

#include "pi_reader.h"

// Palette PROVVISORIA (viva, una tinta netta per cifra) — sostituita dagli
// schemi pastello dello STEP 11.
static const float CHORD_PALETTE_TMP[10][3] = {
    {1.00f, 0.30f, 0.30f}, // 0 rosso
    {1.00f, 0.60f, 0.20f}, // 1 arancio
    {1.00f, 0.90f, 0.30f}, // 2 giallo
    {0.40f, 1.00f, 0.40f}, // 3 verde
    {0.30f, 0.90f, 0.90f}, // 4 ciano
    {0.35f, 0.50f, 1.00f}, // 5 blu
    {0.70f, 0.40f, 1.00f}, // 6 viola
    {1.00f, 0.40f, 0.80f}, // 7 magenta
    {0.90f, 0.90f, 0.90f}, // 8 bianco
    {1.00f, 0.70f, 0.55f}, // 9 salmone
};

// Vertice condiviso con lo shader flat: pos.xy + rgba
struct ChordVertex {
    float x, y, r, g, b, a;
};

struct ChordData {
    long long trans[10][10] = {};
    long long freq[10] = {};
    long long total = 0;   // cifre consumate

    // Accumula le transizioni dalle prime max_digits cifre decimali.
    void accumulate(const PiReader& r, size_t max_digits) {
        size_t n = max_digits < r.decimal_digits() ? max_digits : r.decimal_digits();
        if (n == 0) return;
        int prev = r.digit(0);
        freq[prev]++;
        for (size_t i = 1; i < n; ++i) {
            int d = r.digit(i);
            trans[prev][d]++;
            freq[d]++;
            prev = d;
        }
        total = (long long)n;
    }

    long long max_transition() const {
        long long mx = 0;
        for (int i = 0; i < 10; ++i)
            for (int j = 0; j < 10; ++j)
                if (trans[i][j] > mx) mx = trans[i][j];
        return mx;
    }
};

// Angolo (radianti) del CENTRO del settore della cifra d: dall'alto, orario.
static inline float chord_digit_angle(int d) {
    const float SEG = 2.0f * 3.14159265358979323846f / 10.0f;
    return 3.14159265358979323846f / 2.0f - (float(d) + 0.5f) * SEG;
}

// ---- mesh dei 10 settori (anello con gap): 10 triangolazioni ----
static inline void chord_build_sectors(const float palette[10][3],
                                       std::vector<ChordVertex>& out,
                                       float radius = 1.0f,
                                       float alpha = 0.35f) {
    const float TWO_PI = 6.28318530717958647692f;
    const float SEG = TWO_PI / 10.0f;
    const float GAP = 1.5f * 3.14159265358979323846f / 180.0f;  // gap tra settori
    const float RI = radius * 0.92f, RO = radius * 1.02f;
    const int SUB = 8;  // suddivisioni d'arco per settore
    out.clear();
    out.reserve(10 * SUB * 6);
    for (int d = 0; d < 10; ++d) {
        float a0 = chord_digit_angle(d) - SEG / 2.0f + GAP;
        float a1 = chord_digit_angle(d) + SEG / 2.0f - GAP;
        for (int k = 0; k < SUB; ++k) {
            float s0 = a0 + (a1 - a0) * (float(k) / SUB);
            float s1 = a0 + (a1 - a0) * (float(k + 1) / SUB);
            float c0 = std::cos(s0), s_0 = std::sin(s0);
            float c1 = std::cos(s1), s_1 = std::sin(s1);
            const float* col = palette[d];
            // triangolo 1: (RI,s0) (RO,s0) (RO,s1)
            out.push_back({RI * c0, RI * s_0, col[0], col[1], col[2], alpha});
            out.push_back({RO * c0, RO * s_0, col[0], col[1], col[2], alpha});
            out.push_back({RO * c1, RO * s_1, col[0], col[1], col[2], alpha});
            // triangolo 2: (RI,s0) (RO,s1) (RI,s1)
            out.push_back({RI * c0, RI * s_0, col[0], col[1], col[2], alpha});
            out.push_back({RO * c1, RO * s_1, col[0], col[1], col[2], alpha});
            out.push_back({RI * c1, RI * s_1, col[0], col[1], col[2], alpha});
        }
    }
}

// ---- mesh delle 100 corde (una per transizione con conteggio > 0) ----
static inline void chord_build_static_chords(const ChordData& cd,
                                             const float palette[10][3],
                                             std::vector<ChordVertex>& out,
                                             float radius = 1.0f) {
    const long long cmax = cd.max_transition();
    if (cmax == 0) { out.clear(); return; }
    const float RC = radius * 0.92f;      // corde fino al bordo interno dell'anello
    const float WMIN = 0.0025f, WMAX = 0.045f;
    out.clear();
    out.reserve(100 * 6);
    for (int i = 0; i < 10; ++i) {
        for (int j = 0; j < 10; ++j) {
            long long c = cd.trans[i][j];
            if (c == 0) continue;
            float ai = chord_digit_angle(i), aj = chord_digit_angle(j);
            float x0 = RC * std::cos(ai), y0 = RC * std::sin(ai);
            float x1 = RC * std::cos(aj), y1 = RC * std::sin(aj);
            // larghezza ~ sqrt(conteggio): percettivamente più lineare del conteggio
            float w = WMIN + (WMAX - WMIN) * std::sqrt((float)c / (float)cmax) * 0.5f;
            float alpha = 0.22f + 0.78f * ((float)c / (float)cmax);
            // perpendicolare alla corda
            float dx = x1 - x0, dy = y1 - y0;
            float len = std::sqrt(dx * dx + dy * dy);
            if (len < 1e-9f) continue;   // transizione d->d: corda degenerata
            float nx = -dy / len * w, ny = dx / len * w;
            const float* col = palette[i];
            out.push_back({x0 + nx, y0 + ny, col[0], col[1], col[2], alpha});
            out.push_back({x0 - nx, y0 - ny, col[0], col[1], col[2], alpha});
            out.push_back({x1 + nx, y1 + ny, col[0], col[1], col[2], alpha});
            out.push_back({x0 - nx, y0 - ny, col[0], col[1], col[2], alpha});
            out.push_back({x1 + nx, y1 + ny, col[0], col[1], col[2], alpha});
            out.push_back({x1 - nx, y1 - ny, col[0], col[1], col[2], alpha});
        }
    }
}

#ifdef CHORD_TEST
#include <cstdlib>
// Self-test: bilanciamento delle cifre + costruzione mesh.
int main(int argc, char* argv[]) {
    const char* path = (argc > 1) ? argv[1] : "pi100m.txt";
    PiReader r;
    if (!r.open(path)) return 1;
    ChordData cd;
    cd.accumulate(r, 1000000);   // 1M cifre bastano per giudicare il bilanciamento

    long long mn = cd.freq[0], mx = cd.freq[0];
    for (int d = 0; d < 10; ++d) {
        std::printf("cifra %d: %lld (%.3f%%)\n", d, cd.freq[d], 100.0 * cd.freq[d] / cd.total);
        if (cd.freq[d] < mn) mn = cd.freq[d];
        if (cd.freq[d] > mx) mx = cd.freq[d];
    }
    double ratio = (double)mx / (double)mn;
    std::printf("rapporto max/min frequenze: %.4f\n", ratio);
    if (ratio > 1.03) {
        std::fprintf(stderr, "FALLITO: uno settore domina visivamente (ratio %.4f > 1.03)\n", ratio);
        return 1;
    }

    std::vector<ChordVertex> sec, cho;
    chord_build_sectors(CHORD_PALETTE_TMP, sec);
    chord_build_static_chords(cd, CHORD_PALETTE_TMP, cho);
    std::printf("vertici settori: %zu (attesi %d)\n", sec.size(), 10 * 8 * 6);
    std::printf("vertici corde:   %zu (100 transizioni x 6 = 600 se tutte presenti)\n", cho.size());
    if (sec.size() != (size_t)(10 * 8 * 6)) return 1;
    if (cho.empty()) return 1;
    std::printf("chord_test: OK\n");
    return 0;
}
#endif
