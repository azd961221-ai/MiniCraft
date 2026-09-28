// Генератор обычного мира как в Minecraft 1.0 (для миров с generator=2):
// биомы — стек слоёв (острова, увеличения, снег, грибные острова, реки, сглаживание, Вороной),
// рельеф — трёхмерная плотность из октавного шума с высотами биомов, поверхность по биому,
// пещеры-«черви» с комнатами и ответвлениями, ущелья, озёра воды и лавы, руды «каплями»,
// пятна песка, гравия и глины у воды. Старые миры (generator=1) генерируются прежним кодом.
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>
#include "World.h"

namespace {

const double PI_D = 3.14159265358979323846;

// ---- ГСЧ как java.util.Random (48-битный линейный конгруэнтный)
struct JRandom {
    int64_t s = 0;
    explicit JRandom(int64_t seed = 0) { setSeed(seed); }
    void setSeed(int64_t seed) { s = (seed ^ 0x5DEECE66DLL) & ((1LL << 48) - 1); }
    int next(int bits) {
        s = (s * 0x5DEECE66DLL + 0xBLL) & ((1LL << 48) - 1);
        return (int)(s >> (48 - bits));
    }
    int nextInt(int n) {
        if (n <= 0) return 0;
        if ((n & -n) == n) return (int)(((int64_t)n * (int64_t)next(31)) >> 31);
        int bits, val;
        do {
            bits = next(31);
            val = bits % n;
        } while (bits - val + (n - 1) < 0);
        return val;
    }
    int64_t nextLong() { return ((int64_t)next(32) << 32) + (int64_t)next(32); }
    float nextFloat() { return next(24) / (float)(1 << 24); }
    double nextDouble() { return (((int64_t)next(26) << 27) + next(27)) * (1.0 / (double)(1LL << 53)); }
};

// ---- Улучшенный шум Перлина в double со случайным сдвигом и перестановкой (одна октава)
struct PerlinD {
    int p[512];
    double ox, oy, oz;
    explicit PerlinD(JRandom& r) {
        ox = r.nextDouble() * 256.0;
        oy = r.nextDouble() * 256.0;
        oz = r.nextDouble() * 256.0;
        for (int i = 0; i < 256; ++i) p[i] = i;
        for (int i = 0; i < 256; ++i) {
            int j = r.nextInt(256 - i) + i;
            std::swap(p[i], p[j]);
            p[i + 256] = p[i];
        }
    }
    static double fade(double t) { return t * t * t * (t * (t * 6 - 15) + 10); }
    static double lerp(double t, double a, double b) { return a + t * (b - a); }
    static double grad(int h, double x, double y, double z) {
        h &= 15;
        double u = h < 8 ? x : y;
        double v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
        return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
    }
    double noise(double x, double y, double z) const {
        x += ox; y += oy; z += oz;
        double fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
        int X = (int)((int64_t)fx & 255), Y = (int)((int64_t)fy & 255), Z = (int)((int64_t)fz & 255);
        x -= fx; y -= fy; z -= fz;
        double u = fade(x), v = fade(y), w = fade(z);
        int A = p[X] + Y, AA = p[A] + Z, AB = p[A + 1] + Z;
        int B = p[X + 1] + Y, BA = p[B] + Z, BB = p[B + 1] + Z;
        return lerp(w, lerp(v, lerp(u, grad(p[AA], x, y, z), grad(p[BA], x - 1, y, z)),
                            lerp(u, grad(p[AB], x, y - 1, z), grad(p[BB], x - 1, y - 1, z))),
                    lerp(v, lerp(u, grad(p[AA + 1], x, y, z - 1), grad(p[BA + 1], x - 1, y, z - 1)),
                         lerp(u, grad(p[AB + 1], x, y - 1, z - 1), grad(p[BB + 1], x - 1, y - 1, z - 1))));
    }
};

// Октавы: частота делится на 2, амплитуда удваивается (как NoiseGeneratorOctaves)
struct Octaves {
    std::vector<PerlinD> oct;
    Octaves(JRandom& r, int n) { for (int i = 0; i < n; ++i) oct.emplace_back(r); }
    // Значение в узле сетки (gx, gy, gz) при масштабах sx, sy, sz
    double at(double gx, double gy, double gz, double sx, double sy, double sz) const {
        double sum = 0.0, f = 1.0;
        for (const PerlinD& o : oct) {
            sum += o.noise(gx * sx * f, gy * sy * f, gz * sz * f) / f;
            f *= 0.5;
        }
        return sum;
    }
};

// ---- Слои биомов (GenLayer): у каждого свой «солёный» ГСЧ на клетку
constexpr int64_t LCG_A = 6364136223846793005LL, LCG_C = 1442695040888963407LL;

struct Layer {
    int64_t baseSeed, worldSeed = 0, chunkSeed = 0;
    std::shared_ptr<Layer> parent;
    explicit Layer(int64_t b) {
        baseSeed = b;
        for (int i = 0; i < 3; ++i) { baseSeed *= baseSeed * LCG_A + LCG_C; baseSeed += b; }
    }
    virtual ~Layer() = default;
    void initWorld(int64_t s) {
        worldSeed = s;
        if (parent) parent->initWorld(s);
        for (int i = 0; i < 3; ++i) { worldSeed *= worldSeed * LCG_A + LCG_C; worldSeed += baseSeed; }
    }
    void initChunk(int64_t x, int64_t z) {
        chunkSeed = worldSeed;
        chunkSeed *= chunkSeed * LCG_A + LCG_C; chunkSeed += x;
        chunkSeed *= chunkSeed * LCG_A + LCG_C; chunkSeed += z;
        chunkSeed *= chunkSeed * LCG_A + LCG_C; chunkSeed += x;
        chunkSeed *= chunkSeed * LCG_A + LCG_C; chunkSeed += z;
    }
    int nextInt(int n) {
        int i = (int)((chunkSeed >> 24) % n);
        if (i < 0) i += n;
        chunkSeed *= chunkSeed * LCG_A + LCG_C;
        chunkSeed += worldSeed;
        return i;
    }
    virtual std::vector<int> get(int x, int z, int w, int h) = 0;
};
using LayerP = std::shared_ptr<Layer>;

const int L_OCEAN = 0, L_LAND = 1, L_SNOW = 4;
const int B_OCEAN = (int)Biome::Ocean, B_RIVER = (int)Biome::River, B_MUSH = (int)Biome::MushroomIsland,
          B_MUSH_SHORE = (int)Biome::MushroomShore, B_ICE = (int)Biome::IcePlains, B_FROZEN_RIVER = (int)Biome::FrozenRiver;

struct LIsland : Layer {
    using Layer::Layer;
    std::vector<int> get(int x, int z, int w, int h) override {
        std::vector<int> o(w * h);
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i) {
                initChunk(x + i, z + j);
                o[i + j * w] = nextInt(10) == 0 ? L_LAND : L_OCEAN;
            }
        if (x > -w && x <= 0 && z > -h && z <= 0) o[-x + -z * w] = L_LAND; // суша в начале координат
        return o;
    }
};

