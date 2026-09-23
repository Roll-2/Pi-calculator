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
//
// COLORE: ogni volta che θ attraversa un multiplo pari di π (un "passaggio
// ravvicinato" di e^(iθ) nei pressi di (2,0)) la curva cambia tinta, così il
// cambio colore è visibile proprio nel momento in cui le due branche si
// sfiorano. La tinta N-esima è presa da una ruota HSL (hue = 282° + N·47°,
// satura 78%, lum 62%) convertita in RGB: viola → giallo → verde → ciano...
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

// Tinta del passaggio N (0, 1, 2, ...): ruota HSL con passo 47° (irrazionale
// rispetto al periodo, quindi passi consecutivi hanno sempre tinte diverse).
static inline void pendulum_pass_color(long n, float out[4]) {
    double h = std::fmod(282.0 + 47.0 * (double)n, 360.0) / 60.0;
    const double s = 0.78, l = 0.62;
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

// Passaggio ravvicinato N per un θ: N = floor(θ / (2π)) (θ ≥ 0).
static inline long pendulum_pass_index(double th) {
    if (th < 0) th = 0;
    return (long)(th / (2.0 * PI_PEND));
}

// ---- stream animato con buffer circolare ----
// θ cresce verso theta_target; ogni STEP radianti si aggiunge un punto.
// STEP 0.008 rad: a zoom 190 (secondo dolly-in) 0.008·π ≈ 0.025 px — le
// spirali restano lisce al massimo ingrandimento. La capacità copre l'intera
// animazione (θ_max ≈ 226π ≈ 710 rad → ~89k punti), il wrap è solo di sicurezza.
struct PendulumStream {
    static constexpr double STEP = 0.008;  // rad per punto
    static constexpr int    CAP  = 131072; // punti nel buffer circolare (2^17)

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
    // monotono). Il colore è quello del passaggio ravvicinato N = floor(θ/2π):
    // a ogni sfioramento di (2,0) la curva cambia tinta. Ritorna i punti aggiunti.
    int advance(double theta_target) {
        if (theta_target < theta_head) return 0;
        int added = 0;
        while (theta_head + STEP <= theta_target) {
            theta_head += STEP;
            float c[4];
            pendulum_pass_color(pendulum_pass_index(theta_head), c);
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
