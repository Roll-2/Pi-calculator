// ============================================================
// hud.h — HUD con SDL_ttf: cache di texture testo
//
// Ogni stringa renderizzata viene messa in cache: chiave = stringa,
// valore = texture GL + dimensioni. I contatori cambiano spesso, ma sono
// aggiornati a 10 Hz e la cache viene potata quando supera PRUNE_MAX
// elementi: il costo di SDL_ttf resta trascurabile in regime.
//
// Pattern classico HUD SDL_ttf+GL: render TTF -> SDL_Surface ARGB ->
// texture GL -> quad in coordinate pixel con shader dedicato.
// Font di sistema (nessun asset remoto): su macOS Helvetica.ttc.
// ============================================================
#pragma once

#include <cstdio>
#include <map>
#include <string>

#include <SDL.h>
#include <SDL_ttf.h>
#include <OpenGL/gl3.h>

#include "camera.h"   // mat4_ortho per l'ortho in pixel

static const char* HUD_FONT_PATH = "/System/Library/Fonts/Helvetica.ttc";

struct HudTexture {
    GLuint tex = 0;
    float  w = 0, h = 0;   // dimensioni in pixel
};

struct HudVertex { float x, y, r, g, b, a, u, v; };

struct HudText {
    TTF_Font* font = nullptr;
    TTF_Font* font_small = nullptr;
    TTF_Font* font_big = nullptr;
    int line_h = 0, line_h_small = 0, line_h_big = 0;

    std::map<std::string, HudTexture> cache;        // font normale
    std::map<std::string, HudTexture> cache_small;  // font piccolo
    std::map<std::string, HudTexture> cache_big;    // font grande (cifre del cerchio)
    static constexpr size_t PRUNE_MAX = 512;        // limite texture in cache

    GLuint vao = 0, vbo = 0;
    GLuint prog = 0;
    GLint  u_mvp = -1;
    GLint  u_alpha = -1;

    static const char* VS() {
        return
            "#version 330 core\n"
            "layout(location = 0) in vec2 a_pos;\n"
            "layout(location = 1) in vec4 a_col;\n"
            "layout(location = 2) in vec2 a_uv;\n"
            "uniform mat4 u_mvp;\n"
            "out vec4 v_col;\n"
            "out vec2 v_uv;\n"
            "void main() { gl_Position = u_mvp * vec4(a_pos, 0.0, 1.0); v_col = a_col; v_uv = a_uv; }\n";
    }
    static const char* FS() {
        return
            "#version 330 core\n"
            "in vec4 v_col;\n"
            "in vec2 v_uv;\n"
            "uniform float u_alpha;\n"
            "uniform sampler2D u_tex;\n"
            "out vec4 frag;\n"
            "void main() {\n"
            "    float a = texture(u_tex, v_uv).a;\n"   // glifo bianco: la copertura è nell'alpha
            "    frag = vec4(v_col.rgb, a * v_col.a * u_alpha);\n"
            "}\n";
    }

    bool init(int size_px, int size_small_px, int size_big_px) {
        if (TTF_Init() != 0) {
            std::fprintf(stderr, "TTF_Init fallito: %s\n", TTF_GetError());
            return false;
        }
        font       = TTF_OpenFont(HUD_FONT_PATH, size_px);
        font_small = TTF_OpenFont(HUD_FONT_PATH, size_small_px);
        font_big   = TTF_OpenFont(HUD_FONT_PATH, size_big_px);
        if (!font || !font_small || !font_big) {
            std::fprintf(stderr, "Impossibile aprire il font di sistema %s: %s\n",
                         HUD_FONT_PATH, TTF_GetError());
            return false;
        }
        line_h = TTF_FontHeight(font);
        line_h_small = TTF_FontHeight(font_small);
        line_h_big = TTF_FontHeight(font_big);

        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(HudVertex), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(HudVertex), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(HudVertex), (void*)(6 * sizeof(float)));
        glBindVertexArray(0);

