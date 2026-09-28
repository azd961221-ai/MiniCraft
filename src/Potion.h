#pragma once
// Зелья Minecraft 1.0: значение damage у зелья — битовое поле (как в 1.0):
//   биты 0-3 — эффект, бит 5 (32) — уровень II, бит 6 (64) — продлённое, 8192 — питьевое, 16384 — взрывное.
// Основы: 0 — бутылка воды, 16 — грубое, 32 — густое, 64 — невзрачное.
#include <cstdint>
#include <string>
#include "Item.h"

enum PotionEffect : int {
    EFF_NONE = 0, EFF_REGEN = 1, EFF_SPEED = 2, EFF_FIRE_RES = 3, EFF_POISON = 4, EFF_HEAL = 5,
    EFF_NIGHT_VISION = 6,
    EFF_WEAKNESS = 8, EFF_STRENGTH = 9, EFF_SLOWNESS = 10, EFF_HARM = 12,
    EFF_INVISIBILITY = 14,
    EFF_HUNGER = 17, // не варится — от сырой курицы и гнилой плоти
    EFF_WITHER = 20  // от иссушителя и скелета-иссушителя
};

inline int potionEffect(uint16_t d) { return (d & 8192 || d & 16384) ? (d & 15) : EFF_NONE; }
inline int potionAmp(uint16_t d) { return (d & 32) ? 1 : 0; }
inline bool potionExtended(uint16_t d) { return (d & 64) != 0; }
inline bool potionSplash(uint16_t d) { return (d & 16384) != 0; }
inline bool isInstantEffect(int e) { return e == EFF_HEAL || e == EFF_HARM; }

inline const char* effectName(int e) {
    switch (e) {
    case EFF_REGEN: return "Regeneration";
    case EFF_SPEED: return "Swiftness";
    case EFF_FIRE_RES: return "Fire Resistance";
    case EFF_POISON: return "Poison";
    case EFF_HEAL: return "Healing";
    case EFF_NIGHT_VISION: return "Night Vision";
    case EFF_WEAKNESS: return "Weakness";
    case EFF_STRENGTH: return "Strength";
    case EFF_SLOWNESS: return "Slowness";
    case EFF_HARM: return "Harming";
    case EFF_INVISIBILITY: return "Invisibility";
    case EFF_HUNGER: return "Hunger";
    case EFF_WITHER: return "Wither";
    default: return "";
    }
}

inline std::string potionName(uint16_t d) {
    int e = potionEffect(d);
    if (e == EFF_NONE) {
        if (d == 0) return "Water Bottle";
        if (d == 16) return "Awkward Potion";
        if (d == 32) return "Thick Potion";
        return "Mundane Potion";
    }
    std::string n = std::string(potionSplash(d) ? "Splash Potion of " : "Potion of ") + effectName(e);
    if (potionAmp(d)) n += " II";
    return n;
}

// Имя предмета для подсказки и надписи над хотбаром (зелья — по значению)
inline std::string itemDisplayName(const ItemStack& s) { return s.id == POTION ? potionName(s.damage) : std::string(itemName(s)); }

inline uint32_t potionColor(uint16_t d) {
    switch (potionEffect(d)) {
    case EFF_REGEN: return 0xCD5CAB;
    case EFF_SPEED: return 0x7CAFC6;
    case EFF_FIRE_RES: return 0xE49A3A;
    case EFF_POISON: return 0x4E9331;
    case EFF_HEAL: return 0xF82423;
    case EFF_NIGHT_VISION: return 0x1F1FA1;
    case EFF_WEAKNESS: return 0x484D48;
    case EFF_STRENGTH: return 0x932423;
    case EFF_SLOWNESS: return 0x5A6C81;
    case EFF_HARM: return 0x430A09;
    case EFF_INVISIBILITY: return 0x7F8392;
    case EFF_WITHER: return 0x352A27;
    default: return 0x385DC6; // вода
    }
}