// Увеличение вдвое: углы копируются, рёбра — случайный из двух, центр — «мода» четырёх (fuzzy — случайный из четырёх)
struct LZoom : Layer {
    bool fuzzy;
    LZoom(int64_t b, LayerP p, bool f = false) : Layer(b), fuzzy(f) { parent = std::move(p); }
    int choose2(int a, int b) { return nextInt(2) == 0 ? a : b; }
    int choose4(int a, int b, int c, int d) {
        int r = nextInt(4);
        return r == 0 ? a : r == 1 ? b : r == 2 ? c : d;
    }
    int mode(int a, int b, int c, int d) {
        if (b == c && c == d) return b;
        if (a == b && a == c) return a;
        if (a == b && a == d) return a;
        if (a == c && a == d) return a;
        if (a == b && c != d) return a;
        if (a == c && b != d) return a;
        if (a == d && b != c) return a;
        if (b == c && a != d) return b;
        if (b == d && a != c) return b;
        if (c == d && a != b) return c;
        return choose4(a, b, c, d);
    }
    std::vector<int> get(int x, int z, int w, int h) override {
        int px = x >> 1, pz = z >> 1, pw = (w >> 1) + 3, ph = (h >> 1) + 3;
        std::vector<int> in = parent->get(px, pz, pw, ph);
        int tw = pw * 2, th = ph * 2;
        std::vector<int> tmp(tw * th);
        for (int j = 0; j < ph - 1; ++j) {
            int a = in[j * pw], c = in[(j + 1) * pw];
            for (int i = 0; i < pw - 1; ++i) {
                initChunk((int64_t)(i + px) << 1, (int64_t)(j + pz) << 1);
                int b = in[i + 1 + j * pw], d = in[i + 1 + (j + 1) * pw];
                int idx = (j * 2) * tw + i * 2;
                tmp[idx] = a;
                tmp[idx + tw] = choose2(a, c);
                tmp[idx + 1] = choose2(a, b);
                tmp[idx + 1 + tw] = fuzzy ? choose4(a, b, c, d) : mode(a, b, c, d);
                a = b;
                c = d;
            }
        }
        std::vector<int> o(w * h);
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i) o[i + j * w] = tmp[(j + (z & 1)) * tw + i + (x & 1)];
        return o;
    }
};

LayerP magnify(int64_t b, LayerP p, int times) {
    for (int i = 0; i < times; ++i) p = std::make_shared<LZoom>(b + i, p);
    return p;
}

// Общий каркас: окрестность 3x3 (родитель берётся с полями по 1)
struct LNeighborhood : Layer {
    LNeighborhood(int64_t b, LayerP p) : Layer(b) { parent = std::move(p); }
    virtual int cell(int nw, int ne, int sw, int se, int n, int s, int w, int e, int c) = 0;
    std::vector<int> get(int x, int z, int w, int h) override {
        int pw = w + 2;
        std::vector<int> in = parent->get(x - 1, z - 1, w + 2, h + 2);
        std::vector<int> o(w * h);
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i) {
                auto P = [&](int di, int dj) { return in[(i + di) + (j + dj) * pw]; };
                initChunk(x + i, z + j);
                o[i + j * w] = cell(P(0, 0), P(2, 0), P(0, 2), P(2, 2), P(1, 0), P(1, 2), P(0, 1), P(2, 1), P(1, 1));
            }
        return o;
    }
};

// Острова растут и размываются: океан рядом с сушей иногда становится сушей, берег иногда тонет
struct LAddIsland : LNeighborhood {
    using LNeighborhood::LNeighborhood;
    int cell(int a, int b, int c, int d, int n, int s, int w, int ee, int e) override {
        (void)n; (void)s; (void)w; (void)ee;
        if (e == L_OCEAN && (a != 0 || b != 0 || c != 0 || d != 0)) {
            int k = 1, v = 1;
            if (a != 0 && nextInt(k++) == 0) v = a;
            if (b != 0 && nextInt(k++) == 0) v = b;
            if (c != 0 && nextInt(k++) == 0) v = c;
            if (d != 0 && nextInt(k++) == 0) v = d;
            if (nextInt(3) == 0) return v;
            return v == L_SNOW ? L_SNOW : L_OCEAN;
        }
        if (e > 0 && (a == 0 || b == 0 || c == 0 || d == 0)) {
            if (nextInt(5) == 0) return e == L_SNOW ? L_SNOW : L_OCEAN;
            return e;
        }
        return e;
    }
};

// Каждая пятая суша — снежная
struct LAddSnow : LNeighborhood {
    using LNeighborhood::LNeighborhood;
    int cell(int, int, int, int, int, int, int, int, int e) override {
        if (e == L_OCEAN) return L_OCEAN;
        return nextInt(5) == 0 ? L_SNOW : L_LAND;
    }
};

// Грибной остров посреди открытого океана (1 из 100)
struct LAddMushroom : LNeighborhood {
    using LNeighborhood::LNeighborhood;
    int cell(int a, int b, int c, int d, int, int, int, int, int e) override {
        if (a == 0 && b == 0 && c == 0 && d == 0 && e == 0 && nextInt(100) == 0) return B_MUSH;
        return e;
    }
};

// Реки: у суши случайная метка 2..3, река — там, где метки соседей различаются
struct LRiverInit : Layer {
    LRiverInit(int64_t b, LayerP p) : Layer(b) { parent = std::move(p); }
    std::vector<int> get(int x, int z, int w, int h) override {
        std::vector<int> in = parent->get(x, z, w, h), o(w * h);
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i) {
                initChunk(x + i, z + j);
                o[i + j * w] = in[i + j * w] > 0 ? nextInt(2) + 2 : 0;
            }
        return o;
    }
};

struct LRiver : LNeighborhood {
    using LNeighborhood::LNeighborhood;
    int cell(int, int, int, int, int n, int s, int w, int e, int c) override {
        if (c == 0 || w == 0 || e == 0 || n == 0 || s == 0) return B_RIVER;
        if (c != w || c != n || c != e || c != s) return B_RIVER;
        return -1;
    }
};

