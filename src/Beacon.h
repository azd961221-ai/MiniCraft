#pragma once
// Маяк 1.4.2 (TileEntityBeacon). Выбранные эффекты хранятся в мете блока, чтобы не менять формат сохранений
// и чтобы выбор расходился по сети обычной правкой меты:
//   биты 0-2 — основной эффект (0 нет, 1 скорость, 2 спешка, 3 сопротивление, 4 прыгучесть, 5 сила),
//   биты 3-4 — вторичный (0 нет, 1 регенерация, 2 основной на уровень II).
#include "Potion.h"
#include "World.h"

// Эффект по номеру в мете (0 — нет)
inline int beaconEffect(int idx) {
    static const int E[6] = {EFF_NONE, EFF_SPEED, EFF_HASTE, EFF_RESISTANCE, EFF_JUMP, EFF_STRENGTH};
    return idx >= 0 && idx < 6 ? E[idx] : EFF_NONE;
}
// Ярус пирамиды, с которого доступен основной эффект (effectsList 1.4.2: 0 — скорость, спешка; 1 — сопротивление,
// прыгучесть; 2 — сила)
inline int beaconEffectTier(int idx) { return idx <= 2 ? 0 : idx <= 4 ? 1 : 2; }
inline int beaconPrimary(uint8_t meta) { int p = meta & 7; return p <= 5 ? p : 0; }
inline int beaconSecondary(uint8_t meta) { int s = (meta >> 3) & 3; return s <= 2 ? s : 0; }
inline uint8_t beaconMeta(int primary, int secondary) { return (uint8_t)((primary & 7) | ((secondary & 3) << 3)); }

inline bool isBeaconBase(uint8_t b) { return b == IRON_BLOCK || b == GOLD_BLOCK || b == DIAMOND_BLOCK || b == EMERALD_BLOCK; }
inline bool isBeaconPayment(uint16_t id) { return id == EMERALD || id == DIAMOND || id == GOLD_INGOT || id == IRON_INGOT; }

// Уровни пирамиды 0..4 (updateState): маяк видит небо, под ним сплошные слои 3x3, 5x5, 7x7, 9x9 из блоков
// железа, золота, алмаза или изумруда. 0 — маяк не работает
inline int beaconLevels(const World& w, int x, int y, int z) {
    for (int yy = y + 1; yy < CH; ++yy)
        if (lightOpacity(w.getBlock(x, yy, z)) > 0) return 0;
    int levels = 0;
    for (int i = 1; i <= 4; ++i) {
        int ly = y - i;
        if (ly < 0) break;
        bool full = true;
        for (int dx = -i; dx <= i && full; ++dx)
            for (int dz = -i; dz <= i && full; ++dz) full = isBeaconBase(w.getBlock(x + dx, ly, z + dz));
        if (!full) break;
        levels = i;
    }
    return levels;
}

// Эффекты маяка для игрока в точке pos (addEffectsToPlayers, раз в 80 тиков): радиус 10 + 10·уровень по горизонтали,
// по высоте — от маяка минус радиус и до самого верха; длительность 9 с
template <class AddFn>
inline void beaconApply(int levels, uint8_t meta, glm::ivec3 b, glm::vec3 pos, AddFn add) {
    int prim = beaconPrimary(meta), sec = beaconSecondary(meta);
    if (levels <= 0 || prim == 0 || beaconEffectTier(prim) >= levels) return;
    float r = levels * 10.f + 10.f;
    if (pos.x < b.x - r || pos.x > b.x + 1 + r || pos.z < b.z - r || pos.z > b.z + 1 + r || pos.y < b.y - r) return;
    bool two = levels >= 4 && sec == 2;
    add(beaconEffect(prim), two ? 1 : 0, 180);
    if (levels >= 4 && sec == 1) add(EFF_REGEN, 0, 180);
}
