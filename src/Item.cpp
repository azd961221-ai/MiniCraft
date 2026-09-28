#include "Item.h"
#include <algorithm>
#include <string>

namespace {

struct ItemDef {
    const char* name = nullptr;
    int col = 0, row = 0;       // иконка в items.png
    int maxStack = 64;
    int maxDamage = 0;
    ToolInfo tool;
    FoodInfo food;
    ArmorInfo armor;
    uint8_t places = AIR;
};

struct Tier { int level; int uses; float efficiency; int damage; };
const Tier WOOD_T{0, 59, 2.f, 0}, STONE_T{1, 131, 4.f, 1}, IRON_T{2, 250, 6.f, 2}, DIAMOND_T{3, 1561, 8.f, 3}, GOLD_T{0, 32, 12.f, 0};

struct Table {
    ItemDef defs[ITEM_ID_LIMIT];

    void item(uint16_t id, const char* name, int col, int row, int stack = 64) {
        ItemDef& d = defs[id];
        d.name = name; d.col = col; d.row = row; d.maxStack = stack;
    }
    void tool(uint16_t id, const char* name, int col, int row, Tool type, const Tier& t) {
        item(id, name, col, row, 1);
        ItemDef& d = defs[id];
        d.maxDamage = t.uses;
        d.tool.type = type;
        d.tool.tier = t.level;
        d.tool.efficiency = (type == Tool::Sword || type == Tool::Hoe) ? 1.f : t.efficiency;
        switch (type) {
        case Tool::Sword: d.tool.attack = 4 + t.damage; break;
        case Tool::Axe: d.tool.attack = 3 + t.damage; break;
        case Tool::Pickaxe: d.tool.attack = 2 + t.damage; break;
        case Tool::Shovel: d.tool.attack = 1 + t.damage; break;
        default: d.tool.attack = 1; break;
        }
    }
    void food(uint16_t id, const char* name, int col, int row, int hunger, float sat, int stack = 64) {
        item(id, name, col, row, stack);
        defs[id].food = {hunger, sat, 0};
    }
    // Броня: прочность = множитель материала * {11, 16, 15, 13}, очки защиты по слотам
    std::string ownedNames[ITEM_ID_LIMIT];
    void armorSet(uint16_t first, const char* mat, int col, int factor, const int pts[4]) {
        static const char* parts[4] = {"Helmet", "Chestplate", "Leggings", "Boots"};
        static const int mul[4] = {11, 16, 15, 13};
        for (int i = 0; i < 4; ++i) {
            uint16_t id = (uint16_t)(first + i);
            ownedNames[id] = std::string(mat) + " " + parts[i];
            item(id, ownedNames[id].c_str(), col, i, 1);
            defs[id].maxDamage = factor * mul[i];
            defs[id].armor = {i, pts[i]};
        }
    }

