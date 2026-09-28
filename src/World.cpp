#include "World.h"
#include <algorithm>
#include "Saves.h"
#include <cmath>
#include <cstdio>

static const int DIRS[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

Chunk::~Chunk() {
    for (int i = 0; i < MESH_COUNT; ++i) {
        if (vbo[i]) glDeleteBuffers(1, &vbo[i]);
        if (vao[i]) glDeleteVertexArrays(1, &vao[i]);
    }
}

void Chunk::recomputeHeight(int x, int z) {
    int y = CH - 1;
    while (y >= 0 && lightOpacity(get(x, y, z)) == 0) --y;
    height[z * CW + x] = (uint8_t)(y + 1);
}

World::World(uint32_t seed, int dimension) : seed_(seed), noise_(seed), dimension_(dimension) {}

Chunk* World::chunkAt(int cx, int cz) const {
    int64_t key = chunkKey(cx, cz);
    if (cacheChunk_ && cacheKey_ == key) return cacheChunk_;
    auto it = chunks.find(key);
    if (it == chunks.end()) return nullptr;
    cacheChunk_ = it->second.get();
    cacheKey_ = key;
    return cacheChunk_;
}

uint8_t World::getBlock(int x, int y, int z) const {
    if (y < 0) return dimension_ == 1 ? AIR : BEDROCK; // в Крае под островом — пустота
    if (y >= CH) return AIR;
    Chunk* c = chunkAt(floorDiv(x, CW), floorDiv(z, CW));
    if (!c) return AIR;
    return c->get(x - c->cx * CW, y, z - c->cz * CW);
}

uint8_t World::getMeta(int x, int y, int z) const {
    if (y < 0 || y >= CH) return 0;
    Chunk* c = chunkAt(floorDiv(x, CW), floorDiv(z, CW));
    if (!c) return 0;
    return c->meta[Chunk::index(x - c->cx * CW, y, z - c->cz * CW)];
}

uint8_t* World::lightCell(int x, int y, int z) const {
    if (y < 0 || y >= CH) return nullptr;
    Chunk* c = chunkAt(floorDiv(x, CW), floorDiv(z, CW));
    if (!c) return nullptr;
    return &c->light[Chunk::index(x - c->cx * CW, y, z - c->cz * CW)];
}

int World::getSkyLight(int x, int y, int z) const {
    if (y >= CH) return 15;
    uint8_t* l = lightCell(x, y, z);
    return l ? (*l >> 4) : (y < 0 ? 0 : 15);
}

int World::getBlockLight(int x, int y, int z) const {
    uint8_t* l = lightCell(x, y, z);
    return l ? (*l & 15) : 0;
}

// Помечаем чанк и соседей, у которых эта колонка попадает в «рамку» меша
void World::markDirtyAt(int x, int z) {
    int cx = floorDiv(x, CW), cz = floorDiv(z, CW);
    int lx = x - cx * CW, lz = z - cz * CW;
    for (int dx = -1; dx <= 1; ++dx) {
        if ((dx == -1 && lx != 0) || (dx == 1 && lx != CW - 1)) continue;
        for (int dz = -1; dz <= 1; ++dz) {
            if ((dz == -1 && lz != 0) || (dz == 1 && lz != CW - 1)) continue;
            if (Chunk* n = chunkAt(cx + dx, cz + dz)) n->dirty = true;
        }
    }
}

// ---------------------------------------------------------------- Освещение
//
// Два канала по 4 бита: свет неба и свет блоков. Распространение — поиск в ширину:
// уровень падает на 1 за блок (и на lightOpacity для воды/листвы). Свет неба уровня 15
// идёт вниз без потерь через прозрачные блоки — так получаются «лучи» солнца.

void World::spreadLight(std::deque<LightNode>& q, bool sky) {
    const int shift = sky ? 4 : 0;
    while (!q.empty()) {
        LightNode n = q.front();
        q.pop_front();
        uint8_t* cell = lightCell(n.x, n.y, n.z);
        if (!cell) continue;
        int level = (*cell >> shift) & 15;
        if (level <= 1) continue;
        for (int d = 0; d < 6; ++d) {
            int x = n.x + DIRS[d][0], y = n.y + DIRS[d][1], z = n.z + DIRS[d][2];
            uint8_t* nc = lightCell(x, y, z);
            if (!nc) continue;
            int op = lightOpacity(getBlock(x, y, z));
            if (op >= 15) continue;
            int nl = (sky && d == 3 && level == 15 && op == 0) ? 15 : level - std::max(1, op);
            if (nl > ((*nc >> shift) & 15)) {
                *nc = (uint8_t)((*nc & ~(15 << shift)) | (nl << shift));
                markDirtyAt(x, z);
                q.push_back({x, y, z});
            }
        }
    }
}

void World::removeLight(std::deque<RemoveNode>& rq, std::deque<LightNode>& aq, bool sky) {
    const int shift = sky ? 4 : 0;
    while (!rq.empty()) {
        RemoveNode n = rq.front();
        rq.pop_front();
        for (int d = 0; d < 6; ++d) {
            int x = n.x + DIRS[d][0], y = n.y + DIRS[d][1], z = n.z + DIRS[d][2];
            uint8_t* nc = lightCell(x, y, z);
            if (!nc) continue;
            int nl = (*nc >> shift) & 15;
            if (nl == 0) continue;
            bool derived = nl < n.level || (sky && d == 3 && n.level == 15 && nl == 15);
            if (derived) {
                *nc = (uint8_t)(*nc & ~(15 << shift));
                markDirtyAt(x, z);
                rq.push_back({x, y, z, nl});
            } else {
                aq.push_back({x, y, z}); // более яркий сосед — источник для повторной заливки
            }
        }
    }
}

// Пересчёт света вокруг изменённого блока
void World::relight(int x, int y, int z) {
    uint8_t* cell = lightCell(x, y, z);
    if (!cell) return;
    uint8_t b = getBlock(x, y, z);

    for (int ch = 0; ch < 2; ++ch) {
        bool sky = ch == 1;
        int shift = sky ? 4 : 0;
        int old = (*cell >> shift) & 15;
        std::deque<RemoveNode> rq;
        std::deque<LightNode> aq;
        *cell = (uint8_t)(*cell & ~(15 << shift));
        if (old > 0) rq.push_back({x, y, z, old});
        removeLight(rq, aq, sky);

        if (!sky && lightEmission(b) > 0) {
            *cell = (uint8_t)((*cell & ~15) | lightEmission(b));
            aq.push_back({x, y, z});
        }
        // Соседи снова светят в эту клетку (если она пропускает свет)
        if (lightOpacity(b) < 15)
            for (int d = 0; d < 6; ++d) aq.push_back({x + DIRS[d][0], y + DIRS[d][1], z + DIRS[d][2]});
        spreadLight(aq, sky);
    }
    markDirtyAt(x, z);
}

void World::initLighting(Chunk& c) {
    const int bx = c.cx * CW, bz = c.cz * CW;
    std::deque<LightNode> skyQ, blockQ;

    // Колонки: свет неба сверху вниз, ослабляется водой и листвой
    for (int z = 0; z < CW; ++z) {
        for (int x = 0; x < CW; ++x) {
            int level = dimension_ == 0 ? 15 : 0; // в Незере и Крае нет света неба
            for (int y = CH - 1; y >= 0; --y) {
                int idx = Chunk::index(x, y, z);
                uint8_t b = c.blocks[idx];
                int op = lightOpacity(b);
                if (op >= 15) level = 0;
                else if (op > 0) level = std::max(0, level - op);
                int em = lightEmission(b);
                c.light[idx] = (uint8_t)((level << 4) | em);
                if (em > 0) blockQ.push_back({bx + x, y, bz + z});
            }
            c.recomputeHeight(x, z);
        }
    }

    // Высоты колонок в области 18x18 (чанк + рамка из загруженных соседей)
    auto heightAt = [&](int wx, int wz) -> int {
        Chunk* n = chunkAt(floorDiv(wx, CW), floorDiv(wz, CW));
        if (!n) return -1;
        return n->height[(wz - n->cz * CW) * CW + (wx - n->cx * CW)];
    };

    // Источники растекания света неба: освещённые клетки колонки, рядом с которыми
    // у соседней колонки ещё есть «крыша» (вход в пещеру, навес, обрыв)
    for (int wz = bz - 1; wz <= bz + CW; ++wz) {
        for (int wx = bx - 1; wx <= bx + CW; ++wx) {
            int h = heightAt(wx, wz);
            if (h < 0) continue;
            int maxN = h;
            for (int d : {0, 1, 4, 5}) maxN = std::max(maxN, heightAt(wx + DIRS[d][0], wz + DIRS[d][2]));
            for (int y = h; y < maxN && y < CH; ++y) skyQ.push_back({wx, y, wz});
            // Рамка соседей: подтягиваем их свет блоков внутрь нового чанка
            bool border = wx < bx || wx >= bx + CW || wz < bz || wz >= bz + CW;
            if (border) {
                for (int y = 0; y < CH; ++y) {
                    uint8_t* l = lightCell(wx, y, wz);
                    if (l && (*l & 15) > 1) blockQ.push_back({wx, y, wz});
                }
            }
        }
    }
    spreadLight(skyQ, true);
    spreadLight(blockQ, false);
}

// ---------------------------------------------------------------- Грядки

void World::trampleFarmland(int x, int y, int z, float fallDistance, float roll) {
    if (getBlock(x, y, z) != FARMLAND || roll >= fallDistance - 0.5f) return;
    uint8_t above = getBlock(x, y + 1, z);
    if (above == WHEAT || above == CARROTS || above == POTATOES || above == PUMPKIN_STEM || above == MELON_STEM) {
        popped.push_back({glm::ivec3(x, y + 1, z), above, getMeta(x, y + 1, z)});
        setBlock(x, y + 1, z, AIR);
    }
    setBlock(x, y, z, DIRT);
}

// ---------------------------------------------------------------- Точка появления

glm::vec3 World::safeSpawnNear(const glm::vec3& p, int radius, bool keepIfSafe) const {
    auto open = [&](int x, int y, int z) { uint8_t b = getBlock(x, y, z); return !isSolid(b) && !isLiquid(b); };
    auto loaded = [&](int x, int z) { return isChunkLoaded(floorDiv(x, CW), floorDiv(z, CW)); };
    if (keepIfSafe) {
        int x = (int)std::floor(p.x), y = (int)std::floor(p.y), z = (int)std::floor(p.z);
        if (loaded(x, z) && y > 0 && y + 1 < CH && open(x, y, z) && open(x, y + 1, z)) {
            // Под ногами (в пределах 3 блоков) твёрдая земля, а не вода или пустота
            for (int d = 1; d <= 3; ++d) {
                uint8_t b = getBlock(x, y - d, z);
                if (isLiquid(b)) break;
                if (isSolid(b)) return p;
            }
        }
    }
    auto column = [&](int x, int z, int& top) {
        if (!loaded(x, z)) return false;
        int y = CH - 2;
        while (y > 0 && getBlock(x, y, z) == AIR) --y;
        uint8_t t = getBlock(x, y, z);
        while (y > 0 && !isSolid(t) && !isLiquid(t)) t = getBlock(x, --y, z); // трава, цветы, снег поверх земли
        if (t != GRASS && t != SAND && t != DIRT && t != SNOW && t != MYCELIUM) return false;
        if (!open(x, y + 1, z) || !open(x, y + 2, z)) return false;
        top = y;
        return true;
    };
    int cx = (int)std::floor(p.x), cz = (int)std::floor(p.z);
    for (int r = 0; r <= radius; ++r)
        for (int dz = -r; dz <= r; ++dz)
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
                int top;
                if (column(cx + dx, cz + dz, top)) return glm::vec3(cx + dx + 0.5f, top + 1.f, cz + dz + 0.5f);
            }
    return p;
}

