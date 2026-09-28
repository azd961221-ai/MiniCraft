// Генерация мира: биомы 1.0, рельеф, пещеры, руды, растительность, данжи, заброшенные шахты.
#include <algorithm>
#include <cmath>
#include "World.h"

namespace {

// Детерминированный ГСЧ для генерации (одинаковый результат для одного сида и чанка)
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 1) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    int range(int lo, int hi) { return hi <= lo ? lo : lo + (int)(next() % (uint32_t)(hi - lo + 1)); }
    float f() { return (next() & 0xFFFFFF) / float(0xFFFFFF); }
};

float smooth(float e0, float e1, float x) {
    float t = std::clamp((x - e0) / (e1 - e0), 0.f, 1.f);
    return t * t * (3 - 2 * t);
}

// Жила руды — случайное блуждание внутри чанка, заменяет только камень
void placeVein(Chunk& c, Rng& r, uint8_t ore, int size, int minY, int maxY) {
    int x = r.range(0, CW - 1), y = r.range(minY, maxY), z = r.range(0, CW - 1);
    for (int i = 0; i < size; ++i) {
        if (x >= 0 && x < CW && z >= 0 && z < CW && y > 0 && y < CH && c.get(x, y, z) == STONE) c.set(x, y, z, ore);
        switch (r.next() % 6) {
        case 0: ++x; break; case 1: --x; break;
        case 2: ++y; break; case 3: --y; break;
        case 4: ++z; break; default: --z; break;
        }
    }
}

// Сундуки: добыча из данжей и шахт (наборы 1.0 без отсутствующих предметов)
void fillDungeonChest(TileEntity& te, Rng& r) {
    for (int i = 0; i < 8; ++i) {
        ItemStack s;
        switch (r.range(0, 10)) { // rand(11), как в 1.0
        case 1: s = makeStack(IRON_INGOT, r.range(1, 4)); break;
        case 2: s = makeStack(BREAD); break;
        case 3: s = makeStack(WHEAT_ITEM, r.range(1, 4)); break;
        case 4: s = makeStack(GUNPOWDER, r.range(1, 4)); break;
        case 5: s = makeStack(STRING, r.range(1, 4)); break;
        case 6: s = makeStack(BUCKET); break;
        case 7: if (r.range(0, 99) == 0) s = makeStack(GOLDEN_APPLE); break;
        case 8: if (r.range(0, 1) == 0) s = makeStack(REDSTONE, r.range(1, 4)); break;
        case 9: if (r.range(0, 9) == 0) s = makeStack(r.range(0, 1) ? RECORD_CAT : RECORD_13); break;
        case 10: s = makeStack(DYE, 1, DYE_COCOA); break;
        case 0: s = makeStack(SADDLE); break;
        default: break;
        }
        if (!s.empty()) te.items[r.range(0, 26)] = s;
    }
}

void fillMineshaftChest(TileEntity& te, Rng& r) {
    int n = r.range(3, 6);
    for (int i = 0; i < n; ++i) {
        ItemStack s;
        int w = r.range(0, 36);
        if (w < 10) s = makeStack(IRON_INGOT, r.range(1, 5));
        else if (w < 15) s = makeStack(GOLD_INGOT, r.range(1, 3));
        else if (w < 25) s = makeStack(COAL, r.range(3, 8));
        else if (w < 30) s = makeStack(BREAD, r.range(1, 3));
        else if (w < 31) s = makeStack(IRON_PICKAXE);
        else if (w < 34) s = makeStack(DIAMOND, r.range(1, 2));
        else if (w < 35) s = makeStack(MELON_SEEDS, r.range(2, 4));
        else s = makeStack(RAIL, r.range(4, 8));
        te.items[r.range(0, 26)] = s;
    }
}

// ---- Заброшенная шахта: набор «кусков» с габаритами, строится от стартового чанка
struct MinePiece {
    enum Type { Room, Corridor, Crossing, Stairs } type;
    int x0, y0, z0, x1, y1, z1; // включительно
    int dir;                     // направление коридора/лестницы: 0 -Z, 1 +X, 2 +Z, 3 -X
    bool rails = false, webs = false;
    uint32_t seed = 0;
};

const int DX4[4] = {0, 1, 0, -1}, DZ4[4] = {-1, 0, 1, 0};

void mineCorridor(std::vector<MinePiece>& out, Rng& r, int x, int y, int z, int dir, int depth, int sx, int sz);

// Продолжение от конца куска: коридор, развилка, лестница вниз или тупик
void mineNext(std::vector<MinePiece>& out, Rng& r, int x, int y, int z, int dir, int depth, int sx, int sz) {
    if (depth > 8 || std::abs(x - sx) > 72 || std::abs(z - sz) > 72 || out.size() > 120) return;
    int roll = r.range(0, 99);
    if (roll < 70) {
        mineCorridor(out, r, x, y, z, dir, depth + 1, sx, sz);
    } else if (roll < 85) {
        // Развилка 5x5, выходы вперёд и в стороны
        MinePiece p{MinePiece::Crossing, x - 2, y, z - 2, x + 2, y + (r.range(0, 3) == 0 ? 6 : 2), z + 2, dir};
        p.x0 += DX4[dir] * 2; p.x1 += DX4[dir] * 2; p.z0 += DZ4[dir] * 2; p.z1 += DZ4[dir] * 2;
        out.push_back(p);
        int cx = (p.x0 + p.x1) / 2, cz = (p.z0 + p.z1) / 2;
        for (int d = 0; d < 4; ++d) {
            if (d == (dir + 2) % 4 || r.range(0, 3) == 0) continue;
            mineCorridor(out, r, cx + DX4[d] * 3, y, cz + DZ4[d] * 3, d, depth + 1, sx, sz);
        }
    } else if (roll < 95 && y > 12) {
        // Лестница вниз на 5 блоков
        int len = 8;
        int ex = x + DX4[dir] * (len - 1), ez = z + DZ4[dir] * (len - 1);
        MinePiece p{MinePiece::Stairs, std::min(x, ex) - (DX4[dir] == 0 ? 1 : 0), y - 5, std::min(z, ez) - (DZ4[dir] == 0 ? 1 : 0),
                    std::max(x, ex) + (DX4[dir] == 0 ? 1 : 0), y + 2, std::max(z, ez) + (DZ4[dir] == 0 ? 1 : 0), dir};
        out.push_back(p);
        mineNext(out, r, ex + DX4[dir], y - 5, ez + DZ4[dir], dir, depth + 1, sx, sz);
    }
}

void mineCorridor(std::vector<MinePiece>& out, Rng& r, int x, int y, int z, int dir, int depth, int sx, int sz) {
    int len = 5 * r.range(2, 4);
    int ex = x + DX4[dir] * (len - 1), ez = z + DZ4[dir] * (len - 1);
    MinePiece p{MinePiece::Corridor, std::min(x, ex), y, std::min(z, ez), std::max(x, ex), y + 2, std::max(z, ez), dir};
    if (DX4[dir] == 0) { p.x0 -= 1; p.x1 += 1; } else { p.z0 -= 1; p.z1 += 1; }
    p.rails = r.range(0, 2) != 0;
    p.webs = r.range(0, 22) == 0;
    p.seed = r.next();
    out.push_back(p);
    mineNext(out, r, ex + DX4[dir], y, ez + DZ4[dir], dir, depth, sx, sz);
}

std::vector<MinePiece> buildMineshaft(int cx, int cz, uint32_t seed) {
    Rng r(hash3i(cx, 77, cz, seed ^ 0x4D494E45U));
    std::vector<MinePiece> out;
    int x = cx * CW + r.range(0, 15), z = cz * CW + r.range(0, 15), y = r.range(18, 44);
    int w = r.range(3, 6), l = r.range(3, 6), h = r.range(3, 5);
    MinePiece room{MinePiece::Room, x - w, y, z - l, x + w, y + h, z + l, 0};
    out.push_back(room);
    // Коридоры из комнаты во все стороны
    for (int d = 0; d < 4; ++d) {
        int n = r.range(0, 2);
        for (int i = 0; i < n; ++i) {
            int ox = x, oz = z;
            if (DX4[d] != 0) { ox = d == 1 ? room.x1 + 1 : room.x0 - 1; oz = r.range(room.z0 + 1, room.z1 - 1); }
            else { oz = d == 2 ? room.z1 + 1 : room.z0 - 1; ox = r.range(room.x0 + 1, room.x1 - 1); }
            mineCorridor(out, r, ox, y + 1, oz, d, 0, x, z);
        }
    }
    return out;
}

bool isMineStart(int cx, int cz, uint32_t seed) {
    return hash3i(cx, 99, cz, seed ^ 0x53484146U) % 100 == 0;
}

} // namespace

// ---------------------------------------------------------------- Биомы и рельеф

Biome World::biomeAt(int x, int z) const {
    if (genVersion_ >= 2 && dimension_ == 0) return biomeAt10(x, z);
    float continent = noise_.fbm2(x * 0.0025f, z * 0.0025f, 4);
    float temp = std::clamp(0.5f + noise_.fbm2(x * 0.0015f + 913.f, z * 0.0015f - 377.f, 3) * 1.2f, 0.f, 1.f);
    float rain = std::clamp(0.5f + noise_.fbm2(x * 0.0015f - 511.f, z * 0.0015f + 733.f, 3) * 1.2f, 0.f, 1.f);
    if (continent < -0.18f) {
        float m = noise_.fbm2(x * 0.004f + 4000.f, z * 0.004f, 2);
        if (continent < -0.3f && m > 0.42f) return Biome::MushroomIsland;
        if (continent < -0.28f && m > 0.37f) return Biome::MushroomShore;
        return temp < 0.2f ? Biome::FrozenOcean : Biome::Ocean;
    }
    float river = std::abs(noise_.fbm2(x * 0.0035f + 77.f, z * 0.0035f - 91.f, 3));
    if (river < 0.018f) return temp < 0.2f ? Biome::FrozenRiver : Biome::River;
    if (continent < -0.13f && continent >= -0.18f) return Biome::Beach;
    float hills = noise_.fbm2(x * 0.003f - 1234.f, z * 0.003f + 4321.f, 2);
    if (temp > 0.65f && rain > 0.65f) return hills > 0.2f ? Biome::JungleHills : Biome::Jungle;
    if (temp < 0.2f) return hills > 0.2f ? Biome::IceMountains : Biome::IcePlains;
    if (temp < 0.36f) return hills > 0.2f ? Biome::TaigaHills : Biome::Taiga;
    if (temp > 0.68f && rain < 0.42f) return hills > 0.2f ? Biome::DesertHills : Biome::Desert;
    if (rain > 0.66f && temp > 0.45f) return Biome::Swampland;
    if (hills > 0.26f) return Biome::ExtremeHills;
    if (hills > 0.18f && hills <= 0.26f) return Biome::ExtremeHillsEdge;
    if (rain > 0.5f) return hills > 0.2f ? Biome::ForestHills : Biome::Forest;
    return Biome::Plains;
}

Biome World::loadedBiome(int x, int z) const {
    Chunk* c = chunkAt(floorDiv(x, CW), floorDiv(z, CW));
    if (!c) return biomeAt(x, z);
    return (Biome)c->biome[(z - c->cz * CW) * CW + (x - c->cx * CW)];
}

