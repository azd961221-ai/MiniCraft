// Деревни как в 1.0 (генератор мира 3+): колодец в центре, дороги из гравия от него, вдоль дорог — постройки
// по чертежам 1.0 (VillageData.h) с весами и лимитами старых деревень, каждая дверью к дороге.
// Раскладка деревни считается целиком по сиду (одинаково для всех чанков), чанк рисует только свою часть.
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <sstream>
#include "Noise.h"
#include "VillageData.h"
#include "World.h"

namespace {

// Сундук кузницы (1.0): 3..8 предметов из списка с весами
void fillBlacksmithChest(TileEntity& te, uint32_t seed);

struct VRng {
    uint32_t s;
    explicit VRng(uint32_t seed) : s(seed ? seed : 1) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    int range(int lo, int hi) { return hi <= lo ? lo : lo + (int)(next() % (uint32_t)(hi - lo + 1)); }
};

// ---------------------------------------------------------------- Чертежи

struct Blueprint {
    std::vector<std::vector<std::string>> layers; // [слой][строка z] -> символы по x
    int w = 0, d = 0;
    // Направления для ступенек (вверх), дверей (внутрь), факелов/лестниц (к стене), печей/сундуков (лицом): 0..3 = +X,+Z,-X,-Z
    std::vector<std::vector<std::vector<int8_t>>> dir;
    char at(int l, int r, int c) const {
        if (l < 0 || l >= (int)layers.size() || r < 0 || r >= (int)layers[l].size() || c < 0 || c >= (int)layers[l][r].size()) return '.';
        return layers[l][r][c];
    }
};

const int DX[4] = {1, 0, -1, 0}, DZ[4] = {0, 1, 0, -1};

bool solidCh(char ch) {
    return ch == 'C' || ch == 'P' || ch == 'L' || ch == 'S' || ch == 'D' || ch == 'Z' || ch == 'X' || ch == 'K' || ch == 'U' || ch == 'H' ||
           ch == 'E' || ch == '_';
}
bool stairCh(char ch) { return ch == 's' || ch == 'o' || ch == 'd'; }

void inferDirections(Blueprint& b) {
    b.dir.assign(b.layers.size(), {});
    for (size_t l = 0; l < b.layers.size(); ++l) {
        b.dir[l].assign(b.d, std::vector<int8_t>(b.w, -1));
        for (int r = 0; r < b.d; ++r)
            for (int c = 0; c < b.w; ++c) {
                char ch = b.at((int)l, r, c);
                int res = -1;
                if (stairCh(ch)) {
                    // Ступенька поднимается к соседнему сплошному блоку; входная (под домом) и прочие — к середине постройки
                    int solidN = 0, lastSolid = -1;
                    for (int k = 0; k < 4; ++k)
                        if (solidCh(b.at((int)l, r + DZ[k], c + DX[k]))) { ++solidN; lastSolid = k; }
                    if (l > 0 && solidN == 1) res = lastSolid;
                    else {
                        float dxc = (b.w - 1) * 0.5f - c, dzc = (b.d - 1) * 0.5f - r;
                        res = std::abs(dzc) >= std::abs(dxc) ? (dzc > 0 ? 1 : 3) : (dxc > 0 ? 0 : 2);
                    }
                } else if (ch == 'B') {
                    // Дверь: стена идёт вдоль X или Z; «внутрь» — к середине постройки
                    bool wallAlongX = solidCh(b.at((int)l, r, c - 1)) || solidCh(b.at((int)l, r, c + 1));
                    if (wallAlongX) res = r < b.d / 2 ? 1 : 3;
                    else res = c < b.w / 2 ? 0 : 2;
                } else if (ch == 't' || ch == '#') {
                    // К стене в том же слое; факел без стены — на полу
                    for (int k = 0; k < 4 && res < 0; ++k)
                        if (solidCh(b.at((int)l, r + DZ[k], c + DX[k]))) res = k;
                } else if (ch == 'U' || ch == 'H') {
                    // Лицом к пустоте рядом
                    for (int k = 0; k < 4 && res < 0; ++k)
                        if (b.at((int)l, r + DZ[k], c + DX[k]) == '.') res = k;
                    if (res < 0) res = 1;
                }
                b.dir[l][r][c] = (int8_t)res;
            }
    }
}

// Все чертежи: имя -> {plains, desert}
struct BlueprintSet {
    std::map<std::string, Blueprint> plains, desert;
};

const BlueprintSet& blueprints() {
    static BlueprintSet set = [] {
        BlueprintSet s;
        std::istringstream in(VILLAGE_BLUEPRINTS);
        std::string line, name, variant;
        Blueprint* cur = nullptr;
        auto finish = [&]() {
            if (!cur) return;
            for (auto& L : cur->layers) {
                cur->d = std::max(cur->d, (int)L.size());
                for (auto& row : L) cur->w = std::max(cur->w, (int)row.size());
            }
            inferDirections(*cur);
        };
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.rfind("#####", 0) == 0) { finish(); cur = nullptr; name = line.substr(6); continue; }
            if (line.rfind("== ", 0) == 0) {
                finish();
                variant = line.substr(3);
                cur = &(variant == "desert" ? s.desert : s.plains)[name];
                continue;
            }
            if (!cur) continue;
            if (line == "--") { cur->layers.emplace_back(); continue; }
            if (!cur->layers.empty()) cur->layers.back().push_back(line);
        }
        finish();
        return s;
    }();
    return set;
}

