// ============================================================
// camera.h — mat4, lookAt, perspective, inverse e sistema di keyframe
//
// Convenzione: matrici 4x4 COLUMN-MAJOR in float[16] con m[col*4 + row],
// direttamente compatibili con glUniformMatrix4fv(transpose=GL_FALSE).
// mat4_mul(out, a, b) calcola out = a*b (agisce prima b, poi a).
//
// Keyframe camera: struct Keyframe { double t; float x, y, zoom; } con
// interpolazione smoothstep tra keyframe consecutivi. La camera orbita
// "dritta": eye = (x, y, dist/zoom), guarda (x, y, 0): zoom è un dolly-in.
//
// Unit test temporaneo (compilabile, non è un binario ufficiale):
//   clang++ -x c++ -std=c++17 -DCAMERA_TEST visual/camera.h -o camera_test
//   ./camera_test
// ============================================================
#pragma once

#include <cmath>
#include <cstdio>

// ------------------------------- v3 -------------------------------
struct v3 {
    float x = 0, y = 0, z = 0;
};
static inline v3 v3_sub(const v3& a, const v3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static inline v3 v3_cross(const v3& a, const v3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
static inline float v3_dot(const v3& a, const v3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline v3 v3_norm(const v3& a) {
    float l = std::sqrt(v3_dot(a, a));
    return l > 0 ? v3{a.x / l, a.y / l, a.z / l} : v3{0, 0, 0};
}

// ------------------------------- mat4 -------------------------------
using mat4 = float[16];

static inline void mat4_identity(mat4 m) {
    for (int i = 0; i < 16; ++i) m[i] = 0.0f;
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

// out = a*b (column-major)
static inline void mat4_mul(mat4 out, const mat4 a, const mat4 b) {
    mat4 r;
    for (int c = 0; c < 4; ++c)
        for (int rw = 0; rw < 4; ++rw) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) s += a[k * 4 + rw] * b[c * 4 + k];
            r[c * 4 + rw] = s;
        }
    for (int i = 0; i < 16; ++i) out[i] = r[i];
}

// Trasforma un punto (w=1) e divide per w.
static inline v3 mat4_transform_point(const mat4 m, v3 p) {
    float x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
    float y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
    float z = m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14];
    float w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
    if (w == 0.0f) w = 1.0f;
    return {x / w, y / w, z / w};
}

// Inversa generale 4x4 (cofattori). Ritorna false se determinante nullo.
static inline bool mat4_inverse(mat4 out, const mat4 m) {
    float inv[16], det;
    inv[0]  =  m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4]  = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8]  =  m[4] * m[9]  * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9]  * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1]  = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5]  =  m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9]  = -m[0] * m[9]  * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] =  m[0] * m[9]  * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2]  =  m[1] * m[6]  * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6]  = -m[0] * m[6]  * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] =  m[0] * m[5]  * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5]  * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3]  = -m[1] * m[6]  * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7]  =  m[0] * m[6]  * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5]  * m[11] + m[0] * m[7] * m[9]  + m[4] * m[1] * m[11] - m[4] * m[3] * m[9]  - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] =  m[0] * m[5]  * m[10] - m[0] * m[6] * m[9]  - m[4] * m[1] * m[10] + m[4] * m[2] * m[9]  + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

    det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (det == 0.0f) return false;
    det = 1.0f / det;
    for (int i = 0; i < 16; ++i) out[i] = inv[i] * det;
    return true;
}

// Proiezione prospettica (column-major, standard gluPerspective)
static inline void mat4_perspective(mat4 m, float fovy_rad, float aspect, float znear, float zfar) {
    for (int i = 0; i < 16; ++i) m[i] = 0.0f;
    float f = 1.0f / std::tan(fovy_rad / 2.0f);
    m[0]  = f / aspect;
    m[5]  = f;
    m[10] = (zfar + znear) / (znear - zfar);
    m[11] = -1.0f;
    m[14] = (2.0f * zfar * znear) / (znear - zfar);
}

// lookAt (column-major, standard gluLookAt)
static inline void mat4_lookat(mat4 m, v3 eye, v3 center, v3 up) {
    v3 f = v3_norm(v3_sub(center, eye));   // forward
    v3 s = v3_norm(v3_cross(f, up));       // right
    v3 u = v3_cross(s, f);                 // true up

    mat4 r;
    r[0] = s.x;  r[4] = s.y;  r[8]  = s.z;  r[12] = -v3_dot(s, eye);
    r[1] = u.x;  r[5] = u.y;  r[9]  = u.z;  r[13] = -v3_dot(u, eye);
    r[2] = -f.x; r[6] = -f.y; r[10] = -f.z; r[14] =  v3_dot(f, eye);
    r[3] = 0;    r[7] = 0;    r[11] = 0;    r[15] = 1;
    for (int i = 0; i < 16; ++i) m[i] = r[i];
}

