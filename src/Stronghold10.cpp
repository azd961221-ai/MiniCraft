// Крепость Края как в 1.0 (генератор мира 3+): сеть кусков StructureStrongholdPieces — коридоры, повороты, развилки,
// лестницы, тюремные камеры, большие комнаты, алтари с сундуком, библиотеки и комната с порталом; на входах — случайные двери.
// Куски — по чертежам minecraft.wiki (StrongholdData.h), повороты и лестницы собраны по размерам 1.0.
#include <algorithm>
#include <cmath>
#include <map>
#include "Noise.h"
#include "StructKit.h"
#include "StrongholdData.h"
#include "World.h"

using namespace skit;

namespace {

struct Exit { int dir, r, c, layer; bool optional; };
struct PieceDef {
    const char* name;
    int weight, limit;
    int er, ec, el;          // клетка входа (строка, столбец, слой пола)
    bool door;               // на входе — случайная дверь 3x3
    std::vector<Exit> exits;
};

// Повороты, лестница и большая комната собраны кодом (в вики эти чертежи склеены из вариантов)
Blueprint makeTurn(bool left) {
    Blueprint b;
    for (int l = 0; l < 5; ++l)
        for (int r = 0; r < 5; ++r)
            for (int c = 0; c < 5; ++c) {
                bool wall = l == 0 || l == 4 || r == 0 || r == 4 || c == 0 || c == 4;
                bool open = l >= 1 && l <= 3 && ((r == 4 && c >= 1 && c <= 3) || (left ? c == 0 : c == 4) && r >= 1 && r <= 3);
                b.set(l, r, c, wall && !open ? 'B' : '.');
            }
    return b;
}

Blueprint makeStraightStairs() {
    // 5 x 11 x 8: спуск на 6 к -Z, ступени из булыжника
    Blueprint b;
    const int D = 8, H = 11;
    for (int r = 0; r < D; ++r) {
        for (int l = 0; l < H; ++l) {
            for (int c = 0; c < 5; ++c) {
                if (c == 0 || c == 4) {
                    b.set(l, r, c, 'B');
                    continue;
                }
                char ch = 'B';
                if (r == 0) {
                    if (l == 0) ch = 'B';
                    else if (l >= 1 && l <= 3) ch = '.';
                    else ch = 'B';
                } else if (r >= 1 && r <= 6) {
                    if (l < r) ch = 'B';
                    else if (l == r) ch = 'O';
                    else if (l >= r + 1 && l <= r + 3) ch = '.';
                    else ch = 'B';
                } else { // r == 7 (вход/верхняя площадка)
                    if (l < 7) ch = 'B';
                    else if (l >= 7 && l <= 9) ch = '.';
                    else ch = 'B';
                }
                b.set(l, r, c, ch);
            }
        }
    }
    return b;
}

Blueprint makeRoom(int variant) {
    // Большая комната 11 x 7 x 11 с проходами на все четыре стороны; внутри столб с факелами или фонтан
    Blueprint b;
    for (int l = 0; l < 7; ++l)
        for (int r = 0; r < 11; ++r)
            for (int c = 0; c < 11; ++c) {
                bool wall = l == 0 || l == 6 || r == 0 || r == 10 || c == 0 || c == 10;
                bool open = l >= 1 && l <= 3 && (((r == 0 || r == 10) && c >= 4 && c <= 6) || ((c == 0 || c == 10) && r >= 4 && r <= 6));
                b.set(l, r, c, wall && !open ? 'B' : '.');
            }
    if (variant == 0) {
        for (int l = 1; l <= 5; ++l) b.set(l, 5, 5, 'B');
        b.set(3, 4, 5, 'H'); b.set(3, 6, 5, 'H'); b.set(3, 5, 4, 'H'); b.set(3, 5, 6, 'H');
    } else {
        for (int r = 4; r <= 6; ++r)
            for (int c = 4; c <= 6; ++c) b.set(1, r, c, (r == 5 && c == 5) ? 'B' : 'I');
        for (int r = 3; r <= 7; ++r)
            for (int c = 3; c <= 7; ++c)
                if (r == 3 || r == 7 || c == 3 || c == 7) b.set(1, r, c, 'B');
        b.set(2, 5, 5, 'B'); b.set(3, 5, 5, 'B'); b.set(4, 5, 5, 'M');
    }
    return b;
}

struct Kit {
    std::map<std::string, Blueprint> bp;
    std::vector<PieceDef> defs;
};

const Kit& kit() {
    static Kit k = [] {
        Kit K;
        K.bp = parse(STRONGHOLD_BLUEPRINTS);
        K.bp["TurnLeft"] = makeTurn(true);
        K.bp["TurnRight"] = makeTurn(false);
        K.bp["Stairs"] = makeStraightStairs();
        K.bp["Room0"] = makeRoom(0);
        K.bp["Room1"] = makeRoom(1);
        // Веса и лимиты — как StructureStrongholdPieces 1.0
        K.defs = {
            {"Corridor", 40, 0, 6, 2, 1, true, {{3, 0, 2, 1, false}, {2, 3, 0, 1, true}, {0, 3, 4, 1, true}}},
            {"Jail_Cell", 5, 5, 10, 6, 1, true, {{3, 0, 6, 1, false}}},
            {"TurnLeft", 20, 0, 4, 2, 1, true, {{2, 2, 0, 1, false}}},
            {"TurnRight", 20, 0, 4, 2, 1, true, {{0, 2, 4, 1, false}}},
            {"Room0", 5, 3, 10, 5, 1, true, {{3, 0, 5, 1, false}, {2, 5, 0, 1, false}, {0, 5, 10, 1, false}}},
            {"Room1", 5, 3, 10, 5, 1, true, {{3, 0, 5, 1, false}, {2, 5, 0, 1, false}, {0, 5, 10, 1, false}}},
            {"Stairs", 5, 5, 7, 2, 7, true, {{3, 0, 2, 1, false}}},
            {"Five_way_Crossing", 5, 4, 10, 4, 3, true,
             {{2, 8, 0, 3, false}, {0, 8, 9, 3, false}, {3, 0, 3, 1, false}, {2, 2, 0, 5, true}, {0, 2, 9, 5, true}}},
            {"Loot_Altar", 5, 4, 6, 2, 1, true, {{3, 0, 2, 1, false}}},
            {"Library", 10, 2, 14, 10, 1, false, {}},
            {"Portal_Room", 20, 1, 15, 5, 1, false, {}},
        };
        return K;
    }();
    return k;
}

struct SPiece {
    int def;
    Placement p;
    std::vector<bool> exitUsed;
    int door = 0; // 0 проём, 1 деревянная дверь, 2 решётка, 3 железная дверь с кнопками
};

struct StrongholdLayout { std::vector<SPiece> pieces; };

StrongholdLayout buildStronghold(int X, int Z, uint32_t seed) {
    const Kit& K = kit();
    for (int genTry = 0; genTry < 10; ++genTry) {
        StrongholdLayout L;
        uint32_t s = (seed + (uint32_t)genTry * 0x9E3779B9u) | 1u;
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
        // Старт — лестница вниз от центра
        {
            int d = 6; // Stairs
            const Blueprint& b = bpOf(d);
            Placement p = placeAt(b, 0, K.defs[d].ec, K.defs[d].er, K.defs[d].el, X, 0, Z);
            L.pieces.push_back({d, p, std::vector<bool>(K.defs[d].exits.size(), false), 0});
            ++count[d];
        }
        struct Open { int piece, exit, depth; };
        std::vector<Open> open = {{0, 0, 0}};
        bool portal = false;
        auto tryAttach = [&](const Open& o, bool forcePortal) -> bool {
            const SPiece& src = L.pieces[o.piece];
            const PieceDef& sd = K.defs[src.def];
            const Exit& ex = sd.exits[o.exit];
            const Blueprint& sb = bpOf(src.def);
            int ex0, ez0;
            src.p.toWorld(sb, ex.c, ex.r, ex0, ez0);
            int wdir = src.p.rotDir(ex.dir);
            int wx = ex0 + DX[wdir], wz = ez0 + DZ[wdir], wy = src.p.y0 + ex.layer;
            for (int attempt = 0; attempt < 5; ++attempt) {
                int d = -1;
                if (forcePortal) d = 10;
                else {
                    int total = 0;
                    for (int i = 0; i < (int)K.defs.size(); ++i) {
                        if (i == 10) {
                            if (portal || L.pieces.size() < 8) continue;
                            total += (L.pieces.size() >= 12 ? 80 : K.defs[i].weight);
                            continue;
                        }
                        if (K.defs[i].limit && count[i] >= K.defs[i].limit) continue;
                        total += K.defs[i].weight;
                    }
                    int v = rr(s, 0, total - 1);
                    for (int i = 0; i < (int)K.defs.size(); ++i) {
                        if (i == 10 && (portal || L.pieces.size() < 8)) continue;
                        if (K.defs[i].limit && count[i] >= K.defs[i].limit) continue;
                        int w = (i == 10 && L.pieces.size() >= 12) ? 80 : K.defs[i].weight;
                        if (v < w) { d = i; break; }
                        v -= w;
                    }
                }
                if (d < 0) return false;
                const Blueprint& b = bpOf(d);
                const PieceDef& pd = K.defs[d];
                Placement p = placeAt(b, rotForForward(wdir), pd.ec, pd.er, pd.el, wx, wy, wz);
                if (!boxFree(p, b.h())) {
                    if (forcePortal) return false;
                    continue;
                }
                SPiece np{d, p, std::vector<bool>(pd.exits.size(), false), rr(s, 0, 3)};
                L.pieces.push_back(np);
                ++count[d];
                if (d == 10) portal = true;
                L.pieces[o.piece].exitUsed[o.exit] = true;
                int idx = (int)L.pieces.size() - 1;
                for (int e = 0; e < (int)pd.exits.size(); ++e)
                    if (!pd.exits[e].optional || rr(s, 0, 1) == 0) open.push_back({idx, e, o.depth + 1});
                return true;
            }
            return false;
        };
        size_t head = 0;
        while (head < open.size() && L.pieces.size() < 60) {
            Open o = open[head++];
            if (o.depth > 25) continue;
            tryAttach(o, false);
        }
        // Комната с порталом обязательна: пробуем приставить к любому свободному выходу
        for (size_t i = 0; i < open.size() && !portal; ++i)
            if (!L.pieces[open[i].piece].exitUsed[open[i].exit]) tryAttach(open[i], true);

        // Если напрямую не поместилась — пробуем через прямой коридор в породу
        if (!portal) {
            for (size_t i = 0; i < open.size() && !portal; ++i) {
                if (L.pieces[open[i].piece].exitUsed[open[i].exit]) continue;
                const SPiece& src = L.pieces[open[i].piece];
                const Exit& ex = K.defs[src.def].exits[open[i].exit];
                const Blueprint& sb = bpOf(src.def);
                int ex0, ez0;
                src.p.toWorld(sb, ex.c, ex.r, ex0, ez0);
                int wdir = src.p.rotDir(ex.dir);
                int wx = ex0 + DX[wdir], wz = ez0 + DZ[wdir], wy = src.p.y0 + ex.layer;
                const Blueprint& cb = bpOf(0);
                Placement cp = placeAt(cb, rotForForward(wdir), K.defs[0].ec, K.defs[0].er, K.defs[0].el, wx, wy, wz);
                if (!boxFree(cp, cb.h())) continue;
                SPiece ncp{0, cp, std::vector<bool>(K.defs[0].exits.size(), false), 0};
                L.pieces.push_back(ncp);
                L.pieces[open[i].piece].exitUsed[open[i].exit] = true;
                int cIdx = (int)L.pieces.size() - 1;
                Open co{cIdx, 0, open[i].depth + 1};
                if (tryAttach(co, true)) {
                    portal = true;
                    break;
                }
                L.pieces.pop_back();
                L.pieces[open[i].piece].exitUsed[open[i].exit] = false;
            }
        }

        // Если без портала и есть ещё попытки генерации — пробуем снова с новым сидом
        if (!portal && genTry < 9) continue;

        // Высота: гарантированно под землёй и ниже уровня моря, чтобы не пробивать дно океана (SEA=63)
        int minY = 1 << 30, maxY = -(1 << 30);
        for (auto& q : L.pieces) { minY = std::min(minY, q.p.y0); maxY = std::max(maxY, q.p.y0 + bpOf(q.def).h()); }
        int targetMaxY = 44; // Верх крепости на Y=44 (уровень моря 63, дно океана ~48-52)
        int shift = targetMaxY - maxY;
        if (minY + shift < 10) shift = 10 - minY;
        for (auto& q : L.pieces) q.p.y0 += shift;
        return L;
    }
    return {};
}

struct LootE { uint16_t id; int dmg, lo, hi, w; };
void fillChest(TileEntity& te, uint32_t seed, const LootE* t, int n, int cntLo, int cntHi) {
    uint32_t s = seed | 1u;
    int total = 0;
    for (int i = 0; i < n; ++i) total += t[i].w;
    int k = rr(s, cntLo, cntHi);
    for (int i = 0; i < k; ++i) {
        int v = rr(s, 0, total - 1);
        for (int j = 0; j < n; ++j) {
            if (v < t[j].w) { te.items[rr(s, 0, 26)] = makeStack(t[j].id, rr(s, t[j].lo, t[j].hi), (uint16_t)t[j].dmg); break; }
            v -= t[j].w;
        }
    }
}
// Сундуки крепости 1.0: в коридоре (алтарь), в комнате, в библиотеке
const LootE LOOT_CORRIDOR[] = {{ENDER_PEARL, 0, 1, 1, 10}, {DIAMOND, 0, 1, 3, 3}, {IRON_INGOT, 0, 1, 5, 10}, {GOLD_INGOT, 0, 1, 3, 5},
                               {REDSTONE, 0, 4, 9, 5}, {BREAD, 0, 1, 3, 15}, {APPLE, 0, 1, 3, 15}, {IRON_PICKAXE, 0, 1, 1, 5},
                               {IRON_SWORD, 0, 1, 1, 5}, {IRON_CHESTPLATE, 0, 1, 1, 5}, {IRON_HELMET, 0, 1, 1, 5},
                               {IRON_LEGGINGS, 0, 1, 1, 5}, {IRON_BOOTS, 0, 1, 1, 5}, {GOLDEN_APPLE, 0, 1, 1, 1}};
const LootE LOOT_ROOM[] = {{IRON_INGOT, 0, 1, 5, 10}, {GOLD_INGOT, 0, 1, 3, 5}, {REDSTONE, 0, 4, 9, 5}, {COAL, 0, 3, 8, 10},
                           {BREAD, 0, 1, 3, 15}, {APPLE, 0, 1, 3, 15}, {IRON_PICKAXE, 0, 1, 1, 1}};
const LootE LOOT_LIBRARY[] = {{BOOK, 0, 1, 3, 20}, {PAPER, 0, 2, 7, 20}, {MAP, 0, 1, 1, 1}, {COMPASS, 0, 1, 1, 1}};

} // namespace