// Сглаживание: одиночные клетки между одинаковыми соседями выравниваются
struct LSmooth : LNeighborhood {
    using LNeighborhood::LNeighborhood;
    int cell(int, int, int, int, int n, int s, int w, int e, int c) override {
        if (w == e && n == s) return nextInt(2) == 0 ? w : n;
        if (w == e) c = w;
        if (n == s) c = n;
        return c;
    }
};

// Суша -> биом: пустыня, лес, горы, болото, равнины, тайга, джунгли (1.4.2); снежная суша — ледяные равнины
struct LBiome : Layer {
    LBiome(int64_t b, LayerP p) : Layer(b) { parent = std::move(p); }
    std::vector<int> get(int x, int z, int w, int h) override {
        static const int ALLOWED[7] = {(int)Biome::Desert, (int)Biome::Forest, (int)Biome::ExtremeHills,
                                       (int)Biome::Swampland, (int)Biome::Plains, (int)Biome::Taiga, (int)Biome::Jungle};
        std::vector<int> in = parent->get(x, z, w, h), o(w * h);
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i) {
                initChunk(x + i, z + j);
                int k = in[i + j * w];
                o[i + j * w] = k == L_OCEAN ? B_OCEAN : k == B_MUSH ? B_MUSH : k == L_LAND ? ALLOWED[nextInt(7)] : B_ICE;
            }
        return o;
    }
};

// Берег грибного острова и песчаный пляж
struct LShore : LNeighborhood {
    using LNeighborhood::LNeighborhood;
    int cell(int, int, int, int, int n, int s, int w, int e, int c) override {
        if (c == B_MUSH && (n == B_OCEAN || s == B_OCEAN || w == B_OCEAN || e == B_OCEAN)) return B_MUSH_SHORE;
        if (c != B_OCEAN && c != (int)Biome::Swampland && c != B_RIVER && (n == B_OCEAN || s == B_OCEAN || w == B_OCEAN || e == B_OCEAN)) return (int)Biome::Beach;
        return c;
    }
};

// Холмы и под-биомы (GenLayerHills 1.4.2): DesertHills, ForestHills, TaigaHills, JungleHills, IceMountains, ExtremeHillsEdge
struct LHills : Layer {
    LHills(int64_t b, LayerP p) : Layer(b) { parent = std::move(p); }
    std::vector<int> get(int x, int z, int w, int h) override {
        std::vector<int> in = parent->get(x, z, w, h), o(w * h);
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i) {
                initChunk(x + i, z + j);
                int b = in[i + j * w];
                if (nextInt(3) == 0) {
                    if (b == (int)Biome::Desert) b = (int)Biome::DesertHills;
                    else if (b == (int)Biome::Forest) b = (int)Biome::ForestHills;
                    else if (b == (int)Biome::Taiga) b = (int)Biome::TaigaHills;
                    else if (b == (int)Biome::Jungle) b = (int)Biome::JungleHills;
                    else if (b == (int)Biome::IcePlains) b = (int)Biome::IceMountains;
                    else if (b == (int)Biome::ExtremeHills) b = (int)Biome::ExtremeHillsEdge;
                }
                o[i + j * w] = b;
            }
        return o;
    }
};

struct LRiverMix : Layer {
    LayerP rivers;
    LRiverMix(int64_t b, LayerP biomes, LayerP r) : Layer(b), rivers(std::move(r)) { parent = std::move(biomes); }
    std::vector<int> get(int x, int z, int w, int h) override {
        std::vector<int> b = parent->get(x, z, w, h), r = rivers->get(x, z, w, h), o(w * h);
        for (int i = 0; i < w * h; ++i) {
            if (b[i] == B_OCEAN) o[i] = b[i];
            else if (r[i] >= 0) o[i] = b[i] == B_ICE ? B_FROZEN_RIVER : (b[i] == B_MUSH || b[i] == B_MUSH_SHORE) ? B_MUSH_SHORE : B_RIVER;
            else o[i] = b[i];
        }
        return o;
    }
    void initRivers(int64_t s) { rivers->initWorld(s); }
};

// Увеличение в 4 раза «ячейками Вороного» со сдвинутыми центрами — неровные границы биомов
struct LVoronoi : Layer {
    LVoronoi(int64_t b, LayerP p) : Layer(b) { parent = std::move(p); }
    std::vector<int> get(int x, int z, int w, int h) override {
        x -= 2; z -= 2;
        int px = x >> 2, pz = z >> 2, pw = (w >> 2) + 3, ph = (h >> 2) + 3;
        std::vector<int> in = parent->get(px, pz, pw, ph);
        int tw = pw << 2, th = ph << 2;
        std::vector<int> tmp(tw * th);
        auto jit = [&]() { return (nextInt(1024) / 1024.0 - 0.5) * 3.6; };
        for (int j = 0; j < ph - 1; ++j) {
            int a = in[j * pw], c = in[(j + 1) * pw];
            for (int i = 0; i < pw - 1; ++i) {
                initChunk((int64_t)(i + px) << 2, (int64_t)(j + pz) << 2);
                double ax = jit(), az = jit();
                initChunk((int64_t)(i + px + 1) << 2, (int64_t)(j + pz) << 2);
                double bx = jit() + 4.0, bz = jit();
                initChunk((int64_t)(i + px) << 2, (int64_t)(j + pz + 1) << 2);
                double cx = jit(), cz = jit() + 4.0;
                initChunk((int64_t)(i + px + 1) << 2, (int64_t)(j + pz + 1) << 2);
                double dx = jit() + 4.0, dz = jit() + 4.0;
                int b = in[i + 1 + j * pw], d = in[i + 1 + (j + 1) * pw];
                for (int v = 0; v < 4; ++v) {
                    int idx = ((j << 2) + v) * tw + (i << 2);
                    for (int u = 0; u < 4; ++u) {
                        double da = (v - az) * (v - az) + (u - ax) * (u - ax);
                        double db = (v - bz) * (v - bz) + (u - bx) * (u - bx);
                        double dc = (v - cz) * (v - cz) + (u - cx) * (u - cx);
                        double dd = (v - dz) * (v - dz) + (u - dx) * (u - dx);
                        int r = d;
                        if (da < db && da < dc && da < dd) r = a;
                        else if (db < da && db < dc && db < dd) r = b;
                        else if (dc < da && dc < db && dc < dd) r = c;
                        tmp[idx++] = r;
                    }
                }
                a = b;
                c = d;
            }
        }
        std::vector<int> o(w * h);
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i) o[i + j * w] = tmp[(j + (z & 3)) * tw + i + (x & 3)];
        return o;
    }
};