// ---------------------------------------------------------------- Мультиплеер

void World::applyRemote(int x, int y, int z, uint8_t b, uint8_t meta) {
    if (y < 0 || y >= CH) return;
    Chunk* c = chunkAt(floorDiv(x, CW), floorDiv(z, CW));
    if (!c) {
        // Чанк ещё не сгенерирован: правка сохранится и применится при генерации
        int cx = floorDiv(x, CW), cz = floorDiv(z, CW);
        edits_[chunkKey(cx, cz)][Chunk::index(x - cx * CW, y, z - cz * CW)] = (uint16_t)(b | (meta << 8));
        return;
    }
    int lx = x - c->cx * CW, lz = z - c->cz * CW;
    int idx = Chunk::index(lx, y, lz);
    uint8_t oldB = c->blocks[idx];
    edits_[chunkKey(c->cx, c->cz)][idx] = (uint16_t)(b | (meta << 8));
    if (c->blocks[idx] == b && c->meta[idx] == meta) return;
    c->blocks[idx] = b;
    c->meta[idx] = meta;
    if (b == ENCHANT_TABLE) enchantTables.insert(posKey(x, y, z));
    else if (oldB == ENCHANT_TABLE) enchantTables.erase(posKey(x, y, z));
    if (oldB == b) { markDirtyAt(x, z); return; } // сменилась только мета — свет не трогаем
    c->recomputeHeight(lx, lz);
    relight(x, y, z);
}