// Форма рельефа в точке: среднее параметров биомов по окрестности 5x5 с шагом 4 (плавные переходы).
// Реки не участвуют — их русло прорезается отдельно.
World::ColumnShape World::biomeShape(int x, int z) const {
    ColumnShape s{0.f, 0.f};
    for (int i = -2; i <= 2; ++i)
        for (int j = -2; j <= 2; ++j) {
            Biome b = biomeAt(x + i * 4, z + j * 4);
            if (b == Biome::River) b = Biome::Plains;
            if (b == Biome::FrozenRiver) b = Biome::IcePlains;
            const BiomeInfo& bi = biomeInfo(b);
            s.base += bi.base;
            s.variation += bi.variation;
        }
    s.base /= 25.f;
    s.variation /= 25.f;
    return s;
}

static int heightFromShape(const Perlin& n, float base, float variation, int x, int z) {
    float detail = n.fbm2(x * 0.012f + 100.f, z * 0.012f + 100.f, 5);
    float ridge = std::max(0.f, n.fbm2(x * 0.005f - 300.f, z * 0.005f - 300.f, 3));
    float h = SEA + 1 + base * 17.f + variation * (detail * 22.f + ridge * ridge * 40.f);
    // Русло реки
    float river = std::abs(n.fbm2(x * 0.0035f + 77.f, z * 0.0035f - 91.f, 3));
    if (base > -0.6f && h > SEA - 3) h = SEA - 3 + (h - (SEA - 3)) * smooth(0.012f, 0.05f, river);
    return std::clamp((int)h, 4, CH - 8);
}

int World::terrainHeight(int x, int z) const {
    if (genVersion_ >= 2 && dimension_ == 0) return terrainHeight10(x, z);
    ColumnShape s = biomeShape(x, z);
    return heightFromShape(noise_, s.base, s.variation, x, z);
}

int World::topBlockY(int x, int z) const {
    Chunk* c = chunkAt(floorDiv(x, CW), floorDiv(z, CW));
    if (!c) return terrainHeight(x, z);
    return c->height[(z - c->cz * CW) * CW + (x - c->cx * CW)];
}

// ---------------------------------------------------------------- Генерация чанка

void World::generateOverworld(Chunk& c) {
    const int bx = c.cx * CW, bz = c.cz * CW;

    // Биомы колонок
    for (int z = 0; z < CW; ++z)
        for (int x = 0; x < CW; ++x) c.biome[z * CW + x] = (uint8_t)biomeAt(bx + x, bz + z);

    // Форма рельефа: считаем в узлах сетки с шагом 4 и интерполируем
    ColumnShape grid[5][5];
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 5; ++j) grid[i][j] = biomeShape(bx + i * 4, bz + j * 4);
    int heights[CW][CW];
    for (int z = 0; z < CW; ++z)
        for (int x = 0; x < CW; ++x) {
            int gi = x / 4, gj = z / 4;
            float fx = (x % 4) / 4.f, fz = (z % 4) / 4.f;
            auto lerp2 = [&](float ColumnShape::*f) {
                float a = grid[gi][gj].*f + (grid[gi + 1][gj].*f - grid[gi][gj].*f) * fx;
                float b = grid[gi][gj + 1].*f + (grid[gi + 1][gj + 1].*f - grid[gi][gj + 1].*f) * fx;
                return a + (b - a) * fz;
            };
            heights[x][z] = heightFromShape(noise_, lerp2(&ColumnShape::base), lerp2(&ColumnShape::variation), bx + x, bz + z);
        }

    for (int z = 0; z < CW; ++z) {
        for (int x = 0; x < CW; ++x) {
            int wx = bx + x, wz = bz + z;
            int h = heights[x][z];
            Biome biome = (Biome)c.biome[z * CW + x];
            const BiomeInfo& bi = biomeInfo(biome);
            uint8_t top = bi.top, filler = bi.filler;
            bool beach = h >= SEA - 1 && h <= SEA + 1 && !bi.snowy && biome != Biome::MushroomIsland && biome != Biome::MushroomShore;
            if (beach) {
                bool gravel = noise_.noise(wx * 0.05f, 11.1f, wz * 0.05f) > 0.35f;
                top = filler = gravel ? GRAVEL : SAND;
            }
            if (h < SEA - 1) {
                // Дно: песок у берега, дальше гравий или песок, иногда глина
                bool gravelFloor = h < SEA - 4 && noise_.noise(wx * 0.06f, 7.7f, wz * 0.06f) > 0.15f;
                top = filler = gravelFloor ? GRAVEL : SAND;
                if (!gravelFloor && noise_.noise(wx * 0.08f, 3.1f, wz * 0.08f) > 0.45f) top = CLAY;
            }
            bool sandy = filler == SAND;

            for (int y = 0; y < CH; ++y) {
                uint8_t b = AIR;
                if (y == 0) b = BEDROCK;
                else if (y < 5 && hashf(wx, y, wz, seed_) < 0.5f) b = BEDROCK;
                else if (y < h - 3) b = (sandy && y >= h - 7) ? SANDSTONE : STONE;
                else if (y < h) b = filler;
                else if (y == h) b = top;
                else if (y <= SEA) b = (y == SEA && bi.snowy) ? ICE : WATER;

                // Пещеры-«спагетти»: пересечение двух шумовых поверхностей
                if (b != AIR && b != WATER && b != ICE && b != BEDROCK && y > 4 && (h > SEA + 2 || y < h - 6)) {
                    float n1 = noise_.noise(wx * 0.045f, y * 0.075f, wz * 0.045f);
                    float n2 = noise_.noise(wx * 0.045f + 51.3f, y * 0.075f, wz * 0.045f + 51.3f);
                    // Ниже y=10 пещеры залиты лавой, как в 1.0
                    if (n1 * n1 + n2 * n2 < 0.0045f) b = y <= 10 ? LAVA : AIR;
                }
                c.set(x, y, z, b);
            }
        }
    }

    // Руды и гравий в камне
    Rng rng(hash3i(c.cx, 0, c.cz, seed_ ^ 0xA5A5A5A5U));
    for (int i = 0; i < 20; ++i) placeVein(c, rng, COAL_ORE, 12, 5, 127);
    for (int i = 0; i < 20; ++i) placeVein(c, rng, IRON_ORE, 7, 5, 63);
    for (int i = 0; i < 2; ++i) placeVein(c, rng, GOLD_ORE, 7, 5, 31);
    for (int i = 0; i < 1; ++i) placeVein(c, rng, DIAMOND_ORE, 6, 4, 15);
    for (int i = 0; i < 8; ++i) placeVein(c, rng, REDSTONE_ORE, 7, 5, 15);
    placeVein(c, rng, LAPIS_ORE, 6, rng.range(0, 15) + rng.range(0, 15) + 1, 0); // «треугольник» около y=16
    for (int i = 0; i < 8; ++i) placeVein(c, rng, GRAVEL, 24, 5, 127);
    for (int i = 0; i < 10; ++i) placeVein(c, rng, DIRT, 24, 5, 127);

    placeMineshafts(c);
    placeDungeons(c);
    decorate(c, heights);
    placeStronghold(c);
    placeVillage(c);
}

void World::generate(Chunk& c) {
    const int bx = c.cx * CW, bz = c.cz * CW;
    if (dimension_ == -1) generateNether(c);
    else if (dimension_ == 1) generateEnd(c);
    else if (genVersion_ >= 2) generateOverworld10(c);
    else generateOverworld(c);

    // Правки игрока
    auto e = edits_.find(chunkKey(c.cx, c.cz));
    if (e != edits_.end())
        for (auto& [idx, v] : e->second) {
            c.blocks[idx] = (uint8_t)(v & 0xFF);
            c.meta[idx] = (uint8_t)(v >> 8);
            if ((v & 0xFF) == ENCHANT_TABLE) // индекс = (y*CW + z)*CW + x
                enchantTables.insert(posKey(bx + idx % CW, idx / (CW * CW), bz + (idx / CW) % CW));
        }
    // Сундуки, которые игрок уже сломал, не должны оживать при повторной генерации
    for (auto it = tiles.begin(); it != tiles.end();) {
        const TileEntity& te = it->second;
        bool inChunk = floorDiv(te.x, CW) == c.cx && floorDiv(te.z, CW) == c.cz && te.y >= 0 && te.y < CH;
        uint8_t b = inChunk ? c.get(te.x - bx, te.y, te.z - bz) : AIR;
        if (inChunk && b != CHEST && b != FURNACE && b != FURNACE_LIT && b != DISPENSER && b != BREWING_STAND) it = tiles.erase(it);
        else ++it;
    }
}

// ---------------------------------------------------------------- Деревья

void World::treeInChunk(Chunk& c, int x, int y, int z, int type, uint32_t& seedState) {
    Rng r(seedState);
    seedState = r.next();
    auto inside = [](int a) { return a >= 0 && a < CW; };
    auto leaf = [&](int lx, int ly, int lz) {
        if (!inside(lx) || !inside(lz) || ly <= 0 || ly >= CH) return;
        uint8_t cur = c.get(lx, ly, lz);
        if (cur == AIR || cur == SNOW_LAYER || cur == TALL_GRASS) c.set(lx, ly, lz, LEAVES, (uint8_t)type);
    };
    if (type == 1) {
        // Ель: высокий ствол и конус хвои
        int trunk = r.range(6, 9);
        if (y + trunk + 2 >= CH) return;
        for (int dy = trunk + 1; dy >= 2; --dy) {
            // Сверху вниз: макушка, затем чередование ярусов 1 и 2 (ниже середины)
            int k = trunk + 1 - dy;
            int radius = k == 0 ? 0 : (k % 2 == 1 ? 1 : (k >= 4 ? 2 : 1));
            for (int dz = -radius; dz <= radius; ++dz)
                for (int dx = -radius; dx <= radius; ++dx) {
                    if (radius > 1 && std::abs(dx) == radius && std::abs(dz) == radius) continue;
                    leaf(x + dx, y + dy, z + dz);
                }
        }
        for (int dy = 0; dy < trunk; ++dy) c.set(x, y + dy, z, LOG, 1);
    } else {
        // Дуб и берёза: шар листвы 5x5 внизу и 3x3 сверху
        // (джунгли 1.4.2: WorldGenTrees(4 + rand(7)) — выше обычного дуба)
        int trunk = type == 2 ? r.range(5, 7) : type == 3 ? r.range(4, 10) : r.range(4, 6);
        if (y + trunk + 2 >= CH) return;
        for (int dy = trunk - 3; dy <= trunk; ++dy) {
            int rad = dy >= trunk - 1 ? 1 : 2;
            for (int dz = -rad; dz <= rad; ++dz)
                for (int dx = -rad; dx <= rad; ++dx) {
                    bool corner = std::abs(dx) == rad && std::abs(dz) == rad;
                    if (corner && (dy == trunk || r.range(0, 1) == 0)) continue;
                    leaf(x + dx, y + dy, z + dz);
                }
        }
        for (int dy = 0; dy < trunk; ++dy) c.set(x, y + dy, z, LOG, (uint8_t)type);
    }
    if (y > 0) c.set(x, y - 1, z, DIRT);
}

