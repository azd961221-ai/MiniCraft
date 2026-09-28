#include "Mob.h"
#include <queue>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "Crafting.h"
#include "Physics.h"
#include "Saves.h"
#include "Potion.h"

namespace {

const float PI = 3.14159265f;

uint32_t xr(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
int rint(uint32_t& s, int n) { return n <= 0 ? 0 : (int)(xr(s) % (uint32_t)n); }
float rfl(uint32_t& s) { return (xr(s) & 0xFFFFFF) / float(0xFFFFFF); }
float rgauss(uint32_t& s) {
    float u1 = std::max(rfl(s), 1e-6f), u2 = rfl(s);
    return std::sqrt(-2.f * std::log(u1)) * std::cos(2.f * PI * u2);
}

float wrapDeg(float a) {
    while (a > 180.f) a -= 360.f;
    while (a < -180.f) a += 360.f;
    return a;
}

// ---- Столкновения коробки моба с блоками (как у игрока, но с размерами моба)

bool boxCollides(const World& w, const glm::vec3& p, float hw, float h) { return anyCollision(w, bodyBox(p, hw, h)); }

bool boxInBlock(const World& w, const glm::vec3& p, float hw, float h, uint8_t block) {
    int x0 = (int)std::floor(p.x - hw + 0.001f), x1 = (int)std::floor(p.x + hw - 0.001f);
    int y0 = (int)std::floor(p.y + 0.4f), y1 = (int)std::floor(p.y + std::max(0.5f, h - 0.4f));
    int z0 = (int)std::floor(p.z - hw + 0.001f), z1 = (int)std::floor(p.z + hw - 0.001f);
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x)
                if (w.getBlock(x, y, z) == block) return true;
    return false;
}

void moveMob(const World& w, Mob& m) {
    const MobDef& d = mobDef(m.type);
    BodyState st;
    st.onGround = m.onGround;
    moveBody(w, m.pos, mobHalfW(m), mobHeight(m), m.motion, 0.5f, false, st);
    m.onGround = st.onGround;
    m.collidedH = st.collidedH;
}

// Видно ли точку b из точки a (шагаем по лучу и проверяем непрозрачные блоки)
bool canSee(const World& w, glm::vec3 a, glm::vec3 b) {
    glm::vec3 d = b - a;
    float len = glm::length(d);
    int n = (int)(len / 0.3f) + 1;
    for (int i = 1; i < n; ++i) {
        glm::vec3 p = a + d * ((float)i / n);
        if (isOpaque(w.getBlock((int)std::floor(p.x), (int)std::floor(p.y), (int)std::floor(p.z)))) return false;
    }
    return true;
}

// Пересечение отрезка с коробкой (slab-тест); возвращает долю пути t
bool segmentBox(const glm::vec3& o, const glm::vec3& d, const glm::vec3& mn, const glm::vec3& mx, float& t) {
    float t0 = 0.f, t1 = 1.f;
    for (int i = 0; i < 3; ++i) {
        if (std::abs(d[i]) < 1e-7f) {
            if (o[i] < mn[i] || o[i] > mx[i]) return false;
        } else {
            float a = (mn[i] - o[i]) / d[i], b = (mx[i] - o[i]) / d[i];
            if (a > b) std::swap(a, b);
            t0 = std::max(t0, a);
            t1 = std::min(t1, b);
            if (t0 > t1) return false;
        }
    }
    t = t0;
    return true;
}

// Сопротивление взрыву (blast resistance / 5, как в Explosion 1.0)
float blastResistance(uint8_t b) {
    switch (b) {
    case AIR: return 0.f;
    case BEDROCK: return 1e7f;
    case OBSIDIAN: return 1200.f;
    case WATER: case LAVA: return 60.f;
    case STONE: case COBBLE: case BRICK: case STONE_BRICK: case MOSSY_COBBLE: return 6.f;
    case PLANKS: return 3.f;
    case COBBLE_STAIRS: case BRICK_STAIRS: case STONEBRICK_STAIRS: case SLAB: case DOUBLE_SLAB: case IRON_BARS: return 6.f;
    case IRON_DOOR: return 5.f;
    default: {
        float h = blockInfo(b).hardness;
        return h < 0 ? 1e7f : h;
    }
    }
}

std::string slimeSound(const Mob& m) {
    std::string base = m.type == MobType::MagmaCube ? "mob/magmacube/" : "mob/slime/";
    return base + (m.size > 1 ? "big" : "small");
}

const char* hurtSound(MobType t) {
    switch (t) {
    case MobType::Pig: return "mob/pig/say";
    case MobType::Cow: return "mob/cow/hurt";
    case MobType::Sheep: return "mob/sheep/say";
    case MobType::Chicken: return "mob/chicken/hurt";
    case MobType::Zombie: return "mob/zombie/hurt";
    case MobType::Skeleton: return "mob/skeleton/hurt";
    case MobType::Spider: return "mob/spider/say";
    case MobType::Creeper: return "mob/creeper/say";
    case MobType::Wolf: return "mob/wolf/hurt";
    case MobType::Enderman: return "mob/endermen/hit";
    case MobType::Silverfish: return "mob/silverfish/hit";
    case MobType::CaveSpider: return "mob/spider/say";
    case MobType::Mooshroom: return "mob/cow/hurt";
    case MobType::PigZombie: return "mob/zombiepig/zpighurt";
    case MobType::Ghast: return "mob/ghast/scream";
    case MobType::Blaze: return "mob/blaze/hit";
    case MobType::EnderDragon: return "mob/enderdragon/hit";
    case MobType::Villager: return "damage/hit"; // своих звуков у жителя в 1.0 нет — общий «hurtflesh»
    case MobType::WitherSkeleton: return "mob/skeleton/hurt";
    case MobType::Wither: return "mob/wither/hurt";
    case MobType::Witch: return "damage/hit";
    case MobType::Bat: return "mob/bat/hurt";
    case MobType::IronGolem: return "mob/irongolem/hit";
    case MobType::Ocelot: case MobType::Cat: return "mob/cat/hitt";
    case MobType::ZombieVillager: return "mob/zombie/hurt";
    default: return "";
    }
}

const char* deathSound(MobType t) {
    switch (t) {
    case MobType::Pig: return "mob/pig/death";
    case MobType::Cow: return "mob/cow/hurt";
    case MobType::Sheep: return "mob/sheep/say";
    case MobType::Chicken: return "mob/chicken/hurt";
    case MobType::Zombie: return "mob/zombie/death";
    case MobType::Skeleton: return "mob/skeleton/death";
    case MobType::Spider: return "mob/spider/death";
    case MobType::Creeper: return "mob/creeper/death";
    case MobType::Wolf: return "mob/wolf/death";
    case MobType::Enderman: return "mob/endermen/death";
    case MobType::Silverfish: return "mob/silverfish/kill";
    case MobType::CaveSpider: return "mob/spider/death";
    case MobType::Mooshroom: return "mob/cow/hurt";
    case MobType::PigZombie: return "mob/zombiepig/zpigdeath";
    case MobType::Ghast: return "mob/ghast/death";
    case MobType::Blaze: return "mob/blaze/death";
    case MobType::Villager: return "damage/hit";
    case MobType::WitherSkeleton: return "mob/skeleton/death";
    case MobType::Wither: return "mob/wither/death";
    case MobType::Witch: return "damage/hit";
    case MobType::Bat: return "mob/bat/death";
    case MobType::IronGolem: return "mob/irongolem/death";
    case MobType::Ocelot: case MobType::Cat: return "mob/cat/hitt";
    case MobType::ZombieVillager: return "mob/zombie/death";
    default: return "";
    }
}

// Голос моба (раз в несколько секунд)
std::string livingSound(const Mob& m, uint32_t& rng) {
    switch (m.type) {
    case MobType::Wolf:
        if (m.angry) return "mob/wolf/growl";
        if (m.tamed && m.health < 10) return "mob/wolf/whine";
        return rint(rng, 3) == 0 ? "mob/wolf/panting" : "mob/wolf/bark";
    case MobType::Enderman: return m.angry ? "mob/endermen/scream" : "mob/endermen/idle";
    case MobType::Silverfish: return "mob/silverfish/say";
    case MobType::CaveSpider: return "mob/spider/say";
    case MobType::Mooshroom: return "mob/cow/say";
    case MobType::PigZombie: return m.angry ? "mob/zombiepig/zpigangry" : "mob/zombiepig/zpig";
    case MobType::Ghast: return "mob/ghast/moan";
    case MobType::Blaze: return "mob/blaze/breathe";
    case MobType::EnderDragon: return "mob/enderdragon/growl";
    case MobType::Wither: return "mob/wither/idle";
    case MobType::Bat: return "mob/bat/idle";
    case MobType::Ocelot: return rint(rng, 4) == 0 ? "mob/cat/purr" : "mob/cat/meow";
    case MobType::Cat: return rint(rng, 4) == 0 ? "mob/cat/purreow" : "mob/cat/meow";
    case MobType::WitherSkeleton: return "mob/skeleton/say";
    case MobType::ZombieVillager: return "mob/zombie/say";
    case MobType::Creeper: case MobType::Slime: case MobType::MagmaCube: case MobType::Squid: case MobType::SnowGolem:
    case MobType::Villager: case MobType::EnderCrystal: case MobType::IronGolem: case MobType::Witch: return "";
    default: return std::string(mobDef(m.type).soundDir) + "/say";
    }
}

// Дроп при смерти (1.4.2); burning — свинина выпадает жареной
std::vector<ItemStack> mobDrops(const Mob& m, uint32_t& rng) {
    std::vector<ItemStack> d;
    if (m.growingAge < 0) return d; // детёныши ничего не роняют
    struct LootBoost {
        std::vector<ItemStack>& d; int lvl; uint32_t& rng;
        ~LootBoost() {
            if (lvl <= 0) return;
            for (auto& s : d)
                if (maxStackSize(s.id) > 1) s.count = (uint8_t)std::min(64, s.count + rint(rng, lvl + 1));
        }
    } boost{d, m.looting, rng};
    if (m.saddled) d.push_back(makeStack(SADDLE));
    auto add = [&](uint16_t id, int n) { if (n > 0) d.push_back(makeStack(id, n)); };
    switch (m.type) {
    case MobType::Pig: add(m.fireTicks > 0 ? COOKED_PORKCHOP : RAW_PORKCHOP, rint(rng, 3)); break;
    case MobType::Cow: add(LEATHER, rint(rng, 3)); add(RAW_BEEF, 1 + rint(rng, 3)); break;
    case MobType::Sheep: if (!m.sheared) d.push_back(makeStack(WOOL, 1, (uint16_t)m.color)); break;
    case MobType::Chicken: add(FEATHER, rint(rng, 3)); add(RAW_CHICKEN, 1); break;
    case MobType::Zombie:
        add(ROTTEN_FLESH, rint(rng, 3));
        if (m.playerHitTicks > 0 && rint(rng, 40) == 0) {
            uint16_t rare[3] = {IRON_INGOT, CARROT, POTATO};
            add(rare[rint(rng, 3)], 1);
        }
        break;
    case MobType::Skeleton: add(ARROW, rint(rng, 3)); add(BONE, rint(rng, 3)); break;
    case MobType::Spider: case MobType::CaveSpider:
        add(STRING, rint(rng, 3));
        if (m.playerHitTicks > 0 && rint(rng, 3) == 0) add(SPIDER_EYE, 1);
        break;
    case MobType::Squid: d.push_back(makeStack(DYE, 1 + rint(rng, 3), DYE_INK)); break;
    case MobType::Slime: if (m.size == 1) add(SLIME_BALL, rint(rng, 3)); break;
    case MobType::MagmaCube: if (m.size > 1) add(MAGMA_CREAM, rint(rng, 2)); break;
    case MobType::Enderman: add(ENDER_PEARL, rint(rng, 2)); break;
    case MobType::Mooshroom: add(LEATHER, rint(rng, 3)); add(RAW_BEEF, 1 + rint(rng, 3)); break;
    case MobType::SnowGolem: add(SNOWBALL, rint(rng, 16)); break;
    case MobType::PigZombie: add(ROTTEN_FLESH, rint(rng, 2)); add(GOLD_NUGGET, rint(rng, 2)); break;
    case MobType::Ghast: add(GHAST_TEAR, rint(rng, 2)); add(GUNPOWDER, rint(rng, 3)); break;
    case MobType::Blaze: if (m.playerHitTicks > 0) add(BLAZE_ROD, rint(rng, 2)); break;
    case MobType::Creeper:
        add(GUNPOWDER, rint(rng, 3));
        if (m.recordDrop) add((uint16_t)(RECORD_13 + rint(rng, 11)), 1);
        break;
    case MobType::WitherSkeleton:
        add(BONE, rint(rng, 3));
        add(263 /* COAL */, rint(rng, 2));
        if (m.playerHitTicks > 0 && rint(rng, std::max(1, 40 - m.looting * 5)) == 0)
            d.push_back(makeStack(SKULL_ITEM, 1, 1)); // череп скелета-иссушителя
        break;
    case MobType::Wither:
        d.push_back(makeStack(NETHER_STAR, 1)); // Звезда Незера всегда выпадает
        break;
    case MobType::Witch:
        for (int i = 0; i < 1 + rint(rng, 3); ++i) {
            static const uint16_t WDROPS[7] = {GLASS_BOTTLE, GLOWSTONE_DUST, GUNPOWDER, REDSTONE, SPIDER_EYE, STICK, SUGAR};
            add(WDROPS[rint(rng, 7)], 1 + rint(rng, 2));
        }
        break;
    case MobType::IronGolem:
        add(IRON_INGOT, 3 + rint(rng, 3));
        add(ROSE, rint(rng, 3));
        break;
    case MobType::ZombieVillager:
        add(ROTTEN_FLESH, rint(rng, 3));
        if (m.playerHitTicks > 0 && rint(rng, 40) == 0) {
            uint16_t rare[3] = {IRON_INGOT, CARROT, POTATO};
            add(rare[rint(rng, 3)], 1);
        }
        break;
    default: break;
    }
    return d;
}

bool isUndead(MobType t) {
    return t == MobType::Zombie || t == MobType::Skeleton || t == MobType::PigZombie ||
           t == MobType::WitherSkeleton || t == MobType::Wither || t == MobType::ZombieVillager;
}

} // namespace

const MobDef& mobDef(MobType t) {
    static const MobDef DEFS[(int)MobType::COUNT] = {
        {"Pig", 0.45f, 0.9f, 10, 0.7f, false, "mob/pig", 2},
        {"Cow", 0.45f, 1.3f, 10, 0.7f, false, "mob/cow", 2},
        {"Sheep", 0.45f, 1.3f, 8, 0.7f, false, "mob/sheep", 2},
        {"Chicken", 0.2f, 0.7f, 4, 0.7f, false, "mob/chicken", 1},
        {"Zombie", 0.3f, 1.8f, 20, 0.5f, true, "mob/zombie", 5},
        {"Skeleton", 0.3f, 1.8f, 20, 0.7f, true, "mob/skeleton", 5},
        {"Spider", 0.7f, 0.9f, 16, 0.8f, true, "mob/spider", 5},
        {"Creeper", 0.3f, 1.8f, 20, 0.7f, true, "mob/creeper", 5},
        {"Wolf", 0.3f, 0.8f, 8, 1.0f, false, "mob/wolf", 3},
        {"Squid", 0.475f, 0.95f, 10, 0.7f, false, "mob/squid", 3},
        {"Slime", 0.3f, 0.6f, 1, 0.7f, true, "mob/slime", 1},
        {"Enderman", 0.3f, 2.9f, 40, 0.7f, false, "mob/endermen", 5},
        {"Silverfish", 0.15f, 0.7f, 8, 0.6f, true, "mob/silverfish", 5},
        {"Cave Spider", 0.35f, 0.5f, 12, 0.8f, true, "mob/spider", 5},
        {"Mooshroom", 0.45f, 1.3f, 10, 0.7f, false, "mob/cow", 2},
        {"Snow Golem", 0.35f, 1.8f, 4, 0.5f, false, "mob/snowgolem", 0},
        {"Villager", 0.3f, 1.8f, 20, 0.5f, false, "mob/villager", 0},
        {"Zombie Pigman", 0.3f, 1.8f, 20, 0.5f, false, "mob/zombiepig", 5},
        {"Ghast", 2.0f, 4.0f, 10, 0.7f, true, "mob/ghast", 5},
        {"Blaze", 0.3f, 1.8f, 20, 0.7f, true, "mob/blaze", 10},
        {"Magma Cube", 0.3f, 0.6f, 1, 0.7f, true, "mob/magmacube", 1},
        {"Ender Dragon", 4.0f, 4.0f, 200, 1.0f, true, "mob/enderdragon", 0},
        {"Ender Crystal", 1.0f, 2.0f, 1, 0.f, false, "", 0},
        {"Wither Skeleton", 0.35f, 2.4f, 20, 0.7f, true, "mob/skeleton", 5},
        {"Wither", 0.45f, 3.5f, 300, 0.6f, true, "mob/wither", 50},
        {"Witch", 0.3f, 1.8f, 26, 0.5f, true, "mob/villager", 5},
        {"Bat", 0.25f, 0.9f, 6, 0.6f, false, "mob/bat", 0},
        {"Iron Golem", 0.7f, 2.7f, 100, 0.5f, false, "mob/irongolem", 0},
        {"Ocelot", 0.3f, 0.7f, 10, 0.8f, false, "mob/cat", 1},
        {"Cat", 0.3f, 0.7f, 10, 0.8f, false, "mob/cat", 1},
        {"Zombie Villager", 0.3f, 1.8f, 20, 0.5f, true, "mob/zombie", 5},
    };
    return DEFS[(int)t];
}

// ---------------------------------------------------------------- Появление

Mob& MobManager::spawn(MobType t, const glm::vec3& pos, float yaw) {
    Mob m;
    m.type = t;
    m.pos = m.prev = pos;
    m.yaw = m.prevYaw = yaw;
    m.health = mobDef(t).maxHealth;
    m.id = nextId++;
    if (t == MobType::Sheep) {
        // Окрас как в 1.0: белые ~82%, по 5% чёрных/серых/светло-серых, 3% коричневых, редко розовые
        uint32_t h = hash32(m.id * 2654435761u + (uint32_t)(int)(pos.x * 7 + pos.z * 13));
        int r = (int)(h % 100000);
        m.color = r < 5000 ? 15 : r < 10000 ? 7 : r < 15000 ? 8 : r < 18000 ? 12 : r < 18164 ? 6 : 0;
    }
    if (t == MobType::Villager) m.color = (int)(hash32(m.id * 40503u + 11u) % 5u);
    if (t == MobType::Slime || t == MobType::MagmaCube) {
        m.size = 1 << (int)(hash32(m.id * 7919u + 17u) % 3u);
        m.scale = (float)m.size;
        m.health = m.size * m.size;
    }
    // 5–10 минут до первого яйца (раньше отрицательный остаток давал яйцо сразу после появления)
    m.eggTimer = 6000 + (int)(hash32((uint32_t)(int)std::floor(pos.x) * 131u ^ (uint32_t)(int)std::floor(pos.z) * 17u) % 6000u);
    if (inTick_) { pending.push_back(m); return pending.back(); }
    mobs.push_back(m);
    return mobs.back();
}

