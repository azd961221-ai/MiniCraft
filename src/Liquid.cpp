// Течение воды и лавы (как BlockFlowing в Minecraft 1.0).
// Метаданные жидкости: 0 — источник, 1..7 — удалённость от источника, бит 8 — падающий поток.
#include <algorithm>
#include <cmath>
#include "World.h"

namespace {
const int HDX[4] = {1, -1, 0, 0}, HDZ[4] = {0, 0, 1, -1};
int liquidDelay(uint8_t b) { return b == LAVA ? 30 : 5; }
int effectiveLevel(uint8_t meta) { return (meta & 8) ? 0 : (meta & 7); }
} // namespace

void World::scheduleUpdate(int x, int y, int z, int delay) {
    if (y < 0 || y >= CH || remote) return;
    int64_t key = posKey(x, y, z);
    if (!pending_.insert(key).second) return;
    updates_.insert({now_ + delay, glm::ivec3(x, y, z)});
}

void World::notifyNeighbors(int x, int y, int z) {
    static const int D[7][3] = {{0, 0, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (auto& d : D) {
        uint8_t b = getBlock(x + d[0], y + d[1], z + d[2]);
        if (isLiquid(b)) scheduleUpdate(x + d[0], y + d[1], z + d[2], liquidDelay(b));
        else if (b == SAND || b == GRAVEL) scheduleUpdate(x + d[0], y + d[1], z + d[2], 3); // tickRate песка 3
    }
}

void World::tickUpdates(int64_t now, int budget) {
    if (remote) return;
    now_ = now;
    while (budget-- > 0 && !updates_.empty() && updates_.begin()->first <= now) {
        glm::ivec3 p = updates_.begin()->second;
        updates_.erase(updates_.begin());
        pending_.erase(posKey(p.x, p.y, p.z));
        // Жидкость в незагруженном чанке замирает
        if (!isChunkLoaded(floorDiv(p.x, CW), floorDiv(p.z, CW))) continue;
        uint8_t ub = getBlock(p.x, p.y, p.z);
        if (ub == SAND || ub == GRAVEL) {
            // Под песком/гравием воздух, огонь или жидкость — срывается и падает
            uint8_t below = p.y > 0 ? getBlock(p.x, p.y - 1, p.z) : AIR;
            if (p.y > 0 && (below == AIR || below == FIRE || isLiquid(below))) {
                setBlock(p.x, p.y, p.z, AIR);
                fallingStarts.push_back({p, ub});
            }
            continue;
        }
        if (ub == FIRE) updateFire(p.x, p.y, p.z);
        else if (isLiquid(ub)) updateLiquid(p.x, p.y, p.z);
        else redstoneTick(p.x, p.y, p.z);
    }
}

// Блок останавливает поток (твёрдый, другой источник того же вида и т.п.)
bool World::liquidBlocks(int x, int y, int z, uint8_t liquid) const {
    uint8_t b = getBlock(x, y, z);
    if (b == AIR) return false;
    if (b == liquid) return getMeta(x, y, z) == 0;
    if (isLiquid(b)) return false;
    if (b == CACTUS || b == REEDS) return true;
    // Смываются: растения, снег, рельсы, факелы, редстоун-пыль, повторители, рычаги, кнопки, огонь, лоза, паутина
    if (needsFloor(b) || b == TORCH || isRedstoneTorch(b) || b == COBWEB || b == REDSTONE_WIRE || isRepeater(b) || b == LEVER ||
        b == STONE_BUTTON || b == FIRE || b == VINE)
        return false;
    return true;
}

// Стоимость пути до ближайшего спуска (как calculateFlowCost), глубина до 4
int World::flowCost(int x, int y, int z, int depth, int fromDir, uint8_t liquid) const {
    int best = 1000;
    for (int d = 0; d < 4; ++d) {
        if ((fromDir == 0 && d == 1) || (fromDir == 1 && d == 0) || (fromDir == 2 && d == 3) || (fromDir == 3 && d == 2)) continue;
        int nx = x + HDX[d], nz = z + HDZ[d];
        if (liquidBlocks(nx, y, nz, liquid)) continue;
        if (getBlock(nx, y, nz) == liquid && getMeta(nx, y, nz) == 0) continue;
        if (!liquidBlocks(nx, y - 1, nz, liquid)) return depth;
        if (depth < 4) best = std::min(best, flowCost(nx, y, nz, depth + 1, d, liquid));
    }
    return best;
}

void World::flowInto(int x, int y, int z, uint8_t liquid, uint8_t meta) {
    uint8_t cur = getBlock(x, y, z);
    if (cur == liquid) {
        uint8_t m = getMeta(x, y, z);
        if (m == 0 || (m & 8)) return;
        if ((meta & 8) == 0 && (m & 7) <= (meta & 7)) return;
    } else if (isLiquid(cur)) {
        // Вода натекает на лаву: источник лавы → обсидиан, поток → булыжник
        if (liquid == WATER && cur == LAVA) {
            setBlock(x, y, z, getMeta(x, y, z) == 0 ? OBSIDIAN : COBBLE);
            soundEvents.push_back({glm::vec3(x, y, z) + 0.5f, "random/fizz"});
        }
        return;
    } else if (cur != AIR) {
        // Поток смывает растения, факелы, рельсы
        popped.push_back({glm::ivec3(x, y, z), cur, getMeta(x, y, z)});
    }
    setBlock(x, y, z, liquid, meta);
}

void World::updateLiquid(int x, int y, int z) {
    uint8_t b = getBlock(x, y, z);
    if (!isLiquid(b)) return;
    const bool lava = b == LAVA;
    const int decay = lava ? 2 : 1;
    uint8_t meta = getMeta(x, y, z);

    // Лава рядом с водой застывает
    if (lava) {
        static const int D[5][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, -1}};
        for (auto& d : D)
            if (getBlock(x + d[0], y + d[1], z + d[2]) == WATER) {
                setBlock(x, y, z, meta == 0 ? OBSIDIAN : COBBLE);
                soundEvents.push_back({glm::vec3(x, y, z) + 0.5f, "random/fizz"});
                return;
            }
    }

    // 1) Пересчёт уровня текущей (не источника) жидкости по соседям
    if (meta != 0) {
        int best = 100, sources = 0;
        for (int d = 0; d < 4; ++d) {
            int nx = x + HDX[d], nz = z + HDZ[d];
            if (getBlock(nx, y, nz) != b) continue;
            uint8_t nm = getMeta(nx, y, nz);
            if (nm == 0) ++sources;
            best = std::min(best, effectiveLevel(nm));
        }
        int newMeta;
        if (getBlock(x, y + 1, z) == b) newMeta = 8; // сверху течёт — падающий поток
        else if (best >= 100 || best + decay >= 8) newMeta = -1;
        else newMeta = best + decay;
        // Бесконечный источник: два соседних источника над твёрдым дном или водой-источником
        if (!lava && sources >= 2) {
            uint8_t below = getBlock(x, y - 1, z);
            if ((isSolid(below) && below != ICE) || (below == WATER && getMeta(x, y - 1, z) == 0)) newMeta = 0;
        }
        if (newMeta != meta) {
            if (newMeta < 0) { setBlock(x, y, z, AIR); return; }
            setBlock(x, y, z, b, (uint8_t)newMeta);
            meta = (uint8_t)newMeta;
        }
    }

    // 2) Вниз, если можно
    uint8_t below = getBlock(x, y - 1, z);
    if (y > 0 && !liquidBlocks(x, y - 1, z, b)) {
        if (lava && below == WATER) {
            setBlock(x, y - 1, z, STONE);
            soundEvents.push_back({glm::vec3(x, y - 1, z) + 0.5f, "random/fizz"});
            return;
        }
        if (below != b || (getMeta(x, y - 1, z) != 0 && getMeta(x, y - 1, z) != 8)) flowInto(x, y - 1, z, b, 8);
        if (meta != 0) return;
    }

    // 3) В стороны: к ближайшему спуску (или во все стороны, если спуска нет рядом)
    int next = (meta & 8) ? decay : (meta & 7) + decay;
    if (next >= 8) return;
    // Стоимость 2000 — направление перекрыто; 1000 — спуска в радиусе 4 нет (тогда течём во все открытые стороны)
    int cost[4], minCost = 2000;
    for (int d = 0; d < 4; ++d) {
        int nx = x + HDX[d], nz = z + HDZ[d];
        cost[d] = 2000;
        if (liquidBlocks(nx, y, nz, b)) continue;
        if (getBlock(nx, y, nz) == b && getMeta(nx, y, nz) == 0) continue;
        cost[d] = !liquidBlocks(nx, y - 1, nz, b) ? 0 : flowCost(nx, y, nz, 1, d, b);
        minCost = std::min(minCost, cost[d]);
    }
    for (int d = 0; d < 4; ++d)
        if (cost[d] == minCost && cost[d] < 2000) flowInto(x + HDX[d], y, z + HDZ[d], b, (uint8_t)next);
}

glm::vec3 World::flowVector(int x, int y, int z) const {
    uint8_t b = getBlock(x, y, z);
    if (!isLiquid(b)) return glm::vec3(0.f);
    int self = effectiveLevel(getMeta(x, y, z));
    glm::vec3 v(0.f);
    for (int d = 0; d < 4; ++d) {
        int nx = x + HDX[d], nz = z + HDZ[d];
        uint8_t nb = getBlock(nx, y, nz);
        int lvl;
        if (nb == b) lvl = effectiveLevel(getMeta(nx, y, nz));
        else if (!isSolid(nb) && getBlock(nx, y - 1, nz) == b) lvl = effectiveLevel(getMeta(nx, y - 1, nz)) + 8;
        else continue;
        float diff = (float)(lvl - self);
        v += glm::vec3(HDX[d], 0, HDZ[d]) * diff;
    }
    if (getMeta(x, y, z) & 8) v.y -= 6.f;
    float len = glm::length(v);
    return len > 1e-4f ? v / len : v;
}
