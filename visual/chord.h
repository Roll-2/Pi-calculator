// ============================================================
// chord.h — chord diagram delle cifre di π (stile "digit circle")
//
// Estetica di riferimento (esempi forniti dall'utente):
//   - anello pastello spesso con gap e cifre grandi ACCANTO a ogni settore;
//   - corde CURVE (Bézier quadratiche con controllo verso il centro),
//     sottili, semitrasparenti, distribuite — niente ventagli convergenti;
//   - tutto centrato nel viewport e interamente visibile.
//
// Dettagli tecnici:
//   - ogni settore ha SUB_ANCH punti di aggancio distribuiti lungo l'arco:
//     la k-esima corda che tocca la cifra d usa l'ancoraggio (k mod SUB_ANCH),
//     così le corde si distribuiscono invece di sovrapporsi;
//   - la corda è una Bézier quadratica approssimata con BEZ_SEG segmenti;
//     il punto di controllo è verso il centro (fattore BEZ_BOW), quindi le
//     corde lunghe si curvano dolcemente dentro il cerchio;
//   - lo spessore è in unità mondo (corde ~0.004): visibile ma sottile;
//   - le corde sono SEMPRE vive (saturazione piena, niente desaturazione
//     iniziale né fade verso il bianco): la "ragnatela" si accende perché
//     CRESCA nel numero di linee, non perché cambi tinta;
//   - il layer statico NON è un'anteprima: la matrice 10×10 parte vuota e
//     viene alimentata dallo streaming (ragnatela crescente), con rebuild
//     periodico ogni CHORD_REBUILD_EVERY corde;
//   - il fade temporale delle corde in streaming è calcolato NELLO SHADER
//     (uniform u_now + tempo di emissione per corda): zero upload per frame
//     anche con migliaia di corde;
//   - l'anello (raggio esterno 1.0) è disegnato con ortho [-1.25·a, 1.25·a]
//     × [-1.25, 1.25]: sempre centrato, mai tagliato, con margine per le cifre.
//
// Self-test temporaneo (non è un binario ufficiale):
//   clang++ -x c++ -std=c++17 -DCHORD_TEST visual/chord.h -o chord_test \
//     && ./chord_test [file_pi]
// ============================================================
#pragma once

#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION
#endif

#include <cmath>
#include <cstdio>
#include <vector>

#include <OpenGL/gl3.h>

#include "camera.h"     // mat4 (column-major) per il render dello stream
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

// Palette corrente (switchabile a runtime con i tasti 0–3).
static const float (*CHORD_CURRENT_PALETTE)[3] = CHORD_PALETTES[1];   // default: Botanica

// Vertice condiviso con lo shader flat: pos.xy + rgba
struct ChordVertex {
    float x, y, r, g, b, a;
};

// ---------------------------------------------------------------
// Geometria del cerchio
// ---------------------------------------------------------------
static constexpr float CHORD_RO       = 1.00f;  // raggio esterno dell'anello
static constexpr float CHORD_RI       = 0.86f;  // raggio interno (anello spesso)
static constexpr float CHORD_RANCH    = 0.86f;  // ancoraggi corde: bordo interno anello
static constexpr float CHORD_RLABEL   = 1.13f;  // raggio delle cifre attorno
static constexpr float CHORD_WORLD    = 1.25f;  // half-extent verticale dell'ortho
static constexpr float CHORD_WIDTH    = 0.004f; // spessore corda (unità mondo)
static constexpr float CHORD_WMAX     = 0.010f; // spessore max corda statica
static constexpr int   CHORD_SUBANCH  = 48;     // ancoraggi distribuiti per cifra
static constexpr int   CHORD_BEZ_SEG  = 32;     // segmenti per la Bézier (curve morbide)
static constexpr int   CHORD_SECSUB   = 16;     // suddivisioni arco per settore
static constexpr float CHORD_GAP_DEG  = 2.0f;   // gap tra settori (gradi)
static constexpr float CHORD_FADE_S   = 30.0f;  // durata fade corde in streaming
static constexpr int   CHORD_REBUILD_EVERY = 512; // corde tra i rebuild della ragnatela