// Высоты биомов (minHeight, maxHeight) из 1.0/1.4.2
void biomeHeights(int b, float& mn, float& mx) {
    mn = 0.1f; mx = 0.3f;
    switch ((Biome)b) {
    case Biome::Ocean: mn = -1.f; mx = 0.4f; break;
    case Biome::Desert: mn = 0.1f; mx = 0.2f; break;
    case Biome::ExtremeHills: mn = 0.2f; mx = 1.8f; break;
    case Biome::Taiga: mn = 0.1f; mx = 0.4f; break;
    case Biome::Swampland: mn = -0.2f; mx = 0.1f; break;
    case Biome::River: case Biome::FrozenRiver: mn = -0.5f; mx = 0.f; break;
    case Biome::FrozenOcean: mn = -1.f; mx = 0.5f; break;
    case Biome::MushroomIsland: mn = 0.2f; mx = 1.f; break;
    case Biome::MushroomShore: mn = -1.f; mx = 0.1f; break;
    case Biome::Beach: mn = 0.0f; mx = 0.1f; break;
    case Biome::DesertHills: mn = 0.3f; mx = 0.7f; break;
    case Biome::ForestHills: mn = 0.3f; mx = 0.7f; break;
    case Biome::TaigaHills: mn = 0.3f; mx = 0.7f; break;
    case Biome::ExtremeHillsEdge: mn = 0.2f; mx = 0.8f; break;
    case Biome::Jungle: mn = 0.1f; mx = 0.4f; break;
    case Biome::JungleHills: mn = 0.3f; mx = 0.7f; break;
    case Biome::IceMountains: mn = 0.3f; mx = 0.7f; break;
    default: break;
    }
}

const int XS = 5, YS = 17, ZS = 5; // узлы сетки плотности на чанк (ячейки 4x8x4)

} // namespace

// ---- Всё состояние генератора 1.0 для одного сида
struct Gen10 {
    Octaves n1, n2, n3, n4, n5, n6;
    LayerP coarse;   // биомы 1:4 (для рельефа)
    LayerP full;     // биомы 1:1 (Вороной)
    int64_t seed;
    float weights[25];

    static JRandom& seeded(JRandom& r, int64_t s) { r.setSeed(s); return r; }
    explicit Gen10(int64_t s, JRandom r = JRandom())
        : n1(seeded(r, s), 16), n2(r, 16), n3(r, 8), n4(r, 4), n5(r, 10), n6(r, 16), seed(s) {
        for (int i = -2; i <= 2; ++i)
            for (int j = -2; j <= 2; ++j) weights[i + 2 + (j + 2) * 5] = 10.f / std::sqrt(i * i + j * j + 0.2f);
        LayerP l = std::make_shared<LIsland>(1);
        l = std::make_shared<LZoom>(2000, l, true);
        l = std::make_shared<LAddIsland>(1, l);
        l = std::make_shared<LZoom>(2001, l);
        l = std::make_shared<LAddIsland>(2, l);
        l = std::make_shared<LAddSnow>(2, l);
        l = std::make_shared<LZoom>(2002, l);
        l = std::make_shared<LAddIsland>(3, l);
        l = std::make_shared<LZoom>(2003, l);
        l = std::make_shared<LAddIsland>(4, l);
        l = std::make_shared<LAddMushroom>(5, l);
        const int biomeSize = 4;
        LayerP river = std::make_shared<LRiverInit>(100, l);
        river = magnify(1000, river, biomeSize + 2);
        river = std::make_shared<LRiver>(1, river);
        river = std::make_shared<LSmooth>(1000, river);
        LayerP bio = std::make_shared<LBiome>(200, l);
        bio = magnify(1000, bio, 2);
        for (int i = 0; i < biomeSize; ++i) {
            bio = std::make_shared<LZoom>(1000 + i, bio);
            if (i == 0) bio = std::make_shared<LAddIsland>(3, bio);
            if (i == 1) {
                bio = std::make_shared<LShore>(1000, bio);
                bio = std::make_shared<LHills>(1000, bio);
            }
        }
        bio = std::make_shared<LSmooth>(1000, bio);
        auto mix = std::make_shared<LRiverMix>(100, bio, river);
        coarse = mix;
        full = std::make_shared<LVoronoi>(10, coarse);
        mix->initWorld(s);
        mix->initRivers(s);
        full->initWorld(s);
    }

    // Плотность в узлах сетки чанка (как initializeNoiseField 1.0)
    void density(int cx, int cz, double out[XS * ZS * YS]) {
        std::vector<int> bio = coarse->get(cx * 4 - 2, cz * 4 - 2, XS + 5, ZS + 5);
        const double d = 684.412;
        int k = 0;
        for (int i = 0; i < XS; ++i)
            for (int j = 0; j < ZS; ++j) {
                double gx = cx * 4 + i, gz = cz * 4 + j;
                float mxAvg = 0, mnAvg = 0, wsum = 0, cmn, cmx;
                biomeHeights(bio[i + 2 + (j + 2) * (XS + 5)], cmn, cmx);
                for (int a = -2; a <= 2; ++a)
                    for (int b = -2; b <= 2; ++b) {
                        float mn, mx;
                        biomeHeights(bio[i + a + 2 + (j + b + 2) * (XS + 5)], mn, mx);
                        float wgt = weights[a + 2 + (b + 2) * 5] / (mn + 2.f);
                        if (mn > cmn) wgt /= 2.f;
                        mxAvg += mx * wgt;
                        mnAvg += mn * wgt;
                        wsum += wgt;
                    }
                mxAvg /= wsum;
                mnAvg /= wsum;
                mxAvg = mxAvg * 0.9f + 0.1f;
                mnAvg = (mnAvg * 4.f - 1.f) / 8.f;
                // Крупные «впадины и плато» по двумерному шуму глубины
                double depth = n6.at(gx, 10.0, gz, 200.0, 1.0, 200.0) / 8000.0;
                if (depth < 0) depth = -depth * 0.3;
                depth = depth * 3.0 - 2.0;
                if (depth < 0) {
                    depth /= 2.0;
                    if (depth < -1.0) depth = -1.0;
                    depth /= 1.4;
                    depth /= 2.0;
                } else {
                    if (depth > 1.0) depth = 1.0;
                    depth /= 8.0;
                }
                for (int y = 0; y < YS; ++y) {
                    double base = mnAvg + depth * 0.2;
                    base = base * YS / 16.0;
                    double centre = YS / 2.0 + base * 4.0;
                    double falloff = ((y - centre) * 12.0 * 128.0) / 128.0 / mxAvg;
                    if (falloff < 0) falloff *= 4.0;
                    double lo = n1.at(gx, y, gz, d, d, d) / 512.0;
                    double hi = n2.at(gx, y, gz, d, d, d) / 512.0;
                    double sel = (n3.at(gx, y, gz, d / 80.0, d / 160.0, d / 80.0) / 10.0 + 1.0) / 2.0;
                    double v = sel < 0 ? lo : sel > 1 ? hi : lo + (hi - lo) * sel;
                    v -= falloff;
                    if (y > YS - 4) {
                        double t = (y - (YS - 4)) / 3.0;
                        v = v * (1.0 - t) - 10.0 * t;
                    }
                    out[k++] = v;
                }
            }
    }

