// ============================================================
// camera.h — matrice ortografica per i viewport
//
// Convenzione: matrici 4x4 COLUMN-MAJOR in float[16] con m[col*4 + row],
// direttamente compatibili con glUniformMatrix4fv(transpose=GL_FALSE).
//
// La coreografia del pendolo (zoom e centro in funzione di θ) sta in
// Pi-visual.cpp: è una politica di scena, non una primitiva di matrice.
//
// Unit test temporaneo (compilabile, non è un binario ufficiale):
//   clang++ -x c++ -std=c++17 -DCAMERA_TEST visual/camera.h -o camera_test
//   ./camera_test
// ============================================================
#pragma once

#include <cmath>
#include <cstdio>

using mat4 = float[16];

// Proiezione ortografica (column-major, standard glOrtho)
static inline void mat4_ortho(mat4 m, float l, float r, float b, float t, float n, float f) {
    for (int i = 0; i < 16; ++i) m[i] = 0.0f;
    m[0]  = 2.0f / (r - l);
    m[5]  = 2.0f / (t - b);
    m[10] = -2.0f / (f - n);
    m[12] = -(r + l) / (r - l);
    m[13] = -(t + b) / (t - b);
    m[14] = -(f + n) / (f - n);
    m[15] = 1.0f;
}

#ifdef CAMERA_TEST
// Unit test: matrice orto nota.
#include <cstdlib>
static int fail(const char* msg, float got, float want) {
    std::fprintf(stderr, "FALLITO: %s (got %g, want %g)\n", msg, got, want);
    return 1;
}
int main() {
    // 1) l'origine finisce al centro dell'ortho simmetrica
    mat4 O;
    mat4_ortho(O, -2.0f, 2.0f, -1.0f, 1.0f, -1.0f, 1.0f);
    if (std::fabs(O[12]) > 1e-6f) return fail("origine.x", O[12], 0);
    if (std::fabs(O[13]) > 1e-6f) return fail("origine.y", O[13], 0);
    // 2) il punto a metà del lato destro va a NDC x = 0.5
    float rx = O[0] * 1.0f + O[12];
    if (std::fabs(rx - 0.5f) > 1e-6f) return fail("punto x=1", rx, 0.5f);
    // 3) scala corretta su y ([-1,1] -> NDC [-1,1])
    float ty = O[5] * 1.0f + O[13];
    if (std::fabs(ty - 1.0f) > 1e-6f) return fail("punto y=1", ty, 1.0f);
    std::printf("camera_test: OK (3/3)\n");
    return 0;
}
#endif