// ---------------------------------------------------------------- Раскладка деревни

enum VKind { V_WELL, V_SMALL_HOUSE, V_SHACK, V_LARGE_HOUSE, V_BUTCHER, V_LIBRARY, V_SMALL_FARM, V_LARGE_FARM, V_SMITH, V_CHURCH, V_LAMP, V_COUNT };
const char* V_NAMES[V_COUNT] = {"Well", "Small_house", "Farm_Shack", "Large_house", "Butcher_shop", "Library",
                                "Small_farm", "Large_farm", "Blacksmith", "Church", "Lamp_post"};
// Веса и максимум на деревню (старые деревни 1.0)
const int V_WEIGHT[V_COUNT] = {0, 4, 3, 8, 15, 20, 3, 3, 15, 20, 0};
const int V_MIN[V_COUNT] = {0, 2, 2, 0, 0, 0, 2, 1, 0, 0, 0};
const int V_MAX[V_COUNT] = {0, 4, 5, 3, 2, 2, 4, 4, 1, 1, 0};

struct VPiece {
    VKind kind;
    int x0, z0, w, d; // занятый прямоугольник в мире (после поворота)
    int rot;          // 0: вход к +Z, 1: к -X, 2: к -Z, 3: к +X
    int y = 0;        // уровень слоя 0
};
struct VRoad { int x0, z0, x1, z1; };

struct VillageLayout {
    bool desert = false;
    std::vector<VPiece> pieces;
    std::vector<VRoad> roads;
};

bool overlaps(int ax0, int az0, int ax1, int az1, int bx0, int bz0, int bx1, int bz1) {
    return ax0 <= bx1 && bx0 <= ax1 && az0 <= bz1 && bz0 <= az1;
}

} // namespace

// Центр деревни в области 32x32 чанка (MapGenVillage 1.0: шаг 32, смещение 0..23), только равнины и пустыня
bool World::villageCenter10(int rx, int rz, int& X, int& Z) const {
    uint32_t h = hash3i(rx, 10387312, rz, seed_);
    int ccx = rx * 32 + (int)(h % 24), ccz = rz * 32 + (int)((h >> 8) % 24);
    X = ccx * CW + 8;
    Z = ccz * CW + 8;
    Biome b = biomeAt(X, Z);
    return b == Biome::Plains || b == Biome::Desert;
}

