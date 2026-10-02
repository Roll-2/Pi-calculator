// ============================================================
// pendulum.h — pendolo composto Z(θ) = e^(iθ) + e^(iπθ)
//
// π è hardcoded a 17 cifre decimali (3.14159265358979323): oltre la 17ª
// cifra l'effetto sulla curva è trascurabile anche al 3° contatto
// (errore di fase ~6.5e-11 vs gap 1.2e-4).
//
// CONTATTI RAVVICINATI (UNICA FONTE per colori, camera e slow motion):
//   θ_c = 2πk con k = 7   (22/7)          miss ≈ 1.80e-4  → zoom ~4.0e3
//                k = 113 (355/113)        miss ≈ 2.09e-9  → zoom ~3.4e8
//                k = 33102 (103993/33102) miss ≈ 8.42e-10 → zoom ~8.6e8
// "miss" = distanza MINIMA tra la curva e il punto di nascita (2,0):
//   φ = Im Z(θ_c) è la fase residua, δ* = −φ/(1+π) il parametro in cui il
//   ramo uscente incrocia il livello y=0 e, ai termini dominanti,
//   miss = δ*² (verificato numericamente: il minimo cade a δ*).
// È IL MISS la separazione visibile in close-up — due linee quasi
// parallele (la curva che ritorna vs il ramo nascita) — NON la punta del
// ricciolo: a zoom elevato il ricciolo (scala φ) è una linea dritta e non
// si vede. I vertici sono OFFSET da (2,0): a zoom 1e8 la precisione
// float32 di posizioni ~2 non basterebbe per miss ~1e-9.
//
// Perché SOLO questi tre: k=106 (θ=212π) ha gap quasi identico a k=7 ma a
// 106 giri la curva è già densa e il graffiamento non è più discernibile;
// i ~290 membri della famiglia 113n+106 tra 226π e 66204π (gap decrescente
// fino a 1.2e-4) cadono tutti nella ragnatela ormai piena. Solo i tre
// convergenti "notevoli" sono quasi-contatti visibili a vista d'occhio.
//
// COLORE: la curva cambia tinta SOLO quando supera un contatto (indice
// 0..3 → 4 tinte scelte a mano, ordine ROSSO → AZZURRO → ARANCIONE →
// VIOLA): il cambio colore cade esattamente sull'istante del quasi-
// contatto, non a ogni 2π.
//
// COREOGRAFIA: rate per segmento R[4] (rad per secondo di scena), finestra
// evento W_i = 1.61·R_i (durata ~10 s con slow 0.1), ponti di rate lisci
// dopo ogni evento. Lo slow moltiplica il dt GLOBALE (vedi Pi-visual.cpp):
// pendolo E chord rallentano insieme a ogni close-up.
// Lo spettacolo dura 150 s reali (CH_SHOW_LEN in chord.h): i due primi
// contatti restano dove sono (52 s e 120 s) e lo SPRINT finale è
// accelerato a 28.000 rad/s, così il 3° quasi-contatto (66204π) cade a
// 145 s e il suo close-up finisce a 149,9 s: lo spettacolo si chiude sul
// climax.
//
// PASSO ADATTIVO: base 0.008 rad; raffinato vicino ai contatti
// (sagitta ≤ 1 px al massimo zoom in cui il punto è visibile) + campioni
// ESATTI su θ_c (punta del ricciolo) e sull'incrocio δ* (punto del miss).
// Costi: ~centinaia di punti in più per contatto, nessuna alluvione nel
// buffer.
//
// RAMO NASCITA: il tratto θ∈[0,0.06] (parte da (2,0)) ha un VBO dedicato
// sempre disegnato — il buffer circolare lo espelle dopo ~1048 rad, ma è
// l'altra metà del quasi-contatto al 3° contatto: senza, il close-up sarebbe
// vuoto.
// ============================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include <OpenGL/gl3.h>

// π hardcoded a 17 cifre decimali
static const double PI_PEND = 3.14159265358979323;

