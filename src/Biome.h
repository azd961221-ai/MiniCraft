#pragma once
// Биомы Minecraft 1.0: цвет травы и листвы, форма рельефа, растительность.
#include <cstdint>
#include "Blocks.h"

enum class Biome : uint8_t {
    Ocean, Plains, Desert, ExtremeHills, Forest, Taiga, Swampland, River,
    FrozenOcean, FrozenRiver, IcePlains, MushroomIsland, MushroomShore,
    Hell, Sky,
    Beach, DesertHills, ForestHills, TaigaHills, ExtremeHillsEdge, Jungle, JungleHills, IceMountains,
    COUNT
};

struct BiomeInfo {
    const char* name;
    uint32_t grassColor, foliageColor; // 0xRRGGBB
    float base, variation;             // форма рельефа (minHeight/maxHeight из 1.0)
    uint8_t top, filler;               // верхний блок и слой под ним
    bool snowy;                        // снег вместо дождя, лёд на воде
    bool dry;                          // без осадков (пустыня)
    int trees;                         // деревьев на чанк
    int grassPercent;                  // шанс высокой травы в колонке, %
    int flowers;                       // попыток цветов на чанк
    int deadBushes, cactus, reeds, mushrooms;
};

inline const BiomeInfo& biomeInfo(Biome b) {
    static const BiomeInfo INFO[(int)Biome::COUNT] = {
        // имя                 трава     листва    base   var   верх      заполнитель снег  сухо  дер трава цв  куст как трос гриб
        {"Ocean",              0x8EB971, 0x71A74D, -1.0f, 0.4f, GRASS,    DIRT,       false, false, 0,  0,    0,  0,   0,   3,   0},
        {"Plains",             0x91BD59, 0x77AB2F,  0.1f, 0.3f, GRASS,    DIRT,       false, false, 0,  12,   4,  0,   0,   3,   0},
        {"Desert",             0xBFB755, 0xAEA42A,  0.1f, 0.2f, SAND,     SAND,       false, true,  0,  0,    0,  2,   10,  6,   0},
        {"Extreme Hills",      0x8AB689, 0x6DA36B,  0.3f, 1.5f, GRASS,    DIRT,       false, false, 1,  3,    0,  0,   0,   1,   0},
        {"Forest",             0x79C05A, 0x59AE30,  0.1f, 0.3f, GRASS,    DIRT,       false, false, 10, 4,    2,  0,   0,   3,   0},
        {"Taiga",              0x86B783, 0x68A464,  0.1f, 0.4f, GRASS,    DIRT,       true,  false, 10, 3,    0,  0,   0,   1,   0},
        {"Swampland",          0x6A7039, 0x6A7039, -0.1f, 0.1f, GRASS,    DIRT,       false, false, 2,  5,    0,  0,   0,   10,  8},
        {"River",              0x8EB971, 0x71A74D, -0.5f, 0.0f, GRASS,    DIRT,       false, false, 0,  2,    0,  0,   0,   6,   0},
        {"Frozen Ocean",       0x80B497, 0x60A17B, -1.0f, 0.5f, GRASS,    DIRT,       true,  false, 0,  0,    0,  0,   0,   0,   0},
        {"Frozen River",       0x80B497, 0x60A17B, -0.5f, 0.0f, GRASS,    DIRT,       true,  false, 0,  0,    0,  0,   0,   0,   0},
        {"Ice Plains",         0x80B497, 0x60A17B,  0.1f, 0.3f, GRASS,    DIRT,       true,  false, 0,  0,    0,  0,   0,   0,   0},
        {"Mushroom Island",    0x55C93F, 0x2BBB0F,  0.2f, 1.0f, MYCELIUM, DIRT,       false, false, 0,  0,    0,  0,   0,   0,   12},
        {"Mushroom Shore",     0x55C93F, 0x2BBB0F, -1.0f, 0.1f, MYCELIUM, DIRT,       false, false, 0,  0,    0,  0,   0,   0,   4},
        {"Hell",               0xBFB755, 0xAEA42A,  0.1f, 0.3f, NETHERRACK, NETHERRACK, false, true, 0, 0,   0,  0,   0,   0,   0},
        {"Sky",                0x8EB971, 0x71A74D,  0.1f, 0.3f, DIRT,     DIRT,       false, true,  0,  0,    0,  0,   0,   0,   0},
        {"Beach",              0x91BD59, 0x77AB2F,  0.0f, 0.1f, SAND,     SAND,       false, false, 0,  0,    0,  0,   0,   0,   0},
        {"Desert Hills",       0xBFB755, 0xAEA42A,  0.3f, 0.7f, SAND,     SAND,       false, true,  0,  0,    0,  2,   10,  6,   0},
        {"Forest Hills",       0x79C05A, 0x59AE30,  0.3f, 0.7f, GRASS,    DIRT,       false, false, 10, 4,    2,  0,   0,   3,   0},
        {"Taiga Hills",        0x86B783, 0x68A464,  0.3f, 0.7f, GRASS,    DIRT,       true,  false, 10, 3,    0,  0,   0,   1,   0},
        {"Extreme Hills Edge", 0x8AB689, 0x6DA36B,  0.2f, 0.8f, GRASS,    DIRT,       false, false, 0,  2,    0,  0,   0,   0,   0},
        {"Jungle",             0x59C93C, 0x30BB0B,  0.1f, 0.4f, GRASS,    DIRT,       false, false, 40, 20,   4,  0,   0,   12,  2},
        {"Jungle Hills",       0x59C93C, 0x30BB0B,  0.3f, 0.7f, GRASS,    DIRT,       false, false, 40, 20,   4,  0,   0,   12,  2},
        {"Ice Mountains",      0x80B497, 0x60A17B,  0.3f, 0.7f, GRASS,    DIRT,       true,  false, 0,  0,    0,  0,   0,   0,   0},
    };
    return INFO[(int)b < (int)Biome::COUNT ? (int)b : 0];
}

inline bool isOceanBiome(Biome b) {
    return b == Biome::Ocean || b == Biome::FrozenOcean || b == Biome::MushroomShore || b == Biome::Beach;
}
inline bool isJungleBiome(Biome b) {
    return b == Biome::Jungle || b == Biome::JungleHills;
}