bool World::bigTreeInChunk(Chunk& c, int x, int y, int z, int heightLimit, uint32_t& seedState) {
    Rng r(seedState);
    seedState = r.next();
    const int leafDist = 4;
    if (y < 2 || y + heightLimit + 1 >= CH) return false;
    uint8_t ground = c.get(x, y - 1, z);
    if (ground != GRASS && ground != DIRT) return false;
    auto inside = [](int lx, int ly, int lz) { return lx >= 0 && lx < CW && lz >= 0 && lz < CW && ly > 0 && ly < CH; };
    // Отрезок свободен: только воздух и листва (checkBlockLine)
    auto clearLine = [&](glm::ivec3 a, glm::ivec3 b) {
        glm::ivec3 d = b - a;
        int n = std::max(std::abs(d.x), std::max(std::abs(d.y), std::abs(d.z)));
        for (int i = 0; i <= n; ++i) {
            glm::vec3 p = glm::vec3(a) + glm::vec3(d) * (n ? (float)i / n : 0.f);
            glm::ivec3 q((int)std::floor(p.x + 0.5f), (int)std::floor(p.y + 0.5f), (int)std::floor(p.z + 0.5f));
            if (!inside(q.x, q.y, q.z)) return false;
            uint8_t b0 = c.get(q.x, q.y, q.z);
            if (b0 != AIR && b0 != LEAVES && b0 != TALL_GRASS) return false;
        }
        return true;
    };
    auto logLine = [&](glm::ivec3 a, glm::ivec3 b) { // без последней клетки, как placeBlockLine
        glm::ivec3 d = b - a;
        int n = std::max(std::abs(d.x), std::max(std::abs(d.y), std::abs(d.z)));
        for (int i = 0; i < n; ++i) {
            glm::vec3 p = glm::vec3(a) + glm::vec3(d) * ((float)i / n);
            glm::ivec3 q((int)std::floor(p.x + 0.5f), (int)std::floor(p.y + 0.5f), (int)std::floor(p.z + 0.5f));
            if (inside(q.x, q.y, q.z)) c.set(q.x, q.y, q.z, LOG, 0);
        }
    };
    if (!clearLine(glm::ivec3(x, y, z), glm::ivec3(x, y + heightLimit - 1, z))) return false;
    int height = (int)(heightLimit * 0.618f);
    if (height >= heightLimit) height = heightLimit - 1;
    // Ширина яруса кроны (layerSize): ниже 0.3 высоты веток нет, дальше — «шар»
    auto layerSize = [&](int i) -> float {
        if (i < heightLimit * 0.3f) return -1.618f;
        float f = heightLimit / 2.f, f1 = heightLimit / 2.f - i, f2;
        if (f1 == 0.f) f2 = f;
        else if (std::abs(f1) >= f) f2 = 0.f;
        else f2 = std::sqrt(f * f - f1 * f1);
        return f2 * 0.5f;
    };
    struct Node { glm::ivec3 p; int baseY; };
    std::vector<Node> nodes;
    int perLayer = std::max(1, (int)(1.382 + std::pow(heightLimit / 13.0, 2.0)));
    int topY = y + heightLimit - leafDist, trunkTop = y + height;
    nodes.push_back({glm::ivec3(x, topY, z), trunkTop});
    for (int i1 = topY - 1 - y; i1 >= 0; --i1) {
        int ly = y + i1;
        float f = layerSize(i1);
        if (f < 0.f) continue;
        for (int k = 0; k < perLayer; ++k) {
            double d1 = f * (r.f() + 0.328), ang = r.f() * 2.0 * 3.14159265358979;
            glm::ivec3 node((int)std::floor(d1 * std::sin(ang) + x + 0.5), ly, (int)std::floor(d1 * std::cos(ang) + z + 0.5));
            if (!clearLine(node, node + glm::ivec3(0, leafDist, 0))) continue;
            double dist = std::sqrt((double)(x - node.x) * (x - node.x) + (double)(z - node.z) * (z - node.z));
            int baseY = node.y - dist * 0.381 > trunkTop ? trunkTop : (int)(node.y - dist * 0.381);
            if (clearLine(glm::ivec3(x, baseY, z), node)) nodes.push_back({node, baseY});
        }
    }
    // Листва: у каждого узла 4 диска радиусом 2, 3, 3, 2 (genTreeLayer)
    for (const Node& n : nodes)
        for (int k = 0; k < leafDist; ++k) {
            float rad = (k == 0 || k == leafDist - 1) ? 2.f : 3.f;
            int ir = (int)(rad + 0.618f), ly = n.p.y + k;
            for (int dx = -ir; dx <= ir; ++dx)
                for (int dz = -ir; dz <= ir; ++dz) {
                    if (std::sqrt(std::pow(std::abs(dx) + 0.5, 2.0) + std::pow(std::abs(dz) + 0.5, 2.0)) > rad) continue;
                    int lx = n.p.x + dx, lz = n.p.z + dz;
                    if (!inside(lx, ly, lz)) continue;
                    uint8_t cur = c.get(lx, ly, lz);
                    if (cur == AIR || cur == LEAVES || cur == TALL_GRASS || cur == SNOW_LAYER) c.set(lx, ly, lz, LEAVES, 0);
                }
        }
    // Ствол и ветки к узлам (ветки — только от узлов выше 0.2 высоты)
    logLine(glm::ivec3(x, y, z), glm::ivec3(x, y + height, z));
    for (const Node& n : nodes)
        if (n.baseY - y >= heightLimit * 0.2f) logLine(glm::ivec3(x, n.baseY, z), n.p);
    c.set(x, y - 1, z, DIRT);
    return true;
}

// ---------------------------------------------------------------- Растительность и снег

