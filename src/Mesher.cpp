// Построение геометрии чанков: кубы с AO и плавным светом, жидкости с уровнями,
// растения, факелы, кактусы, снег, заборы, рельсы; подкраска травы и листвы по биому.
#include <algorithm>
#include <cmath>
#include "Vehicle.h"
#include "World.h"

namespace {

const int DIRS[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
// Углы каждой грани против часовой стрелки при взгляде снаружи: низ, верх, верх, низ
const int FACE[6][4][3] = {
    {{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}},
    {{0, 0, 1}, {0, 1, 1}, {0, 1, 0}, {0, 0, 0}},
    {{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}},
    {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}},
    {{1, 0, 1}, {1, 1, 1}, {0, 1, 1}, {0, 0, 1}},
    {{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}},
};
const int TANGENTS[6][2] = {{1, 2}, {1, 2}, {0, 2}, {0, 2}, {0, 1}, {0, 1}};
const float FACE_SHADE[6] = {0.6f, 0.6f, 1.0f, 0.5f, 0.8f, 0.8f}; // как в 1.0
const float AO_CURVE[4] = {0.5f, 0.7f, 0.85f, 1.0f};
// UV углов: (u, «верх»). Атлас загружен сверху вниз, поэтому верх тайла — меньший v
const float CORNER_UV[4][2] = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
const float TS = 1.0f / 16.0f, EPS = 0.0002f;

glm::vec3 rgb(uint32_t c) { return glm::vec3((c >> 16) & 255, (c >> 8) & 255, c & 255) / 255.f; }

bool faceVisible(uint8_t b, uint8_t nb) {
    if (nb == AIR) return true;
    if (isOpaque(nb)) return false;
    if (nb == b) return b == LEAVES;
    if (b == WATER) return !isTranslucent(nb) && nb != LAVA;
    if (b == LAVA) return nb != WATER;
    return true;
}

void upload(Chunk& c, int i, const std::vector<Vertex>& v) {
    if (!c.vao[i]) {
        glGenVertexArrays(1, &c.vao[i]);
        glGenBuffers(1, &c.vbo[i]);
        glBindVertexArray(c.vao[i]);
        glBindBuffer(GL_ARRAY_BUFFER, c.vbo[i]);
        setupVertexAttribs();
    }
    glBindBuffer(GL_ARRAY_BUFFER, c.vbo[i]);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(Vertex), v.empty() ? nullptr : v.data(), GL_STATIC_DRAW);
    c.count[i] = (int)v.size();
}

Vertex V(float x, float y, float z, float u, float v, float shade, float sky, float bl, glm::vec3 t) {
    Vertex r{x, y, z, u, v, shade, sky, bl};
    r.r = t.r; r.g = t.g; r.b = t.b;
    return r;
}

// Двусторонний четырёхугольник (для растений, рельсов)
void quad2(std::vector<Vertex>& out, const Vertex& a, const Vertex& b, const Vertex& c, const Vertex& d) {
    out.insert(out.end(), {a, b, c, a, c, d, a, c, b, a, d, c});
}

// Растение: две диагональные плоскости, каждая с двух сторон
void emitCross(std::vector<Vertex>& out, float x, float y, float z, int tex, float sky, float bl, glm::vec3 t = glm::vec3(1)) {
    float u0 = (tex % 16) * TS + EPS, u1 = (tex % 16 + 1) * TS - EPS;
    float vTop = (tex / 16) * TS + EPS, vBot = (tex / 16 + 1) * TS - EPS;
    const float a = 0.15f, b = 0.85f, l = 0.9f;
    float px[2][2] = {{a, b}, {a, b}}, pz[2][2] = {{a, b}, {b, a}};
    for (int q = 0; q < 2; ++q)
        quad2(out, V(x + px[q][0], y, z + pz[q][0], u0, vBot, l, sky, bl, t), V(x + px[q][0], y + 1, z + pz[q][0], u0, vTop, l, sky, bl, t),
              V(x + px[q][1], y + 1, z + pz[q][1], u1, vTop, l, sky, bl, t), V(x + px[q][1], y, z + pz[q][1], u1, vBot, l, sky, bl, t));
}

// Посевы: решётка «#» из четырёх двусторонних плоскостей
void emitCrop(std::vector<Vertex>& out, float x, float y, float z, int tex, float sky, float bl) {
    float u0 = (tex % 16) * TS + EPS, u1 = (tex % 16 + 1) * TS - EPS;
    float vTop = (tex / 16) * TS + EPS, vBot = (tex / 16 + 1) * TS - EPS;
    const float l = 0.9f;
    const glm::vec3 w(1.f);
    for (float k : {0.25f, 0.75f}) {
        quad2(out, V(x + k, y, z, u0, vBot, l, sky, bl, w), V(x + k, y + 1, z, u0, vTop, l, sky, bl, w),
              V(x + k, y + 1, z + 1, u1, vTop, l, sky, bl, w), V(x + k, y, z + 1, u1, vBot, l, sky, bl, w));
        quad2(out, V(x, y, z + k, u0, vBot, l, sky, bl, w), V(x, y + 1, z + k, u0, vTop, l, sky, bl, w),
              V(x + 1, y + 1, z + k, u1, vTop, l, sky, bl, w), V(x + 1, y, z + k, u1, vBot, l, sky, bl, w));
    }
}

// Факел: 4 плоскости через центр блока (прозрачные пиксели тайла отсекаются) + верхушка.
// Настенный факел сдвинут к стене и наклонён от неё, как в оригинале.
void emitTorch(std::vector<Vertex>& out, float x, float y, float z, uint8_t meta, float sky, float bl, int tex = T(0, 5)) {
    const float px = TS / 16.f;
    float u0 = (tex % 16) * TS + EPS, u1 = (tex % 16 + 1) * TS - EPS;
    float vTop = (tex / 16) * TS + EPS, vBot = (tex / 16 + 1) * TS - EPS;
    glm::vec3 wall(0.f);
    switch (meta) {
    case TORCH_WEST_WALL: wall = {-1, 0, 0}; break;
    case TORCH_EAST_WALL: wall = {1, 0, 0}; break;
    case TORCH_NORTH_WALL: wall = {0, 0, -1}; break;
    case TORCH_SOUTH_WALL: wall = {0, 0, 1}; break;
    default: break;
    }
    bool onWall = glm::length(wall) > 0.f;
    auto P = [&](float lx, float ly, float lz, float u, float v) {
        glm::vec3 p(lx, ly, lz);
        if (onWall) p += wall * (0.5f - 0.4f * ly) + glm::vec3(0, 0.2f, 0);
        return V(x + p.x, y + p.y, z + p.z, u, v, 1.f, sky, bl, glm::vec3(1));
    };
    const float lo = 7.f / 16.f, hi = 9.f / 16.f;
    for (float xp : {lo, hi}) quad2(out, P(xp, 0, 0, u0, vBot), P(xp, 1, 0, u0, vTop), P(xp, 1, 1, u1, vTop), P(xp, 0, 1, u1, vBot));
    for (float zp : {lo, hi}) quad2(out, P(0, 0, zp, u0, vBot), P(0, 1, zp, u0, vTop), P(1, 1, zp, u1, vTop), P(1, 0, zp, u1, vBot));
    float tu0 = (tex % 16) * TS + 7 * px, tu1 = tu0 + 2 * px, tv0 = (tex / 16) * TS + 6 * px, tv1 = tv0 + 2 * px;
    const float top = 10.f / 16.f;
    quad2(out, P(lo, top, lo, tu0, tv0), P(lo, top, hi, tu0, tv1), P(hi, top, hi, tu1, tv1), P(hi, top, lo, tu1, tv0));
}

// Коробка внутри блока (кактус, снег, забор): UV — проекция граней на тайл, свет — свой.
// faces — маска граней по индексам DIRS.
void emitBoxL(std::vector<Vertex>& out, glm::vec3 base, glm::vec3 mn, glm::vec3 mx, const int tex[6], int faces,
              const float sky[6], const float bl[6]);

void emitBox(std::vector<Vertex>& out, glm::vec3 base, glm::vec3 mn, glm::vec3 mx, const int tex[6], int faces,
             float sky, float bl) {
    const float s6[6] = {sky, sky, sky, sky, sky, sky}, b6[6] = {bl, bl, bl, bl, bl, bl};
    emitBoxL(out, base, mn, mx, tex, faces, s6, b6);
}

void emitBoxL(std::vector<Vertex>& out, glm::vec3 base, glm::vec3 mn, glm::vec3 mx, const int tex[6], int faces,
              const float sky[6], const float bl[6]) {
    for (int d = 0; d < 6; ++d) {
        if (!(faces & (1 << d))) continue;
        float tu = (tex[d] % 16) * TS, tv = (tex[d] / 16) * TS;
        Vertex v[4];
        for (int i = 0; i < 4; ++i) {
            const int* cr = FACE[d][i];
            glm::vec3 p(cr[0] ? mx.x : mn.x, cr[1] ? mx.y : mn.y, cr[2] ? mx.z : mn.z);
            float fu, fv; // доли по тайлу: fv = 0 — верх тайла
            if (d < 2) { fu = p.z; fv = 1.f - p.y; }
            else if (d < 4) { fu = p.x; fv = p.z; }
            else { fu = p.x; fv = 1.f - p.y; }
            fu = std::clamp(fu - std::floor(std::min(fu, 0.9999f)), 0.f, 1.f) * (TS - 2 * EPS) + EPS;
            fv = std::clamp(fv - std::floor(std::min(fv, 0.9999f)), 0.f, 1.f) * (TS - 2 * EPS) + EPS;
            v[i] = V(base.x + p.x, base.y + p.y, base.z + p.z, tu + fu, tv + fv, FACE_SHADE[d], sky[d], bl[d], glm::vec3(1));
        }
        out.insert(out.end(), {v[0], v[1], v[2], v[0], v[2], v[3]});
    }
}

