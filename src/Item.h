#pragma once
// Предметы. ID < 256 — блоки, 256+ — предметы с номерами как в Minecraft 1.0.
#include <cstdint>
#include "Blocks.h"

enum ItemId : uint16_t {
    IRON_SHOVEL = 256, IRON_PICKAXE = 257, IRON_AXE = 258, FLINT_AND_STEEL = 259,
    APPLE = 260, BOW = 261, ARROW = 262, COAL = 263, DIAMOND = 264, IRON_INGOT = 265, GOLD_INGOT = 266,
    IRON_SWORD = 267, WOOD_SWORD = 268, WOOD_SHOVEL = 269, WOOD_PICKAXE = 270, WOOD_AXE = 271,
    STONE_SWORD = 272, STONE_SHOVEL = 273, STONE_PICKAXE = 274, STONE_AXE = 275,
    DIAMOND_SWORD = 276, DIAMOND_SHOVEL = 277, DIAMOND_PICKAXE = 278, DIAMOND_AXE = 279,
    STICK = 280, BOWL = 281, MUSHROOM_STEW = 282,
    GOLD_SWORD = 283, GOLD_SHOVEL = 284, GOLD_PICKAXE = 285, GOLD_AXE = 286,
    STRING = 287, FEATHER = 288, GUNPOWDER = 289,
    WOOD_HOE = 290, STONE_HOE = 291, IRON_HOE = 292, DIAMOND_HOE = 293, GOLD_HOE = 294,
    SEEDS = 295, WHEAT_ITEM = 296, BREAD = 297,
    LEATHER_HELMET = 298, LEATHER_CHESTPLATE = 299, LEATHER_LEGGINGS = 300, LEATHER_BOOTS = 301,
    CHAIN_HELMET = 302, CHAIN_CHESTPLATE = 303, CHAIN_LEGGINGS = 304, CHAIN_BOOTS = 305,
    IRON_HELMET = 306, IRON_CHESTPLATE = 307, IRON_LEGGINGS = 308, IRON_BOOTS = 309,
    DIAMOND_HELMET = 310, DIAMOND_CHESTPLATE = 311, DIAMOND_LEGGINGS = 312, DIAMOND_BOOTS = 313,
    GOLD_HELMET = 314, GOLD_CHESTPLATE = 315, GOLD_LEGGINGS = 316, GOLD_BOOTS = 317,
    FLINT = 318, RAW_PORKCHOP = 319, COOKED_PORKCHOP = 320, PAINTING = 321, GOLDEN_APPLE = 322, SIGN_ITEM = 323, WOOD_DOOR_ITEM = 324,
    BUCKET = 325, WATER_BUCKET = 326, LAVA_BUCKET = 327, IRON_DOOR_ITEM = 330, REDSTONE = 331, SNOWBALL = 332,
    LEATHER = 334, MILK_BUCKET = 335, BRICK_ITEM = 336, CLAY_BALL = 337, REEDS_ITEM = 338, PAPER = 339, BOOK = 340,
    EGG = 344, COMPASS = 345, CLOCK = 347, GLOWSTONE_DUST = 348, DYE = 351, BONE = 352, SUGAR = 353, CAKE_ITEM = 354, BED_ITEM = 355, REPEATER_ITEM = 356,
    COOKIE = 357, SLIME_BALL = 341, SADDLE = 329, PUMPKIN_SEEDS = 361, MELON_SEEDS = 362,
    MINECART = 328, BOAT = 333, CHEST_MINECART = 342, FURNACE_MINECART = 343,
    FISHING_ROD = 346, RAW_FISH = 349, COOKED_FISH = 350,
    ENDER_PEARL = 368, BLAZE_ROD = 369, GHAST_TEAR = 370, GOLD_NUGGET = 371, NETHER_WART_ITEM = 372, POTION = 373,
    GLASS_BOTTLE = 374, SPIDER_EYE = 375, FERMENTED_SPIDER_EYE = 376, BLAZE_POWDER = 377, MAGMA_CREAM = 378,
    BREWING_STAND_ITEM = 379, CAULDRON_ITEM = 380, EYE_OF_ENDER = 381, GLISTERING_MELON = 382, EXP_BOTTLE = 384, FIRE_CHARGE = 385,
    // 1.4.2 Items
    EMERALD = 388, ITEM_FRAME_ITEM = 389, FLOWER_POT_ITEM = 390, CARROT = 391, POTATO = 392,
    BAKED_POTATO = 393, POISONOUS_POTATO = 394, GOLDEN_CARROT = 396, SKULL_ITEM = 397,
    CARROT_ON_A_STICK = 398, NETHER_STAR = 399, PUMPKIN_PIE = 400,
    RECORD_13 = 2256, RECORD_CAT, RECORD_BLOCKS, RECORD_CHIRP, RECORD_FAR, RECORD_MALL, RECORD_MELLOHI, RECORD_STAL,
    RECORD_STRAD, RECORD_WARD, RECORD_11, SHEARS = 359, MELON = 360, MAP = 358,
    RAW_BEEF = 363, STEAK = 364, RAW_CHICKEN = 365, COOKED_CHICKEN = 366, ROTTEN_FLESH = 367,
    ITEM_ID_LIMIT = 2270
};

