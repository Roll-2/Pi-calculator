// ============================================================
// Pi-visual.cpp — visualizzatore π a due viewport (SDL2 + OpenGL 3.3 core)
//
// Finestra unica 1600×900 divisa in due viewport:
//   sinistro : pendolo composto Z(θ) = e^(iθ) + e^(iπθ), camera a EVENTI:
//              close-up rapidi e mirati sui tre quasi-contatti con (2,0)
//              — 14π, 226π, 66204π (miss 1.8e-4 / 2.1e-9 / 8.4e-10, zoom
//              ~4e3 / 3.4e8 / 8.6e8) — con slow motion GLOBALE sull'intera
//              scena; la curva cambia colore solo a ogni quasi-contatto
//   destro   : chord diagram a RAGNATELA CRESCENTE delle cifre di π lette
//              da pi_314159.txt (mmap): la matrice parte VUOTA e le corde
//              curve (Bézier) si accumulano man mano; le prime ricche di
//              colore, le successive sempre più flebili (niente bianco),
//              le recenti brillano in primo piano con fade
//
// Un solo clock di SCENA t (comprimibile per i test con SCALE) guida
// entrambi i pannelli, che sono legati a θ e non a due curve di tempo
// indipendenti: rate_cifre(θ) = C(θ)·R(θ) (0.5 cifre/s all'inizio, poi la
// stessa accelerazione del pendolo) e θ̇ = R(θ)·slow(θ). Il moltiplicatore
// slow(θ) — attivo solo entro le finestre dei tre contatti — scala il dt
// GLOBALE: a ogni close-up L'INTERA simulazione rallenta (pendolo + chord
// insieme). Finiti i dati la scena si CONGELA (dt = 0: t e il fade delle
// corde si fermano, il tappeto finale resta colorato).
//
// Tasti:  Esc   esci
//         0-3   palette (0 provvisoria, 1 Botanica, 2 Alba, 3 Notturna)
//         S     screenshot PNG in pictures/ (fallback: $HOME)
//         R     reset del clock e riavvio dall'inizio del file π
//         F     fullscreen on/off
//
// Compilazione (macOS ARM, Homebrew):
//   make            (oppure il comando equivalente qui sotto)
//   clang++ -std=c++17 -O2 Pi-visual.cpp -o Pi-visual \
//     -I/opt/homebrew/include -I/opt/homebrew/include/SDL2 -Ivisual \
//     -L/opt/homebrew/lib -framework OpenGL -framework Cocoa \
//     -lSDL2 -lSDL2_ttf -lz
//
// Esecuzione:
//   ./Pi-visual              (default: legge pi_314159.txt nella cwd)
// ============================================================
#define GL_SILENCE_DEPRECATION
#include <SDL.h>
#include <OpenGL/gl3.h>

#ifndef GL_MULTISAMPLE
#define GL_MULTISAMPLE 0x809D
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <sys/stat.h>

#include "visual/camera.h"
#include "visual/chord.h"
#include "visual/hud.h"
#include "visual/pendulum.h"
#include "visual/pi_reader.h"
#include "visual/screenshot.h"

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

// ------------------------- clock e rate condivisi -------------------------
// UN SOLO orologio di scena t guida entrambi i pannelli.
//   - Le cifre: rate(θ) in chord.h — CIFRE PER RADIANTE di θ, cioè
//     C(θ)·R(θ): 0.5 cifre/s (1 cifra ogni 2 s) all'inizio, poi la C(θ)
//     insegue l'accelerazione del pendolo. C1 è calibrato sul file: le
//     cifre disponibili finiscono alla FINE DEL TERZO CLOSE-UP.
//   - Il pendolo: θ̇ = R(θ)·slow(θ) con rate per segmento e close-up
//     lenti ai tre contatti (pendulum.h: contatti, finestre, camera).
//   - slow(θ) scala il dt di SCENA (quindi anche rate·dt): a ogni
//     close-up rallenta l'intera simulazione.
//   - Finite le cifre (o la coreografia) la scena si blocca: dt = 0.
//
// Il fattore SCALE comprime il tempo per i test (1.0: tempi reali).
#if 1
static const double SCALE = 1.0;    // tempi reali (0.5 cifre/s, contatti a ~52/120/244 s)
#else
static const double SCALE = 0.1;    // TEST: scena in slow-motion 10x (debug ravvicinato)
#endif
//
// Formattazione con separatori delle migliaia (it-IT: 1234567 -> "1.234.567")
static std::string fmt_group(long long v) {
    std::string s = std::to_string(v), out;
    int c = 0;
    for (int i = (int)s.size() - 1; i >= 0; --i) {
        out += s[i];
        if (++c % 3 == 0 && i > 0) out += '.';
    }
    std::reverse(out.begin(), out.end());
    return out;
}