std::vector<std::pair<uint16_t, uint16_t>> World::chunkEdits(int cx, int cz) const {
    std::vector<std::pair<uint16_t, uint16_t>> out;
    auto it = edits_.find(chunkKey(cx, cz));
    if (it == edits_.end()) return out;
    out.reserve(it->second.size());
    for (auto& [idx, v] : it->second) out.push_back({(uint16_t)idx, v});
    return out;
}

void World::applyChunkEdits(int cx, int cz, const std::vector<std::pair<uint16_t, uint16_t>>& e) {
    for (auto& [idx, v] : e) {
        int lx = idx % CW, lz = (idx / CW) % CW, y = idx / (CW * CW);
        applyRemote(cx * CW + lx, y, cz * CW + lz, (uint8_t)(v & 0xFF), (uint8_t)(v >> 8));
    }
}

void World::updateMulti(const std::vector<glm::vec3>& centers, int radius, int maxGen) {
    auto near = [&](int cx, int cz, int r) {
        for (auto& c : centers) {
            int pcx = floorDiv((int)std::floor(c.x), CW), pcz = floorDiv((int)std::floor(c.z), CW);
            if (std::abs(cx - pcx) <= r && std::abs(cz - pcz) <= r) return true;
        }
        return false;
    };
    for (auto it = chunks.begin(); it != chunks.end();) {
        if (!near(it->second->cx, it->second->cz, radius + 2)) { cacheChunk_ = nullptr; it = chunks.erase(it); }
        else ++it;
    }
    int made = 0;
    for (auto& c : centers) {
        int pcx = floorDiv((int)std::floor(c.x), CW), pcz = floorDiv((int)std::floor(c.z), CW);
        for (int r = 0; r <= radius && made < maxGen; ++r)
            for (int dz = -r; dz <= r && made < maxGen; ++dz)
                for (int dx = -r; dx <= r && made < maxGen; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dz)) != r || isChunkLoaded(pcx + dx, pcz + dz)) continue;
                    auto ch = std::make_unique<Chunk>(pcx + dx, pcz + dz);
                    generate(*ch);
                    Chunk* raw = ch.get();
                    chunks[chunkKey(pcx + dx, pcz + dz)] = std::move(ch);
                    cacheChunk_ = nullptr;
                    initLighting(*raw);
                    for (int y = 0; y < CH; ++y)
                        for (int lz = 0; lz < CW; ++lz)
                            for (int lx = 0; lx < CW; ++lx)
                                if (raw->get(lx, y, lz) == FIRE) scheduleUpdate(raw->cx * CW + lx, y, raw->cz * CW + lz, 40 + fireRand(10));
                    generated.push_back({pcx + dx, pcz + dz});
                    ++made;
                }
    }
}

// ---------------------------------------------------------------- Изменение блоков