const void* World::strongholdLayout10(int X, int Z) const {
    static thread_local std::map<std::pair<uint32_t, int64_t>, StrongholdLayout> cache;
    auto key = std::make_pair(seed_, chunkKey(X, Z));
    auto it = cache.find(key);
    if (it == cache.end()) {
        if (cache.size() > 16) cache.clear();
        it = cache.emplace(key, buildStronghold(X, Z, hash3i(X, 4321, Z, seed_ ^ 0x57A0u))).first;
    }
    return &it->second;
}

void World::placeStronghold10(Chunk& c) {
    const int bx = c.cx * CW, bz = c.cz * CW;
    const Kit& K = kit();
    for (const glm::ivec3& S : strongholdCenters()) {
        if (bx + CW < S.x - 130 || bx > S.x + 130 || bz + CW < S.z - 130 || bz > S.z + 130) continue;
        const StrongholdLayout& L = *static_cast<const StrongholdLayout*>(strongholdLayout10(S.x, S.z));
        auto in = [&](int x, int z) { return x >= bx && x < bx + CW && z >= bz && z < bz + CW; };
        auto setW = [&](int x, int y, int z, uint8_t b, uint8_t m = 0) {
            if (in(x, z) && y > 0 && y < CH - 1) c.set(x - bx, y, z - bz, b, m);
        };
        // Кирпич крепости (StructureStrongholdStones): треснутый 20%, мшистый 30%, с чешуйницей 5%, иначе обычный
        auto brick = [&](int x, int y, int z) {
            float h = hashf(x, y, z, seed_ ^ 0xB71Cu);
            if (h < 0.2f) setW(x, y, z, STONE_BRICK, 2);
            else if (h < 0.5f) setW(x, y, z, STONE_BRICK, 1);
            else if (h < 0.55f) setW(x, y, z, MONSTER_EGG, 2);
            else setW(x, y, z, STONE_BRICK, 0);
        };
        for (const SPiece& sp : L.pieces) {
            const Placement& p = sp.p;
            const Blueprint& b = K.bp.at(K.defs[sp.def].name);
            if (p.x0 > bx + CW - 1 || p.x0 + p.w - 1 < bx || p.z0 > bz + CW - 1 || p.z0 + p.d - 1 < bz) continue;
            const PieceDef& pd = K.defs[sp.def];
            // Все неиспользованные проходы заделываем каменным кирпичом
            Blueprint bb = b;
            for (int e = 0; e < (int)pd.exits.size(); ++e) {
                const Exit& ex = pd.exits[e];
                if (sp.exitUsed[e]) continue;
                for (int k = -1; k <= 1; ++k)
                    for (int l = ex.layer; l < ex.layer + 3; ++l) {
                        int r = ex.r + (ex.dir == 1 || ex.dir == 3 ? 0 : k), cc = ex.c + (ex.dir == 0 || ex.dir == 2 ? 0 : k);
                        bb.set(l, r, cc, 'B');
                    }
            }
            // Дверь на входе (у коридорных кусков): проём, деревянная, решётка или железная с кнопками
            if (pd.door) {
                int r = pd.er, c0 = pd.ec, l = pd.el;
                switch (sp.door) {
                case 1: bb.set(l, r, c0 - 1, 'B'); bb.set(l, r, c0 + 1, 'B'); bb.set(l, r, c0, 'C');
                        bb.set(l + 1, r, c0 - 1, 'B'); bb.set(l + 1, r, c0 + 1, 'B'); bb.set(l + 1, r, c0, 'D');
                        bb.set(l + 2, r, c0 - 1, 'B'); bb.set(l + 2, r, c0, 'B'); bb.set(l + 2, r, c0 + 1, 'B'); break;
                case 2: bb.set(l, r, c0 - 1, 'A');     bb.set(l, r, c0, '.');     bb.set(l, r, c0 + 1, 'A');
                        bb.set(l + 1, r, c0 - 1, 'A'); bb.set(l + 1, r, c0, '.'); bb.set(l + 1, r, c0 + 1, 'A');
                        bb.set(l + 2, r, c0 - 1, 'A'); bb.set(l + 2, r, c0, 'A'); bb.set(l + 2, r, c0 + 1, 'A'); break;
                case 3: bb.set(l, r, c0 - 1, 'B'); bb.set(l, r, c0 + 1, 'B'); bb.set(l, r, c0, 'E');
                        bb.set(l + 1, r, c0 - 1, 'B'); bb.set(l + 1, r, c0 + 1, 'B'); bb.set(l + 1, r, c0, 'G');
                        bb.set(l + 2, r, c0 - 1, 'B'); bb.set(l + 2, r, c0, 'B'); bb.set(l + 2, r, c0 + 1, 'B'); break;
                default: break;
                }
            }
            const int nl = bb.h();
            auto solid = [&](int l, int r, int cc) { char ch = bb.at(l, r, cc); return ch == 'B' || ch == 'J' || ch == 'L' || ch == 'R' || ch == 'P'; };
            for (int l = 0; l < nl; ++l)
                for (int r = 0; r < bb.d; ++r)
                    for (int cc = 0; cc < bb.w; ++cc) {
                        char ch = bb.at(l, r, cc);
                        int x, z;
                        p.toWorld(bb, cc, r, x, z);
                        int y = p.y0 + l;
                        if (!in(x, z) || y <= 0 || y >= CH - 1) continue;
                        // Направление к стене (факел, лестница, кнопка) и лицом в пустоту (сундук)
                        auto wallDir = [&]() {
                            for (int k = 0; k < 4; ++k)
                                if (solid(l, r + DZ[k], cc + DX[k])) return p.rotDir(k);
                            return -1;
                        };
                        switch (ch) {
                        case '.': setW(x, y, z, AIR); break;
                        case 'B': brick(x, y, z); break;
                        case 'A': setW(x, y, z, IRON_BARS); break;
                        case 'J': setW(x, y, z, COBBLE); break;
                        case 'L': setW(x, y, z, PLANKS); break;
                        case 'M': setW(x, y, z, WATER, 0); break;
                        case 'T': setW(x, y, z, LAVA, 0); break;
                        case 'I': setW(x, y, z, SLAB, 0); break;
                        case 'P': setW(x, y, z, DOUBLE_SLAB, 0); break;
                        case 'Q': setW(x, y, z, SLAB, 5); break;
                        case 'R': setW(x, y, z, BOOKSHELF); break;
                        case 'S': setW(x, y, z, FENCE); break;
                        case 'H': case 'F': {
                            static const uint8_t TM[4] = {TORCH_EAST_WALL, TORCH_SOUTH_WALL, TORCH_WEST_WALL, TORCH_NORTH_WALL};
                            int k = wallDir();
                            if (ch == 'H') setW(x, y, z, TORCH, k < 0 ? (uint8_t)TORCH_FLOOR : TM[k]);
                            else if (k >= 0) setW(x, y, z, STONE_BUTTON, TM[k]);
                            else setW(x, y, z, AIR);
                            break;
                        }
                        case 'K': setW(x, y, z, LADDER, (uint8_t)std::max(0, wallDir())); break;
                        case 'O': case 'U': {
                            // Ступени поднимаются к сплошному соседу (Stairs к +Z, Portal_Room к -Z)
                            int k = -1;
                            if (sp.def == 6) k = 1;
                            else if (sp.def == 10) k = 3;
                            else {
                                for (int q = 0; q < 4 && k < 0; ++q)
                                    if (solid(l, r + DZ[q], cc + DX[q]) || solid(l + 1, r + DZ[q], cc + DX[q])) k = q;
                            }
                            if (k < 0) k = 1;
                            static const uint8_t SM[4] = {0, 2, 1, 3};
                            setW(x, y, z, ch == 'O' ? COBBLE_STAIRS : STONEBRICK_STAIRS, SM[p.rotDir(k)]);
                            break;
                        }
                        case 'C': case 'D': case 'E': case 'G': {
                            // Дверь: «внутрь» — к -Z чертежа (двери стоят на входе и в стенах камер вдоль Z)
                            int k = 3;
                            bool wallAlongX = solid(l, r, cc - 1) || solid(l, r, cc + 1);
                            if (!wallAlongX) k = cc < bb.w / 2 ? 0 : 2;
                            bool top = ch == 'D' || ch == 'G';
                            uint8_t f = (uint8_t)p.rotDir(k);
                            setW(x, y, z, (ch == 'C' || ch == 'D') ? WOOD_DOOR : IRON_DOOR, top ? (uint8_t)(f | 8) : f);
                            break;
                        }
                        case 'N': {
                            int k = 1;
                            for (int q = 0; q < 4; ++q)
                                if (bb.at(l, r + DZ[q], cc + DX[q]) == '.') { k = q; break; }
                            static const uint8_t FM[4] = {5, 3, 4, 2};
                            setW(x, y, z, CHEST, FM[p.rotDir(k)]);
                            if (!tileAt(x, y, z)) {
                                TileEntity& te = createTile(x, y, z, TileEntity::Chest);
                                uint32_t cs = hash3i(x, y, z, seed_ ^ 0x5C4E57u);
                                if (sp.def == 9) fillChest(te, cs, LOOT_LIBRARY, 4, 1, 4);
                                else if (sp.def == 4 || sp.def == 5) fillChest(te, cs, LOOT_ROOM, 7, 2, 4);
                                else fillChest(te, cs, LOOT_CORRIDOR, 14, 2, 4);
                            }
                            break;
                        }
                        case 'V': {
                            // Рамка смотрит к середине кольца (середина — строка 5, столбец 5); око — с шансом 10%
                            int dr = 5 - r, dc = 5 - cc;
                            int k = std::abs(dr) > std::abs(dc) ? (dr > 0 ? 1 : 3) : (dc > 0 ? 0 : 2);
                            uint8_t eye = hashf(x, y, z, seed_ ^ 0xE7Eu) < 0.1f ? 4 : 0;
                            setW(x, y, z, END_PORTAL_FRAME, (uint8_t)(p.rotDir(k) | eye));
                            break;
                        }
                        case 'W':
                            setW(x, y, z, MOB_SPAWNER, 5);
                            newSpawners.push_back({glm::ivec3(x, y, z), 5});
                            break;
                        default: break;
                        }
                    }
        }
    }
}

bool World::locateStronghold10(const glm::vec3& from, glm::ivec3& out) const {
    double best = 1e30;
    bool found = false;
    for (const glm::ivec3& S : strongholdCenters()) {
        const StrongholdLayout& L = *static_cast<const StrongholdLayout*>(strongholdLayout10(S.x, S.z));
        glm::ivec3 at(S.x, L.pieces.empty() ? 30 : L.pieces[0].p.y0, S.z);
        for (auto& q : L.pieces)
            if (q.def == 10) at = glm::ivec3(q.p.x0 + q.p.w / 2, q.p.y0 + 1, q.p.z0 + q.p.d / 2); // комната с порталом
        double d = (at.x - from.x) * (at.x - from.x) + (at.z - from.z) * (at.z - from.z);
        if (d < best) { best = d; out = at; found = true; }
    }
    return found;
}