// ---------------------------------------------------------------
// Rate delle cifre (orologio di SCENA: con lo slow globale ai close-up
// anche il chord rallenta insieme al pendolo).
//   fase 1: RATE_SLOW cifre/s fino a RATE_SLOW_T → ~32 cifre a 2,5 s/cifra
//           (inizio delicato ma non rallento: prima era 0.1 → 10 s/cifra)
//   fase 2: esponenziale da lì: 100 cifre/s a ~500 cifre (t≈107 s),
//           poi la crescita esaurisce 314.159 cifre a t≈140 s
//           (i 100M a t≈174 s con cap RATE_MAX)
// ---------------------------------------------------------------
static constexpr double RATE_SLOW   = 0.4;    // cifre/s iniziali (2,5 s/cifra)
static constexpr double RATE_SLOW_T = 80.0;   // s di rampa lenta (~8 cifre)
static constexpr double RATE_K2     = 0.203;  // 0.1 → 100 in ~34 s
static constexpr double RATE_MAX    = 9.0e6;  // saturazione

static inline double rate_of(double t) {
    if (t < RATE_SLOW_T) return RATE_SLOW;
    double r = RATE_SLOW * std::exp(RATE_K2 * (t - RATE_SLOW_T));
    return r < RATE_MAX ? r : RATE_MAX;
}

// Ricchezza di colore della corda k-esima emessa: le PRIME sono ricche
// (0.85), le successive sempre più flebili (mai verso il bianco: si
// sommano in sfumature sempre più tenui, floor 0.08).
static inline float chord_stream_alpha(long long k) {
    double a = 0.85 * std::pow(1.0 + (double)k / 400.0, -0.30);
    if (a < 0.08) a = 0.08;
    return (float)a;
}

static constexpr float CHORD_TWO_PI = 6.28318530717958647692f;
static constexpr float CHORD_PI     = 3.14159265358979323846f;

// Angolo (radianti) del CENTRO del settore della cifra d: dall'alto, orario.
static inline float chord_digit_angle(int d) {
    const float SEG = CHORD_TWO_PI / 10.0f;
    return CHORD_PI / 2.0f - (float(d) + 0.5f) * SEG;
}

// k-esimo ancoraggio della cifra d: punto distribuito lungo l'arco del settore.
static inline void chord_anchor(int d, int k, float out[2]) {
    const float SEG = CHORD_TWO_PI / 10.0f;
    const float GAP = CHORD_GAP_DEG * CHORD_PI / 180.0f;
    float a0 = chord_digit_angle(d) - SEG / 2.0f + GAP;
    float a1 = chord_digit_angle(d) + SEG / 2.0f - GAP;
    // distribuzione deterministica che copre l'arco (evita moiré con passo puro)
    float s = (float)(k % CHORD_SUBANCH) / (float)(CHORD_SUBANCH - 1);
    float jitter = ((float)((k * 2654435761u) & 0xFFFF) / 65535.0f - 0.5f)
                   * (a1 - a0) / (float)CHORD_SUBANCH;
    float a = a0 + (a1 - a0) * s + jitter;
    out[0] = CHORD_RANCH * std::cos(a);
    out[1] = CHORD_RANCH * std::sin(a);
}

struct ChordData {
    long long trans[10][10] = {};
    long long freq[10] = {};
    long long total = 0;   // cifre consumate
    long long emitted[10] = {};   // corde già emesse per cifra d'origine (round-robin)

    // Aggiorna la matrice con una transizione (ragnatela crescente).
    void update(int prev, int d) {
        trans[prev][d]++;
        freq[d]++;
        total++;
    }

    void accumulate(const PiReader& r, size_t max_digits) {
        size_t n = max_digits < r.decimal_digits() ? max_digits : r.decimal_digits();
        if (n == 0) return;
        int prev = r.digit(0);
        freq[prev]++; total++;
        for (size_t i = 1; i < n; ++i) {
            update(prev, r.digit(i));
            prev = r.digit(i);
        }
    }

    long long max_transition() const {
        long long mx = 0;
        for (int i = 0; i < 10; ++i)
            for (int j = 0; j < 10; ++j)
                if (trans[i][j] > mx) mx = trans[i][j];
        return mx;
    }

    // Svuota la matrice (reset del tasto R: la ragnatela riparte da zero).
    void clear() {
        for (int i = 0; i < 10; ++i)
            for (int j = 0; j < 10; ++j) trans[i][j] = 0;
        for (int d = 0; d < 10; ++d) freq[d] = 0;
        total = 0;
    }
};

// Matrice della ragnatela crescente: parte VUOTA e viene alimentata dallo
// streaming; il layer statico la visualizza (rebuild periodico nel main).
static ChordData chord_web;