    Table() {
        const Tool P = Tool::Pickaxe, A = Tool::Axe, S = Tool::Shovel, W = Tool::Sword, H = Tool::Hoe;
        tool(WOOD_SWORD, "Wooden Sword", 0, 4, W, WOOD_T);
        tool(WOOD_SHOVEL, "Wooden Shovel", 0, 5, S, WOOD_T);
        tool(WOOD_PICKAXE, "Wooden Pickaxe", 0, 6, P, WOOD_T);
        tool(WOOD_AXE, "Wooden Axe", 0, 7, A, WOOD_T);
        tool(WOOD_HOE, "Wooden Hoe", 0, 8, H, WOOD_T);
        tool(STONE_SWORD, "Stone Sword", 1, 4, W, STONE_T);
        tool(STONE_SHOVEL, "Stone Shovel", 1, 5, S, STONE_T);
        tool(STONE_PICKAXE, "Stone Pickaxe", 1, 6, P, STONE_T);
        tool(STONE_AXE, "Stone Axe", 1, 7, A, STONE_T);
        tool(STONE_HOE, "Stone Hoe", 1, 8, H, STONE_T);
        tool(IRON_SWORD, "Iron Sword", 2, 4, W, IRON_T);
        tool(IRON_SHOVEL, "Iron Shovel", 2, 5, S, IRON_T);
        tool(IRON_PICKAXE, "Iron Pickaxe", 2, 6, P, IRON_T);
        tool(IRON_AXE, "Iron Axe", 2, 7, A, IRON_T);
        tool(IRON_HOE, "Iron Hoe", 2, 8, H, IRON_T);
        tool(DIAMOND_SWORD, "Diamond Sword", 3, 4, W, DIAMOND_T);
        tool(DIAMOND_SHOVEL, "Diamond Shovel", 3, 5, S, DIAMOND_T);
        tool(DIAMOND_PICKAXE, "Diamond Pickaxe", 3, 6, P, DIAMOND_T);
        tool(DIAMOND_AXE, "Diamond Axe", 3, 7, A, DIAMOND_T);
        tool(DIAMOND_HOE, "Diamond Hoe", 3, 8, H, DIAMOND_T);
        tool(GOLD_SWORD, "Golden Sword", 4, 4, W, GOLD_T);
        tool(GOLD_SHOVEL, "Golden Shovel", 4, 5, S, GOLD_T);
        tool(GOLD_PICKAXE, "Golden Pickaxe", 4, 6, P, GOLD_T);
        tool(GOLD_AXE, "Golden Axe", 4, 7, A, GOLD_T);
        tool(GOLD_HOE, "Golden Hoe", 4, 8, H, GOLD_T);

        const int leather[4] = {1, 3, 2, 1}, gold[4] = {2, 5, 3, 1}, iron[4] = {2, 6, 5, 2}, diamond[4] = {3, 8, 6, 3};
        const int chain[4] = {2, 5, 4, 1};
        armorSet(LEATHER_HELMET, "Leather", 0, 5, leather);
        armorSet(CHAIN_HELMET, "Chain", 1, 15, chain);
        armorSet(IRON_HELMET, "Iron", 2, 15, iron);
        armorSet(DIAMOND_HELMET, "Diamond", 3, 33, diamond);
        armorSet(GOLD_HELMET, "Golden", 4, 7, gold);
        // Кожаная броня в 1.0 называлась «Leather Cap/Tunic/Pants»
        defs[LEATHER_HELMET].name = "Leather Cap";
        defs[LEATHER_CHESTPLATE].name = "Leather Tunic";
        defs[LEATHER_LEGGINGS].name = "Leather Pants";
        defs[LEATHER_BOOTS].name = "Leather Boots";

        item(COAL, "Coal", 7, 0);
        item(DIAMOND, "Diamond", 7, 3);
        item(IRON_INGOT, "Iron Ingot", 7, 1);
        item(GOLD_INGOT, "Gold Ingot", 7, 2);
        item(STICK, "Stick", 5, 3);
        item(BOWL, "Bowl", 7, 4);
        item(STRING, "String", 8, 0);
        item(FEATHER, "Feather", 8, 1);
        item(GUNPOWDER, "Gunpowder", 8, 2);
        item(SEEDS, "Seeds", 9, 0);
        defs[SEEDS].places = WHEAT;
        item(WHEAT_ITEM, "Wheat", 9, 1);
        item(FLINT, "Flint", 6, 0);
        item(BOW, "Bow", 5, 1, 1);
        defs[BOW].maxDamage = 384;
        item(ARROW, "Arrow", 5, 2);
        item(SHEARS, "Shears", 13, 5, 1);
        defs[SHEARS].maxDamage = 238;
        item(EGG, "Egg", 12, 0, 16);
        item(SNOWBALL, "Snowball", 14, 0, 16);
        item(REEDS_ITEM, "Reeds", 11, 1);
        defs[REEDS_ITEM].places = REEDS;
        item(BUCKET, "Bucket", 10, 4, 1);
        item(WATER_BUCKET, "Water Bucket", 11, 4, 1);
        item(LAVA_BUCKET, "Lava Bucket", 12, 4, 1);
        item(LEATHER, "Leather", 7, 6);
        item(BRICK_ITEM, "Brick", 6, 1);
        item(CLAY_BALL, "Clay", 9, 3);
        item(PAPER, "Paper", 10, 3);
        item(BOOK, "Book", 11, 3);
        item(GLOWSTONE_DUST, "Glowstone Dust", 9, 4);
        item(BONE, "Bone", 12, 1);
        item(SUGAR, "Sugar", 13, 0);
        item(FLINT_AND_STEEL, "Flint and Steel", 5, 0, 1);
        defs[FLINT_AND_STEEL].maxDamage = 64;
        item(WOOD_DOOR_ITEM, "Wooden Door", 11, 2, 1);
        item(IRON_DOOR_ITEM, "Iron Door", 12, 2, 1);
        item(REDSTONE, "Redstone", 8, 3);
        item(MILK_BUCKET, "Milk", 13, 4, 1);
        item(COMPASS, "Compass", 6, 3);
        item(CLOCK, "Clock", 6, 4);
        item(DYE, "Dye", 14, 4);
        item(CAKE_ITEM, "Cake", 13, 1, 1);
        defs[CAKE_ITEM].places = CAKE;
        item(BED_ITEM, "Bed", 13, 2, 1);
        item(SLIME_BALL, "Slimeball", 14, 1);
        item(MINECART, "Minecart", 7, 8, 1);
        item(FISHING_ROD, "Fishing Rod", 5, 4, 1);
        defs[FISHING_ROD].maxDamage = 64;
        item(CHEST_MINECART, "Minecart with Chest", 7, 9, 1);
        item(FURNACE_MINECART, "Minecart with Furnace", 7, 10, 1);
        item(BOAT, "Boat", 8, 8, 1);
        item(PUMPKIN_SEEDS, "Pumpkin Seeds", 13, 3);
        defs[PUMPKIN_SEEDS].places = PUMPKIN_STEM;
        item(MELON_SEEDS, "Melon Seeds", 14, 3);
        defs[MELON_SEEDS].places = MELON_STEM;
        item(SADDLE, "Saddle", 8, 6, 1);
        item(ENDER_PEARL, "Ender Pearl", 11, 6, 16);
        item(BLAZE_ROD, "Blaze Rod", 12, 6);
        item(GHAST_TEAR, "Ghast Tear", 11, 7);
        item(GOLD_NUGGET, "Gold Nugget", 12, 7);
        item(NETHER_WART_ITEM, "Nether Wart", 13, 7);
        defs[NETHER_WART_ITEM].places = NETHER_WART;
        item(GLASS_BOTTLE, "Glass Bottle", 12, 8);
        item(POTION, "Potion", 12, 8, 1);
        food(SPIDER_EYE, "Spider Eye", 11, 8, 2, 0.8f);
        item(FERMENTED_SPIDER_EYE, "Fermented Spider Eye", 10, 8);
        item(BLAZE_POWDER, "Blaze Powder", 13, 9);
        item(MAGMA_CREAM, "Magma Cream", 13, 10);
        item(EYE_OF_ENDER, "Eye of Ender", 11, 9);
        item(GLISTERING_MELON, "Glistering Melon", 9, 8);
        item(BREWING_STAND_ITEM, "Brewing Stand", 12, 10);
        defs[BREWING_STAND_ITEM].places = BREWING_STAND;
        item(CAULDRON_ITEM, "Cauldron", 12, 9);
        defs[CAULDRON_ITEM].places = CAULDRON;
        for (int i = 0; i < 11; ++i) item((uint16_t)(RECORD_13 + i), "Music Disc", i, 15, 1);
        item(SIGN_ITEM, "Sign", 10, 2, 1);
        item(PAINTING, "Painting", 10, 1);
        item(MAP, "Map", 12, 3, 1);
        item(REPEATER_ITEM, "Redstone Repeater", 6, 5);
        defs[REDSTONE].places = REDSTONE_WIRE;
        defs[REPEATER_ITEM].places = REPEATER_OFF;

        food(APPLE, "Apple", 10, 0, 4, 0.3f);
        food(BREAD, "Bread", 9, 2, 5, 0.6f);
        food(RAW_PORKCHOP, "Raw Porkchop", 7, 5, 3, 0.3f);
        food(COOKED_PORKCHOP, "Cooked Porkchop", 8, 5, 8, 0.8f);
        food(GOLDEN_APPLE, "Golden Apple", 11, 0, 4, 1.2f);
        food(MUSHROOM_STEW, "Mushroom Stew", 8, 4, 6, 0.6f, 1);
        defs[MUSHROOM_STEW].food.leftover = BOWL;
        food(COOKIE, "Cookie", 12, 5, 1, 0.1f);
        food(MELON, "Melon", 13, 6, 2, 0.3f);
        food(RAW_BEEF, "Raw Beef", 9, 6, 3, 0.3f);
        food(STEAK, "Steak", 10, 6, 8, 0.8f);
        food(RAW_CHICKEN, "Raw Chicken", 9, 7, 2, 0.3f);
        food(COOKED_CHICKEN, "Cooked Chicken", 10, 7, 6, 0.6f);
        food(ROTTEN_FLESH, "Rotten Flesh", 11, 5, 4, 0.1f);
        food(RAW_FISH, "Raw Fish", 9, 5, 2, 0.3f);
        food(COOKED_FISH, "Cooked Fish", 10, 5, 5, 0.6f);

        // 1.4.2 Items (иконки — по items.png 1.4.2; (14,2) там — огненный шар, (10,13) пусто)
        item(EMERALD, "Emerald", 10, 11);
        item(ITEM_FRAME_ITEM, "Item Frame", 14, 12);
        item(FLOWER_POT_ITEM, "Flower Pot", 13, 11);
        defs[FLOWER_POT_ITEM].places = FLOWER_POT;
        item(CARROT_ON_A_STICK, "Carrot on a Stick", 6, 6, 1);
        defs[CARROT_ON_A_STICK].maxDamage = 25;
        item(NETHER_STAR, "Nether Star", 9, 11);
        item(EXP_BOTTLE, "Bottle o' Enchanting", 11, 10);
        item(FIRE_CHARGE, "Fire Charge", 14, 2);
        item(SKULL_ITEM, "Skeleton Skull", 0, 14); // черепа — строка 14: скелет, иссушитель, зомби, игрок, крипер
        defs[SKULL_ITEM].places = SKULL_BLOCK;

        food(CARROT, "Carrot", 8, 7, 3, 0.6f);
        defs[CARROT].places = CARROTS;
        food(POTATO, "Potato", 7, 7, 1, 0.3f);
        defs[POTATO].places = POTATOES;
        food(BAKED_POTATO, "Baked Potato", 6, 7, 5, 0.6f);
        food(POISONOUS_POTATO, "Poisonous Potato", 6, 8, 2, 0.3f);
        food(GOLDEN_CARROT, "Golden Carrot", 6, 9, 6, 1.2f);
        food(PUMPKIN_PIE, "Pumpkin Pie", 8, 9, 8, 0.8f);
    }
};

const Table& table() {
    static Table t;
    return t;
}

} // namespace

