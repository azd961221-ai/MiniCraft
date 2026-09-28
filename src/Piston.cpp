// Поршни (BlockPistonBase 1.0): выдвигаются от питания с любой стороны, кроме передней (и «сверху»),
// толкают до 12 блоков; растения, факелы, жидкости и т.п. при толчке ломаются. Липкий тянет блок назад.
// Раздатчик: по фронту сигнала выбрасывает случайный предмет (снаряды запускает игра).
#include <algorithm>
#include "World.h"

namespace {
const int D[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

bool breaksOnPush(uint8_t b) {
    return !isSolid(b) || isDoor(b) || b == BED || b == CAKE || b == CACTUS || b == PUMPKIN || b == JACK_O_LANTERN;
}
bool movable(uint8_t b, uint8_t m) {
    if (b == AIR || b == OBSIDIAN || b == BEDROCK || b == PISTON_HEAD || b == MOB_SPAWNER) return false;
    if (hasGui(b) || b == JUKEBOX || blockInfo(b).hardness < 0.f) return false;
    if (isPiston(b) && (m & 8)) return false;
    return true;
}
} // namespace

bool World::pistonPowered(int x, int y, int z, int facing) const {
    auto powerFrom = [&](int px, int py, int pz, int d) {
        int nx = px + D[d][0], ny = py + D[d][1], nz = pz + D[d][2];
        uint8_t n = getBlock(nx, ny, nz);
        if (isOpaque(n)) return blockPowerInto(nx, ny, nz) > 0;
        return weakPower(nx, ny, nz, d ^ 1) > 0;
    };
    for (int d = 0; d < 6; ++d)
        if (d != facing && powerFrom(x, y, z, d)) return true;
    // «Квазисвязность» 1.0: питание блока над поршнем тоже включает его
    for (int d = 0; d < 6; ++d)
        if (d != 3 && powerFrom(x, y + 1, z, d)) return true;
    return false;
}

bool World::pistonExtend(int x, int y, int z) {
    uint8_t b = getBlock(x, y, z), m = getMeta(x, y, z);
    int f = m & 7;
    glm::ivec3 dv(D[f][0], D[f][1], D[f][2]), p(x, y, z);
    std::vector<glm::ivec3> line;
    glm::ivec3 q = p + dv;
    for (int i = 0;; ++i) {
        if (q.y < 0 || q.y >= CH) return false;
        uint8_t qb = getBlock(q.x, q.y, q.z);
        if (qb == AIR || breaksOnPush(qb)) break;
        if (!movable(qb, getMeta(q.x, q.y, q.z)) || i >= 12) return false;
        line.push_back(q);
        q += dv;
    }
    // Ломаем то, что стоит в конце цепочки
    uint8_t endB = getBlock(q.x, q.y, q.z);
    if (endB != AIR) {
        if (!isLiquid(endB) && endB != FIRE) popped.push_back({q, endB, getMeta(q.x, q.y, q.z)});
        setBlock(q.x, q.y, q.z, AIR);
    }
    for (int i = (int)line.size() - 1; i >= 0; --i) {
        glm::ivec3 s = line[i];
        setBlock(s.x + dv.x, s.y + dv.y, s.z + dv.z, getBlock(s.x, s.y, s.z), getMeta(s.x, s.y, s.z));
    }
    setMeta(x, y, z, (uint8_t)(m | 8));
    setBlock(x + dv.x, y + dv.y, z + dv.z, PISTON_HEAD, (uint8_t)(f | (b == STICKY_PISTON ? 8 : 0)));
    soundEvents.push_back({glm::vec3(p) + 0.5f, "tile/piston/out"});
    return true;
}

void World::pistonRetract(int x, int y, int z) {
    uint8_t b = getBlock(x, y, z), m = getMeta(x, y, z);
    int f = m & 7;
    glm::ivec3 dv(D[f][0], D[f][1], D[f][2]), p(x, y, z), head = p + dv;
    setMeta(x, y, z, (uint8_t)(m & 7)); // сначала «задвинут», чтобы снятие головки не сломало основание
    if (getBlock(head.x, head.y, head.z) == PISTON_HEAD) setBlock(head.x, head.y, head.z, AIR);
    if (b == STICKY_PISTON) {
        glm::ivec3 s = head + dv;
        uint8_t sb = getBlock(s.x, s.y, s.z), sm = getMeta(s.x, s.y, s.z);
        if (s.y >= 0 && s.y < CH && movable(sb, sm) && !breaksOnPush(sb) && getBlock(head.x, head.y, head.z) == AIR) {
            setBlock(head.x, head.y, head.z, sb, sm);
            setBlock(s.x, s.y, s.z, AIR);
        }
    }
    soundEvents.push_back({glm::vec3(p) + 0.5f, "tile/piston/in"});
}

void World::dispense(int x, int y, int z) {
    TileEntity* te = tileAt(x, y, z);
    if (!te) return;
    int filled[9], n = 0;
    for (int i = 0; i < 9; ++i)
        if (!te->items[i].empty()) filled[n++] = i;
    glm::vec3 c = glm::vec3(x, y, z) + 0.5f;
    if (n == 0) { soundEvents.push_back({c, "random/click_fail"}); return; }
    ItemStack& s = te->items[filled[fireRand(n)]];
    ItemStack one = s;
    one.count = 1;
    if (--s.count == 0) s.clear();
    dispenseEvents.push_back({glm::ivec3(x, y, z), facingToDir(getMeta(x, y, z)), one});
}