    // Камень и вода по плотности (трилинейная интерполяция узлов); heights — верхний камень колонки
    void terrain(int cx, int cz, Chunk* c, int heights[CW][CW]) {
        double n[XS * ZS * YS];
        density(cx, cz, n);
        for (int x = 0; x < CW; ++x)
            for (int z = 0; z < CW; ++z) heights[x][z] = 0;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                for (int k = 0; k < 16; ++k) {
                    auto N = [&](int a, int b, int y) { return n[((i + a) * ZS + (j + b)) * YS + y]; };
                    double v00 = N(0, 0, k), v01 = N(0, 1, k), v10 = N(1, 0, k), v11 = N(1, 1, k);
                    double s00 = (N(0, 0, k + 1) - v00) * 0.125, s01 = (N(0, 1, k + 1) - v01) * 0.125;
                    double s10 = (N(1, 0, k + 1) - v10) * 0.125, s11 = (N(1, 1, k + 1) - v11) * 0.125;
                    for (int ly = 0; ly < 8; ++ly) {
                        double a = v00, b = v01;
                        double da = (v10 - v00) * 0.25, db = (v11 - v01) * 0.25;
                        for (int lx = 0; lx < 4; ++lx) {
                            double v = a, dv = (b - a) * 0.25;
                            for (int lz = 0; lz < 4; ++lz) {
                                int x = i * 4 + lx, z = j * 4 + lz, y = k * 8 + ly;
                                uint8_t blk = v > 0.0 ? STONE : (y <= SEA ? WATER : AIR);
                                if (c) c->set(x, y, z, blk);
                                if (blk == STONE) heights[x][z] = y;
                                v += dv;
                            }
                            a += da;
                            b += db;
                        }
                        v00 += s00; v01 += s01; v10 += s10; v11 += s11;
                    }
                }
    }

    // Поверхность по биому (replaceBlocksForBiome): слой верхнего блока и заполнителя толщиной по шуму,
    // под водой — заполнитель, песок снизу подпирается песчаником, бедрок снизу неровный
    void surface(Chunk& c) {
        JRandom r((int64_t)c.cx * 0x4F9939F508LL + (int64_t)c.cz * 0x1EF1565BD5LL);
        const int seaLevel = SEA + 1;
        for (int x = 0; x < CW; ++x)
            for (int z = 0; z < CW; ++z) {
                Biome b = (Biome)c.biome[z * CW + x];
                const BiomeInfo& bi = biomeInfo(b);
                double sn = n4.at(c.cx * 16 + x, c.cz * 16 + z, 0.0, 0.0625, 0.0625, 0.0625);
                int depth = (int)(sn / 3.0 + 3.0 + r.nextDouble() * 0.25);
                int run = -1;
                uint8_t top = bi.top, filler = bi.filler;
                for (int y = CH - 1; y >= 0; --y) {
                    if (y <= r.nextInt(5)) { c.set(x, y, z, BEDROCK); continue; }
                    uint8_t cur = c.get(x, y, z);
                    if (cur == AIR) { run = -1; continue; }
                    if (cur != STONE) continue;
                    if (run == -1) {
                        if (depth <= 0) { top = AIR; filler = STONE; }
                        else if (y >= seaLevel - 4 && y <= seaLevel + 1) { top = bi.top; filler = bi.filler; }
                        if (y < seaLevel && top == AIR) top = WATER;
                        run = depth;
                        c.set(x, y, z, y >= seaLevel - 1 ? top : filler);
                    } else if (run > 0) {
                        --run;
                        c.set(x, y, z, filler);
                        if (run == 0 && filler == SAND) { run = r.nextInt(4); filler = SANDSTONE; }
                    }
                }
            }
    }

    // ---- Пещеры (MapGenCaves): от каждого чанка в радиусе 8 тянутся «черви», режем только свой чанк
    void caveNode(int64_t nodeSeed, Chunk& c, double x, double y, double z, float width, float yaw, float pitch,
                  int step, int maxSteps, double yScale) {
        double cxm = c.cx * 16 + 8, czm = c.cz * 16 + 8;
        float dYaw = 0.f, dPitch = 0.f;
        JRandom r(nodeSeed);
        if (maxSteps <= 0) {
            int m = 8 * 16 - 16;
            maxSteps = m - r.nextInt(m / 4);
        }
        bool room = false;
        if (step == -1) { step = maxSteps / 2; room = true; }
        int branchAt = r.nextInt(maxSteps / 2) + maxSteps / 4;
        bool steep = r.nextInt(6) == 0;
        for (; step < maxSteps; ++step) {
            double rad = 1.5 + std::sin(step * PI_D / maxSteps) * width;
            double radY = rad * yScale;
            float cp = std::cos(pitch), sp = std::sin(pitch);
            x += std::cos(yaw) * cp;
            y += sp;
            z += std::sin(yaw) * cp;
            pitch *= steep ? 0.92f : 0.7f;
            pitch += dPitch * 0.1f;
            yaw += dYaw * 0.1f;
            dPitch *= 0.9f;
            dYaw *= 0.75f;
            dPitch += (r.nextFloat() - r.nextFloat()) * r.nextFloat() * 2.f;
            dYaw += (r.nextFloat() - r.nextFloat()) * r.nextFloat() * 4.f;
            if (!room && step == branchAt && width > 1.f) {
                caveNode(r.nextLong(), c, x, y, z, r.nextFloat() * 0.5f + 0.5f, yaw - (float)PI_D / 2, pitch / 3.f, step, maxSteps, 1.0);
                caveNode(r.nextLong(), c, x, y, z, r.nextFloat() * 0.5f + 0.5f, yaw + (float)PI_D / 2, pitch / 3.f, step, maxSteps, 1.0);
                return;
            }
            if (!room && r.nextInt(4) == 0) continue;
            double ddx = x - cxm, ddz = z - czm, left = maxSteps - step, reach = width + 2.f + 16.f;
            if (ddx * ddx + ddz * ddz - left * left > reach * reach) return;
            if (x < cxm - 16 - rad * 2 || z < czm - 16 - rad * 2 || x > cxm + 16 + rad * 2 || z > czm + 16 + rad * 2) continue;
            carve(c, x, y, z, rad, radY, 1, nullptr);
            if (room) break;
        }
    }