// Четырёхугольник с долями тайла в каждой вершине (0..1), одна сторона
void quadUV(std::vector<Vertex>& out, const glm::vec3 p[4], const glm::vec2 uv[4], int tex, float shade, float sky, float bl,
            glm::vec3 tint = glm::vec3(1), bool twoSided = false) {
    Vertex v[4];
    for (int i = 0; i < 4; ++i)
        v[i] = V(p[i].x, p[i].y, p[i].z, (tex % 16) * TS + EPS + std::clamp(uv[i].x, 0.f, 1.f) * (TS - 2 * EPS),
                 (tex / 16) * TS + EPS + std::clamp(uv[i].y, 0.f, 1.f) * (TS - 2 * EPS), shade, sky, bl, tint);
    if (twoSided) quad2(out, v[0], v[1], v[2], v[3]);
    else out.insert(out.end(), {v[0], v[1], v[2], v[0], v[2], v[3]});
}

// Факел-«палочка» вдоль направления up из точки pivot (рычаг, факелы повторителя)
void emitStick(std::vector<Vertex>& out, glm::vec3 pivot, glm::vec3 up, int tex, float sky, float bl, float scale = 1.f) {
    up = glm::normalize(up);
    glm::vec3 a = glm::normalize(glm::cross(up, std::abs(up.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
    glm::vec3 c = glm::cross(up, a);
    auto P = [&](float lx, float ly, float lz) { return pivot + (a * (lx - 0.5f) + up * ly + c * (lz - 0.5f)) * scale; };
    const float lo = 7.f / 16.f, hi = 9.f / 16.f;
    for (float xp : {lo, hi}) {
        glm::vec3 p[4] = {P(xp, 0, 0), P(xp, 1, 0), P(xp, 1, 1), P(xp, 0, 1)};
        glm::vec2 uv[4] = {{0, 1}, {0, 0}, {1, 0}, {1, 1}};
        quadUV(out, p, uv, tex, 1.f, sky, bl, glm::vec3(1), true);
    }
    for (float zp : {lo, hi}) {
        glm::vec3 p[4] = {P(0, 0, zp), P(0, 1, zp), P(1, 1, zp), P(1, 0, zp)};
        glm::vec2 uv[4] = {{0, 1}, {0, 0}, {1, 0}, {1, 1}};
        quadUV(out, p, uv, tex, 1.f, sky, bl, glm::vec3(1), true);
    }
    // Верхушка — квадратик 2x2 пикселя из середины тайла
    glm::vec3 t[4] = {P(lo, 10 / 16.f, lo), P(lo, 10 / 16.f, hi), P(hi, 10 / 16.f, hi), P(hi, 10 / 16.f, lo)};
    glm::vec2 tuv[4] = {{7 / 16.f, 6 / 16.f}, {7 / 16.f, 8 / 16.f}, {9 / 16.f, 8 / 16.f}, {9 / 16.f, 6 / 16.f}};
    quadUV(out, t, tuv, tex, 1.f, sky, bl, glm::vec3(1), true);
}

// Коробка в локальной системе поршня (+Y — вперёд), повёрнутая в направление f.
// tex[6] — тайлы по локальным граням; UV как у emitBox (бока «верхом» вперёд)
void emitOriented(std::vector<Vertex>& out, glm::vec3 base, glm::vec3 mn, glm::vec3 mx, int f, const int tex[6], float sky, float bl) {
    for (int d = 0; d < 6; ++d) {
        float tu = (tex[d] % 16) * TS, tv = (tex[d] / 16) * TS;
        glm::vec3 n = orientDir(glm::vec3(DIRS[d][0], DIRS[d][1], DIRS[d][2]), f);
        int wd = n.x > 0.5f ? 0 : n.x < -0.5f ? 1 : n.y > 0.5f ? 2 : n.y < -0.5f ? 3 : n.z > 0.5f ? 4 : 5;
        Vertex v[4];
        for (int i = 0; i < 4; ++i) {
            const int* cr = FACE[d][i];
            glm::vec3 p(cr[0] ? mx.x : mn.x, cr[1] ? mx.y : mn.y, cr[2] ? mx.z : mn.z);
            float fu, fv;
            if (d < 2) { fu = p.z; fv = 1.f - p.y; }
            else if (d < 4) { fu = p.x; fv = p.z; }
            else { fu = p.x; fv = 1.f - p.y; }
            fu = std::clamp(fu, 0.f, 1.f) * (TS - 2 * EPS) + EPS;
            fv = std::clamp(fv, 0.f, 1.f) * (TS - 2 * EPS) + EPS;
            glm::vec3 w = base + orientPoint(p, f);
            v[i] = V(w.x, w.y, w.z, tu + fu, tv + fv, FACE_SHADE[wd], sky, bl, glm::vec3(1));
        }
        out.insert(out.end(), {v[0], v[1], v[2], v[0], v[2], v[3]});
    }
}

// Цвет редстоун-пыли по силе сигнала (как в 1.0)
glm::vec3 wireColor(int level) {
    float f = level / 15.f;
    float r = level == 0 ? 0.3f : f * 0.6f + 0.4f;
    return glm::vec3(r, std::max(0.f, f * f * 0.7f - 0.5f), std::max(0.f, f * f * 0.6f - 0.7f));
}

} // namespace

void setupVertexAttribs() {
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(8 * sizeof(float)));
}

void appendBlockModel(std::vector<Vertex>& out, uint8_t b, float sky, float block, uint8_t meta) {
    if (b == AIR) return;
    if (isPlant(b) || b == COBWEB) { emitCross(out, 0, 0, 0, blockTex(b, 4, meta), sky, block); return; }
    if (b == TORCH || isRedstoneTorch(b)) { emitTorch(out, 0, 0, 0, TORCH_FLOOR, sky, block, blockTex(b, 4, 5)); return; }
    if (b == WHEAT) { emitCrop(out, 0, 0, 0, blockTex(b, 4, 7), sky, block); return; }
    int tex[6];
    for (int d = 0; d < 6; ++d) tex[d] = blockTex(b, d, (b == SLAB || b == WOOL) ? meta : meta);
    if (b == DISPENSER || b == FURNACE) tex[5] = blockInfo(b).front; // лицом к зрителю
    std::vector<std::pair<glm::vec3, glm::vec3>> boxes;
    itemModelBoxes(b, boxes);
    for (const auto& [mn, mx] : boxes) emitBox(out, glm::vec3(0.f), mn, mx, tex, 63, sky, block);
}

void World::buildMesh(Chunk& c) {
    // Копия чанка, метаданных и света с рамкой в 1 блок: [-1..16] x [-1..CH] x [-1..16]
    constexpr int PW = CW + 2, PH = CH + 2;
    static std::vector<uint8_t> pad(PW * PH * PW), mpad(PW * PH * PW), lpad(PW * PH * PW);
    auto I = [&](int x, int y, int z) { return ((y + 1) * PW + (z + 1)) * PW + (x + 1); };
    auto P = [&](int x, int y, int z) -> uint8_t { return pad[I(x, y, z)]; };
    auto M = [&](int x, int y, int z) -> uint8_t { return mpad[I(x, y, z)]; };
    auto L = [&](int x, int y, int z) -> uint8_t { return lpad[I(x, y, z)]; };

    const Chunk* nb[3][3];
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx) nb[dx + 1][dz + 1] = chunkAt(c.cx + dx, c.cz + dz);

    // Цвет травы и листвы по биому колонки
    glm::vec3 grassCol[PW][PW], foliageCol[PW][PW];
    for (int z = -1; z <= CW; ++z) {
        for (int x = -1; x <= CW; ++x) {
            int dx = x < 0 ? -1 : (x >= CW ? 1 : 0);
            int dz = z < 0 ? -1 : (z >= CW ? 1 : 0);
            const Chunk* src = nb[dx + 1][dz + 1];
            int lx = x - dx * CW, lz = z - dz * CW;
            Biome bio = (Biome)(src ? src->biome[lz * CW + lx] : c.biome[std::clamp(z, 0, CW - 1) * CW + std::clamp(x, 0, CW - 1)]);
            grassCol[x + 1][z + 1] = rgb(biomeInfo(bio).grassColor);
            foliageCol[x + 1][z + 1] = rgb(biomeInfo(bio).foliageColor);

            pad[I(x, -1, z)] = BEDROCK; lpad[I(x, -1, z)] = 0; mpad[I(x, -1, z)] = 0;
            pad[I(x, CH, z)] = AIR;     lpad[I(x, CH, z)] = 0xF0; mpad[I(x, CH, z)] = 0;
            for (int y = 0; y < CH; ++y) {
                int i = I(x, y, z);
                if (src) {
                    int si = Chunk::index(lx, y, lz);
                    pad[i] = src->blocks[si];
                    mpad[i] = src->meta[si];
                    lpad[i] = src->light[si];
                } else {
                    pad[i] = AIR;
                    mpad[i] = 0;
                    lpad[i] = 0xF0;
                }
            }
        }
    }
    auto tint = [&](int type, int x, int z) -> glm::vec3 {
        switch (type) {
        case 1: return grassCol[x + 1][z + 1];
        case 2: return foliageCol[x + 1][z + 1];
        case 3: return rgb(0x619961); // ель
        case 4: return rgb(0x80A755); // берёза
        default: return glm::vec3(1.f);
        }
    };

    // Высота жидкости в углу (x, z) — среднее по четырём колонкам вокруг, как getFluidHeight в 1.0
    auto cornerHeight = [&](int x, int y, int z, uint8_t liq) -> float {
        float sum = 0.f;
        int cnt = 0;
        for (int i = 0; i < 2; ++i)
            for (int j = 0; j < 2; ++j) {
                int cx = x - i, cz = z - j;
                if (P(cx, y + 1, cz) == liq) return 1.f;
                uint8_t bb = P(cx, y, cz);
                if (bb == liq) {
                    uint8_t m = M(cx, y, cz);
                    float f = ((m >= 8 ? 0 : m) + 1) / 9.f;
                    if (m >= 8 || m == 0) { sum += f * 10.f; cnt += 10; }
                    else { sum += f; ++cnt; }
                } else if (!isSolid(bb)) {
                    sum += 1.f;
                    ++cnt;
                }
            }
        return cnt ? 1.f - sum / cnt : 1.f;
    };

    std::vector<Vertex> mesh[MESH_COUNT];
    mesh[MESH_SOLID].reserve(40000);
    const float inv15 = 1.f / 15.f;
    c.chests.clear();
    c.endPortals.clear();

    for (int y = 0; y < CH; ++y) {
        for (int z = 0; z < CW; ++z) {
            for (int x = 0; x < CW; ++x) {
                uint8_t b = P(x, y, z);
                if (b == AIR) continue;
                uint8_t meta = M(x, y, z);
                Shape shape = blockShape(b);
                uint8_t l = L(x, y, z);
                float ownSky = (l >> 4) * inv15, ownBl = (l & 15) * inv15;
                glm::vec3 base((float)x, (float)y, (float)z);

                // ---- Нестандартные формы
                if (isPlant(b) || b == TORCH || isRedstoneTorch(b) || b == COBWEB) {
                    if (b == TORCH || isRedstoneTorch(b))
                        emitTorch(mesh[MESH_CUTOUT], base.x, base.y, base.z, meta, ownSky, ownBl, blockTex(b, 4, meta));
                    else if (b == WHEAT) emitCrop(mesh[MESH_CUTOUT], base.x, base.y, base.z, blockTex(b, 4, meta), ownSky, ownBl);
                    else {
                        glm::vec3 o = hasPlantOffset(b) ? plantOffset(b, c.cx * CW + x, c.cz * CW + z) : glm::vec3(0.f);
                        emitCross(mesh[MESH_CUTOUT], base.x + o.x, base.y + o.y, base.z + o.z, blockTex(b, 4, meta), ownSky, ownBl,
                                  tint(tintType(b, 4, meta), x, z));
                    }
                    continue;
                }
                if (shape == Shape::Cactus) {
                    int tex[6];
                    for (int d = 0; d < 6; ++d) tex[d] = blockTex(b, d, meta);
                    const float k = 1.f / 16.f;
                    int caps = (P(x, y + 1, z) != CACTUS ? 4 : 0) | (P(x, y - 1, z) != CACTUS ? 8 : 0);
                    emitBox(mesh[MESH_CUTOUT], base, glm::vec3(0.f), glm::vec3(1.f), tex, caps, ownSky, ownBl);
                    emitBox(mesh[MESH_CUTOUT], base, glm::vec3(k, 0.f, k), glm::vec3(1 - k, 1.f, 1 - k), tex, 1 | 2 | 16 | 32, ownSky, ownBl);
                    continue;
                }
                if (shape == Shape::SnowLayer) {
                    int tex[6];
                    for (int d = 0; d < 6; ++d) tex[d] = T(2, 4);
                    int faces = 4;
                    for (int d = 0; d < 6; ++d) {
                        if (d == 2) continue;
                        uint8_t n = P(x + DIRS[d][0], y + DIRS[d][1], z + DIRS[d][2]);
                        if (!isOpaque(n) && n != SNOW_LAYER) faces |= 1 << d;
                    }
                    emitBox(mesh[MESH_SOLID], base, glm::vec3(0.f), glm::vec3(1.f, 0.125f, 1.f), tex, faces, ownSky, ownBl);
                    continue;
                }
                if (shape == Shape::Fence) {
                    int tex[6];
                    for (int d = 0; d < 6; ++d) tex[d] = blockTex(b, d, meta);
                    const float a = 6.f / 16.f, bb = 10.f / 16.f;
                    emitBox(mesh[MESH_SOLID], base, glm::vec3(a, 0, a), glm::vec3(bb, 1, bb), tex, 63, ownSky, ownBl);
                    // Перекладины к соседним заборам, калиткам и сплошным блокам (как в 1.0)
                    auto connects = [&](uint8_t n) { return n == b || isFence(n) || n == FENCE_GATE || isOpaque(n); };
                    const float p0 = 7.f / 16.f, p1 = 9.f / 16.f;
                    for (float yb : {6.f / 16.f, 12.f / 16.f}) {
                        float yt = yb + 3.f / 16.f;
                        if (connects(P(x + 1, y, z))) emitBox(mesh[MESH_SOLID], base, {bb, yb, p0}, {1.f, yt, p1}, tex, 63, ownSky, ownBl);
                        if (connects(P(x - 1, y, z))) emitBox(mesh[MESH_SOLID], base, {0.f, yb, p0}, {a, yt, p1}, tex, 63, ownSky, ownBl);
                        if (connects(P(x, y, z + 1))) emitBox(mesh[MESH_SOLID], base, {p0, yb, bb}, {p1, yt, 1.f}, tex, 63, ownSky, ownBl);
                        if (connects(P(x, y, z - 1))) emitBox(mesh[MESH_SOLID], base, {p0, yb, 0.f}, {p1, yt, a}, tex, 63, ownSky, ownBl);
                    }
                    continue;
                }
                if (shape == Shape::Rail) {
                    // Рельсы: прямые, подъёмы (наклонная плоскость) и повороты; энергорельсы и детекторные — без поворотов
                    int rs = b == RAIL ? meta : (meta & 7);
                    int tex = blockTex(b, 2, meta);
                    float u0 = (tex % 16) * TS + EPS, u1 = (tex % 16 + 1) * TS - EPS;
                    float v0 = (tex / 16) * TS + EPS, v1 = (tex / 16 + 1) * TS - EPS;
                    const glm::vec3 w(1.f);
                    const float h = 1.f / 16.f;
                    auto& cut = mesh[MESH_CUTOUT];
                    if (rs >= 6) {
                        // Поворот: тайл повёрнут по форме (6 юг+восток, 7 юг+запад, 8 север+запад, 9 север+восток)
                        glm::vec2 uv[4] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
                        int rot = rs == 6 ? 0 : rs == 7 ? 1 : rs == 8 ? 2 : 3;
                        glm::vec3 pts[4] = {{0, h, 0}, {1, h, 0}, {1, h, 1}, {0, h, 1}};
                        Vertex vq[4];
                        for (int i = 0; i < 4; ++i) {
                            glm::vec2 q = uv[(i + 4 - rot) & 3]; // поворот по часовой: 6 юг+восток, 7 юг+запад, 8 север+запад, 9 север+восток
                            vq[i] = V(base.x + pts[i].x, base.y + pts[i].y, base.z + pts[i].z, q.x, q.y, 1, ownSky, ownBl, w);
                        }
                        quad2(cut, vq[0], vq[1], vq[2], vq[3]);
                    } else {
                        glm::vec3 A, B2;
                        railExits(rs, A, B2);
                        glm::vec3 dir = B2 - A;
                        glm::vec3 perp = glm::normalize(glm::vec3(-dir.z, 0, dir.x)) * 0.5f;
                        glm::vec3 a0 = base + A + perp + glm::vec3(0, h, 0), a1 = base + A - perp + glm::vec3(0, h, 0);
                        glm::vec3 b0 = base + B2 - perp + glm::vec3(0, h, 0), b1 = base + B2 + perp + glm::vec3(0, h, 0);
                        quad2(cut, V(a0.x, a0.y, a0.z, u0, v1, 1, ownSky, ownBl, w), V(a1.x, a1.y, a1.z, u1, v1, 1, ownSky, ownBl, w),
                              V(b0.x, b0.y, b0.z, u1, v0, 1, ownSky, ownBl, w), V(b1.x, b1.y, b1.z, u0, v0, 1, ownSky, ownBl, w));
                    }
                    continue;
                }

                // ---- Кровать, редстоун: пыль, рычаг, кнопка, плита, повторитель; таблички рисует игра
                if (shape == Shape::Sign) continue;
                if (shape == Shape::Chest || shape == Shape::Skull) { c.chests.push_back(glm::ivec3(c.cx * CW + x, y, c.cz * CW + z)); continue; }
                if (shape == Shape::Anvil || shape == Shape::Beacon || shape == Shape::FlowerPot) {
                    const float k = 1.f / 16.f;
                    int edgeMask = 63; // грани на краю блока прячутся за непрозрачным соседом
                    for (int d = 0; d < 6; ++d)
                        if (isOpaque(P(x + DIRS[d][0], y + DIRS[d][1], z + DIRS[d][2]))) edgeMask &= ~(1 << d);
                    int tex[6];
                    for (int d = 0; d < 6; ++d) tex[d] = blockTex(b, d, meta);
                    if (shape == Shape::Beacon) {
                        // RenderBlocks.renderBlockBeacon 1.4.2: стекло, обсидиановое основание, ядро маяка
                        int glass[6], obs[6], core[6];
                        for (int d = 0; d < 6; ++d) { glass[d] = T(1, 3); obs[d] = T(5, 2); core[d] = T(9, 2); }
                        emitBox(mesh[MESH_CUTOUT], base, {0, 0, 0}, {1, 1, 1}, glass, edgeMask, ownSky, ownBl);
                        emitBox(mesh[MESH_SOLID], base, {2 * k, 0.1f * k, 2 * k}, {14 * k, 3 * k, 14 * k}, obs, 63, ownSky, ownBl);
                        emitBox(mesh[MESH_SOLID], base, {3 * k, 3 * k, 3 * k}, {13 * k, 14 * k, 13 * k}, core, 63, ownSky, ownBl);
                    } else if (shape == Shape::FlowerPot) {
                        emitBox(mesh[MESH_CUTOUT], base, {5 * k, 0, 5 * k}, {11 * k, 6 * k, 11 * k}, tex, edgeMask | 0x37, ownSky, ownBl);
                    } else {
                        // Наковальня (renderBlockAnvilOrient 1.4.2): четыре коробки, верхняя грань верхней — своя текстура;
                        // бит 0 меты — длинная сторона вдоль X
                        bool alongX = meta & 1;
                        std::vector<std::pair<glm::vec3, glm::vec3>> parts;
                        itemModelBoxes(ANVIL, parts);
                        int baseTex[6];
                        for (int d = 0; d < 6; ++d) baseTex[d] = T(7, 13);
                        for (size_t i = 0; i < parts.size(); ++i) {
                            glm::vec3 mn = parts[i].first, mx = parts[i].second;
                            if (alongX) { std::swap(mn.x, mn.z); std::swap(mx.x, mx.z); }
                            bool top = i + 1 == parts.size();
                            // На краю блока: у основания — низ, у верха — торцы по длинной стороне (верх рисуется отдельно)
                            int ends = alongX ? 0x03 : 0x30;
                            int mask = top ? ((edgeMask & ends) | (0x3F & ~ends & ~4)) : i == 0 ? (edgeMask | 0x37) : 63;
                            emitBox(mesh[MESH_SOLID], base, mn, mx, baseTex, mask, ownSky, ownBl);
                            if (top) {
                                // Верх: рисунок вдоль длинной стороны (при повороте UV транспонируются)
                                glm::vec3 q[4] = {base + glm::vec3(mn.x, mx.y, mn.z), base + glm::vec3(mn.x, mx.y, mx.z),
                                                  base + glm::vec3(mx.x, mx.y, mx.z), base + glm::vec3(mx.x, mx.y, mn.z)};
                                glm::vec2 uv[4];
                                for (int j = 0; j < 4; ++j) {
                                    glm::vec3 lp = q[j] - base;
                                    uv[j] = alongX ? glm::vec2(lp.z, lp.x) : glm::vec2(lp.x, lp.z);
                                }
                                if (edgeMask & 4) quadUV(mesh[MESH_SOLID], q, uv, tex[2], 1.f, ownSky, ownBl, glm::vec3(1), true);
                            }
                        }
                    }
                    continue;
                }
                if (shape == Shape::Piston || shape == Shape::PistonHead) {
                    int f = meta & 7;
                    const float k = 1.f / 16.f;
                    const int side = T(12, 6), back = T(13, 6);
                    // Порядок граней DIRS: +X, -X, +Y (перед), -Y (зад), +Z, -Z
                    if (shape == Shape::Piston) {
                        bool ext = meta & 8;
                        int front = ext ? T(14, 6) : (b == STICKY_PISTON ? T(10, 6) : T(11, 6));
                        const int tex[6] = {side, side, front, back, side, side};
                        emitOriented(mesh[MESH_SOLID], base, {0, 0, 0}, {1, ext ? 12 * k : 1.f, 1}, f, tex, ownSky, ownBl);
                    } else {
                        int front = (meta & 8) ? T(10, 6) : T(11, 6);
                        const int plate[6] = {side, side, front, T(11, 6), side, side};
                        emitOriented(mesh[MESH_SOLID], base, {0, 12 * k, 0}, {1, 1, 1}, f, plate, ownSky, ownBl);
                        const int arm[6] = {side, side, side, side, side, side};
                        emitOriented(mesh[MESH_SOLID], base, {6 * k, -4 * k, 6 * k}, {10 * k, 12 * k, 10 * k}, f, arm, ownSky, ownBl);
                    }
                    continue;
                }
                if (shape == Shape::Table || shape == Shape::Brewing || shape == Shape::Cauldron) {
                    const float k = 1.f / 16.f;
                    auto& cut = mesh[MESH_CUTOUT];
                    if (shape == Shape::Table) {
                        int tex[6];
                        for (int d = 0; d < 6; ++d) tex[d] = blockTex(b, d, meta);
                        emitBox(mesh[MESH_SOLID], base, {0, 0, 0}, {1, 0.75f, 1}, tex, 63, ownSky, ownBl);
                    } else if (shape == Shape::Brewing) {
                        // Каменное основание и стержень; бутылки — полупрозрачный тайл крест-накрест
                        int st[6];
                        for (int d = 0; d < 6; ++d) st[d] = T(12, 9);
                        emitBox(cut, base, {9 * k, 0, 5 * k}, {15 * k, 2 * k, 11 * k}, st, 63, ownSky, ownBl);
                        emitBox(cut, base, {2 * k, 0, 1 * k}, {8 * k, 2 * k, 7 * k}, st, 63, ownSky, ownBl);
                        emitBox(cut, base, {2 * k, 0, 9 * k}, {8 * k, 2 * k, 15 * k}, st, 63, ownSky, ownBl);
                        int rod[6];
                        for (int d = 0; d < 6; ++d) rod[d] = T(13, 9);
                        emitBox(cut, base, {7 * k, 0, 7 * k}, {9 * k, 14 * k, 9 * k}, rod, 63, ownSky, ownBl);
                        emitCross(cut, base.x, base.y, base.z, T(13, 9), ownSky, ownBl);
                    } else {
                        // Котёл: дно, четыре стенки, внутри вода по уровню
                        int side[6];
                        for (int d = 0; d < 6; ++d) side[d] = blockTex(b, d, 0);
                        emitBox(cut, base, {0, 0, 0}, {1, 1, 2 * k}, side, 63, ownSky, ownBl);
                        emitBox(cut, base, {0, 0, 1 - 2 * k}, {1, 1, 1}, side, 63, ownSky, ownBl);
                        emitBox(cut, base, {0, 0, 2 * k}, {2 * k, 1, 1 - 2 * k}, side, 63, ownSky, ownBl);
                        emitBox(cut, base, {1 - 2 * k, 0, 2 * k}, {1, 1, 1 - 2 * k}, side, 63, ownSky, ownBl);
                        int in[6];
                        for (int d = 0; d < 6; ++d) in[d] = T(11, 8);
                        emitBox(cut, base, {2 * k, 3 * k, 2 * k}, {1 - 2 * k, 4 * k, 1 - 2 * k}, in, 4, ownSky, ownBl);
                        if (meta > 0) {
                            int wt[6];
                            for (int d = 0; d < 6; ++d) wt[d] = T(13, 12);
                            float h = (6.f + 3.f * std::min<int>(meta, 3)) / 16.f;
                            emitBox(mesh[MESH_TRANSLUCENT], base, {2 * k, h - 0.01f, 2 * k}, {1 - 2 * k, h, 1 - 2 * k}, wt, 4, ownSky, ownBl);
                        }
                    }
                    continue;
                }
                if (shape == Shape::Frame || shape == Shape::EndPortal || shape == Shape::Egg) {
                    const float k = 1.f / 16.f;
                    if (shape == Shape::Frame) {
                        int tex[6];
                        for (int d = 0; d < 6; ++d) tex[d] = blockTex(b, d, meta);
                        emitBox(mesh[MESH_SOLID], base, {0, 0, 0}, {1, 13 * k, 1}, tex, 63, ownSky, ownBl);
                        if (meta & 4) {
                            int eye[6];
                            for (int d = 0; d < 6; ++d) eye[d] = T(14, 10);
                            emitBox(mesh[MESH_SOLID], base, {4 * k, 13 * k, 4 * k}, {12 * k, 16 * k, 12 * k}, eye, 63 & ~8, ownSky, ownBl);
                        }
                    } else if (shape == Shape::EndPortal) {
                        // Рисует игра (TileEntityEndPortalRenderer): 16 слоёв «космоса» с параллаксом
                        c.endPortals.push_back(glm::ivec3(c.cx * CW + x, y, c.cz * CW + z));
                    } else {
                        int tex[6];
                        for (int d = 0; d < 6; ++d) tex[d] = T(7, 10);
                        // Яйцо — стопка сужающихся коробок (RenderBlocks.renderBlockDragonEgg)
                        const float rows[8][2] = {{0, 6}, {1, 8}, {2, 10}, {3, 12}, {5, 14}, {8, 14}, {11, 12}, {13, 8}};
                        for (int i = 0; i < 8; ++i) {
                            float w = rows[i][1] * k * 0.5f, y0 = rows[i][0] * k, y1 = (i < 7 ? rows[i + 1][0] : 16) * k;
                            if (i == 0) y1 = 1 * k;
                            emitBox(mesh[MESH_SOLID], base, {0.5f - w, y0, 0.5f - w}, {0.5f + w, y1, 0.5f + w}, tex, 63, ownSky, ownBl);
                        }
                    }
                    continue;
                }
                if (shape == Shape::Stem || shape == Shape::Vine || shape == Shape::LilyPad) {
                    auto& cut = mesh[MESH_CUTOUT];
                    const float k = 1.f / 16.f;
                    if (shape == Shape::Stem) {
                        // Цвет по возрасту: от зелёного к жёлто-бурому (BlockStem.getRenderColor)
                        int age = meta & 7;
                        glm::vec3 col(age * 32 / 255.f, (255 - age * 8) / 255.f, age * 4 / 255.f);
                        uint8_t fruit = b == PUMPKIN_STEM ? PUMPKIN : MELON_BLOCK;
                        int fd = -1;
                        for (int d : {0, 1, 4, 5})
                            if (P(x + DIRS[d][0], y, z + DIRS[d][2]) == fruit) fd = d;
                        if (age == 7 && fd >= 0) {
                            // Изогнутый стебель к плоду
                            int t = T(15, 7);
                            float u0 = (t % 16) * TS + EPS, u1 = (t % 16 + 1) * TS - EPS, vT = (t / 16) * TS + EPS, vB = (t / 16 + 1) * TS - EPS;
                            glm::vec3 a = base + glm::vec3(0.5f, 0, 0.5f), dir(DIRS[fd][0] * 0.5f, 0, DIRS[fd][2] * 0.5f);
                            glm::vec3 q0 = a - dir, q1 = a + dir;
                            if (DIRS[fd][0] + DIRS[fd][2] < 0) std::swap(q0, q1);
                            quad2(cut, V(q0.x, q0.y, q0.z, u1, vB, 1, ownSky, ownBl, col), V(q0.x, q0.y + 0.5f, q0.z, u1, vT, 1, ownSky, ownBl, col),
                                  V(q1.x, q1.y + 0.5f, q1.z, u0, vT, 1, ownSky, ownBl, col), V(q1.x, q1.y, q1.z, u0, vB, 1, ownSky, ownBl, col));
                        } else {
                            float h = (age * 2 + 2) * k;
                            int t = T(15, 6);
                            float u0 = (t % 16) * TS + EPS, u1 = (t % 16 + 1) * TS - EPS;
                            float vB = (t / 16 + 1) * TS - EPS, vT = vB - h * TS;
                            const float a2 = 0.15f, b2 = 0.85f;
                            float px[2][2] = {{a2, b2}, {a2, b2}}, pz[2][2] = {{a2, b2}, {b2, a2}};
                            for (int q = 0; q < 2; ++q)
                                quad2(cut, V(base.x + px[q][0], base.y, base.z + pz[q][0], u0, vB, 1, ownSky, ownBl, col),
                                      V(base.x + px[q][0], base.y + h, base.z + pz[q][0], u0, vT, 1, ownSky, ownBl, col),
                                      V(base.x + px[q][1], base.y + h, base.z + pz[q][1], u1, vT, 1, ownSky, ownBl, col),
                                      V(base.x + px[q][1], base.y, base.z + pz[q][1], u1, vB, 1, ownSky, ownBl, col));
                        }
                    } else if (shape == Shape::Vine) {
                        glm::vec3 col = tint(2, x, z);
                        int t = T(15, 8);
                        float u0 = (t % 16) * TS + EPS, u1 = (t % 16 + 1) * TS - EPS, vT = (t / 16) * TS + EPS, vB = (t / 16 + 1) * TS - EPS;
                        const float o = k * 0.8f;
                        auto wall = [&](glm::vec3 p0, glm::vec3 p1) {
                            quad2(cut, V(p0.x, p0.y, p0.z, u0, vB, 0.8f, ownSky, ownBl, col), V(p0.x, p0.y + 1, p0.z, u0, vT, 0.8f, ownSky, ownBl, col),
                                  V(p1.x, p1.y + 1, p1.z, u1, vT, 0.8f, ownSky, ownBl, col), V(p1.x, p1.y, p1.z, u1, vB, 0.8f, ownSky, ownBl, col));
                        };
                        if (meta & 1) wall(base + glm::vec3(0, 0, 1 - o), base + glm::vec3(1, 0, 1 - o));
                        if (meta & 2) wall(base + glm::vec3(o, 0, 0), base + glm::vec3(o, 0, 1));
                        if (meta & 4) wall(base + glm::vec3(0, 0, o), base + glm::vec3(1, 0, o));
                        if (meta & 8) wall(base + glm::vec3(1 - o, 0, 0), base + glm::vec3(1 - o, 0, 1));
                        if (meta == 0) { // лоза под потолком
                            glm::vec3 a = base + glm::vec3(0, 1 - o, 0);
                            quad2(cut, V(a.x, a.y, a.z, u0, vT, 1, ownSky, ownBl, col), V(a.x + 1, a.y, a.z, u1, vT, 1, ownSky, ownBl, col),
                                  V(a.x + 1, a.y, a.z + 1, u1, vB, 1, ownSky, ownBl, col), V(a.x, a.y, a.z + 1, u0, vB, 1, ownSky, ownBl, col));
                        }
                    } else {
                        // Кувшинка: плоская, повёрнута «случайно» по координатам
                        glm::vec3 col(0x20 / 255.f, 0x80 / 255.f, 0x30 / 255.f);
                        int t = T(12, 4);
                        float u0 = (t % 16) * TS + EPS, u1 = (t % 16 + 1) * TS - EPS, v0 = (t / 16) * TS + EPS, v1 = (t / 16 + 1) * TS - EPS;
                        int wx = c.cx * CW + x, wz = c.cz * CW + z;
                        int rot = (int)((uint32_t)(wx * 3129871 ^ wz * 116129781) >> 16) & 3;
                        glm::vec2 uv[4] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
                        float yy = base.y + 1.f / 64.f;
                        glm::vec3 pts[4] = {{base.x, yy, base.z}, {base.x + 1, yy, base.z}, {base.x + 1, yy, base.z + 1}, {base.x, yy, base.z + 1}};
                        Vertex v4[4];
                        for (int i = 0; i < 4; ++i) {
                            glm::vec2 q = uv[(i + rot) & 3];
                            v4[i] = V(pts[i].x, pts[i].y, pts[i].z, q.x, q.y, 1, ownSky, ownBl, col);
                        }
                        quad2(cut, v4[0], v4[1], v4[2], v4[3]);
                    }
                    continue;
                }
                if (shape == Shape::Portal) {
                    // Портал: пластина толщиной 1/4 посреди блока, полупрозрачная и светящаяся
                    int tex[6];
                    for (int d = 0; d < 6; ++d) tex[d] = T(14, 0);
                    const float a = 6.f / 16.f, c2 = 10.f / 16.f;
                    glm::vec3 mn = (meta & 1) ? glm::vec3(a, 0, 0) : glm::vec3(0, 0, a);
                    glm::vec3 mx = (meta & 1) ? glm::vec3(c2, 1, 1) : glm::vec3(1, 1, c2);
                    int faces = 0;
                    for (int d = 0; d < 6; ++d)
                        if (P(x + DIRS[d][0], y + DIRS[d][1], z + DIRS[d][2]) != PORTAL) faces |= 1 << d;
                    emitBox(mesh[MESH_TRANSLUCENT], base, mn, mx, tex, faces, ownSky, 1.f);
                    continue;
                }
                if (shape == Shape::Bed || shape == Shape::Wire || shape == Shape::Lever || shape == Shape::Button ||
                    shape == Shape::Plate || shape == Shape::Repeater) {
                    auto& cut = mesh[MESH_CUTOUT];
                    const float k = 1.f / 16.f;
                    const glm::vec3 white(1.f);
                    // along/across для направления f (0 +X, 1 +Z, 2 -X, 3 -Z): along = 1 у переднего края
                    auto alongOf = [](int f, float x, float z) { return f == 0 ? x : f == 1 ? z : f == 2 ? 1 - x : 1 - z; };
                    auto acrossOf = [](int f, float x, float z) { return f == 0 ? z : f == 1 ? 1 - x : f == 2 ? 1 - z : x; };
                    // vAlong — у тайла «длина» по вертикали (повторитель: дорожка сверху вниз, выход вверху), у кровати — по горизонтали
                    auto topRot = [&](float y0, int f, int tile, float shade, bool vAlong = false) {
                        glm::vec3 p[4] = {base + glm::vec3(0, y0, 0), base + glm::vec3(0, y0, 1), base + glm::vec3(1, y0, 1),
                                          base + glm::vec3(1, y0, 0)};
                        const float cxz[4][2] = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
                        glm::vec2 uv[4];
                        for (int i = 0; i < 4; ++i)
                            uv[i] = vAlong ? glm::vec2(acrossOf(f, cxz[i][0], cxz[i][1]), 1.f - alongOf(f, cxz[i][0], cxz[i][1]))
                                           : glm::vec2(alongOf(f, cxz[i][0], cxz[i][1]), acrossOf(f, cxz[i][0], cxz[i][1]));
                        quadUV(cut, p, uv, tile, shade, ownSky, ownBl);
                    };
                    if (shape == Shape::Bed) {
                        int f = meta & 3;
                        bool head = meta & 8;
                        const float h = 9 * k;
                        // Верх: подушка (u = 1) у изголовья
                        topRot(h, f, head ? T(7, 8) : T(6, 8), 1.f);
                        // Низ на высоте ножек
                        int bt[6];
                        for (int d = 0; d < 6; ++d) bt[d] = T(4, 0);
                        emitBox(cut, base, {0, 3 * k, 0}, {1, 3 * k, 1}, bt, 8, ownSky * 0.5f, ownBl * 0.5f);
                        // Бока: вдоль длины — боковой тайл, торцы — торцевые, к другой половине — не рисуем
                        static const int DIRF[4] = {0, 4, 1, 5}; // f -> индекс грани
                        int towardHead = DIRF[f], towardFoot = DIRF[(f + 2) & 3];
                        for (int d : {0, 1, 4, 5}) {
                            if ((head && d == towardFoot) || (!head && d == towardHead)) continue;
                            int tile;
                            if (d == towardHead) tile = T(8, 9);
                            else if (d == towardFoot) tile = T(5, 9);
                            else tile = head ? T(7, 9) : T(6, 9);
                            glm::vec3 p[4];
                            glm::vec2 uv[4];
                            for (int i = 0; i < 4; ++i) {
                                const int* cr = FACE[d][i];
                                float px = (float)cr[0], py = cr[1] ? h : 0.f, pz = (float)cr[2];
                                p[i] = base + glm::vec3(px, py, pz);
                                float u = (d == towardHead || d == towardFoot) ? acrossOf(f, px, pz) : alongOf(f, px, pz);
                                uv[i] = {u, 1.f - py};
                            }
                            quadUV(cut, p, uv, tile, FACE_SHADE[d], ownSky, ownBl);
                        }
                    } else if (shape == Shape::Wire) {
                        auto isWire = [&](int xx, int yy, int zz) { return P(xx, yy, zz) == REDSTONE_WIRE; };
                        auto conn = [&](int d) {
                            int nx = x + DIRS[d][0], nz = z + DIRS[d][2];
                            uint8_t n = P(nx, y, nz);
                            if (n == REDSTONE_WIRE) return true;
                            if (n == REPEATER_ON || n == REPEATER_OFF) {
                                static const int SD[4] = {0, 4, 1, 5};
                                int ff = SD[M(nx, y, nz) & 3];
                                return ff == d || ff == (d ^ 1);
                            }
                            if (n == REDSTONE_TORCH_ON || n == REDSTONE_TORCH_OFF || n == LEVER || n == STONE_BUTTON ||
                                n == STONE_PLATE || n == WOOD_PLATE || n == DETECTOR_RAIL)
                                return true;
                            if (!isOpaque(n) && isWire(nx, y - 1, nz)) return true;
                            if (!isOpaque(P(x, y + 1, z)) && isWire(nx, y + 1, nz)) return true;
                            return false;
                        };
                        bool e = conn(0), wv = conn(1), s = conn(4), n = conn(5);
                        glm::vec3 col = wireColor(meta);
                        const float yy = 1.f / 64.f;
                        if ((e || wv) && !s && !n) {
                            // Линия вдоль X: повёрнутый тайл
                            glm::vec3 p[4] = {base + glm::vec3(0, yy, 0), base + glm::vec3(0, yy, 1), base + glm::vec3(1, yy, 1),
                                              base + glm::vec3(1, yy, 0)};
                            glm::vec2 uv[4] = {{0, 0}, {0, 1}, {1, 1}, {1, 0}}; // u тайла (линия) — вдоль X
                            quadUV(cut, p, uv, T(5, 10), 1.f, ownSky, ownBl, col, true);
                        } else if ((s || n) && !e && !wv) {
                            glm::vec3 p[4] = {base + glm::vec3(0, yy, 0), base + glm::vec3(0, yy, 1), base + glm::vec3(1, yy, 1),
                                              base + glm::vec3(1, yy, 0)};
                            glm::vec2 uv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}}; // u тайла — вдоль Z
                            quadUV(cut, p, uv, T(5, 10), 1.f, ownSky, ownBl, col, true);
                        } else {
                            // Крест: стороны без соединения обрезаны до середины; одиночная пыль — крест на весь блок (как в 1.0)
                            bool any = e || wv || s || n;
                            float x0 = (wv || !any) ? 0.f : 5 * k, x1 = (e || !any) ? 1.f : 11 * k;
                            float z0 = (n || !any) ? 0.f : 5 * k, z1 = (s || !any) ? 1.f : 11 * k;
                            glm::vec3 p[4] = {base + glm::vec3(x0, yy, z0), base + glm::vec3(x0, yy, z1), base + glm::vec3(x1, yy, z1),
                                              base + glm::vec3(x1, yy, z0)};
                            glm::vec2 uv[4] = {{x0, z0}, {x0, z1}, {x1, z1}, {x1, z0}};
                            quadUV(cut, p, uv, T(4, 10), 1.f, ownSky, ownBl, col, true);
                        }
                        // Подъём по стене к пыли выше
                        if (!isOpaque(P(x, y + 1, z)))
                            for (int d : {0, 1, 4, 5}) {
                                int nx = x + DIRS[d][0], nz = z + DIRS[d][2];
                                if (!isOpaque(P(nx, y, nz)) || !isWire(nx, y + 1, nz)) continue;
                                float o = 1.f - 1.f / 64.f;
                                glm::vec3 q[4];
                                if (d == 0) { q[0] = {o, 0, 0}; q[1] = {o, 1, 0}; q[2] = {o, 1, 1}; q[3] = {o, 0, 1}; }
                                else if (d == 1) { q[0] = {1 - o, 0, 0}; q[1] = {1 - o, 1, 0}; q[2] = {1 - o, 1, 1}; q[3] = {1 - o, 0, 1}; }
                                else if (d == 4) { q[0] = {0, 0, o}; q[1] = {0, 1, o}; q[2] = {1, 1, o}; q[3] = {1, 0, o}; }
                                else { q[0] = {0, 0, 1 - o}; q[1] = {0, 1, 1 - o}; q[2] = {1, 1, 1 - o}; q[3] = {1, 0, 1 - o}; }
                                for (auto& qq : q) qq += base;
                                glm::vec2 uv[4] = {{0, 1}, {0, 0}, {1, 0}, {1, 1}};
                                quadUV(cut, q, uv, T(5, 10), 1.f, ownSky, ownBl, col, true);
                            }
                    } else if (shape == Shape::Plate) {
                        int tex6[6];
                        for (int d = 0; d < 6; ++d) tex6[d] = blockTex(b, d, meta);
                        emitBox(cut, base, {k, 0, k}, {1 - k, meta ? k / 2 : k, 1 - k}, tex6, 63 & ~8, ownSky, ownBl);
                    } else if (shape == Shape::Button) {
                        int tex6[6];
                        for (int d = 0; d < 6; ++d) tex6[d] = T(1, 0);
                        float depth = (meta & 8) ? k : 2 * k;
                        int s = supportDir(b, meta);
                        glm::vec3 mn, mx;
                        if (s == 1) { mn = {0, 6 * k, 5 * k}; mx = {depth, 10 * k, 11 * k}; }
                        else if (s == 0) { mn = {1 - depth, 6 * k, 5 * k}; mx = {1, 10 * k, 11 * k}; }
                        else if (s == 5) { mn = {5 * k, 6 * k, 0}; mx = {11 * k, 10 * k, depth}; }
                        else { mn = {5 * k, 6 * k, 1 - depth}; mx = {11 * k, 10 * k, 1}; }
                        emitBox(cut, base, mn, mx, tex6, 63, ownSky, ownBl);
                    } else if (shape == Shape::Lever) {
                        int tex6[6];
                        for (int d = 0; d < 6; ++d) tex6[d] = T(0, 1);
                        bool on = meta & 8;
                        int s = supportDir(b, meta);
                        glm::vec3 mn, mx, pivot, up;
                        if (s == 3) {
                            mn = {5 * k, 0, 4 * k}; mx = {11 * k, 3 * k, 12 * k};
                            pivot = {0.5f, k, 0.5f};
                            up = {on ? 0.8f : -0.8f, 1.f, 0.f};
                        } else {
                            glm::vec3 out(-DIRS[s][0], 0.f, -DIRS[s][2]);
                            glm::vec3 wall = glm::vec3(0.5f) - out * 0.5f;
                            glm::vec3 half = glm::abs(out) * (1.5f * k) + (glm::vec3(1.f) - glm::abs(out)) * (3.f * k);
                            half.y = 4.f * k;
                            glm::vec3 c = wall + out * (1.5f * k);
                            mn = c - half; mx = c + half;
                            pivot = wall + out * k;
                            up = out + glm::vec3(0, on ? -0.8f : 0.8f, 0);
                        }
                        emitBox(cut, base, mn, mx, tex6, 63, ownSky, ownBl);
                        emitStick(cut, base + pivot, up, T(0, 6), ownSky, ownBl, 0.8f);
                    } else if (shape == Shape::Repeater) {
                        int f = meta & 3, delay = (meta >> 2) & 3;
                        int tex6[6];
                        for (int d = 0; d < 6; ++d) tex6[d] = T(6, 0);
                        emitBox(cut, base, {0, 0, 0}, {1, 2 * k, 1}, tex6, 63 & ~4, ownSky, ownBl);
                        topRot(2 * k, f, blockTex(b, 2, meta), 1.f, true);
                        int torch = b == REPEATER_ON ? T(3, 6) : T(3, 7);
                        static const float FD[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
                        glm::vec3 fwd(FD[f][0], 0, FD[f][1]);
                        glm::vec3 c = base + glm::vec3(0.5f, -3 * k, 0.5f);
                        emitStick(cut, c + fwd * (5 * k), {0, 1, 0}, torch, ownSky, ownBl);
                        emitStick(cut, c + fwd * ((-1.f - 2.f * delay) * k), {0, 1, 0}, torch, ownSky, ownBl);
                    }
                    continue;
                }

                // ---- Коробки: полублоки, ступеньки, двери, люки, калитки, панели, торт
                if (shape == Shape::Slab || shape == Shape::Stairs || shape == Shape::Door || shape == Shape::Trapdoor ||
                    shape == Shape::FenceGate || shape == Shape::Pane || shape == Shape::Cake || shape == Shape::Ladder ||
                    shape == Shape::Fire) {
                    int tex[6];
                    for (int d = 0; d < 6; ++d) tex[d] = blockTex(b, d, meta);
                    float nsky[6], nbl[6];
                    bool nOpaque[6];
                    for (int d = 0; d < 6; ++d) {
                        int nx = x + DIRS[d][0], ny = y + DIRS[d][1], nz = z + DIRS[d][2];
                        uint8_t nl = L(nx, ny, nz);
                        nOpaque[d] = isOpaque(P(nx, ny, nz));
                        nsky[d] = (nl >> 4) * inv15;
                        nbl[d] = (nl & 15) * inv15;
                    }
                    auto& cut = mesh[MESH_CUTOUT];
                    // Грань на границе блока берёт свет соседа и скрывается за непрозрачным соседом
                    auto box = [&](std::vector<Vertex>& out, glm::vec3 mn, glm::vec3 mx, int mask, const int* tx) {
                        float s6[6], b6[6];
                        for (int d = 0; d < 6; ++d) {
                            bool onEdge = (d == 0 && mx.x >= 1.f) || (d == 1 && mn.x <= 0.f) || (d == 2 && mx.y >= 1.f) ||
                                          (d == 3 && mn.y <= 0.f) || (d == 4 && mx.z >= 1.f) || (d == 5 && mn.z <= 0.f);
                            if (onEdge && nOpaque[d]) mask &= ~(1 << d);
                            s6[d] = onEdge ? nsky[d] : ownSky;
                            b6[d] = onEdge ? nbl[d] : ownBl;
                        }
                        emitBoxL(out, base, mn, mx, tx ? tx : tex, mask, s6, b6);
                    };
                    const float k = 1.f / 16.f;
                    auto sidePlate = [&](int s, float t) -> std::pair<glm::vec3, glm::vec3> {
                        switch (s & 3) {
                        case 0: return {{1 - t, 0, 0}, {1, 1, 1}};
                        case 1: return {{0, 0, 1 - t}, {1, 1, 1}};
                        case 2: return {{0, 0, 0}, {t, 1, 1}};
                        default: return {{0, 0, 0}, {1, 1, t}};
                        }
                    };
                    const glm::vec3 white(1.f);
                    if (shape == Shape::Slab) {
                        box(mesh[MESH_SOLID], {0, 0, 0}, {1, 0.5f, 1}, 63, nullptr);
                    } else if (shape == Shape::Stairs) {
                        bool upsideDown = (meta & 4) != 0;
                        float yBase0 = upsideDown ? 0.5f : 0.0f, yBase1 = upsideDown ? 1.0f : 0.5f;
                        float yStep0 = upsideDown ? 0.0f : 0.5f, yStep1 = upsideDown ? 0.5f : 1.0f;
                        box(mesh[MESH_SOLID], {0, yBase0, 0}, {1, yBase1, 1}, 63, nullptr);

                        int d = meta & 3; // 0: +X, 1: -X, 2: +Z, 3: -Z
                        bool q[2][2] = {false};
                        switch (d) {
                        case 0: q[1][0] = q[1][1] = true; break;
                        case 1: q[0][0] = q[0][1] = true; break;
                        case 2: q[0][1] = q[1][1] = true; break;
                        default: q[0][0] = q[1][0] = true; break;
                        }

                        auto isMatchingStair = [&](int nx, int ny, int nz) -> bool {
                            return isStairs(P(nx, ny, nz)) && ((M(nx, ny, nz) & 4) == (meta & 4));
                        };

                        // Check behind (inner corner)
                        bool inner = false;
                        if (d == 0 && isMatchingStair(x + 1, y, z)) {
                            int nd = M(x + 1, y, z) & 3;
                            if (nd == 2) { q[0][1] = true; inner = true; }
                            else if (nd == 3) { q[0][0] = true; inner = true; }
                        } else if (d == 1 && isMatchingStair(x - 1, y, z)) {
                            int nd = M(x - 1, y, z) & 3;
                            if (nd == 2) { q[1][1] = true; inner = true; }
                            else if (nd == 3) { q[1][0] = true; inner = true; }
                        } else if (d == 2 && isMatchingStair(x, y, z + 1)) {
                            int nd = M(x, y, z + 1) & 3;
                            if (nd == 0) { q[1][0] = true; inner = true; }
                            else if (nd == 1) { q[0][0] = true; inner = true; }
                        } else if (d == 3 && isMatchingStair(x, y, z - 1)) {
                            int nd = M(x, y, z - 1) & 3;
                            if (nd == 0) { q[1][0] = true; inner = true; }
                            else if (nd == 1) { q[0][1] = true; inner = true; }
                        }

                        // Check in front (outer corner)
                        if (!inner) {
                            if (d == 0 && isMatchingStair(x - 1, y, z)) {
                                int nd = M(x - 1, y, z) & 3;
                                if (nd == 2) q[1][0] = false;
                                else if (nd == 3) q[1][1] = false;
                            } else if (d == 1 && isMatchingStair(x + 1, y, z)) {
                                int nd = M(x + 1, y, z) & 3;
                                if (nd == 2) q[0][0] = false;
                                else if (nd == 3) q[0][1] = false;
                            } else if (d == 2 && isMatchingStair(x, y, z - 1)) {
                                int nd = M(x, y, z - 1) & 3;
                                if (nd == 0) q[0][1] = false;
                                else if (nd == 1) q[1][1] = false;
                            } else if (d == 3 && isMatchingStair(x, y, z + 1)) {
                                int nd = M(x, y, z + 1) & 3;
                                if (nd == 0) q[0][0] = false;
                                else if (nd == 1) q[1][0] = false;
                            }
                        }

                        int mask = 63 & (upsideDown ? ~4 : ~8);
                        for (int qx = 0; qx < 2; ++qx) {
                            for (int qz = 0; qz < 2; ++qz) {
                                if (q[qx][qz]) {
                                    box(mesh[MESH_SOLID], {qx * 0.5f, yStep0, qz * 0.5f},
                                                          {(qx + 1) * 0.5f, yStep1, (qz + 1) * 0.5f},
                                                          mask, nullptr);
                                }
                            }
                        }
                    } else if (shape == Shape::Door) {
                        int f = meta & 3;
                        auto pl = sidePlate((meta & 4) ? (f + 3) & 3 : (f + 2) & 3, 3 * k);
                        box(cut, pl.first, pl.second, 63, nullptr);
                    } else if (shape == Shape::Trapdoor) {
                        if (meta & 4) {
                            auto pl = sidePlate(meta & 3, 3 * k);
                            box(cut, pl.first, pl.second, 63, nullptr);
                        } else {
                            box(cut, {0, 0, 0}, {1, 3 * k, 1}, 63, nullptr);
                        }
                    } else if (shape == Shape::Ladder) {
                        // Одна плоскость в 1/16 от стены, видна с обеих сторон
                        float u0 = (tex[4] % 16) * TS + EPS, u1 = (tex[4] % 16 + 1) * TS - EPS;
                        float vT = (tex[4] / 16) * TS + EPS, vB = (tex[4] / 16 + 1) * TS - EPS;
                        int wd = sideToDir(meta);
                        float l = 0.8f, sk = ownSky, bb = ownBl;
                        const float o = k;
                        glm::vec3 a, bq, c2, d2; // низ-лево, верх-лево, верх-право, низ-право
                        switch (meta & 3) {
                        case 0: a = {1 - o, 0, 0}; bq = {1 - o, 1, 0}; c2 = {1 - o, 1, 1}; d2 = {1 - o, 0, 1}; break;
                        case 1: a = {0, 0, 1 - o}; bq = {0, 1, 1 - o}; c2 = {1, 1, 1 - o}; d2 = {1, 0, 1 - o}; break;
                        case 2: a = {o, 0, 0}; bq = {o, 1, 0}; c2 = {o, 1, 1}; d2 = {o, 0, 1}; break;
                        default: a = {0, 0, o}; bq = {0, 1, o}; c2 = {1, 1, o}; d2 = {1, 0, o}; break;
                        }
                        (void)wd;
                        quad2(cut, V(base.x + a.x, base.y + a.y, base.z + a.z, u0, vB, l, sk, bb, white),
                              V(base.x + bq.x, base.y + bq.y, base.z + bq.z, u0, vT, l, sk, bb, white),
                              V(base.x + c2.x, base.y + c2.y, base.z + c2.z, u1, vT, l, sk, bb, white),
                              V(base.x + d2.x, base.y + d2.y, base.z + d2.z, u1, vB, l, sk, bb, white));
                    } else if (shape == Shape::FenceGate) {
                        bool alongZ = (meta & 1) == 0; // смотрит вдоль X — полотно поперёк, вдоль Z
                        bool open = meta & 4;
                        auto B = [&](float x0, float y0, float z0, float x1, float y1, float z1) {
                            if (alongZ) box(mesh[MESH_SOLID], {x0, y0, z0}, {x1, y1, z1}, 63, nullptr);
                            else box(mesh[MESH_SOLID], {z0, y0, x0}, {z1, y1, x1}, 63, nullptr);
                        };
                        const float c0 = 7 * k, c1 = 9 * k;
                        B(c0, 5 * k, 0, c1, 1, 2 * k);          // столбики
                        B(c0, 5 * k, 14 * k, c1, 1, 1);
                        if (!open) {
                            B(c0, 6 * k, 2 * k, c1, 9 * k, 14 * k);   // перекладины
                            B(c0, 12 * k, 2 * k, c1, 15 * k, 14 * k);
                            B(c0, 9 * k, 6 * k, c1, 12 * k, 10 * k);  // середина
                        } else {
                            // Створки распахнуты в сторону взгляда игрока при открытии
                            int f = meta & 3;
                            bool pos = (f == 0 || f == 1);
                            float a0 = pos ? c1 : 1.f - c1 - 6 * k, a1 = pos ? c1 + 6 * k : 1.f - c1;
                            if (!pos) { a0 = c0 - 6 * k; a1 = c0; }
                            B(a0, 6 * k, 0, a1, 9 * k, 2 * k);
                            B(a0, 12 * k, 0, a1, 15 * k, 2 * k);
                            B(a0, 6 * k, 14 * k, a1, 9 * k, 1);
                            B(a0, 12 * k, 14 * k, a1, 15 * k, 1);
                        }
                    } else if (shape == Shape::Pane) {
                        auto conn = [&](uint8_t nbk) { return isOpaque(nbk) || nbk == GLASS_PANE || nbk == IRON_BARS || nbk == GLASS; };
                        bool px = conn(P(x + 1, y, z)), nx = conn(P(x - 1, y, z)), pz = conn(P(x, y, z + 1)), nz = conn(P(x, y, z - 1));
                        if (!px && !nx && !pz && !nz) px = nx = pz = nz = true;
                        const float a = 7 * k, c = 9 * k;
                        int t6[6];
                        for (int d = 0; d < 6; ++d) t6[d] = tex[d];
                        if (b == GLASS_PANE) t6[2] = t6[3] = T(4, 9); // торцы стекла — узкая полоска
                        box(cut, {a, 0, a}, {c, 1, c}, 63, t6);
                        if (px) box(cut, {c, 0, a}, {1, 1, c}, 63 & ~2 & ~(P(x + 1, y, z) == b ? 1 : 0), t6);
                        if (nx) box(cut, {0, 0, a}, {a, 1, c}, 63 & ~1 & ~(P(x - 1, y, z) == b ? 2 : 0), t6);
                        if (pz) box(cut, {a, 0, c}, {c, 1, 1}, 63 & ~32 & ~(P(x, y, z + 1) == b ? 16 : 0), t6);
                        if (nz) box(cut, {a, 0, 0}, {c, 1, a}, 63 & ~16 & ~(P(x, y, z - 1) == b ? 32 : 0), t6);
                    } else if (shape == Shape::Cake) {
                        box(cut, {(1 + 2 * (meta & 7)) * k, 0, k}, {1 - k, 0.5f, 1 - k}, 63, nullptr);
                    } else if (shape == Shape::Fire) {
                        // Пламя: наклонённые внутрь плоскости по краям и крест, выше блока (RenderBlocks.renderBlockFire)
                        int t0 = T(15, 1), t1 = T(15, 2);
                        const float hgt = 1.4f;
                        // Плоскость пламени: нижняя кромка p0-p1, верхняя = нижняя + top (двусторонняя)
                        auto plane = [&](int tx, glm::vec3 p0, glm::vec3 p1, glm::vec3 top) {
                            float u0 = (tx % 16) * TS + EPS, u1 = (tx % 16 + 1) * TS - EPS;
                            float vT = (tx / 16) * TS + EPS, vB = (tx / 16 + 1) * TS - EPS;
                            glm::vec3 q0 = base + p0, q1 = base + p1, q2 = base + p1 + top, q3 = base + p0 + top;
                            quad2(cut, V(q0.x, q0.y, q0.z, u0, vB, 1, 1, 1, white), V(q3.x, q3.y, q3.z, u0, vT, 1, 1, 1, white),
                                  V(q2.x, q2.y, q2.z, u1, vT, 1, 1, 1, white), V(q1.x, q1.y, q1.z, u1, vB, 1, 1, 1, white));
                        };
                        uint8_t below = P(x, y - 1, z);
                        if (isOpaque(below) || fireCanCatch(below)) {
                            // На земле: наклонённые внутрь плоскости по краям и крест
                            const float in = 0.2f;
                            plane(t0, {0, 0, 0}, {0, 0, 1}, {in, hgt, 0});
                            plane(t1, {1, 0, 0}, {1, 0, 1}, {-in, hgt, 0});
                            plane(t0, {0, 0, 0}, {1, 0, 0}, {0, hgt, in});
                            plane(t1, {0, 0, 1}, {1, 0, 1}, {0, hgt, -in});
                            plane(t1, {0.15f, 0, 0}, {0.15f, 0, 1}, {0.7f, hgt, 0});
                            plane(t0, {0, 0, 0.15f}, {1, 0, 0.15f}, {0, hgt, 0.7f});
                        } else {
                            // Без опоры: пламя «ползёт» только по граням горючих соседей — плоскость у грани,
                            // верх наклонён внутрь на 0.2, всё приподнято на 1/16
                            const float in = 0.2f, lift = 0.0625f;
                            int tx = ((x + y + z) & 1) ? t1 : t0;
                            if (fireCanCatch(P(x - 1, y, z))) plane(tx, {0, lift, 0}, {0, lift, 1}, {in, hgt, 0});
                            if (fireCanCatch(P(x + 1, y, z))) plane(tx, {1, lift, 0}, {1, lift, 1}, {-in, hgt, 0});
                            if (fireCanCatch(P(x, y, z - 1))) plane(tx, {0, lift, 0}, {1, lift, 0}, {0, hgt, in});
                            if (fireCanCatch(P(x, y, z + 1))) plane(tx, {0, lift, 1}, {1, lift, 1}, {0, hgt, -in});
                            if (fireCanCatch(P(x, y + 1, z))) {
                                // Под горючим потолком: две пологие плоскости от одного края к другому
                                if (((x + y + z) & 1) == 0) {
                                    plane(t0, {0, 1, 0}, {0, 1, 1}, {1, -0.2f, 0});
                                    plane(t1, {1, 1, 0}, {1, 1, 1}, {-1, -0.2f, 0});
                                } else {
                                    plane(t0, {0, 1, 0}, {1, 1, 0}, {0, -0.2f, 1});
                                    plane(t1, {0, 1, 1}, {1, 1, 1}, {0, -0.2f, -1});
                                }
                            }
                        }
                    }
                    continue;
                }

                // ---- Кубы и жидкости
                bool liquid = isLiquid(b);
                bool liquidCovered = liquid && P(x, y + 1, z) == b;
                float hCorner[2][2] = {{1, 1}, {1, 1}};
                if (liquid && !liquidCovered)
                    for (int i = 0; i < 2; ++i)
                        for (int j = 0; j < 2; ++j) hCorner[i][j] = cornerHeight(x + i, y, z + j, b);
                int bucket = isTranslucent(b) ? MESH_TRANSLUCENT : shape == Shape::Cutout ? MESH_CUTOUT : MESH_SOLID;
                bool fastLeaves = b == LEAVES && !fancyGraphics;
                if (fastLeaves) bucket = MESH_SOLID;
                bool snowOnTop = b == GRASS && (P(x, y + 1, z) == SNOW_LAYER || P(x, y + 1, z) == SNOW_BLOCK);

                for (int d = 0; d < 6; ++d) {
                    int nx = x + DIRS[d][0], ny = y + DIRS[d][1], nz = z + DIRS[d][2];
                    uint8_t nbk = P(nx, ny, nz);
                    // Верх жидкости виден, даже если сверху твёрдый блок не вплотную (уровень ниже 1)
                    if (!faceVisible(b, nbk) && !(liquid && d == 2 && !liquidCovered && nbk != b && !isOpaque(nbk))) continue;
                    if (fastLeaves && nbk == LEAVES) continue; // быстрая листва не рисует внутренние грани

                    int tex = blockTex(b, d, meta);
                    if (fastLeaves) tex += 1; // соседний тайл — непрозрачная «быстрая» листва
                    int tType = tintType(b, d, meta);
                    if (snowOnTop && d != 2 && d != 3) { tex = T(4, 4); tType = 0; }
                    float tu = (tex % 16) * TS, tv = (tex / 16) * TS;
                    uint8_t l0 = L(nx, ny, nz);
                    Vertex v[4];
                    for (int i = 0; i < 4; ++i) {
                        const int* cr = FACE[d][i];
                        int a = TANGENTS[d][0], bb = TANGENTS[d][1];
                        int o1[3] = {0, 0, 0}, o2[3] = {0, 0, 0};
                        o1[a] = cr[a] ? 1 : -1;
                        o2[bb] = cr[bb] ? 1 : -1;
                        int x1 = nx + o1[0], y1 = ny + o1[1], z1 = nz + o1[2];
                        int x2 = nx + o2[0], y2 = ny + o2[1], z2 = nz + o2[2];
                        int x3 = nx + o1[0] + o2[0], y3 = ny + o1[1] + o2[1], z3 = nz + o1[2] + o2[2];
                        uint8_t b1 = P(x1, y1, z1), b2 = P(x2, y2, z2), b3 = P(x3, y3, z3);

                        // Ambient occlusion (не для жидкостей)
                        int level = 3;
                        if (!liquid) {
                            int s1 = occludesAO(b1), s2 = occludesAO(b2), cc = occludesAO(b3);
                            level = (s1 && s2) ? 0 : 3 - (s1 + s2 + cc);
                        }

                        // Плавное освещение: среднее по прозрачным клеткам у вершины
                        int skySum = l0 >> 4, blSum = l0 & 15, n = 1;
                        bool t1 = !isOpaque(b1), t2 = !isOpaque(b2);
                        if (t1) { uint8_t q = L(x1, y1, z1); skySum += q >> 4; blSum += q & 15; ++n; }
                        if (t2) { uint8_t q = L(x2, y2, z2); skySum += q >> 4; blSum += q & 15; ++n; }
                        if ((t1 || t2) && !isOpaque(b3)) { uint8_t q = L(x3, y3, z3); skySum += q >> 4; blSum += q & 15; ++n; }
                        if (liquid && d == 2 && isOpaque(nbk)) { skySum = l >> 4; blSum = l & 15; n = 1; }
                        // Без сглаживания грань целиком берёт свет соседней клетки, углы не затеняются
                        if (!smoothLighting) {
                            level = 3;
                            skySum = l0 >> 4;
                            blSum = l0 & 15;
                            n = 1;
                            if (liquid && d == 2 && isOpaque(nbk)) { skySum = l >> 4; blSum = l & 15; }
                        }

                        float py = (float)(y + cr[1]);
                        if (liquid && cr[1] == 1) py = y + hCorner[cr[0]][cr[2]];
                        glm::vec3 tc = tint(tType, x, z);
                        v[i] = V((float)(x + cr[0]), py, (float)(z + cr[2]),
                                 tu + (CORNER_UV[i][0] ? TS - EPS : EPS), tv + (CORNER_UV[i][1] ? EPS : TS - EPS),
                                 FACE_SHADE[d] * AO_CURVE[level], skySum * inv15 / n, blSum * inv15 / n, tc);
                    }
                    if (liquid && d != 3) {
                        // Текстуры жидкости как RenderBlocks.renderBlockFluids 1.0: верх — неподвижный тайл или «поток»,
                        // повёрнутый по течению; бока — верхняя половина тайла потока (он стекает вниз)
                        const int flowTile = b == WATER ? T(14, 12) : T(14, 14);
                        const float fu = (flowTile % 16) * TS, fv = (flowTile / 16) * TS, H = TS * 0.5f;
                        if (d == 2) {
                            glm::vec3 fl = flowVector(c.cx * CW + x, y, c.cz * CW + z);
                            float cu = tu + H, cv = tv + H, ang = 0.f, R = H - EPS;
                            if (fl.x != 0.f || fl.z != 0.f) { ang = std::atan2(fl.z, fl.x) - 1.5707963f; cu = fu + TS; cv = fv + TS; R = H; }
                            float cs = std::cos(ang) * R, sn = std::sin(ang) * R;
                            for (int i = 0; i < 4; ++i) {
                                float a = FACE[d][i][0] ? 1.f : -1.f, bz = FACE[d][i][2] ? 1.f : -1.f;
                                v[i].u = cu + a * cs + bz * sn;
                                v[i].v = cv + bz * cs - a * sn;
                            }
                        } else {
                            for (int i = 0; i < 4; ++i) {
                                bool top = FACE[d][i][1] != 0;
                                float h = top ? v[i].y - (float)y : 0.f;
                                v[i].u = fu + (CORNER_UV[i][0] ? TS - EPS : EPS);
                                v[i].v = top ? fv + (1.f - h) * H + EPS : fv + H;
                            }
                        }
                    }
                    auto& out = mesh[bucket];
                    // Выбираем диагональ так, чтобы освещение интерполировалось без артефактов
                    auto lum = [](const Vertex& q) { return q.shade * std::max(q.sky, q.block); };
                    bool diag02 = lum(v[0]) + lum(v[2]) >= lum(v[1]) + lum(v[3]);
                    if (diag02) out.insert(out.end(), {v[0], v[1], v[2], v[0], v[2], v[3]});
                    else out.insert(out.end(), {v[1], v[2], v[3], v[1], v[3], v[0]});

                    // Бок травы: поверх — серая «чёлка» (тайл 6,2), крашеная в цвет биома, как в 1.0
                    if (b == GRASS && d != 2 && d != 3 && !snowOnTop) {
                        const int ov = T(6, 2);
                        float ou = (ov % 16) * TS, ovv = (ov / 16) * TS;
                        glm::vec3 gc = tint(1, x, z);
                        Vertex o[4];
                        for (int i = 0; i < 4; ++i) {
                            o[i] = v[i];
                            o[i].u = ou + (CORNER_UV[i][0] ? TS - EPS : EPS);
                            o[i].v = ovv + (CORNER_UV[i][1] ? EPS : TS - EPS);
                            o[i].r = gc.r; o[i].g = gc.g; o[i].b = gc.b;
                        }
                        auto& co = mesh[MESH_CUTOUT];
                        if (diag02) co.insert(co.end(), {o[0], o[1], o[2], o[0], o[2], o[3]});
                        else co.insert(co.end(), {o[1], o[2], o[3], o[1], o[3], o[0]});
                    }
                }
            }
        }
    }

    for (int i = 0; i < MESH_COUNT; ++i) upload(c, i, mesh[i]);
    c.dirty = false;
    c.meshed = true;
}