void World::setBlock(int x, int y, int z, uint8_t b, uint8_t meta) {
    if (y < 0 || y >= CH) return;
    if (remote) { // сетевая игра: показать сразу, остальное (и последствия) решит сервер
        applyRemote(x, y, z, b, meta);
        if (sendEdit) sendEdit(x, y, z, b, meta, false);
        return;
    }
    Chunk* c = chunkAt(floorDiv(x, CW), floorDiv(z, CW));
    if (!c) return;
    int lx = x - c->cx * CW, lz = z - c->cz * CW;
    int idx = Chunk::index(lx, y, lz);
    uint8_t oldB = c->blocks[idx], oldM = c->meta[idx];
    c->blocks[idx] = b;
    c->meta[idx] = meta;
    edits_[chunkKey(c->cx, c->cz)][idx] = (uint16_t)(b | (meta << 8));
    if (b == ENCHANT_TABLE) enchantTables.insert(posKey(x, y, z));
    else if (oldB == ENCHANT_TABLE) enchantTables.erase(posKey(x, y, z));
    c->recomputeHeight(lx, lz);
    relight(x, y, z);
    if (onChange) onChange(x, y, z, b, meta);

    if (b == FIRE && oldB != FIRE) scheduleUpdate(x, y, z, 40 + fireRand(10));

    // Кровать — две половины: убрали одну, пропадает другая (дроп — с ножной половины)
    if (oldB == BED && b != BED) {
        static const int SD[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
        int f = oldM & 3, sgn = (oldM & 8) ? -1 : 1;
        int px = x + SD[f][0] * sgn, pz = z + SD[f][1] * sgn;
        if (getBlock(px, y, pz) == BED) {
            if (oldM & 8) popped.push_back({glm::ivec3(px, y, pz), BED});
            setBlock(px, y, pz, AIR);
        }
    }
    if ((oldB == SIGN_POST || oldB == WALL_SIGN) && b != oldB) signs.erase(posKey(x, y, z));
    if ((oldB == OBSIDIAN || oldB == PORTAL) && b != PORTAL && !breakingPortal_) {
        // Разрушили рамку или часть портала — гаснет весь связанный портал
        breakingPortal_ = true;
        std::vector<glm::ivec3> q;
        static const int ND[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        for (auto& d : ND)
            if (getBlock(x + d[0], y + d[1], z + d[2]) == PORTAL) q.push_back({x + d[0], y + d[1], z + d[2]});
        for (size_t i = 0; i < q.size() && i < 64; ++i) {
            glm::ivec3 p = q[i];
            if (getBlock(p.x, p.y, p.z) != PORTAL) continue;
            setBlock(p.x, p.y, p.z, AIR);
            for (auto& d : ND)
                if (getBlock(p.x + d[0], p.y + d[1], p.z + d[2]) == PORTAL) q.push_back({p.x + d[0], p.y + d[1], p.z + d[2]});
        }
        breakingPortal_ = false;
    }
    // Рельсы: при удалении рельса обновляем соседние рельсы
    if ((oldB == RAIL || oldB == POWERED_RAIL || oldB == DETECTOR_RAIL) && b != oldB) {
        static const int RD[4][2] = {{0, -1}, {0, 1}, {1, 0}, {-1, 0}};
        for (auto& d : RD) {
            for (int dy : {0, 1, -1}) {
                int nx = x + d[0], ny = y + dy, nz = z + d[1];
                uint8_t nb = getBlock(nx, ny, nz);
                if (nb == RAIL || nb == POWERED_RAIL || nb == DETECTOR_RAIL) updateRailShape(nx, ny, nz, true);
            }
        }
    }
    // При установке рельса обновляем форму
    if ((b == RAIL || b == POWERED_RAIL || b == DETECTOR_RAIL) && b != oldB) {
        updateRailShape(x, y, z, true);
    }
    // Поршень и головка: убрали головку у выдвинутого поршня — ломается и поршень; убрали поршень — пропадает головка
    {
        static const int PD[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        if (oldB == PISTON_HEAD && b != PISTON_HEAD) {
            int f = oldM & 7;
            int bx = x - PD[f][0], by = y - PD[f][1], bz = z - PD[f][2];
            uint8_t bb = getBlock(bx, by, bz);
            if (isPiston(bb) && (getMeta(bx, by, bz) & 8)) {
                popped.push_back({glm::ivec3(bx, by, bz), bb});
                setBlock(bx, by, bz, AIR);
            }
        }
        if (isPiston(oldB) && (oldM & 8) && b != oldB) {
            int f = oldM & 7;
            int hx = x + PD[f][0], hy = y + PD[f][1], hz = z + PD[f][2];
            if (getBlock(hx, hy, hz) == PISTON_HEAD) setBlock(hx, hy, hz, AIR);
        }
    }

    // Дверь — два блока: убрали одну половину, пропадает и другая (дроп — с нижней)
    if (isDoor(oldB) && b != oldB) {
        int py = (oldM & 8) ? y - 1 : y + 1;
        if (getBlock(x, py, z) == oldB) {
            if (oldM & 8) popped.push_back({glm::ivec3(x, py, z), oldB});
            setBlock(x, py, z, AIR);
        }
    }

    // Потерявшие опору растения, снег, рельсы, факелы, лестницы, люки, двери
    if (!isSolid(b) || isDoor(oldB)) {
        static const int D[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        for (int k = 0; k < 6; ++k) {
            int px = x + D[k][0], py = y + D[k][1], pz = z + D[k][2];
            uint8_t nb = getBlock(px, py, pz);
            if (nb == AIR || (nb == CACTUS && b == CACTUS)) continue;
            uint8_t nm = getMeta(px, py, pz);
            if (supportDir(nb, nm) != (k ^ 1)) continue;
            if (isDoor(nb) && (nm & 8) && b == nb) continue; // верх двери стоит на её же низе
            // Верхняя половина двери выпадает вместе с нижней (дроп даёт нижняя)
            if (!(isDoor(nb) && (nm & 8))) popped.push_back({glm::ivec3(px, py, pz), nb, nm});
            setBlock(px, py, pz, AIR);
        }
    }
    notifyNeighbors(x, y, z);
    if (inRedstone_ == 0 && (isRedstoneBlock(b) || isRedstoneBlock(oldB) || isOpaque(b) != isOpaque(oldB))) redstoneChanged(x, y, z);
}

// ---------------------------------------------------------------- Блоки с содержимым

TileEntity* World::tileAt(int x, int y, int z) {
    auto it = tiles.find(posKey(x, y, z));
    return it == tiles.end() ? nullptr : &it->second;
}

bool World::chestPair(int x, int y, int z, glm::ivec3& first, glm::ivec3& second) const {
    const glm::ivec3 p(x, y, z);
    static const glm::ivec3 N[4] = {{-1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 1}};
    for (const glm::ivec3& d : N) {
        glm::ivec3 q = p + d;
        if (getBlock(q.x, q.y, q.z) != CHEST) continue;
        bool before = d.x < 0 || d.z < 0;
        first = before ? q : p;
        second = before ? p : q;
        return true;
    }
    first = second = p;
    return false;
}

bool World::chestBlocked(int x, int y, int z) const {
    auto covered = [&](int bx, int by, int bz) { uint8_t b = getBlock(bx, by + 1, bz); return isOpaque(b) && isSolid(b); };
    if (covered(x, y, z)) return true;
    glm::ivec3 a, b;
    if (chestPair(x, y, z, a, b)) {
        glm::ivec3 o = a == glm::ivec3(x, y, z) ? b : a;
        if (covered(o.x, o.y, o.z)) return true;
    }
    return false;
}

bool World::canPlaceChest(int x, int y, int z) const {
    static const int D[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    int n = 0;
    for (auto& d : D) {
        int nx = x + d[0], nz = z + d[1];
        if (getBlock(nx, y, nz) != CHEST) continue;
        if (++n > 1) return false;
        for (auto& e : D) // у соседа уже есть пара
            if (!(nx + e[0] == x && nz + e[1] == z) && getBlock(nx + e[0], y, nz + e[1]) == CHEST) return false;
    }
    return true;
}

TileEntity& World::createTile(int x, int y, int z, TileEntity::Type type) {
    TileEntity& te = tiles[posKey(x, y, z)];
    te = TileEntity{};
    te.type = type;
    te.x = x; te.y = y; te.z = z;
    return te;
}

void World::removeTile(int x, int y, int z) { tiles.erase(posKey(x, y, z)); }

// ---------------------------------------------------------------- Случайные тики

static uint32_t xorshift(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }

bool World::growTree(int x, int y, int z, uint32_t& rng, int type) {
    int trunk = type == 1 ? 6 + (int)(xorshift(rng) % 4) : type == 2 ? 5 + (int)(xorshift(rng) % 3) : 4 + (int)(xorshift(rng) % 3);
    if (y < 1 || y + trunk + 2 >= CH) return false;
    uint8_t soil = getBlock(x, y - 1, z);
    if (soil != GRASS && soil != DIRT) return false;
    // Место под ствол и крону
    for (int dy = 0; dy <= trunk + 1; ++dy) {
        int r = dy < 2 ? 0 : (dy >= trunk ? 1 : 2);
        for (int dz = -r; dz <= r; ++dz)
            for (int dx = -r; dx <= r; ++dx) {
                uint8_t b = getBlock(x + dx, y + dy, z + dz);
                if (b != AIR && b != LEAVES && !isReplaceable(b) && !(dx == 0 && dz == 0 && dy == 0)) return false;
            }
    }
    setBlock(x, y - 1, z, DIRT);
    auto leaf = [&](int lx, int ly, int lz) {
        uint8_t cur = getBlock(lx, ly, lz);
        if (cur == AIR || isReplaceable(cur)) setBlock(lx, ly, lz, LEAVES, (uint8_t)type);
    };
    if (type == 1) {
        for (int dy = trunk + 1; dy >= 2; --dy) {
            int k = trunk + 1 - dy;
            int radius = k == 0 ? 0 : (k % 2 == 1 ? 1 : (k >= 4 ? 2 : 1));
            for (int dz = -radius; dz <= radius; ++dz)
                for (int dx = -radius; dx <= radius; ++dx)
                    if (!(radius > 1 && std::abs(dx) == radius && std::abs(dz) == radius)) leaf(x + dx, y + dy, z + dz);
        }
    } else {
        for (int dy = trunk - 3; dy <= trunk; ++dy) {
            int r = dy >= trunk - 1 ? 1 : 2;
            for (int dz = -r; dz <= r; ++dz)
                for (int dx = -r; dx <= r; ++dx) {
                    bool corner = std::abs(dx) == r && std::abs(dz) == r;
                    if (corner && (dy == trunk || xorshift(rng) % 2 == 0)) continue;
                    leaf(x + dx, y + dy, z + dz);
                }
        }
    }
    for (int dy = 0; dy < trunk; ++dy) setBlock(x, y + dy, z, LOG, (uint8_t)type);
    return true;
}

void World::randomTick(const glm::vec3& center, uint32_t& rng, bool precipitation) {
    if (remote) return; // рост, снег, листва — на сервере
    int pcx = floorDiv((int)std::floor(center.x), CW), pcz = floorDiv((int)std::floor(center.z), CW);
    auto light = [&](int x, int y, int z) { return std::max(getSkyLight(x, y, z), getBlockLight(x, y, z)); };
    auto columnHeight = [&](int x, int y, int z, uint8_t b) {
        int h = 1;
        while (getBlock(x, y - h, z) == b) ++h;
        return h;
    };
    for (int dz = -6; dz <= 6; ++dz)
        for (int dx = -6; dx <= 6; ++dx) {
            Chunk* c = chunkAt(pcx + dx, pcz + dz);
            if (!c) continue;
            const int bx = c->cx * CW, bz = c->cz * CW;

            // Снегопад: в холодных биомах копится снег, вода на поверхности замерзает
            if (precipitation && xorshift(rng) % 16 == 0) {
                uint32_t r = xorshift(rng);
                int lx = r & 15, lz = (r >> 4) & 15;
                if (biomeInfo((Biome)c->biome[lz * CW + lx]).snowy) {
                    int y = c->height[lz * CW + lx] - 1;
                    if (y > 0 && y + 1 < CH) {
                        uint8_t t = c->get(lx, y, lz);
                        if (t == WATER && c->meta[Chunk::index(lx, y, lz)] == 0) setBlock(bx + lx, y, bz + lz, ICE);
                        else if ((isOpaque(t) || t == LEAVES) && c->get(lx, y + 1, lz) == AIR) setBlock(bx + lx, y + 1, bz + lz, SNOW_LAYER);
                    }
                }
            }

            // 3 случайных блока на каждую секцию 16x16x16
            for (int s = 0; s < CH / 16; ++s)
                for (int k = 0; k < 3; ++k) {
                    uint32_t r = xorshift(rng);
                    int lx = r & 15, lz = (r >> 4) & 15, y = s * 16 + ((r >> 8) & 15);
                    uint8_t b = c->get(lx, y, lz);
                    int x = bx + lx, z = bz + lz;
                    uint8_t& m = c->meta[Chunk::index(lx, y, lz)];
                    switch (b) {
                    case GRASS:
                    case MYCELIUM: {
                        uint8_t above = getBlock(x, y + 1, z);
                        if (light(x, y + 1, z) < 4 && lightOpacity(above) > 2) {
                            setBlock(x, y, z, DIRT);
                        } else if (light(x, y + 1, z) >= 9) {
                            int tx = x + (int)((r >> 12) % 3) - 1, ty = y + (int)((r >> 16) % 5) - 3, tz = z + (int)((r >> 20) % 3) - 1;
                            if (getBlock(tx, ty, tz) == DIRT && light(tx, ty + 1, tz) >= 4 && lightOpacity(getBlock(tx, ty + 1, tz)) <= 2)
                                setBlock(tx, ty, tz, b);
                        }
                        break;
                    }
                    case SAPLING:
                        if (light(x, y + 1, z) >= 9 && (r >> 24) % 7 == 0) growTree(x, y, z, rng, m & 3);
                        break;
                    case PUMPKIN_STEM: case MELON_STEM: {
                        if (light(x, y + 1, z) < 9 || (r >> 24) % 5 != 0) break;
                        if (m < 7) { setBlock(x, y, z, b, (uint8_t)(m + 1)); break; }
                        uint8_t fruit = b == PUMPKIN_STEM ? PUMPKIN : MELON_BLOCK;
                        static const int FD[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                        bool has = false;
                        for (auto& d : FD) has |= getBlock(x + d[0], y, z + d[1]) == fruit;
                        if (has) break;
                        const int* d = FD[(r >> 12) & 3];
                        uint8_t under = getBlock(x + d[0], y - 1, z + d[1]);
                        if (getBlock(x + d[0], y, z + d[1]) == AIR && (under == FARMLAND || under == DIRT || under == GRASS))
                            setBlock(x + d[0], y, z + d[1], fruit, (uint8_t)((r >> 16) & 3));
                        break;
                    }
                    case VINE:
                        // Лоза разрастается вниз
                        if ((r >> 24) % 4 == 0 && getBlock(x, y - 1, z) == AIR && m != 0) setBlock(x, y - 1, z, VINE, m);
                        break;
                    case NETHER_WART:
                        if (m < 3 && getBlock(x, y - 1, z) == SOUL_SAND && (r >> 24) % 10 == 0) setBlock(x, y, z, NETHER_WART, (uint8_t)(m + 1));
                        break;
                    case WHEAT:
                    case CARROTS:
                    case POTATOES: {
                        // BlockCrops.getGrowthRate: грядки 3x3 под посевом (сухая 1, политая 3, соседние — четверть);
                        // ряды пшеницы по обеим осям или по диагонали растут вдвое медленнее
                        if (m >= 7 || light(x, y + 1, z) < 9) break;
                        float rate = 1.f;
                        for (int dz = -1; dz <= 1; ++dz)
                            for (int dx = -1; dx <= 1; ++dx) {
                                if (getBlock(x + dx, y - 1, z + dz) != FARMLAND) continue;
                                float f = getMeta(x + dx, y - 1, z + dz) > 0 ? 3.f : 1.f;
                                rate += (dx || dz) ? f / 4.f : f;
                            }
                        auto crop = [&](int dx, int dz) { return getBlock(x + dx, y, z + dz) == b; };
                        bool alongX = crop(-1, 0) || crop(1, 0), alongZ = crop(0, -1) || crop(0, 1);
                        bool diag = crop(-1, -1) || crop(1, -1) || crop(1, 1) || crop(-1, 1);
                        if (diag || (alongX && alongZ)) rate /= 2.f;
                        if (xorshift(rng) % (uint32_t)std::max(1, (int)(25.f / rate)) == 0) setBlock(x, y, z, b, (uint8_t)(m + 1));
                        break;
                    }
                    case FARMLAND: {
                        bool water = false;
                        for (int wz = -4; wz <= 4 && !water; ++wz)
                            for (int wx = -4; wx <= 4 && !water; ++wx)
                                for (int wy = 0; wy <= 1 && !water; ++wy)
                                    water = getBlock(x + wx, y + wy, z + wz) == WATER;
                        if (!water && rainsAt(x, y + 1, z)) water = true; // дождь тоже поливает
                        if (water) { if (m != 7) setBlock(x, y, z, FARMLAND, 7); }
                        else if (m > 0) setBlock(x, y, z, FARMLAND, (uint8_t)(m - 1));
                        else if (getBlock(x, y + 1, z) != WHEAT && getBlock(x, y + 1, z) != CARROTS && getBlock(x, y + 1, z) != POTATOES &&
                                 getBlock(x, y + 1, z) != PUMPKIN_STEM && getBlock(x, y + 1, z) != MELON_STEM)
                            setBlock(x, y, z, DIRT);
                        break;
                    }
                    case CACTUS:
                    case REEDS: {
                        // Кактус ломается, если рядом твёрдый блок
                        if (b == CACTUS && (isSolid(getBlock(x + 1, y, z)) || isSolid(getBlock(x - 1, y, z)) ||
                                            isSolid(getBlock(x, y, z + 1)) || isSolid(getBlock(x, y, z - 1)))) {
                            popped.push_back({glm::ivec3(x, y, z), b});
                            setBlock(x, y, z, AIR);
                            break;
                        }
                        // Растут до высоты 3: счётчик в метаданных, на 15 — новый блок сверху
                        if (getBlock(x, y + 1, z) == AIR && columnHeight(x, y, z, b) < 3) {
                            if (m >= 15) { m = 0; setBlock(x, y + 1, z, b); }
                            else ++m;
                        }
                        break;
                    }
                    case LEAVES: {
                        if (m & LEAVES_PLAYER) break;
                        // Листва опадает, если по листве за 4 шага не добраться до ствола
                        bool log = false;
                        struct Q { int x, y, z, d; };
                        std::vector<Q> q{{x, y, z, 0}};
                        std::unordered_set<int64_t> seen{posKey(x, y, z)};
                        for (size_t qi = 0; qi < q.size() && !log; ++qi) {
                            for (int d = 0; d < 6 && !log; ++d) {
                                int nx = q[qi].x + DIRS[d][0], ny = q[qi].y + DIRS[d][1], nz = q[qi].z + DIRS[d][2];
                                uint8_t nb = getBlock(nx, ny, nz);
                                if (nb == LOG) log = true;
                                else if (nb == LEAVES && q[qi].d < 4 && seen.insert(posKey(nx, ny, nz)).second)
                                    q.push_back({nx, ny, nz, q[qi].d + 1});
                            }
                        }
                        if (!log) {
                            popped.push_back({glm::ivec3(x, y, z), b, m});
                            setBlock(x, y, z, AIR);
                        }
                        break;
                    }
                    case LAVA: {
                        int n = (int)((r >> 24) % 3), lx2 = x, ly2 = y, lz2 = z;
                        for (int i = 0; i < n; ++i) {
                            uint32_t q = xorshift(rng);
                            lx2 += (int)(q % 3) - 1;
                            ly2 += 1;
                            lz2 += (int)((q >> 4) % 3) - 1;
                            uint8_t t = getBlock(lx2, ly2, lz2);
                            if (t == AIR) {
                                if (flammableNear(lx2, ly2, lz2)) { setBlock(lx2, ly2, lz2, FIRE); break; }
                                continue;
                            }
                            if (isSolid(t)) break;
                        }
                        break;
                    }
                    case ICE:
                        if (getBlockLight(x, y, z) > 11) setBlock(x, y, z, WATER);
                        break;
                    case SNOW_LAYER:
                        if (getBlockLight(x, y, z) > 11) setBlock(x, y, z, AIR);
                        break;
                    default: break;
                    }
                }
        }
}

// ---------------------------------------------------------------- Стриминг чанков

void World::update(const glm::vec3& center, int maxGen, int maxMesh) {
    int pcx = floorDiv((int)std::floor(center.x), CW);
    int pcz = floorDiv((int)std::floor(center.z), CW);
    const int rd = renderDistance;

    // Выгрузка далёких чанков
    for (auto it = chunks.begin(); it != chunks.end();) {
        const Chunk& c = *it->second;
        if (std::abs(c.cx - pcx) > rd + 3 || std::abs(c.cz - pcz) > rd + 3) {
            cacheChunk_ = nullptr;
            it = chunks.erase(it);
        } else {
            ++it;
        }
    }

    // Генерация недостающих (на 1 больше дальности прорисовки — для соседей меша)
    struct Cand { int d, x, z; };
    std::vector<Cand> need;
    const int gr = rd + 1;
    for (int dz = -gr; dz <= gr; ++dz)
        for (int dx = -gr; dx <= gr; ++dx) {
            int d = dx * dx + dz * dz;
            if (d > gr * gr + 1) continue;
            if (!isChunkLoaded(pcx + dx, pcz + dz)) need.push_back({d, pcx + dx, pcz + dz});
        }
    std::sort(need.begin(), need.end(), [](const Cand& a, const Cand& b) { return a.d < b.d; });
    for (int i = 0; i < (int)need.size() && i < maxGen; ++i) {
        auto c = std::make_unique<Chunk>(need[i].x, need[i].z);
        generate(*c);
        Chunk* raw = c.get();
        chunks[chunkKey(need[i].x, need[i].z)] = std::move(c);
        cacheChunk_ = nullptr;
        initLighting(*raw);
        // Запланированные тики не сохраняются: огонь из сохранения снова ставим в очередь,
        // иначе он станет вечным и не погаснет от дождя
        for (int y = 0; y < CH; ++y)
            for (int lz = 0; lz < CW; ++lz)
                for (int lx = 0; lx < CW; ++lx)
                    if (raw->get(lx, y, lz) == FIRE) scheduleUpdate(raw->cx * CW + lx, y, raw->cz * CW + lz, 40 + fireRand(10));
        generated.push_back({need[i].x, need[i].z});
    }

    // Меширование: только чанки, у которых загружены все 8 соседей
    std::vector<std::pair<int, Chunk*>> toMesh;
    for (auto& [key, c] : chunks) {
        if (!c->dirty) continue;
        int dx = c->cx - pcx, dz = c->cz - pcz;
        int d = dx * dx + dz * dz;
        if (d > rd * rd + 1) continue;
        bool ready = true;
        for (int oz = -1; oz <= 1 && ready; ++oz)
            for (int ox = -1; ox <= 1 && ready; ++ox)
                if (!isChunkLoaded(c->cx + ox, c->cz + oz)) ready = false;
        if (ready) toMesh.push_back({d, c.get()});
    }
    std::sort(toMesh.begin(), toMesh.end(), [](auto& a, auto& b) { return a.first < b.first; });
    for (int i = 0; i < (int)toMesh.size() && i < maxMesh; ++i) buildMesh(*toMesh[i].second);
}

// ---------------------------------------------------------------- Сохранение

static const uint32_t SAVE_MAGIC = 0x3657434D;   // "MCW6": предметы с чарами
static const uint32_t SAVE_MAGIC_V5 = 0x3557434D; // "MCW5": предметы без чар (читается и переводится)

namespace {
// Раскладка структур версии MCW5 (до чар) — для чтения старых сохранений
struct LegacyStack { uint16_t id; uint8_t count; uint16_t damage; };
struct LegacySaveState {
    glm::vec3 pos, spawn;
    float yaw, pitch;
    int64_t worldTime;
    int health, food, air;
    float saturation;
    uint8_t gameMode;
    LegacyStack slots[Inventory::SIZE];
    LegacyStack armor[4];
    uint8_t raining, thundering;
    int rainTime, thunderTime;
};
struct LegacyTile {
    TileEntity::Type type;
    int x, y, z;
    LegacyStack items[27];
    int burnTime, burnMax, cookTime;
};
ItemStack fromLegacy(const LegacyStack& l) { return makeStack(l.id, l.count, l.damage); }
} // namespace

bool World::save(const std::string& path, const SaveState& st) const {
    const std::string tmpPath = path + ".tmp";
    FILE* f = openFileUtf8(tmpPath, "wb");
    if (!f) return false;
    uint32_t n = (uint32_t)edits_.size();
    std::fwrite(&SAVE_MAGIC, 4, 1, f);
    std::fwrite(&seed_, 4, 1, f);
    std::fwrite(&st, sizeof(SaveState), 1, f);
    std::fwrite(&n, 4, 1, f);
    for (auto& [key, m] : edits_) {
        uint32_t cnt = (uint32_t)m.size();
        std::fwrite(&key, 8, 1, f);
        std::fwrite(&cnt, 4, 1, f);
        for (auto& [idx, v] : m) {
            int32_t i = idx;
            std::fwrite(&i, 4, 1, f);
            std::fwrite(&v, 2, 1, f);
        }
    }
    uint32_t nt = (uint32_t)tiles.size();
    std::fwrite(&nt, 4, 1, f);
    for (auto& [key, te] : tiles) std::fwrite(&te, sizeof(TileEntity), 1, f);
    // Дополнение (старые версии его не читают): точки появления и тексты табличек
    const uint32_t EXT = 0x31545845; // "EXT1"
    std::fwrite(&EXT, 4, 1, f);
    std::fwrite(&worldSpawn, sizeof(worldSpawn), 1, f);
    uint8_t hb = hasBedSpawn ? 1 : 0;
    std::fwrite(&hb, 1, 1, f);
    std::fwrite(&bedSpawn, sizeof(bedSpawn), 1, f);
    uint32_t ns = (uint32_t)signs.size();
    uint8_t dd = dragonDefeated ? 1 : 0;
    std::fwrite(&ns, 4, 1, f);
    for (auto& [key, lines] : signs) {
        std::fwrite(&key, 8, 1, f);
        for (auto& l : lines) {
            uint8_t len = (uint8_t)std::min<size_t>(l.size(), 255);
            std::fwrite(&len, 1, 1, f);
            std::fwrite(l.data(), 1, len, f);
        }
    }
    std::fwrite(&dd, 1, 1, f);
    bool ok = !std::ferror(f);
    ok = std::fclose(f) == 0 && ok;
    return ok && commitFile(tmpPath, path);
}

bool World::load(const std::string& path, SaveState& st) {
    if (loadFrom(path, st)) return true;
    // Основной файл повреждён или его нет (оборвалась запись) — берём предыдущее сохранение
    return fileExistsUtf8(path + ".bak") && loadFrom(path + ".bak", st);
}

bool World::loadFrom(const std::string& path, SaveState& st) {
    FILE* f = openFileUtf8(path, "rb");
    if (!f) return false;
    uint32_t magic = 0, n = 0, seed = 0;
    SaveState tmp;
    bool ok = std::fread(&magic, 4, 1, f) == 1 && (magic == SAVE_MAGIC || magic == SAVE_MAGIC_V5) &&
              std::fread(&seed, 4, 1, f) == 1;
    bool legacy = magic == SAVE_MAGIC_V5;
    if (ok && legacy) {
        LegacySaveState l;
        ok = std::fread(&l, sizeof(LegacySaveState), 1, f) == 1;
        if (ok) {
            tmp.pos = l.pos; tmp.spawn = l.spawn; tmp.yaw = l.yaw; tmp.pitch = l.pitch; tmp.worldTime = l.worldTime;
            tmp.health = l.health; tmp.food = l.food; tmp.air = l.air; tmp.saturation = l.saturation; tmp.gameMode = l.gameMode;
            for (int i = 0; i < Inventory::SIZE; ++i) tmp.inventory.slots[i] = fromLegacy(l.slots[i]);
            for (int i = 0; i < 4; ++i) tmp.inventory.armor[i] = fromLegacy(l.armor[i]);
            tmp.raining = l.raining; tmp.thundering = l.thundering; tmp.rainTime = l.rainTime; tmp.thunderTime = l.thunderTime;
        }
    } else if (ok) {
        ok = std::fread(&tmp, sizeof(SaveState), 1, f) == 1;
    }
    ok = ok && std::fread(&n, 4, 1, f) == 1;
    if (ok) {
        edits_.clear();
        for (uint32_t k = 0; k < n && ok; ++k) {
            int64_t key; uint32_t cnt;
            ok = std::fread(&key, 8, 1, f) == 1 && std::fread(&cnt, 4, 1, f) == 1;
            auto& m = edits_[key];
            for (uint32_t j = 0; j < cnt && ok; ++j) {
                int32_t idx; uint16_t v;
                ok = std::fread(&idx, 4, 1, f) == 1 && std::fread(&v, 2, 1, f) == 1 &&
                     idx >= 0 && idx < CW * CH * CW && (v & 0xFF) < BLOCK_COUNT;
                if (ok) m[idx] = v;
            }
        }
    }
    std::unordered_map<int64_t, TileEntity> loadedTiles;
    uint32_t nt = 0;
    if (ok && std::fread(&nt, 4, 1, f) == 1) {
        for (uint32_t i = 0; i < nt && ok; ++i) {
            TileEntity te;
            if (legacy) {
                LegacyTile lt;
                ok = std::fread(&lt, sizeof(LegacyTile), 1, f) == 1;
                te.type = lt.type; te.x = lt.x; te.y = lt.y; te.z = lt.z;
                for (int k = 0; k < 27; ++k) te.items[k] = fromLegacy(lt.items[k]);
                te.burnTime = lt.burnTime; te.burnMax = lt.burnMax; te.cookTime = lt.cookTime;
            } else {
                ok = std::fread(&te, sizeof(TileEntity), 1, f) == 1;
            }
            if (ok) loadedTiles[posKey(te.x, te.y, te.z)] = te;
        }
    }
    signs.clear();
    hasBedSpawn = false;
    worldSpawn = tmp.spawn;
    uint32_t ext = 0;
    if (ok && std::fread(&ext, 4, 1, f) == 1 && ext == 0x31545845) {
        uint8_t hb = 0;
        uint32_t ns = 0;
        bool eok = std::fread(&worldSpawn, sizeof(worldSpawn), 1, f) == 1 && std::fread(&hb, 1, 1, f) == 1 &&
                   std::fread(&bedSpawn, sizeof(bedSpawn), 1, f) == 1 && std::fread(&ns, 4, 1, f) == 1;
        hasBedSpawn = eok && hb;
        for (uint32_t i = 0; i < ns && eok; ++i) {
            int64_t key;
            std::array<std::string, 4> lines;
            eok = std::fread(&key, 8, 1, f) == 1;
            for (auto& l : lines) {
                uint8_t len = 0;
                eok = eok && std::fread(&len, 1, 1, f) == 1;
                l.resize(len);
                eok = eok && (len == 0 || std::fread(&l[0], 1, len, f) == len);
            }
            if (eok) signs[key] = lines;
        }
        uint8_t dd = 0;
        if (eok && std::fread(&dd, 1, 1, f) == 1) dragonDefeated = dd != 0;
    }
    std::fclose(f);
    if (ok) tiles = std::move(loadedTiles);
    if (ok) {
        st = tmp;
        seed_ = seed;
        noise_ = Perlin(seed);
        chunks.clear();
        cacheChunk_ = nullptr;
    } else {
        edits_.clear();
    }
    return ok;
}