// Длительность в тиках (1.0): обычное / продлённое / уровень II
inline int potionDuration(uint16_t d) {
    int e = potionEffect(d);
    int base = 0, ext = 0, two = 0;
    switch (e) {
    case EFF_REGEN: case EFF_POISON: base = 900; ext = 2400; two = 440; break;
    case EFF_SPEED: case EFF_FIRE_RES: case EFF_STRENGTH: base = 3600; ext = 9600; two = 1800; break;
    case EFF_WEAKNESS: case EFF_SLOWNESS: base = 1800; ext = 4800; two = 900; break;
    default: return 0;
    }
    return potionAmp(d) ? two : potionExtended(d) ? ext : base;
}

// Результат варки: ингредиент + зелье -> новое значение (или то же, если не меняется)
inline uint16_t brewResult(uint16_t ingredient, uint16_t d) {
    int e = potionEffect(d);
    uint16_t mods = (uint16_t)(d & (32 | 64 | 16384));
    uint16_t kind = (d & 16384) ? 16384 : 8192;
    if (d == 0) { // вода
        if (ingredient == NETHER_WART_ITEM) return 16;
        if (ingredient == GLOWSTONE_DUST) return 32;
        if (ingredient == FERMENTED_SPIDER_EYE) return 8192 | EFF_WEAKNESS;
        if (ingredient == REDSTONE || ingredient == SUGAR || ingredient == MAGMA_CREAM || ingredient == GLISTERING_MELON ||
            ingredient == SPIDER_EYE || ingredient == GHAST_TEAR || ingredient == BLAZE_POWDER)
            return 64;
        return d;
    }
    if (d == 16) { // грубое
        switch (ingredient) {
        case SUGAR: return 8192 | EFF_SPEED;
        case MAGMA_CREAM: return 8192 | EFF_FIRE_RES;
        case GLISTERING_MELON: return 8192 | EFF_HEAL;
        case SPIDER_EYE: return 8192 | EFF_POISON;
        case GHAST_TEAR: return 8192 | EFF_REGEN;
        case BLAZE_POWDER: return 8192 | EFF_STRENGTH;
        case FERMENTED_SPIDER_EYE: return 8192 | EFF_WEAKNESS;
        default: return d;
        }
    }
    if (e == EFF_NONE) return d;
    switch (ingredient) {
    case FERMENTED_SPIDER_EYE: {
        int ne = e;
        if (e == EFF_SPEED || e == EFF_FIRE_RES) ne = EFF_SLOWNESS;
        else if (e == EFF_HEAL || e == EFF_POISON) ne = EFF_HARM;
        else if (e == EFF_REGEN || e == EFF_STRENGTH) ne = EFF_WEAKNESS;
        return (uint16_t)(kind | mods | ne);
    }
    case REDSTONE:
        if (isInstantEffect(e)) return d;
        return (uint16_t)(kind | ((mods | 64) & ~32) | e);
    case GLOWSTONE_DUST:
        if (e == EFF_FIRE_RES) return d;
        return (uint16_t)(kind | ((mods | 32) & ~64) | e);
    case GUNPOWDER:
        return (uint16_t)(16384 | (mods & (32 | 64)) | e);
    default: return d;
    }
}

inline bool isBrewingIngredient(uint16_t id) {
    return id == NETHER_WART_ITEM || id == GLOWSTONE_DUST || id == REDSTONE || id == FERMENTED_SPIDER_EYE || id == SUGAR ||
           id == MAGMA_CREAM || id == GLISTERING_MELON || id == SPIDER_EYE || id == GHAST_TEAR || id == BLAZE_POWDER ||
           id == GUNPOWDER;
}

// Иконка эффекта в inventory.png (x, y — левый верхний угол 18x18)
inline bool effectIcon(int e, int& u, int& v) {
    int idx = -1;
    switch (e) {
    case EFF_SPEED: idx = 0; break;
    case EFF_SLOWNESS: idx = 1; break;
    case EFF_STRENGTH: idx = 4; break;
    case EFF_WEAKNESS: idx = 5; break;
    case EFF_POISON: idx = 6; break;
    case EFF_REGEN: idx = 7; break;
    case EFF_FIRE_RES: idx = 15; break;
    case EFF_HUNGER: idx = 9; break;
    default: return false;
    }
    u = (idx % 8) * 18;
    v = 198 + (idx / 8) * 18;
    return true;
}