        auto compile = [](GLenum type, const char* src) -> GLuint {
            GLuint s = glCreateShader(type);
            glShaderSource(s, 1, &src, nullptr);
            glCompileShader(s);
            GLint ok = GL_FALSE;
            glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                char log[1024];
                glGetShaderInfoLog(s, sizeof(log), nullptr, log);
                std::fprintf(stderr, "Compilazione shader HUD fallita:\n%s\n", log);
                return 0;
            }
            return s;
        };
        GLuint vs = compile(GL_VERTEX_SHADER, VS());
        GLuint fs = compile(GL_FRAGMENT_SHADER, FS());
        prog = glCreateProgram();
        glAttachShader(prog, vs);
        glAttachShader(prog, fs);
        glLinkProgram(prog);
        glDeleteShader(vs);
        glDeleteShader(fs);
        GLint ok = GL_FALSE;
        glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[1024];
            glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
            std::fprintf(stderr, "Link programma HUD fallito:\n%s\n", log);
            return false;
        }
        u_mvp = glGetUniformLocation(prog, "u_mvp");
        u_alpha = glGetUniformLocation(prog, "u_alpha");
        return true;
    }

    void destroy() {
        for (auto& kv : cache) if (kv.second.tex) glDeleteTextures(1, &kv.second.tex);
        for (auto& kv : cache_small) if (kv.second.tex) glDeleteTextures(1, &kv.second.tex);
        for (auto& kv : cache_big) if (kv.second.tex) glDeleteTextures(1, &kv.second.tex);
        cache.clear(); cache_small.clear(); cache_big.clear();
        if (font) TTF_CloseFont(font);
        if (font_small) TTF_CloseFont(font_small);
        if (font_big) TTF_CloseFont(font_big);
        if (vbo) glDeleteBuffers(1, &vbo);
        if (vao) glDeleteVertexArrays(1, &vao);
        if (prog) glDeleteProgram(prog);
        vao = vbo = prog = 0;
        TTF_Quit();
    }

    // Potatura: oltre PRUNE_MAX texture si svuota la cache (le poche stringhe
    // visibili vengono ri-renderizzate al frame successivo).
    void prune() {
        auto prune_one = [](std::map<std::string, HudTexture>& c) {
            if (c.size() <= PRUNE_MAX) return;
            for (auto& kv : c) if (kv.second.tex) glDeleteTextures(1, &kv.second.tex);
            c.clear();
        };
        prune_one(cache);
        prune_one(cache_small);
        prune_one(cache_big);
    }

    // Renderizza la stringa (o prende la cache) e ritorna la texture.
    // class: 0 = normale, 1 = small, 2 = big
    HudTexture& get(const std::string& s, int cls = 0) {
        auto& c = cls == 1 ? cache_small : (cls == 2 ? cache_big : cache);
        auto it = c.find(s);
        if (it != c.end()) return it->second;

        TTF_Font* f = cls == 1 ? font_small : (cls == 2 ? font_big : font);
        SDL_Color white = {255, 255, 255, 255};
        SDL_Surface* surf = TTF_RenderUTF8_Blended(f, s.c_str(), white);
        if (!surf) { static HudTexture empty; return empty; }
        SDL_Surface* argb = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_ABGR8888, 0);
        SDL_FreeSurface(surf);
        if (!argb) { static HudTexture empty2; return empty2; }

        HudTexture ht;
        ht.w = (float)argb->w;
        ht.h = (float)argb->h;
        glGenTextures(1, &ht.tex);
        glBindTexture(GL_TEXTURE_2D, ht.tex);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, argb->pitch / 4);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, argb->w, argb->h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, argb->pixels);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        SDL_FreeSurface(argb);
        return c.emplace(s, ht).first->second;
    }

    float width(const std::string& s, int cls = 0) {
        return get(s, cls).w;
    }

    // Disegna una stringa a coordinate pixel (origine in basso a sinistra).
    // Con y_is_top=true, y è il bordo SUPERIORE del testo (comodo per gli HUD
    // in alto). Richiede: glUseProgram(prog) e u_mvp già impostati dal chiamante.
    void draw(const std::string& s, float x, float y, float r, float g, float b, float a,
              int cls = 0, bool y_is_top = true) {
        HudTexture& ht = get(s, cls);
        if (!ht.tex) return;
        float yy = y_is_top ? y - ht.h : y;
        // riga 0 della texture = cima del glifo: la quad mappa v=0 in alto
        HudVertex q[6] = {
            {x,        yy,        r, g, b, a, 0.0f, 1.0f},
            {x + ht.w, yy,        r, g, b, a, 1.0f, 1.0f},
            {x + ht.w, yy + ht.h, r, g, b, a, 1.0f, 0.0f},
            {x,        yy,        r, g, b, a, 0.0f, 1.0f},
            {x + ht.w, yy + ht.h, r, g, b, a, 1.0f, 0.0f},
            {x,        yy + ht.h, r, g, b, a, 0.0f, 0.0f},
        };
        glBindVertexArray(vao);
        glBindTexture(GL_TEXTURE_2D, ht.tex);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(q), q, GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
    }
};