    // Вырезать эллипсоид (ravine: своя ширина на каждый y); вода рядом — не режем, ниже y=10 — лава
    void carve(Chunk& c, double x, double y, double z, double rad, double radY, int kind, const float* ravine) {
        int x0 = std::max(0, (int)std::floor(x - rad) - c.cx * 16 - 1), x1 = std::min(16, (int)std::floor(x + rad) - c.cx * 16 + 1);
        int y0 = std::max(1, (int)std::floor(y - radY) - 1), y1 = std::min(120, (int)std::floor(y + radY) + 1);
        int z0 = std::max(0, (int)std::floor(z - rad) - c.cz * 16 - 1), z1 = std::min(16, (int)std::floor(z + rad) - c.cz * 16 + 1);
        if (x0 >= x1 || z0 >= z1 || y0 >= y1) return;
        // Вода на границе объёма — этот кусок не режем (иначе пещера зальёт мир)
        for (int bx = x0; bx < x1; ++bx)
            for (int bz = z0; bz < z1; ++bz)
                for (int by = std::max(0, y0 - 1); by <= std::min(CH - 1, y1 + 1); ++by) {
                    bool shell = bx == x0 || bx == x1 - 1 || bz == z0 || bz == z1 - 1 || by == y0 - 1 || by == y1 + 1;
                    if (!shell) continue;
                    if (c.get(bx, by, bz) == WATER) return;
                }
        for (int bx = x0; bx < x1; ++bx) {
            double dx = (bx + c.cx * 16 + 0.5 - x) / rad;
            for (int bz = z0; bz < z1; ++bz) {
                double dz = (bz + c.cz * 16 + 0.5 - z) / rad;
                if (!ravine && dx * dx + dz * dz >= 1.0) continue;
                bool grass = false;
                for (int by = y1 - 1; by >= y0; --by) {
                    double dy = (by + 0.5 - y) / radY;
                    bool inside = ravine ? (dx * dx + dz * dz) * ravine[by] + dy * dy / 6.0 < 1.0
                                         : (dy > -0.7 && dx * dx + dy * dy + dz * dz < 1.0);
                    if (!inside) continue;
                    uint8_t b = c.get(bx, by, bz);
                    if (b == GRASS) grass = true;
                    if (b == STONE || b == DIRT || b == GRASS || b == SAND || b == SANDSTONE || b == GRAVEL || b == MYCELIUM) {
                        if (by < 10) c.set(bx, by, bz, LAVA);
                        else {
                            c.set(bx, by, bz, AIR);
                            if (grass && by > 0 && c.get(bx, by - 1, bz) == DIRT) c.set(bx, by - 1, bz, GRASS);
                        }
                    }
                }
            }
        }
        (void)kind;
    }

    void caves(Chunk& c) {
        JRandom r(seed);
        int64_t a = r.nextLong() / 2 * 2 + 1, b = r.nextLong() / 2 * 2 + 1;
        for (int ox = c.cx - 8; ox <= c.cx + 8; ++ox)
            for (int oz = c.cz - 8; oz <= c.cz + 8; ++oz) {
                r.setSeed(((int64_t)ox * a + (int64_t)oz * b) ^ seed);
                int n = r.nextInt(r.nextInt(r.nextInt(40) + 1) + 1);
                if (r.nextInt(15) != 0) n = 0;
                for (int i = 0; i < n; ++i) {
                    double x = ox * 16 + r.nextInt(16), y = r.nextInt(r.nextInt(120) + 8), z = oz * 16 + r.nextInt(16);
                    int tunnels = 1;
                    if (r.nextInt(4) == 0) {
                        // Большая комната: короткий толстый «червь» от середины
                        int64_t s = r.nextLong();
                        caveNode(s, c, x, y, z, 1.f + r.nextFloat() * 6.f, 0.f, 0.f, -1, -1, 0.5);
                        tunnels += r.nextInt(4);
                    }
                    for (int t = 0; t < tunnels; ++t) {
                        float yaw = r.nextFloat() * (float)PI_D * 2.f;
                        float pitch = (r.nextFloat() - 0.5f) * 2.f / 8.f;
                        float width = r.nextFloat() * 2.f + r.nextFloat();
                        caveNode(r.nextLong(), c, x, y, z, width, yaw, pitch, 0, 0, 1.0);
                    }
                }
            }
    }

    // ---- Ущелья (MapGenRavine): 1 из 50 чанков, высокий узкий разрез с неровными стенами
    void ravineNode(int64_t nodeSeed, Chunk& c, double x, double y, double z, float width, float yaw, float pitch, double yScale) {
        double cxm = c.cx * 16 + 8, czm = c.cz * 16 + 8;
        float dYaw = 0.f, dPitch = 0.f;
        JRandom r(nodeSeed);
        int m = 8 * 16 - 16;
        int maxSteps = m - r.nextInt(m / 4);
        float table[CH];
        float f = 1.f;
        for (int i = 0; i < CH; ++i) {
            if (i == 0 || r.nextInt(3) == 0) f = 1.f + r.nextFloat() * r.nextFloat();
            table[i] = f * f;
        }
        for (int step = 0; step < maxSteps; ++step) {
            double rad = 1.5 + std::sin(step * PI_D / maxSteps) * width;
            double radY = rad * yScale;
            rad *= r.nextFloat() * 0.25 + 0.75;
            radY *= r.nextFloat() * 0.25 + 0.75;
            float cp = std::cos(pitch), sp = std::sin(pitch);
            x += std::cos(yaw) * cp;
            y += sp;
            z += std::sin(yaw) * cp;
            pitch *= 0.7f;
            pitch += dPitch * 0.05f;
            yaw += dYaw * 0.05f;
            dPitch *= 0.8f;
            dYaw *= 0.5f;
            dPitch += (r.nextFloat() - r.nextFloat()) * r.nextFloat() * 2.f;
            dYaw += (r.nextFloat() - r.nextFloat()) * r.nextFloat() * 4.f;
            if (r.nextInt(4) == 0) continue;
            double ddx = x - cxm, ddz = z - czm, left = maxSteps - step, reach = width + 2.f + 16.f;
            if (ddx * ddx + ddz * ddz - left * left > reach * reach) return;
            if (x < cxm - 16 - rad * 2 || z < czm - 16 - rad * 2 || x > cxm + 16 + rad * 2 || z > czm + 16 + rad * 2) continue;
            carve(c, x, y, z, rad, radY, 2, table);
        }
    }

