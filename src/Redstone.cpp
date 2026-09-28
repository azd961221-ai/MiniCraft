// Редстоун как в Minecraft 1.0: слабое питание (в соседний блок-механизм) и сильное (в сплошной блок,
// который дальше питает механизмы вокруг себя). Пыль несёт силу 15..0, факел инвертирует,
// повторитель задерживает сигнал. Механизмы: двери, люки, динамит, нотные блоки.
#include <algorithm>
#include <deque>
#include "World.h"

namespace {
const int D[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
const int HD[4] = {0, 1, 4, 5}; // горизонтальные направления
} // namespace

void World::setMeta(int x, int y, int z, uint8_t m) {
    if (y < 0 || y >= CH) return;
    Chunk* c = chunkAt(floorDiv(x, CW), floorDiv(z, CW));
    if (!c) return;
    int lx = x - c->cx * CW, lz = z - c->cz * CW;
    int idx = Chunk::index(lx, y, lz);
    if (remote) {
        applyRemote(x, y, z, c->blocks[idx], m);
        if (sendEdit) sendEdit(x, y, z, c->blocks[idx], m, true);
        return;
    }
    c->meta[idx] = m;
    edits_[chunkKey(c->cx, c->cz)][idx] = (uint16_t)(c->blocks[idx] | (m << 8));
    markDirtyAt(x, z);
    if (onChange) onChange(x, y, z, c->blocks[idx], m);
}

// Соединяется ли пыль в клетке (x,y,z) с соседом по горизонтали d (для отрисовки и направления питания)
bool World::wireConnects(int x, int y, int z, int d) const {
    int nx = x + D[d][0], nz = z + D[d][2];
    uint8_t n = getBlock(nx, y, nz);
    if (n == REDSTONE_WIRE) return true;
    if (isRepeater(n)) {
        int f = sideToDir(getMeta(nx, y, nz) & 3);
        return f == d || f == (d ^ 1); // только спереди и сзади
    }
    if (isRedstoneTorch(n) || n == LEVER || n == STONE_BUTTON || n == STONE_PLATE || n == WOOD_PLATE || n == DETECTOR_RAIL) return true;
    if (!isOpaque(n) && getBlock(nx, y - 1, nz) == REDSTONE_WIRE) return true;           // вниз по ступеньке
    if (!isOpaque(getBlock(x, y + 1, z)) && getBlock(nx, y + 1, nz) == REDSTONE_WIRE) return true; // вверх
    return false;
}

// Слабое питание из блока (x,y,z) в соседа по направлению d
int World::weakPower(int x, int y, int z, int d) const {
    uint8_t b = getBlock(x, y, z);
    uint8_t m = getMeta(x, y, z);
    switch (b) {
    case REDSTONE_TORCH_ON: return d == supportDir(b, m) ? 0 : 15;
    case LEVER: case STONE_BUTTON: return (m & 8) ? 15 : 0;
    case STONE_PLATE: case WOOD_PLATE: return m ? 15 : 0;
    case DETECTOR_RAIL: return (m & 8) ? 15 : 0;
    case REPEATER_ON: return d == sideToDir(m & 3) ? 15 : 0;
    case REDSTONE_WIRE: {
        if (!wiresPower_ || m == 0) return 0;
        if (d == 3) return m; // в блок под собой
        if (d == 2) return 0;
        bool c[6] = {};
        bool any = false;
        for (int h : HD) any |= (c[h] = wireConnects(x, y, z, h));
        if (!any) return m; // точка питает во все стороны
        // Линия «смотрит» на соседа: соединена с противоположной стороны и не соединена вбок (1.0)
        bool side = (d == 0 || d == 1) ? (c[4] || c[5]) : (c[0] || c[1]);
        return (c[d ^ 1] && !side) ? m : 0;
    }
    default: return 0;
    }
}

// Сильное питание в сплошной блок-сосед по направлению d
int World::strongPower(int x, int y, int z, int d) const {
    uint8_t b = getBlock(x, y, z);
    uint8_t m = getMeta(x, y, z);
    switch (b) {
    case REDSTONE_TORCH_ON: return d == 2 ? 15 : 0;
    case LEVER: case STONE_BUTTON: return ((m & 8) && d == supportDir(b, m)) ? 15 : 0;
    case STONE_PLATE: case WOOD_PLATE: return (m && d == 3) ? 15 : 0;
    case DETECTOR_RAIL: return ((m & 8) && d == 3) ? 15 : 0;
    case REPEATER_ON: return d == sideToDir(m & 3) ? 15 : 0;
    case REDSTONE_WIRE: return wiresPower_ ? weakPower(x, y, z, d) : 0;
    default: return 0;
    }
}

// Насколько сильно запитан сплошной блок (x,y,z) своими соседями
int World::blockPowerInto(int x, int y, int z) const {
    int p = 0;
    for (int d = 0; d < 6; ++d) p = std::max(p, strongPower(x + D[d][0], y + D[d][1], z + D[d][2], d ^ 1));
    return p;
}

// Питание механизма в клетке: напрямую от соседей или через запитанные сплошные блоки
int World::inputPower(int x, int y, int z) const {
    int p = 0;
    for (int d = 0; d < 6; ++d) {
        int nx = x + D[d][0], ny = y + D[d][1], nz = z + D[d][2];
        uint8_t n = getBlock(nx, ny, nz);
        if (isOpaque(n)) p = std::max(p, blockPowerInto(nx, ny, nz));
        else p = std::max(p, weakPower(nx, ny, nz, d ^ 1));
    }
    return p;
}

int World::wireTarget(int x, int y, int z) const {
    wiresPower_ = false;
    int p = inputPower(x, y, z);
    wiresPower_ = true;
    if (p >= 15) return 15;
    bool upOpen = !isOpaque(getBlock(x, y + 1, z));
    for (int h : HD) {
        int nx = x + D[h][0], nz = z + D[h][2];
        uint8_t n = getBlock(nx, y, nz);
        if (n == REDSTONE_WIRE) p = std::max(p, getMeta(nx, y, nz) - 1);
        if (!isOpaque(n) && getBlock(nx, y - 1, nz) == REDSTONE_WIRE) p = std::max(p, getMeta(nx, y - 1, nz) - 1);
        if (upOpen && getBlock(nx, y + 1, nz) == REDSTONE_WIRE) p = std::max(p, getMeta(nx, y + 1, nz) - 1);
    }
    return std::max(p, 0);
}

// Пересчёт силы в связной сети пыли; меняющиеся клетки будят механизмы вокруг
void World::updateWires(const std::vector<glm::ivec3>& start) {
    std::deque<glm::ivec3> q(start.begin(), start.end());
    std::vector<glm::ivec3> changed;
    int budget = 20000;
    while (!q.empty() && budget-- > 0) {
        glm::ivec3 p = q.front();
        q.pop_front();
        if (getBlock(p.x, p.y, p.z) != REDSTONE_WIRE) continue;
        int t = wireTarget(p.x, p.y, p.z);
        if (t == getMeta(p.x, p.y, p.z)) continue;
        setMeta(p.x, p.y, p.z, (uint8_t)t);
        changed.push_back(p);
        for (int h : HD)
            for (int dy = -1; dy <= 1; ++dy) {
                glm::ivec3 n(p.x + D[h][0], p.y + dy, p.z + D[h][2]);
                if (getBlock(n.x, n.y, n.z) == REDSTONE_WIRE) q.push_back(n);
            }
    }
    for (auto& p : changed) updateComponentsNear(p);
}

void World::updateComponentsNear(const glm::ivec3& c) {
    for (int dy = -2; dy <= 2; ++dy)
        for (int dz = -2; dz <= 2; ++dz)
            for (int dx = -2; dx <= 2; ++dx) {
                if (std::abs(dx) + std::abs(dy) + std::abs(dz) > 2) continue;
                updateComponent(c.x + dx, c.y + dy, c.z + dz);
            }
}

void World::redstoneChanged(int x, int y, int z) {
    if (remote) return; // редстоун считает сервер
    if (inRedstone_ > 8) return; // защита от бесконечной рекурсии
    ++inRedstone_;
    std::vector<glm::ivec3> wires;
    for (int dy = -2; dy <= 2; ++dy)
        for (int dz = -2; dz <= 2; ++dz)
            for (int dx = -2; dx <= 2; ++dx) {
                if (std::abs(dx) + std::abs(dy) + std::abs(dz) > 2) continue;
                if (getBlock(x + dx, y + dy, z + dz) == REDSTONE_WIRE) wires.push_back({x + dx, y + dy, z + dz});
            }
    updateWires(wires);
    updateComponentsNear({x, y, z});
    --inRedstone_;
}

void World::updateComponent(int x, int y, int z) {
    uint8_t b = getBlock(x, y, z);
    if (b == AIR) return;
    int64_t key = posKey(x, y, z);
    switch (b) {
    case REDSTONE_TORCH_ON: case REDSTONE_TORCH_OFF: {
        int s = supportDir(b, getMeta(x, y, z));
        int ax = x + D[s][0], ay = y + D[s][1], az = z + D[s][2];
        bool powered = isOpaque(getBlock(ax, ay, az)) && blockPowerInto(ax, ay, az) > 0;
        if (powered == (b == REDSTONE_TORCH_ON)) scheduleUpdate(x, y, z, 2);
        break;
    }
    case REPEATER_ON: case REPEATER_OFF: {
        uint8_t m = getMeta(x, y, z);
        int f = sideToDir(m & 3), back = f ^ 1;
        int bx = x + D[back][0], bz = z + D[back][2];
        uint8_t n = getBlock(bx, y, bz);
        bool in = n == REDSTONE_WIRE ? getMeta(bx, y, bz) > 0
                : isOpaque(n)       ? blockPowerInto(bx, y, bz) > 0
                                    : weakPower(bx, y, bz, f) > 0;
        if (in != (b == REPEATER_ON)) scheduleUpdate(x, y, z, (((m >> 2) & 3) + 1) * 2);
        break;
    }
    case WOOD_DOOR: case IRON_DOOR: {
        uint8_t m = getMeta(x, y, z);
        int ly = (m & 8) ? y - 1 : y;
        int64_t k = posKey(x, ly, z);
        bool powered = inputPower(x, ly, z) > 0 || inputPower(x, ly + 1, z) > 0;
        bool was = poweredComps_.count(k) != 0;
        // Верхняя половина всегда повторяет нижнюю (направление и «открыта»)
        if (getBlock(x, ly, z) == b && getBlock(x, ly + 1, z) == b &&
            (getMeta(x, ly + 1, z) & 7) != (getMeta(x, ly, z) & 7))
            setMeta(x, ly + 1, z, (uint8_t)((getMeta(x, ly, z) & 7) | 8));
        if (powered == was) break;
        if (powered) poweredComps_.insert(k);
        else poweredComps_.erase(k);
        uint8_t lm = getMeta(x, ly, z);
        bool open = lm & 4;
        if (open != powered && getBlock(x, ly, z) == b) {
            lm = (uint8_t)((lm & 3) | (powered ? 4 : 0));
            setMeta(x, ly, z, lm);
            if (getBlock(x, ly + 1, z) == b) setMeta(x, ly + 1, z, (uint8_t)(lm | 8));
            soundEvents.push_back({glm::vec3(x + 0.5f, ly + 0.5f, z + 0.5f), powered ? "random/door_open" : "random/door_close"});
        }
        break;
    }
    case TRAPDOOR: {
        bool powered = inputPower(x, y, z) > 0;
        bool was = poweredComps_.count(key) != 0;
        if (powered == was) break;
        if (powered) poweredComps_.insert(key);
        else poweredComps_.erase(key);
        uint8_t m = getMeta(x, y, z);
        if (((m & 4) != 0) != powered) {
            setMeta(x, y, z, (uint8_t)((m & 3) | (powered ? 4 : 0)));
            soundEvents.push_back({glm::vec3(x + 0.5f, y + 0.5f, z + 0.5f), powered ? "random/door_open" : "random/door_close"});
        }
        break;
    }
    case POWERED_RAIL:
        updatePoweredRails(x, y, z);
        break;
    case RAIL:
        updateRailShape(x, y, z, false);
        break;
    case TNT:
        if (inputPower(x, y, z) > 0) {
            setBlock(x, y, z, AIR);
            ignitedTnt.push_back({glm::ivec3(x, y, z), false});
        }
        break;
    case PISTON: case STICKY_PISTON: {
        uint8_t m = getMeta(x, y, z);
        if (pistonPowered(x, y, z, m & 7) != ((m & 8) != 0)) scheduleUpdate(x, y, z, 1);
        break;
    }
    case DISPENSER: {
        bool powered = inputPower(x, y, z) > 0 || inputPower(x, y + 1, z) > 0;
        bool was = poweredComps_.count(key) != 0;
        if (powered == was) break;
        if (powered) {
            poweredComps_.insert(key);
            scheduleUpdate(x, y, z, 4);
        } else {
            poweredComps_.erase(key);
        }
        break;
    }
    case REDSTONE_LAMP_OFF: // BlockRedstoneLight 1.4.2: загорается сразу, гаснет через 4 тика
        if (inputPower(x, y, z) > 0) setBlock(x, y, z, REDSTONE_LAMP_ON);
        break;
    case REDSTONE_LAMP_ON:
        if (inputPower(x, y, z) == 0) scheduleUpdate(x, y, z, 4);
        break;
    case NOTE_BLOCK: {
        bool powered = inputPower(x, y, z) > 0;
        bool was = poweredComps_.count(key) != 0;
        if (powered == was) break;
        if (powered) {
            poweredComps_.insert(key);
            noteEvents.push_back(glm::ivec3(x, y, z));
        } else {
            poweredComps_.erase(key);
        }
        break;
    }
    default: break;
    }
}

// Прямая линия соединённых энергорельс через (x,y,z) — по порядку, не дальше 16 в каждую сторону
std::vector<glm::ivec3> World::poweredRailLine(int x, int y, int z) const {
    auto alongX = [](uint8_t m) { int s = m & 7; return s == 1 || s == 2 || s == 3; };
    bool ax = alongX(getMeta(x, y, z));
    std::deque<glm::ivec3> line{glm::ivec3(x, y, z)};
    for (int dir : {-1, 1}) {
        glm::ivec3 c(x, y, z);
        for (int i = 0; i < 16; ++i) {
            glm::ivec3 n = c + (ax ? glm::ivec3(dir, 0, 0) : glm::ivec3(0, 0, dir));
            bool found = false;
            for (int dy : {0, 1, -1}) {
                glm::ivec3 q = n + glm::ivec3(0, dy, 0);
                if (getBlock(q.x, q.y, q.z) == POWERED_RAIL && alongX(getMeta(q.x, q.y, q.z)) == ax) { c = q; found = true; break; }
            }
            if (!found) break;
            if (dir < 0) line.push_front(c);
            else line.push_back(c);
        }
    }
    return std::vector<glm::ivec3>(line.begin(), line.end());
}

// Рельса запитана, если источник питания есть у неё или у энергорельсы той же линии не дальше 8
void World::updatePoweredRails(int x, int y, int z) {
    std::vector<glm::ivec3> line = poweredRailLine(x, y, z);
    std::vector<int> sources;
    for (int i = 0; i < (int)line.size(); ++i) {
        const glm::ivec3& q = line[i];
        if (inputPower(q.x, q.y, q.z) > 0 || inputPower(q.x, q.y + 1, q.z) > 0) sources.push_back(i);
    }
    for (int i = 0; i < (int)line.size(); ++i) {
        bool on = false;
        for (int s : sources) on |= std::abs(s - i) <= 8;
        const glm::ivec3& q = line[i];
        uint8_t m = getMeta(q.x, q.y, q.z);
        if (on != ((m & 8) != 0)) setMeta(q.x, q.y, q.z, (uint8_t)((m & 7) | (on ? 8 : 0)));
    }
}

// Запланированный тик: переключение факела, повторителя, отпускание кнопки
void World::redstoneTick(int x, int y, int z) {
    uint8_t b = getBlock(x, y, z);
    uint8_t m = getMeta(x, y, z);
    switch (b) {
    case REDSTONE_TORCH_ON: case REDSTONE_TORCH_OFF: {
        int s = supportDir(b, m);
        int ax = x + D[s][0], ay = y + D[s][1], az = z + D[s][2];
        bool powered = isOpaque(getBlock(ax, ay, az)) && blockPowerInto(ax, ay, az) > 0;
        uint8_t want = powered ? REDSTONE_TORCH_OFF : REDSTONE_TORCH_ON;
        if (want != b) {
            setBlock(x, y, z, want, m);
            redstoneChanged(x, y, z);
            redstoneChanged(x, y + 1, z);
        }
        break;
    }
    case REPEATER_ON: case REPEATER_OFF: {
        int f = sideToDir(m & 3), back = f ^ 1;
        int bx = x + D[back][0], bz = z + D[back][2];
        uint8_t n = getBlock(bx, y, bz);
        bool in = n == REDSTONE_WIRE ? getMeta(bx, y, bz) > 0
                : isOpaque(n)       ? blockPowerInto(bx, y, bz) > 0
                                    : weakPower(bx, y, bz, f) > 0;
        uint8_t want = in ? REPEATER_ON : REPEATER_OFF;
        if (want != b) {
            setBlock(x, y, z, want, m);
            int fx = x + D[f][0], fz = z + D[f][2];
            redstoneChanged(x, y, z);
            redstoneChanged(fx, y, fz);
        }
        break;
    }
    case PISTON: case STICKY_PISTON: {
        bool want = pistonPowered(x, y, z, m & 7);
        if (want && !(m & 8)) pistonExtend(x, y, z);
        else if (!want && (m & 8)) pistonRetract(x, y, z);
        break;
    }
    case DISPENSER:
        dispense(x, y, z);
        break;
    case REDSTONE_LAMP_ON:
        if (inputPower(x, y, z) == 0) setBlock(x, y, z, REDSTONE_LAMP_OFF);
        break;
    case STONE_BUTTON:
        if (m & 8) {
            setMeta(x, y, z, (uint8_t)(m & 7));
            soundEvents.push_back({glm::vec3(x + 0.5f, y + 0.5f, z + 0.5f), "random/click"});
            redstoneChanged(x, y, z);
            int s = supportDir(b, m);
            redstoneChanged(x + D[s][0], y + D[s][1], z + D[s][2]);
        }
        break;
    default: break;
    }
}