void MobManager::populateChunk(World& w, int cx, int cz, uint32_t& rng) {
    int64_t key = chunkKey(cx, cz);
    if (!populated.insert(key).second) return;
    if (w.dimension() != 0) return; // в Незере и Крае животных нет
    for (auto it = w.villagerSpawns.begin(); it != w.villagerSpawns.end();) {
        if (floorDiv((int)std::floor(it->x), CW) == cx && floorDiv((int)std::floor(it->z), CW) == cz) {
            Mob& v = spawn(MobType::Villager, glm::vec3(*it), rfl(rng) * 360.f);
            if (it->w >= 0.f) v.color = (int)it->w; // профессия по постройке
            it = w.villagerSpawns.erase(it);
        } else {
            ++it;
        }
    }
    Biome bio = w.loadedBiome(cx * CW + 8, cz * CW + 8);
    // Спруты в океанах и реках
    if ((bio == Biome::Ocean || bio == Biome::River || bio == Biome::FrozenOcean || bio == Biome::FrozenRiver) && rint(rng, 3) == 0) {
        for (int i = 0; i < 1 + rint(rng, 3); ++i) {
            int x = cx * CW + rint(rng, CW), z = cz * CW + rint(rng, CW);
            int y = SEA - 2 - rint(rng, 6);
            if (w.getBlock(x, y, z) == WATER && w.getBlock(x, y + 1, z) == WATER) spawn(MobType::Squid, glm::vec3(x + 0.5f, (float)y, z + 0.5f), rfl(rng) * 360.f);
        }
    }
    if (rint(rng, 10) != 0) return; // в 1.0 шанс стада на чанк при генерации — 10%
    // Веса видов как в 1.0: овца 12, свинья 10, курица 10, корова 8; волки 5 в лесу и 8 в тайге
    bool mushroom = bio == Biome::MushroomIsland || bio == Biome::MushroomShore;
    int wolfW = bio == Biome::Forest ? 5 : bio == Biome::Taiga ? 8 : 0;
    int r = rint(rng, 40 + wolfW);
    MobType t = r < 12 ? MobType::Sheep : r < 22 ? MobType::Pig : r < 32 ? MobType::Chicken : r < 40 ? MobType::Cow : MobType::Wolf;
    if (mushroom) t = MobType::Mooshroom;
    int placed = 0;
    for (int tries = 0; tries < 12 && placed < 4; ++tries) {
        int x = cx * CW + rint(rng, CW), z = cz * CW + rint(rng, CW);
        int y = CH - 2;
        while (y > 1 && !isSolid(w.getBlock(x, y, z))) --y;
        uint8_t ground = w.getBlock(x, y, z);
        if (ground != GRASS && !(mushroom && ground == MYCELIUM)) continue;
        if (isSolid(w.getBlock(x, y + 1, z)) || isSolid(w.getBlock(x, y + 2, z))) continue;
        spawn(t, glm::vec3(x + 0.5f, y + 1.f, z + 0.5f), rfl(rng) * 360.f);
        ++placed;
    }
}

void MobManager::spawnPassive(World& w, const Player& p, bool animals, bool water, uint32_t& rng) {
    if (w.dimension() != 0 || (!animals && !water)) return;
    const int R = 8;
    int pcx = floorDiv((int)std::floor(p.pos.x), CW), pcz = floorDiv((int)std::floor(p.pos.z), CW);
    int eligible = 0;
    for (int dz = -R; dz <= R; ++dz)
        for (int dx = -R; dx <= R; ++dx) eligible += w.isChunkLoaded(pcx + dx, pcz + dz);
    auto isAnimal = [](MobType t) {
        return t == MobType::Pig || t == MobType::Cow || t == MobType::Sheep || t == MobType::Chicken || t == MobType::Wolf ||
               t == MobType::Mooshroom;
    };
    // Считаем только тех, кто рядом (в списке лежат и мобы давно выгруженных чанков)
    int nAnimals = 0, nSquid = 0;
    for (auto& m : mobs) {
        if (std::abs(m.pos.x - p.pos.x) > R * CW + 8 || std::abs(m.pos.z - p.pos.z) > R * CW + 8) continue;
        nAnimals += isAnimal(m.type);
        nSquid += m.type == MobType::Squid;
    }
    const int animalCap = 15 * eligible / 256, squidCap = 5 * eligible / 256;
    for (int dz = -R; dz <= R; ++dz)
        for (int dx = -R; dx <= R; ++dx) {
            int cx = pcx + dx, cz = pcz + dz;
            if (!w.isChunkLoaded(cx, cz)) continue;
            int x = cx * CW + rint(rng, CW), z = cz * CW + rint(rng, CW);
            if (animals && nAnimals < animalCap) {
                int y = CH - 2;
                while (y > 1 && w.getBlock(x, y, z) == AIR) --y;
                uint8_t ground = w.getBlock(x, y, z);
                Biome bio = w.loadedBiome(x, z);
                bool mushroom = bio == Biome::MushroomIsland || bio == Biome::MushroomShore;
                if ((ground == GRASS || (mushroom && ground == MYCELIUM)) && glm::length(glm::vec3(x + 0.5f, (float)y, z + 0.5f) - p.pos) >= 24.f &&
                    std::max(w.getSkyLight(x, y + 1, z), w.getBlockLight(x, y + 1, z)) > 8) {
                    int wolfW = bio == Biome::Forest ? 5 : bio == Biome::Taiga ? 8 : 0;
                    int r = rint(rng, 40 + wolfW);
                    MobType t = r < 12 ? MobType::Sheep : r < 22 ? MobType::Pig : r < 32 ? MobType::Chicken : r < 40 ? MobType::Cow : MobType::Wolf;
                    if (mushroom) t = MobType::Mooshroom;
                    for (int k = 0; k < 4 && nAnimals < animalCap; ++k) {
                        int px = x + rint(rng, 6) - rint(rng, 6), pz = z + rint(rng, 6) - rint(rng, 6);
                        int py = CH - 2;
                        while (py > 1 && !isSolid(w.getBlock(px, py, pz))) --py;
                        uint8_t g = w.getBlock(px, py, pz);
                        if ((g != GRASS && !(mushroom && g == MYCELIUM)) || isSolid(w.getBlock(px, py + 1, pz)) || isSolid(w.getBlock(px, py + 2, pz)) ||
                            isLiquid(w.getBlock(px, py + 1, pz)))
                            continue;
                        spawn(t, glm::vec3(px + 0.5f, py + 1.f, pz + 0.5f), rfl(rng) * 360.f);
                        ++nAnimals;
                    }
                }
            }
            if (water && nSquid < squidCap) {
                int y = 46 + rint(rng, 17); // 46..62, как у спрута в 1.0
                if (w.getBlock(x, y, z) == WATER && w.getBlock(x, y + 1, z) == WATER &&
                    glm::length(glm::vec3(x + 0.5f, (float)y, z + 0.5f) - p.pos) >= 24.f) {
                    spawn(MobType::Squid, glm::vec3(x + 0.5f, (float)y, z + 0.5f), rfl(rng) * 360.f);
                    ++nSquid;
                }
            }
        }
}

void MobManager::strikeLightning(World& w, const glm::vec3& at, Player& p0, TickEvents& pev, const MobHooks& hooks, uint32_t& rng) {
    std::vector<Player*> victims = netPlayers;
    if (victims.empty()) victims.push_back(&p0);
    for (Player* vp : victims) {
        Player& q = *vp;
        if (q.dead || glm::length(q.pos - at) >= 3.f) continue;
        q.damageSource = 1;
        if (scaleDamage(5) > 0 && hurtPlayer(q, 5, pev)) q.fireTicks = std::max(q.fireTicks, 160);
    }
    for (auto& m : mobs) {
        if (m.dying() || glm::length(m.pos - at) >= 3.f) continue;
        if (m.type == MobType::Creeper) { m.charged = true; continue; } // заряженный крипер
        if (m.type == MobType::Pig) {                                  // свинья -> свинозомби
            m.type = MobType::PigZombie;
            m.health = mobDef(m.type).maxHealth;
            m.saddled = false;
            m.growingAge = 0;
            continue;
        }
        if (!isFireImmune(m.type)) m.fireTicks = std::max(m.fireTicks, 160);
        m.invulnerable = 0;
        hurt(m, 5, at, 0.f, false, hooks);
    }
    // Огонь в точке удара и рядом (1.0: на нормальной и сложной сложности)
    if (difficulty >= 2) {
        glm::ivec3 b((int)std::floor(at.x), (int)std::floor(at.y), (int)std::floor(at.z));
        for (int i = 0; i < 5; ++i) {
            glm::ivec3 q = i == 0 ? b : b + glm::ivec3(rint(rng, 3) - 1, rint(rng, 3) - 1, rint(rng, 3) - 1);
            if (w.getBlock(q.x, q.y, q.z) == AIR && w.canPlaceFire(q.x, q.y, q.z)) w.setBlock(q.x, q.y, q.z, FIRE);
        }
    }
}

int MobManager::countHostile() const {
    int n = 0;
    for (auto& m : mobs) n += countsAsMonster(m.type);
    return n;
}

int MobManager::countPassive() const { return (int)mobs.size() - countHostile(); }

void MobManager::addSpawner(const glm::ivec3& p, uint8_t meta) {
    int64_t key = posKey(p.x, p.y, p.z);
    if (spawners.count(key)) return;
    Spawner s;
    s.x = p.x; s.y = p.y; s.z = p.z;
    s.type = meta == 1 ? MobType::Skeleton : meta == 2 ? MobType::Spider : meta == 3 ? MobType::CaveSpider
           : meta == 4 ? MobType::Blaze : meta == 5 ? MobType::Silverfish : MobType::Zombie;
    spawners[key] = s;
}

void MobManager::tickSpawners(World& w, const Player& p, std::vector<Particle>& particles, uint32_t& rng) {
    for (auto it = spawners.begin(); it != spawners.end();) {
        Spawner& s = it->second;
        if (!w.isChunkLoaded(floorDiv(s.x, CW), floorDiv(s.z, CW))) { ++it; continue; }
        if (w.getBlock(s.x, s.y, s.z) != MOB_SPAWNER) { it = spawners.erase(it); continue; }
        ++it;
        glm::vec3 c(s.x + 0.5f, s.y + 0.5f, s.z + 0.5f);
        s.prevSpin = s.spin;
        // Спавнер работает, только если игрок ближе 16 блоков
        bool near = glm::length(p.pos - c) <= 16.f;
        for (Player* q : netPlayers) near |= glm::length(q->pos - c) <= 16.f;
        if (!near) continue;
        s.spin += 1000.f / (s.delay + 200.f);
        spawnSmoke(particles, c + glm::vec3(rfl(rng) - 0.5f, rfl(rng) - 0.5f, rfl(rng) - 0.5f), false);
        spawnSmoke(particles, c + glm::vec3(rfl(rng) - 0.5f, rfl(rng) - 0.5f, rfl(rng) - 0.5f), true);
        if (--s.delay > 0) continue;
        s.delay = 200 + rint(rng, 600);
        int nearby = 0;
        for (auto& m : mobs)
            if (m.type == s.type && std::abs(m.pos.x - c.x) < 8.5f && std::abs(m.pos.y - c.y) < 4.5f && std::abs(m.pos.z - c.z) < 8.5f) ++nearby;
        const MobDef& d = mobDef(s.type);
        for (int i = 0; i < 4 && nearby < 6; ++i) {
            glm::vec3 pos(s.x + 0.5f + (rfl(rng) - rfl(rng)) * 4.f, (float)(s.y + rint(rng, 3) - 1), s.z + 0.5f + (rfl(rng) - rfl(rng)) * 4.f);
            glm::ivec3 b((int)std::floor(pos.x), (int)std::floor(pos.y), (int)std::floor(pos.z));
            if (!isSolid(w.getBlock(b.x, b.y - 1, b.z)) || boxCollides(w, pos, d.halfWidth, d.height)) continue;
            if (std::max(w.getSkyLight(b.x, b.y, b.z), w.getBlockLight(b.x, b.y, b.z)) > rint(rng, 8) + 3) continue;
            spawn(s.type, glm::vec3(b.x + 0.5f, (float)b.y, b.z + 0.5f), rfl(rng) * 360.f);
            spawnPoof(particles, glm::vec3(b.x + 0.5f, b.y + 0.9f, b.z + 0.5f), 12, 1.f);
            ++nearby;
        }
    }
}

void MobManager::spawnHostiles(World& w, const Player& p, int skySub, int renderDistance, uint32_t& rng) {
    // SpawnerAnimals 1.0: чанки в радиусе 8 от игрока; лимит монстров 70 на 256 чанков
    (void)renderDistance;
    if (difficulty == 0) return;
    const int R = 8;
    int pcx = floorDiv((int)std::floor(p.pos.x), CW), pcz = floorDiv((int)std::floor(p.pos.z), CW);
    int eligible = 0;
    for (int dz = -R; dz <= R; ++dz)
        for (int dx = -R; dx <= R; ++dx) eligible += w.isChunkLoaded(pcx + dx, pcz + dz);
    int cap = 70 * eligible / 256;
    int hostile = countHostile();
    if (hostile > cap) return;
    const int dim = w.dimension();
    // Вид по списку биома (веса 1.4.2): обычный мир — паук, зомби, зомби-житель, скелет, крипер, слизень, эндермен, ведьма, мышь;
    // Незер — свинозомби, гаст, скелет-иссушитель, ифрит, лавовый куб; Край — эндермен; Джунгли — оцелот
    auto pickType = [&](Biome b, int py) -> int {
        if (dim == -1) {
            int r = rint(rng, 100);
            if (r < 40) return (int)MobType::PigZombie;
            if (r < 65) return (int)MobType::Ghast;
            if (r < 85) return (int)MobType::WitherSkeleton;
            if (r < 95) return (int)MobType::Blaze;
            return (int)MobType::MagmaCube;
        }
        if (dim == 1) return (int)MobType::Enderman;
        if (b == Biome::MushroomIsland || b == Biome::MushroomShore) return -1;
        if (isJungleBiome(b) && py >= 63 && rint(rng, 6) == 0) return (int)MobType::Ocelot;
        if (py < 63 && rint(rng, 10) == 0) return (int)MobType::Bat;
        int r = rint(rng, 100);
        if (r < 18) return (int)MobType::Spider;
        if (r < 38) return (rint(rng, 20) == 0) ? (int)MobType::ZombieVillager : (int)MobType::Zombie;
        if (r < 58) return (int)MobType::Skeleton;
        if (r < 76) return (int)MobType::Creeper;
        if (r < 86) return (int)MobType::Slime;
        if (r < 94) return (int)MobType::Enderman;
        return (int)MobType::Witch;
    };
    auto normalCube = [&](int x, int y, int z) { uint8_t b = w.getBlock(x, y, z); return isOpaque(b) && isSolid(b); };
    for (int dz = -R; dz <= R && hostile <= cap; ++dz)
        for (int dx = -R; dx <= R && hostile <= cap; ++dx) {
            int cx = pcx + dx, cz = pcz + dz;
            if (!w.isChunkLoaded(cx, cz)) continue;
            int x = cx * CW + rint(rng, CW), y = rint(rng, CH), z = cz * CW + rint(rng, CW);
            if (normalCube(x, y, z) || w.getBlock(x, y, z) != AIR) continue;
            int spawned = 0;
            for (int pack = 0; pack < 3; ++pack) {
                int px = x, py = y, pz = z, type = -2;
                for (int tries = 0; tries < 4; ++tries) {
                    px += rint(rng, 6) - rint(rng, 6);
                    pz += rint(rng, 6) - rint(rng, 6);
                    // Место: снизу целый блок (не бедрок), сам блок и над ним — не целые и не жидкость
                    uint8_t below = w.getBlock(px, py - 1, pz);
                    if (!normalCube(px, py - 1, pz) || below == BEDROCK) continue;
                    if (normalCube(px, py, pz) || isLiquid(w.getBlock(px, py, pz)) || normalCube(px, py + 1, pz)) continue;
                    glm::vec3 pos(px + 0.5f, (float)py, pz + 0.5f);
                    if (glm::length(pos - p.pos) < 24.f) continue;
                    if (type == -2) type = pickType(w.loadedBiome(px, pz), py);
                    if (type < 0) break;
                    MobType t = (MobType)type;
                    const MobDef& d = mobDef(t);
                    if (boxCollides(w, pos, d.halfWidth, d.height) || boxInBlock(w, pos, d.halfWidth, d.height, WATER) || boxInBlock(w, pos, d.halfWidth, d.height, LAVA)) continue;
                    // getCanSpawnHere
                    int sky = w.getSkyLight(px, py, pz);
                    int light = std::max(sky - skySub, w.getBlockLight(px, py, pz));
                    if (t == MobType::Slime) {
                        bool slimeChunk = hash32((uint32_t)floorDiv(px, CW) * 4987142u + (uint32_t)floorDiv(pz, CW) * 5947611u + w.seed()) % 10u == 0;
                        if (!(slimeChunk && py < 40 && rint(rng, 10) == 0)) continue;
                    } else if (t == MobType::Ghast) {
                        if (rint(rng, 20) != 0) continue;
                    } else if (dim == 0) {
                        if (sky > rint(rng, 32) || light > rint(rng, 8)) continue; // тёмное место
                    }
                    Mob& m = spawn(t, pos, rfl(rng) * 360.f);
                    if (t == MobType::Slime || t == MobType::MagmaCube) {
                        int sz = 1 << rint(rng, 3);
                        m.size = sz;
                        m.scale = (float)sz;
                        m.health = sz * sz;
                    }
                    ++hostile;
                    // В чанке не больше 4 за раз (гаст — 1)
                    if (++spawned >= (t == MobType::Ghast ? 1 : 4)) { pack = 3; break; }
                }
            }
        }
}

void MobManager::despawn(const Player& p, World& w, float skyFactor) {
    // EntityLiving.despawnEntity 1.0: монстр дальше 128 блоков исчезает сразу; «постаревший» (600+) дальше 32 —
    // с шансом 1/800 за тик; рядом с игроком возраст сбрасывается. Животные, жители и приручённые не исчезают
    for (auto& m : mobs) {
        // Спруты у нас появляются только при генерации чанка, поэтому не исчезают (иначе океаны опустеют)
        bool canDespawn = (countsAsMonster(m.type) && m.type != MobType::EnderCrystal) || m.type == MobType::Squid; // спруты досыпаются
        if (!canDespawn || m.dying()) continue;
        glm::ivec3 c((int)std::floor(m.pos.x), (int)std::floor(m.pos.y + 0.5f), (int)std::floor(m.pos.z));
        float bright = std::max(w.getSkyLight(c.x, c.y, c.z) * skyFactor, (float)w.getBlockLight(c.x, c.y, c.z)) / 15.f;
        m.despawnAge += bright > 0.5f ? 2 : 1;
        float d = glm::length(m.pos - p.pos);
        for (Player* q : netPlayers) d = std::min(d, glm::length(m.pos - q->pos));
        if (d > 128.f) { m.removed = true; continue; }
        if (m.despawnAge > 600 && rint(rngDespawn_, 800) == 0) {
            if (d < 32.f) m.despawnAge = 0;
            else m.removed = true;
        }
    }
}

