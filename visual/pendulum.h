// ============================================================
// pendulum.h — pendolo composto Z(θ) = e^(iθ) + e^(iπθ)
//
// π è hardcoded a 17 cifre decimali (3.14159265358979323): oltre la 17ª
// cifra l'effetto sulla curva sarebbe sottopixel a qualunque zoom della
// coreografia.
//
// La curva è famosa per le "quasi-chiusure" vicino a (2, 0): quando θ è un
// multiplo pari di π, e^(iθ) = 1, e la seconda fase πθ ricade vicino a un
// multiplo di 2π con l'approssimazione di π con i convergenti di 22/7 e
// 355/113:
//   θ  =  14π  →  δ = 2π(22 − 7π)    ≈ 0.056 rad  (gap ~0.056, visibile)
//   θ  = 226π  →  δ = 2π(355 − 113π) ≈ 0.0002 rad (gap ~200× più sottile)
// La coreografia della camera (STEP 8) punta proprio a questi due punti.
// ============================================================
#pragma once

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

// Colore del punto θ: gradiente ciano → magenta lungo la curva.
static inline void pendulum_color(double th, double th_max, float out[4]) {
    double s = th_max > 0 ? th / th_max : 0.0;
    if (s < 0) s = 0; if (s > 1) s = 1;
    out[0] = (float)(0.35 + 0.50 * s);
    out[1] = (float)(0.80 - 0.30 * s);
    out[2] = (float)(1.00 - 0.10 * s);
    out[3] = 0.9f;
}

// ---- polilinea statica per θ ∈ [0, th_max] (STEP 7) ----
static inline void pendulum_build_strip(double th_max, double step,
                                        std::vector<PendulumVertex>& out) {
    out.clear();
    out.reserve((size_t)(th_max / step) + 2);
    for (double th = 0.0; th < th_max; th += step) {
        float c[4];
        pendulum_color(th, th_max, c);
        out.push_back({(float)Pendulum::re(th), (float)Pendulum::im(th),
                       c[0], c[1], c[2], c[3]});
    }
    float c[4];
    pendulum_color(th_max, th_max, c);
    out.push_back({(float)Pendulum::re(th_max), (float)Pendulum::im(th_max),
                   c[0], c[1], c[2], c[3]});
}

// ---- stream animato con buffer circolare (STEP 8) ----
// θ cresce verso theta_target; ogni STEP radianti si aggiunge un punto.
// La capacità copre l'intera animazione (θ_max ≈ 226π ≈ 710 rad → ~35.500
// punti con STEP 0.02), quindi il wrap è solo di sicurezza.
struct PendulumStream {
    static constexpr double STEP = 0.02;   // rad per punto (~50 punti/radiante)
    static constexpr int    CAP  = 65536;  // punti nel buffer circolare

    std::vector<PendulumVertex> pts;
    double theta_head = 0.0;               // θ dell'ultimo punto inserito
    double theta_max  = 1.0;               // per il gradiente di colore
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
    // monotono). Ritorna il numero di punti aggiunti.
    int advance(double theta_target) {
        if (theta_target < theta_head) return 0;
        int added = 0;
        while (theta_head + STEP <= theta_target) {
            theta_head += STEP;
            float c[4];
            pendulum_color(theta_head, theta_max, c);
            pts[head] = {(float)Pendulum::re(theta_head), (float)Pendulum::im(theta_head),
                         c[0], c[1], c[2], c[3]};
            head = (head + 1) % CAP;
            if (count < CAP) count++; else tail = (tail + 1) % CAP;
            added++;
        }
        dirty_len += added;   // la CPU ha scritto in coda: marca da ricaricare
        if (dirty_len > count) dirty_len = count;
        return added;
    }

    // Carica solo i punti nuovi e disegna la polilinea (line width 1 su core).
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
            // dati contigui in [0, head): un solo draw
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