// ------------------------------- keyframe -------------------------------
struct Keyframe {
    double t;      // tempo del clock globale [s]
    float  x, y;   // centro inquadrato nel piano complesso
    float  zoom;   // 1 = vista ampia, >1 = dolly-in (distanza camera = dist/zoom)
};

static inline float smoothstep01(float s) {
    if (s < 0) s = 0;
    if (s > 1) s = 1;
    return s * s * (3.0f - 2.0f * s);
}

// Interpolazione smoothstep dei keyframe (ordinati per t crescente).
// Con 0 o 1 keyframe ritorna quello (o il default); oltre l'ultimo resta fermo.
static inline Keyframe keyframe_eval(const Keyframe* kf, int n, double t) {
    if (n <= 0) return {t, 0.0f, 0.0f, 1.0f};
    if (t <= kf[0].t) return kf[0];
    if (t >= kf[n - 1].t) return kf[n - 1];
    for (int i = 0; i + 1 < n; ++i) {
        if (t >= kf[i].t && t <= kf[i + 1].t) {
            float span = (float)(kf[i + 1].t - kf[i].t);
            float s = span > 0 ? (float)((t - kf[i].t) / span) : 0.0f;
            s = smoothstep01(s);
            Keyframe out;
            out.t = t;
            out.x = kf[i].x + (kf[i + 1].x - kf[i].x) * s;
            out.y = kf[i].y + (kf[i + 1].y - kf[i].y) * s;
            out.zoom = kf[i].zoom * std::pow(kf[i + 1].zoom / kf[i].zoom, s); // zoom geometrico
            return out;
        }
    }
    return kf[n - 1];
}

#ifdef CAMERA_TEST
// Unit test: proiezioni note + round-trip con l'inversa.
#include <cstdlib>
static int fail(const char* msg, float got, float want) {
    std::fprintf(stderr, "FALLITO: %s (got %g, want %g)\n", msg, got, want);
    return 1;
}
int main() {
    // 1) l'origine visto dall'origine finisce al centro dello schermo
    mat4 P, V, VP, IVP;
    mat4_perspective(P, 60.0f * 3.14159265f / 180.0f, 1.0f, 0.1f, 100.0f);
    mat4_lookat(V, {0, 0, 5}, {0, 0, 0}, {0, 1, 0});
    mat4_mul(VP, P, V);
    v3 c = mat4_transform_point(VP, {0, 0, 0});
    if (std::fabs(c.x) > 1e-5f) return fail("origine.x", c.x, 0);
    if (std::fabs(c.y) > 1e-5f) return fail("origine.y", c.y, 0);
    // 2) punto +Y in alto: NDC y > 0
    v3 up_p = mat4_transform_point(VP, {0, 2, 0});
    if (up_p.y <= 0) return fail("punto +Y deve stare in alto", up_p.y, 1);
    // 3) round-trip: inversa(VP) riporta il punto nel mondo
    if (!mat4_inverse(IVP, VP)) return fail("inversa non calcolabile", 0, 0);
    v3 back = mat4_transform_point(IVP, {up_p.x, up_p.y, up_p.z});
    if (std::fabs(back.x) > 1e-3f || std::fabs(back.y - 2.0f) > 1e-3f)
        return fail("round-trip", back.y, 2);
    // 4) keyframe: zoom interpolato geometricamente, smoothstep ai estremi
    Keyframe ks[] = {{0, 0, 0, 1.0f}, {10, 2, 0, 4.0f}};
    Keyframe k0 = keyframe_eval(ks, 2, 0.0);
    Keyframe k5 = keyframe_eval(ks, 2, 5.0);   // smoothstep(0.5)=0.5 -> zoom geometrico 2
    Keyframe k10 = keyframe_eval(ks, 2, 10.0);
    if (k0.zoom != 1.0f) return fail("kf zoom inizio", k0.zoom, 1);
    if (std::fabs(k5.zoom - 2.0f) > 1e-4f) return fail("kf zoom medio", k5.zoom, 2);
    if (k10.zoom != 4.0f) return fail("kf zoom fine", k10.zoom, 4);
    Keyframe before = keyframe_eval(ks, 2, -5.0), after = keyframe_eval(ks, 2, 99.0);
    if (before.zoom != 1.0f || after.zoom != 4.0f) return fail("kf clamp", 0, 0);
    std::printf("camera_test: OK (4/4)\n");
    return 0;
}
#endif