// ---------------------------------------------------------------
// Mesh dell'anello: 10 settori pastello con gap (triangolazioni d'arco)
// ---------------------------------------------------------------
static inline void chord_build_sectors(const float palette[10][3],
                                       std::vector<ChordVertex>& out,
                                       float alpha = 1.0f) {
    const float SEG = CHORD_TWO_PI / 10.0f;
    const float GAP = CHORD_GAP_DEG * CHORD_PI / 180.0f;
    out.clear();
    out.reserve(10 * CHORD_SECSUB * 6);
    for (int d = 0; d < 10; ++d) {
        float a0 = chord_digit_angle(d) - SEG / 2.0f + GAP;
        float a1 = chord_digit_angle(d) + SEG / 2.0f - GAP;
        for (int k = 0; k < CHORD_SECSUB; ++k) {
            float s0 = a0 + (a1 - a0) * (float(k) / CHORD_SECSUB);
            float s1 = a0 + (a1 - a0) * (float(k + 1) / CHORD_SECSUB);
            float c0 = std::cos(s0), s_0 = std::sin(s0);
            float c1 = std::cos(s1), s_1 = std::sin(s1);
            const float* col = palette[d];
            out.push_back({CHORD_RI * c0, CHORD_RI * s_0, col[0], col[1], col[2], alpha});
            out.push_back({CHORD_RO * c0, CHORD_RO * s_0, col[0], col[1], col[2], alpha});
            out.push_back({CHORD_RO * c1, CHORD_RO * s_1, col[0], col[1], col[2], alpha});
            out.push_back({CHORD_RI * c0, CHORD_RI * s_0, col[0], col[1], col[2], alpha});
            out.push_back({CHORD_RO * c1, CHORD_RO * s_1, col[0], col[1], col[2], alpha});
            out.push_back({CHORD_RI * c1, CHORD_RI * s_1, col[0], col[1], col[2], alpha});
        }
    }
}

// ---------------------------------------------------------------
// Corda curva: Bézier quadratica P0->P1 con controllo verso il centro.
// Larghezza costante w, triangolata come nastro lungo la curva.
// Colore del settore d'origine; alpha uniforme (fade gestito altrove).
// ---------------------------------------------------------------
static inline void chord_push_curved(std::vector<ChordVertex>& out,
                                     const float p0[2], const float p1[2],
                                     const float col[3], float alpha, float w) {
    // punto di controllo: verso il centro, raggio ridotto (curva dolce dentro)
    float mx = (p0[0] + p1[0]) * 0.5f, my = (p0[1] + p1[1]) * 0.5f;
    float ml = std::sqrt(mx * mx + my * my);
    if (ml < 1e-9f) return;                       // corda d->d dal centro: salta
    const float BOW = 0.62f;                      // il controllo sta al 62% del raggio
    float cx = mx / ml * ml * BOW, cy = my / ml * ml * BOW;

    // nastro: per ogni campione, perpendicolare locale alla curva
    const int SEG = CHORD_BEZ_SEG;
    float prev_px = 0, prev_py = 0, prev_nx = 0, prev_ny = 0;
    for (int s = 0; s <= SEG; ++s) {
        float u = (float)s / SEG, v = 1.0f - u;
        float x = v * v * p0[0] + 2 * v * u * cx + u * u * p1[0];
        float y = v * v * p0[1] + 2 * v * u * cy + u * u * p1[1];
        // derivata della Bézier = tangente
        float tx = 2 * v * (cx - p0[0]) + 2 * u * (p1[0] - cx);
        float ty = 2 * v * (cy - p0[1]) + 2 * u * (p1[1] - cy);
        float tl = std::sqrt(tx * tx + ty * ty);
        if (tl < 1e-9f) { tx = 1; ty = 0; tl = 1; }
        float nx = -ty / tl * w, ny = tx / tl * w;
        if (s > 0) {
            out.push_back({prev_px + prev_nx, prev_py + prev_ny, col[0], col[1], col[2], alpha});
            out.push_back({prev_px - prev_nx, prev_py - prev_ny, col[0], col[1], col[2], alpha});
            out.push_back({x + nx, y + ny, col[0], col[1], col[2], alpha});
            out.push_back({prev_px - prev_nx, prev_py - prev_ny, col[0], col[1], col[2], alpha});
            out.push_back({x + nx, y + ny, col[0], col[1], col[2], alpha});
            out.push_back({x - nx, y - ny, col[0], col[1], col[2], alpha});
        }
        prev_px = x; prev_py = y; prev_nx = nx; prev_ny = ny;
    }
}