namespace {

VillageLayout buildLayout(const World& w, int X, int Z, uint32_t seed) {
    VillageLayout L;
    L.desert = w.biomeAt(X, Z) == Biome::Desert;
    const BlueprintSet& bs = blueprints();
    VRng r(seed);
    auto bpOf = [&](VKind k) -> const Blueprint& { return bs.plains.at(V_NAMES[k]); };
    int limit[V_COUNT], placed[V_COUNT] = {};
    for (int k = 0; k < V_COUNT; ++k) limit[k] = r.range(V_MIN[k], V_MAX[k]);

    // Колодец в центре
    const Blueprint& wb = bpOf(V_WELL);
    L.pieces.push_back({V_WELL, X - wb.w / 2, Z - wb.d / 2, wb.w, wb.d, 0});

    auto freeRect = [&](int x0, int z0, int x1, int z1) {
        if (std::max(std::abs(x0 - X), std::abs(x1 - X)) > 72 || std::max(std::abs(z0 - Z), std::abs(z1 - Z)) > 72) return false;
        for (auto& p : L.pieces)
            if (overlaps(x0, z0, x1, z1, p.x0, p.z0, p.x0 + p.w - 1, p.z0 + p.d - 1)) return false;
        for (auto& rd : L.roads)
            if (overlaps(x0, z0, x1, z1, rd.x0, rd.z0, rd.x1, rd.z1)) return false;
        return true;
    };
    auto pickKind = [&]() -> int {
        int total = 0;
        for (int k = V_SMALL_HOUSE; k <= V_CHURCH; ++k)
            if (placed[k] < limit[k]) total += V_WEIGHT[k];
        if (total == 0) return -1;
        int v = r.range(0, total - 1);
        for (int k = V_SMALL_HOUSE; k <= V_CHURCH; ++k)
            if (placed[k] < limit[k]) {
                if (v < V_WEIGHT[k]) return k;
                v -= V_WEIGHT[k];
            }
        return -1;
    };

    // Дорога шириной 3 от точки (sx, sz) в направлении dir (0..3) длиной len; вдоль неё — постройки с обеих сторон
    std::function<void(int, int, int, int)> road = [&](int sx, int sz, int dir, int depth) {
        int len = 7 * r.range(3, 5);
        int dx = DX[dir], dz = DZ[dir], px = -dz, pz = dx; // px,pz — «вбок»
        int ex = sx + dx * (len - 1), ez = sz + dz * (len - 1);
        VRoad rd{std::min(sx, ex) - std::abs(px), std::min(sz, ez) - std::abs(pz), std::max(sx, ex) + std::abs(px),
                 std::max(sz, ez) + std::abs(pz)};
        // Дорога не должна налезать на постройки
        for (auto& p : L.pieces)
            if (p.kind != V_WELL && overlaps(rd.x0, rd.z0, rd.x1, rd.z1, p.x0, p.z0, p.x0 + p.w - 1, p.z0 + p.d - 1)) return;
        if (std::max(std::abs(ex - X), std::abs(ez - Z)) > 64) return;
        L.roads.push_back(rd);
        // Постройки вдоль обеих сторон
        for (int side : {1, -1}) {
            int t = 0;
            while (t < len) {
                int kind = pickKind();
                if (kind < 0) break;
                const Blueprint& b = bpOf((VKind)kind);
                // Вход должен смотреть на дорогу: сторона side по оси (px,pz); вход чертежа — к +Z
                int ox = px * side, oz = pz * side; // от дороги к постройке
                int rot = (ox == 0 && oz == 1) ? 2 : (ox == 0 && oz == -1) ? 0 : (ox == 1) ? 1 : 3;
                int bw = (rot & 1) ? b.d : b.w, bd = (rot & 1) ? b.w : b.d;
                // Точка на краю дороги
                int cx = sx + dx * t + ox * 2, cz = sz + dz * t + oz * 2;
                int along = (dx != 0) ? bw : bd;
                int x0, z0;
                if (dx != 0) { x0 = dx > 0 ? cx : cx - bw + 1; z0 = oz > 0 ? cz : cz - bd + 1; }
                else { z0 = dz > 0 ? cz : cz - bd + 1; x0 = ox > 0 ? cx : cx - bw + 1; }
                if (t + along <= len + 2 && freeRect(x0, z0, x0 + bw - 1, z0 + bd - 1)) {
                    L.pieces.push_back({(VKind)kind, x0, z0, bw, bd, rot});
                    ++placed[kind];
                    t += along + 1;
                } else {
                    // Не поместилось — фонарь у дороги иногда, и сдвиг дальше
                    if (r.range(0, 3) == 0 && freeRect(cx - 1 + ox, cz - 1 + oz, cx + 1 + ox, cz + 1 + oz))
                        L.pieces.push_back({V_LAMP, cx - 1 + ox, cz - 1 + oz, 3, 3, 0});
                    t += 2;
                }
            }
        }
        // Развилка в конце дороги
        if (depth < 3)
            for (int side : {1, -1})
                if (r.range(0, 99) < 70) road(ex + dx * 2 + px * side * 2, ez + dz * 2 + pz * side * 2, side > 0 ? ((dir + 1) & 3) : ((dir + 3) & 3), depth + 1);
    };
    // Четыре дороги от колодца
    for (int dir = 0; dir < 4; ++dir) {
        int sx = X + DX[dir] * (wb.w / 2 + 1), sz = Z + DZ[dir] * (wb.d / 2 + 1);
        road(sx, sz, dir, 0);
    }
    // Уровень каждой постройки — средняя высота земли под ней (как в 1.0)
    for (auto& p : L.pieces) {
        long sum = 0;
        int n = 0;
        for (int x = p.x0; x < p.x0 + p.w; x += 2)
            for (int z = p.z0; z < p.z0 + p.d; z += 2) { sum += std::max(SEA, w.terrainHeight(x, z)); ++n; }
        p.y = n ? (int)(sum / n) + 1 : SEA + 1;
    }
    return L;
}

} // namespace

