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
//   - la SATURAZIONE delle corde cresce con la velocità: partenza desaturata,
//     full color quando la matrice 10×10 è ~satura (niente bianco prematuro,
//     il colore "si accende" man mano che lo streaming accelera);
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
static constexpr float CHORD_FADE_S   = 6.0f;   // durata fade corde in streaming
static constexpr double CHORD_SAT_MIN    = 0.10;  // saturazione iniziale delle corde
static constexpr double CHORD_SAT_DIGITS = 1.0e5; // decollo: sotto questa soglia tutto desaturato
static constexpr float  CHORD_STATIC_SAT = 0.55f; // saturazione del layer statico (contesto)

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

    // Reaccumula dal file (reset del tasto R).
    void reset(const PiReader& r) {
        for (int i = 0; i < 10; ++i)
            for (int j = 0; j < 10; ++j) trans[i][j] = 0;
        for (int d = 0; d < 10; ++d) freq[d] = 0;
        total = 0;
        accumulate(r, r.decimal_digits());
    }
};

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
            float alpha = 0.10f + 0.14f * ((float)c / (float)cmax);
            // desaturato verso il nero: il layer statico non deve bruciare in bianco
            float cs[3] = {palette[i][0] * CHORD_STATIC_SAT,
                           palette[i][1] * CHORD_STATIC_SAT,
                           palette[i][2] * CHORD_STATIC_SAT};
            chord_push_curved(out, p0, p1, cs, alpha, w);
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
    "layout(location = 3) in float a_sat;\n"
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
    "    v_col = vec4(a_col.rgb * a_sat, a_col.a * f * f);\n"   // fade quadratico + saturazione
    "}\n";

// Vertice dello stream: pos + rgba + istante di emissione + saturazione (8 float).
struct StreamVertex {
    float x, y, r, g, b, a, birth, sat;
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
    int       dirty_start = 0, dirty_len = 0;   // range da ricaricare sul VBO
    GLuint    vao = 0, vbo = 0;
    GLuint    prog = 0;
    GLint     u_mvp = -1, u_now = -1;

    bool init_gl() {
        vtx.assign((size_t)N * VPC, {0, 0, 0, 0, 0, 0, -1e30f, 0});
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
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(StreamVertex),
                              (void*)(7 * sizeof(float)));
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
        dirty_start = dirty_len = 0;
        for (auto& v : vtx) v = {0, 0, 0, 0, 0, 0, -1e30f, 0};
        for (auto& b : birth) b = -1e30f;
    }

    // Emette le corde per n nuove cifre. Le cifre vengono TUTTE consumate
    // (base_index avanza al ritmo di rate(t), l'HUD è onesto) ma solo le
    // ultime `draw_budget` diventano corde disegnate: a rate satura (~9M/s)
    // generare tutto sarebbe spreco puro — la finestra rolling ne mostra
    // comunque solo N, e si rinnova con le più recenti.
    void push(double t, const PiReader& r, const float palette[10][3],
              long long n, long long draw_budget) {
        const size_t max_digits = r.decimal_digits();
        // la matrice 10×10 è ~satura a ~86% del file: oltre, tutto full color
        const double sat_cap = 0.86 * (double)max_digits;
        long long skip = n > draw_budget ? n - draw_budget : 0;
        base_index += (size_t)skip;
        const long long to_draw = n - skip;
        for (long long k = 0; k < to_draw; ++k) {
            size_t i = (size_t)base_index;   // cifra d[i]
            size_t j = i + 1;                // cifra d[i+1]
            if (j >= max_digits) return;     // file finito: resta stabilizzato
            int a = r.digit(i), b = r.digit(j);
            base_index++;
            // saturazione crescente con le cifre consumate: 0.10 alla partenza,
            // 1.0 quando la matrice è ~satura (dopo le ~86M cifre)
            double x = ((double)base_index - CHORD_SAT_DIGITS) /
                       (sat_cap - CHORD_SAT_DIGITS);
            if (x < 0.0) x = 0.0;
            if (x > 1.0) x = 1.0;
            float sat = (float)(CHORD_SAT_MIN + (1.0 - CHORD_SAT_MIN) * x);
            add_chord(t, a, b, palette[a], sat);
        }
    }

    void add_chord(double t, int da, int db, const float col[3], float sat) {
        // ancoraggi DISTRIBUITI lungo l'arco: la k-esima corda che tocca una
        // cifra usa il k-esimo punto del settore (round-robin deterministico)
        float p0[2], p1[2];
        chord_anchor(da, (int)(emitted % CHORD_SUBANCH), p0);
        chord_anchor(db, (int)((emitted * 7 + 3) % CHORD_SUBANCH), p1);
        // il colore è quello della cifra d'arrivo (stile degli esempi: il
        // "filo" entra nel settore del suo valore)
        (void)col;
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
                q[out++] = {px + pnx, py + pny, c[0], c[1], c[2], 0.55f, b, sat};
                q[out++] = {px - pnx, py - pny, c[0], c[1], c[2], 0.55f, b, sat};
                q[out++] = {x + nx, y + ny, c[0], c[1], c[2], 0.55f, b, sat};
                q[out++] = {px - pnx, py - pny, c[0], c[1], c[2], 0.55f, b, sat};
                q[out++] = {x + nx, y + ny, c[0], c[1], c[2], 0.55f, b, sat};
                q[out++] = {x - nx, y - ny, c[0], c[1], c[2], 0.55f, b, sat};
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
        glUseProgram(prog);
        glUniformMatrix4fv(mvp_loc, 1, GL_FALSE, mvp);
        glUniform1f(u_now, (float)t_now);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, N * VPC);
        glBindVertexArray(0);
        glUseProgram(0);
    }
};

#ifdef CHORD_TEST
#include <cstdlib>
// Self-test: bilanciamento + costruzione mesh + controlli di forma.
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
    return 0;
}
#endif