// Z(θ) = e^(iθ) + e^(iπθ)
struct Pendulum {
    static inline double re(double th) { return std::cos(th) + std::cos(PI_PEND * th); }
    static inline double im(double th) { return std::sin(th) + std::sin(PI_PEND * th); }
};

struct PendulumVertex {
    float x, y, r, g, b, a;
};

// ---------------- contatti (unica fonte) ----------------
static const int   PEND_N_CONTACTS = 3;
static const long  PEND_CONTACT_K[PEND_N_CONTACTS] = {7, 113, 33102};

static inline double pend_contact_th(int i) {
    return 2.0 * PI_PEND * (double)PEND_CONTACT_K[i];
}
// Im Z(θ_c): fase residua del quasi-contatto (con segno)
static inline double pend_contact_y(int i) {
    return Pendulum::im(pend_contact_th(i));
}
// δ* = −φ/(1+π): parametro in cui il ramo uscente incrocia y=0
static inline double pend_cross_delta(int i) {
    return -pend_contact_y(i) / (1.0 + PI_PEND);
}
// distanza MINIMA curva↔(2,0) al contatto i (= δ*²): la separazione
// realmente visibile in close-up
static inline double pend_miss(int i) {
    double d = pend_cross_delta(i);
    return d * d;
}
// θ in cui il ramo usente passa a distanza minima da (2,0)
static inline double pend_cross_th(int i) {
    return pend_contact_th(i) + pend_cross_delta(i);
}
// indice contatto superato da th (0..3): colore della curva
static inline int pend_contact_index(double th) {
    int n = 0;
    for (int i = 0; i < PEND_N_CONTACTS; ++i)
        if (th >= pend_contact_th(i)) n = i + 1;
    return n;
}

// Tinta dell'indice contatto n (0 = iniziale): 4 tinte scelte a mano.
//   0 nascita → ROSSO    1 (14π) → AZZURRO
//   2 (226π) → ARANCIONE 3 (66204π) → VIOLA
static const double PEND_TINT_H[4] = {  2.0, 200.0,  28.0, 282.0 };
static const double PEND_TINT_S[4] = {0.80,  0.70,  0.85,  0.55 };
static const double PEND_TINT_L[4] = {0.58,  0.66,  0.60,  0.62 };

// Tinta dell'indice contatto n (0 = iniziale, 3 = dopo il 3° contatto).
static inline void pendulum_contact_color(int n, float out[4]) {
    if (n < 0) n = 0;
    if (n > 3) n = 3;
    double h = PEND_TINT_H[n] / 60.0;
    const double s = PEND_TINT_S[n], l = PEND_TINT_L[n];
    double c = (1.0 - std::fabs(2.0 * l - 1.0)) * s;
    double x = c * (1.0 - std::fabs(std::fmod(h, 2.0) - 1.0));
    double r = 0, g = 0, b = 0;
    if      (h < 1) { r = c; g = x; }
    else if (h < 2) { r = x; g = c; }
    else if (h < 3) { g = c; b = x; }
    else if (h < 4) { g = x; b = c; }
    else if (h < 5) { r = x; b = c; }
    else            { r = c; b = x; }
    const double m = l - c * 0.5;
    out[0] = (float)(r + m);
    out[1] = (float)(g + m);
    out[2] = (float)(b + m);
    out[3] = 0.95f;
}