const void* World::villageLayout10(int X, int Z) const {
    static thread_local std::map<std::pair<uint32_t, int64_t>, VillageLayout> cache;
    auto key = std::make_pair(seed_, chunkKey(X, Z));
    auto it = cache.find(key);
    if (it == cache.end()) {
        if (cache.size() > 64) cache.clear();
        it = cache.emplace(key, buildLayout(*this, X, Z, hash3i(X, 77, Z, seed_ ^ 0x5EEDu))).first;
    }
    return &it->second;
}

void World::placeVillage10(Chunk& c) {
    const int bx = c.cx * CW, bz = c.cz * CW;
    const BlueprintSet& bs = blueprints();
    // Деревня влияет на чанки в пределах ~72 блоков от центра: центры соседних областей
    int rx0 = floorDiv(c.cx - 6, 32), rx1 = floorDiv(c.cx + 6, 32), rz0 = floorDiv(c.cz - 6, 32), rz1 = floorDiv(c.cz + 6, 32);
    for (int rx = rx0; rx <= rx1; ++rx)
        for (int rz = rz0; rz <= rz1; ++rz) {
            int X, Z;
            if (!villageCenter10(rx, rz, X, Z)) continue;
            if (bx + CW < X - 80 || bx > X + 80 || bz + CW < Z - 80 || bz > Z + 80) continue;
            const VillageLayout& L = *static_cast<const VillageLayout*>(villageLayout10(X, Z));
            auto in = [&](int x, int z) { return x >= bx && x < bx + CW && z >= bz && z < bz + CW; };
            auto setW = [&](int x, int y, int z, uint8_t b, uint8_t m = 0) {
                if (in(x, z) && y > 0 && y < CH) c.set(x - bx, y, z - bz, b, m);
            };
            auto getW = [&](int x, int y, int z) -> uint8_t { return in(x, z) && y >= 0 && y < CH ? c.get(x - bx, y, z - bz) : AIR; };
            const uint8_t BASE = L.desert ? SANDSTONE : COBBLE;

            // ---- Дороги: верхний блок земли → гравий (в пустыне — песчаник), над водой — доски
            for (const VRoad& rd : L.roads)
                for (int x = std::max(rd.x0, bx); x <= std::min(rd.x1, bx + CW - 1); ++x)
                    for (int z = std::max(rd.z0, bz); z <= std::min(rd.z1, bz + CW - 1); ++z) {
                        int y = CH - 2;
                        while (y > 1) {
                            uint8_t b = getW(x, y, z);
                            if (b != AIR && b != LEAVES && b != LOG && !isPlant(b) && b != SNOW_LAYER && b != CACTUS && b != REEDS) break;
                            --y;
                        }
                        uint8_t top = getW(x, y, z);
                        for (int yy = y + 1; yy < std::min(CH, y + 4); ++yy) {
                            uint8_t a = getW(x, yy, z);
                            if (isPlant(a) || a == SNOW_LAYER) setW(x, yy, z, AIR);
                        }
                        if (top == WATER) setW(x, y, z, PLANKS);
                        else if (top == GRASS || top == DIRT || top == SAND || top == GRAVEL || top == MYCELIUM || top == STONE)
                            setW(x, y, z, L.desert ? SANDSTONE : GRAVEL);
                    }

            // ---- Постройки
            for (const VPiece& p : L.pieces) {
                if (p.x0 > bx + CW - 1 || p.x0 + p.w - 1 < bx || p.z0 > bz + CW - 1 || p.z0 + p.d - 1 < bz) continue;
                const auto& set = L.desert ? bs.desert : bs.plains;
                auto bit = set.find(V_NAMES[p.kind]);
                if (bit == set.end()) bit = bs.plains.find(V_NAMES[p.kind]);
                const Blueprint& b = bit->second;
                const int y0 = p.y + (p.kind == V_WELL ? -2 : 0);
                const int nl = (int)b.layers.size();
                // Мировые координаты клетки чертежа (c — по X, r — по Z) при повороте
                auto toWorld = [&](int cc, int rr, int& x, int& z) {
                    switch (p.rot) {
                    case 0: x = cc; z = rr; break;
                    case 1: x = b.d - 1 - rr; z = cc; break;
                    case 2: x = b.w - 1 - cc; z = b.d - 1 - rr; break;
                    default: x = rr; z = b.w - 1 - cc; break;
                    }
                    x += p.x0;
                    z += p.z0;
                };
                auto rotDir = [&](int k) { // направление 0..3 (+X,+Z,-X,-Z) при повороте
                    int dx = DX[k], dz = DZ[k], rx2, rz2;
                    switch (p.rot) {
                    case 0: rx2 = dx; rz2 = dz; break;
                    case 1: rx2 = -dz; rz2 = dx; break;
                    case 2: rx2 = -dx; rz2 = -dz; break;
                    default: rx2 = dz; rz2 = -dx; break;
                    }
                    return rx2 == 1 ? 0 : rz2 == 1 ? 1 : rx2 == -1 ? 2 : 3;
                };
                // Расчистка объёма над землёй и фундамент до твёрдого
                bool farm = p.kind == V_SMALL_FARM || p.kind == V_LARGE_FARM || p.kind == V_LAMP;
                for (int rr = 0; rr < b.d; ++rr)
                    for (int cc = 0; cc < b.w; ++cc) {
                        int x, z;
                        toWorld(cc, rr, x, z);
                        if (!in(x, z)) continue;
                        bool used = false;
                        for (int l = 0; l < nl; ++l) used |= b.at(l, rr, cc) != '.';
                        if (!used) continue;
                        for (int y = y0 + (p.kind == V_WELL ? 3 : 1); y < std::min(CH - 1, y0 + nl + 3); ++y) setW(x, y, z, AIR);
                        if (b.at(0, rr, cc) != '.' || farm)
                            for (int y = y0 - 1; y > y0 - 12 && y > 0; --y) {
                                uint8_t g = getW(x, y, z);
                                if (isSolid(g) && g != LEAVES && g != LOG) break;
                                setW(x, y, z, BASE);
                            }
                    }
                // Блоки по слоям
                for (int l = 0; l < nl; ++l)
                    for (int rr = 0; rr < b.d; ++rr)
                        for (int cc = 0; cc < b.w; ++cc) {
                            char ch = b.at(l, rr, cc);
                            if (ch == '.') continue;
                            int x, z;
                            toWorld(cc, rr, x, z);
                            int y = y0 + l;
                            if (!in(x, z) || y <= 0 || y >= CH - 1) continue;
                            int k = b.dir[l][rr][cc] >= 0 ? rotDir(b.dir[l][rr][cc]) : -1;
                            uint32_t hv = hash3i(x, y, z, seed_ ^ 0xB1Du);
                            switch (ch) {
                            case 'C': setW(x, y, z, COBBLE); break;
                            case 'P': setW(x, y, z, PLANKS); break;
                            case 'L': setW(x, y, z, LOG, 0); break;
                            case 'S': setW(x, y, z, SANDSTONE); break;
                            case 'd': setW(x, y, z, L.desert ? SANDSTONE : WOOD_STAIRS, L.desert ? 0 : (uint8_t)std::max(0, k)); break;
                            case 's': case 'o': {
                                static const uint8_t SM[4] = {0, 2, 1, 3}; // +X,+Z,-X,-Z -> мета ступенек (+X,-X,+Z,-Z)
                                setW(x, y, z, ch == 's' ? COBBLE_STAIRS : WOOD_STAIRS, SM[std::max(0, k)]);
                                break;
                            }
                            case 'V': setW(x, y, z, LAVA, 0); break;
                            case 'W': setW(x, y, z, WATER, 0); break;
                            case 'F': setW(x, y, z, FENCE); break;
                            case 'D': setW(x, y, z, DOUBLE_SLAB, 0); break;
                            case '_': setW(x, y, z, SLAB, 0); break;
                            case 'G': setW(x, y, z, GLASS_PANE); break;
                            case 'I': setW(x, y, z, IRON_BARS); break;
                            case 'p': setW(x, y, z, WOOD_PLATE); break;
                            case 'E': setW(x, y, z, DIRT); break;
                            case 'X': setW(x, y, z, CRAFTING_TABLE); break;
                            case 'Z': setW(x, y, z, BOOKSHELF); break;
                            case 'K': setW(x, y, z, WOOL, 15); break;
                            case 'f': setW(x, y, z, FARMLAND, 7); break;
                            case 'w': setW(x, y, z, WHEAT, (uint8_t)(2 + hv % 6)); break;
                            case 'U': {
                                static const uint8_t FM[4] = {5, 3, 4, 2}; // лицом к +X,+Z,-X,-Z
                                setW(x, y, z, FURNACE, FM[std::max(0, k)]);
                                break;
                            }
                            case 'H': {
                                static const uint8_t FM[4] = {5, 3, 4, 2};
                                setW(x, y, z, CHEST, FM[std::max(0, k)]);
                                if (in(x, z) && !tileAt(x, y, z)) {
                                    TileEntity& te = createTile(x, y, z, TileEntity::Chest);
                                    fillBlacksmithChest(te, hash3i(x, y, z, seed_ ^ 0xC4E57u));
                                }
                                break;
                            }
                            case 'B': case 'T': {
                                uint8_t f = (uint8_t)std::max(0, k); // «внутрь»: 0 +X, 1 +Z, 2 -X, 3 -Z (как при установке игроком)
                                setW(x, y, z, WOOD_DOOR, ch == 'B' ? f : (uint8_t)(f | 8));
                                break;
                            }
                            case 't': {
                                static const uint8_t TM[4] = {TORCH_EAST_WALL, TORCH_SOUTH_WALL, TORCH_WEST_WALL, TORCH_NORTH_WALL};
                                setW(x, y, z, TORCH, k < 0 ? (uint8_t)TORCH_FLOOR : TM[k]);
                                break;
                            }
                            case '#': setW(x, y, z, LADDER, (uint8_t)std::max(0, k)); break;
                            default: break;
                            }
                        }
                // Жители (как в 1.0: по дому, профессия по постройке)
                int prof = -1, count = 0;
                switch (p.kind) {
                case V_SMALL_HOUSE: case V_SHACK: prof = 0; count = 1; break;
                case V_LARGE_HOUSE: prof = 0; count = 2; break;
                case V_LIBRARY: prof = 1; count = 1; break;
                case V_CHURCH: prof = 2; count = 1; break;
                case V_SMITH: prof = 3; count = 1; break;
                case V_BUTCHER: prof = 4; count = 2; break;
                default: break;
                }
                for (int i = 0; i < count; ++i) {
                    int cx = p.x0 + p.w / 2 + i, cz = p.z0 + p.d / 2;
                    if (in(cx, cz)) villagerSpawns.push_back(glm::vec4(cx + 0.5f, (float)(y0 + 1), cz + 0.5f, (float)prof));
                }
            }
        }
}