// ---------------------------------------------------------------- Урон

bool MobManager::hurt(Mob& m, int dmg, const glm::vec3& from, float knockback, bool byPlayer, const MobHooks& hooks) {
    if (m.dying() || m.invulnerable > 0 || dmg < 0) return false; // 0 — только отбрасывание (снежок)
    if (m.type == MobType::EnderCrystal) {
        // Кристалл от любого удара взрывается (сила 6)
        m.removed = true;
        crystalBlasts.push_back(m.pos + glm::vec3(0, 1.f, 0));
        return true;
    }
    m.health -= dmg;
    m.hurtTime = 10;
    m.invulnerable = 10;
    if (byPlayer) { m.playerHitTicks = 100; playerTargetId = m.id; if (currentAttacker) m.lastAttacker = currentAttacker; }
    bool neutral = m.type == MobType::Wolf || m.type == MobType::Enderman || m.type == MobType::PigZombie;
    // Животные убегают (EntityAnimal 1.0); житель — EntityCreature, он не убегает
    if (!mobDef(m.type).hostile && !neutral && m.type != MobType::SnowGolem && m.type != MobType::Villager) m.fleeTicks = 60;
    if (isSpiderLike(m.type)) m.angry = true;
    if (m.type == MobType::Wolf) {
        if (m.tamed) m.sitting = false;
        else if (byPlayer) // стая злится вместе
            for (auto& o : mobs)
                if (o.type == MobType::Wolf && !o.tamed && glm::length(o.pos - m.pos) < 16.f) o.angry = true;
    }
    if (m.type == MobType::PigZombie && byPlayer)
        for (auto& o : mobs)
            if (o.type == MobType::PigZombie && glm::length(o.pos - m.pos) < 32.f) {
                o.angry = true;
                o.angerTicks = 400 + (int)(hash32(o.id * 31u + (uint32_t)m.age) % 400u);
            }
    if (m.type == MobType::Enderman) { if (byPlayer) m.angry = true; m.attackCounter = -1; } // -1: телепорт в тике
    if (knockback > 0.f) {
        glm::vec2 d(m.pos.x - from.x, m.pos.z - from.z);
        float len = glm::length(d);
        if (len > 1e-4f) d /= len;
        m.motion.x = m.motion.x / 2 + d.x * 0.4f * knockback;
        m.motion.z = m.motion.z / 2 + d.y * 0.4f * knockback;
        m.motion.y = std::min(0.4f, m.motion.y / 2 + 0.4f);
    }
    glm::vec3 sp = m.pos + glm::vec3(0, mobHeight(m) * 0.5f, 0);
    if (m.health <= 0) {
        m.deathTime = 0;
        if (m.type == MobType::Slime || m.type == MobType::MagmaCube) hooks.sound(slimeSound(m), 1.f, 1.f, &sp);
        else hooks.sound(deathSound(m.type), 1.f, 1.f, &sp);
    } else {
        if (m.type == MobType::Slime || m.type == MobType::MagmaCube) hooks.sound(slimeSound(m), 1.f, 1.f, &sp);
        else hooks.sound(hurtSound(m.type), 1.f, 1.f, &sp);
    }
    return true;
}

Mob* MobManager::raycast(const glm::vec3& o, const glm::vec3& d, float maxDist, float& dist) {
    Mob* best = nullptr;
    float bestT = 2.f;
    for (auto& m : mobs) {
        if (m.dying()) continue;
        const MobDef& def = mobDef(m.type);
        glm::vec3 mn = m.pos - glm::vec3(mobHalfW(m) + 0.1f, 0.f, mobHalfW(m) + 0.1f);
        glm::vec3 mx = m.pos + glm::vec3(mobHalfW(m) + 0.1f, mobHeight(m) + 0.1f, mobHalfW(m) + 0.1f);
        float t;
        if (segmentBox(o, d * maxDist, mn, mx, t) && t < bestT) { bestT = t; best = &m; }
    }
    dist = bestT * maxDist;
    return best;
}

void MobManager::tickFalling(World& w, std::vector<ItemEntity>& items, uint32_t& rng) {
    for (auto& [p, b] : w.fallingStarts) {
        FallingBlock f;
        f.pos = f.prev = glm::vec3(p.x + 0.5f, (float)p.y, p.z + 0.5f);
        f.block = b;
        falling.push_back(f);
    }
    w.fallingStarts.clear();
    for (FallingBlock& f : falling) {
        if (f.dead) continue;
        f.prev = f.pos;
        ++f.age;
        f.motion.y -= 0.04f;
        BodyState st;
        st.onGround = f.onGround;
        moveBody(w, f.pos, 0.49f, 0.98f, f.motion, 0.f, false, st);
        f.onGround = st.onGround;
        f.motion *= 0.98f;
        if (f.onGround) {
            // Приземлился: встаёт блоком, если клетка свободна и под ней опора; иначе выпадает предметом
            glm::ivec3 c((int)std::floor(f.pos.x), (int)std::floor(f.pos.y + 0.01f), (int)std::floor(f.pos.z));
            uint8_t at = w.getBlock(c.x, c.y, c.z), below = w.getBlock(c.x, c.y - 1, c.z);
            bool fallsFurther = below == AIR || below == FIRE || isLiquid(below);
            if (isReplaceable(at) && !fallsFurther) w.setBlock(c.x, c.y, c.z, f.block);
            else dropFromBlock(items, c, makeStack(f.block), rng);
            f.dead = true;
        } else if (f.age > 100 && f.pos.y < 0.f) {
            f.dead = true; // упал в пустоту
        } else if (f.age > 600) {
            dropFromBlock(items, glm::ivec3((int)std::floor(f.pos.x), (int)std::floor(f.pos.y), (int)std::floor(f.pos.z)), makeStack(f.block), rng);
            f.dead = true;
        }
    }
    falling.erase(std::remove_if(falling.begin(), falling.end(), [](const FallingBlock& f) { return f.dead; }), falling.end());
}

// Картины проверяют стену: пропала опора или место заняли — падают предметом
void MobManager::checkPaintings(World& w, std::vector<ItemEntity>& items, uint32_t& rng) {
    for (Painting& pt : paintings)
        if (!pt.dead && w.isChunkLoaded(floorDiv(pt.wall.x, CW), floorDiv(pt.wall.z, CW)) && !paintingFits(pt, w, paintings)) {
            pt.dead = true;
            glm::vec3 c = pt.center();
            dropFromBlock(items, glm::ivec3((int)std::floor(c.x), (int)std::floor(c.y), (int)std::floor(c.z)), makeStack(PAINTING), rng);
        }
    paintings.erase(std::remove_if(paintings.begin(), paintings.end(), [](const Painting& q) { return q.dead; }), paintings.end());
}