struct ItemStack {
    uint16_t id = 0;
    uint8_t count = 0;
    uint16_t damage = 0; // износ инструмента или вариант (древесный уголь = уголь:1)
    uint16_t ench[4] = {0, 0, 0, 0}; // чары: (номер << 8) | уровень, как список ench в 1.0

    bool empty() const { return id == 0 || count == 0; }
    void clear() { *this = ItemStack{}; }
    bool enchanted() const { return ench[0] != 0; }
    int enchLevel(int enchId) const {
        for (uint16_t e : ench)
            if (e && (e >> 8) == enchId) return e & 0xFF;
        return 0;
    }
    void addEnch(int enchId, int level) {
        for (uint16_t& e : ench)
            if (!e) { e = (uint16_t)((enchId << 8) | level); return; }
    }
    bool sameItem(const ItemStack& o) const {
        return id == o.id && damage == o.damage && ench[0] == o.ench[0] && ench[1] == o.ench[1] && ench[2] == o.ench[2] &&
               ench[3] == o.ench[3];
    }
};

inline ItemStack makeStack(uint16_t id, int count = 1, uint16_t damage = 0) {
    ItemStack s;
    s.id = id;
    s.count = (uint8_t)count;
    s.damage = damage;
    return s;
}

struct ToolInfo {
    Tool type = Tool::None;
    int tier = -1;         // 0 дерево/золото, 1 камень, 2 железо, 3 алмаз
    float efficiency = 1.f;
    int attack = 1;        // урон в половинках сердца
};

struct FoodInfo {
    int hunger = 0;
    float saturation = 0.f;
    uint16_t leftover = 0; // что остаётся (миска от супа)
};

struct ArmorInfo {
    int slot = -1;  // 0 шлем, 1 нагрудник, 2 поножи, 3 ботинки
    int points = 0; // очки брони (половинки иконок)
};

bool isBlockItem(uint16_t id);
bool isValidItem(uint16_t id);
const char* itemName(const ItemStack& s);
int maxStackSize(uint16_t id);
int maxDamage(uint16_t id);        // 0 — не изнашивается
ToolInfo toolInfo(uint16_t id);
FoodInfo foodInfo(uint16_t id);
ArmorInfo armorInfo(uint16_t id);
// Иконка в items.png (для блоков — false, рисуется кубиком или тайлом terrain.png)
bool itemIcon(uint16_t id, int& col, int& row, uint16_t damage = 0);

// Красители (dye:damage), как в 1.0
enum DyeColor : uint16_t {
    DYE_INK = 0, DYE_RED = 1, DYE_GREEN = 2, DYE_COCOA = 3, DYE_LAPIS = 4, DYE_PURPLE = 5, DYE_CYAN = 6,
    DYE_LIGHT_GRAY = 7, DYE_GRAY = 8, DYE_PINK = 9, DYE_LIME = 10, DYE_YELLOW = 11, DYE_LIGHT_BLUE = 12,
    DYE_MAGENTA = 13, DYE_ORANGE = 14, DYE_BONE_MEAL = 15
};
inline bool isRecord(uint16_t id) { return id >= RECORD_13 && id <= RECORD_11; }
// Имя трека пластинки (файл sounds/records/<имя>.ogg)
inline const char* recordName(uint16_t id) {
    static const char* N[11] = {"13", "cat", "blocks", "chirp", "far", "mall", "mellohi", "stal", "strad", "ward", "11"};
    return isRecord(id) ? N[id - RECORD_13] : "";
}
// Предмет с вариантами в damage (не износ): краситель, уголь
inline bool itemHasVariants(uint16_t id) { return id == DYE || id == 263 || id == SKULL_ITEM; }
// Блок, который ставит предмет (семена → посевы), AIR если никакой
uint8_t placedBlock(uint16_t id);

// Добыча блока: можно ли получить дроп и скорость ломания за тик (доля 0..1), как в 1.0
bool canHarvest(uint8_t block, const ItemStack& held);
float breakStrength(uint8_t block, const ItemStack& held, bool inWater, bool onGround);