    void ravines(Chunk& c) {
        JRandom r(seed);
        int64_t a = r.nextLong() / 2 * 2 + 1, b = r.nextLong() / 2 * 2 + 1;
        for (int ox = c.cx - 8; ox <= c.cx + 8; ++ox)
            for (int oz = c.cz - 8; oz <= c.cz + 8; ++oz) {
                r.setSeed(((int64_t)ox * a + (int64_t)oz * b) ^ seed);
                if (r.nextInt(50) != 0) continue;
                double x = ox * 16 + r.nextInt(16), y = r.nextInt(r.nextInt(40) + 8) + 20, z = oz * 16 + r.nextInt(16);
                float yaw = r.nextFloat() * (float)PI_D * 2.f;
                float pitch = (r.nextFloat() - 0.5f) * 2.f / 8.f;
                float width = (r.nextFloat() * 2.f + r.nextFloat()) * 2.f;
                ravineNode(r.nextLong(), c, x, y, z, width, yaw, pitch, 3.0);
            }
    }
};

// ---- Наполнение чанка (populate): озёра, руды, пятна песка/гравия/глины у воды
namespace {

// Озеро (WorldGenLakes): несколько эллипсоидов в коробке 16x8x16; не строится, если край открыт наружу
void lake(Chunk& c, JRandom& r, int baseY, uint8_t liquid) {
    int y = baseY;
    while (y > 5 && c.get(8, y, 8) == AIR) --y;
    if (y <= 4) return;
    y -= 4;
    bool fill[16 * 16 * 8] = {};
    int blobs = r.nextInt(4) + 4;
    for (int i = 0; i < blobs; ++i) {
        double rx = r.nextDouble() * 6.0 + 3.0, ry = r.nextDouble() * 4.0 + 2.0, rz = r.nextDouble() * 6.0 + 3.0;
        double px = r.nextDouble() * (16.0 - rx - 2.0) + 1.0 + rx / 2.0;
        double py = r.nextDouble() * (8.0 - ry - 4.0) + 2.0 + ry / 2.0;
        double pz = r.nextDouble() * (16.0 - rz - 2.0) + 1.0 + rz / 2.0;
        for (int x = 1; x < 15; ++x)
            for (int z = 1; z < 15; ++z)
                for (int yy = 1; yy < 7; ++yy) {
                    double dx = (x - px) / (rx / 2.0), dy = (yy - py) / (ry / 2.0), dz = (z - pz) / (rz / 2.0);
                    if (dx * dx + dy * dy + dz * dz < 1.0) fill[(x * 16 + z) * 8 + yy] = true;
                }
    }
    auto F = [&](int x, int yy, int z) { return x >= 0 && x < 16 && z >= 0 && z < 16 && yy >= 0 && yy < 8 && fill[(x * 16 + z) * 8 + yy]; };
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z)
            for (int yy = 0; yy < 8; ++yy) {
                bool edge = !F(x, yy, z) && (F(x + 1, yy, z) || F(x - 1, yy, z) || F(x, yy + 1, z) || F(x, yy - 1, z) || F(x, yy, z + 1) || F(x, yy, z - 1));
                if (!edge) continue;
                uint8_t b = c.get(x, y + yy, z);
                if (yy >= 4 && isLiquid(b)) return;
                if (yy < 4 && !isSolid(b) && b != liquid) return;
            }
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z)
            for (int yy = 0; yy < 8; ++yy)
                if (F(x, yy, z)) c.set(x, y + yy, z, yy >= 4 ? AIR : liquid);
    // Земля, открывшаяся небу, снова зарастает травой
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z)
            for (int yy = 4; yy < 8; ++yy)
                if (F(x, yy, z) && c.get(x, y + yy - 1, z) == DIRT) c.set(x, y + yy - 1, z, GRASS);
    // У лавового озера каменная кайма
    if (liquid == LAVA)
        for (int x = 0; x < 16; ++x)
            for (int z = 0; z < 16; ++z)
                for (int yy = 0; yy < 8; ++yy) {
                    bool edge = !F(x, yy, z) && (F(x + 1, yy, z) || F(x - 1, yy, z) || F(x, yy + 1, z) || F(x, yy - 1, z) || F(x, yy, z + 1) || F(x, yy, z - 1));
                    if (edge && (yy < 4 || r.nextInt(2) != 0) && isSolid(c.get(x, y + yy, z))) c.set(x, y + yy, z, STONE);
                }
}

// Руда каплей (WorldGenMinable): вдоль отрезка — шары переменного радиуса, заменяет камень
void ore(Chunk& c, JRandom& r, uint8_t block, int size, int x, int y, int z) {
    float a = r.nextFloat() * (float)PI_D;
    double x1 = x + 8 + std::sin(a) * size / 8.0, x2 = x + 8 - std::sin(a) * size / 8.0;
    double z1 = z + 8 + std::cos(a) * size / 8.0, z2 = z + 8 - std::cos(a) * size / 8.0;
    double y1 = y + r.nextInt(3) + 2, y2 = y + r.nextInt(3) + 2;
    for (int i = 0; i <= size; ++i) {
        double cx = x1 + (x2 - x1) * i / size, cy = y1 + (y2 - y1) * i / size, cz = z1 + (z2 - z1) * i / size;
        double k = r.nextDouble() * size / 16.0;
        double rh = (std::sin(i * PI_D / size) + 1.0) * k + 1.0, rv = rh;
        int bx0 = (int)std::floor(cx - rh / 2), bx1 = (int)std::floor(cx + rh / 2);
        int by0 = (int)std::floor(cy - rv / 2), by1 = (int)std::floor(cy + rv / 2);
        int bz0 = (int)std::floor(cz - rh / 2), bz1 = (int)std::floor(cz + rh / 2);
        for (int bx = bx0; bx <= bx1; ++bx) {
            double dx = (bx + 0.5 - cx) / (rh / 2);
            if (dx * dx >= 1.0) continue;
            for (int by = by0; by <= by1; ++by) {
                double dy = (by + 0.5 - cy) / (rv / 2);
                if (dx * dx + dy * dy >= 1.0) continue;
                for (int bz = bz0; bz <= bz1; ++bz) {
                    double dz = (bz + 0.5 - cz) / (rh / 2);
                    int lx = bx - c.cx * 16, lz = bz - c.cz * 16;
                    if (dx * dx + dy * dy + dz * dz >= 1.0 || lx < 0 || lx >= 16 || lz < 0 || lz >= 16 || by < 1 || by >= CH) continue;
                    if (c.get(lx, by, lz) == STONE) c.set(lx, by, lz, block);
                }
            }
        }
    }
}

