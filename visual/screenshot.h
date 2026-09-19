// ============================================================
// screenshot.h — screenshot PNG del frame corrente (tasto S)
//
// Pipeline classica: glReadPixels (riga 0 in basso) -> flip verticale ->
// filtro Paeth per riga -> zlib compress -> chunk IHDR/IDAT/IEND con CRC32.
//
// Il codice segue lo schema standard "PNG writer minimale": stessa idea di
// quello usato nei visualizzatori SDL2+GL con zlib (reperibile in molti
// progetti open), riscritto per questo progetto.
// ============================================================
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include <zlib.h>

#include <OpenGL/gl3.h>

// CRC32 dei chunk PNG (tabella standard)
static inline uint32_t png_crc32(const uint8_t* data, size_t len, uint32_t crc = 0xffffffffu) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
        init = true;
    }
    for (size_t i = 0; i < len; ++i)
        crc = table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
    return crc ^ 0xffffffffu;
}

static inline void png_be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

static inline bool png_write_chunk(std::FILE* f, const char type[4],
                                   const uint8_t* data, size_t len) {
    uint8_t hdr[8];
    png_be32(hdr, (uint32_t)len);
    std::memcpy(hdr + 4, type, 4);
    if (std::fwrite(hdr, 1, 8, f) != 8) return false;
    if (len && std::fwrite(data, 1, len, f) != len) return false;
    uint8_t crcb[4];
    uint32_t crc = png_crc32((const uint8_t*)type, 4);
    crc = png_crc32(data, len, crc);
    png_be32(crcb, crc);
    return std::fwrite(crcb, 1, 4, f) == 4;
}

// Cattura il framebuffer corrente (GL_RGB, 8 bit) e scrive un PNG.
// Ritorna il percorso scritto, o stringa vuota se fallito.
static inline std::string screenshot_save(const std::string& base_name) {
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    const int w = vp[2], h = vp[3];

    std::vector<uint8_t> px((size_t)w * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());

    // flip verticale (glReadPixels parte dal basso, PNG dall'alto)
    std::vector<uint8_t> flipped((size_t)w * h * 3);
    const size_t row = (size_t)w * 3;
    for (int y = 0; y < h; ++y)
        std::memcpy(&flipped[(size_t)(h - 1 - y) * row], &px[(size_t)y * row], row);

    // filtro Paeth per ogni riga (best filter fisso, semplice e robusto)
    std::vector<uint8_t> raw((size_t)h * (row + 1));
    for (int y = 0; y < h; ++y) {
        uint8_t* dst = &raw[(size_t)y * (row + 1)];
        dst[0] = 4;   // Paeth
        const uint8_t* src = &flipped[(size_t)y * row];
        const uint8_t* prev = y > 0 ? &flipped[(size_t)(y - 1) * row] : nullptr;
        for (size_t x = 0; x < row; ++x) {
            int a = x >= 3 ? src[x - 3] : 0;              // left
            int b = prev ? prev[x] : 0;                   // up
            int c = (x >= 3 && prev) ? prev[x - 3] : 0;   // upleft
            int p = a + b - c;
            int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
            int pred = (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
            dst[1 + x] = (uint8_t)(src[x] - pred);
        }
    }

    // compressione zlib dell'intero flusso filtrato
    uLongf comp_len = compressBound(raw.size());
    std::vector<uint8_t> comp(comp_len);
    if (compress2(comp.data(), &comp_len, raw.data(), raw.size(),
                  Z_BEST_SPEED) != Z_OK) {
        std::fprintf(stderr, "screenshot: compressione zlib fallita\n");
        return "";
    }
    comp.resize(comp_len);

    // nome file: base_YYYYMMDD_HHMMSS.png
    char ts[32];
    std::time_t now = std::time(nullptr);
    std::tm* lt = std::localtime(&now);
    std::strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", lt);
    std::string path = base_name + "_" + ts + ".png";

    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        // fallback nella home se pictures/ non è scrivibile
        const char* home = std::getenv("HOME");
        if (!home) {
            std::fprintf(stderr, "screenshot: impossibile aprire %s\n", path.c_str());
            return "";
        }
        path = std::string(home) + "/" + base_name + "_" + ts + ".png";
        f = std::fopen(path.c_str(), "wb");
        if (!f) {
            std::fprintf(stderr, "screenshot: impossibile aprire %s né la home\n", path.c_str());
            return "";
        }
    }

    const uint8_t SIG[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    bool ok = std::fwrite(SIG, 1, 8, f) == 8;
    if (ok) {
        uint8_t ihdr[13];
        png_be32(ihdr, (uint32_t)w);
        png_be32(ihdr + 4, (uint32_t)h);
        ihdr[8] = 8;   // bit depth
        ihdr[9] = 2;   // color type RGB
        ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
        ok = png_write_chunk(f, "IHDR", ihdr, 13);
    }
    if (ok) ok = png_write_chunk(f, "IDAT", comp.data(), comp.size());
    if (ok) ok = png_write_chunk(f, "IEND", nullptr, 0);
    if (std::fclose(f) != 0) ok = false;
    if (!ok) {
        std::fprintf(stderr, "screenshot: scrittura fallita su %s\n", path.c_str());
        return "";
    }
    return path;
}
