// Адская крепость как в 1.0 (генератор мира 3+): мосты с опорами (StructureNetherBridgePieces) и коридоры «замка»;
// куски — по чертежам minecraft.wiki (FortressData.h). Мостовые куски тянут к мостовым, коридорные — к коридорным,
// комната с лавовым колодцем соединяет мосты с замком.
#include <algorithm>
#include <cmath>
#include <map>
#include "FortressData.h"
#include "Noise.h"
#include "StructKit.h"
#include "World.h"

using namespace skit;

namespace {

struct Exit { int dir, r, c, layer; bool castle; }; // castle — дальше идут коридорные куски
struct PieceDef {
    const char* name;
    int weight, limit;
    bool castle; // коридорный кусок (иначе мостовой)
    int er, ec, el;
    std::vector<Exit> exits;
};

Blueprint mirrorX(const Blueprint& b) {
    Blueprint m = b;
    for (auto& L : m.layers)
        for (auto& row : L) {
            row.resize(b.w, '.');
            std::reverse(row.begin(), row.end());
        }
    return m;
}

struct Kit {
    std::map<std::string, Blueprint> bp;
    std::vector<PieceDef> defs;
};

const Kit& kit() {
    static Kit k = [] {
        Kit K;
        K.bp = parse(FORTRESS_BLUEPRINTS);
        K.bp["Corridor_Turn_R"] = mirrorX(K.bp["Corridor_Turn"]);
        // Веса и лимиты — StructureNetherBridgePieces 1.0
        K.defs = {
            // мостовые
            {"Bridge", 30, 0, false, 18, 2, 5, {{3, 0, 2, 5, false}}},
            {"Bridge_Open_Intersection", 10, 4, false, 18, 9, 4, {{3, 0, 9, 4, false}, {2, 9, 0, 4, false}, {0, 9, 18, 4, false}}},
            {"Bridge_Closed_Intersection", 10, 4, false, 6, 3, 1, {{3, 0, 3, 1, false}, {2, 3, 0, 1, false}, {0, 3, 6, 1, false}}},
            {"Bridge_Staircase", 10, 3, false, 6, 3, 1, {{2, 3, 0, 7, false}}},
            {"Blaze_Spawner", 5, 2, false, 8, 3, 2, {}},
            {"Lava_Well_Room", 5, 1, false, 12, 6, 5, {{3, 0, 6, 5, true}}},
            // коридорные («замок»)
            {"Corridor", 25, 0, true, 4, 2, 1, {{3, 0, 2, 1, true}}},
            {"Corridor_Crossing", 15, 5, true, 4, 2, 1, {{3, 0, 2, 1, true}, {2, 2, 0, 1, true}, {0, 2, 4, 1, true}}},
            {"Corridor_Turn", 5, 10, true, 4, 2, 1, {{2, 2, 0, 1, true}}},
            {"Corridor_Turn_R", 5, 10, true, 4, 2, 1, {{0, 2, 4, 1, true}}},
            {"Corridor_Stairs", 10, 3, true, 9, 2, 8, {{3, 0, 2, 1, true}}},
            {"Corridor_Balcony", 7, 2, true, 8, 4, 3, {{2, 6, 0, 3, true}, {0, 6, 8, 3, true}}},
            {"Nether_Wart_Farm", 5, 2, true, 12, 6, 5, {{3, 0, 6, 12, true}}},
        };
        return K;
    }();
    return k;
}

struct FPiece { int def; Placement p; std::vector<bool> exitUsed; };
struct FortressLayout { std::vector<FPiece> pieces; };

FortressLayout buildFortress(int X, int Z, uint32_t seed) {
    const Kit& K = kit();
    FortressLayout L;
    uint32_t s = seed | 1u;
    int count[16] = {};
    auto bpOf = [&](int d) -> const Blueprint& { return K.bp.at(K.defs[d].name); };
    auto boxFree = [&](const Placement& p, int h) {
        if (std::abs(p.x0 + p.w / 2 - X) > 112 || std::abs(p.z0 + p.d / 2 - Z) > 112) return false;
        for (auto& q : L.pieces) {
            int qh = bpOf(q.def).h();
            if (p.x0 < q.p.x0 + q.p.w && q.p.x0 < p.x0 + p.w && p.z0 < q.p.z0 + q.p.d && q.p.z0 < p.z0 + p.d && p.y0 < q.p.y0 + qh &&
                q.p.y0 < p.y0 + h)
                return false;
        }
        return true;
    };
    // Старт — большой открытый перекрёсток мостов
    {
        int d = 1;
        const Blueprint& b = bpOf(d);
        L.pieces.push_back({d, placeAt(b, 0, K.defs[d].ec, K.defs[d].er, K.defs[d].el, X, 0, Z), std::vector<bool>(K.defs[d].exits.size(), false)});
        ++count[d];
    }
    struct Open { int piece, exit, depth; };
    std::vector<Open> open;
    for (int e = 0; e < (int)K.defs[1].exits.size(); ++e) open.push_back({0, e, 0});
    // Вход самого стартового перекрёстка — тоже продолжаем мостом назад
    size_t head = 0;
    while (head < open.size() && L.pieces.size() < 64) {
        Open o = open[head++];
        if (o.depth > 30) continue;
        const FPiece& src = L.pieces[o.piece];
        const Exit& ex = K.defs[src.def].exits[o.exit];
        const Blueprint& sb = bpOf(src.def);
        int ex0, ez0;
        src.p.toWorld(sb, ex.c, ex.r, ex0, ez0);
        int wdir = src.p.rotDir(ex.dir);
        int wx = ex0 + DX[wdir], wz = ez0 + DZ[wdir], wy = src.p.y0 + ex.layer;
        for (int attempt = 0; attempt < 5; ++attempt) {
            int total = 0;
            for (int i = 0; i < (int)K.defs.size(); ++i)
                if (K.defs[i].castle == ex.castle && !(K.defs[i].limit && count[i] >= K.defs[i].limit)) total += K.defs[i].weight;
            if (total == 0) break;
            int v = rr(s, 0, total - 1), d = -1;
            for (int i = 0; i < (int)K.defs.size(); ++i) {
                if (K.defs[i].castle != ex.castle || (K.defs[i].limit && count[i] >= K.defs[i].limit)) continue;
                if (v < K.defs[i].weight) { d = i; break; }
                v -= K.defs[i].weight;
            }
            if (d < 0) break;
            const Blueprint& b = bpOf(d);
            const PieceDef& pd = K.defs[d];
            Placement p = placeAt(b, rotForForward(wdir), pd.ec, pd.er, pd.el, wx, wy, wz);
            if (!boxFree(p, b.h())) continue;
            L.pieces.push_back({d, p, std::vector<bool>(pd.exits.size(), false)});
            L.pieces[o.piece].exitUsed[o.exit] = true;
            ++count[d];
            int idx = (int)L.pieces.size() - 1;
            for (int e = 0; e < (int)pd.exits.size(); ++e) open.push_back({idx, e, o.depth + 1});
            break;
        }
    }
    // Высота: низ крепости между 48 и 70 (setRandomHeight 1.0)
    int minY = 1 << 30;
    for (auto& q : L.pieces) minY = std::min(minY, q.p.y0);
    int shift = rr(s, 48, 70) - minY;
    for (auto& q : L.pieces) q.p.y0 += shift;
    return L;
}

struct LootE { uint16_t id; int lo, hi, w; };
const LootE LOOT_FORTRESS[] = {{DIAMOND, 1, 3, 5},       {IRON_INGOT, 1, 5, 5},       {GOLD_INGOT, 1, 3, 15},
                               {GOLD_SWORD, 1, 1, 5},    {GOLD_CHESTPLATE, 1, 1, 5},  {FLINT_AND_STEEL, 1, 1, 5},
                               {NETHER_WART_ITEM, 3, 7, 5}, {SADDLE, 1, 1, 10}};

} // namespace

