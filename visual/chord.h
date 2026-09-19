// ============================================================
// chord.h — chord diagram delle cifre di π
//
// 10 settori (uno per cifra 0–9) disposti in cerchio in senso orario partendo
// dall'alto; ogni coppia di cifre consecutive (d[k], d[k+1]) è una corda tra
// il settore di d[k] e quello di d[k+1].
//
// La geometria è pre-costruita come mesh di triangoli (su macOS GL 3.3 core
// glLineWidth è limitato a 1, quindi lo spessore delle corde viene dalla
// geometria: quad per corda con larghezza proporzionale a sqrt(conteggio)).
// Il colore della corda è quello della cifra di partenza; l'alpha cresce con
// il conteggio normalizzato (usato con blending additivo).
//
// Streaming (STEP 6): rolling window circolare delle ultime N corde; ogni
// corda memorizza il tempo di emissione e ogni frame l'alpha di fade viene
// ricalcolata sulla CPU (degrada verso il nero in FADE_S secondi; con il
// blending additivo rgb·alpha → 0 significa davvero spegnersi).
//
// Self-test temporaneo (non è un binario ufficiale):
//   clang++ -x c++ -std=c++17 -DCHORD_TEST visual/chord.h -o chord_test \
//     && ./chord_test [file_pi]
// ============================================================
#pragma once

#include <cmath>
#include <cstdio>
#include <vector>

#include <OpenGL/gl3.h>

#include "pi_reader.h"

// Palette (STEP 11): la 0 è la tinta vivace provvisoria, la 1-3 sono le
// varianti pastello richieste (Botanica, Alba, Notturna). Una tinta per cifra.
static const float CHORD_PALETTES[4][10][3] = {
    { // 0: provvisoria (viva)
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
    },
    { // 1: Botanica
        {0.63f, 0.70f, 0.55f}, // 0 salvia
        {0.80f, 0.62f, 0.31f}, // 1 ocra
        {0.80f, 0.48f, 0.38f}, // 2 terracotta
        {0.71f, 0.64f, 0.83f}, // 3 lavanda
        {0.45f, 0.56f, 0.73f}, // 4 blu polvere
        {0.87f, 0.66f, 0.68f}, // 5 rosa cipria
        {0.94f, 0.76f, 0.64f}, // 6 pesca
        {0.96f, 0.92f, 0.80f}, // 7 panna
        {0.61f, 0.60f, 0.35f}, // 8 oliva
        {0.70f, 0.36f, 0.25f}, // 9 ruggine
    },
    { // 2: Alba
        {0.93f, 0.72f, 0.76f}, // 0 rosa tenue
        {0.72f, 0.66f, 0.90f}, // 1 lavanda
        {0.96f, 0.80f, 0.68f}, // 2 pesca
        {0.91f, 0.60f, 0.58f}, // 3 corallo desaturato
        {0.95f, 0.74f, 0.55f}, // 4 albicocca
        {0.97f, 0.89f, 0.64f}, // 5 giallo burro
        {0.65f, 0.85f, 0.72f}, // 6 verde menta
        {0.63f, 0.79f, 0.91f}, // 7 azzurro cielo
        {0.84f, 0.74f, 0.88f}, // 8 lilla
        {0.97f, 0.94f, 0.86f}, // 9 crema
    },
    { // 3: Notturna
        {0.42f, 0.53f, 0.70f}, // 0 blu polvere
        {0.36f, 0.40f, 0.62f}, // 1 indaco desaturato
        {0.78f, 0.80f, 0.83f}, // 2 argento
        {0.72f, 0.73f, 0.76f}, // 3 grigio perla
        {0.30f, 0.38f, 0.55f}, // 4 blu notte chiaro
        {0.55f, 0.47f, 0.68f}, // 5 viola tenue
        {0.52f, 0.70f, 0.75f}, // 6 ciano desaturato
        {0.48f, 0.44f, 0.63f}, // 7 lavanda scura
        {0.36f, 0.50f, 0.42f}, // 8 verde bosco tenue
        {0.88f, 0.88f, 0.85f}, // 9 bianco sporco
    },
};
static const char* CHORD_PALETTE_NAMES[4] = {"provvisoria", "Botanica", "Alba", "Notturna"};

// Palette corrente (switchabile a runtime con i tasti 0–3): le mesh statiche
// vengono ricostruite al cambio; le corde in streaming usano questa al momento
// dell'emissione (le corde già emesse conservano il loro colore).
static const float (*CHORD_CURRENT_PALETTE)[3] = CHORD_PALETTES[1];   // default: Botanica

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

// Raggio delle corde statiche e in streaming: coincide con il bordo interno
// dei settori (0.92), così le corde "arrivano" all'anello.
static const float CHORD_RADIUS = 0.92f;