int main(int argc, char* argv[]) {
    const char* pi_path = "pi_314159.txt";   // argomento --pi <file> per cambiarlo
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
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);      // anti-aliasing
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 2);      // 2x MSAA (GPU più leggera)

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

    ::mkdir("pictures", 0755);   // per gli screenshot (EEXIST è ok, ignorato)

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
    // ragnatela crescente: la matrice statica parte VUOTA e si riempie con lo
    // streaming (il diagramma cresce davanti all'utente, non è un'anteprima)
    chord_web.clear();

    std::vector<ChordVertex> chord_sec_v, chord_chd_v;
    chord_build_sectors(CHORD_CURRENT_PALETTE, chord_sec_v);
    chord_build_static_chords(chord_web, CHORD_CURRENT_PALETTE, chord_chd_v);

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
    GLsizei chord_sec_v_count = (GLsizei)chord_sec_v.size();
    GLsizei chord_chd_v_count = (GLsizei)chord_chd_v.size();
    chord_sec_v.clear(); chord_sec_v.shrink_to_fit();
    chord_chd_v.clear(); chord_chd_v.shrink_to_fit();

    // Ricostruzione delle mesh statiche: palette (tasti 0-3) o nuova ragnatela.
    auto rebuild_static_meshes = [&]() {
        std::vector<ChordVertex> sv, cv;
        chord_build_sectors(CHORD_CURRENT_PALETTE, sv);
        chord_build_static_chords(chord_web, CHORD_CURRENT_PALETTE, cv);
        glBindBuffer(GL_ARRAY_BUFFER, sec_vbo);
        glBufferData(GL_ARRAY_BUFFER, sv.size() * sizeof(ChordVertex), sv.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, chd_vbo);
        glBufferData(GL_ARRAY_BUFFER, cv.size() * sizeof(ChordVertex), cv.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        chord_sec_v_count = (GLsizei)sv.size();
        chord_chd_v_count = (GLsizei)cv.size();
    };

    // ---- streaming delle corde (STEP 6): rolling window, fade in shader ----
    ChordStream stream;
    if (!stream.init_gl()) {
        std::fprintf(stderr, "Inizializzazione VBO streaming fallita\n");
        SDL_GL_DeleteContext(gl);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    // ---- HUD (STEP 10): SDL_ttf + cache di texture (3 corpi: normale, piccolo, cifre) ----
    HudText hud;
    if (!hud.init(22, 16, 52)) {
        std::fprintf(stderr, "Inizializzazione HUD fallita\n");
        stream.destroy_gl();
        SDL_GL_DeleteContext(gl);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    // ---- pendolo animato: stream circolare + coreografia in spazio-θ ----
    // Il pendolo NON usa il rate delle cifre: cresce a R(θ) rad/s (rate per
    // segmento, pendulum.h) modulato dalla slow motion globale. La
    // coreografia raggiunge i tre quasi-contatti —
    //   th_1 =  14π (k=7):      miss 1.80e-4,  zoom ~4.0e3
    //   th_2 = 226π (k=113):    miss 2.09e-9,  zoom ~3.4e8
    //   th_3 = 66204π (k=33102): miss 8.42e-10, zoom ~8.6e8
    // — con tempi ~52 / 120 / 244 s (verificati da CHORD_TEST).
    const double THETA_MAX = pend_contact_th(2) + pend_window(2) + 120.0 * PEND_R[3];
    const double total_digits = (double)pi_reader.decimal_digits();
    // Coefficiente cifre/rad calibrato sul file: le cifre disponibili
    // finiscono alla FINE DEL TERZO CLOSE-UP, così lo spettacolo si chiude
    // sul climax e i due pannelli restano coerenti (file enormi: satura).
    chord_set_coefficient(chord_c1_for(total_digits));
    std::printf("rate cifre: %.2f cifre/s iniziali, %.3f cifre/rad dopo il rodaggio, "
                "%lld cifre disponibili, fine a θ=%.0f rad\n",
                CH_RATE_START, chord_c1, (long long)total_digits, CH_TH_END);
    PendulumStream pend_stream;
    pend_stream.init_gl(THETA_MAX);
    PendulumBirth pend_birth;              // ramo nascita (2,0): sempre in quadro
    pend_birth.init_gl();
    // passo adattivo: raffinato solo entro ~0.04 rad dai contatti
    auto step_for = [](double th) { return pend_point_step(th); };

    Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 t_start = SDL_GetPerformanceCounter();
    double theta_cur = 0.0;     // θ corrente del pendolo (detta dal rate R(θ))
    double t = 0.0;             // tempo di SCENA (dopo lo slow globale)

    // Verifica geometrica automatica (PI_VISUAL_CHECK=1): a t=20 s misura nel
    // framebuffer centratura dell'anello, varietà di colore delle corde e
    // visibilità del pendolo, e salva un PNG di controllo in pictures/.
    const bool selfcheck = std::getenv("PI_VISUAL_CHECK") != nullptr;
    bool selfcheck_done = false;      // parte geometrica (prima dell'HUD)
    bool selfcheck_hud_done = false;  // parte etichette (dopo l'HUD)

    bool fullscreen = false;
    bool screenshot_requested = false;
    bool reset_requested = false;
    bool selfcheck_tint_done = false;       // parte tinte (dopo ~400 cifre)
    bool closeup_shot[3] = {false, false, false};  // screenshot a ogni close-up

    bool running = true;
    bool finished = false;   // BLOCCO FINALE: cifre finite (o θ a fondo) → scena ferma
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
            else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) running = false;
            else if (ev.type == SDL_KEYDOWN) {
                int pk = -1;
                switch (ev.key.keysym.sym) {
                    case SDLK_0: pk = 0; break;
                    case SDLK_1: pk = 1; break;
                    case SDLK_2: pk = 2; break;
                    case SDLK_3: pk = 3; break;
                    case SDLK_s: screenshot_requested = true; break;
                    case SDLK_r: reset_requested = true; break;
                    case SDLK_f:
                        fullscreen = !fullscreen;
                        SDL_SetWindowFullscreen(win, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                        break;
                    default: break;
                }
                if (pk >= 0) {
                    CHORD_CURRENT_PALETTE = CHORD_PALETTES[pk];
                    rebuild_static_meshes();
                }
            }
        }

        // orologio grezzo (compressibile per i test via SCALE) + clamp
        double t_raw = SCALE * (double)(SDL_GetPerformanceCounter() - t_start) / (double)freq;
        static double t_raw_prev = 0.0;
        double dt_raw = t_raw - t_raw_prev;
        if (dt_raw < 0.0 || dt_raw > 0.1) dt_raw = 0.1;   // clamp (pausa/lag: nessuno scatto)
        t_raw_prev = t_raw;
        // SLOW GLOBALE ai close-up: UN dt di scena per entrambi i pannelli —
        // a ogni quasi-contatto del pendolo rallenta anche il chord
        double slow = pend_slow(theta_cur);
        double dt = dt_raw * slow;
        // BLOCCO FINALE: a scena finita dt = 0 — t si ferma (quindi anche il
        // fade delle corde in streaming: il tappeto finale resta vivo e
        // colorato, senza sparire) e θ non avanza più (niente loop infinito).
        if (finished) dt = 0.0;
        t += dt;                              // t = tempo di scena (HUD, rate, fade)

        int w = 0, h = 0;
        SDL_GL_GetDrawableSize(win, &w, &h);
        int half = w / 2;

        glEnable(GL_BLEND);
        glEnable(GL_MULTISAMPLE);   // anti-aliasing delle corde (2x MSAA)
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Un solo clear per frame sull'intera finestra: glClear ignora il
        // viewport (senza scissor test), quindi i clear per-viewport si
        // cancellano a vicenda e il pendolo risultava invisibile.
        glViewport(0, 0, w, h);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // ---- viewport sinistro: pendolo con coreografia in spazio-θ ----
        glViewport(0, 0, half, h);
        {
            // 1) θ̇ = R(θ)·slow(θ): rate per segmento (piano all'inizio,
            //    sprint nello slog verso il 3° contatto) × slow agli eventi;
            //    slow è lo STESSO moltiplicatore del dt del chord
            theta_cur += pend_rate_at(theta_cur) * dt;   // dt già di scena
            pend_stream.advance(theta_cur, step_for);

            // 2) camera: zoom LOGARITMICO sui tre contatti (con zoom fino a
            //    1e8 una rampa lineare salterebbe subito a milioni), centro
            //    funzione dello zoom in COORDINI OFFSET da (2,0) — dove i
            //    vertici del pendolo sono memorizzati — così (2,0), e quindi
            //    la punta del pendolo, resta in quadro durante TUTTA la
            //    discesa: niente più traslazioni che perdono il pendolo.
            const float  aspect = (float)half / (float)h;
            const double zoom = pend_zoom(theta_cur);
            const double cx = pend_cam_cx(zoom, aspect);   // centro (offset)
            const float  hx = (float)(2.4 / zoom);
            mat4 O;
            mat4_ortho(O, (float)(cx - (double)hx * aspect), (float)(cx + (double)hx * aspect),
                            -hx, hx, -1.0f, 1.0f);
            glUseProgram(flat.prog);
            glUniformMatrix4fv(flat.u_mvp, 1, GL_FALSE, O);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            pend_birth.render(flat.u_alpha);   // ramo nascita: mai espulso
            pend_stream.render(flat.u_alpha);
            glUseProgram(0);
        }

        // ---- fps (EMA) e righe HUD aggiornate a 10 Hz ----
        static double fps_ema = 60.0;
        if (dt_raw > 1e-4) fps_ema = 0.9 * fps_ema + 0.1 * (1.0 / dt_raw);
        static std::string hud_digits, hud_time, hud_rate, hud_fps, hud_end;
        static double hud_accum = 1.0;   // primo frame: aggiorna subito
        hud_accum += dt_raw;
        if (hud_accum >= 0.1) {
            hud_accum = 0.0;
            hud_digits = "cifre consumate: " + fmt_group(stream.base_index) + " / " +
                         fmt_group((long long)total_digits);
            char b[64];
            std::snprintf(b, sizeof(b), "tempo: %.1f s", t);
            hud_time = b;
            // rate delle cifre = C(θ)·R(θ): segue l'accelerazione del pendolo
            if (finished) {
                std::snprintf(b, sizeof(b), "rate: — scena ferma");
                hud_end = "COMPLETATO — R per riavviare, Esc per uscire";
            } else {
                std::snprintf(b, sizeof(b), "rate: %s cifre/s",
                              fmt_group((long long)rate_of(theta_cur)).c_str());
                hud_end = "";
            }
            hud_rate = b;
            std::snprintf(b, sizeof(b), "fps: %.1f", fps_ema);
            hud_fps = b;
        }

        // ---- chord streaming: quante corde emettere in questo frame ----
        // Finché il file non è esaurito: le cifre vengono TUTTE consumate al
        // ritmo di rate(θ) (HUD onesto), con tetto per frame. L'ultima cifra
        // del file non genera corda (serve la successiva): `remaining` è il
        // tetto esatto, così base_index non supera mai la fine del file.
        const long long remaining = (long long)total_digits - 1 - (long long)stream.base_index;
        if (!finished && remaining > 0) {
            stream.frac_acc += rate_of(theta_cur) * dt;
            long long to_emit = (long long)stream.frac_acc;
            if (to_emit > 0) {
                if (to_emit > remaining) { to_emit = remaining; stream.frac_acc = 0.0; }
                else stream.frac_acc -= (double)to_emit;
                if (to_emit > 500000) to_emit = 500000;   // tetto per frame (burst)
                stream.push(t, pi_reader, to_emit, ChordStream::DRAW_BUDGET);
            }
        }
        // ---- FINE: file esaurito OPPURE coreografia arrivata a θ_max ----
        // Rilevato qui ma applicato al frame successivo (dt = 0), così lo
        // screenshot di controllo sotto cattura l'ULTIMO frame disegnato.
        const bool just_finished = !finished &&
            ((long long)stream.base_index + 1 >= (long long)total_digits ||
             theta_cur >= THETA_MAX);
        // ragnatela crescente: ricontestualizza il layer statico quando lo
        // streaming ne ha aggiunte abbastanza (e alla prima corda emessa)
        if (stream.web_dirty) {
            stream.web_dirty = false;
            rebuild_static_meshes();
        }

        // ---- viewport destro: chord diagram (stile digit circle) ----
        glViewport(half, 0, w - half, h);
        {
            // ortho quadrata con margine (CHORD_WORLD): cerchio centrato e
            // sempre interamente visibile, cifre incluse
            float aspect = (float)(w - half) / (float)h;
            mat4 O;
            mat4_ortho(O, -CHORD_WORLD * aspect, CHORD_WORLD * aspect,
                            -CHORD_WORLD, CHORD_WORLD, -1.0f, 1.0f);

            glUseProgram(flat.prog);
            glUniformMatrix4fv(flat.u_mvp, 1, GL_FALSE, O);

            // settori: alpha blending standard, pieni (come nei riferimenti)
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glUniform1f(flat.u_alpha, 0.85f);
            glBindVertexArray(sec_vao);
            glDrawArrays(GL_TRIANGLES, 0, (GLint)chord_sec_v_count);

            // corde statiche: alpha standard — NIENTE additivo: con palette
            // pastello l'accumulo additivo schiacciava la ragnatela verso il
            // bianco invece che aggiungere sfumature
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glUniform1f(flat.u_alpha, 0.5f);
            glBindVertexArray(chd_vao);
            if (chord_chd_v_count > 0)
                glDrawArrays(GL_TRIANGLES, 0, (GLint)chord_chd_v_count);

            // corde in streaming: alpha standard, ricchezza decrescente
            // per-corda (prime ricche, successive flebili) + fade in shader
            glUniform1f(flat.u_alpha, 1.0f);
            stream.render(t, flat.u_mvp, O);

            glBindVertexArray(0);
            glUseProgram(0);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }

        // ---- BLOCCO FINALE: la scena si spegne e resta ferma ----
        // Da qui dt = 0: θ non avanza, il rate non cresce più, il clock di
        // scena t si ferma (e con lui il fade delle corde: il tappeto finale
        // resta vivo e colorato invece di svuotarsi). Il tappeto che si vede
        // è quello dell'ultimo frame — la memoria completa del file è la
        // ragnatela statica, che a qui contiene tutte le transizioni.
        if (just_finished) {
            finished = true;
            std::printf("FINE ANIMAZIONE: %lld/%lld cifre, θ=%.0f rad, t=%.1f s — "
                        "scena congelata (R per riavviare)\n",
                        (long long)stream.base_index, (long long)total_digits,
                        theta_cur, t);
            std::fflush(stdout);
        }

        // ---- verifica geometrica automatica (una tantum, a t>20 s) ----
        // (viewport a finestra intera: screenshot_save usa GL_VIEWPORT)
        glViewport(0, 0, w, h);
        if (selfcheck && !selfcheck_done && t > 20.0) {
            selfcheck_done = true;
            std::vector<uint8_t> px((size_t)w * h * 3);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
            auto px_at = [&](int x, int y) -> const uint8_t* {   // y=0 in ALTO
                return &px[((size_t)(h - 1 - y) * w + x) * 3];
            };
            auto lum = [&](const uint8_t* p) { return std::max({p[0], p[1], p[2]}); };

            // 1) centratura/rotondità anello: bounding box di tutti i pixel
            // accesi nel half destro (le corde restano dentro l'anello)
            int xl = w, xr = -1, yt = h, yb = -1;
            for (int y = 0; y < h; ++y)
                for (int x = half; x < w; ++x) {
                    if (lum(px_at(x, y)) > 24) {
                        if (x < xl) xl = x;
                        if (x > xr) xr = x;
                        if (y < yt) yt = y;
                        if (y > yb) yb = y;
                    }
                }
            float cx_ring = 0.5f * (xl + xr), cy_ring = 0.5f * (yt + yb);
            float want_cx = half + (w - half) * 0.5f, want_cy = h * 0.5f;
            float rad_h = 0.5f * (xr - xl), rad_v = 0.5f * (yb - yt);
            std::printf("CHECK centro anello: (%.0f,%.0f) atteso (%.0f,%.0f)  "
                        "raggi h/v: %.0f/%.0f (rapporto %.3f)\n",
                        cx_ring, cy_ring, want_cx, want_cy, rad_h, rad_v, rad_v / rad_h);
            bool centered = std::fabs(cx_ring - want_cx) < 0.03f * (w - half) &&
                            std::fabs(cy_ring - want_cy) < 0.03f * h &&
                            std::fabs(rad_v / rad_h - 1.0f) < 0.05f;
            std::printf("CHECK centratura: %s\n", centered ? "OK" : "FALLITA");

            // (2) varietà di colore spostata nella fase 2: a t=20 s ci sono
            //     solo ~10 cifre — vedi sotto

            // 2) pendolo: pixel non neri nel half sinistro + bounding box
            long long lit = 0;
            int pl = w, pr = -1, pt = h, pb = -1;
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < half; ++x) {
                    if (lum(px_at(x, y)) > 20) {
                        lit++;
                        if (x < pl) pl = x;
                        if (x > pr) pr = x;
                        if (y < pt) pt = y;
                        if (y > pb) pb = y;
                    }
                }
            double ratio_l = (double)lit / ((double)half * h);
            std::printf("CHECK pendolo: lit=%.3f%% bbox=[%d..%d]x[%d..%d] (viewport %dx%d)\n",
                        100.0 * ratio_l, pl, pr, pt, pb, half, h);
            std::printf("CHECK pendolo stato: theta=%.2f rad (%.1f pi), count=%d, head=(%.6f,%.6f) [assoluto]\n",
                        theta_cur, theta_cur / PI_PEND, (int)pend_stream.count,
                        (double)pend_stream.pts[pend_stream.head == 0 ? 0 : pend_stream.head - 1].x + 2.0,
                        (double)pend_stream.pts[pend_stream.head == 0 ? 0 : pend_stream.head - 1].y);
            bool pend_ok = ratio_l > 0.002 && (pr - pl) > 0.4f * half && (pb - pt) > 0.4f * h;
            std::printf("CHECK pendolo visibile: %s\n", pend_ok ? "OK" : "FALLITO");

            // Mappa ASCII del half destro (48×24): forma dell'anello a colpo d'occhio
            for (int ry = 0; ry < 24; ++ry) {
                std::string row;
                for (int rx = 0; rx < 48; ++rx) {
                    long acc = 0; int n = 0;
                    int x0 = half + (w - half) * rx / 48, x1 = half + (w - half) * (rx + 1) / 48;
                    int y0 = h * ry / 24,             y1 = h * (ry + 1) / 24;
                    for (int y = y0; y < y1; y += 4) for (int x = x0; x < x1; x += 4) {
                        acc += lum(px_at(x, y)); n++;
                    }
                    long m = n ? acc / n : 0;
                    row += (m > 120 ? '#' : m > 60 ? '+' : m > 24 ? '.' : ' ');
                }
                std::printf("MAP |%s|\n", row.c_str());
            }

            std::string shot = screenshot_save("pictures/check_20s");
            std::printf("CHECK screenshot: %s\n", shot.c_str());
            std::fflush(stdout);
        }

        // ---- verifica tinte (fase 2): scatta quando ~400 cifre sono state
        // consumate (t≈83 s) — prima non c'è
        // abbastanza ragnatela per parlare di varietà di colore ----
        if (selfcheck && !selfcheck_tint_done && (long long)stream.base_index >= 400) {
            selfcheck_tint_done = true;
            std::vector<uint8_t> px((size_t)w * h * 3);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
            auto px_at = [&](int x, int y) -> const uint8_t* {
                return &px[((size_t)(h - 1 - y) * w + x) * 3];
            };
            auto lum = [&](const uint8_t* p) { return std::max({p[0], p[1], p[2]}); };
            float cx_ring = half + (w - half) * 0.5f, cy_ring = h * 0.5f;
            float rad = 0.35f * (float)h * 0.5f;
            std::vector<bool> seen(1 << 12, false);
            int buckets = 0;
            for (int y = (int)(cy_ring - rad); y < (int)(cy_ring + rad); y += 2)
                for (int x = (int)(cx_ring - rad); x < (int)(cx_ring + rad); x += 2) {
                    const uint8_t* p = px_at(x, y);
                    if (lum(p) < 30) continue;
                    uint16_t key = (uint16_t)((p[0] >> 4) << 8 | (p[1] >> 4) << 4 | (p[2] >> 4));
                    if (!seen[key]) { seen[key] = true; buckets++; }
                }
            std::printf("CHECK tinte nel disco: %d (attese > 15, a t=%.0f s con %lld cifre)\n",
                        buckets, t, (long long)stream.base_index);
            bool colorful = buckets > 15;
            std::printf("CHECK corde colorate: %s\n", colorful ? "OK" : "FALLITO");
            std::string shot = screenshot_save("pictures/check_chords");
            std::printf("CHECK screenshot chord: %s\n", shot.c_str());
            std::fflush(stdout);
        }

        // ---- screenshot ai close-up: un frame SUBITO dopo che il ramo
        // usente ha superato il punto d'incrocio (miss già disegnato, zoom
        // al massimo) per ogni contatto ----
        if (selfcheck) {
            glViewport(0, 0, w, h);   // screenshot a finestra intera
            for (int i = 0; i < PEND_N_CONTACTS; ++i) {
                // pend_shot_th: un passo oltre l'incrocio, così la testa
                // disegnata supera y=0 e il segmento finale attraversa anche
                // la metà superiore (al 1° contatto il salto di θ per frame
                // era < del passo e la curva si fermava sul centro)
                if (!closeup_shot[i] && theta_cur > pend_shot_th(i) &&
                    pend_zoom(theta_cur) > 0.9 * pend_contact_zoom(i)) {
                    closeup_shot[i] = true;
                    double hdx = Pendulum::re(theta_cur) - 2.0;
                    double hdy = Pendulum::im(theta_cur);
                    double h2x = Pendulum::re(pend_stream.theta_head) - 2.0;
                    double h2y = Pendulum::im(pend_stream.theta_head);
                    std::string sc = screenshot_save(
                        (std::string("pictures/check_closeup") + (char)('1' + i)).c_str());
                    std::printf("CHECK close-up %d: theta=%.4f rad, zoom=%.3g, "
                                "testa-(2,0)=%.3e, head_disegnata=%.4f "
                                "(dist %.3e, miss atteso %.3e), shot: %s\n",
                                i + 1, theta_cur, pend_zoom(theta_cur),
                                std::sqrt(hdx * hdx + hdy * hdy),
                                pend_stream.theta_head,
                                std::sqrt(h2x * h2x + h2y * h2y),
                                pend_miss(i), sc.c_str());
                    std::fflush(stdout);
                }
            }
        }

        // ---- HUD (STEP 10): pixel ortho sull'intera finestra ----
        glViewport(0, 0, w, h);
        {
            mat4 H;
            mat4_ortho(H, 0.0f, (float)w, 0.0f, (float)h, -1.0f, 1.0f);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            // titolo in alto a sinistra
            glUseProgram(hud.prog);
            glUniformMatrix4fv(hud.u_mvp, 1, GL_FALSE, H);
            glUniform1f(hud.u_alpha, 1.0f);
            hud.draw("Pi-visual — pendolo composto + chord diagram di π",
                     16.0f, (float)h - 14.0f, 1, 1, 1, 0.95f);

            // contatori in alto a destra, allineati a destra
            const std::string* lines[5] = {&hud_digits, &hud_time, &hud_rate, &hud_fps, &hud_end};
            for (int i = 0; i < 5; ++i) {
                float tw = hud.width(*lines[i]);
                hud.draw(*lines[i], (float)w - 16.0f - tw, (float)h - 14.0f - i * hud.line_h,
                         1, 1, 1, 0.9f);
            }

            // cifre 0-9 attorno al cerchio del chord (grande, colore del settore):
            // posizione = trasformazione mondo->pixel del viewport destro
            {
                // PI_VISUAL_LABEL_COLOR=1: cifre in magenta puro (sonda del check)
                const bool lbl_dbg = std::getenv("PI_VISUAL_LABEL_COLOR") != nullptr;
                float vh2 = (float)(w - half) < (float)h ? (float)(w - half) : (float)h;
                float scale = vh2 / (2.0f * CHORD_WORLD);
                float ccx = (float)half + (float)(w - half) * 0.5f;
                float ccy = (float)h * 0.5f;
                for (int d = 0; d < 10; ++d) {
                    float a = chord_digit_angle(d);
                    float wx = CHORD_RLABEL * std::cos(a);
                    float wy = CHORD_RLABEL * std::sin(a);
                    const float* c = CHORD_CURRENT_PALETTE[d];
                    char lbl[2] = {(char)('0' + d), 0};
                    float tw = hud.width(lbl, 2);
                    float th = hud.get(lbl, 2).h;
                    hud.draw(lbl, ccx + wx * scale - tw * 0.5f,
                                  ccy + wy * scale - th * 0.5f,
                             lbl_dbg ? 1.0f : c[0], lbl_dbg ? 0.0f : c[1],
                             lbl_dbg ? 1.0f : c[2], 1.0f, /*big=*/2, /*y_is_top=*/false);
                }
            }

            // legenda delle 10 cifre in basso a sinistra: quad colorato + cifra
            glUseProgram(flat.prog);
            glUniformMatrix4fv(flat.u_mvp, 1, GL_FALSE, H);
            {
                HudVertex sq[10 * 6];
                for (int d = 0; d < 10; ++d) {
                    float x = 16.0f, y = 16.0f + (9 - d) * 18.0f;
                    const float* c = CHORD_CURRENT_PALETTE[d];
                    sq[d * 6 + 0] = {x, y, c[0], c[1], c[2], 0.9f, 0, 0};
                    sq[d * 6 + 1] = {x + 12, y, c[0], c[1], c[2], 0.9f, 0, 0};
                    sq[d * 6 + 2] = {x + 12, y + 12, c[0], c[1], c[2], 0.9f, 0, 0};
                    sq[d * 6 + 3] = {x, y, c[0], c[1], c[2], 0.9f, 0, 0};
                    sq[d * 6 + 4] = {x + 12, y + 12, c[0], c[1], c[2], 0.9f, 0, 0};
                    sq[d * 6 + 5] = {x, y + 12, c[0], c[1], c[2], 0.9f, 0, 0};
                }
                // il flat shader usa locations 0/1: la legenda riusa il VBO dell'HUD
                glBindVertexArray(hud.vao);
                glBindBuffer(GL_ARRAY_BUFFER, hud.vbo);
                glBufferData(GL_ARRAY_BUFFER, sizeof(sq), sq, GL_STREAM_DRAW);
                glUniform1f(flat.u_alpha, 0.9f);
                glDrawArrays(GL_TRIANGLES, 0, 60);
                glBindVertexArray(0);
            }
            for (int d = 0; d < 10; ++d) {
                const float* c = CHORD_CURRENT_PALETTE[d];
                char lbl[2] = {(char)('0' + d), 0};
                glUseProgram(hud.prog);
                glUniformMatrix4fv(hud.u_mvp, 1, GL_FALSE, H);
                hud.draw(lbl, 34.0f, 17.0f + (9 - d) * 18.0f + 12.0f,
                         c[0], c[1], c[2], 0.95f, /*small=*/1);
            }
            glUseProgram(0);
        }
        hud.prune();

        // ---- screenshot del BLOCCO FINALE (dopo l'HUD: nel quadro finale si
        // vede anche il banner "COMPLETATO — R per riavviare") ----
        if (just_finished && selfcheck) {
            glViewport(0, 0, w, h);
            std::string sf = screenshot_save("pictures/check_fine");
            std::printf("CHECK screenshot fine: %s\n", sf.c_str());
            std::fflush(stdout);
        }

        // ---- verifica etichette (dopo l'HUD: con PI_VISUAL_LABEL_COLOR=1 le
        // cifre grandi sono magenta puro) + screenshot finale del check ----
        if (selfcheck && selfcheck_done && !selfcheck_hud_done && t > 20.0) {
            selfcheck_hud_done = true;
            std::vector<uint8_t> px2((size_t)w * h * 3);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px2.data());
            long magenta = 0;
            for (int y = 0; y < h; ++y)
                for (int x = half; x < w; ++x) {
                    const uint8_t* p = &px2[((size_t)(h - 1 - y) * w + x) * 3];
                    if (p[0] > 200 && p[1] < 40 && p[2] > 200) magenta++;
                }
            if (std::getenv("PI_VISUAL_LABEL_COLOR")) {
                std::printf("CHECK magenta cifre: %ld px (attesi > 1500)\n", magenta);
                std::printf("CHECK etichette: %s\n", magenta > 1500 ? "OK" : "FALLITO");
            }
            std::string shot = screenshot_save("pictures/check_hud");
            std::printf("CHECK screenshot HUD: %s\n", shot.c_str());
            std::fflush(stdout);
        }

        // ---- reset (tasto R): clock e stream da capo ----
        if (reset_requested) {
            reset_requested = false;
            t_start = SDL_GetPerformanceCounter();
            t_raw_prev = 0.0;
            t = 0.0;                   // la scena riparte da zero
            theta_cur = 0.0;           // anche la coreografia riparte da θ = 0
            finished = false;          // la scena riparte: il blocco finale si toglie
            stream.reset();          // rolling window, base_index, accumulo frazionario
            pend_stream.reset();     // curva da θ=0
            chord_web.clear();       // ragnatela riparte da zero
            rebuild_static_meshes();
        }

        // ---- screenshot (tasto S): cattura il back buffer PRIMA dello swap ----
        if (screenshot_requested) {
            screenshot_requested = false;
            std::string path = screenshot_save("pictures/Pi-visual");
            if (!path.empty()) std::printf("screenshot salvato: %s\n", path.c_str());
        }

        SDL_GL_SwapWindow(win);
    }

    pend_stream.destroy_gl();
    pend_birth.destroy_gl();
    glDeleteBuffers(1, &sec_vbo);
    glDeleteVertexArrays(1, &sec_vao);
    glDeleteBuffers(1, &chd_vbo);
    glDeleteVertexArrays(1, &chd_vao);
    stream.destroy_gl();
    hud.destroy();
    glDeleteProgram(flat.prog);
    pi_reader.close();
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