// ---------------------------------------------------------------
// Corde statiche (contesto): una Bézier per transizione con conteggio > 0,
// spessore ~ sqrt(conteggio), alpha basso, blending additivo da parte del caller.
// ---------------------------------------------------------------
static inline void chord_build_static_chords(const ChordData& cd,
                                             const float palette[10][3],
                                             std::vector<ChordVertex>& out) {
    const long long cmax = cd.max_transition();
    out.clear();
    if (cmax == 0) return;
    out.reserve(100 * CHORD_BEZ_SEG * 6);
    for (int i = 0; i < 10; ++i) {
        for (int j = 0; j < 10; ++j) {
            long long c = cd.trans[i][j];
            if (c == 0) continue;
            float p0[2], p1[2];
            // ancoraggi distribuiti anche nel layer statico (hash deterministico
            // per transizione): niente ventagli convergenti su un punto solo
            chord_anchor(i, (i * 7 + j * 13) % CHORD_SUBANCH, p0);
            chord_anchor(j, (i * 13 + j * 7 + 5) % CHORD_SUBANCH, p1);
            float w = CHORD_WIDTH + (CHORD_WMAX - CHORD_WIDTH)
                      * std::sqrt((float)c / (float)cmax);
            float alpha = 0.30f + 0.25f * ((float)c / (float)cmax);
            chord_push_curved(out, p0, p1, palette[i], alpha, w);
        }
    }
}

// ---------------------------------------------------------------
// Streaming: rolling window di corde recenti, fade CALCOLATO NELLO SHADER.
// Ogni corda occupa CHORD_BEZ_SEG*6 vertici consecutivi e porta l'istante di
// emissione come attributo (a_birth, location 2): il fragment calcola
// alpha = 1 - (u_now - a_birth)/FADE e lo moltiplica.
// ---------------------------------------------------------------
static const char* CHORD_STREAM_VS =
    "#version 330 core\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "layout(location = 1) in vec4 a_col;\n"
    "layout(location = 2) in float a_birth;\n"
    "uniform mat4 u_mvp;\n"
    "uniform float u_now;\n"
    "out vec4 v_col;\n"
    "void main() {\n"
    "    gl_Position = u_mvp * vec4(a_pos, 0.0, 1.0);\n"
    "    float age = u_now - a_birth;\n"
    "    float f = 1.0 - age / float(";
static const char* CHORD_STREAM_VS2 =
    ");\n"
    "    f = clamp(f, 0.0, 1.0);\n"
    "    v_col = vec4(a_col.rgb, a_col.a * f * f);\n"   // fade quadratico
    "}\n";

// Vertice dello stream: pos + rgba + istante di emissione (7 float).
struct StreamVertex {
    float x, y, r, g, b, a, birth;
};

struct ChordStream {
    static constexpr int N         = 4000;
    static constexpr int VPC       = CHORD_BEZ_SEG * 6;   // vertici per corda
    static constexpr long long CAP_PER_FRAME = 500000;      // tetto cifre/frame
    static constexpr long long DRAW_BUDGET   = 500;         // corde disegnate/frame

    std::vector<StreamVertex> vtx;     // N*VPC vertici
    std::vector<float>       birth;    // N istanti di emissione
    size_t    head = 0;                // prossima corda da sovrascrivere
    long long emitted = 0;
    long long base_index = 0;          // prossima cifra da leggere
    double    frac_acc = 0.0;
    bool      web_dirty = false;       // ragnatela da ricontestualizzare
    int       dirty_start = 0, dirty_len = 0;   // range da ricaricare sul VBO
    GLuint    vao = 0, vbo = 0;
    GLuint    prog = 0;
    GLint     u_mvp = -1, u_now = -1;