bool findPath(const World& w, const glm::vec3& from, const glm::vec3& to, float height, int maxDist, std::vector<glm::ivec3>& out) {
    out.clear();
    const int tall = height > 1.f ? 2 : 1;
    auto passable = [&](int x, int y, int z) {
        uint8_t b = w.getBlock(x, y, z);
        return !isSolid(b) && b != LAVA && b != FIRE && b != CACTUS;
    };
    auto walkable = [&](int x, int y, int z) {
        if (y < 1 || y >= CH - 2) return false;
        for (int k = 0; k < tall; ++k)
            if (!passable(x, y + k, z)) return false;
        if (w.getBlock(x, y, z) == WATER) return true; // плывёт
        uint8_t bl = w.getBlock(x, y - 1, z);
        return isSolid(bl) && bl != CACTUS && bl != FENCE && bl != NETHER_FENCE && bl != FENCE_GATE; // забор выше прыжка
    };
    glm::ivec3 s((int)std::floor(from.x), (int)std::floor(from.y + 0.01f), (int)std::floor(from.z));
    glm::ivec3 g((int)std::floor(to.x), (int)std::floor(to.y + 0.01f), (int)std::floor(to.z));
    for (int k = 0; k < 3 && !walkable(s.x, s.y, s.z); ++k) --s.y; // в прыжке — от клетки под ногами
    if (!walkable(s.x, s.y, s.z)) return false;
    auto key = [](const glm::ivec3& p) { return posKey(p.x, p.y, p.z); };
    auto hdist = [&](const glm::ivec3& p) { glm::vec3 d(p - g); return glm::length(d); };
    struct Rec { glm::ivec3 p; int cost; int64_t parent; bool closed; };
    std::unordered_map<int64_t, Rec> nodes;
    using QE = std::pair<float, int64_t>;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> open;
    nodes[key(s)] = {s, 0, INT64_MIN, false};
    open.push({hdist(s), key(s)});
    int64_t best = key(s);
    float bestH = hdist(s);
    int expanded = 0;
    while (!open.empty() && expanded < 1500) {
        int64_t k = open.top().second;
        open.pop();
        Rec& r = nodes[k];
        if (r.closed) continue;
        r.closed = true;
        ++expanded;
        glm::ivec3 p = r.p;
        float h = hdist(p);
        if (h < bestH) { bestH = h; best = k; }
        if (p == g) break;
        static const int D[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (auto& d : D) {
            glm::ivec3 q(p.x + d[0], p.y, p.z + d[1]);
            int step = 1;
            if (walkable(q.x, q.y, q.z)) {
            } else if (walkable(q.x, q.y + 1, q.z) && passable(p.x, p.y + tall, p.z)) {
                ++q.y; step = 2; // запрыгнуть на блок
            } else {
                bool into = true;
                for (int t = 0; t < tall; ++t) into = into && passable(q.x, q.y + t, q.z);
                if (!into) continue;
                bool landed = false;
                for (int fall = 1; fall <= 3; ++fall) { // спрыгнуть не выше 3 блоков
                    if (walkable(q.x, q.y - fall, q.z)) { q.y -= fall; step = 1 + fall; landed = true; break; }
                    if (!passable(q.x, q.y - fall, q.z)) break;
                }
                if (!landed) continue;
            }
            if (std::abs(q.x - s.x) > maxDist || std::abs(q.z - s.z) > maxDist || std::abs(q.y - s.y) > maxDist) continue;
            int64_t qk = key(q);
            int cost = r.cost + step;
            auto it = nodes.find(qk);
            if (it != nodes.end() && (it->second.closed || it->second.cost <= cost)) continue;
            nodes[qk] = {q, cost, k, false};
            open.push({cost + hdist(q), qk});
        }
    }
    for (int64_t k = best; k != INT64_MIN;) {
        const Rec& r = nodes[k];
        if (r.parent == INT64_MIN) break; // стартовая клетка не нужна
        out.push_back(r.p);
        k = r.parent;
    }
    std::reverse(out.begin(), out.end());
    return !out.empty();
}

void MobManager::tickOrbs(World& w, Player& p0, const MobHooks& hooks, uint32_t& rng) {
    std::vector<Player*> ps = netPlayers;
    if (ps.empty()) ps.push_back(&p0);
    for (Player* q : ps)
        if (q->xpCooldown > 0) --q->xpCooldown;
    for (XpOrb& o : orbs) {
        if (o.dead) continue;
        if (!w.isChunkLoaded(floorDiv((int)std::floor(o.pos.x), CW), floorDiv((int)std::floor(o.pos.z), CW))) { o.prev = o.pos; continue; }
        Player* best = nullptr;
        float bd = 8.f;
        for (Player* q : ps) {
            if (q->dead) continue;
            float d = glm::length(q->eye() - o.pos);
            if (d < bd) { bd = d; best = q; }
        }
        glm::vec3 eye = best ? best->eye() : glm::vec3(0.f);
        if (!moveXpOrb(o, w, best ? &eye : nullptr, rng)) {
            hooks.sound("random/fizz", 0.4f, 2.f + rfl(rng) * 0.4f, &o.pos);
            continue;
        }
        if (o.dead) continue;
        // Касание: коробка игрока, расширенная на 1 блок по горизонтали (onLivingUpdate 1.0)
        for (Player* q : ps) {
            if (q->dead || q->xpCooldown > 0) continue;
            glm::vec3 d = o.pos - q->pos;
            if (std::abs(d.x) > PLAYER_HALF_W + 1.f + 0.25f || std::abs(d.z) > PLAYER_HALF_W + 1.f + 0.25f || d.y < -0.5f || d.y > PLAYER_H) continue;
            q->xpCooldown = 2;
            hooks.sound("random/orb", 0.1f, 0.5f * ((rfl(rng) - rfl(rng)) * 0.7f + 1.8f), &o.pos);
            if (!netPlayers.empty() && hooks.addXpTo) hooks.addXpTo(*q, o.value);
            else if (hooks.addXp) hooks.addXp(o.value);
            o.dead = true;
            break;
        }
    }
    orbs.erase(std::remove_if(orbs.begin(), orbs.end(), [](const XpOrb& o) { return o.dead; }), orbs.end());
}

Player* MobManager::nearestPlayer(const glm::vec3& pos, Player& p0, bool vulnerableOnly) {
    if (netPlayers.empty()) return &p0;
    Player* best = nullptr;
    float bd = 1e18f;
    for (int pass = 0; pass < 2 && !best; ++pass)
        for (Player* q : netPlayers) {
            if (pass == 0 && vulnerableOnly && (q->dead || q->creative())) continue;
            float d = glm::length(q->pos - pos);
            if (d < bd) { bd = d; best = q; }
        }
    return best ? best : &p0;
}

Player* MobManager::playerById(uint32_t id, Player& p0) {
    for (Player* q : netPlayers) if (q->netId == id) return q;
    return &p0;
}

bool MobManager::deflectFireball(const glm::vec3& eye, const glm::vec3& look, float maxDist) {
    Fireball* best = nullptr;
    float bestT = maxDist;
    for (auto& f : fireballs) {
        float r = f.small ? 0.3125f : 0.5f, t;
        if (f.dead || !segmentBox(eye, look * maxDist, f.pos - glm::vec3(r), f.pos + glm::vec3(r), t)) continue;
        if (t * maxDist < bestT) { bestT = t * maxDist; best = &f; }
    }
    if (!best) return false;
    // EntityFireball.attackEntityFrom: скорость и ускорение — по взгляду ударившего
    glm::vec3 d = glm::normalize(look);
    best->motion = d;
    best->accel = d * 0.1f;
    best->deflected = true;
    best->age = 0;
    return true;
}

Arrow& MobManager::shootArrow(const glm::vec3& from, const glm::vec3& dir, float speed, float inaccuracy, bool fromPlayer,
                              bool critical, uint32_t& rng) {
    Arrow a;
    glm::vec3 d = glm::normalize(dir);
    d += glm::vec3(rgauss(rng), rgauss(rng), rgauss(rng)) * 0.0075f * inaccuracy;
    a.motion = d * speed;
    a.pos = a.prev = from;
    a.dir = glm::normalize(a.motion);
    a.fromPlayer = fromPlayer;
    a.critical = critical;
    arrows.push_back(a);
    return arrows.back();
}

void MobManager::igniteTnt(const glm::ivec3& b, bool fromExplosion, uint32_t& rng) {
    PrimedTnt t;
    t.pos = t.prev = glm::vec3(b) + glm::vec3(0.5f, 0.f, 0.5f);
    float a = rfl(rng) * 6.2831853f;
    t.motion = glm::vec3(-std::sin(a) * 0.02f, 0.2f, -std::cos(a) * 0.02f);
    t.fuse = fromExplosion ? rint(rng, 20) + 10 : 80;
    tnts.push_back(t);
}

bool MobManager::interact(Mob& m, const ItemStack& held, Player& p, std::vector<ItemEntity>& items, std::vector<Particle>& particles,
                          const MobHooks& hooks, uint32_t& rng, int& consume, ItemStack& replace) {
    consume = 0;
    replace = {};
    if (m.dying()) return false;
    glm::vec3 c = m.pos + glm::vec3(0, mobHeight(m) * 0.8f, 0);
    auto hearts = [&]() {
        for (int i = 0; i < 7; ++i) spawnHeart(particles, c + glm::vec3(rfl(rng) - 0.5f, rfl(rng) * 0.5f, rfl(rng) - 0.5f));
    };
    auto smokes = [&]() {
        for (int i = 0; i < 7; ++i) spawnSmoke(particles, c + glm::vec3(rfl(rng) - 0.5f, rfl(rng) * 0.5f, rfl(rng) - 0.5f), false);
    };
    bool meat = held.id == RAW_PORKCHOP || held.id == COOKED_PORKCHOP || held.id == RAW_BEEF || held.id == STEAK ||
                held.id == RAW_CHICKEN || held.id == COOKED_CHICKEN || held.id == ROTTEN_FLESH;
    if (m.type == MobType::Wolf) {
        if (!m.tamed && !m.angry && held.id == BONE) {
            consume = 1;
            if (rint(rng, 3) == 0) {
                m.tamed = true;
                m.sitting = true;
                m.health = 20;
                hearts();
            } else {
                smokes();
            }
            return true;
        }
        if (m.tamed && meat) {
            if (m.health < 20) {
                m.health = std::min(20, m.health + foodInfo(held.id).hunger);
                consume = 1;
                return true;
            }
            if (m.growingAge == 0 && m.inLove == 0) { m.inLove = 600; consume = 1; hearts(); return true; }
        }
        if (m.tamed && held.id == DYE) {
            m.color = 15 - (held.damage & 15); // Окрашивание ошейника волка (1.4.2)
            consume = 1;
            return true;
        }
        if (m.tamed) { m.sitting = !m.sitting; m.hasWander = false; m.motion = glm::vec3(0.f); return true; }
        return false;
    }
    if (m.type == MobType::Ocelot && held.id == RAW_FISH) {
        consume = 1;
        if (rint(rng, 3) == 0) {
            m.type = MobType::Cat;
            m.color = rint(rng, 3); // 0=черный/смокинг, 1=рыжий, 2=сиамский
            m.tamed = true;
            m.sitting = true;
            m.health = 10;
            hearts();
        } else {
            smokes();
        }
        return true;
    }
    if (m.type == MobType::Cat) {
        if (held.id == RAW_FISH && m.growingAge == 0 && m.inLove == 0) {
            m.inLove = 600;
            consume = 1;
            hearts();
            return true;
        }
        if (m.tamed && (held.id == 0 || held.id == AIR)) {
            m.sitting = !m.sitting;
            m.hasWander = false;
            m.motion = glm::vec3(0.f);
            return true;
        }
    }
    if (m.type == MobType::ZombieVillager && held.id == GOLDEN_APPLE) {
        m.type = MobType::Villager;
        m.health = 20;
        spawnPoof(particles, c, 15, 1.f);
        hooks.sound("random/fizz", 1.f, 1.f, &c);
        consume = 1;
        return true;
    }
    if (m.type == MobType::Pig && (held.id == CARROT || held.id == WHEAT_ITEM) && m.growingAge == 0 && m.inLove == 0) {
        m.inLove = 600;
        consume = 1;
        hearts();
        return true;
    }
    if (m.type == MobType::Chicken && (held.id == SEEDS || held.id == PUMPKIN_SEEDS || held.id == MELON_SEEDS || held.id == WHEAT_ITEM) && m.growingAge == 0 && m.inLove == 0) {
        m.inLove = 600;
        consume = 1;
        hearts();
        return true;
    }
    if ((m.type == MobType::Cow || m.type == MobType::Sheep || m.type == MobType::Mooshroom) && held.id == WHEAT_ITEM && m.growingAge == 0 && m.inLove == 0) {
        m.inLove = 600;
        consume = 1;
        hearts();
        return true;
    }
    if (m.type == MobType::Sheep && held.id == DYE && !m.sheared && m.color != 15 - (held.damage & 15)) {
        m.color = 15 - (held.damage & 15);
        consume = 1;
        return true;
    }
    if (m.type == MobType::Mooshroom && held.id == SHEARS && m.growingAge >= 0) {
        m.type = MobType::Cow;
        glm::ivec3 b((int)std::floor(m.pos.x), (int)std::floor(m.pos.y + 1.f), (int)std::floor(m.pos.z));
        for (int i = 0; i < 5; ++i) dropFromBlock(items, b, makeStack(RED_MUSHROOM), rng);
        spawnPoof(particles, c, 10, 1.f);
        hooks.sound("mob/sheep/shear", 1.f, 1.f, &c);
        return true;
    }
    if (m.type == MobType::Mooshroom && held.id == BOWL) {
        consume = 1;
        replace = makeStack(MUSHROOM_STEW);
        return true;
    }
    if (m.type == MobType::Pig && held.id == SADDLE && !m.saddled && m.growingAge >= 0) {
        m.saddled = true;
        consume = 1;
        return true;
    }
    return false;
}

void MobManager::throwItem(const glm::vec3& from, const glm::vec3& dir, uint16_t item, uint32_t& rng, uint16_t damage) {
    Throwable t;
    glm::vec3 d = glm::normalize(dir);
    d += glm::vec3(rgauss(rng), rgauss(rng), rgauss(rng)) * 0.0075f;
    t.motion = d * 1.5f;
    t.pos = t.prev = from;
    t.item = item;
    t.damage = damage;
    if (item == POTION) t.motion *= 0.5f / 1.5f; // взрывное зелье летит медленнее (0.5)
    throwables.push_back(t);
}

// ---------------------------------------------------------------- Взрыв (Explosion 1.0)

void MobManager::explode(World& w, const glm::vec3& c, float power, Player& p, TickEvents& pev, std::vector<ItemEntity>& items,
                         std::vector<Particle>& particles, const MobHooks& hooks, uint32_t& rng, bool incendiary) {
    // Лучи из центра к поверхности куба 16x16x16; каждый гаснет, пробивая блоки
    std::unordered_set<int64_t> seen;
    std::vector<glm::ivec3> destroyed;
    for (int i = 0; i < 16; ++i)
        for (int j = 0; j < 16; ++j)
            for (int k = 0; k < 16; ++k) {
                if (i != 0 && i != 15 && j != 0 && j != 15 && k != 0 && k != 15) continue;
                glm::vec3 d = glm::normalize(glm::vec3(i / 15.f * 2 - 1, j / 15.f * 2 - 1, k / 15.f * 2 - 1));
                float intensity = power * (0.7f + rfl(rng) * 0.6f);
                glm::vec3 pos = c;
                while (intensity > 0.f) {
                    glm::ivec3 b((int)std::floor(pos.x), (int)std::floor(pos.y), (int)std::floor(pos.z));
                    uint8_t id = w.getBlock(b.x, b.y, b.z);
                    if (id != AIR) intensity -= (blastResistance(id) + 0.3f) * 0.3f;
                    if (intensity > 0.f && id != AIR && seen.insert(posKey(b.x, b.y, b.z)).second) destroyed.push_back(b);
                    pos += d * 0.3f;
                    intensity -= 0.225f;
                }
            }

    // Урон и отбрасывание сущностей: зависит от расстояния и того, сколько лучей до них доходит
    auto exposure = [&](const glm::vec3& feet, float hw, float h) {
        int seenN = 0, total = 0;
        for (float fx = 0; fx <= 1.f; fx += 0.5f)
            for (float fy = 0; fy <= 1.f; fy += 0.5f)
                for (float fz = 0; fz <= 1.f; fz += 0.5f) {
                    glm::vec3 pt = feet + glm::vec3((fx * 2 - 1) * hw, fy * h, (fz * 2 - 1) * hw);
                    ++total;
                    if (canSee(w, c, pt)) ++seenN;
                }
        return (float)seenN / total;
    };
    auto impactOn = [&](const glm::vec3& feet, float hw, float h, glm::vec3& push) -> int {
        glm::vec3 center = feet + glm::vec3(0, h * 0.5f, 0);
        float dist = glm::length(center - c) / (power * 2.f);
        if (dist > 1.f) return 0;
        glm::vec3 dir = glm::length(center - c) > 1e-4f ? glm::normalize(center - c) : glm::vec3(0, 1, 0);
        float impact = (1.f - dist) * exposure(feet, hw, h);
        push = dir * impact;
        return (int)((impact * impact + impact) / 2.f * 8.f * power + 1.f);
    };
    std::vector<Player*> victims = netPlayers;
    if (victims.empty()) victims.push_back(&p);
    for (Player* vp : victims) {
        Player& q = *vp;
        if (q.dead) continue;
        glm::vec3 push;
        int dmg = impactOn(q.pos, PLAYER_HALF_W, PLAYER_H, push);
        if (dmg > 0) {
            q.invulnerable = 0;
            q.damageSource = 3;
            if (scaleDamage(dmg) > 0) hurtPlayer(q, scaleDamage(dmg), pev);
            q.motion += push;
        }
    }
    for (auto& m : mobs) {
        if (m.dying()) continue;
        const MobDef& d = mobDef(m.type);
        glm::vec3 push;
        int dmg = impactOn(m.pos, mobHalfW(m), mobHeight(m), push);
        if (dmg > 0) {
            m.invulnerable = 0;
            hurt(m, dmg, c, 0.f, false, hooks);
            m.motion += push;
        }
    }

    for (auto& t : tnts) {
        glm::vec3 push;
        if (impactOn(t.pos, 0.49f, 0.98f, push) > 0) t.motion += push;
    }

    // Разрушение блоков: 30% блоков выпадают предметами, содержимое сундуков — всегда
    for (const glm::ivec3& b : destroyed) {
        uint8_t id = w.getBlock(b.x, b.y, b.z);
        if (id == AIR) continue;
        if (id == TNT) { // соседний динамит взводится с коротким фитилём
            w.setBlock(b.x, b.y, b.z, AIR);
            igniteTnt(b, true, rng);
            continue;
        }
        if (TileEntity* te = w.tileAt(b.x, b.y, b.z)) {
            for (int i = 0; i < te->size(); ++i) dropFromBlock(items, b, te->items[i], rng);
            w.removeTile(b.x, b.y, b.z);
        }
        if (rfl(rng) < 0.3f)
            for (const ItemStack& d : blockDrops(id, w.getMeta(b.x, b.y, b.z), rng)) dropFromBlock(items, b, d, rng);
        w.setBlock(b.x, b.y, b.z, AIR);
        if (rint(rng, 3) == 0) spawnPoof(particles, glm::vec3(b) + 0.5f, 1, 1.f, 0.2f);
    }
    w.popped.clear(); // сорванные факелы/цветы уже учтены взрывом
    if (incendiary)
        for (const glm::ivec3& b : destroyed)
            if (w.getBlock(b.x, b.y, b.z) == AIR && isOpaque(w.getBlock(b.x, b.y - 1, b.z)) && rint(rng, 3) == 0)
                w.setBlock(b.x, b.y, b.z, FIRE);
    spawnPoof(particles, c, 40, power * 1.2f, 0.25f);
    if (hooks.explosionFx) hooks.explosionFx(c, power);
    hooks.sound("random/explode", 4.f, (1.f + (rfl(rng) - rfl(rng)) * 0.2f) * 0.7f, &c);
}

// ---------------------------------------------------------------- Тик

void MobManager::tick(World& w, Player& p0, TickEvents& pev, std::vector<ItemEntity>& items, std::vector<Particle>& particles,
                      const MobHooks& hooks, float skyFactor, uint32_t& rng) {
    inTick_ = true;
    std::vector<glm::vec3> fireExplosions;
    std::vector<std::pair<glm::vec3, float>> explosions;

    for (auto& m : mobs) {
        if (m.removed) continue;
        const MobDef& def = mobDef(m.type);
        // Моб в незагруженном чанке замирает
        if (!w.isChunkLoaded(floorDiv((int)std::floor(m.pos.x), CW), floorDiv((int)std::floor(m.pos.z), CW))) {
            m.prev = m.pos;
            m.prevYaw = m.yaw;
            continue;
        }
        // Цель — ближайший игрок (в сетевой игре их несколько)
        Player& p = *nearestPlayer(m.pos, p0);
        const glm::vec3 playerEye = p.eye();
        const bool playerTargetable = !p.creative() && !p.dead;
        m.prev = m.pos;
        m.prevYaw = m.yaw;
        m.prevLimbAmount = m.limbAmount;
        m.prevFuse = m.fuse;
        m.prevWingFlap = m.wingFlap;
        ++m.age;
        if (m.hurtTime > 0) --m.hurtTime;
        if (m.invulnerable > 0) --m.invulnerable;
        if (m.playerHitTicks > 0) --m.playerHitTicks;
        if (m.attackCooldown > 0) --m.attackCooldown;
        glm::vec3 center = m.pos + glm::vec3(0, mobHeight(m) * 0.5f, 0);

        if (m.type == MobType::EnderCrystal) continue; // висит на столбе и вращается
        if (m.type == MobType::EnderDragon) {
            if (m.dying()) {
                // Смерть: поднимается, сыплет взрывами, через 200 тиков исчезает и открывает портал выхода
                m.pos.y += 0.1f;
                // Опыт сыплется шарами: по 1000 каждые 5 тиков в конце и 10000 напоследок (EntityDragon 1.0)
                if (m.deathTime > 150 && m.deathTime % 5 == 0) spawnXpOrbs(orbs, m.pos, 1000, rng);
                if (m.deathTime == 199) spawnXpOrbs(orbs, m.pos, 10000, rng);
                m.yaw += 20.f; // крутится, поднимаясь (renderYawOffset += 20)
                if (m.deathTime == 1) hooks.sound("mob/enderdragon/end", 5.f, 1.f, nullptr);
                if (++m.deathTime >= 200) {
                    m.removed = true;
                    dragonKilled = true;
                    dragonXp = 0; // опыт уже высыпан шарами
                }
                continue;
            }
            // Цель: круг над островом или пикирование на игрока (EntityDragon)
            glm::vec3 d = m.wanderTarget - m.pos;
            float distP = glm::length(p.pos - m.pos);
            if (!m.hasWander || glm::length(d) < 10.f || rint(rng, 400) == 0) {
                if (playerTargetable && distP < 150.f && rint(rng, 3) == 0) {
                    m.wanderTarget = p.pos + glm::vec3(0, 2.f, 0);
                    m.attackCounter = 1;
                } else {
                    float a = rfl(rng) * 6.2831853f, r = 30.f + rfl(rng) * 50.f;
                    m.wanderTarget = glm::vec3(std::cos(a) * r, 70.f + rfl(rng) * 30.f, std::sin(a) * r);
                    m.attackCounter = 0;
                }
                m.hasWander = true;
                d = m.wanderTarget - m.pos;
            }
            if (m.attackCounter == 1 && playerTargetable) m.wanderTarget = p.pos + glm::vec3(0, 2.f, 0);
            glm::vec3 dir = glm::length(d) > 1e-3f ? glm::normalize(d) : glm::vec3(1, 0, 0);
            float spd = m.attackCounter == 1 ? 0.9f : 0.6f;
            m.motion += (dir * spd - m.motion) * 0.04f;
            m.pos += m.motion;
            if (glm::length(glm::vec2(m.motion.x, m.motion.z)) > 1e-3f) {
                float want = glm::degrees(std::atan2(m.motion.z, m.motion.x));
                m.yaw += std::clamp(wrapDeg(want - m.yaw), -8.f, 8.f);
            }
            m.wingFlap += 0.1f + (m.motion.y > 0.f ? 0.1f : 0.f);
            if ((int)(m.wingFlap / 6.2831853f) != (int)(m.prevWingFlap / 6.2831853f))
                hooks.sound("mob/enderdragon/wings", 5.f, 0.8f + rfl(rng) * 0.3f, &center);
            // Удар телом и крыльями: 10 урона и отбрасывание, потом дракон улетает
            glm::vec3 rel = p.pos - m.pos;
            if (playerTargetable && std::abs(rel.x) < 5.f && std::abs(rel.z) < 5.f && rel.y > -3.f && rel.y < 4.f && m.attackCooldown == 0) {
                m.attackCooldown = 20;
                playerAttackerId = m.id;
                if (scaleDamage(10) > 0 && hurtPlayer(p, scaleDamage(10), pev)) {
                    glm::vec2 k = glm::normalize(glm::vec2(rel.x, rel.z) + glm::vec2(1e-3f));
                    p.motion += glm::vec3(k.x * 1.5f, 0.6f, k.y * 1.5f);
                }
                m.hasWander = false;
                m.attackCounter = 0;
            }
            // Лечится от ближайшего кристалла (1 здоровье раз в 10 тиков)
            m.targetId = 0;
            float best = 32.f;
            for (auto& o : mobs)
                if (o.type == MobType::EnderCrystal && !o.removed && glm::length(o.pos - m.pos) < best) { best = glm::length(o.pos - m.pos); m.targetId = o.id; }
            if (m.targetId && m.age % 10 == 0 && m.health < 200) ++m.health;
            if (rint(rng, 1000) < m.livingSound++) { m.livingSound = -400; hooks.sound("mob/enderdragon/growl", 5.f, 0.8f + rfl(rng) * 0.3f, &center); }
            continue;
        }

        // Умирание: 20 тиков заваливается на бок, потом облачко дыма и дроп
        if (m.dying()) {
            if (++m.deathTime >= 20) {
                m.removed = true;
                if ((m.type == MobType::Slime || m.type == MobType::MagmaCube) && m.size > 1) {
                    int n = 2 + rint(rng, 3);
                    for (int i = 0; i < n; ++i) {
                        glm::vec3 off((i % 2 - 0.5f) * m.size * 0.25f, 0.5f, (i / 2 - 0.5f) * m.size * 0.25f);
                        Mob& c = spawn(m.type, m.pos + off, rfl(rng) * 360.f);
                        c.size = m.size / 2;
                        c.scale = (float)c.size;
                        c.health = c.size * c.size;
                    }
                }
                spawnPoof(particles, center, 20, mobHalfW(m) * 2.f + 0.5f);
                glm::ivec3 b((int)std::floor(m.pos.x), (int)std::floor(m.pos.y), (int)std::floor(m.pos.z));
                for (const ItemStack& d : mobDrops(m, rng)) dropFromBlock(items, b, d, rng);
                int xp = def.xp > 2 ? def.xp : 1 + rint(rng, 3);
                // Опыт — шарами, только если моба недавно бил игрок (как в 1.0)
                if (m.playerHitTicks > 0) spawnXpOrbs(orbs, m.pos + glm::vec3(0, 0.5f, 0), xp, rng);
                if (m.playerHitTicks > 0 && m.lastAttacker && hooks.onKillBy) {
                    hooks.onKillBy(*playerById(m.lastAttacker, p0), m.type);
                } else if (m.playerHitTicks > 0 && hooks.onKill) {
                    hooks.onKill(m.type);
                }
            }
            m.motion.x *= 0.5f;
            m.motion.z *= 0.5f;
            m.motion.y -= 0.08f;
            moveMob(w, m);
            continue;
        }

        m.inWater = boxInBlock(w, m.pos, mobHalfW(m), mobHeight(m), WATER);
        if (m.inWater) {
            glm::vec3 fl = w.flowVector((int)std::floor(m.pos.x), (int)std::floor(m.pos.y + 0.2f), (int)std::floor(m.pos.z));
            m.motion += fl * 0.014f;
        }
        bool inWeb = boxInBlock(w, m.pos, mobHalfW(m), mobHeight(m), COBWEB);
        m.inLava = boxInBlock(w, m.pos, mobHalfW(m), mobHeight(m), LAVA);
        m.scale = (m.type == MobType::Slime || m.type == MobType::MagmaCube) ? (float)m.size : (m.growingAge < 0 ? 0.5f : 1.f);
        if (m.growingAge < 0) ++m.growingAge;
        else if (m.growingAge > 0) --m.growingAge;
        if (m.inLove > 0) --m.inLove;
        if (m.angerTicks > 0 && --m.angerTicks == 0) m.angry = false;
        m.prevSquish = m.squish;
        m.prevTentacle = m.tentacle;
        m.prevSquidPitch = m.squidPitch;
        // Взмах рукой (swingItem): 8 тиков
        m.prevSwing = m.swing;
        if (m.swingTicks >= 0 && ++m.swingTicks >= 8) m.swingTicks = -1;
        m.swing = m.swingTicks >= 0 ? m.swingTicks / 8.f : 0.f;
        m.prevSquidRoll = m.squidRoll;

        // Эндермен (1.4.2): игрок смотрит ему в глаза/туловище (без тыквы на голове)
        bool staredAt = false;
        if (m.type == MobType::Enderman && playerTargetable && !playerPumpkin) {
            glm::vec3 centerMob = m.pos + glm::vec3(0, mobHeight(m) * 0.5f, 0);
            glm::vec3 to = centerMob - playerEye;
            float len = glm::length(to);
            staredAt = len < 64.f && len > 0.1f && glm::dot(p.look(), to / len) > 1.f - 0.035f / len && canSee(w, playerEye, centerMob);
        }
        // Злится сразу при взгляде, начинает дрожать и издаёт звук
        if (m.type == MobType::Enderman && !m.angry) {
            if (staredAt) {
                m.angry = true;
                hooks.sound("mob/endermen/stare", 1.f, 1.f, &center);
            }
        }

        // ---- Выбор цели
        glm::vec3 toPlayer = p.pos - m.pos;
        float distP = glm::length(toPlayer);
        bool neutralAngry = (m.type == MobType::Wolf && m.angry && !m.tamed) || (m.type == MobType::Enderman && m.angry) ||
                            (m.type == MobType::PigZombie && m.angry);
        bool target = false;
        float maxTargetDist = (m.type == MobType::Ghast || (m.type == MobType::Enderman && m.angry)) ? 64.f : 16.f;
        if ((def.hostile || neutralAngry) && playerTargetable && distP < maxTargetDist) {
            target = true;
            if (isSpiderLike(m.type)) {
                // Паук нападает в темноте или если его ударили
                int light = std::max(w.getSkyLight((int)std::floor(center.x), (int)std::floor(center.y), (int)std::floor(center.z)) -
                                         (skyFactor > 0.5f ? 0 : 11),
                                     w.getBlockLight((int)std::floor(center.x), (int)std::floor(center.y), (int)std::floor(center.z)));
                if (light < 8) m.angry = true;
                else if (m.angry && rint(rng, 100) == 0) m.angry = false;
                target = m.angry;
            }
        }

        float forward = 0.f, strafe = 0.f, speed = def.moveSpeed;
        float wantYaw = m.yaw;
        bool seesPlayer = target && canSee(w, m.pos + glm::vec3(0, mobHeight(m) * 0.85f, 0), playerEye);
        // Идти по пути к goal: пересчёт, если цель сместилась или вышло время (life тиков). false — пути нет или дошли
        auto steerPath = [&](const glm::vec3& goal, int life) -> bool {
            glm::ivec3 gc((int)std::floor(goal.x), (int)std::floor(goal.y + 0.01f), (int)std::floor(goal.z));
            if (--m.pathTimer <= 0 || glm::length(glm::vec3(gc - m.pathGoal)) > 1.5f) {
                findPath(w, m.pos, goal, mobHeight(m), 16, m.path);
                m.pathGoal = gc;
                m.pathIdx = 0;
                m.pathTimer = life + rint(rng, 20);
            }
            while (m.pathIdx < (int)m.path.size()) {
                glm::vec3 c = glm::vec3(m.path[m.pathIdx]) + glm::vec3(0.5f, 0.f, 0.5f);
                glm::vec2 d(c.x - m.pos.x, c.z - m.pos.z);
                if (glm::length(d) < std::max(0.35f, mobHalfW(m)) && std::abs(c.y - m.pos.y) < 1.5f) { ++m.pathIdx; continue; }
                wantYaw = glm::degrees(std::atan2(d.y, d.x));
                return true;
            }
            return false;
        };
        const bool walker = m.type == MobType::Zombie || m.type == MobType::Spider || m.type == MobType::CaveSpider ||
                            m.type == MobType::Creeper || m.type == MobType::PigZombie || m.type == MobType::Enderman ||
                            m.type == MobType::Silverfish || m.type == MobType::Wolf || m.type == MobType::Skeleton ||
                            m.type == MobType::WitherSkeleton || m.type == MobType::ZombieVillager || m.type == MobType::Witch ||
                            m.type == MobType::IronGolem || m.type == MobType::Ocelot || m.type == MobType::Cat;
        const bool flyer = m.type == MobType::Ghast || m.type == MobType::Blaze || m.type == MobType::Squid || m.type == MobType::Slime ||
                           m.type == MobType::MagmaCube || m.type == MobType::EnderDragon || m.type == MobType::EnderCrystal ||
                           m.type == MobType::Bat || m.type == MobType::Wither;

        if (target) {
            wantYaw = glm::degrees(std::atan2(toPlayer.z, toPlayer.x));
            forward = 1.f;
            // Вплотную и на виду — прямо на игрока, иначе в обход стен и ям
            if (walker && m.type != MobType::Skeleton && m.type != MobType::Witch && !(seesPlayer && distP < 3.f)) steerPath(p.pos, 20);
            switch (m.type) {
            case MobType::Zombie: case MobType::Spider: case MobType::CaveSpider: case MobType::Wolf: case MobType::Enderman:
            case MobType::PigZombie: case MobType::Silverfish: case MobType::Blaze: case MobType::ZombieVillager: case MobType::WitherSkeleton:
                // Ближний бой (нормальная сложность): зомби 4, паук 2, волк 2, эндермен 7, свинозомби 5, ифрит 6, визер-скелет 5
                if (distP < 2.f + mobHalfW(m) && std::abs(toPlayer.y) < 1.5f && m.attackCooldown == 0) {
                    m.attackCooldown = 20;
                    if (m.type == MobType::Zombie || m.type == MobType::PigZombie || m.type == MobType::ZombieVillager || m.type == MobType::WitherSkeleton) {
                        m.swingTicks = 0; m.swing = 0.f;
                    }
                    int base = m.type == MobType::Zombie ? 4 : m.type == MobType::Enderman ? 7 : m.type == MobType::PigZombie ? 5
                             : m.type == MobType::Blaze ? 6 : m.type == MobType::WitherSkeleton ? 5 : m.type == MobType::Silverfish ? 1 : 2;
                    int dmg = scaleDamage(base);
                    playerAttackerId = m.id;
                    if (m.type == MobType::CaveSpider && difficulty >= 2) p.poisonTicks = std::max(p.poisonTicks, difficulty == 3 ? 300 : 140);
                    if (m.type == MobType::Blaze) p.fireTicks = std::max(p.fireTicks, 100);
                    if (m.type == MobType::WitherSkeleton) addEffect(p, EFF_WITHER, 0, 200); // 10 сек иссушения
                    if (dmg > 0 && hurtPlayer(p, dmg, pev)) {
                        glm::vec2 d = glm::normalize(glm::vec2(toPlayer.x, toPlayer.z) + glm::vec2(1e-4f));
                        p.motion.x = p.motion.x / 2 + d.x * 0.4f;
                        p.motion.z = p.motion.z / 2 + d.y * 0.4f;
                        p.motion.y = std::min(0.4f, p.motion.y / 2 + 0.4f);
                    }
                }
                // Паук прыгает на игрока с 2..6 блоков
                if (isSpiderLike(m.type) && distP > 2.f && distP < 6.f && m.onGround && rint(rng, 10) == 0) {
                    glm::vec2 d = glm::normalize(glm::vec2(toPlayer.x, toPlayer.z));
                    m.motion.x = d.x * 0.5f * 0.8f + m.motion.x * 0.2f;
                    m.motion.z = d.y * 0.5f * 0.8f + m.motion.z * 0.2f;
                    m.motion.y = 0.4f;
                }
                break;
            case MobType::Ghast: forward = 0.f; break;
            case MobType::Skeleton: {
                // ИИ Скелета (1.4.2): прицеливание из лука с 15 блоков;
                // держит дистанцию: отходит назад если игрок ближе 6 блоков, стрейфит на 6..12, идёт на сближение дальше 12
                bool shooting = distP < 15.f && seesPlayer;
                m.aiming = shooting;
                if (shooting) {
                    wantYaw = glm::degrees(std::atan2(toPlayer.z, toPlayer.x));
                    if (distP < 6.f) {
                        forward = -0.5f; // пятится назад, разрывая дистанцию
                    } else if (distP > 12.f) {
                        forward = 0.7f;  // подходит ближе
                        steerPath(p.pos, 20);
                    } else {
                        forward = 0.f;   // держит дистанцию
                    }
                    // Стрейф влево-вправо при прицеливании
                    strafe = ((m.age / 25 + m.id) % 2 == 0) ? 0.4f : -0.4f;

                    if (m.attackCooldown == 0) {
                        // В 1.4.2: интервал стрельбы масштабируется от дистанции (20..40 тиков)
                        m.attackCooldown = 20 + (int)(distP * 1.33f);
                        glm::vec3 from = m.pos + glm::vec3(0, 1.4f, 0);
                        glm::vec3 d = playerEye - glm::vec3(0, 0.7f, 0) - from;
                        d.y += std::sqrt(d.x * d.x + d.z * d.z) * 0.2f;
                        from += glm::normalize(glm::vec3(d.x, 0, d.z)) * 0.4f;
                        shootArrow(from, d, 1.6f, 12.f, false, false, rng);
                        hooks.sound("random/bow", 1.f, 1.f / (rfl(rng) * 0.4f + 0.8f), &center);
                    }
                } else {
                    steerPath(p.pos, 20);
                    forward = 0.8f;
                }
                break;
            }
            case MobType::Witch: {
                // Ведьма (1.4.2): бросает взрывные зелья и пьёт защитные зелья
                bool canAttack = distP < 12.f && seesPlayer;
                if (canAttack) {
                    wantYaw = glm::degrees(std::atan2(toPlayer.z, toPlayer.x));
                    if (distP < 4.f) forward = -0.5f;
                    else if (distP > 8.f) forward = 0.5f;
                    else forward = 0.f;

                    if (m.attackCooldown == 0) {
                        m.attackCooldown = 45 + rint(rng, 20);
                        m.swingTicks = 0; m.swing = 0.f;
                        uint16_t potionDmg = 16384 | EFF_HARM;
                        if (distP > 8.f && !p.hasEffect(EFF_SLOWNESS)) potionDmg = 16384 | EFF_SLOWNESS;
                        else if (p.health > 8 && !p.hasEffect(EFF_POISON)) potionDmg = 16384 | EFF_POISON;
                        else if (distP < 3.f && !p.hasEffect(EFF_WEAKNESS)) potionDmg = 16384 | EFF_WEAKNESS;

                        Throwable t;
                        t.id = nextEntityId++;
                        t.pos = m.pos + glm::vec3(0, 1.4f, 0);
                        t.prev = t.pos;
                        glm::vec3 dir = playerEye - glm::vec3(0, 0.4f, 0) - t.pos;
                        float dlen = std::sqrt(dir.x * dir.x + dir.z * dir.z);
                        dir.y += dlen * 0.2f;
                        t.motion = glm::normalize(dir) * 0.75f;
                        t.item = POTION;
                        t.damage = potionDmg;
                        throwables.push_back(t);
                        hooks.sound("random/bow", 1.f, 0.8f, &center);
                    }
                } else {
                    steerPath(p.pos, 20);
                    forward = 0.6f;
                }
                if (m.health < 15 && m.attackCooldown <= 10 && rint(rng, 40) == 0) {
                    m.health = std::min(26, m.health + 4);
                    m.attackCooldown = 30;
                    hooks.sound("random/drink", 1.f, 1.f, &center);
                }
                break;
            }
            case MobType::Creeper: {
                bool scaredOfCat = false;
                for (const auto& o : mobs) {
                    if ((o.type == MobType::Ocelot || o.type == MobType::Cat) && !o.dying()) {
                        glm::vec3 toCat = m.pos - o.pos;
                        float cd = glm::length(toCat);
                        if (cd < 16.f && cd > 0.01f) {
                            wantYaw = glm::degrees(std::atan2(toCat.z, toCat.x));
                            forward = 1.f;
                            scaredOfCat = true;
                            break;
                        }
                    }
                }
                if (scaredOfCat) {
                    if (m.fuse > 0) --m.fuse;
                    break;
                }
                if (distP < 3.f && seesPlayer) {
                    if (m.fuse == 0) hooks.sound("random/fuse", 1.f, 0.5f, &center);
                    forward = 0.f;
                    if (++m.fuse >= 30) {
                        m.removed = true;
                        explosions.push_back({center, m.charged ? 6.f : 3.f});
                    }
                } else if (distP > 7.f && m.fuse > 0) {
                    --m.fuse;
                } else if (m.fuse > 0) {
                    --m.fuse;
                }
                break;
            }
            default: break;
            }
        } else {
            if (m.type == MobType::Creeper && m.fuse > 0) --m.fuse;
            bool handled = false;
            // Цель-моб: у волка — цель игрока, у снеговика и железного голема — монстры
            Mob* foe = nullptr;
            if ((m.type == MobType::Wolf && m.tamed && !m.sitting) || m.type == MobType::SnowGolem || m.type == MobType::IronGolem) {
                for (auto& o : mobs) {
                    if (&o == &m || o.dying() || o.removed) continue;
                    bool want = (m.type == MobType::IronGolem) ?
                        (o.type == MobType::Zombie || o.type == MobType::Skeleton || o.type == MobType::Spider ||
                         o.type == MobType::CaveSpider || o.type == MobType::ZombieVillager || o.type == MobType::WitherSkeleton) :
                        (m.type == MobType::SnowGolem) ? mobDef(o.type).hostile :
                        ((o.id == playerTargetId || o.id == playerAttackerId) && !(o.type == MobType::Wolf && o.tamed));
                    if (!want) continue;
                    float dd = glm::length(o.pos - m.pos);
                    if (dd < 16.f && (!foe || dd < glm::length(foe->pos - m.pos))) foe = &o;
                }
            }
            if (foe) {
                glm::vec3 d = foe->pos - m.pos;
                float dd = glm::length(d);
                wantYaw = glm::degrees(std::atan2(d.z, d.x));
                handled = true;
                if (m.type == MobType::Wolf) {
                    forward = 1.f;
                    if (dd < 1.5f + mobHalfW(*foe) && m.attackCooldown == 0) {
                        m.attackCooldown = 20;
                        hurt(*foe, 4, m.pos, 1.f, false, hooks);
                    }
                } else if (m.type == MobType::IronGolem) {
                    forward = 1.f;
                    steerPath(foe->pos, 20);
                    if (dd < 2.5f + mobHalfW(*foe) && m.attackCooldown == 0) {
                        m.attackCooldown = 20;
                        m.swingTicks = 0; m.swing = 0.f;
                        hooks.sound("mob/irongolem/throw", 1.f, 1.f, &center);
                        hurt(*foe, 7 + rint(rng, 15), m.pos, 0.5f, false, hooks);
                        foe->motion.y += 0.45f;
                    }
                } else {
                    forward = dd > 8.f ? 1.f : 0.f;
                    glm::vec3 from = m.pos + glm::vec3(0, 1.4f, 0);
                    glm::vec3 aim = foe->pos + glm::vec3(0, mobHeight(*foe) * 0.6f, 0) - from;
                    if (m.attackCooldown == 0 && dd < 10.f && canSee(w, from, foe->pos + glm::vec3(0, mobHeight(*foe) * 0.6f, 0))) {
                        m.attackCooldown = 20;
                        aim.y += std::sqrt(aim.x * aim.x + aim.z * aim.z) * 0.2f;
                        throwItem(from, aim, SNOWBALL, rng);
                        hooks.sound("random/bow", 0.5f, 0.4f / (rfl(rng) * 0.4f + 0.8f), &center);
                    }
                }
            } else if (m.type == MobType::Cat && m.tamed) {
                handled = true;
                if (!m.sitting && !p.dead) {
                    glm::vec3 d = p.pos - m.pos;
                    float dd = glm::length(d);
                    if (dd > 12.f && p.onGround) {
                        m.pos = m.prev = p.pos + glm::vec3(rfl(rng) * 2.f - 1.f, 0.f, rfl(rng) * 2.f - 1.f);
                        m.motion = glm::vec3(0.f);
                    } else if (dd > 3.f) {
                        wantYaw = glm::degrees(std::atan2(d.z, d.x));
                        forward = 1.f;
                    } else {
                        handled = false;
                    }
                }
            } else if (m.type == MobType::Ocelot) {
                if (distP < 8.f) {
                    wantYaw = glm::degrees(std::atan2(-toPlayer.z, -toPlayer.x));
                    forward = 1.f;
                    handled = true;
                }
            } else if (m.inLove > 0) {
                // Ищет пару того же вида, тоже «влюблённую»
                Mob* mate = nullptr;
                for (auto& o : mobs)
                    if (&o != &m && o.type == m.type && o.inLove > 0 && !o.dying() && glm::length(o.pos - m.pos) < 8.f &&
                        (!mate || glm::length(o.pos - m.pos) < glm::length(mate->pos - m.pos)))
                        mate = &o;
                if (mate) {
                    handled = true;
                    glm::vec3 d = mate->pos - m.pos;
                    wantYaw = glm::degrees(std::atan2(d.z, d.x));
                    forward = glm::length(d) > 2.f ? 1.f : 0.f;
                    if (glm::length(d) < 2.5f && ++m.breedTimer >= 60) {
                        Mob& baby = spawn(m.type, m.pos, m.yaw);
                        baby.growingAge = -24000;
                        baby.scale = 0.5f;
                        baby.tamed = m.tamed;
                        if (m.type == MobType::Sheep) baby.color = (rint(rng, 2) ? m.color : mate->color);
                        m.inLove = mate->inLove = 0;
                        m.breedTimer = mate->breedTimer = 0;
                        m.growingAge = mate->growingAge = 6000;
                        for (int i = 0; i < 7; ++i)
                            spawnHeart(particles, m.pos + glm::vec3(rfl(rng) - 0.5f, 0.5f + rfl(rng), rfl(rng) - 0.5f));
                    }
                }
            }
            // Стриженая овца щиплет траву — шерсть отрастает
            if (m.type == MobType::Sheep && m.sheared && rint(rng, 1000) == 0) {
                glm::ivec3 b((int)std::floor(m.pos.x), (int)std::floor(m.pos.y - 0.5f), (int)std::floor(m.pos.z));
                if (w.getBlock(b.x, b.y, b.z) == GRASS) {
                    w.setBlock(b.x, b.y, b.z, DIRT);
                    m.sheared = false;
                }
            }
            if (handled) {
                // уже выбрали, куда идти
            } else if (m.type == MobType::Wolf && m.sitting) {
                forward = 0.f;
            } else if (m.fleeTicks > 0) {
                // Паника: точка в стороне от игрока, новая — раз в ~секунду или когда добежали
                --m.fleeTicks;
                glm::vec3 d = m.wanderTarget - m.pos;
                if (!m.hasWander || m.fleeTicks % 20 == 0 || glm::length(glm::vec2(d.x, d.z)) < 1.5f) {
                    glm::vec2 away = -glm::vec2(toPlayer.x, toPlayer.z);
                    if (glm::length(away) < 0.01f) away = glm::vec2(1.f, 0.f);
                    float ang = std::atan2(away.y, away.x) + (rfl(rng) - 0.5f) * 1.6f;
                    m.wanderTarget = m.pos + glm::vec3(std::cos(ang), 0.f, std::sin(ang)) * (6.f + rfl(rng) * 4.f);
                    m.hasWander = true;
                    d = m.wanderTarget - m.pos;
                }
                wantYaw = glm::degrees(std::atan2(d.z, d.x));
                forward = 1.f;
                speed *= 1.4f;
                if (m.fleeTicks == 0) m.hasWander = false;
            } else {
                // Бродим: изредка выбираем точку в пределах 10 блоков
                if (m.hasWander) {
                    glm::vec3 d = m.wanderTarget - m.pos;
                    if (glm::length(glm::vec2(d.x, d.z)) < 1.f || rint(rng, 200) == 0) m.hasWander = false;
                    else if (flyer) { wantYaw = glm::degrees(std::atan2(d.z, d.x)); forward = 1.f; }
                    else if (!steerPath(m.wanderTarget, 200)) m.hasWander = false; // дошли по пути (или пути нет)
                    else forward = 1.f;
                } else if (rint(rng, 120) == 0) {
                    // Точка в 10 блоках (updateWanderPath); путь обходит воду по краю, лаву и обрывы выше 3 блоков
                    m.wanderTarget = m.pos + glm::vec3(rint(rng, 21) - 10, flyer ? 0 : rint(rng, 7) - 3, rint(rng, 21) - 10);
                    m.hasWander = true;
                    m.pathTimer = 0;
                }
            }
        }
        if (m.type == MobType::Creeper && m.fuse > 0 && !target) m.fuse = std::max(0, m.fuse - 1);
        bool enderTeleportTo = false;
        if (m.type == MobType::Enderman) {
            // Блоки берёт и ставит всегда (не только когда бродит): взять — на уровне ног и выше в пределах 2,
            // поставить — рядом на твёрдый блок
            glm::ivec3 b((int)std::floor(m.pos.x - 2.f + rfl(rng) * 4.f), (int)std::floor(m.pos.y + rfl(rng) * 3.f),
                         (int)std::floor(m.pos.z - 2.f + rfl(rng) * 4.f));
            uint8_t bb = w.getBlock(b.x, b.y, b.z);
            auto pickable = [](uint8_t k) {
                return k == GRASS || k == DIRT || k == SAND || k == GRAVEL || k == DANDELION || k == ROSE || k == BROWN_MUSHROOM ||
                       k == RED_MUSHROOM || k == TNT || k == CACTUS || k == CLAY || k == PUMPKIN || k == MELON_BLOCK || k == MYCELIUM;
            };
            if (m.heldBlock == 0) {
                if (rint(rng, 20) == 0 && pickable(bb)) {
                    m.heldBlock = bb;
                    m.heldMeta = w.getMeta(b.x, b.y, b.z);
                    w.setBlock(b.x, b.y, b.z, AIR);
                }
            } else if (rint(rng, 2000) == 0) {
                glm::ivec3 q((int)std::floor(m.pos.x - 1.f + rfl(rng) * 2.f), (int)std::floor(m.pos.y + rfl(rng) * 2.f),
                             (int)std::floor(m.pos.z - 1.f + rfl(rng) * 2.f));
                if (w.getBlock(q.x, q.y, q.z) == AIR && isOpaque(w.getBlock(q.x, q.y - 1, q.z))) {
                    w.setBlock(q.x, q.y, q.z, m.heldBlock, m.heldMeta);
                    m.heldBlock = 0;
                }
            }
            if (!target) speed *= 0.35f / 0.7f; // бродит медленно
            else if (m.angry) speed = 1.05f;    // в ярости бежит очень быстро (спринт 1.4.2)

            if (target && m.angry) {
                if (staredAt) {
                    // Пока игрок смотрит прямо в глаза — стоит, дрожит и кричит; вплотную — телепортируется
                    forward = 0.f;
                    if (distP < 3.5f) m.attackCounter = -1;
                    m.teleportDelay = 0;
                } else {
                    // Игрок отвёл взгляд — бежит к нему и периодически телепортируется прямо к игроку
                    forward = 1.f;
                    if (++m.teleportDelay >= 60 || (distP > 8.f && m.teleportDelay >= 20 && rint(rng, 6) == 0)) {
                        m.teleportDelay = 0;
                        enderTeleportTo = true;
                    }
                }
            } else {
                m.teleportDelay = 0;
            }
        }

        // Поворот не больше 30° за тик
        float dy = std::clamp(wrapDeg(wantYaw - m.yaw), -30.f, 30.f);
        m.yaw += dy;

        glm::vec3 before = m.pos;
        bool customMove = false;
        if (m.type == MobType::Squid) {
            // EntitySquid 1.0: цикл щупалец; в первой половине цикла — рывок (скорость 1), дальше скольжение с затуханием 0.9
            customMove = true;
            const float PI_F = 3.1415927f;
            if (m.squidSpeed <= 0.f) m.squidSpeed = 1.f / (rfl(rng) + 1.f) * 0.2f;
            // updateEntityActionState: новое направление раз в ~50 тиков (и всегда вне воды)
            if (rint(rng, 50) == 0 || !m.inWater || glm::length(m.swimDir) < 1e-6f) {
                float a = rfl(rng) * PI_F * 2.f;
                m.swimDir = glm::vec3(std::cos(a) * 0.2f, -0.1f + rfl(rng) * 0.2f, std::sin(a) * 0.2f);
            }
            moveMob(w, m); // moveEntityWithHeading: просто сдвиг на motion, без гравитации и трения
            m.swimPhase += m.squidSpeed;
            if (m.swimPhase > PI_F * 2.f) {
                m.swimPhase -= PI_F * 2.f;
                if (rint(rng, 10) == 0) m.squidSpeed = 1.f / (rfl(rng) + 1.f) * 0.2f;
            }
            if (m.inWater) {
                if (m.swimPhase < PI_F) {
                    float f = m.swimPhase / PI_F;
                    m.tentacle = std::sin(f * f * PI_F) * PI_F * 0.25f;
                    if (f > 0.75f) { m.squidVel = 1.f; m.squidRollSpeed = 1.f; }
                    else m.squidRollSpeed *= 0.8f;
                } else {
                    m.tentacle = 0.f;
                    m.squidVel *= 0.9f;
                    m.squidRollSpeed *= 0.99f;
                }
                m.motion = m.swimDir * m.squidVel;
                float hs = glm::length(glm::vec2(m.motion.x, m.motion.z));
                if (hs > 1e-4f) m.yaw = m.prevYaw + wrapDeg(glm::degrees(std::atan2(m.motion.z, m.motion.x)) - m.prevYaw) * 0.1f;
                else m.yaw = m.prevYaw;
                m.squidRoll += PI_F * m.squidRollSpeed * 1.5f;
                m.squidPitch += (-glm::degrees(std::atan2(hs, m.motion.y)) - m.squidPitch) * 0.1f;
            } else {
                m.tentacle = std::abs(std::sin(m.swimPhase)) * PI_F * 0.25f;
                m.motion.x = m.motion.z = 0.f;
                m.motion.y = (m.motion.y - 0.08f) * 0.98f;
                m.squidPitch += (-90.f - m.squidPitch) * 0.02f;
                m.yaw = m.prevYaw;
            }
        } else if (m.type == MobType::Ghast) {
            customMove = true;
            glm::vec3 d = m.wanderTarget - m.pos;
            if (!m.hasWander || glm::length(d) < 1.f || glm::length(d) > 60.f || rint(rng, 60) == 0) {
                m.wanderTarget = m.pos + glm::vec3(rfl(rng) * 32.f - 16.f, rfl(rng) * 32.f - 16.f, rfl(rng) * 32.f - 16.f);
                m.wanderTarget.y = std::clamp(m.wanderTarget.y, 8.f, 110.f);
                m.hasWander = true;
                d = m.wanderTarget - m.pos;
            }
            if (glm::length(d) > 1e-3f) m.motion += glm::normalize(d) * 0.01f;
            if (target && seesPlayer) {
                m.yaw = glm::degrees(std::atan2(toPlayer.z, toPlayer.x));
                if (++m.attackCounter == 10) hooks.sound("mob/ghast/charge", 10.f, (rfl(rng) - rfl(rng)) * 0.2f + 1.f, &center);
                if (m.attackCounter == 20) {
                    glm::vec3 look = glm::normalize(playerEye - center);
                    Fireball f;
                    f.pos = f.prev = center + look * 2.2f;
                    f.accel = glm::normalize(playerEye - f.pos + glm::vec3(rfl(rng) - 0.5f, rfl(rng) - 0.5f, rfl(rng) - 0.5f) * 0.4f) * 0.1f;
                    fireballs.push_back(f);
                    hooks.sound("mob/ghast/fireball", 10.f, (rfl(rng) - rfl(rng)) * 0.2f + 1.f, &center);
                    m.attackCounter = -40;
                }
            } else if (m.attackCounter > 0) {
                --m.attackCounter;
            } else if (m.attackCounter < 0) {
                ++m.attackCounter;
            }
            moveMob(w, m);
            m.motion *= 0.91f;
        } else if (m.type == MobType::Slime || m.type == MobType::MagmaCube) {
            customMove = true;
            bool magma = m.type == MobType::MagmaCube;
            if (m.onGround) {
                if (m.squishTarget < 0.f) m.squishTarget = 0.f;
                if (--m.jumpDelay <= 0) {
                    m.jumpDelay = rint(rng, 20) + 10;
                    if (target) m.jumpDelay /= 3;
                    if (target) m.yaw = glm::degrees(std::atan2(toPlayer.z, toPlayer.x));
                    else m.yaw += (rfl(rng) - 0.5f) * 90.f;
                    float yr = glm::radians(m.yaw);
                    float hs = 0.2f + 0.05f * m.size;
                    m.motion.x = std::cos(yr) * hs;
                    m.motion.z = std::sin(yr) * hs;
                    m.motion.y = magma ? 0.42f + 0.1f * m.size : 0.42f;
                    m.squishTarget = 1.f;
                    hooks.sound(magma ? std::string("mob/magmacube/jump") : slimeSound(m), 0.4f * m.size,
                                ((rfl(rng) - rfl(rng)) * 0.2f + 1.f) * 0.8f, &center);
                } else {
                    m.motion.x *= 0.5f;
                    m.motion.z *= 0.5f;
                }
            }
            bool was = m.onGround;
            m.motion.y -= 0.08f;
            m.motion.y *= 0.98f;
            moveMob(w, m);
            if (m.onGround && !was) m.squishTarget = -0.5f;
            m.squish += (m.squishTarget - m.squish) * 0.5f;
            m.squishTarget *= 0.6f;
            // Касание игрока: слизни от размера 2, лавовые кубы всегда
            if ((m.size > 1 || magma) && playerTargetable && m.attackCooldown == 0 && distP < 0.6f * m.size + 0.3f &&
                std::abs(toPlayer.y) < 0.6f * m.size + 1.f) {
                m.attackCooldown = 20;
                int dmg = scaleDamage(magma ? m.size + 2 : m.size);
                if (dmg > 0 && hurtPlayer(p, dmg, pev)) {
                    playerAttackerId = m.id;
                    hooks.sound("mob/slime/attack", 1.f, (rfl(rng) - rfl(rng)) * 0.2f + 1.f, &center);
                }
            }
        } else if (m.type == MobType::Blaze) {
            // Ифрит парит на уровне игрока и стреляет очередями по 3 огненных шара
            if (target && playerEye.y > m.pos.y + 1.6f + 0.5f) m.motion.y += (0.3f - m.motion.y) * 0.3f;
            if (!m.onGround && m.motion.y < 0.f) m.motion.y *= 0.6f;
            if (target && seesPlayer && distP > 2.f && distP < 30.f) {
                ++m.attackCounter;
                if (m.attackCounter == 60 || m.attackCounter == 66 || m.attackCounter == 72) {
                    glm::vec3 from = m.pos + glm::vec3(0, 1.4f, 0);
                    float sp = std::sqrt(distP) * 0.5f;
                    Fireball f;
                    f.small = true;
                    f.pos = f.prev = from;
                    glm::vec3 aim = playerEye - glm::vec3(0, 0.5f, 0) - from + glm::vec3(rgauss(rng), 0, rgauss(rng)) * sp;
                    f.accel = glm::normalize(aim) * 0.1f;
                    fireballs.push_back(f);
                    hooks.sound("mob/ghast/fireball", 1.f, 1.f, &center);
                }
                if (m.attackCounter > 100) m.attackCounter = 0;
                forward = distP > 8.f ? forward : 0.f;
            }
        } else if (m.type == MobType::Bat) {
            customMove = true;
            m.wingFlap = std::cos(m.age * 1.3f) * 0.8f;
            if (distP < 4.f && m.attackCounter <= 0) {
                m.attackCounter = 80;
                hooks.sound("mob/bat/takeoff", 0.8f, 1.f, &center);
            }
            if (m.attackCounter > 0) --m.attackCounter;
            if (rint(rng, 15) == 0 || m.collidedH) {
                float a = rfl(rng) * 6.2831853f;
                m.motion.x = std::cos(a) * 0.12f;
                m.motion.z = std::sin(a) * 0.12f;
                m.motion.y = (rfl(rng) - 0.45f) * 0.12f;
                m.yaw = glm::degrees(a);
            }
            moveMob(w, m);
        } else if (m.type == MobType::Wither) {
            customMove = true;
            if (m.invulnerable > 0) {
                --m.invulnerable;
                m.health = std::min(300, (int)((1.f - m.invulnerable / 220.f) * 300.f) + 1);
                if (m.invulnerable == 0) {
                    explosions.push_back({center, 7.f});
                    hooks.sound("random/explode", 1.5f, 0.6f, &center);
                }
            } else {
                float targetY = p.pos.y + (m.health <= 150 ? 2.f : 4.5f);
                m.motion.y += (targetY - m.pos.y) * 0.05f;
                m.motion.y = std::clamp(m.motion.y, -0.25f, 0.25f);
                if (distP > 10.f) {
                    glm::vec2 d = glm::normalize(glm::vec2(toPlayer.x, toPlayer.z));
                    m.motion.x += d.x * 0.035f;
                    m.motion.z += d.y * 0.035f;
                } else if (distP < 4.f) {
                    glm::vec2 d = glm::normalize(glm::vec2(toPlayer.x, toPlayer.z));
                    m.motion.x -= d.x * 0.03f;
                    m.motion.z -= d.y * 0.03f;
                }
                m.motion.x *= 0.9f;
                m.motion.z *= 0.9f;
                m.yaw = glm::degrees(std::atan2(toPlayer.z, toPlayer.x));

                // Основная голова
                if (m.attackCooldown > 0) --m.attackCooldown;
                if (m.attackCooldown == 0 && seesPlayer && distP < 40.f) {
                    m.attackCooldown = (m.health <= 150) ? 25 : 40;
                    Fireball fb;
                    fb.id = nextEntityId++;
                    fb.pos = fb.prev = m.pos + glm::vec3(0, 2.5f, 0);
                    glm::vec3 dir = playerEye - fb.pos;
                    if (glm::length(dir) > 0.1f) fb.motion = glm::normalize(dir) * 0.65f;
                    fb.wither = true;
                    fb.blue = (rint(rng, 10) == 0);
                    fireballs.push_back(fb);
                    hooks.sound("mob/wither/shoot", 1.f, 1.f, &center);
                }
                // Боковые головы
                if (m.attackCounter > 0) --m.attackCounter;
                if (m.attackCounter == 0 && rint(rng, 30) == 0 && seesPlayer && distP < 40.f) {
                    m.attackCounter = 35;
                    Fireball fb;
                    fb.id = nextEntityId++;
                    float ho = (rint(rng, 2) == 0) ? -1.f : 1.f;
                    glm::vec3 sideHead = m.pos + glm::vec3(ho * 0.8f, 2.3f, 0);
                    fb.pos = fb.prev = sideHead;
                    glm::vec3 dir = playerEye - sideHead;
                    if (glm::length(dir) > 0.1f) fb.motion = glm::normalize(dir) * 0.6f;
                    fb.wither = true;
                    fb.blue = false;
                    fireballs.push_back(fb);
                    hooks.sound("mob/wither/shoot", 0.9f, 1.1f, &center);
                }
            }
            moveMob(w, m);
        }

        // ---- Движение (как moveEntityWithHeading в 1.0)
        float s = std::sin(glm::radians(m.yaw)), c = std::cos(glm::radians(m.yaw));
        glm::vec3 fwd(c, 0, s);
        glm::vec3 rgt(-s, 0, c);
        if (customMove) {
            // уже подвинулись выше
        } else if (m.inWater || m.inLava) {
            if (rfl(rng) < 0.8f) m.motion.y += 0.04f; // всплывают
            m.motion += (fwd * forward + rgt * strafe) * (speed * 0.02f);
            moveMob(w, m);
            m.motion *= m.inWater ? 0.8f : 0.5f;
            m.motion.y -= 0.02f;
            if (m.collidedH) m.motion.y = 0.3f;
        } else {
            uint8_t below = w.getBlock((int)std::floor(m.pos.x), (int)std::floor(m.pos.y - 0.5f), (int)std::floor(m.pos.z));
            float slip = m.onGround ? slipperiness(below) * 0.91f : 0.91f;
            float accel = m.onGround ? 0.1f * (0.16277136f / (slip * slip * slip)) : 0.02f;
            // Препятствие — прыжок (паук вместо этого лезет по стене)
            if (m.collidedH && (forward > 0.f || strafe != 0.f)) {
                if (m.type == MobType::Spider) m.motion.y = 0.2f;
                else if (m.onGround) m.motion.y = 0.42f;
            }
            m.motion += (fwd * forward + rgt * strafe) * (speed * accel);
            if (inWeb) { m.motion.x *= 0.25f; m.motion.z *= 0.25f; m.motion.y *= 0.05f; m.fallDistance = 0.f; }
            moveMob(w, m);
            m.motion.y -= 0.08f;
            m.motion.y *= 0.98f;
            if (m.type == MobType::Chicken && !m.onGround && m.motion.y < 0.f) m.motion.y *= 0.6f; // курица планирует
            m.motion.x *= slip;
            m.motion.z *= slip;
        }

        // ---- Падение
        glm::vec3 moved = m.pos - before;
        if (m.inWater) m.fallDistance = 0.f;
        else if (m.onGround) {
            if (m.fallDistance > 0.f)
                w.trampleFarmland((int)std::floor(m.pos.x), (int)std::floor(m.pos.y - 0.2f), (int)std::floor(m.pos.z), m.fallDistance, rfl(rng));
            if (m.fallDistance > 3.f && m.type != MobType::Chicken && m.type != MobType::Slime && m.type != MobType::MagmaCube &&
                m.type != MobType::Ghast && m.type != MobType::Blaze && m.type != MobType::Squid)
                hurt(m, (int)std::ceil(m.fallDistance - 3.f), m.pos, 0.f, false, hooks);
            // «When Pigs Fly»: свинья с игроком в седле разбилась с высоты больше 5
            if (m.type == MobType::Pig && m.ridden && m.fallDistance > 5.f) {
                if (m.riderId && hooks.achievementFor) hooks.achievementFor(*playerById(m.riderId, p0), 15);
                else if (hooks.achievement) hooks.achievement(15);
            }
            m.fallDistance = 0.f;
        } else if (moved.y < 0.f) {
            m.fallDistance -= moved.y;
        }

        // ---- Анимация ног и крыльев
        float h = std::sqrt(moved.x * moved.x + moved.z * moved.z);
        m.limbAmount += (std::min(1.f, h * 4.f) - m.limbAmount) * 0.4f;
        m.limbSwing += m.limbAmount;
        if (m.type == MobType::Chicken) {
            m.wingSpeed = m.onGround ? m.wingSpeed * 0.9f : std::min(1.f, m.wingSpeed + 0.3f);
            m.wingFlap += m.wingSpeed * 2.f;
        }

        // ---- Звуки: голос и шаги
        if (rint(rng, 1000) < m.livingSound++) {
            m.livingSound = -80;
            std::string g = livingSound(m, rng);
            if (!g.empty()) hooks.sound(g, m.type == MobType::Ghast ? 10.f : 1.f, (rfl(rng) - rfl(rng)) * 0.2f + 1.f, &center);
        }
        if (m.onGround) {
            m.walked += h * 0.6f;
            if (m.walked > m.nextStep) {
                m.nextStep = std::floor(m.walked) + 1.f;
                if (m.type != MobType::Creeper) hooks.sound(std::string(def.soundDir) + "/step", 0.15f, 1.f, &center);
            }
        }

        // ---- Курица несёт яйца
        if (m.type == MobType::Chicken && --m.eggTimer <= 0) {
            m.eggTimer = 6000 + rint(rng, 6000);
            glm::ivec3 b((int)std::floor(m.pos.x), (int)std::floor(m.pos.y), (int)std::floor(m.pos.z));
            dropFromBlock(items, b, makeStack(EGG), rng);
            hooks.sound("mob/chicken/plop", 1.f, (rfl(rng) - rfl(rng)) * 0.2f + 1.f, &center);
        }

        // ---- Нежить горит на солнце; огонь и лава
        bool exposedToRain = false;
        if (raining) {
            glm::ivec3 e((int)std::floor(m.pos.x), (int)std::floor(m.pos.y + mobHeight(m)), (int)std::floor(m.pos.z));
            exposedToRain = w.getSkyLight(e.x, e.y, e.z) == 15 && !biomeInfo(w.loadedBiome(e.x, e.z)).dry;
            if (exposedToRain) m.fireTicks = 0;
        }
        bool burnsInDaylight = (m.type == MobType::Zombie || m.type == MobType::Skeleton || m.type == MobType::ZombieVillager);
        if (burnsInDaylight && skyFactor > 0.5f && !m.inWater && !exposedToRain) {
            glm::ivec3 e((int)std::floor(m.pos.x), (int)std::floor(m.pos.y + mobHeight(m)), (int)std::floor(m.pos.z));
            if (w.getSkyLight(e.x, e.y, e.z) == 15 && rfl(rng) * 30.f < (skyFactor - 0.4f) * 2.f) m.fireTicks = 160;
        }
        if (isFireImmune(m.type)) m.fireTicks = 0;
        // Эндермена, ифрита и снеговика вода ранит; эндермен при этом телепортируется
        bool wet = m.inWater || exposedToRain;
        if ((m.type == MobType::Enderman || m.type == MobType::Blaze || m.type == MobType::SnowGolem) && wet && m.age % 10 == 0)
            hurt(m, 1, m.pos, 0.f, false, hooks);
        if (m.type == MobType::SnowGolem && biomeInfo(w.loadedBiome((int)std::floor(m.pos.x), (int)std::floor(m.pos.z))).dry && m.age % 20 == 0)
            hurt(m, 1, m.pos, 0.f, false, hooks);
        if (m.type == MobType::SnowGolem && !m.dying()) {
            glm::ivec3 f((int)std::floor(m.pos.x), (int)std::floor(m.pos.y), (int)std::floor(m.pos.z));
            if (w.getBlock(f.x, f.y, f.z) == AIR && isOpaque(w.getBlock(f.x, f.y - 1, f.z)) &&
                !biomeInfo(w.loadedBiome(f.x, f.z)).dry)
                w.setBlock(f.x, f.y, f.z, SNOW_LAYER);
        }
        // Эндермен телепортируется: от удара/снаряда, в воде и огне, днём на солнце (и тогда забывает игрока),
        // или к игроку, если тот далеко
        bool enderSun = false;
        if (m.type == MobType::Enderman && skyFactor > 0.5f) {
            glm::ivec3 e((int)std::floor(m.pos.x), (int)std::floor(m.pos.y + mobHeight(m)), (int)std::floor(m.pos.z));
            enderSun = w.getSkyLight(e.x, e.y, e.z) == 15 && rfl(rng) * 30.f < (skyFactor - 0.4f) * 2.f;
        }
        if (m.type == MobType::Enderman && (enderSun || wet || m.fireTicks > 0)) m.angry = false;
        if (m.type == MobType::Enderman && !m.dying() && (m.attackCounter < 0 || wet || m.fireTicks > 0 || enderSun || enderTeleportTo)) {
            m.attackCounter = 0;
            for (int tries = 0; tries < 64; ++tries) {
                // Телепортация к игроку (teleportToEntity в 1.4.2): в радиусе 2..5 блоков от игрока
                glm::vec3 t;
                if (enderTeleportTo) {
                    float a = rfl(rng) * 6.2831853f;
                    float d = 2.f + rfl(rng) * 3.f;
                    t = p.pos + glm::vec3(std::cos(a) * d, (float)(rint(rng, 5) - 2), std::sin(a) * d);
                } else {
                    // Телепортация от опасности (вода, солнце, урон): случайная точка в радиусе 32 блоков
                    t = m.pos + glm::vec3(rfl(rng) * 64.f - 32.f, (float)(rint(rng, 32) - 16), rfl(rng) * 64.f - 32.f);
                }
                glm::ivec3 b((int)std::floor(t.x), (int)std::floor(t.y), (int)std::floor(t.z));
                if (b.y < 1 || b.y > CH - 4 || !w.isChunkLoaded(floorDiv(b.x, CW), floorDiv(b.z, CW))) continue;
                while (b.y > 1 && !isSolid(w.getBlock(b.x, b.y - 1, b.z))) --b.y;
                if (!isSolid(w.getBlock(b.x, b.y - 1, b.z))) continue; // под точкой пустота
                if (isLiquid(w.getBlock(b.x, b.y, b.z)) || boxCollides(w, glm::vec3(b) + glm::vec3(0.5f, 0.f, 0.5f), 0.3f, 2.9f)) continue;
                if (isLiquid(w.getBlock(b.x, b.y - 1, b.z))) continue; // не вставать в воду
                for (int i = 0; i < 16; ++i) {
                    Particle pp = makeParticle(PType::Portal, m.pos + glm::vec3(rfl(rng) - 0.5f, rfl(rng) * 2.9f, rfl(rng) - 0.5f), glm::vec3(0.f));
                    pp.color = glm::vec3(0.7f, 0.2f, 0.9f);
                    particles.push_back(pp);
                }
                hooks.sound("mob/endermen/portal", 1.f, 1.f, &center);
                m.pos = m.prev = glm::vec3(b) + glm::vec3(0.5f, 0.f, 0.5f);
                m.motion = glm::vec3(0.f);
                m.yaw = m.prevYaw = glm::degrees(std::atan2(p.pos.z - m.pos.z, p.pos.x - m.pos.x));
                break;
            }
        }
        if (m.inLava && !isFireImmune(m.type)) { hurt(m, 4, m.pos, 0.f, false, hooks); m.fireTicks = 300; }
        if (!isFireImmune(m.type) && boxInBlock(w, m.pos, mobHalfW(m), mobHeight(m), FIRE)) {
            hurt(m, 1, m.pos, 0.f, false, hooks);
            m.fireTicks = std::max(m.fireTicks, 160);
        }
        if (m.fireTicks > 0) {
            if (m.inWater) m.fireTicks = 0;
            else {
                if (--m.fireTicks % 20 == 0) { m.invulnerable = 0; hurt(m, 1, m.pos, 0.f, false, hooks); }
                if (rint(rng, 3) == 0) {
                    glm::vec3 fp = m.pos + glm::vec3((rfl(rng) - 0.5f) * mobHalfW(m) * 2, rfl(rng) * mobHeight(m), (rfl(rng) - 0.5f) * mobHalfW(m) * 2);
                    spawnSmoke(particles, fp, true);
                }
            }
        }
        if (m.pos.y < -64.f) m.removed = true;

        // ---- Толкаемся с игроком
        glm::vec3 dp = m.pos - p.pos;
        float reach = mobHalfW(m) + PLAYER_HALF_W;
        if (std::abs(dp.x) < reach && std::abs(dp.z) < reach && dp.y < PLAYER_H && -dp.y < mobHeight(m) && !p.dead) {
            glm::vec2 d(dp.x, dp.z);
            float len = glm::length(d);
            if (len > 0.01f) {
                d /= len;
                m.motion.x += d.x * 0.05f;
                m.motion.z += d.y * 0.05f;
                p.motion.x -= d.x * 0.05f;
                p.motion.z -= d.y * 0.05f;
            }
        }

        // ---- Исчезновение монстров вдали (и всех — на мирной сложности)
        // Мирная сложность убирает монстров; исчезновение вдали — в despawn() (despawnEntity 1.0)
        if (countsAsMonster(m.type) && difficulty == 0) m.removed = true;
    }

    // ---- Зажжённый динамит
    for (size_t i = 0; i < tnts.size(); ++i) {
        PrimedTnt& t = tnts[i];
        t.prev = t.pos;
        t.motion.y -= 0.04f;
        BodyState st;
        st.onGround = t.onGround;
        moveBody(w, t.pos, 0.49f, 0.98f, t.motion, 0.f, false, st);
        t.onGround = st.onGround;
        t.motion *= 0.98f;
        if (t.onGround) { t.motion.x *= 0.7f; t.motion.z *= 0.7f; t.motion.y *= -0.5f; }
        if (--t.fuse <= 0) {
            t.dead = true;
            explosions.push_back({t.pos + glm::vec3(0, 0.49f, 0), 4.f});
        } else {
            spawnSmoke(particles, t.pos + glm::vec3(0, 1.f, 0), false);
        }
    }
    tnts.erase(std::remove_if(tnts.begin(), tnts.end(), [](const PrimedTnt& t) { return t.dead; }), tnts.end());

    // ---- Огненные шары: ускоряются, большие взрываются и поджигают, маленькие жгут цель и ставят огонь
    for (auto& f : fireballs) {
        Player& p = *nearestPlayer(f.pos, p0, false);
        f.prev = f.pos;
        if (++f.age > 600) { f.dead = true; continue; }
        spawnSmoke(particles, f.pos + glm::vec3(0, f.small ? 0.1f : 0.3f, 0), false); // дымный след
        f.motion += f.accel;
        f.motion *= 0.95f;
        glm::vec3 seg = f.motion;
        bool hitPlayer = false;
        Mob* victim = nullptr;
        float bestT = 2.f, tt;
        if (!p.dead && !f.deflected && segmentBox(f.pos, seg, p.pos - glm::vec3(0.4f, 0.1f, 0.4f), p.pos + glm::vec3(0.4f, 1.9f, 0.4f), tt) && f.age > 2) {
            hitPlayer = true;
            bestT = tt;
        }
        for (auto& m : mobs) {
            if (m.dying() || f.age < 3) continue;
            if (!f.deflected && (m.type == MobType::Ghast || m.type == MobType::Blaze)) continue;
            if (segmentBox(f.pos, seg, m.pos - glm::vec3(mobHalfW(m), 0, mobHalfW(m)), m.pos + glm::vec3(mobHalfW(m), mobHeight(m), mobHalfW(m)), tt) &&
                tt < bestT) {
                bestT = tt;
                victim = &m;
                hitPlayer = false;
            }
        }
        glm::vec3 hp = f.pos + seg;
        bool hitBlock = false;
        int n = (int)(glm::length(seg) / 0.1f) + 1;
        glm::vec3 lastFree = f.pos;
        for (int k = 1; k <= n; ++k) {
            float fr = (float)k / n;
            if (fr >= bestT) break;
            glm::vec3 q = f.pos + seg * fr;
            if (pointCollides(w, q)) { hitBlock = true; hp = q; victim = nullptr; hitPlayer = false; break; }
            lastFree = q;
        }
        if (!(hitPlayer || victim || hitBlock)) { f.pos += seg; continue; }
        f.dead = true;
        if (hitPlayer) hp = f.pos + seg * bestT;
        if (victim) hp = f.pos + seg * bestT;
        if (f.wither) {
            if (hitPlayer) {
                int dmg = scaleDamage(f.blue ? 8 : 5);
                if (dmg > 0 && hurtPlayer(p, dmg, pev)) addEffect(p, EFF_WITHER, 0, 200);
            } else if (victim) {
                hurt(*victim, f.blue ? 8 : 5, f.pos, 0.f, false, hooks);
            }
            explosions.push_back({hp, f.blue ? 2.f : 1.f});
        } else if (f.small) {
            if (hitPlayer) { if (scaleDamage(5) > 0 && hurtPlayer(p, scaleDamage(5), pev)) p.fireTicks = std::max(p.fireTicks, 100); }
            else if (victim) { hurt(*victim, 5, f.pos, 0.f, false, hooks); if (!isFireImmune(victim->type)) victim->fireTicks = 100; }
            else {
                glm::ivec3 b((int)std::floor(lastFree.x), (int)std::floor(lastFree.y), (int)std::floor(lastFree.z));
                if (w.getBlock(b.x, b.y, b.z) == AIR) w.setBlock(b.x, b.y, b.z, FIRE);
            }
        } else {
            if (hitPlayer && scaleDamage(4) > 0) hurtPlayer(p, scaleDamage(4), pev);
            // Отбитый шар убивает гаста одним попаданием (как в 1.0 — урон 1000 от игрока)
            if (victim) {
                hurt(*victim, f.deflected && victim->type == MobType::Ghast ? 1000 : 4, f.pos, 0.f, f.deflected, hooks);
                if (f.deflected && victim->type == MobType::Ghast && victim->dying()) { // «Return to Sender»
                    if (f.owner && hooks.achievementFor) hooks.achievementFor(*playerById(f.owner, p0), 19);
                    else if (hooks.achievement) hooks.achievement(19);
                }
            }
            fireExplosions.push_back(hp);
        }
    }
    fireballs.erase(std::remove_if(fireballs.begin(), fireballs.end(), [](const Fireball& f) { return f.dead; }), fireballs.end());

    for (auto& [c, power] : explosions) explode(w, c, power, p0, pev, items, particles, hooks, rng);
    for (auto& c : fireExplosions) explode(w, c, 1.f, p0, pev, items, particles, hooks, rng, true);
    for (auto& c : crystalBlasts) explode(w, c, 6.f, p0, pev, items, particles, hooks, rng);
    crystalBlasts.clear();
    mobs.erase(std::remove_if(mobs.begin(), mobs.end(), [](const Mob& m) { return m.removed; }), mobs.end());
    tickFalling(w, items, rng);
    tickOrbs(w, p0, hooks, rng);
    if (++paintingCheck_ >= 100) {
        paintingCheck_ = 0;
        checkPaintings(w, items, rng); // раз в 5 секунд
    }
    inTick_ = false;
    for (auto& nm : pending) mobs.push_back(nm);
    pending.clear();

    // ---- Стрелы
    for (auto& a : arrows) {
        // Свою стрелу подбирает стрелок; стрела скелета бьёт ближайшего
        Player& p = (a.fromPlayer && a.owner) ? *playerById(a.owner, p0) : *nearestPlayer(a.pos, p0, false);
        a.prev = a.pos;
        ++a.age;
        if (a.stuck) {
            if (a.age > 1200) a.dead = true;
            // Свои стрелы можно подобрать
            if (a.fromPlayer && a.pickup && !p.dead && glm::length(a.pos - (p.pos + glm::vec3(0, 0.9f, 0))) < 1.5f) {
                ItemStack s = makeStack(ARROW);
                if (p.creative() || (hooks.giveItemTo ? hooks.giveItemTo(p, s) : (hooks.giveItem && hooks.giveItem(s)))) {
                    a.dead = true;
                    hooks.sound("random/pop", 0.2f, ((rfl(rng) - rfl(rng)) * 0.7f + 1.f) * 2.f, nullptr);
                }
            }
            continue;
        }
        glm::vec3 seg = a.motion;
        int dmg = (int)std::ceil(glm::length(a.motion) * a.damage);
        if (a.critical) dmg += rint(rng, dmg / 2 + 2);
        bool hitSomething = false;
        // Попадание в существо
        if (a.fromPlayer) {
            for (auto& m : mobs) {
                if (m.dying()) continue;
                const MobDef& d = mobDef(m.type);
                float t;
                glm::vec3 mn = m.pos - glm::vec3(mobHalfW(m), 0, mobHalfW(m)), mx = m.pos + glm::vec3(mobHalfW(m), mobHeight(m), mobHalfW(m));
                if (segmentBox(a.pos, seg, mn, mx, t)) {
                    if (m.type == MobType::Enderman) { m.attackCounter = -1; continue; } // уворачивается от снарядов
                    if (m.type == MobType::Wither && m.health <= mobDef(m.type).maxHealth / 2) {
                        hooks.sound("random/bowhit", 1.f, 1.2f, &a.pos);
                        hitSomething = true;
                        break;
                    }
                    if (a.flame && !isFireImmune(m.type)) m.fireTicks = std::max(m.fireTicks, 100);
                    currentAttacker = a.owner;
                    bool wounded = hurt(m, dmg, a.pos - a.motion, 1.f, true, hooks);
                    currentAttacker = 0;
                    // «Sniper Duel»: скелет убит стрелой игрока дальше 50 блоков
                    if (wounded && m.dying() && m.type == MobType::Skeleton && glm::length(m.pos - p.pos) > 50.f) {
                        if (hooks.achievementFor && a.owner) hooks.achievementFor(p, 16);
                        else if (hooks.achievement) hooks.achievement(16);
                    }
                    if (wounded && a.punch > 0) {
                        // Отдача: толчок по ходу стрелы
                        glm::vec2 hd(a.motion.x, a.motion.z);
                        if (glm::length(hd) > 1e-4f) {
                            hd = glm::normalize(hd) * (a.punch * 0.6f);
                            m.motion += glm::vec3(hd.x, 0.1f, hd.y);
                        }
                    }
                    hitSomething = true;
                    break;
                }
            }
        } else if (a.age > 2) {
            // Стрела скелета задевает и мобов (крипер, убитый ею, роняет пластинку)
            for (auto& m : mobs) {
                if (m.dying() || m.type == MobType::Skeleton) continue;
                const MobDef& d = mobDef(m.type);
                float t;
                glm::vec3 mn = m.pos - glm::vec3(mobHalfW(m), 0, mobHalfW(m)), mx = m.pos + glm::vec3(mobHalfW(m), mobHeight(m), mobHalfW(m));
                if (segmentBox(a.pos, seg, mn, mx, t)) {
                    if (m.type == MobType::Enderman) { m.attackCounter = -1; continue; }
                    hurt(m, dmg, a.pos - a.motion, 1.f, false, hooks);
                    if (m.type == MobType::Creeper && m.dying()) m.recordDrop = true;
                    hitSomething = true;
                    break;
                }
            }
        }
        if (!hitSomething && !a.fromPlayer && !p.dead) { // вплотную тоже попадает (раньше ждала 3 тика и пролетала сквозь)
            float t;
            glm::vec3 mn = p.pos - glm::vec3(PLAYER_HALF_W, 0, PLAYER_HALF_W), mx = p.pos + glm::vec3(PLAYER_HALF_W, PLAYER_H, PLAYER_HALF_W);
            if (segmentBox(a.pos, seg, mn, mx, t)) {
                p.damageSource = 4;
                if (scaleDamage(dmg) > 0 && hurtPlayer(p, scaleDamage(dmg), pev)) {
                    glm::vec2 d = glm::normalize(glm::vec2(a.motion.x, a.motion.z) + glm::vec2(1e-4f));
                    p.motion.x = p.motion.x / 2 + d.x * 0.4f;
                    p.motion.z = p.motion.z / 2 + d.y * 0.4f;
                    p.motion.y = std::min(0.4f, p.motion.y / 2 + 0.4f);
                }
                hitSomething = true;
            }
        }
        if (hitSomething) {
            a.dead = true;
            hooks.sound("random/bowhit", 1.f, 1.2f / (rfl(rng) * 0.2f + 0.9f), &a.pos);
            continue;
        }
        // Попадание в блок: стрела застревает
        int n = (int)(glm::length(seg) / 0.1f) + 1;
        bool stuck = false;
        for (int i = 1; i <= n; ++i) {
            glm::vec3 q = a.pos + seg * ((float)i / n);
            if (pointCollides(w, q)) {
                a.pos = a.pos + seg * ((float)(i - 1) / n) + glm::normalize(seg) * 0.05f;
                stuck = true;
                break;
            }
        }
        if (stuck) {
            a.stuck = true;
            a.age = 0;
            a.motion = glm::vec3(0.f);
            hooks.sound("random/bowhit", 1.f, 1.2f / (rfl(rng) * 0.2f + 0.9f), &a.pos);
            continue;
        }
        a.pos += seg;
        bool water = w.getBlock((int)std::floor(a.pos.x), (int)std::floor(a.pos.y), (int)std::floor(a.pos.z)) == WATER;
        a.motion *= water ? 0.8f : 0.99f;
        a.motion.y -= 0.05f;
        if (glm::length(a.motion) > 1e-4f) a.dir = glm::normalize(a.motion);
        if (a.age > 1200 || a.pos.y < -64.f) a.dead = true;
    }
    arrows.erase(std::remove_if(arrows.begin(), arrows.end(), [](const Arrow& a) { return a.dead; }), arrows.end());

    // ---- Снежки и яйца
    for (auto& t : throwables) {
        t.prev = t.pos;
        ++t.age;
        if (t.seeking) {
            // EntityEnderEye: к цели по горизонтали, плавно замедляясь и зависая; через 80 тиков падает или разбивается
            glm::vec2 to(t.target.x - t.pos.x, t.target.z - t.pos.z);
            float dist = glm::length(to);
            float ang = std::atan2(to.y, to.x);
            float hs = glm::length(glm::vec2(t.motion.x, t.motion.z));
            hs = hs + (dist - hs) * 0.05f;
            if (dist < 1.f) hs *= 0.8f;
            t.motion.x = std::cos(ang) * hs;
            t.motion.z = std::sin(ang) * hs;
            t.motion.y += ((t.pos.y < t.target.y ? 0.3f : -0.3f) - t.motion.y) * 0.05f;
            t.pos += t.motion;
            Particle pp = makeParticle(PType::Smoke, t.pos, glm::vec3(0.f));
            pp.color = glm::vec3(0.6f, 0.2f, 0.8f);
            particles.push_back(pp);
            if (t.age > 80) {
                t.dead = true;
                glm::ivec3 b((int)std::floor(t.pos.x), (int)std::floor(t.pos.y), (int)std::floor(t.pos.z));
                if (rint(rng, 5) > 0) dropFromBlock(items, b, makeStack(EYE_OF_ENDER), rng);
                else hooks.sound("random/glass", 1.f, 1.f, &t.pos);
            }
            continue;
        }
        glm::vec3 seg = t.motion;
        // Ближайшее существо на отрезке полёта
        float bestT = 2.f;
        Mob* victim = nullptr;
        for (auto& m : mobs) {
            if (m.dying()) continue;
            const MobDef& d = mobDef(m.type);
            float tt;
            glm::vec3 mn = m.pos - glm::vec3(mobHalfW(m) + 0.3f, 0.3f, mobHalfW(m) + 0.3f);
            glm::vec3 mx = m.pos + glm::vec3(mobHalfW(m) + 0.3f, mobHeight(m) + 0.3f, mobHalfW(m) + 0.3f);
            if (segmentBox(t.pos, seg, mn, mx, tt) && tt < bestT) { bestT = tt; victim = &m; }
        }
        // Блоки: шагаем вдоль отрезка до существа
        bool hit = false;
        glm::vec3 hitPos = t.pos + seg;
        int n = (int)(glm::length(seg) / 0.1f) + 1;
        for (int k = 1; k <= n; ++k) {
            float f = (float)k / n;
            if (f >= bestT) break;
            glm::vec3 q = t.pos + seg * f;
            if (pointCollides(w, q)) {
                hitPos = t.pos + seg * ((float)(k - 1) / n);
                hit = true;
                victim = nullptr;
                break;
            }
        }
        if (victim && victim->type == MobType::Enderman) {
            victim->attackCounter = -1; // снежок/яйцо пролетает мимо, эндермен телепортируется
            victim = nullptr;
        }
        if (victim) {
            hit = true;
            hitPos = t.pos + seg * bestT;
            hurt(*victim, (t.item == SNOWBALL && victim->type == MobType::Blaze) ? 3 : 0, t.pos - t.motion, 1.f, true, hooks);
        }
        if (hit) {
            t.dead = true;
            if (t.item == POTION) {
                // Взрывное зелье: действует в радиусе 4 блоков, слабее с расстоянием
                int e = potionEffect(t.damage), amp = potionAmp(t.damage);
                auto factorAt = [&](const glm::vec3& c) { return 1.f - glm::length(c - hitPos) / 4.f; };
                std::vector<Player*> hitPlayers = netPlayers;
                if (hitPlayers.empty()) hitPlayers.push_back(&p0);
                for (Player* hp : hitPlayers) {
                    Player& p = *hp;
                    float fp = factorAt(p.pos + glm::vec3(0, 0.9f, 0));
                    if (p.dead || fp <= 0.f) continue;
                    if (e == EFF_HARM) { p.invulnerable = 0; p.damageSource = -1; hurtPlayer(p, (int)((6 << amp) * fp + 0.5f), pev, true); }
                    else if (hooks.potionOn && e != EFF_NONE) {
                        // Сервер: лечение и эффекты применяет клиент
                        hooks.potionOn(p, e, amp, e == EFF_HEAL ? (int)((4 << amp) * fp + 0.5f) : (int)(potionDuration(t.damage) * fp));
                    } else if (e == EFF_HEAL) p.health = std::min(20, p.health + (int)((4 << amp) * fp + 0.5f));
                    else if (e != EFF_NONE && (int)(potionDuration(t.damage) * fp) > 20) addEffect(p, e, amp, (int)(potionDuration(t.damage) * fp));
                }
                for (auto& m : mobs) {
                    float fm = factorAt(m.pos + glm::vec3(0, mobHeight(m) * 0.5f, 0));
                    if (fm <= 0.f || m.dying()) continue;
                    bool undead = isUndead(m.type) || m.type == MobType::PigZombie;
                    int amount = (int)(((e == EFF_HEAL) ? (4 << amp) : (6 << amp)) * fm + 0.5f);
                    if ((e == EFF_HEAL && !undead) || (e == EFF_HARM && undead)) m.health = std::min(mobDef(m.type).maxHealth, m.health + amount);
                    else if (e == EFF_HEAL || e == EFF_HARM) { m.invulnerable = 0; hurt(m, amount, hitPos, 0.f, true, hooks); }
                }
                uint32_t col = potionColor(t.damage);
                glm::vec3 pCol(((col >> 16) & 255) / 255.f, ((col >> 8) & 255) / 255.f, (col & 255) / 255.f);
                for (int i = 0; i < 30; ++i) {
                    glm::vec3 spd = glm::vec3(rfl(rng) - 0.5f, rfl(rng) * 0.5f, rfl(rng) - 0.5f) * 0.15f;
                    spawnSpell(particles, hitPos + glm::vec3(rfl(rng) - 0.5f, rfl(rng) * 0.8f, rfl(rng) - 0.5f) * 1.2f, pCol, spd);
                }
                hooks.sound("random/glass", 1.f, rfl(rng) * 0.1f + 0.9f, &hitPos);
                continue;
            }
            if (t.item == ENDER_PEARL) {
                // Жемчуг Края: бросивший переносится в точку падения и получает 5 урона
                Player& p = t.owner ? *playerById(t.owner, p0) : p0;
                if (hooks.teleport && t.owner) hooks.teleport(p, hitPos);
                if (!p.dead) {
                    p.pos = p.prevPos = hitPos;
                    p.motion = glm::vec3(0.f);
                    p.fallDistance = 0.f;
                    p.invulnerable = 0;
                    hurtPlayer(p, 5, pev, true);
                }
                hooks.sound("mob/endermen/portal", 1.f, 1.f, &hitPos);
                continue;
            }
            if (t.item == EGG && rint(rng, 8) == 0) {
                int cnt = rint(rng, 32) == 0 ? 4 : 1;
                for (int c = 0; c < cnt; ++c) spawn(MobType::Chicken, hitPos, rfl(rng) * 360.f);
            }
            int col = 14, row = 0; // «snowballpoof» в 1.0 — осколки снежка и для яйца
            itemIcon(SNOWBALL, col, row);
            spawnItemCrack(particles, hitPos, col, row);
            continue;
        }
        t.pos += seg;
        bool water = w.getBlock((int)std::floor(t.pos.x), (int)std::floor(t.pos.y), (int)std::floor(t.pos.z)) == WATER;
        t.motion *= water ? 0.8f : 0.99f;
        t.motion.y -= 0.03f;
        if (t.age > 1200 || t.pos.y < -64.f) t.dead = true;
    }
    throwables.erase(std::remove_if(throwables.begin(), throwables.end(), [](const Throwable& t) { return t.dead; }),
                     throwables.end());
}

// ---------------------------------------------------------------- Сохранение

namespace {
const uint32_t ENT_MAGIC = 0x3145434D; // "MCE1"
struct MobSave {
    uint8_t type;
    uint8_t sheared;
    int16_t health;
    float x, y, z, yaw;
};
} // namespace

bool MobManager::save(const std::string& path, const Player& p, const std::vector<ItemEntity>* items) const {
    const std::string tmpPath = path + ".tmp";
    FILE* f = openFileUtf8(tmpPath, "wb");
    if (!f) return false;
    std::fwrite(&ENT_MAGIC, 4, 1, f);
    std::fwrite(&p.xpLevel, 4, 1, f);
    std::fwrite(&p.xpProgress, 4, 1, f);
    std::fwrite(&p.xpTotal, 4, 1, f);
    uint32_t n = (uint32_t)populated.size();
    std::fwrite(&n, 4, 1, f);
    for (int64_t k : populated) std::fwrite(&k, 8, 1, f);
    std::vector<MobSave> ms;
    for (auto& m : mobs)
        if (!m.dying()) ms.push_back({(uint8_t)m.type, (uint8_t)m.sheared, (int16_t)m.health, m.pos.x, m.pos.y, m.pos.z, m.yaw});
    n = (uint32_t)ms.size();
    std::fwrite(&n, 4, 1, f);
    if (n) std::fwrite(ms.data(), sizeof(MobSave), n, f);
    // Дополнение: окрас, размер, возраст, приручение и т.п. (старые версии его не читают)
    const uint32_t MX = 0x3130584D; // "MX01"
    std::fwrite(&MX, 4, 1, f);
    for (auto& m : mobs) {
        if (m.dying()) continue;
        uint8_t ex[4] = {(uint8_t)m.color, (uint8_t)m.size,
                         (uint8_t)((m.tamed ? 1 : 0) | (m.sitting ? 2 : 0) | (m.saddled ? 4 : 0) | (m.angry ? 8 : 0) | (m.charged ? 16 : 0)),
                         m.heldBlock};
        int32_t age = m.growingAge;
        std::fwrite(ex, 1, 4, f);
        std::fwrite(&age, 4, 1, f);
    }
    const uint32_t VH = 0x31304856; // "VH01": вагонетки и лодки
    std::fwrite(&VH, 4, 1, f);
    uint32_t nv = (uint32_t)vehicles.size();
    std::fwrite(&nv, 4, 1, f);
    for (auto& v : vehicles) {
        uint8_t k = (uint8_t)v.kind;
        std::fwrite(&k, 1, 1, f);
        std::fwrite(&v.pos, sizeof(v.pos), 1, f);
        std::fwrite(&v.yaw, 4, 1, f);
        std::fwrite(&v.fuel, 4, 1, f);
        std::fwrite(&v.push, sizeof(v.push), 1, f);
        std::fwrite(v.chest.items, sizeof(ItemStack), 27, f);
    }
    const uint32_t IT = 0x31305449; // "IT01": выпавшие предметы
    std::fwrite(&IT, 4, 1, f);
    uint32_t ni = 0;
    if (items)
        for (auto& e : *items) ni += !e.dead && !e.stack.empty();
    std::fwrite(&ni, 4, 1, f);
    if (items)
        for (auto& e : *items) {
            if (e.dead || e.stack.empty()) continue;
            int32_t age = e.age, delay = e.pickupDelay;
            std::fwrite(&e.pos, sizeof(e.pos), 1, f);
            std::fwrite(&e.motion, sizeof(e.motion), 1, f);
            std::fwrite(&age, 4, 1, f);
            std::fwrite(&delay, 4, 1, f);
            std::fwrite(&e.stack, sizeof(ItemStack), 1, f);
        }
    const uint32_t PT = 0x31305450; // "PT01": картины
    std::fwrite(&PT, 4, 1, f);
    uint32_t np = 0;
    for (auto& pt : paintings) np += !pt.dead;
    std::fwrite(&np, 4, 1, f);
    for (auto& pt : paintings) {
        if (pt.dead) continue;
        int32_t v[5] = {pt.wall.x, pt.wall.y, pt.wall.z, pt.dir, pt.art};
        std::fwrite(v, 4, 5, f);
    }
    bool ok = !std::ferror(f);
    ok = std::fclose(f) == 0 && ok;
    return ok && commitFile(tmpPath, path);
}

bool MobManager::load(const std::string& path, Player& p, std::vector<ItemEntity>* items) {
    const size_t mobs0 = mobs.size(), vehicles0 = vehicles.size(), items0 = items ? items->size() : 0;
    const auto populated0 = populated;
    if (loadFrom(path, p, items)) return true;
    // Основной файл оборвался посередине: убираем уже прочитанное из него, иначе с .bak мобы,
    // транспорт и предметы на земле загрузились бы второй раз поверх
    mobs.erase(mobs.begin() + (long)std::min(mobs0, mobs.size()), mobs.end());
    vehicles.erase(vehicles.begin() + (long)std::min(vehicles0, vehicles.size()), vehicles.end());
    if (items) items->erase(items->begin() + (long)std::min(items0, items->size()), items->end());
    populated = populated0;
    return fileExistsUtf8(path + ".bak") && loadFrom(path + ".bak", p, items);
}

bool MobManager::loadFrom(const std::string& path, Player& p, std::vector<ItemEntity>* items) {
    FILE* f = openFileUtf8(path, "rb");
    if (!f) return false;
    uint32_t magic = 0, n = 0;
    bool ok = std::fread(&magic, 4, 1, f) == 1 && magic == ENT_MAGIC && std::fread(&p.xpLevel, 4, 1, f) == 1 &&
              std::fread(&p.xpProgress, 4, 1, f) == 1 && std::fread(&p.xpTotal, 4, 1, f) == 1 && std::fread(&n, 4, 1, f) == 1;
    for (uint32_t i = 0; ok && i < n; ++i) {
        int64_t k;
        ok = std::fread(&k, 8, 1, f) == 1;
        if (ok) populated.insert(k);
    }
    if (ok) ok = std::fread(&n, 4, 1, f) == 1;
    for (uint32_t i = 0; ok && i < n; ++i) {
        MobSave s;
        ok = std::fread(&s, sizeof(MobSave), 1, f) == 1 && s.type < (uint8_t)MobType::COUNT;
        if (ok) {
            Mob& m = spawn((MobType)s.type, {s.x, s.y, s.z}, s.yaw);
            m.health = s.health;
            m.sheared = s.sheared != 0;
        }
    }
    uint32_t mx = 0;
    size_t first = mobs.size() >= n ? mobs.size() - n : 0;
    if (ok && std::fread(&mx, 4, 1, f) == 1 && mx == 0x3130584D) {
        for (size_t i = first; i < mobs.size(); ++i) {
            uint8_t ex[4];
            int32_t age;
            if (std::fread(ex, 1, 4, f) != 4 || std::fread(&age, 4, 1, f) != 1) break;
            Mob& m = mobs[i];
            m.color = ex[0];
            m.size = std::max<int>(1, ex[1]);
            m.tamed = ex[2] & 1; m.sitting = ex[2] & 2; m.saddled = ex[2] & 4; m.angry = ex[2] & 8; m.charged = ex[2] & 16;
            m.heldBlock = ex[3] < BLOCK_COUNT ? ex[3] : 0;
            m.growingAge = age;
            m.scale = (m.type == MobType::Slime || m.type == MobType::MagmaCube) ? (float)m.size : (age < 0 ? 0.5f : 1.f);
        }
        uint32_t vh = 0, nv = 0;
        if (std::fread(&vh, 4, 1, f) == 1 && vh == 0x31304856 && std::fread(&nv, 4, 1, f) == 1) {
            for (uint32_t i = 0; i < nv && i < 10000; ++i) {
                Vehicle v;
                uint8_t k = 0;
                bool rok = std::fread(&k, 1, 1, f) == 1 && std::fread(&v.pos, sizeof(v.pos), 1, f) == 1 && std::fread(&v.yaw, 4, 1, f) == 1 &&
                           std::fread(&v.fuel, 4, 1, f) == 1 && std::fread(&v.push, sizeof(v.push), 1, f) == 1 &&
                           std::fread(v.chest.items, sizeof(ItemStack), 27, f) == 27;
                if (!rok || k > 3) break;
                v.kind = (VehicleKind)k;
                v.prev = v.pos;
                v.prevYaw = v.yaw;
                v.id = nextId++;
                v.chest.type = TileEntity::Chest;
                vehicles.push_back(v);
            }
            uint32_t it = 0, ni = 0;
            if (items && std::fread(&it, 4, 1, f) == 1 && it == 0x31305449 && std::fread(&ni, 4, 1, f) == 1) {
                for (uint32_t i = 0; i < ni && i < 100000; ++i) {
                    ItemEntity e;
                    int32_t age = 0, delay = 0;
                    bool rok = std::fread(&e.pos, sizeof(e.pos), 1, f) == 1 && std::fread(&e.motion, sizeof(e.motion), 1, f) == 1 &&
                               std::fread(&age, 4, 1, f) == 1 && std::fread(&delay, 4, 1, f) == 1 &&
                               std::fread(&e.stack, sizeof(ItemStack), 1, f) == 1;
                    if (!rok) break;
                    if (e.stack.empty() || !isValidItem(e.stack.id)) continue;
                    e.prev = e.pos;
                    e.age = age;
                    e.pickupDelay = delay;
                    e.bobOffset = (float)(i % 628) / 100.f;
                    items->push_back(e);
                }
                uint32_t pt = 0, np = 0;
                paintings.clear();
                if (std::fread(&pt, 4, 1, f) == 1 && pt == 0x31305450 && std::fread(&np, 4, 1, f) == 1)
                    for (uint32_t i = 0; i < np && i < 100000; ++i) {
                        int32_t v[5];
                        if (std::fread(v, 4, 5, f) != 5) break;
                        Painting q;
                        q.wall = glm::ivec3(v[0], v[1], v[2]);
                        q.dir = v[3] & 3;
                        q.art = std::clamp(v[4], 0, PAINTING_ART_COUNT - 1);
                        paintings.push_back(q);
                    }
            }
        }
    }
    std::fclose(f);
    // Сохранения прошлых версий могли накопить тысячи эндерменов/свинозомби (они не считались монстрами).
    // Если монстров слишком много — оставляем до 50 на расстоянии 24..128 от игрока (как правило спавна),
    // толпу вплотную и дальних убираем
    int monsters = 0;
    for (auto& m : mobs) monsters += countsAsMonster(m.type);
    if (monsters > 100) {
        int kept = 0;
        for (auto& m : mobs) {
            if (!countsAsMonster(m.type)) continue;
            float d = glm::length(m.pos - p.pos);
            if (d >= 24.f && d <= 128.f && m.pos.y > 0.f && kept < 50) ++kept;
            else m.removed = true;
        }
        mobs.erase(std::remove_if(mobs.begin(), mobs.end(), [](const Mob& m) { return m.removed; }), mobs.end());
    }
    return ok;
}