// ---------------- coreografia (funzioni di θ) ----------------
// Rate per segmento (rad per secondo di scena, senza slow):
//   0 → contatto 1 : 0.9    (piano: i primi digit si vedono crescere)
//   contatto 1 → 2 : 14
//   contatto 2 → 3 : 28000  (slog finale: la curva riempie il disco in
//                              ~20 s, così il 3° contatto arriva a 145 s
//                              e lo spettacolo finisce a 150 s)
//   dopo contatto 3 : 40    (rielaborazione morbida)
static const double PEND_R[4] = {0.9, 14.0, 28000.0, 40.0};
// Finestra evento: W = 1.61·R  →  durata evento ≈ 6.21·W/R ≈ 10 s
static const double PEND_W_K   = 1.61;
static const double PEND_SLOW_R = 0.10;            // slow minimo
// Ponti di rate (smoothstep) dopo l'evento i: [θ_i+W_i, +PEND_BRIDGE[i]]
// Dopo il 2° contatto il ponte è di 6.000 rad: il rate sale da 14 a
// 28.000 rad/s in ~7 s (il passaggio graduale all'ultimo atto).
static const double PEND_BRIDGE[PEND_N_CONTACTS] = {60.0, 6000.0, 60.0};
// Separazione visibile al contatto: frazione di mezza-altezza viewport
static const double PEND_ZOOM_FRAC = 0.30;         // zoom_i = 0.72/miss_i
static const double PEND_CAM_MARG  = 0.94;         // (2,0) entro 94% del half-width
// Passo base (rad/punto) e coefficiente del passo adattivo
static const double PEND_STEP_BASE = 0.008;
static const double PEND_STEP_K    = 0.0202;       // sagitta ≤ 1 px al zoom max

static inline double pend_smooth01(double s) {
    if (s <= 0.0) return 0.0;
    if (s >= 1.0) return 1.0;
    return s * s * (3.0 - 2.0 * s);
}

static inline double pend_window(int i) {          // semi-ampiezza θ dell'evento i
    return PEND_W_K * PEND_R[i];
}

// bump smoothstep centrato sul contatto i
static inline double pend_bump(double th, int i) {
    return pend_smooth01(1.0 - std::fabs(th - pend_contact_th(i)) / pend_window(i));
}

// zoom al contatto i: il MISS occupa PEND_ZOOM_FRAC della mezza-altezza
static inline double pend_contact_zoom(int i) {
    static double z[PEND_N_CONTACTS];
    static bool init = false;
    if (!init) {
        for (int k = 0; k < PEND_N_CONTACTS; ++k)
            z[k] = PEND_ZOOM_FRAC * 2.4 / pend_miss(k);
        init = true;
    }
    return z[i];
}

// slow globale: 1 fuori dagli eventi, PEND_SLOW_R al centro di ogni evento
static inline double pend_slow(double th) {
    double slow = 1.0;
    for (int i = 0; i < PEND_N_CONTACTS; ++i) {
        double d = std::fabs(th - pend_contact_th(i)) / pend_window(i);
        if (d < 1.0) {
            double s = 1.0 - (1.0 - PEND_SLOW_R) * (1.0 - pend_smooth01(d));
            if (s < slow) slow = s;
        }
    }
    return slow;
}

// zoom della camera: interpolazione LOGARITMICA (con Z fino a 6000 una
// interpolazione lineare salterebbe subito a zoom centinaia)
static inline double pend_zoom(double th) {
    double lz = 0.0;
    for (int i = 0; i < PEND_N_CONTACTS; ++i)
        lz += pend_bump(th, i) * std::log(pend_contact_zoom(i));
    return lz > 0.0 ? std::exp(lz) : 1.0;
}

// rate θ(θ) con ponti lisci tra i segmenti (continui dove slow = 1)
static inline double pend_rate_at(double th) {
    double R = PEND_R[0];
    for (int i = 0; i < PEND_N_CONTACTS; ++i) {
        double st = pend_contact_th(i) + pend_window(i);
        if (th > st)
            R += (PEND_R[i + 1] - R) * pend_smooth01((th - st) / PEND_BRIDGE[i]);
    }
    return R;
}

// centro orizzontale in COORDINI OFFSET da (2,0): tiene (2,0) entro
// PEND_CAM_MARG del half-width a qualunque zoom — niente più traslazioni
// che perdono il pendolo durante la discesa — con vincolo −2 al zoom 1
// (vista ampia centrata sul disco)
static inline double pend_cam_cx(double zoom, float aspect) {
    double half_w = 2.4 * (double)aspect / zoom;
    return -std::min(2.0, PEND_CAM_MARG * half_w);
}

