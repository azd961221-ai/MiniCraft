// Огонь (как BlockFire в Minecraft 1.0): горит, стареет (мета 0..15), перекидывается на горючие блоки,
// сжигает их, гаснет под дождём и без топлива. Динамит от огня взводится.
#include <algorithm>
#include "World.h"

namespace {

// chanceToEncourageFire / abilityToCatchFire из 1.0
int encouragement(uint8_t b) {
    switch (b) {
    case PLANKS: case FENCE: case WOOD_STAIRS: case SPRUCE_STAIRS: case BIRCH_STAIRS: case JUNGLE_STAIRS: case WOOD_SLAB: case DOUBLE_WOOD_SLAB: case LOG: return 5;
    case LEAVES: case BOOKSHELF: case WOOL: return 30;
    case TNT: return 15;
    case TALL_GRASS: return 60;
    default: return 0;
    }
}
int flammability(uint8_t b) {
    switch (b) {
    case PLANKS: case FENCE: case WOOD_STAIRS: case SPRUCE_STAIRS: case BIRCH_STAIRS: case JUNGLE_STAIRS: case WOOD_SLAB: case DOUBLE_WOOD_SLAB: case BOOKSHELF: return 20;
    case LOG: return 5;
    case LEAVES: case WOOL: return 60;
    case TNT: case TALL_GRASS: return 100;
    default: return 0;
    }
}

} // namespace

bool fireCanCatch(uint8_t b) { return encouragement(b) > 0; }

int World::fireRand(int n) {
    fireRng_ ^= fireRng_ << 13; fireRng_ ^= fireRng_ >> 17; fireRng_ ^= fireRng_ << 5;
    return n > 0 ? (int)(fireRng_ % (uint32_t)n) : 0;
}

bool World::rainsAt(int x, int y, int z) const {
    if (!raining || dimension_ != 0) return false;
    if (biomeInfo(loadedBiome(x, z)).dry) return false;
    return y >= topBlockY(x, z);
}

bool World::flammableNear(int x, int y, int z) const {
    return flammability(getBlock(x + 1, y, z)) > 0 || flammability(getBlock(x - 1, y, z)) > 0 ||
           flammability(getBlock(x, y + 1, z)) > 0 || flammability(getBlock(x, y - 1, z)) > 0 ||
           flammability(getBlock(x, y, z + 1)) > 0 || flammability(getBlock(x, y, z - 1)) > 0;
}

bool World::canPlaceFire(int x, int y, int z) const {
    return isOpaque(getBlock(x, y - 1, z)) || flammableNear(x, y, z);
}

void World::tryCatchFire(int x, int y, int z, int chance, int age) {
    uint8_t b = getBlock(x, y, z);
    int flam = flammability(b);
    if (fireRand(chance) >= flam) return;
    if (fireRand(age + 10) < 5 && !rainsAt(x, y, z)) setBlock(x, y, z, FIRE, (uint8_t)std::min(age + fireRand(5) / 4, 15));
    else setBlock(x, y, z, AIR);
    if (b == TNT) {
        if (getBlock(x, y, z) == TNT) setBlock(x, y, z, AIR);
        ignitedTnt.push_back({glm::ivec3(x, y, z), false});
    }
}

void World::updateFire(int x, int y, int z) {
    if (!canPlaceFire(x, y, z)) { setBlock(x, y, z, AIR); return; }
    if (getBlock(x, y - 1, z) == NETHERRACK) { scheduleUpdate(x, y, z, 40 + fireRand(10)); return; } // вечный огонь
    if (rainsAt(x, y, z) || rainsAt(x + 1, y, z) || rainsAt(x - 1, y, z) || rainsAt(x, y, z + 1) || rainsAt(x, y, z - 1)) {
        setBlock(x, y, z, AIR);
        return;
    }
    int age = getMeta(x, y, z);
    if (age < 15) {
        age = std::min(15, age + fireRand(3) / 2);
        Chunk* c = chunkAt(floorDiv(x, CW), floorDiv(z, CW));
        if (c) c->meta[Chunk::index(x - c->cx * CW, y, z - c->cz * CW)] = (uint8_t)age; // без перестройки света
    }
    scheduleUpdate(x, y, z, 40 + fireRand(10));
    if (!flammableNear(x, y, z)) {
        if (!isOpaque(getBlock(x, y - 1, z)) || age > 3) setBlock(x, y, z, AIR);
        return;
    }
    if (flammability(getBlock(x, y - 1, z)) == 0 && age == 15 && fireRand(4) == 0) {
        setBlock(x, y, z, AIR);
        return;
    }
    tryCatchFire(x + 1, y, z, 300, age);
    tryCatchFire(x - 1, y, z, 300, age);
    tryCatchFire(x, y - 1, z, 250, age);
    tryCatchFire(x, y + 1, z, 250, age);
    tryCatchFire(x, y, z - 1, 300, age);
    tryCatchFire(x, y, z + 1, 300, age);

    // Перекидывание на воздух рядом с горючим (выше — вероятнее)
    for (int i = x - 1; i <= x + 1; ++i)
        for (int k = z - 1; k <= z + 1; ++k)
            for (int j = y - 1; j <= y + 4; ++j) {
                if (i == x && j == y && k == z) continue;
                if (getBlock(i, j, k) != AIR) continue;
                int limit = 100 + (j > y + 1 ? (j - (y + 1)) * 100 : 0);
                int enc = 0;
                enc = std::max(enc, encouragement(getBlock(i + 1, j, k)));
                enc = std::max(enc, encouragement(getBlock(i - 1, j, k)));
                enc = std::max(enc, encouragement(getBlock(i, j - 1, k)));
                enc = std::max(enc, encouragement(getBlock(i, j + 1, k)));
                enc = std::max(enc, encouragement(getBlock(i, j, k - 1)));
                enc = std::max(enc, encouragement(getBlock(i, j, k + 1)));
                if (enc <= 0) continue;
                int c = (enc + 40) / (age + 30);
                if (c > 0 && fireRand(limit) <= c && !rainsAt(i, j, k))
                    setBlock(i, j, k, FIRE, (uint8_t)std::min(age + fireRand(5) / 4, 15));
            }
}