void World::decorate(Chunk& c, const int heights[CW][CW]) {
    const int bx = c.cx * CW, bz = c.cz * CW;
    Rng r(hash3i(c.cx, 5, c.cz, seed_ ^ 0x5EED5EEDU));
    Biome center = (Biome)c.biome[8 * CW + 8];
    const BiomeInfo& bi = biomeInfo(center);
    auto topY = [&](int x, int z) {
        int y = CH - 1;
        while (y > 0 && c.get(x, y, z) == AIR) --y;
        return y;
    };

    // Деревья
    int trees = bi.trees + (r.range(0, 9) == 0 ? 1 : 0);
    uint32_t treeSeed = r.next();
    for (int i = 0; i < trees; ++i) {
        int x = r.range(2, 13), z = r.range(2, 13);
        int y = heights[x][z];
        if (y < SEA) continue;
        uint8_t ground = c.get(x, y, z);
        if (ground != GRASS && ground != DIRT) continue;
        if (c.get(x, y + 1, z) != AIR) continue;
        int type = (center == Biome::Taiga || center == Biome::TaigaHills) ? 1
                 : isJungleBiome(center) ? 3
                 : ((center == Biome::Forest || center == Biome::ForestHills) && r.range(0, 4) == 0) ? 2 : 0;
        if (genVersion_ >= 3 && type == 0 && center != Biome::Swampland && !isJungleBiome(center) && r.range(0, 9) == 0) {
            // Каждый десятый дуб — большой; высота ограничена местом до края чанка (крона ~ высота/3 + 3)
            int room = std::min(std::min(x, z), std::min(CW - 1 - x, CW - 1 - z));
            int maxH = (int)((room - 3) * 4 / 1.328f);
            int hl = std::min(maxH, 5 + (int)(treeSeed % 12));
            if (hl >= 5 && bigTreeInChunk(c, x, y + 1, z, hl, treeSeed)) continue;
        }
        treeInChunk(c, x, y + 1, z, type, treeSeed);
        if (center == Biome::Swampland || isJungleBiome(center)) {
            // Болотные и джунглевые деревья обвиты лозой: свисает с краёв кроны
            for (int dz = -3; dz <= 3; ++dz)
                for (int dx = -3; dx <= 3; ++dx) {
                    int lx = x + dx, lz = z + dz;
                    if (lx < 1 || lx > 14 || lz < 1 || lz > 14) continue;
                    for (int ly = y + 7; ly > y + 2; --ly) {
                        if (c.get(lx, ly, lz) != AIR) continue;
                        uint8_t side = 0;
                        if (c.get(lx + 1, ly, lz) == LEAVES) side = 8;
                        else if (c.get(lx - 1, ly, lz) == LEAVES) side = 2;
                        else if (c.get(lx, ly, lz + 1) == LEAVES) side = 1;
                        else if (c.get(lx, ly, lz - 1) == LEAVES) side = 4;
                        if (!side || r.range(0, 3) != 0) continue;
                        for (int hy = ly; hy > ly - 4 && hy > 0 && c.get(lx, hy, lz) == AIR; --hy) c.set(lx, hy, lz, VINE, side);
                    }
                }
        }
        if (isJungleBiome(center) && c.get(x, y + 1, z) == LOG) {
            // Какао на стволе (WorldGenTrees 1.4.2 с лозой): у каждого пятого дерева выше 5 — на двух ярусах под кроной,
            // сторона с шансом 1/4 и 1/3, случайный возраст. Свой генератор, чтобы не сдвигать остальное наполнение
            int h = 0;
            while (y + 1 + h < CH && c.get(x, y + 1 + h, z) == LOG) ++h;
            Rng cr(treeSeed ^ 0xC0C0A5u);
            if (h > 5 && cr.range(0, 4) == 0) {
                static const int CD[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}}; // где стручок, если ствол на юге/западе/севере/востоке
                for (int t = 0; t < 2; ++t)
                    for (int s = 0; s < 4; ++s) {
                        if (cr.range(0, 3 - t) != 0) continue;
                        int age = cr.range(0, 2), px = x + CD[s][0], py = y + 1 + h - 5 + t, pz = z + CD[s][1];
                        if (c.get(px, py, pz) == AIR) c.set(px, py, pz, COCOA, (uint8_t)(s | (age << 2)));
                    }
            }
        }
    }
    // Кувшинки на воде в болотах
    if (center == Biome::Swampland)
        for (int i = 0; i < 4; ++i) {
            int x = r.range(0, 15), z = r.range(0, 15);
            if (c.get(x, SEA, z) == WATER && c.get(x, SEA + 1, z) == AIR) c.set(x, SEA + 1, z, LILY_PAD);
        }

    // Высокая трава и папоротник
    for (int z = 0; z < CW; ++z)
        for (int x = 0; x < CW; ++x) {
            const BiomeInfo& cb = biomeInfo((Biome)c.biome[z * CW + x]);
            if (cb.grassPercent == 0) continue;
            int y = heights[x][z];
            if (y + 1 >= CH || c.get(x, y, z) != GRASS || c.get(x, y + 1, z) != AIR) continue;
            if (r.range(0, 99) < cb.grassPercent) {
                bool fern = (Biome)c.biome[z * CW + x] == Biome::Taiga && r.range(0, 1) == 0;
                c.set(x, y + 1, z, TALL_GRASS, fern ? 2 : 1);
            }
        }

    // Цветы полянками
    for (int i = 0; i < bi.flowers; ++i) {
        int cx = r.range(2, 13), cz = r.range(2, 13);
        uint8_t flower = r.range(0, 2) == 0 ? ROSE : DANDELION;
        for (int k = 0; k < 8; ++k) {
            int x = std::clamp(cx + r.range(-3, 3), 0, CW - 1), z = std::clamp(cz + r.range(-3, 3), 0, CW - 1);
            int y = heights[x][z];
            if (y + 1 < CH && c.get(x, y, z) == GRASS && c.get(x, y + 1, z) == AIR) c.set(x, y + 1, z, flower);
        }
    }

    // Пустыня: мёртвые кусты и кактусы
    for (int i = 0; i < bi.deadBushes; ++i) {
        int x = r.range(0, 15), z = r.range(0, 15), y = heights[x][z];
        if (y + 1 < CH && c.get(x, y, z) == SAND && c.get(x, y + 1, z) == AIR) c.set(x, y + 1, z, DEAD_BUSH);
    }
    // Кактусы (WorldGenCactus 1.0): у попытки случайная высота 0..127, и только если она попала к поверхности (±4),
    // вокруг делается 10 проб; кактус растёт на песке, если по бокам пусто. Итого 1–2 кактуса на чанк, кучками
    for (int i = 0; i < bi.cactus; ++i) {
        int ax = r.range(1, 14), az = r.range(1, 14), ay = r.range(0, CH - 1);
        for (int k = 0; k < 10; ++k) {
            int x = ax + r.range(0, 7) - r.range(0, 7), z = az + r.range(0, 7) - r.range(0, 7);
            int y = ay + r.range(0, 3) - r.range(0, 3);
            if (x < 1 || x > 14 || z < 1 || z > 14 || y < 1 || y >= CH - 4) continue;
            if (c.get(x, y, z) != AIR || c.get(x, y - 1, z) != SAND) continue;
            int hgt = 1 + r.range(0, r.range(0, 2));
            for (int h = 0; h < hgt; ++h) {
                int yy = y + h;
                bool free = c.get(x, yy, z) == AIR && c.get(x + 1, yy, z) == AIR && c.get(x - 1, yy, z) == AIR &&
                            c.get(x, yy, z + 1) == AIR && c.get(x, yy, z - 1) == AIR;
                if (!free) break;
                c.set(x, yy, z, CACTUS);
            }
        }
    }

    // Тростник у воды
    for (int i = 0; i < bi.reeds + 2; ++i) {
        int x = r.range(1, 14), z = r.range(1, 14), y = heights[x][z];
        uint8_t g = c.get(x, y, z);
        if (y != SEA || (g != GRASS && g != DIRT && g != SAND) || c.get(x, y + 1, z) != AIR) continue;
        bool water = c.get(x + 1, y, z) == WATER || c.get(x - 1, y, z) == WATER || c.get(x, y, z + 1) == WATER || c.get(x, y, z - 1) == WATER;
        if (!water) continue;
        int hgt = r.range(2, 4);
        for (int k = 1; k <= hgt && y + k < CH && c.get(x, y + k, z) == AIR; ++k) c.set(x, y + k, z, REEDS);
    }

    // Тыквы: редкие грядки
    if (r.range(0, 31) == 0) {
        int cx = r.range(3, 12), cz = r.range(3, 12);
        for (int k = 0; k < 10; ++k) {
            int x = std::clamp(cx + r.range(-3, 3), 0, CW - 1), z = std::clamp(cz + r.range(-3, 3), 0, CW - 1);
            int y = heights[x][z];
            if (y + 1 < CH && c.get(x, y, z) == GRASS && c.get(x, y + 1, z) == AIR) c.set(x, y + 1, z, PUMPKIN, (uint8_t)r.range(2, 5));
        }
    }

    // Арбузы: грядки в джунглях (1.4.2)
    if (isJungleBiome(center) && r.range(0, 4) == 0) {
        int cx = r.range(3, 12), cz = r.range(3, 12);
        for (int k = 0; k < 12; ++k) {
            int x = std::clamp(cx + r.range(-3, 3), 0, CW - 1), z = std::clamp(cz + r.range(-3, 3), 0, CW - 1);
            int y = heights[x][z];
            if (y + 1 < CH && c.get(x, y, z) == GRASS && c.get(x, y + 1, z) == AIR) c.set(x, y + 1, z, MELON_BLOCK);
        }
    }

    // Грибы: в болотах и на грибных островах, иногда в пещерах
    for (int i = 0; i < bi.mushrooms; ++i) {
        int x = r.range(0, 15), z = r.range(0, 15), y = heights[x][z];
        uint8_t g = c.get(x, y, z);
        if (y + 1 < CH && (g == GRASS || g == MYCELIUM) && c.get(x, y + 1, z) == AIR)
            c.set(x, y + 1, z, r.range(0, 1) ? BROWN_MUSHROOM : RED_MUSHROOM);
    }
    if (r.range(0, 3) == 0) {
        for (int k = 0; k < 6; ++k) {
            int x = r.range(0, 15), z = r.range(0, 15), y = r.range(12, 55);
            if (c.get(x, y, z) == AIR && c.get(x, y - 1, z) == STONE) c.set(x, y, z, r.range(0, 3) ? BROWN_MUSHROOM : RED_MUSHROOM);
        }
    }

    // Снег в холодных биомах и на вершинах гор
    for (int z = 0; z < CW; ++z)
        for (int x = 0; x < CW; ++x) {
            Biome b = (Biome)c.biome[z * CW + x];
            int y = topY(x, z);
            bool cold = biomeInfo(b).snowy || (b == Biome::ExtremeHills && y > 98);
            if (!cold || y + 1 >= CH) continue;
            uint8_t t = c.get(x, y, z);
            if (t == WATER) { c.set(x, y, z, ICE); continue; }
            if ((isOpaque(t) || t == LEAVES) && c.get(x, y + 1, z) == AIR) c.set(x, y + 1, z, SNOW_LAYER);
            else if (t == TALL_GRASS) c.set(x, y, z, SNOW_LAYER);
        }
}

// ---------------------------------------------------------------- Данжи (WorldGenDungeons)

bool World::locateStructure(const std::string& kind, const glm::vec3& from, glm::ivec3& out) const {
    double best = 1e30;
    bool found = false;
    auto consider = [&](int x, int y, int z) {
        double dx = x - from.x, dz = z - from.z, d = dx * dx + dz * dz;
        if (d < best) { best = d; out = glm::ivec3(x, y, z); found = true; }
    };
    const int fcx = floorDiv((int)std::floor(from.x), CW), fcz = floorDiv((int)std::floor(from.z), CW);
    if (kind == "village" && dimension_ == 0 && genVersion_ >= 3) {
        return locateVillage10(from, out);
    } else if (kind == "village" && dimension_ == 0) {
        const int R = 24; // те же правила, что в placeVillage
        int rx0 = floorDiv(fcx, R), rz0 = floorDiv(fcz, R);
        for (int rx = rx0 - 4; rx <= rx0 + 4; ++rx)
            for (int rz = rz0 - 4; rz <= rz0 + 4; ++rz) {
                uint32_t h = hash3i(rx, 3, rz, seed_ ^ 0x71A6Eu);
                if (h % 2 != 0) continue;
                int ccx = rx * R + 4 + (int)((h >> 8) % (R - 8)), ccz = rz * R + 4 + (int)((h >> 16) % (R - 8));
                int X0 = ccx * CW + 8, Z0 = ccz * CW + 8;
                Biome vb = biomeAt(X0, Z0);
                if (vb != Biome::Plains && vb != Biome::Desert) continue;
                consider(X0, std::max(SEA, terrainHeight(X0, Z0)) + 1, Z0);
            }
    } else if (kind == "stronghold" && dimension_ == 0 && genVersion_ >= 3) {
        return locateStronghold10(from, out);
    } else if (kind == "stronghold" && dimension_ == 0) {
        for (const glm::ivec3& s : strongholdCenters()) consider(s.x, s.y, s.z);
    } else if (kind == "fortress" && dimension_ == -1 && genVersion_ >= 3) {
        return locateFortress10(from, out);
    } else if (kind == "fortress" && dimension_ == -1) {
        const int R = 12; // как в placeFortress
        int rx0 = floorDiv(fcx, R), rz0 = floorDiv(fcz, R);
        for (int rx = rx0 - 5; rx <= rx0 + 5; ++rx)
            for (int rz = rz0 - 5; rz <= rz0 + 5; ++rz) {
                uint32_t h = hash3i(rx, 7, rz, seed_ ^ 0xF0F7u);
                if (h % 3 == 2) continue;
                int ccx = rx * R + 4 + (int)((h >> 8) % 4), ccz = rz * R + 4 + (int)((h >> 12) % 4);
                consider(ccx * CW + 8, 64, ccz * CW + 8);
            }
    } else if (kind == "mineshaft" && dimension_ == 0) {
        for (int r = 0; r <= 64 && !found; ++r) // кольцами от игрока: первое найденное кольцо — ближайшее
            for (int sx = fcx - r; sx <= fcx + r; ++sx)
                for (int sz = fcz - r; sz <= fcz + r; ++sz) {
                    if (std::max(std::abs(sx - fcx), std::abs(sz - fcz)) != r) continue;
                    if (isMineStart(sx, sz, seed_)) consider(sx * CW + 8, 40, sz * CW + 8);
                }
    } else if (kind == "dungeon") {
        for (auto& [key, ch] : chunks)
            for (int i = 0; i < CW * CH * CW; ++i)
                if (ch->blocks[i] == MOB_SPAWNER) {
                    int x = i % CW, z = (i / CW) % CW, y = i / (CW * CW);
                    consider(ch->cx * CW + x, y, ch->cz * CW + z);
                }
    }
    return found;
}