    bool init_gl() {
        vtx.assign((size_t)N * VPC, {0, 0, 0, 0, 0, 0, -1e30f});
        birth.assign((size_t)N, -1e30f);
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, vtx.size() * sizeof(StreamVertex),
                     vtx.data(), GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(StreamVertex), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(StreamVertex),
                              (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(StreamVertex),
                              (void*)(6 * sizeof(float)));
        glBindVertexArray(0);

        // programma con fade temporale (birth come attributo float)
        auto compile = [](GLenum type, const char* src) -> GLuint {
            GLuint s = glCreateShader(type);
            glShaderSource(s, 1, &src, nullptr);
            glCompileShader(s);
            GLint ok = GL_FALSE;
            glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                char log[1024];
                glGetShaderInfoLog(s, sizeof(log), nullptr, log);
                std::fprintf(stderr, "Compilazione shader chord stream fallita:\n%s\n", log);
                return 0;
            }
            return s;
        };
        char vs[1024];
        std::snprintf(vs, sizeof(vs), "%s%.1f%s",
                      CHORD_STREAM_VS, CHORD_FADE_S, CHORD_STREAM_VS2);
        static const char* FS =
            "#version 330 core\n"
            "in vec4 v_col;\n"
            "out vec4 frag;\n"
            "void main() { frag = v_col; }\n";
        GLuint v = compile(GL_VERTEX_SHADER, vs);
        GLuint f = compile(GL_FRAGMENT_SHADER, FS);
        prog = glCreateProgram();
        glAttachShader(prog, v);
        glAttachShader(prog, f);
        glLinkProgram(prog);
        glDeleteShader(v);
        glDeleteShader(f);
        GLint ok = GL_FALSE;
        glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[1024];
            glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
            std::fprintf(stderr, "Link programma chord stream fallito:\n%s\n", log);
            return false;
        }
        u_mvp = glGetUniformLocation(prog, "u_mvp");
        u_now = glGetUniformLocation(prog, "u_now");
        return prog != 0;
    }

    void destroy_gl() {
        if (vbo) glDeleteBuffers(1, &vbo);
        if (vao) glDeleteVertexArrays(1, &vao);
        if (prog) glDeleteProgram(prog);
        vao = vbo = prog = 0;
    }

    void reset() {
        head = 0; emitted = 0; base_index = 0; frac_acc = 0.0;
        dirty_start = dirty_len = 0; web_dirty = false;
        for (auto& v : vtx) v = {0, 0, 0, 0, 0, 0, -1e30f};
        for (auto& b : birth) b = -1e30f;
    }

    // Emette le corde per n nuove cifre. Le cifre vengono TUTTE consumate
    // (base_index avanza al ritmo di rate(t), l'HUD è onesto) ma solo le
    // ultime `draw_budget` diventano corde disegnate: a rate satura (~9M/s)
    // generare tutto sarebbe spreco puro — la finestra rolling ne mostra
    // comunque solo N, e si rinnova con le più recenti.
    void push(double t, const PiReader& r, long long n, long long draw_budget) {
        const size_t max_digits = r.decimal_digits();
        long long skip = n > draw_budget ? n - draw_budget : 0;
        base_index += (size_t)skip;
        const long long to_draw = n - skip;
        for (long long k = 0; k < to_draw; ++k) {
            size_t i = (size_t)base_index;   // cifra d[i]
            size_t j = i + 1;                // cifra d[i+1]
            if (j >= max_digits) return;     // file finito: resta stabilizzato
            int a = r.digit(i), b = r.digit(j);
            base_index++;
            add_chord(t, a, b);
            // ragnatela crescente: la corda entra anche nella matrice/layer
            // statico, ricontestualizzato ogni CHORD_REBUILD_EVERY corde
            chord_web.update(a, b);
            if (emitted % CHORD_REBUILD_EVERY == 0) web_dirty = true;
        }
    }

    void add_chord(double t, int da, int db) {
        // ancoraggi DISTRIBUITI lungo l'arco: la k-esima corda che tocca una
        // cifra usa il k-esimo punto del settore (round-robin deterministico)
        float p0[2], p1[2];
        chord_anchor(da, (int)(emitted % CHORD_SUBANCH), p0);
        chord_anchor(db, (int)((emitted * 7 + 3) % CHORD_SUBANCH), p1);
        // il colore è quello della cifra d'arrivo (stile degli esempi: il
        // "filo" entra nel settore del suo valore)
        const float* c = CHORD_CURRENT_PALETTE[db];

        const size_t base = head * VPC;
        StreamVertex* q = &vtx[base];
        // la corda viene triangolata direttamente in q (VPC vertici esatti)
        const int SEG = CHORD_BEZ_SEG;
        float mx = (p0[0] + p1[0]) * 0.5f, my = (p0[1] + p1[1]) * 0.5f;
        float ml = std::sqrt(mx * mx + my * my);
        if (ml < 1e-6f) return;   // corda degenerata: non consuma slot
        const float BOW = 0.62f;
        float cx = mx * BOW, cy = my * BOW;   // controllo verso il centro
        const float b = (float)t;
        int out = 0;
        float px = 0, py = 0, pnx = 0, pny = 0;
        for (int s = 0; s <= SEG; ++s) {
            float u = (float)s / SEG, v = 1.0f - u;
            float x = v * v * p0[0] + 2 * v * u * cx + u * u * p1[0];
            float y = v * v * p0[1] + 2 * v * u * cy + u * u * p1[1];
            float tx = 2 * v * (cx - p0[0]) + 2 * u * (p1[0] - cx);
            float ty = 2 * v * (cy - p0[1]) + 2 * u * (p1[1] - cy);
            float tl = std::sqrt(tx * tx + ty * ty);
            if (tl < 1e-9f) { tx = 1; ty = 0; tl = 1; }
            float nx = -ty / tl * CHORD_WIDTH, ny = tx / tl * CHORD_WIDTH;
            if (s > 0) {
                // alpha = ricchezza decrescente con l'indice di emissione
                float ca = chord_stream_alpha(emitted);
                q[out++] = {px + pnx, py + pny, c[0], c[1], c[2], ca, b};
                q[out++] = {px - pnx, py - pny, c[0], c[1], c[2], ca, b};
                q[out++] = {x + nx, y + ny, c[0], c[1], c[2], ca, b};
                q[out++] = {px - pnx, py - pny, c[0], c[1], c[2], ca, b};
                q[out++] = {x + nx, y + ny, c[0], c[1], c[2], ca, b};
                q[out++] = {x - nx, y - ny, c[0], c[1], c[2], ca, b};
            }
            px = x; py = y; pnx = nx; pny = ny;
        }
        // out == VPC per costruzione (SEG triangolazioni x 6 vertici)

        birth[head] = (float)t;
        head = (head + 1) % (size_t)N;
        emitted++;
        dirty_len += VPC;
        if (dirty_len > (int)vtx.size()) dirty_len = (int)vtx.size();
    }

    // Carica SOLO le corde nuove (il fade è nello shader: il resto non cambia)
    // e disegna l'intera finestra.
    void render(double t_now, GLint mvp_loc, const mat4 mvp) {
        if (emitted == 0) return;
        if (dirty_len > 0) {
            const size_t bytes = sizeof(StreamVertex);
            int start = (head - dirty_len / VPC + N) % N;   // in corde
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            if (start * VPC + dirty_len <= (int)vtx.size()) {
                glBufferSubData(GL_ARRAY_BUFFER, (GLintptr)start * VPC * bytes,
                                (GLsizeiptr)dirty_len * bytes, &vtx[(size_t)start * VPC]);
            } else {   // range avvolto: due tratti
                int first = (int)vtx.size() - start * VPC;
                glBufferSubData(GL_ARRAY_BUFFER, (GLintptr)start * VPC * bytes,
                                (GLsizeiptr)first * bytes, &vtx[(size_t)start * VPC]);
                glBufferSubData(GL_ARRAY_BUFFER, 0,
                                (GLsizeiptr)(dirty_len - first) * bytes, &vtx[0]);
            }
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            dirty_start = 0; dirty_len = 0;
        }
        // disegna SOLO le corde ancora vive (birth > t_now - FADE): all'inizio
        // (rate 0.1 cifre/s) sono 2-3 corde invece di 768k vertici — il grosso
        // del risparmio GPU nelle prime due minute, che è dove si guarda di più
        int nslots = (int)std::min<long long>(emitted, N);
        int start = (emitted < N) ? 0 : (int)head;
        while (nslots > 0 && birth[start] < (float)(t_now - (double)CHORD_FADE_S)) {
            start = (start + 1) % N;
            nslots--;
        }
        glUseProgram(prog);
        glUniformMatrix4fv(mvp_loc, 1, GL_FALSE, mvp);
        glUniform1f(u_now, (float)t_now);
        glBindVertexArray(vao);
        if (start + nslots <= N) {
            if (nslots > 0)
                glDrawArrays(GL_TRIANGLES, start * VPC, nslots * VPC);
        } else {
            glDrawArrays(GL_TRIANGLES, start * VPC, (N - start) * VPC);
            glDrawArrays(GL_TRIANGLES, 0, (start + nslots - N) * VPC);
        }
        glBindVertexArray(0);
        glUseProgram(0);
    }
};