namespace {
void fillBlacksmithChest(TileEntity& te, uint32_t seed) {
    struct E { uint16_t id; int lo, hi, w; };
    static const E T[] = {{DIAMOND, 1, 3, 3},        {IRON_INGOT, 1, 5, 10},    {GOLD_INGOT, 1, 3, 5},   {BREAD, 1, 3, 15},
                          {APPLE, 1, 3, 15},         {IRON_PICKAXE, 1, 1, 5},   {IRON_SWORD, 1, 1, 5},   {IRON_CHESTPLATE, 1, 1, 5},
                          {IRON_HELMET, 1, 1, 5},    {IRON_LEGGINGS, 1, 1, 5},  {IRON_BOOTS, 1, 1, 5},   {OBSIDIAN, 3, 7, 5},
                          {SAPLING, 3, 7, 5}};
    VRng r(seed);
    int total = 0;
    for (auto& e : T) total += e.w;
    int n = r.range(3, 8);
    for (int i = 0; i < n; ++i) {
        int v = r.range(0, total - 1);
        for (auto& e : T) {
            if (v < e.w) { te.items[r.range(0, 26)] = makeStack(e.id, r.range(e.lo, e.hi)); break; }
            v -= e.w;
        }
    }
}
} // namespace

bool World::locateVillage10(const glm::vec3& from, glm::ivec3& out) const {
    int fcx = floorDiv((int)std::floor(from.x), CW), fcz = floorDiv((int)std::floor(from.z), CW);
    double best = 1e30;
    bool found = false;
    for (int rx = floorDiv(fcx, 32) - 4; rx <= floorDiv(fcx, 32) + 4; ++rx)
        for (int rz = floorDiv(fcz, 32) - 4; rz <= floorDiv(fcz, 32) + 4; ++rz) {
            int X, Z;
            if (!villageCenter10(rx, rz, X, Z)) continue;
            double d = (X - from.x) * (X - from.x) + (Z - from.z) * (Z - from.z);
            if (d < best) { best = d; out = glm::ivec3(X, std::max(SEA, terrainHeight(X, Z)) + 1, Z); found = true; }
        }
    return found;
}