// passo adattivo: sagitta ≤ 1 px al massimo zoom in cui il punto è visibile
// (distanza stimata da (2,0) con floor = miss del contatto più vicino)
static inline double pend_point_step(double th) {
    double s = PEND_STEP_BASE;
    for (int i = 0; i < PEND_N_CONTACTS; ++i) {
        double d = 4.14 * std::fabs(th - pend_contact_th(i));
        double g = pend_miss(i);
        if (d < g) d = g;
        double si = PEND_STEP_K * std::sqrt(d);
        if (si < s) s = si;
    }
    return s > 1e-9 ? s : 1e-9;
}

// θ minimo per lo scatto del close-up: un passo OLTRE l'incrocio y=0, così
// la testa disegnata supera il centro e il segmento finale attraversa tutta
// l'altezza. Senza il margine al 1° contatto il salto di θ per frame
// (rate 0.9·slow ≈ 1.4e-3 rad) era più piccolo del passo adattivo (4.8e-3
// rad): la curva si fermava ESATTAMENTE sul centro e la seconda linea
// mancava nella metà superiore, dove il ramo nascita la farebbe risaltare.
static inline double pend_shot_th(int i) {
    double tw = pend_cross_th(i);
    return tw + 1.1 * pend_point_step(tw);
}

// ---- stream animato con buffer circolare ----
// θ cresce verso theta_target; passo adattivo (pend_point_step) con snap
// ESATTO su θ_c (punta del ricciolo) e sull'incrocio δ* (punto del miss):
// la separazione visibile in close-up cade dove deve. I vertici sono
// offset da (2,0) (vedi testa file). CAP copre la coreografia con passo
// base (θ_max ≈ 208k rad → il wrap decima la coda).
struct PendulumStream {
    static constexpr double STEP = 0.008;   // passo base (rad/punto)
    static constexpr int    CAP  = 131072;  // punti nel buffer circolare (2^17)

    std::vector<PendulumVertex> pts;
    double theta_head = 0.0;               // θ dell'ultimo punto inserito
    double theta_max  = 1.0;
    int    tail = 0, head = 0, count = 0;  // buffer circolare
    int    dirty_start = 0, dirty_len = 0; // range da ricaricare sul VBO
    GLuint vao = 0, vbo = 0;