#ifdef CHORD_TEST
#include <cstdlib>
#include "pendulum.h"   // coreografia + contatti (verificati qui sotto)
// Self-test: bilanciamento + costruzione mesh + controlli di forma
// + pacing cifre (rate_of) + coreografia pendolo (contatti/tempi/zoom).
int main(int argc, char* argv[]) {
    const char* path = (argc > 1) ? argv[1] : "pi100m.txt";
    PiReader r;
    if (!r.open(path)) return 1;
    ChordData cd;
    cd.accumulate(r, 1000000);

    long long mn = cd.freq[0], mx = cd.freq[0];
    for (int d = 0; d < 10; ++d) {
        std::printf("cifra %d: %lld (%.3f%%)\n", d, cd.freq[d], 100.0 * cd.freq[d] / cd.total);
        if (cd.freq[d] < mn) mn = cd.freq[d];
        if (cd.freq[d] > mx) mx = cd.freq[d];
    }
    double ratio = (double)mx / (double)mn;
    std::printf("rapporto max/min frequenze: %.4f\n", ratio);
    if (ratio > 1.03) return 1;

    std::vector<ChordVertex> sec, cho;
    chord_build_sectors(CHORD_PALETTES[1], sec);
    chord_build_static_chords(cd, CHORD_PALETTES[1], cho);
    std::printf("vertici settori: %zu\n", sec.size());
    std::printf("vertici corde statiche: %zu (curve)\n", cho.size());
    if (sec.size() != (size_t)(10 * CHORD_SECSUB * 6)) return 1;
    if (cho.empty()) return 1;

    // la mesh curva deve avere più vertici della vecchia retta (6 per corda)
    if (cho.size() <= 100 * 6) return 1;

    // ancoraggi: la distribuzione deve coprire l'arco del settore
    float a_first[2], a_last[2];
    chord_anchor(0, 0, a_first);
    chord_anchor(0, CHORD_SUBANCH - 1, a_last);
    float ang_f = std::atan2(a_first[1], a_first[0]);
    float ang_l = std::atan2(a_last[1], a_last[0]);
    float dang = std::fabs(ang_f - ang_l);
    if (dang < 0.05f) {   // ~2.9° minimo: gli ancoraggi devono coprire ~30°
        std::fprintf(stderr, "FALLITO: ancoraggi non distribuiti (%.4f rad)\n", dang);
        return 1;
    }
    std::printf("chord_test: OK (mesh curve + ancoraggi distribuiti)\n");

    // ---- 1) contatti: miss (distanza minima curva↔(2,0)) e zoom ----
    static const double miss_ref[3] = {1.8014e-4, 2.0914e-9, 8.4222e-10};
    bool ok = true;
    for (int i = 0; i < PEND_N_CONTACTS; ++i) {
        double miss = pend_miss(i);
        double z = pend_contact_zoom(i);
        // minimo numerico di |Z(θ)−(2,0)| attorno al contatto (ricerca
        // ternaria su [θ_c−span, θ_c+span], span che contiene δ*)
        double span = 2.0 * std::fabs(pend_cross_delta(i)) + 1e-6;
        auto dist2 = [](double th) {
            double dx = Pendulum::re(th) - 2.0, dy = Pendulum::im(th);
            return dx * dx + dy * dy;
        };
        double lo = pend_contact_th(i) - span, hi = pend_contact_th(i) + span;
        for (int it = 0; it < 300; ++it) {
            double third = (hi - lo) / 3.0;
            double m1 = lo + third, m2 = hi - third;
            if (dist2(m1) < dist2(m2)) hi = m2; else lo = m1;
        }
        double best = std::sqrt(dist2(0.5 * (lo + hi)));
        std::printf("contatto %d: k=%ld θ=%.4f rad (%.0fπ), miss=%.4e "
                    "(minimo numerico %.4e), zoom=%.3g, W=%.2f rad\n",
                    i + 1, PEND_CONTACT_K[i], pend_contact_th(i),
                    pend_contact_th(i) / PI_PEND, miss, best, z, pend_window(i));
        double rel = std::fabs(miss - miss_ref[i]) / miss_ref[i];
        if (rel > 0.05) { std::printf("FALLITO: miss%d fuori tolleranza (%.3f)\n", i, rel); ok = false; }
        double rel2 = std::fabs(best - miss) / miss;
        if (rel2 > 0.05) { std::printf("FALLITO: minimo numerico≠formula (%.3f)\n", rel2); ok = false; }
        if (z < 100.0 || z > 2.0e9) { std::printf("FALLITO: zoom%d fuori range\n", i); ok = false; }
    }

    // ---- 2) pacing: replica il loop principale (dt 1/60, slow globale) ----
    // t = tempo di SCENA (parametrizza rate_of); tw = tempo reale (quello
    // che l'utente guarda: gli eventi con slow 0.1 durano ~10 s reali
    // benché coprano solo ~3.2 s di scena).
    double t = 0.0, tw = 0.0, th = 0.0, dig = 0.0;
    double t8 = -1, t500 = -1, tend = -1, rate500 = -1;
    double tc[PEND_N_CONTACTS], ev_in[PEND_N_CONTACTS], ev_dur[PEND_N_CONTACTS];
    bool   in_ev[PEND_N_CONTACTS];
    for (int i = 0; i < PEND_N_CONTACTS; ++i) {
        tc[i] = ev_in[i] = ev_dur[i] = -1; in_ev[i] = false;
    }
    const double total = (double)r.decimal_digits();
    for (long frame = 0; frame < 60L * 420; ++frame) {
        double slow = pend_slow(th);
        double dts = (1.0 / 60.0) * slow;       // dt di scena (slow GLOBALE)
        t  += dts;
        tw += 1.0 / 60.0;                       // tempo reale
        th += pend_rate_at(th) * dts;
        double prev = dig;
        dig += rate_of(t) * dts;
        if (t8   < 0 && prev < 8.0   && dig >= 8.0)   t8 = tw;
        if (t500 < 0 && prev < 500.0 && dig >= 500.0) { t500 = tw; rate500 = rate_of(t); }
        if (tend < 0 && dig >= total) tend = tw;
        for (int i = 0; i < PEND_N_CONTACTS; ++i) {
            if (tc[i] < 0 && th >= pend_contact_th(i)) tc[i] = tw;
            double d = std::fabs(th - pend_contact_th(i)) / pend_window(i);
            if (!in_ev[i] && d < 1.0) { in_ev[i] = true; ev_in[i] = tw; }
            else if (in_ev[i] && d >= 1.0) { ev_dur[i] = tw - ev_in[i]; in_ev[i] = false; }
        }
        if (tend > 0 && th > pend_contact_th(2) + pend_window(2) + 100.0) break;
    }
    for (int i = 0; i < PEND_N_CONTACTS; ++i)
        if (in_ev[i]) ev_dur[i] = tw - ev_in[i];

    std::printf("pacing: cifra 8 a t=%.1f s, cifra 500 a t=%.1f s (rate %.0f), "
                "%s a t=%.1f s (tempi reali)\n", t8, t500, rate500,
                total < 1e6 ? "314159 cifre" : "file completo", tend);
    for (int i = 0; i < PEND_N_CONTACTS; ++i)
        std::printf("evento %d: contatto a t=%.1f s, durata close-up %.1f s\n",
                    i + 1, tc[i], ev_dur[i]);

    // vincoli di progetto (pacing richiesto dall'utente)
    struct { double v, lo, hi; const char* nome; } checks[] = {
        {t8,          10,   40, "cifra 8 (2.5 s/cifra)"},
        {t500,       100, 135, "cifra 500"},
        {rate500, 70, 140, "rate a 500 cifre (~100)"},
        {tend,       110, 260, "file esaurito"},
        {tc[0],       40,  75, "contatto 1"},
        {tc[1],      100, 170, "contatto 2"},
        {tc[2],      180, 330, "contatto 3"},
        {ev_dur[0],    5,  20, "durata evento 1"},
        {ev_dur[1],    5,  20, "durata evento 2"},
        {ev_dur[2],    5,  20, "durata evento 3"},
    };
    for (auto& c : checks) {
        bool pass = c.v >= c.lo && c.v <= c.hi && c.v > 0;
        std::printf("%s: %.1f (atteso %.0f..%.0f) %s\n", c.nome, c.v, c.lo, c.hi,
                    pass ? "OK" : "FALLITO");
        if (!pass) ok = false;
    }
    if (!ok) return 1;
    std::printf("chord_test: OK (contatti + pacing + coreografia)\n");
    return 0;
}
#endif