bool isBlockItem(uint16_t id) { return id > 0 && id < BLOCK_COUNT; }

bool isValidItem(uint16_t id) {
    if (isBlockItem(id)) return true;
    return id < ITEM_ID_LIMIT && table().defs[id].name != nullptr;
}

const char* itemName(const ItemStack& s) {
    if (s.id == SLAB) {
        static const char* N[6] = {"Stone Slab", "Sandstone Slab", "Wooden Slab", "Cobblestone Slab", "Brick Slab", "Stone Brick Slab"};
        return N[s.damage % 6];
    }
    if (s.id == WOOL) {
        // ItemCloth 1.0: название по цвету (мета шерсти = 15 - номер красителя)
        static const char* N[16] = {"White Wool", "Orange Wool", "Magenta Wool", "Light Blue Wool", "Yellow Wool", "Lime Wool",
                                    "Pink Wool", "Gray Wool", "Light Gray Wool", "Cyan Wool", "Purple Wool", "Blue Wool",
                                    "Brown Wool", "Green Wool", "Red Wool", "Black Wool"};
        return N[s.damage & 15];
    }
    if (s.id == STONE_BRICK) {
        static const char* N[4] = {"Stone Bricks", "Mossy Stone Bricks", "Cracked Stone Bricks", "Chiseled Stone Bricks"};
        return N[s.damage & 3];
    }
    if (s.id == PLANKS) {
        static const char* N[4] = {"Oak Wood Planks", "Spruce Wood Planks", "Birch Wood Planks", "Jungle Wood Planks"};
        return N[s.damage & 3];
    }
    if (s.id == LOG) {
        static const char* N[4] = {"Oak Wood", "Spruce Wood", "Birch Wood", "Jungle Wood"};
        return N[s.damage & 3];
    }
    if (s.id == SAPLING) {
        static const char* N[4] = {"Oak Sapling", "Spruce Sapling", "Birch Sapling", "Jungle Sapling"};
        return N[s.damage & 3];
    }
    if (s.id == LEAVES) {
        static const char* N[4] = {"Oak Leaves", "Spruce Leaves", "Birch Leaves", "Jungle Leaves"};
        return N[s.damage & 3];
    }
    if (s.id == SANDSTONE) {
        static const char* N[3] = {"Sandstone", "Chiseled Sandstone", "Smooth Sandstone"};
        return N[std::min(2, s.damage & 3)];
    }
    if (s.id == COBBLE_WALL) return (s.damage & 1) ? "Mossy Cobblestone Wall" : "Cobblestone Wall";
    if (s.id == TALL_GRASS) {
        static const char* N[3] = {"Shrub", "Grass", "Fern"};
        return N[s.damage % 3];
    }
    if (isBlockItem(s.id)) return blockName((uint8_t)s.id);
    if (isRecord(s.id)) {
        static std::string names[11];
        std::string& n = names[s.id - RECORD_13];
        if (n.empty()) n = std::string("Music Disc (C418 - ") + recordName(s.id) + ")";
        return n.c_str();
    }
    if (s.id == COAL && s.damage == 1) return "Charcoal";
    if (s.id == DYE) {
        static const char* N[16] = {"Ink Sac", "Rose Red", "Cactus Green", "Cocoa Beans", "Lapis Lazuli", "Purple Dye",
                                    "Cyan Dye", "Light Gray Dye", "Gray Dye", "Pink Dye", "Lime Dye", "Dandelion Yellow",
                                    "Light Blue Dye", "Magenta Dye", "Orange Dye", "Bone Meal"};
        return N[s.damage & 15];
    }
    if (s.id == SKULL_ITEM) {
        static const char* N[5] = {"Skeleton Skull", "Wither Skeleton Skull", "Zombie Head", "Head", "Creeper Head"};
        return N[std::min(4, (int)s.damage)];
    }
    if (s.id < ITEM_ID_LIMIT && table().defs[s.id].name) return table().defs[s.id].name;
    return "?";
}