    void init_gl(double theta_max_total) {
        theta_max = theta_max_total;
        pts.assign(CAP, {0, 0, 0, 0, 0, 0});
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)CAP * sizeof(PendulumVertex),
                     pts.data(), GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(PendulumVertex), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(PendulumVertex),
                              (void*)(2 * sizeof(float)));
        glBindVertexArray(0);
    }

    void destroy_gl() {
        if (vbo) glDeleteBuffers(1, &vbo);
        if (vao) glDeleteVertexArrays(1, &vao);
        vao = vbo = 0;
    }

    void reset() {
        theta_head = 0.0; tail = head = count = 0; dirty_start = dirty_len = 0;
        for (auto& p : pts) p = {0, 0, 0, 0, 0, 0};
    }

    // Aggiunge punti finché θ non raggiunge il target (nessun ribasso: θ è
    // monotono). Passo = step_fn(θ) con snap: se un contatto cade entro il
    // prossimo passo, il punto cade ESATTAMENTE su θ_c. Colore = indice
    // dell'ultimo contatto superato (cambio tinta solo ai quasi-contatti).
    template <typename StepFn>
    int advance(double theta_target, StepFn step_fn) {
        if (theta_target < theta_head) return 0;
        int added = 0;
        for (;;) {
            if (theta_head >= theta_target) break;
            double s = step_fn(theta_head);
            if (!(s > 1e-9)) s = 1e-9;
            double next = theta_head + s;
            for (int i = 0; i < PEND_N_CONTACTS; ++i) {
                double tc = pend_contact_th(i);
                if (tc > theta_head && tc < next) next = tc;   // snap: punta ricciolo
                double tw = pend_cross_th(i);
                if (tw > theta_head && tw < next) next = tw;   // snap: incrocio (miss)
            }
            if (next > theta_target) break;
            theta_head = next;
            float c[4];
            pendulum_contact_color(pend_contact_index(theta_head), c);
            // offset da (2,0): precisione float32 piena anche a zoom 1e8
            pts[head] = {(float)(Pendulum::re(theta_head) - 2.0), (float)Pendulum::im(theta_head),
                         c[0], c[1], c[2], c[3]};
            head = (head + 1) % CAP;
            if (count < CAP) count++; else tail = (tail + 1) % CAP;
            added++;
        }
        dirty_len += added;   // la CPU ha scritto in coda: marca da ricaricare
        if (dirty_len > count) dirty_len = count;
        return added;
    }

    // Carica solo i punti nuovi e disegna la polilinea.
    // Richiede shader flat attivo e MVP già caricato.
    void render(GLint u_alpha_loc) {
        if (count == 0) return;
        if (dirty_len > 0) {
            int start = (head - dirty_len + CAP) % CAP;
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            if (start + dirty_len <= CAP) {
                glBufferSubData(GL_ARRAY_BUFFER, (GLintptr)start * sizeof(PendulumVertex),
                                (GLsizeiptr)dirty_len * sizeof(PendulumVertex), &pts[start]);
            } else {   // range avvolto: due tratti
                int first = CAP - start;
                glBufferSubData(GL_ARRAY_BUFFER, (GLintptr)start * sizeof(PendulumVertex),
                                (GLsizeiptr)first * sizeof(PendulumVertex), &pts[start]);
                glBufferSubData(GL_ARRAY_BUFFER, 0,
                                (GLsizeiptr)(dirty_len - first) * sizeof(PendulumVertex), &pts[0]);
            }
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            dirty_start = 0; dirty_len = 0;
        }
        glUniform1f(u_alpha_loc, 1.0f);
        glBindVertexArray(vao);
        if (count < CAP) {
            glDrawArrays(GL_LINE_STRIP, 0, head);
        } else if (tail < head) {
            glDrawArrays(GL_LINE_STRIP, tail, head - tail);
        } else {   // buffer pieno e avvolto: due draw
            glDrawArrays(GL_LINE_STRIP, tail, CAP - tail);
            glDrawArrays(GL_LINE_STRIP, 0, head);
        }
        glBindVertexArray(0);
    }
};

// ---- ramo nascita (VBO statico, SEMPRE disegnato) ----
// Tratto θ∈[0, 0.06] che parte da (2,0): è l'altra metà di ogni
// quasi-contatto. Il buffer circolare lo espelle dopo ~1048 rad (normale
// durante lo slog verso il 3° contatto), quindi ha un VBO dedicato.
// Vertici come offset da (2,0), come lo stream.
struct PendulumBirth {
    static constexpr double TH = 0.06;   // copre la vista a zoom 13 (half_ext 0.185)
    static constexpr int    N  = 601;    // passo 1e-4 rad
    GLuint vao = 0, vbo = 0;

    void init_gl() {
        std::vector<PendulumVertex> p(N);
        float c[4];
        pendulum_contact_color(0, c);     // tinta iniziale (prima dei contatti)
        for (int k = 0; k < N; ++k) {
            double th = TH * (double)k / (double)(N - 1);
            p[k] = {(float)(Pendulum::re(th) - 2.0), (float)Pendulum::im(th),
                    c[0], c[1], c[2], c[3]};
        }
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)N * sizeof(PendulumVertex),
                     p.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(PendulumVertex), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(PendulumVertex),
                              (void*)(2 * sizeof(float)));
        glBindVertexArray(0);
    }

    void destroy_gl() {
        if (vbo) glDeleteBuffers(1, &vbo);
        if (vao) glDeleteVertexArrays(1, &vao);
        vao = vbo = 0;
    }

    void render(GLint u_alpha_loc) {
        glUniform1f(u_alpha_loc, 1.0f);
        glBindVertexArray(vao);
        glDrawArrays(GL_LINE_STRIP, 0, N);
        glBindVertexArray(0);
    }
};
