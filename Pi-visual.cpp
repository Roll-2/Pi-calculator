// ============================================================
// Pi-visual.cpp — visualizzatore π a due viewport (SDL2 + OpenGL 3.3 core)
//
// Finestra unica 1600×900 divisa in due viewport:
//   sinistro : pendolo composto Z(θ) = e^(iθ) + e^(iπθ) con camera a keyframe
//   destro   : chord diagram delle cifre di π lette da pi100m.txt (mmap),
//              10 settori, corde tra cifre consecutive, rolling window con
//              fade, blending additivo
//
// Un solo clock t e una sola rate(t) monotona crescente (esponenziale che
// satura a t≈120 s) guidano entrambi i pannelli, così accelerano insieme.
//
// Tasti:  Esc  esci          (gli altri arrivano negli STEP successivi)
//
// Compilazione (macOS ARM, Homebrew):
//   make            (oppure il comando equivalente qui sotto)
//   clang++ -std=c++17 -O2 Pi-visual.cpp -o Pi-visual \
//     -I/opt/homebrew/include -I/opt/homebrew/include/SDL2 -Ivisual \
//     -L/opt/homebrew/lib -framework OpenGL -framework Cocoa \
//     -lSDL2 -lSDL2_ttf -lz
//
// Esecuzione:
//   ./Pi-visual              (default: legge pi100m.txt nella cwd)
// ============================================================
#define GL_SILENCE_DEPRECATION
#include <SDL.h>
#include <OpenGL/gl3.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "visual/camera.h"
#include "visual/chord.h"
#include "visual/pi_reader.h"

static const int WIN_W = 1600;
static const int WIN_H = 900;