void World::placeDungeons(Chunk& c) {
    const int bx = c.cx * CW, bz = c.cz * CW;
    Rng r(hash3i(c.cx, 13, c.cz, seed_ ^ 0xD0D0CAFEU));
    // 1.0: 8 попыток на чанк на любой высоте (миры генератора 3+; старые миры не меняем)
    const int attempts = genVersion_ >= 3 ? 8 : 3;
    for (int attempt = 0; attempt < attempts; ++attempt) {
        int x = r.range(4, 11), y = genVersion_ >= 3 ? r.range(5, CH - 8) : r.range(8, 60), z = r.range(4, 11);
        int rx = r.range(2, 3), rz = r.range(2, 3);
        // Не генерировать близко к поверхности (данж должен быть глубоко под землёй)
        if (genVersion_ >= 3) {
            int th = terrainHeight(bx + x, bz + z);
            if (y + 5 >= th - 2) continue;
        }
        // Пол и потолок сплошные, в стенах 1..5 проходов (так данж оказывается у пещеры).
        // Полный запрет жидкостей внутри, в полу, в потолке или прямо под полом.
        bool ok = true;
        int openings = 0;
        for (int dx = -rx - 1; dx <= rx + 1 && ok; ++dx)
            for (int dz = -rz - 1; dz <= rz + 1 && ok; ++dz) {
                // Под полом (y - 2) не должно быть лавы/воды
                if (isLiquid(c.get(x + dx, y - 2, z + dz))) { ok = false; break; }
                // Пол (y - 1) и потолок (y + 4) должны быть сплошными
                if (!isSolid(c.get(x + dx, y - 1, z + dz)) || !isSolid(c.get(x + dx, y + 4, z + dz))) { ok = false; break; }
                // Жидкостей не должно быть внутри комнаты, на полу или в потолке
                for (int dy = -1; dy <= 4; ++dy) {
                    if (isLiquid(c.get(x + dx, y + dy, z + dz))) { ok = false; break; }
                }
                if (!ok) break;
                bool wall = std::abs(dx) == rx + 1 || std::abs(dz) == rz + 1;
                if (wall && c.get(x + dx, y, z + dz) == AIR && c.get(x + dx, y + 1, z + dz) == AIR) ++openings;
            }
        if (!ok || openings < 1 || openings > 5) continue;

        for (int dx = -rx - 1; dx <= rx + 1; ++dx)
            for (int dy = -1; dy <= 4; ++dy)
                for (int dz = -rz - 1; dz <= rz + 1; ++dz) {
                    int px = x + dx, py = y + dy, pz = z + dz;
                    bool wall = std::abs(dx) == rx + 1 || std::abs(dz) == rz + 1 || dy == -1 || dy == 4;
                    if (!wall) c.set(px, py, pz, AIR);
                    else if (dy == -1) c.set(px, py, pz, r.range(0, 3) != 0 ? MOSSY_COBBLE : COBBLE);
                    else if (isSolid(c.get(px, py, pz))) c.set(px, py, pz, COBBLE);
                }
        // Два сундука у стен
        for (int k = 0; k < 2; ++k)
            for (int tries = 0; tries < 3; ++tries) {
                int px = x + r.range(-rx, rx), pz = z + r.range(-rz, rz);
                bool solidE = isSolid(c.get(px + 1, y, pz));
                bool solidW = isSolid(c.get(px - 1, y, pz));
                bool solidS = isSolid(c.get(px, y, pz + 1));
                bool solidN = isSolid(c.get(px, y, pz - 1));
                int walls = solidE + solidW + solidS + solidN;
                if (walls != 1 || c.get(px, y, pz) != AIR) continue;
                uint8_t meta = 2;
                if (solidS) meta = 2;      // стена на +Z -> смотрит на -Z (север)
                else if (solidN) meta = 3; // стена на -Z -> смотрит на +Z (юг)
                else if (solidE) meta = 4; // стена на +X -> смотрит на -X (запад)
                else if (solidW) meta = 5; // стена на -X -> смотрит на +X (восток)
                c.set(px, y, pz, CHEST, meta);
                if (!tileAt(bx + px, y, bz + pz)) {
                    TileEntity& te = createTile(bx + px, y, bz + pz, TileEntity::Chest);
                    fillDungeonChest(te, r);
                }
                break;
            }
        // Спавнер: скелет 1/4, зомби 1/2, паук 1/4 (метаданные: 0 зомби, 1 скелет, 2 паук)
        int roll = r.range(0, 3);
        uint8_t type = roll == 0 ? 1 : roll == 3 ? 2 : 0;
        c.set(x, y, z, MOB_SPAWNER, type);
        newSpawners.push_back({glm::ivec3(bx + x, y, bz + z), type});
    }
}

// ---------------------------------------------------------------- Заброшенные шахты

void World::placeMineshafts(Chunk& c) {
    const int bx = c.cx * CW, bz = c.cz * CW;
    const int R = 5; // шахта не уходит дальше ~80 блоков от старта
    for (int sz = c.cz - R; sz <= c.cz + R; ++sz)
        for (int sx = c.cx - R; sx <= c.cx + R; ++sx) {
            if (!isMineStart(sx, sz, seed_)) continue;
            std::vector<MinePiece> pieces = buildMineshaft(sx, sz, seed_);
            for (const MinePiece& p : pieces) {
                if (p.x1 < bx || p.x0 > bx + CW - 1 || p.z1 < bz || p.z0 > bz + CW - 1) continue;
                Rng r(p.seed ^ (uint32_t)(c.cx * 73856093) ^ (uint32_t)(c.cz * 19349663));
                auto inChunk = [&](int wx, int wz) { return wx >= bx && wx < bx + CW && wz >= bz && wz < bz + CW; };
                auto setW = [&](int wx, int wy, int wz, uint8_t b, uint8_t m = 0) {
                    if (!inChunk(wx, wz) || wy <= 0 || wy >= CH) return;
                    uint8_t cur = c.get(wx - bx, wy, wz - bz);
                    if (isLiquid(cur) || cur == BEDROCK) return;
                    c.set(wx - bx, wy, wz - bz, b, m);
                };
                auto getW = [&](int wx, int wy, int wz) -> uint8_t {
                    if (!inChunk(wx, wz) || wy < 0 || wy >= CH) return STONE;
                    return c.get(wx - bx, wy, wz - bz);
                };
                switch (p.type) {
                case MinePiece::Room:
                    for (int x = p.x0; x <= p.x1; ++x)
                        for (int z = p.z0; z <= p.z1; ++z) {
                            setW(x, p.y0, z, DIRT);
                            for (int y = p.y0 + 1; y <= p.y1; ++y) setW(x, y, z, AIR);
                        }
                    break;
                case MinePiece::Crossing:
                    for (int x = p.x0; x <= p.x1; ++x)
                        for (int z = p.z0; z <= p.z1; ++z)
                            for (int y = p.y0; y <= p.y1; ++y) setW(x, y, z, AIR);
                    if (p.y1 - p.y0 > 3)
                        for (int y = p.y0; y <= p.y1; ++y) {
                            setW(p.x0 + 1, y, p.z0 + 1, PLANKS); setW(p.x1 - 1, y, p.z0 + 1, PLANKS);
                            setW(p.x0 + 1, y, p.z1 - 1, PLANKS); setW(p.x1 - 1, y, p.z1 - 1, PLANKS);
                        }
                    break;
                case MinePiece::Stairs:
                    for (int i = 0; i < 8; ++i) {
                        int step = std::min(5, i * 5 / 7);
                        int lx = p.dir == 1 ? p.x0 + i : p.dir == 3 ? p.x1 - i : p.x0;
                        int lz = p.dir == 2 ? p.z0 + i : p.dir == 0 ? p.z1 - i : p.z0;
                        int yTop = p.y1 - step;
                        for (int w = 0; w < 3; ++w)
                            for (int y = yTop - 2; y <= yTop; ++y)
                                setW(DX4[p.dir] == 0 ? p.x0 + w : lx, y, DX4[p.dir] == 0 ? lz : p.z0 + w, AIR);
                    }
                    break;
                case MinePiece::Corridor: {
                    bool alongX = DX4[p.dir] != 0;
                    int len = alongX ? p.x1 - p.x0 + 1 : p.z1 - p.z0 + 1;
                    for (int i = 0; i < len; ++i) {
                        for (int w = 0; w < 3; ++w) {
                            int x = alongX ? p.x0 + i : p.x0 + w, z = alongX ? p.z0 + w : p.z0 + i;
                            for (int y = p.y0; y <= p.y0 + 2; ++y) setW(x, y, z, AIR);
                            // Мостик из досок над пустотой
                            if (getW(x, p.y0 - 1, z) == AIR && inChunk(x, z)) setW(x, p.y0 - 1, z, PLANKS);
                            if (p.webs && inChunk(x, z) && r.range(0, 9) == 0) setW(x, p.y0 + 2, z, COBWEB);
                        }
                        int cx = alongX ? p.x0 + i : p.x0 + 1, cz = alongX ? p.z0 + 1 : p.z0 + i;
                        // Опоры: два столба-забора и балка из досок каждые 5 блоков
                        if (i % 5 == 2) {
                            int ax = alongX ? cx : p.x0, az = alongX ? p.z0 : cz;
                            int bx2 = alongX ? cx : p.x1, bz2 = alongX ? p.z1 : cz;
                            setW(ax, p.y0, az, FENCE); setW(ax, p.y0 + 1, az, FENCE);
                            setW(bx2, p.y0, bz2, FENCE); setW(bx2, p.y0 + 1, bz2, FENCE);
                            for (int w = 0; w < 3; ++w)
                                setW(alongX ? cx : p.x0 + w, p.y0 + 2, alongX ? p.z0 + w : cz, PLANKS);
                        } else if (p.rails && r.range(0, 9) < 7 && isSolid(getW(cx, p.y0 - 1, cz))) {
                            setW(cx, p.y0, cz, RAIL, alongX ? 1 : 0);
                        }
                        // Изредка сундук у стены
                        if (i % 5 == 0 && r.range(0, 99) < 2 && inChunk(cx, cz)) {
                            int sx2 = alongX ? cx : p.x0, sz2 = alongX ? p.z0 : cz;
                            if (inChunk(sx2, sz2)) {
                                setW(sx2, p.y0, sz2, CHEST, 2);
                                if (!tileAt(sx2, p.y0, sz2)) {
                                    TileEntity& te = createTile(sx2, p.y0, sz2, TileEntity::Chest);
                                    fillMineshaftChest(te, r);
                                }
                            }
                        }
                    }
                    break;
                }
                }
            }
        }
}

// ---------------------------------------------------------------- Незер (ChunkProviderHell 1.0)