// ------------------------- streaming con rolling window -------------------------
// Finestra circolare delle ultime N corde; ogni corda memorizza il tempo di
// emissione in times[] e ogni frame l'alpha di fade viene ricalcolata sulla
// CPU per tutti i vertici (N è piccola: 5000 corde = 30000 vertici).
// L'upload avviene una sola volta per frame con orphaning del buffer.
struct ChordStream {
    static constexpr int    N      = 5000;   // dimensione della rolling window
    static constexpr double FADE_S = 8.0;    // durata del fade verso il nero
    static constexpr int    VPC    = 6;      // vertici per corda (2 triangoli)

    std::vector<ChordVertex> vtx;            // N*VPC vertici (copia CPU)
    std::vector<float>       times;          // N tempi di emissione
    size_t    head = 0;                      // prossima corda da sovrascrivere
    long long emitted = 0;                   // corde emesse dall'inizio
    long long base_index = 0;                // prossima cifra da leggere (d[base_index])
    double    frac_acc = 0.0;                // accumulo frazionario (rate·dt)
    GLuint    vao = 0, vbo = 0;

    bool init_gl() {
        vtx.assign((size_t)N * VPC, {0, 0, 0, 0, 0, 0});
        times.assign((size_t)N, -1e30f);     // "mai emessa": fade già esaurito
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, vtx.size() * sizeof(ChordVertex),
                     vtx.data(), GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(ChordVertex), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(ChordVertex),
                              (void*)(2 * sizeof(float)));
        glBindVertexArray(0);
        return vbo != 0;
    }

    void destroy_gl() {
        if (vbo) glDeleteBuffers(1, &vbo);
        if (vao) glDeleteVertexArrays(1, &vao);
        vao = vbo = 0;
    }

    // Reset completo (tasto R): torna all'inizio del file e svuota la finestra.
    void reset() {
        head = 0; emitted = 0; base_index = 0; frac_acc = 0.0;
        for (auto& v : vtx) v = {0, 0, 0, 0, 0, 0};
        for (auto& tm : times) tm = -1e30f;
    }

    // Legge le prossime n transizioni dal file π e le aggiunge alla finestra.
    // Non tocca GL: l'upload avviene in render() (una sola chiamata a frame).
    void push(double t, const PiReader& r, const float anchors[10][2],
              const float palette[10][3], long long n) {
        const size_t max_digits = r.decimal_digits();
        for (long long k = 0; k < n; ++k) {
            size_t i = (size_t)base_index;   // cifra d[i]
            size_t j = i + 1;                // cifra d[i+1]
            if (j >= max_digits) return;     // file finito: resta stabilizzato
            int a = r.digit(i), b = r.digit(j);
            base_index++;
            add_chord(t, anchors[a], anchors[b], palette[a]);
        }
    }

    void add_chord(double t, const float p0[2], const float p1[2], const float col[3]) {
        float dx = p1[0] - p0[0], dy = p1[1] - p0[1];
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-9f) return;             // transizione d->d: corda degenerata
        const float w = 0.004f;
        float nx = -dy / len * w, ny = dx / len * w;
        ChordVertex* q = &vtx[head * VPC];
        q[0] = {p0[0] + nx, p0[1] + ny, col[0], col[1], col[2], 1.0f};
        q[1] = {p0[0] - nx, p0[1] - ny, col[0], col[1], col[2], 1.0f};
        q[2] = {p1[0] + nx, p1[1] + ny, col[0], col[1], col[2], 1.0f};
        q[3] = q[1];
        q[4] = q[2];
        q[5] = {p1[0] - nx, p1[1] - ny, col[0], col[1], col[2], 1.0f};
        times[head] = (float)t;
        head = (head + 1) % (size_t)N;
        emitted++;
    }

    // Ricalcola l'alpha di fade e ridisegna la finestra. Richiede che il
    // chiamante abbia già: glUseProgram(flat.prog), u_alpha=1, blending additivo.
    void render(double t_now, GLint u_mvp_loc, const mat4 mvp) {
        if (emitted == 0) return;
        const float inv_fade = (float)(1.0 / FADE_S);
        for (size_t c = 0; c < (size_t)N; ++c) {
            float age = (float)t_now - times[c];
            float f = 1.0f - age * inv_fade;
            if (f < 0.0f) f = 0.0f; else if (f > 1.0f) f = 1.0f;
            f = f * f;                       // fade quadratico, più morbido
            ChordVertex* q = &vtx[c * VPC];
            q[0].a = f; q[1].a = f; q[2].a = f; q[3].a = f; q[4].a = f; q[5].a = f;
        }
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, vtx.size() * sizeof(ChordVertex),
                     nullptr, GL_DYNAMIC_DRAW);            // orphaning
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        vtx.size() * sizeof(ChordVertex), vtx.data());
        glUniformMatrix4fv(u_mvp_loc, 1, GL_FALSE, mvp);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, N * VPC);
        glBindVertexArray(0);
    }
};

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
        std::fprintf(stderr, "FALLITO: un settore domina visivamente (ratio %.4f > 1.03)\n", ratio);
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