// ------------------------- helper shader -------------------------
static GLuint compile_shader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = GL_FALSE;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Errore compilazione shader (%s):\n%s\n",
                     type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint make_program(const char* vs_src, const char* fs_src) {
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = GL_FALSE;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Errore link programma:\n%s\n", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

// Shader "flat": posizione + colore per vertice, MVP uniform.
static const char* FLAT_VS =
    "#version 330 core\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "layout(location = 1) in vec4 a_col;\n"
    "uniform mat4 u_mvp;\n"
    "out vec4 v_col;\n"
    "void main() {\n"
    "    gl_Position = u_mvp * vec4(a_pos, 0.0, 1.0);\n"
    "    v_col = a_col;\n"
    "}\n";
static const char* FLAT_FS =
    "#version 330 core\n"
    "in vec4 v_col;\n"
    "uniform float u_alpha;\n"
    "out vec4 frag;\n"
    "void main() { frag = vec4(v_col.rgb, v_col.a * u_alpha); }\n";

struct FlatShader {
    GLuint prog = 0;
    GLint  u_mvp = -1;
    GLint  u_alpha = -1;
    bool init() {
        prog = make_program(FLAT_VS, FLAT_FS);
        if (!prog) return false;
        u_mvp = glGetUniformLocation(prog, "u_mvp");
        u_alpha = glGetUniformLocation(prog, "u_alpha");
        return true;
    }
};

// ------------------------- clock e rate condivisi (provisionali, si tarano allo STEP 9) -------------------------
// rate(t) = corde/secondo per il chord stream; il pendolo usa θ̇ = K_θ · rate(t).
// Esponenziale che satura a t≈120 s: l'integrale su [0,120] è ≈ 100M cifre
// (il file di default), quindi l'animazione "significativa" dura ~2 minuti.
static const double RATE_A   = 184.0;   // ampiezza iniziale (rate(0)=0, crescita lenta)
static const double RATE_K   = 0.09;    // costante esponenziale
static const double RATE_MAX = 9.0e6;   // saturazione (raggiunta a t≈120 s)
static inline double rate_of(double t) {
    double r = RATE_A * (std::exp(RATE_K * t) - 1.0);
    return r < RATE_MAX ? r : RATE_MAX;
}

int main(int argc, char* argv[]) {
    const char* pi_path = "pi100m.txt";   // argomento --pi <file> per cambiarlo
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--pi" && i + 1 < argc) {
            pi_path = argv[++i];
        } else {
            std::fprintf(stderr, "Uso: %s [--pi file_cifre]\n", argv[0]);
            return 1;
        }
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "SDL_Init fallito: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Window* win = SDL_CreateWindow(
        "Pi-visual — pendolo composto + chord diagram di π",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIN_W, WIN_H,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) {
        std::fprintf(stderr, "SDL_CreateWindow fallito: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_GLContext gl = SDL_GL_CreateContext(win);
    if (!gl) {
        std::fprintf(stderr, "SDL_GL_CreateContext fallito: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    SDL_GL_SetSwapInterval(1);  // vsync

    FlatShader flat;
    if (!flat.init()) {
        std::fprintf(stderr, "Inizializzazione shader fallita\n");
        SDL_GL_DeleteContext(gl);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    // ---- chord diagram (viewport destro): lettura mmap + mesh statiche ----
    PiReader pi_reader;
    if (!pi_reader.open(pi_path)) {
        std::fprintf(stderr, "Impossibile leggere il file π: %s\n", pi_path);
        SDL_GL_DeleteContext(gl);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    ChordData chord_data;
    chord_data.accumulate(pi_reader, pi_reader.decimal_digits());

    std::vector<ChordVertex> chord_sec_v, chord_chd_v;
    chord_build_sectors(CHORD_PALETTE_TMP, chord_sec_v);
    chord_build_static_chords(chord_data, CHORD_PALETTE_TMP, chord_chd_v);

    GLuint sec_vao = 0, sec_vbo = 0, chd_vao = 0, chd_vbo = 0;
    auto upload_mesh = [](std::vector<ChordVertex>& v, GLuint* vao, GLuint* vbo) {
        glGenVertexArrays(1, vao);
        glBindVertexArray(*vao);
        glGenBuffers(1, vbo);
        glBindBuffer(GL_ARRAY_BUFFER, *vbo);
        glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(ChordVertex), v.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(ChordVertex), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(ChordVertex), (void*)(2 * sizeof(float)));
        glBindVertexArray(0);
    };
    upload_mesh(chord_sec_v, &sec_vao, &sec_vbo);
    upload_mesh(chord_chd_v, &chd_vao, &chd_vbo);
    const GLsizei chord_sec_v_count = (GLsizei)chord_sec_v.size();
    const GLsizei chord_chd_v_count = (GLsizei)chord_chd_v.size();

    // ---- streaming delle corde (STEP 6): rolling window con fade ----
    // Ancoraggi precalcolati delle 10 cifre (evita trig per ogni corda emessa).
    float anchors[10][2];
    for (int d = 0; d < 10; ++d) {
        float a = chord_digit_angle(d);
        anchors[d][0] = CHORD_RADIUS * std::cos(a);
        anchors[d][1] = CHORD_RADIUS * std::sin(a);
    }
    ChordStream stream;
    if (!stream.init_gl()) {
        std::fprintf(stderr, "Inizializzazione VBO streaming fallita\n");
        SDL_GL_DeleteContext(gl);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    chord_sec_v.clear(); chord_sec_v.shrink_to_fit();
    chord_chd_v.clear(); chord_chd_v.shrink_to_fit();

    // ---- triangolo di prova per la camera (STEP 4: verifica visiva) ----
    GLuint tri_vao = 0, tri_vbo = 0;
    {
        const float verts[] = {
            // x      y      r     g     b     a
            -0.6f, -0.5f,  1.0f, 0.3f, 0.3f, 1.0f,
             0.6f, -0.5f,  0.3f, 1.0f, 0.4f, 1.0f,
             0.0f,  0.7f,  0.4f, 0.5f, 1.0f, 1.0f,
        };
        glGenVertexArrays(1, &tri_vao);
        glBindVertexArray(tri_vao);
        glGenBuffers(1, &tri_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, tri_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));
        glBindVertexArray(0);
    }

    // Camera a keyframe di prova: orbita x e dolly-in in 12 secondi.
    const Keyframe demo_kf[] = {
        {0.0,  -1.0f, 0.0f, 1.0f},
        {12.0,  1.0f, 0.0f, 2.0f},
    };
    const int demo_nkf = 2;

    Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 t_start = SDL_GetPerformanceCounter();

    bool running = true;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
            else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) running = false;
        }

        double t = (double)(SDL_GetPerformanceCounter() - t_start) / (double)freq;

        int w = 0, h = 0;
        SDL_GL_GetDrawableSize(win, &w, &h);
        int half = w / 2;

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // ---- viewport sinistro: triangolo rotante con camera a keyframe ----
        glViewport(0, 0, half, h);
        glClearColor(0.03f, 0.03f, 0.05f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        {
            Keyframe k = keyframe_eval(demo_kf, demo_nkf, t);
            mat4 P, V, VP;
            mat4_perspective(P, 50.0f * 3.14159265f / 180.0f, (float)half / (float)h, 0.1f, 100.0f);
            mat4_lookat(V, {k.x, k.y, 5.0f / k.zoom}, {k.x, k.y, 0.0f}, {0, 1, 0});
            mat4_mul(VP, P, V);

            float ang = (float)t * 0.8f;
            float ca = std::cos(ang), sa = std::sin(ang);
            mat4 M;
            mat4_identity(M);
            M[0] = ca; M[1] = sa;      // rotazione attorno a Z (column-major)
            M[4] = -sa; M[5] = ca;
            mat4 MVP;
            mat4_mul(MVP, VP, M);

            glUseProgram(flat.prog);
            glUniformMatrix4fv(flat.u_mvp, 1, GL_FALSE, MVP);
            glBindVertexArray(tri_vao);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBindVertexArray(0);
            glUseProgram(0);
        }

        // ---- chord streaming: quante corde emettere in questo frame ----
        static double t_prev = 0.0;
        double dt = t - t_prev;
        if (dt < 0.0 || dt > 0.1) dt = 0.1;   // clamp (pausa/lag: nessun scatto)
        t_prev = t;

        // ---- chord streaming: quante corde emettere in questo frame ----
        stream.frac_acc += rate_of(t) * dt;
        long long to_emit = (long long)stream.frac_acc;
        if (to_emit > 0) {
            if (to_emit > 500000) to_emit = 500000;   // tetto per frame (burst)
            stream.frac_acc -= (double)to_emit;
            stream.push(t, pi_reader, anchors, CHORD_PALETTE_TMP, to_emit);
        }

        // ---- viewport destro: chord diagram statico (STEP 5) ----
        glViewport(half, 0, w - half, h);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        {
            float aspect = (float)(w - half) / (float)h;
            mat4 O;
            mat4_ortho(O, -aspect, aspect, -1.0f, 1.0f, -1.0f, 1.0f);

            glUseProgram(flat.prog);
            glUniformMatrix4fv(flat.u_mvp, 1, GL_FALSE, O);

            // settori: alpha blending standard
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glUniform1f(flat.u_alpha, 0.6f);
            glBindVertexArray(sec_vao);
            glDrawArrays(GL_TRIANGLES, 0, (GLint)chord_sec_v_count);

            // corde statiche: additivo, molto tenui (contesto per lo streaming)
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            glUniform1f(flat.u_alpha, 0.10f);
            glBindVertexArray(chd_vao);
            glDrawArrays(GL_TRIANGLES, 0, (GLint)chord_chd_v_count);

            // corde in streaming: additivo, alpha per-vertex con fade temporale
            glUniform1f(flat.u_alpha, 1.0f);
            stream.render(t, flat.u_mvp, O);

            glBindVertexArray(0);
            glUseProgram(0);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }

        SDL_GL_SwapWindow(win);
    }

    glDeleteBuffers(1, &tri_vbo);
    glDeleteVertexArrays(1, &tri_vao);
    glDeleteBuffers(1, &sec_vbo);
    glDeleteVertexArrays(1, &sec_vao);
    glDeleteBuffers(1, &chd_vbo);
    glDeleteVertexArrays(1, &chd_vao);
    stream.destroy_gl();
    glDeleteProgram(flat.prog);
    pi_reader.close();
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