void World::generateNether(Chunk& c) {
    const int bx = c.cx * CW, bz = c.cz * CW;
    for (auto& b : c.biome) b = (uint8_t)Biome::Hell;
    for (int z = 0; z < CW; ++z)
        for (int x = 0; x < CW; ++x) {
            int wx = bx + x, wz = bz + z;
            for (int y = 0; y < CH; ++y) {
                // Огромные пещеры: трёхмерный шум, у пола и потолка камень сплошной
                float n = noise_.noise(wx * 0.022f, y * 0.045f, wz * 0.022f) * 0.75f +
                          noise_.noise(wx * 0.07f + 100.f, y * 0.1f, wz * 0.07f) * 0.25f;
                float edge = (y < 40 ? (40 - y) * 0.03f : 0.f) + (y > 92 ? (y - 92) * 0.035f : 0.f);
                uint8_t b = n + edge > 0.1f ? NETHERRACK : (y <= 31 ? LAVA : AIR);
                if (y == 0 || y == CH - 1) b = BEDROCK;
                else if (y < 5 && hashf(wx, y, wz, seed_) < 0.5f) b = BEDROCK;
                else if (y > CH - 6 && hashf(wx, y, wz, seed_ ^ 77u) < 0.5f) b = BEDROCK;
                c.set(x, y, z, b);
            }
            // Песок душ и гравий на уровне около 64
            for (int y = 72; y >= 56; --y) {
                if (c.get(x, y, z) != NETHERRACK || c.get(x, y + 1, z) != AIR) continue;
                float s = noise_.noise(wx * 0.06f, 5.5f, wz * 0.06f), g = noise_.noise(wx * 0.06f, 9.5f, wz * 0.06f);
                uint8_t cover = s > 0.2f ? SOUL_SAND : g > 0.3f ? GRAVEL : NETHERRACK;
                for (int d = 0; d < 3 && cover != NETHERRACK; ++d)
                    if (c.get(x, y - d, z) == NETHERRACK) c.set(x, y - d, z, cover);
            }
        }
    Rng rng(hash3i(c.cx, 1, c.cz, seed_ ^ 0x6E657468u));
    auto isRack = [&](int x, int y, int z) { return x >= 0 && x < CW && z >= 0 && z < CW && y > 0 && y < CH && c.get(x, y, z) == NETHERRACK; };
    // Лавовые источники в стенах
    for (int i = 0; i < 8; ++i) {
        int x = rng.range(1, 14), y = rng.range(10, 118), z = rng.range(1, 14);
        if (!isRack(x, y, z) || !isRack(x, y + 1, z)) continue;
        int racks = isRack(x + 1, y, z) + isRack(x - 1, y, z) + isRack(x, y, z + 1) + isRack(x, y, z - 1) + isRack(x, y - 1, z);
        if (racks == 4) c.set(x, y, z, LAVA);
    }
    // Вечный огонь на камне
    int fires = rng.range(1, 10);
    for (int i = 0; i < fires; ++i) {
        int x = rng.range(0, 15), z = rng.range(0, 15), y = rng.range(33, 110);
        for (int k = 0; k < 20 && y > 32; ++k, --y)
            if (c.get(x, y, z) == AIR && c.get(x, y - 1, z) == NETHERRACK) { c.set(x, y, z, FIRE); break; }
    }
    // Светокамень гроздьями под потолком
    int glows = rng.range(0, 10);
    for (int i = 0; i < glows; ++i) {
        int sx = rng.range(3, 12), sz = rng.range(3, 12), sy = -1;
        for (int y = 120; y > 40; --y)
            if (c.get(sx, y, sz) == AIR && c.get(sx, y + 1, sz) == NETHERRACK) { sy = y; break; }
        if (sy < 0) continue;
        c.set(sx, sy, sz, GLOWSTONE);
        for (int k = 0; k < 400; ++k) {
            int x = sx + rng.range(-3, 3), y = sy - rng.range(0, 6), z = sz + rng.range(-3, 3);
            if (x < 0 || x >= CW || z < 0 || z >= CW || y <= 0 || c.get(x, y, z) != AIR) continue;
            int n = 0;
            const int D[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
            for (auto& d : D) {
                int nx = x + d[0], ny = y + d[1], nz = z + d[2];
                if (nx >= 0 && nx < CW && nz >= 0 && nz < CW && ny > 0 && ny < CH && c.get(nx, ny, nz) == GLOWSTONE) ++n;
            }
            if (n == 1) c.set(x, y, z, GLOWSTONE);
        }
    }
    // Редкие грибы
    for (int i = 0; i < 2; ++i) {
        int x = rng.range(0, 15), z = rng.range(0, 15), y = rng.range(33, 110);
        if (c.get(x, y, z) == AIR && c.get(x, y - 1, z) == NETHERRACK) c.set(x, y, z, rng.range(0, 1) ? BROWN_MUSHROOM : RED_MUSHROOM);
    }
    if (genVersion_ >= 3) placeFortress10(c);
    else placeFortress(c);
}

// Крепость Незера: сеть мостов из адского кирпича с перилами, центральный зал и платформы
// со спавнерами ифритов, сад адского нароста на песке душ. Одна крепость на область 12x12 чанков.
void World::placeFortress(Chunk& c) {
    const int R = 12;
    int rx = floorDiv(c.cx, R), rz = floorDiv(c.cz, R);
    uint32_t h = hash3i(rx, 7, rz, seed_ ^ 0xF0F7u);
    if (h % 3 == 2) return;
    int ccx = rx * R + 4 + (int)((h >> 8) % 4), ccz = rz * R + 4 + (int)((h >> 12) % 4);
    const int X0 = ccx * CW + 8, Z0 = ccz * CW + 8, Y = 64;
    const int bx = c.cx * CW, bz = c.cz * CW;
    if (bx + CW < X0 - 60 || bx > X0 + 60 || bz + CW < Z0 - 60 || bz > Z0 + 60) return;
    auto inside = [&](int wx, int wy, int wz) { return wx >= bx && wx < bx + CW && wz >= bz && wz < bz + CW && wy > 0 && wy < CH - 1; };
    auto setW = [&](int wx, int wy, int wz, uint8_t b, uint8_t m = 0) { if (inside(wx, wy, wz)) c.set(wx - bx, wy, wz - bz, b, m); };
    auto getW = [&](int wx, int wy, int wz) -> uint8_t { return inside(wx, wy, wz) ? c.get(wx - bx, wy, wz - bz) : AIR; };
    auto pillar = [&](int wx, int wz, int top) {
        for (int y = top; y > 1; --y) {
            uint8_t g = getW(wx, y, wz);
            if (g == NETHERRACK || g == SOUL_SAND || g == GRAVEL || g == BEDROCK || g == NETHER_BRICK) break;
            setW(wx, y, wz, NETHER_BRICK);
        }
    };
    // Мост шириной 5: настил, перила, пустота над ним, арки и опоры каждые 8 блоков
    auto bridge = [&](bool alongX, int from, int to, int cc) {
        for (int t = from; t <= to; ++t)
            for (int w = -2; w <= 2; ++w) {
                int wx = alongX ? t : cc + w, wz = alongX ? cc + w : t;
                if (!inside(wx, Y, wz)) continue;
                setW(wx, Y, wz, NETHER_BRICK);
                for (int dy = 1; dy <= 4; ++dy) setW(wx, Y + dy, wz, AIR);
                if (std::abs(w) == 2) setW(wx, Y + 1, wz, NETHER_FENCE);
                int m8 = ((t % 8) + 8) % 8;
                if (std::abs(w) == 2 || m8 == 0) setW(wx, Y - 1, wz, NETHER_BRICK);
                if (m8 == 0 && std::abs(w) >= 1) pillar(wx, wz, Y - 1);
                if ((m8 == 1 || m8 == 7) && std::abs(w) == 2) setW(wx, Y - 2, wz, NETHER_BRICK);
            }
    };
    bridge(true, X0 - 56, X0 + 56, Z0);
    bridge(false, Z0 - 56, Z0 + 56, X0);
    bridge(false, Z0 - 20, Z0 + 20, X0 - 32);
    bridge(false, Z0 - 20, Z0 + 20, X0 + 32);
    bridge(true, X0 - 20, X0 + 20, Z0 - 32);
    bridge(true, X0 - 20, X0 + 20, Z0 + 32);
    // Центральный зал 13x13 с окнами-решётками и входами с четырёх сторон
    for (int dx = -6; dx <= 6; ++dx)
        for (int dz = -6; dz <= 6; ++dz) {
            int wx = X0 + dx, wz = Z0 + dz;
            bool wall = std::abs(dx) == 6 || std::abs(dz) == 6;
            setW(wx, Y, wz, NETHER_BRICK);
            setW(wx, Y + 7, wz, NETHER_BRICK);
            if (std::abs(dx) <= 6 && std::abs(dz) <= 6 && (std::abs(dx) == 6 || std::abs(dz) == 6)) pillar(wx, wz, Y - 1);
            for (int dy = 1; dy <= 6; ++dy) {
                uint8_t b = AIR;
                if (wall) {
                    bool door = (std::abs(dx) <= 1 || std::abs(dz) <= 1) && dy <= 3;
                    bool window = !door && dy >= 3 && dy <= 4 && ((dx + dz) % 3 == 0);
                    b = door ? AIR : window ? NETHER_FENCE : NETHER_BRICK;
                }
                setW(wx, Y + dy, wz, b);
            }
        }
    // Спавнеры ифритов: в центре зала и на перекрёстках
    const int SP[5][2] = {{0, 0}, {-32, 0}, {32, 0}, {0, -32}, {0, 32}};
    for (auto& sp : SP) {
        int wx = X0 + sp[0], wz = Z0 + sp[1];
        if (!inside(wx, Y + 1, wz)) continue;
        setW(wx, Y + 1, wz, MOB_SPAWNER, 4);
        newSpawners.push_back({glm::ivec3(wx, Y + 1, wz), 4});
    }
    // Сад адского нароста в конце главного моста
    for (int dx = -2; dx <= 2; ++dx)
        for (int dz = -1; dz <= 1; ++dz) {
            int wx = X0 + 52 + dx, wz = Z0 + dz;
            setW(wx, Y, wz, SOUL_SAND);
            setW(wx, Y + 1, wz, NETHER_WART, (uint8_t)((wx * 7 + wz * 3) & 3));
        }
}

// ---------------------------------------------------------------- Крепости (строгхолды)

std::vector<glm::ivec3> World::strongholdCenters() const {
    // Как в 1.0: три крепости на расстоянии 640..1152 блоков, через 120°
    std::vector<glm::ivec3> out;
    Rng r(seed_ ^ 0x5714A1u);
    float a = r.f() * 6.2831853f;
    for (int i = 0; i < 3; ++i) {
        float dist = (1.25f + r.f()) * 32.f * CW;
        out.push_back({(int)(std::cos(a) * dist), 32, (int)(std::sin(a) * dist)});
        a += 6.2831853f / 3.f;
    }
    return out;
}

void World::placeStronghold(Chunk& c) {
    const int bx = c.cx * CW, bz = c.cz * CW;
    for (const glm::ivec3& S : strongholdCenters()) {
        if (bx + CW < S.x - 24 || bx > S.x + 24 || bz + CW < S.z - 48 || bz > S.z + 12) continue;
        auto inside = [&](int wx, int wy, int wz) { return wx >= bx && wx < bx + CW && wz >= bz && wz < bz + CW && wy > 0 && wy < CH - 1; };
        auto setW = [&](int wx, int wy, int wz, uint8_t b, uint8_t m = 0) { if (inside(wx, wy, wz)) c.set(wx - bx, wy, wz - bz, b, m); };
        // Кирпич крепости: обычный, мшистый, треснутый, иногда с чешуйницей внутри
        auto brick = [&](int wx, int wy, int wz) {
            float h = hashf(wx, wy, wz, seed_ ^ 0xB71Cu);
            if (h < 0.03f) setW(wx, wy, wz, MONSTER_EGG, 2);
            else setW(wx, wy, wz, STONE_BRICK, h < 0.25f ? 1 : h < 0.45f ? 2 : 0);
        };
        // Коробка: стены из кирпича, внутри пусто
        auto room = [&](int x0, int y0, int z0, int x1, int y1, int z1) {
            for (int x = x0; x <= x1; ++x)
                for (int z = z0; z <= z1; ++z)
                    for (int y = y0; y <= y1; ++y) {
                        bool wall = x == x0 || x == x1 || z == z0 || z == z1 || y == y0 || y == y1;
                        if (wall) brick(x, y, z);
                        else setW(x, y, z, AIR);
                    }
        };
        const int Y = S.y;
        // ---- Зал с порталом: 11 x 16, помост с кольцом из 12 рамок над лавой, лестница, спавнер чешуйниц
        room(S.x - 5, Y, S.z - 8, S.x + 5, Y + 8, S.z + 8);
        for (int x = S.x - 2; x <= S.x + 2; ++x)
            for (int z = S.z + 2; z <= S.z + 6; ++z)
                for (int y = Y + 1; y <= Y + 3; ++y) brick(x, y, z);
        for (int x = S.x - 1; x <= S.x + 1; ++x) {
            setW(x, Y + 1, S.z - 1, STONEBRICK_STAIRS, 2);
            setW(x, Y + 2, S.z, STONEBRICK_STAIRS, 2);
            setW(x, Y + 3, S.z + 1, STONEBRICK_STAIRS, 2);
            for (int z = S.z + 3; z <= S.z + 5; ++z) {
                setW(x, Y + 3, z, LAVA);
                setW(x, Y + 4, z, AIR);
            }
        }
        for (int i = -1; i <= 1; ++i) {
            auto eye = [&](int k) { return hashf(S.x + k, Y, S.z + i * 7, seed_ ^ 0xE7Eu) < 0.1f ? 4 : 0; };
            setW(S.x + i, Y + 4, S.z + 2, END_PORTAL_FRAME, (uint8_t)(1 | eye(1)));  // смотрит на +Z
            setW(S.x + i, Y + 4, S.z + 6, END_PORTAL_FRAME, (uint8_t)(3 | eye(2)));  // на -Z
            setW(S.x - 2, Y + 4, S.z + 4 + i, END_PORTAL_FRAME, (uint8_t)(0 | eye(3))); // на +X
            setW(S.x + 2, Y + 4, S.z + 4 + i, END_PORTAL_FRAME, (uint8_t)(2 | eye(4))); // на -X
        }
        if (inside(S.x, Y + 1, S.z - 3)) {
            setW(S.x, Y + 1, S.z - 3, MOB_SPAWNER, 5);
            newSpawners.push_back({glm::ivec3(S.x, Y + 1, S.z - 3), 5});
        }
        for (int dx : {-4, 4})
            for (int dz : {-6, 6}) setW(S.x + dx, Y + 1, S.z + dz, TORCH, TORCH_FLOOR);
        // ---- Коридор на север и поперечный коридор
        for (int y = Y + 1; y <= Y + 3; ++y)
            for (int x = S.x - 1; x <= S.x + 1; ++x) setW(x, y, S.z - 8, AIR);
        room(S.x - 2, Y, S.z - 34, S.x + 2, Y + 4, S.z - 8);
        room(S.x - 22, Y, S.z - 22, S.x + 22, Y + 4, S.z - 18);
        for (int y = Y + 1; y <= Y + 3; ++y)
            for (int x = S.x - 1; x <= S.x + 1; ++x) {
                setW(x, y, S.z - 8, AIR);
                for (int z = S.z - 22; z <= S.z - 18; ++z) setW(x, y, z, AIR);
            }
        for (int z = S.z - 32; z <= S.z - 10; z += 8) setW(S.x + 1, Y + 1, z, TORCH, TORCH_FLOOR);
        // ---- Библиотека в конце коридора: книжные полки, паутина, сундук
        room(S.x - 7, Y, S.z - 48, S.x + 7, Y + 7, S.z - 34);
        for (int y = Y + 1; y <= Y + 3; ++y)
            for (int x = S.x - 1; x <= S.x + 1; ++x) setW(x, y, S.z - 34, AIR);
        for (int y = Y + 1; y <= Y + 5; ++y) {
            for (int x = S.x - 6; x <= S.x + 6; ++x) setW(x, y, S.z - 47, BOOKSHELF);
            for (int z = S.z - 46; z <= S.z - 36; ++z) {
                setW(S.x - 6, y, z, BOOKSHELF);
                setW(S.x + 6, y, z, BOOKSHELF);
            }
        }
        for (int z = S.z - 44; z <= S.z - 38; z += 3)
            for (int y = Y + 1; y <= Y + 4; ++y) { setW(S.x - 3, y, z, BOOKSHELF); setW(S.x + 3, y, z, BOOKSHELF); }
        setW(S.x - 5, Y + 6, S.z - 46, COBWEB);
        setW(S.x + 5, Y + 6, S.z - 37, COBWEB);
        if (inside(S.x, Y + 1, S.z - 46)) {
            setW(S.x, Y + 1, S.z - 46, CHEST, 3);
            TileEntity& te = createTile(S.x, Y + 1, S.z - 46, TileEntity::Chest);
            Rng lr(hash3i(S.x, Y, S.z, seed_ ^ 0x11B7u));
            for (int i = 0; i < 6; ++i) {
                int k = lr.range(0, 3);
                ItemStack st = k == 0 ? makeStack(BOOK, lr.range(1, 3)) : k == 1 ? makeStack(PAPER, lr.range(2, 7))
                              : k == 2 ? makeStack(COMPASS) : makeStack(ENDER_PEARL);
                te.items[lr.range(0, 26)] = st;
            }
        }
        // ---- Фонтан на западе, камеры на востоке
        room(S.x - 32, Y, S.z - 26, S.x - 22, Y + 6, S.z - 14);
        for (int y = Y + 1; y <= Y + 3; ++y)
            for (int z = S.z - 21; z <= S.z - 19; ++z) setW(S.x - 22, y, z, AIR);
        for (int x = S.x - 28; x <= S.x - 26; ++x)
            for (int z = S.z - 21; z <= S.z - 19; ++z) { brick(x, Y + 1, z); }
        setW(S.x - 27, Y + 2, S.z - 20, WATER);
        room(S.x + 22, Y, S.z - 26, S.x + 32, Y + 5, S.z - 14);
        for (int y = Y + 1; y <= Y + 3; ++y)
            for (int z = S.z - 21; z <= S.z - 19; ++z) setW(S.x + 22, y, z, AIR);
        for (int z = S.z - 25; z <= S.z - 15; ++z)
            for (int y = Y + 1; y <= Y + 3; ++y)
                if (z != S.z - 20) setW(S.x + 27, y, z, (z % 3 == 0) ? STONE_BRICK : IRON_BARS);
    }
}

// ---------------------------------------------------------------- Деревни (1.0: равнины и пустыни)

bool World::findVillage(int nearX, int nearZ, glm::ivec3& out) const {
    const int R = 24;
    int rx0 = floorDiv(floorDiv(nearX, CW), R), rz0 = floorDiv(floorDiv(nearZ, CW), R);
    float best = 1e18f;
    for (int rz = rz0 - 4; rz <= rz0 + 4; ++rz)
        for (int rx = rx0 - 4; rx <= rx0 + 4; ++rx) {
            uint32_t h = hash3i(rx, 3, rz, seed_ ^ 0x71A6Eu);
            if (h % 2 != 0) continue;
            int ccx = rx * R + 4 + (int)((h >> 8) % (R - 8)), ccz = rz * R + 4 + (int)((h >> 16) % (R - 8));
            int X0 = ccx * CW + 8, Z0 = ccz * CW + 8;
            Biome vb = biomeAt(X0, Z0);
            if (vb != Biome::Plains && vb != Biome::Desert) continue;
            float d = (float)(X0 - nearX) * (X0 - nearX) + (float)(Z0 - nearZ) * (Z0 - nearZ);
            if (d < best) { best = d; out = {X0, terrainHeight(X0, Z0), Z0}; }
        }
    return best < 1e18f;
}

void World::placeVillage(Chunk& c) {
    const int R = 24;
    int rx = floorDiv(c.cx, R), rz = floorDiv(c.cz, R);
    uint32_t h = hash3i(rx, 3, rz, seed_ ^ 0x71A6Eu);
    if (h % 2 != 0) return;
    int ccx = rx * R + 4 + (int)((h >> 8) % (R - 8)), ccz = rz * R + 4 + (int)((h >> 16) % (R - 8));
    const int X0 = ccx * CW + 8, Z0 = ccz * CW + 8;
    Biome vb = biomeAt(X0, Z0);
    if (vb != Biome::Plains && vb != Biome::Desert) return;
    const int bx = c.cx * CW, bz = c.cz * CW;
    if (bx + CW < X0 - 56 || bx > X0 + 56 || bz + CW < Z0 - 56 || bz > Z0 + 56) return;
    const bool desert = vb == Biome::Desert;
    const uint8_t WALL = desert ? SANDSTONE : PLANKS, BASE = desert ? SANDSTONE : COBBLE, POST = desert ? SANDSTONE : LOG;
    const uint8_t ROAD = desert ? SANDSTONE : GRAVEL;
    auto inside = [&](int wx, int wy, int wz) { return wx >= bx && wx < bx + CW && wz >= bz && wz < bz + CW && wy > 0 && wy < CH - 1; };
    auto setW = [&](int wx, int wy, int wz, uint8_t b, uint8_t m = 0) { if (inside(wx, wy, wz)) c.set(wx - bx, wy, wz - bz, b, m); };
    auto ground = [&](int wx, int wz) { return std::max(SEA, terrainHeight(wx, wz)); };
    Rng r(h);

    // Фундамент и расчистка под постройку x0..x1, z0..z1 на уровне y (пол)
    auto pad = [&](int x0, int z0, int x1, int z1, int y, int height) {
        for (int x = x0; x <= x1; ++x)
            for (int z = z0; z <= z1; ++z) {
                for (int yy = y - 1; yy > y - 6 && yy > 0; --yy) setW(x, yy, z, BASE);
                setW(x, y, z, BASE);
                for (int yy = y + 1; yy <= y + height; ++yy) setW(x, yy, z, AIR);
            }
    };
    // Дом: стены, углы-столбы, окна, дверь со стороны дороги (doorSide: 0 +X, 1 +Z, 2 -X, 3 -Z), крыша
    auto house = [&](int x0, int z0, int w, int d, int wallH, int doorSide, bool loot, bool books) {
        int x1 = x0 + w - 1, z1 = z0 + d - 1, y = ground(x0 + w / 2, z0 + d / 2);
        pad(x0, z0, x1, z1, y, wallH + 3);
        for (int x = x0; x <= x1; ++x)
            for (int z = z0; z <= z1; ++z) {
                bool edgeX = x == x0 || x == x1, edgeZ = z == z0 || z == z1;
                if (!edgeX && !edgeZ) { setW(x, y, z, desert ? SANDSTONE : PLANKS); continue; }
                for (int yy = y + 1; yy <= y + wallH; ++yy) {
                    bool corner = edgeX && edgeZ;
                    bool window = !corner && yy == y + 2 && ((edgeX && (z - z0) % 2 == 0) || (edgeZ && (x - x0) % 2 == 0));
                    setW(x, yy, z, corner ? POST : window ? GLASS_PANE : WALL);
                }
            }
        for (int x = x0; x <= x1; ++x)
            for (int z = z0; z <= z1; ++z) setW(x, y + wallH + 1, z, desert ? SLAB : WOOD_STAIRS, desert ? 1 : (uint8_t)((x - x0) < w / 2 ? 1 : 0));
        for (int x = x0 + 1; x < x1; ++x)
            for (int z = z0 + 1; z < z1; ++z) setW(x, y + wallH + 1, z, desert ? SANDSTONE : PLANKS);
        // Дверь
        int dx = doorSide == 0 ? x1 : doorSide == 2 ? x0 : x0 + w / 2;
        int dz = doorSide == 1 ? z1 : doorSide == 3 ? z0 : z0 + d / 2;
        uint8_t f = (uint8_t)((doorSide + 2) & 3); // смотрит внутрь
        setW(dx, y + 1, dz, WOOD_DOOR, f);
        setW(dx, y + 2, dz, WOOD_DOOR, (uint8_t)(f | 8));
        setW(x0 + 1, y + 1, z0 + 1, TORCH, TORCH_FLOOR);
        if (books)
            for (int x = x0 + 1; x < x1; ++x) { setW(x, y + 1, z1 - 1, BOOKSHELF); setW(x, y + 2, z1 - 1, BOOKSHELF); }
        if (loot && inside(x1 - 1, y + 1, z1 - 1)) {
            setW(x1 - 1, y + 1, z1 - 1, CHEST, 3);
            TileEntity& te = createTile(x1 - 1, y + 1, z1 - 1, TileEntity::Chest);
            Rng lr(hash3i(x0, y, z0, seed_ ^ 0xB1AC5u));
            for (int i = 0; i < 5; ++i) {
                int k = lr.range(0, 7);
                ItemStack st = k == 0 ? makeStack(IRON_INGOT, lr.range(1, 5)) : k == 1 ? makeStack(BREAD, lr.range(1, 3))
                              : k == 2 ? makeStack(APPLE, lr.range(1, 3)) : k == 3 ? makeStack(IRON_PICKAXE)
                              : k == 4 ? makeStack(IRON_SWORD) : k == 5 ? makeStack(OBSIDIAN, lr.range(3, 7))
                              : k == 6 ? makeStack(SAPLING, lr.range(3, 7)) : makeStack(GOLD_INGOT, lr.range(1, 3));
                te.items[lr.range(0, 26)] = st;
            }
        }
        glm::vec3 vp(x0 + w / 2 + 0.5f, (float)y + 1.f, z0 + d / 2 + 0.5f);
        if (inside((int)vp.x, (int)vp.y, (int)vp.z)) villagerSpawns.push_back(glm::vec4(vp, -1.f));
    };
    // Огород: грядки с пшеницей и канавка воды посередине
    auto farm = [&](int x0, int z0) {
        int y = ground(x0 + 3, z0 + 4) ;
        pad(x0, z0, x0 + 6, z0 + 8, y, 3);
        for (int x = x0; x <= x0 + 6; ++x)
            for (int z = z0; z <= z0 + 8; ++z) {
                bool border = x == x0 || x == x0 + 6 || z == z0 || z == z0 + 8;
                if (border) { setW(x, y, z, POST); continue; }
                if (x == x0 + 3) { setW(x, y, z, WATER); continue; }
                setW(x, y, z, FARMLAND, 7);
                setW(x, y + 1, z, WHEAT, (uint8_t)r.range(2, 7));
            }
    };
    // Фонарь: столб из забора с чёрной шерстью и четырьмя факелами
    auto lamp = [&](int x, int z) {
        int y = ground(x, z);
        setW(x, y, z, BASE);
        for (int yy = y + 1; yy <= y + 3; ++yy) setW(x, yy, z, desert ? SANDSTONE : FENCE);
        setW(x, y + 4, z, WOOL, 15);
        setW(x + 1, y + 4, z, TORCH, TORCH_WEST_WALL);
        setW(x - 1, y + 4, z, TORCH, TORCH_EAST_WALL);
        setW(x, y + 4, z + 1, TORCH, TORCH_NORTH_WALL);
        setW(x, y + 4, z - 1, TORCH, TORCH_SOUTH_WALL);
    };

    // ---- Колодец в центре
    {
        int y = ground(X0, Z0);
        pad(X0 - 3, Z0 - 3, X0 + 2, Z0 + 2, y, 5);
        for (int x = X0 - 2; x <= X0 + 1; ++x)
            for (int z = Z0 - 2; z <= Z0 + 1; ++z) {
                bool rim = x == X0 - 2 || x == X0 + 1 || z == Z0 - 2 || z == Z0 + 1;
                for (int yy = y - 4; yy < y; ++yy) setW(x, yy, z, rim ? BASE : WATER);
                setW(x, y, z, rim ? BASE : WATER);
                if (rim) setW(x, y + 1, z, BASE);
                bool pillar = (x == X0 - 2 || x == X0 + 1) && (z == Z0 - 2 || z == Z0 + 1);
                if (pillar) { setW(x, y + 2, z, desert ? SANDSTONE : FENCE); setW(x, y + 3, z, desert ? SANDSTONE : FENCE); }
                setW(x, y + 4, z, BASE);
            }
    }
    // ---- Дороги и постройки вдоль них
    const int DXS[4] = {1, 0, -1, 0}, DZS[4] = {0, 1, 0, -1};
    bool church = false;
    for (int arm = 0; arm < 4; ++arm) {
        int len = 24 + (int)((h >> (arm * 4)) % 20);
        for (int t = 4; t <= len; ++t)
            for (int wdt = -1; wdt <= 1; ++wdt) {
                int x = X0 + DXS[arm] * t + (DXS[arm] == 0 ? wdt : 0), z = Z0 + DZS[arm] * t + (DZS[arm] == 0 ? wdt : 0);
                int y = ground(x, z);
                setW(x, y, z, ROAD);
                setW(x, y + 1, z, AIR);
                setW(x, y + 2, z, AIR);
            }
        lamp(X0 + DXS[arm] * (len + 1) + DZS[arm] * 2, Z0 + DZS[arm] * (len + 1) + DXS[arm] * 2);
        // По обе стороны дороги — постройки
        for (int t = 8, k = 0; t + 6 <= len; t += 11, ++k) {
            for (int sideSign : {-1, 1}) {
                uint32_t ph = hash3i(X0 + arm * 31 + t, k, Z0 + sideSign, seed_ ^ 0x40B5u);
                int kind = (int)(ph % 7);
                if (kind == 6 && church) kind = 0;
                // Постройка отступает от дороги на 3 блока; вход смотрит на дорогу
                int ox = X0 + DXS[arm] * t + DZS[arm] * sideSign * 3, oz = Z0 + DZS[arm] * t + DXS[arm] * sideSign * 3;
                int doorSide;
                if (DXS[arm] != 0) doorSide = sideSign > 0 ? 3 : 1;
                else doorSide = sideSign > 0 ? 2 : 0;
                int w = 5, d = 5;
                if (kind == 2) { // огород
                    int fx = DXS[arm] != 0 ? ox : (sideSign > 0 ? ox : ox - 6);
                    int fz = DZS[arm] != 0 ? oz : (sideSign > 0 ? oz : oz - 8);
                    farm(fx, fz);
                    continue;
                }
                if (kind == 1) { w = 4; d = 4; }
                if (kind == 3 || kind == 4) { w = 7; d = 6; }
                if (kind == 6) { w = 5; d = 7; church = true; }
                int x0 = DXS[arm] != 0 ? ox - w / 2 : (sideSign > 0 ? ox : ox - w + 1);
                int z0 = DZS[arm] != 0 ? oz - d / 2 : (sideSign > 0 ? oz : oz - d + 1);
                if (DXS[arm] != 0) { x0 = ox - w / 2; z0 = sideSign > 0 ? oz : oz - d + 1; }
                else { z0 = oz - d / 2; x0 = sideSign > 0 ? ox : ox - w + 1; }
                house(x0, z0, w, d, kind == 6 ? 8 : 3, doorSide, kind == 3, kind == 4);
            }
        }
    }
}

bool World::tryOpenEndPortal(int x, int y, int z) {
    // Ищем центр 3x3, вокруг которого все 12 рамок с оком
    for (int dz = -3; dz <= 3; ++dz)
        for (int dx = -3; dx <= 3; ++dx) {
            int cx = x + dx, cz = z + dz;
            bool ok = true;
            for (int i = -1; i <= 1 && ok; ++i) {
                auto frame = [&](int fx, int fz) {
                    return getBlock(fx, y, fz) == END_PORTAL_FRAME && (getMeta(fx, y, fz) & 4);
                };
                ok = frame(cx + i, cz - 2) && frame(cx + i, cz + 2) && frame(cx - 2, cz + i) && frame(cx + 2, cz + i);
            }
            for (int i = -1; i <= 1 && ok; ++i)
                for (int j = -1; j <= 1 && ok; ++j) ok = !isSolid(getBlock(cx + i, y, cz + j)) || getBlock(cx + i, y, cz + j) == END_PORTAL;
            if (!ok) continue;
            for (int i = -1; i <= 1; ++i)
                for (int j = -1; j <= 1; ++j) setBlock(cx + i, y, cz + j, END_PORTAL);
            return true;
        }
    return false;
}

// ---------------------------------------------------------------- Край (ChunkProviderEnd 1.0)

std::vector<glm::ivec3> World::endPillarTops() const {
    std::vector<glm::ivec3> out;
    Rng r(seed_ ^ 0xE4Du);
    for (int i = 0; i < 10; ++i) {
        float a = i * 6.2831853f / 10.f + r.f() * 0.3f;
        float d = 38.f + r.f() * 6.f;
        out.push_back({(int)std::floor(std::cos(a) * d), 76, (int)std::floor(std::sin(a) * d)});
    }
    return out;
}

void World::generateEnd(Chunk& c) {
    const int bx = c.cx * CW, bz = c.cz * CW;
    for (auto& b : c.biome) b = (uint8_t)Biome::Sky;
    // Парящий остров эндерняка: сверху почти плоский, книзу сужается
    for (int z = 0; z < CW; ++z)
        for (int x = 0; x < CW; ++x) {
            float wx = (float)(bx + x), wz = (float)(bz + z);
            float d = std::sqrt(wx * wx + wz * wz);
            float radius = 96.f + noise_.noise(wx * 0.02f, 3.3f, wz * 0.02f) * 24.f;
            if (d > radius) continue;
            float f = 1.f - d / radius;
            int top = 64 + (int)(noise_.noise(wx * 0.05f, 1.1f, wz * 0.05f) * 4.f);
            int depth = (int)(f * 16.f + noise_.noise(wx * 0.08f, 8.8f, wz * 0.08f) * 6.f) + 2;
            for (int y = std::max(1, top - depth); y <= top; ++y) c.set(x, y, z, END_STONE);
        }
    // Обсидиановые столбы с бедроком наверху (на нём кристалл)
    for (const glm::ivec3& t : endPillarTops()) {
        int r = 2 + (std::abs(t.x * 7 + t.z * 3) % 3);
        for (int dz = -r; dz <= r; ++dz)
            for (int dx = -r; dx <= r; ++dx) {
                if (dx * dx + dz * dz > r * r + 1) continue;
                int wx = t.x + dx, wz = t.z + dz;
                if (wx < bx || wx >= bx + CW || wz < bz || wz >= bz + CW) continue;
                for (int y = 50; y < t.y; ++y) c.set(wx - bx, y, wz - bz, OBSIDIAN);
            }
        if (t.x >= bx && t.x < bx + CW && t.z >= bz && t.z < bz + CW) c.set(t.x - bx, t.y, t.z - bz, BEDROCK);
    }
}