int maxStackSize(uint16_t id) {
    if (isBlockItem(id)) return 64;
    return id < ITEM_ID_LIMIT ? table().defs[id].maxStack : 64;
}

int maxDamage(uint16_t id) { return (id >= 256 && id < ITEM_ID_LIMIT) ? table().defs[id].maxDamage : 0; }

ToolInfo toolInfo(uint16_t id) { return (id >= 256 && id < ITEM_ID_LIMIT) ? table().defs[id].tool : ToolInfo{}; }
FoodInfo foodInfo(uint16_t id) { return (id >= 256 && id < ITEM_ID_LIMIT) ? table().defs[id].food : FoodInfo{}; }
ArmorInfo armorInfo(uint16_t id) { return (id >= 256 && id < ITEM_ID_LIMIT) ? table().defs[id].armor : ArmorInfo{}; }
uint8_t placedBlock(uint16_t id) {
    if (isBlockItem(id)) return (uint8_t)id;
    return (id < ITEM_ID_LIMIT) ? table().defs[id].places : AIR;
}

bool itemIcon(uint16_t id, int& col, int& row, uint16_t damage) {
    if (id < 256 || id >= ITEM_ID_LIMIT || !table().defs[id].name) return false;
    col = table().defs[id].col;
    row = table().defs[id].row;
    if (id == DYE) { col += (damage & 15) / 8; row += (damage & 15) % 8; } // ItemDye.getIconFromDamage
    if (id == SKULL_ITEM) { col += std::min(4, (int)damage); }
    return true;
}