bool World::fortressCenter10(int rx, int rz, int& X, int& Z) const {
    uint32_t h = hash3i(rx, 0x4E7B, rz, seed_);
    if (h % 3 != 0) return false; // 1 из 3 областей 16x16 чанков
    X = (rx * 16 + 4 + (int)((h >> 8) % 8)) * CW + 8;
    Z = (rz * 16 + 4 + (int)((h >> 16) % 8)) * CW + 8;
    return true;
}

const void* World::fortressLayout10(int X, int Z) const {
    static thread_local std::map<std::pair<uint32_t, int64_t>, FortressLayout> cache;
    auto key = std::make_pair(seed_, chunkKey(X, Z));
    auto it = cache.find(key);
    if (it == cache.end()) {
        if (cache.size() > 16) cache.clear();
        it = cache.emplace(key, buildFortress(X, Z, hash3i(X, 9999, Z, seed_ ^ 0xF0F7u))).first;
    }
    return &it->second;
}

void World::placeFortress10(Chunk& c) {
    const int bx = c.cx * CW, bz = c.cz * CW;
    const Kit& K = kit();
    for (int rx = floorDiv(c.cx - 8, 16); rx <= floorDiv(c.cx + 8, 16); ++rx)
        for (int rz = floorDiv(c.cz - 8, 16); rz <= floorDiv(c.cz + 8, 16); ++rz) {
            int X, Z;
            if (!fortressCenter10(rx, rz, X, Z)) continue;
            if (bx + CW < X - 130 || bx > X + 130 || bz + CW < Z - 130 || bz > Z + 130) continue;
            const FortressLayout& L = *static_cast<const FortressLayout*>(fortressLayout10(X, Z));
            auto in = [&](int x, int z) { return x >= bx && x < bx + CW && z >= bz && z < bz + CW; };
            auto setW = [&](int x, int y, int z, uint8_t b, uint8_t m = 0) {
                if (in(x, z) && y > 0 && y < CH - 1) c.set(x - bx, y, z - bz, b, m);
            };
            auto getW = [&](int x, int y, int z) -> uint8_t { return in(x, z) && y >= 0 && y < CH ? c.get(x - bx, y, z - bz) : NETHERRACK; };
            for (const FPiece& fp : L.pieces) {
                const Placement& p = fp.p;
                const Blueprint& origB = K.bp.at(K.defs[fp.def].name);
                if (p.x0 > bx + CW - 1 || p.x0 + p.w - 1 < bx || p.z0 > bz + CW - 1 || p.z0 + p.d - 1 < bz) continue;
                const PieceDef& pd = K.defs[fp.def];
                Blueprint b = origB;
                // Заделываем неиспользованные выходы замковых коридоров адским кирпичом
                if (pd.castle) {
                    for (int e = 0; e < (int)pd.exits.size(); ++e) {
                        if (fp.exitUsed[e]) continue;
                        const Exit& ex = pd.exits[e];
                        for (int k = -1; k <= 1; ++k)
                            for (int l = ex.layer; l < ex.layer + 3; ++l) {
                                int r = ex.r + (ex.dir == 1 || ex.dir == 3 ? 0 : k);
                                int cc = ex.c + (ex.dir == 0 || ex.dir == 2 ? 0 : k);
                                b.set(l, r, cc, 'A');
                            }
                    }
                }
                auto solid = [&](int l, int r, int cc) { char ch = b.at(l, r, cc); return ch == 'A' || ch == 'E'; };
                for (int l = 0; l < b.h(); ++l)
                    for (int r = 0; r < b.d; ++r)
                        for (int cc = 0; cc < b.w; ++cc) {
                            char ch = b.at(l, r, cc);
                            int x, z;
                            p.toWorld(b, cc, r, x, z);
                            int y = p.y0 + l;
                            if (!in(x, z) || y <= 0 || y >= CH - 1) continue;
                            switch (ch) {
                            case '.': {
                                // Пустота только над полом куска (под мостом арки — не вырезаем), и только в его контуре
                                if (l < K.defs[fp.def].el) break;
                                bool inside = false;
                                for (int q = 0; q < b.h() && !inside; ++q) inside = b.at(q, r, cc) != '.';
                                if (inside) {
                                    setW(x, y, z, AIR);
                                    // Для открытых мостовых кусков вырезаем воздух над проходом
                                    if (!K.defs[fp.def].castle && l == b.h() - 1) {
                                        for (int dy = 1; dy <= 3; ++dy)
                                            setW(x, y + dy, z, AIR);
                                    }
                                }
                                break;
                            }
                            case 'A':
                                setW(x, y, z, NETHER_BRICK);
                                // Опоры: нижний слой тянется вниз до твёрдого (fillCurrentPositionBlocksDownwards)
                                if (l == 0)
                                    for (int yy = y - 1; yy > 1; --yy) {
                                        uint8_t g = getW(x, yy, z);
                                        if (g != AIR && g != LAVA && g != FIRE) break;
                                        setW(x, yy, z, NETHER_BRICK);
                                    }
                                break;
                            case 'B': setW(x, y, z, NETHER_FENCE); break;
                            case 'D': setW(x, y, z, LAVA, 0); break;
                            case 'E': setW(x, y, z, SOUL_SAND); break;
                            case 'G': setW(x, y, z, NETHER_WART, (uint8_t)(hash3i(x, y, z, seed_) % 4)); break;
                            case 'C':
                                setW(x, y, z, MOB_SPAWNER, 4);
                                newSpawners.push_back({glm::ivec3(x, y, z), 4});
                                break;
                            case 'F': {
                                int k = -1;
                                for (int q = 0; q < 4 && k < 0; ++q)
                                    if (solid(l, r + DZ[q], cc + DX[q]) && !solid(l, r - DZ[q], cc - DX[q])) k = q;
                                if (k < 0) k = 1;
                                static const uint8_t SM[4] = {0, 2, 1, 3};
                                setW(x, y, z, NETHER_STAIRS, SM[p.rotDir(k)]);
                                break;
                            }
                            case 'H': {
                                int k = 1;
                                for (int q = 0; q < 4; ++q)
                                    if (b.at(l, r + DZ[q], cc + DX[q]) == '.') { k = q; break; }
                                static const uint8_t FM[4] = {5, 3, 4, 2};
                                setW(x, y, z, CHEST, FM[p.rotDir(k)]);
                                if (!tileAt(x, y, z)) {
                                    TileEntity& te = createTile(x, y, z, TileEntity::Chest);
                                    uint32_t s = hash3i(x, y, z, seed_ ^ 0xF0C4E5u) | 1u;
                                    int total = 0;
                                    for (auto& e : LOOT_FORTRESS) total += e.w;
                                    int n = rr(s, 2, 5);
                                    for (int i = 0; i < n; ++i) {
                                        int v = rr(s, 0, total - 1);
                                        for (auto& e : LOOT_FORTRESS) {
                                            if (v < e.w) { te.items[rr(s, 0, 26)] = makeStack(e.id, rr(s, e.lo, e.hi)); break; }
                                            v -= e.w;
                                        }
                                    }
                                }
                                break;
                            }
                            default: break;
                            }
                        }
            }
        }
}

bool World::locateFortress10(const glm::vec3& from, glm::ivec3& out) const {
    int fcx = floorDiv((int)std::floor(from.x), CW), fcz = floorDiv((int)std::floor(from.z), CW);
    double best = 1e30;
    bool found = false;
    for (int rx = floorDiv(fcx, 16) - 5; rx <= floorDiv(fcx, 16) + 5; ++rx)
        for (int rz = floorDiv(fcz, 16) - 5; rz <= floorDiv(fcz, 16) + 5; ++rz) {
            int X, Z;
            if (!fortressCenter10(rx, rz, X, Z)) continue;
            double d = (X - from.x) * (X - from.x) + (Z - from.z) * (Z - from.z);
            if (d < best) {
                best = d;
                const FortressLayout& L = *static_cast<const FortressLayout*>(fortressLayout10(X, Z));
                out = glm::ivec3(X, L.pieces.empty() ? 64 : L.pieces[0].p.y0 + 4, Z);
                found = true;
            }
        }
    return found;
}