// Пятно (WorldGenSand / WorldGenClay): диск у воды заменяет землю/траву (глина — только землю... и траву) на свой блок
void disk(Chunk& c, JRandom& r, uint8_t block, int radius, int x, int z, int halfH) {
    // Верхний твёрдый или жидкий блок
    int y = CH - 1;
    while (y > 0 && c.get(x, y, z) == AIR) --y;
    if (c.get(x, y, z) != WATER) return;
    int rad = r.nextInt(radius - 2) + 2;
    for (int bx = x - rad; bx <= x + rad; ++bx)
        for (int bz = z - rad; bz <= z + rad; ++bz) {
            int dx = bx - x, dz = bz - z;
            if (dx * dx + dz * dz > rad * rad || bx < 0 || bx >= 16 || bz < 0 || bz >= 16) continue;
            for (int by = y - halfH; by <= y + halfH; ++by) {
                if (by < 1 || by >= CH) continue;
                uint8_t b = c.get(bx, by, bz);
                if (b == DIRT || (b == GRASS && block != CLAY)) c.set(bx, by, bz, block);
                else if (b == GRASS && block == CLAY) c.set(bx, by, bz, block);
            }
        }
}

} // namespace

Gen10& World::gen10() const {
    if (!gen10_) gen10_ = std::make_shared<Gen10>((int64_t)(int32_t)seed_);
    return *gen10_;
}

Biome World::biomeAt10(int x, int z) const {
    std::vector<int> v = gen10().full->get(x, z, 1, 1);
    return (Biome)std::clamp(v[0], 0, (int)Biome::COUNT - 1);
}

int World::terrainHeight10(int x, int z) const {
    int cx = floorDiv(x, CW), cz = floorDiv(z, CW);
    int64_t key = chunkKey(cx, cz);
    auto it = heightCache_.find(key);
    if (it == heightCache_.end()) {
        if (heightCache_.size() > 4096) heightCache_.clear();
        int h[CW][CW];
        gen10().terrain(cx, cz, nullptr, h);
        std::array<uint8_t, CW * CW> a{};
        for (int i = 0; i < CW; ++i)
            for (int j = 0; j < CW; ++j) a[j * CW + i] = (uint8_t)h[i][j];
        it = heightCache_.emplace(key, a).first;
    }
    return it->second[(z - cz * CW) * CW + (x - cx * CW)];
}

void World::generateOverworld10(Chunk& c) {
    Gen10& g = gen10();
    std::vector<int> bio = g.full->get(c.cx * CW, c.cz * CW, CW, CW);
    for (int i = 0; i < CW * CW; ++i) c.biome[i] = (uint8_t)std::clamp(bio[i], 0, (int)Biome::COUNT - 1);

    int raw[CW][CW];
    g.terrain(c.cx, c.cz, &c, raw);
    g.surface(c);
    g.caves(c);
    g.ravines(c);

    // Наполнение: озёра, руды, пятна у воды
    JRandom r((int64_t)(int32_t)seed_);
    int64_t ka = r.nextLong() / 2 * 2 + 1, kb = r.nextLong() / 2 * 2 + 1;
    r.setSeed(((int64_t)c.cx * ka + (int64_t)c.cz * kb) ^ (int64_t)(int32_t)seed_);
    if (r.nextInt(4) == 0) lake(c, r, r.nextInt(CH), WATER);
    if (r.nextInt(8) == 0) {
        int y = r.nextInt(r.nextInt(CH - 8) + 8);
        if (y < SEA + 1 || r.nextInt(10) == 0) lake(c, r, y, LAVA);
    }
    int bx = c.cx * CW, bz = c.cz * CW;
    auto ores = [&](int count, uint8_t b, int size, int maxY) {
        for (int i = 0; i < count; ++i) ore(c, r, b, size, bx + r.nextInt(16) - 8, r.nextInt(maxY), bz + r.nextInt(16) - 8);
    };
    ores(20, DIRT, 32, CH);
    ores(10, GRAVEL, 32, CH);
    ores(20, COAL_ORE, 16, CH);
    ores(20, IRON_ORE, 8, CH / 2);
    ores(2, GOLD_ORE, 8, CH / 4);
    ores(8, REDSTONE_ORE, 7, CH / 8);
    ores(1, DIAMOND_ORE, 7, CH / 8);
    ore(c, r, LAPIS_ORE, 6, bx + r.nextInt(16) - 8, r.nextInt(16) + r.nextInt(16), bz + r.nextInt(16) - 8);
    for (int i = 0; i < 3; ++i) disk(c, r, SAND, 7, r.nextInt(16), r.nextInt(16), 2);
    disk(c, r, CLAY, 4, r.nextInt(16), r.nextInt(16), 1);
    disk(c, r, GRAVEL, 6, r.nextInt(16), r.nextInt(16), 2);

    // Высоты поверхности для растительности: верхний не-воздух и не-жидкость
    int heights[CW][CW];
    for (int x = 0; x < CW; ++x)
        for (int z = 0; z < CW; ++z) {
            int y = CH - 1;
            while (y > 0 && (c.get(x, y, z) == AIR || isLiquid(c.get(x, y, z)))) --y;
            heights[x][z] = y;
        }
    placeMineshafts(c);
    placeDungeons(c);
    decorate(c, heights);
    if (genVersion_ >= 3) {
        // Родники (WorldGenLiquids 1.0): источник в камне — сверху и снизу камень, с трёх сторон камень, с одной воздух.
        // 50 попыток воды и 20 лавы на чанк, лава ниже
        auto spring = [&](int x, int y, int z, uint8_t liq) {
            if (x < 1 || x > 14 || z < 1 || z > 14 || y < 1 || y >= CH - 1) return;
            if (c.get(x, y + 1, z) != STONE || c.get(x, y - 1, z) != STONE) return;
            uint8_t self = c.get(x, y, z);
            if (self != AIR && self != STONE) return;
            int stones = 0, airs = 0;
            for (auto [dx, dz] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                uint8_t b = c.get(x + dx, y, z + dz);
                stones += b == STONE;
                airs += b == AIR;
            }
            if (stones != 3 || airs != 1) return;
            c.set(x, y, z, liq, 0);
            scheduleUpdate(bx + x, y, bz + z, 1);
        };
        for (int i = 0; i < 50; ++i) {
            int x = r.nextInt(16), y = r.nextInt(r.nextInt(CH - 8) + 8), z = r.nextInt(16);
            spring(x, y, z, WATER);
        }
        for (int i = 0; i < 20; ++i) {
            int x = r.nextInt(16), y = r.nextInt(r.nextInt(r.nextInt(CH - 16) + 8) + 8), z = r.nextInt(16);
            spring(x, y, z, LAVA);
        }
    }
    if (genVersion_ >= 3) placeStronghold10(c);
    else placeStronghold(c);
    if (genVersion_ >= 3) placeVillage10(c);
    else placeVillage(c);
}