bool canHarvest(uint8_t block, const ItemStack& held) {
    const BlockInfo& b = blockInfo(block);
    // Снег добывается только лопатой (как в 1.0)
    if (block == SNOW_LAYER || block == SNOW_BLOCK) return toolInfo(held.id).type == Tool::Shovel;
    if (b.harvestTier < 0) return true;
    ToolInfo t = toolInfo(held.id);
    return t.type == Tool::Pickaxe && t.tier >= b.harvestTier;
}

float breakStrength(uint8_t block, const ItemStack& held, bool inWater, bool onGround) {
    float hard = blockInfo(block).hardness;
    if (hard < 0.f) return 0.f;
    if (hard == 0.f) return 1.f;
    ToolInfo t = toolInfo(held.id);
    float speed = 1.f;
    if (t.type != Tool::None && t.type == blockInfo(block).tool) speed = t.efficiency;
    int eff = held.enchLevel(32); // Эффективность
    if (eff > 0 && speed > 1.f) speed += eff * eff + 1;
    if (t.type == Tool::Sword) speed = block == COBWEB ? 15.f : 1.5f;
    if (held.id == SHEARS) speed = (block == LEAVES || block == COBWEB) ? 15.f : block == WOOL ? 5.f : 1.f;
    float s = canHarvest(block, held) ? speed / hard / 30.f : 1.f / hard / 100.f;
    if (inWater) s /= 5.f;
    if (!onGround) s /= 5.f;
    return s;
}
