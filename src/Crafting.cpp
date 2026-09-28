#include "Crafting.h"
#include <map>
#include <string>
#include <vector>

namespace {

struct Ingredient {
    uint16_t id = 0;
    int damage = -1; // -1 — любой вариант (уголь и древесный уголь)
    Ingredient() = default;
    Ingredient(uint16_t i, int d = -1) : id(i), damage(d) {}
    bool matches(const ItemStack& s) const {
        if (id == 0) return s.empty();
        return !s.empty() && s.id == id && (damage < 0 || s.damage == damage);
    }
};

struct Recipe {
    int w = 0, h = 0;
    Ingredient cells[9];
    ItemStack result;
    bool shapeless = false;
    std::vector<Ingredient> parts; // для рецептов без формы
};

struct Book {
    std::vector<Recipe> recipes;

    void shaped(ItemStack result, std::vector<std::string> rows, std::map<char, uint16_t> key) {
        std::map<char, Ingredient> k;
        for (auto& [c, id] : key) k[c] = Ingredient(id);
        shapedI(result, rows, k);
    }
    void shapedI(ItemStack result, std::vector<std::string> rows, std::map<char, Ingredient> key) {
        Recipe r;
        r.h = (int)rows.size();
        r.w = (int)rows[0].size();
        for (int y = 0; y < r.h; ++y)
            for (int x = 0; x < r.w; ++x) {
                char c = x < (int)rows[y].size() ? rows[y][x] : ' ';
                if (c != ' ') r.cells[y * r.w + x] = key.at(c);
            }
        r.result = result;
        recipes.push_back(r);
    }

    void shapeless(ItemStack result, std::vector<Ingredient> parts) {
        Recipe r;
        r.shapeless = true;
        r.parts = std::move(parts);
        r.result = result;
        recipes.push_back(r);
    }

    Book() {
        using S = std::vector<std::string>;
        for (uint16_t d = 0; d < 4; ++d) shapedI(makeStack(PLANKS, 4, d), S{"#"}, {{'#', Ingredient(LOG, d)}}); // доски своей породы
        shaped(makeStack(STICK, 4), S{"#", "#"}, {{'#', PLANKS}});
        shaped(makeStack(TORCH, 4), S{"X", "#"}, {{'X', COAL}, {'#', STICK}});
        shaped(makeStack(CRAFTING_TABLE), S{"##", "##"}, {{'#', PLANKS}});
        shaped(makeStack(FURNACE), S{"###", "# #", "###"}, {{'#', COBBLE}});
        shaped(makeStack(CHEST), S{"###", "# #", "###"}, {{'#', PLANKS}});

        // Инструменты: доски, булыжник, железо, алмаз, золото
        struct Mat { uint16_t item; uint16_t sword, shovel, pick, axe, hoe; };
        const Mat mats[] = {
            {PLANKS, WOOD_SWORD, WOOD_SHOVEL, WOOD_PICKAXE, WOOD_AXE, WOOD_HOE},
            {COBBLE, STONE_SWORD, STONE_SHOVEL, STONE_PICKAXE, STONE_AXE, STONE_HOE},
            {IRON_INGOT, IRON_SWORD, IRON_SHOVEL, IRON_PICKAXE, IRON_AXE, IRON_HOE},
            {DIAMOND, DIAMOND_SWORD, DIAMOND_SHOVEL, DIAMOND_PICKAXE, DIAMOND_AXE, DIAMOND_HOE},
            {GOLD_INGOT, GOLD_SWORD, GOLD_SHOVEL, GOLD_PICKAXE, GOLD_AXE, GOLD_HOE},
        };
        for (const Mat& m : mats) {
            std::map<char, uint16_t> k = {{'X', m.item}, {'#', STICK}};
            shaped(makeStack(m.pick), S{"XXX", " # ", " # "}, k);
            shaped(makeStack(m.axe), S{"XX", "X#", " #"}, k);
            shaped(makeStack(m.shovel), S{"X", "#", "#"}, k);
            shaped(makeStack(m.hoe), S{"XX", " #", " #"}, k);
            shaped(makeStack(m.sword), S{"X", "X", "#"}, k);
        }

        // Броня: кожа, железо, алмаз, золото
        const std::pair<uint16_t, uint16_t> armor[] = {
            {LEATHER, LEATHER_HELMET}, {IRON_INGOT, IRON_HELMET}, {DIAMOND, DIAMOND_HELMET}, {GOLD_INGOT, GOLD_HELMET}};
        for (auto [mat, first] : armor) {
            std::map<char, uint16_t> k = {{'X', mat}};
            shaped(makeStack(first), S{"XXX", "X X"}, k);
            shaped(makeStack(first + 1), S{"X X", "XXX", "XXX"}, k);
            shaped(makeStack(first + 2), S{"XXX", "X X", "X X"}, k);
            shaped(makeStack(first + 3), S{"X X", "X X"}, k);
        }

        // Блоки из ресурсов и обратно
        const std::pair<uint16_t, uint16_t> storage[] = {{IRON_INGOT, IRON_BLOCK}, {GOLD_INGOT, GOLD_BLOCK}, {DIAMOND, DIAMOND_BLOCK}};
        for (auto [item, block] : storage) {
            shaped(makeStack(block), S{"###", "###", "###"}, {{'#', item}});
            shaped(makeStack(item, 9), S{"#"}, {{'#', block}});
        }

        shaped(makeStack(BREAD), S{"###"}, {{'#', WHEAT_ITEM}});
        shaped(makeStack(BOWL, 4), S{"# #", " # "}, {{'#', PLANKS}});
        shaped(makeStack(GOLDEN_APPLE), S{"###", "#X#", "###"}, {{'#', GOLD_BLOCK}, {'X', APPLE}});
        shaped(makeStack(GLOWSTONE), S{"##", "##"}, {{'#', GLOWSTONE_DUST}});
        shaped(makeStack(CLAY), S{"##", "##"}, {{'#', CLAY_BALL}});
        shaped(makeStack(BRICK), S{"##", "##"}, {{'#', BRICK_ITEM}});
        shaped(makeStack(SANDSTONE), S{"##", "##"}, {{'#', SAND}});
        shapedI(makeStack(SANDSTONE, 4, 2), S{"##", "##"}, {{'#', Ingredient(SANDSTONE, 0)}});  // гладкий (1.4.2)
        shapedI(makeStack(SANDSTONE, 1, 1), S{"#", "#"}, {{'#', Ingredient(SLAB, 1)}});         // резной из двух плит песчаника
        shaped(makeStack(STONE_BRICK, 4), S{"##", "##"}, {{'#', STONE}});
        shaped(makeStack(WOOL), S{"##", "##"}, {{'#', STRING}});
        shaped(makeStack(BOOKSHELF), S{"###", "XXX", "###"}, {{'#', PLANKS}, {'X', BOOK}});
        shaped(makeStack(BOOK), S{"#", "#", "#"}, {{'#', PAPER}});
        shaped(makeStack(TNT), S{"X#X", "#X#", "X#X"}, {{'X', GUNPOWDER}, {'#', SAND}});
        shaped(makeStack(BUCKET), S{"# #", " # "}, {{'#', IRON_INGOT}});
        shaped(makeStack(BOW), S{" #X", "# X", " #X"}, {{'#', STICK}, {'X', STRING}});
        shaped(makeStack(ARROW, 4), S{"X", "#", "Y"}, {{'X', FLINT}, {'#', STICK}, {'Y', FEATHER}});
        shaped(makeStack(SHEARS), S{" #", "# "}, {{'#', IRON_INGOT}});
        shaped(makeStack(FENCE, 2), S{"###", "###"}, {{'#', STICK}});
        shaped(makeStack(JACK_O_LANTERN), S{"A", "B"}, {{'A', PUMPKIN}, {'B', TORCH}});
        shaped(makeStack(SUGAR), S{"#"}, {{'#', REEDS_ITEM}});
        shaped(makeStack(PAPER, 3), S{"###"}, {{'#', REEDS_ITEM}});
        shaped(makeStack(SNOW_BLOCK), S{"##", "##"}, {{'#', SNOWBALL}});
        shaped(makeStack(RAIL, 16), S{"X X", "X#X", "X X"}, {{'X', IRON_INGOT}, {'#', STICK}});
        shaped(makeStack(MUSHROOM_STEW), S{"Y", "X", "#"}, {{'Y', BROWN_MUSHROOM}, {'X', RED_MUSHROOM}, {'#', BOWL}});
        shaped(makeStack(MUSHROOM_STEW), S{"Y", "X", "#"}, {{'Y', RED_MUSHROOM}, {'X', BROWN_MUSHROOM}, {'#', BOWL}});

        // Полублоки (3 штуки в 1.0) и ступеньки (4 штуки)
        const uint16_t slabMats[6] = {STONE, SANDSTONE, PLANKS, COBBLE, BRICK, STONE_BRICK};
        for (int i = 0; i < 6; ++i) shaped(makeStack(SLAB, 3, (uint16_t)i), S{"###"}, {{'#', slabMats[i]}});
        const std::pair<uint16_t, uint16_t> stairs[] = {{COBBLE, COBBLE_STAIRS}, {BRICK, BRICK_STAIRS}, {STONE_BRICK, STONEBRICK_STAIRS},
                                                        {SANDSTONE, SANDSTONE_STAIRS}};
        for (auto [mat, st] : stairs) shaped(makeStack(st, 4), S{"#  ", "## ", "###"}, {{'#', mat}});
        // Деревянные ступени 1.4.2 — своей породы
        const uint16_t woodStairs[4] = {WOOD_STAIRS, SPRUCE_STAIRS, BIRCH_STAIRS, JUNGLE_STAIRS};
        for (uint16_t d = 0; d < 4; ++d) shapedI(makeStack(woodStairs[d], 4), S{"#  ", "## ", "###"}, {{'#', Ingredient(PLANKS, d)}});
        shaped(makeStack(REDSTONE_LAMP_OFF), S{" R ", "RGR", " R "}, {{'R', REDSTONE}, {'G', GLOWSTONE}});
        shapeless(makeStack(FIRE_CHARGE, 3), {Ingredient(GUNPOWDER), Ingredient(BLAZE_POWDER), Ingredient(COAL)});

        shaped(makeStack(LADDER, 2), S{"# #", "###", "# #"}, {{'#', STICK}});
        shaped(makeStack(WOOD_DOOR_ITEM), S{"##", "##", "##"}, {{'#', PLANKS}});
        shaped(makeStack(IRON_DOOR_ITEM), S{"##", "##", "##"}, {{'#', IRON_INGOT}});
        shaped(makeStack(TRAPDOOR, 2), S{"###", "###"}, {{'#', PLANKS}});
        shaped(makeStack(FENCE_GATE), S{"#X#", "#X#"}, {{'#', STICK}, {'X', PLANKS}});
        shaped(makeStack(GLASS_PANE, 16), S{"###", "###"}, {{'#', GLASS}});
        shaped(makeStack(IRON_BARS, 16), S{"###", "###"}, {{'#', IRON_INGOT}});
        shapedI(makeStack(LAPIS_BLOCK), S{"###", "###", "###"}, {{'#', Ingredient(DYE, DYE_LAPIS)}});
        shaped(makeStack(DYE, 9, DYE_LAPIS), S{"#"}, {{'#', LAPIS_BLOCK}});
        shapeless(makeStack(FLINT_AND_STEEL), {Ingredient(IRON_INGOT), Ingredient(FLINT)});
        shaped(makeStack(COMPASS), S{" # ", "#X#", " # "}, {{'#', IRON_INGOT}, {'X', REDSTONE}});
        shaped(makeStack(CLOCK), S{" # ", "#X#", " # "}, {{'#', GOLD_INGOT}, {'X', REDSTONE}});
        shaped(makeStack(CAKE_ITEM), S{"AAA", "BEB", "CCC"}, {{'A', MILK_BUCKET}, {'B', SUGAR}, {'E', EGG}, {'C', WHEAT_ITEM}});
        shapedI(makeStack(COOKIE, 8), S{"#X#"}, {{'#', Ingredient(WHEAT_ITEM)}, {'X', Ingredient(DYE, DYE_COCOA)}});

        // Кровать, табличка, редстоун
        shapedI(makeStack(BED_ITEM), S{"###", "XXX"}, {{'#', Ingredient(WOOL)}, {'X', Ingredient(PLANKS)}});
        shaped(makeStack(MAP), S{"###", "#X#", "###"}, {{'#', PAPER}, {'X', COMPASS}});
        shapedI(makeStack(PAINTING), S{"###", "#X#", "###"}, {{'#', Ingredient(STICK)}, {'X', Ingredient(WOOL)}});
        shaped(makeStack(SIGN_ITEM), S{"###", "###", " X "}, {{'#', PLANKS}, {'X', STICK}});
        shaped(makeStack(REDSTONE_TORCH_ON), S{"X", "#"}, {{'X', REDSTONE}, {'#', STICK}});
        shaped(makeStack(LEVER), S{"X", "#"}, {{'X', STICK}, {'#', COBBLE}});
        shaped(makeStack(STONE_BUTTON), S{"#", "#"}, {{'#', STONE}});
        shaped(makeStack(STONE_PLATE), S{"##"}, {{'#', STONE}});
        shaped(makeStack(WOOD_PLATE), S{"##"}, {{'#', PLANKS}});
        shaped(makeStack(REPEATER_ITEM), S{"#X#", "III"}, {{'#', REDSTONE_TORCH_ON}, {'X', REDSTONE}, {'I', STONE}});
        shaped(makeStack(NOTE_BLOCK), S{"###", "#X#", "###"}, {{'#', PLANKS}, {'X', REDSTONE}});
        shaped(makeStack(PISTON), S{"TTT", "#X#", "#R#"}, {{'T', PLANKS}, {'#', COBBLE}, {'X', IRON_INGOT}, {'R', REDSTONE}});
        shaped(makeStack(STICKY_PISTON), S{"S", "P"}, {{'S', SLIME_BALL}, {'P', PISTON}});
        shaped(makeStack(PUMPKIN_SEEDS, 4), S{"#"}, {{'#', PUMPKIN}});
        shaped(makeStack(MINECART), S{"# #", "###"}, {{'#', IRON_INGOT}});
        shaped(makeStack(FISHING_ROD), S{"  #", " #X", "# X"}, {{'#', STICK}, {'X', STRING}});
        shaped(makeStack(CHEST_MINECART), S{"C", "M"}, {{'C', CHEST}, {'M', MINECART}});
        shaped(makeStack(FURNACE_MINECART), S{"F", "M"}, {{'F', FURNACE}, {'M', MINECART}});
        shaped(makeStack(BOAT), S{"# #", "###"}, {{'#', PLANKS}});
        shaped(makeStack(POWERED_RAIL, 6), S{"X X", "X#X", "XRX"}, {{'X', GOLD_INGOT}, {'#', STICK}, {'R', REDSTONE}});
        shaped(makeStack(DETECTOR_RAIL, 6), S{"X X", "X#X", "XRX"}, {{'X', IRON_INGOT}, {'#', STONE_PLATE}, {'R', REDSTONE}});
        shaped(makeStack(MELON_SEEDS), S{"#"}, {{'#', MELON}});
        shaped(makeStack(MELON_BLOCK), S{"###", "###", "###"}, {{'#', MELON}});
        // Предметы Незера и Края (1.0)
        shaped(makeStack(NETHER_FENCE, 6), S{"###", "###"}, {{'#', NETHER_BRICK}});
        shaped(makeStack(ENCHANT_TABLE), S{" B ", "D#D", "###"}, {{'B', BOOK}, {'D', DIAMOND}, {'#', OBSIDIAN}});
        shaped(makeStack(BREWING_STAND_ITEM), S{" B ", "###"}, {{'B', BLAZE_ROD}, {'#', COBBLE}});
        shaped(makeStack(CAULDRON_ITEM), S{"# #", "# #", "###"}, {{'#', IRON_INGOT}});
        shaped(makeStack(NETHER_STAIRS, 4), S{"#  ", "## ", "###"}, {{'#', NETHER_BRICK}});
        shaped(makeStack(GOLD_INGOT), S{"###", "###", "###"}, {{'#', GOLD_NUGGET}});
        shaped(makeStack(GOLD_NUGGET, 9), S{"#"}, {{'#', GOLD_INGOT}});
        shaped(makeStack(BLAZE_POWDER, 2), S{"#"}, {{'#', BLAZE_ROD}});
        shapeless(makeStack(EYE_OF_ENDER), {Ingredient(ENDER_PEARL), Ingredient(BLAZE_POWDER)});
        shapeless(makeStack(MAGMA_CREAM), {Ingredient(SLIME_BALL), Ingredient(BLAZE_POWDER)});
        shapeless(makeStack(FERMENTED_SPIDER_EYE), {Ingredient(SPIDER_EYE), Ingredient(BROWN_MUSHROOM), Ingredient(SUGAR)});
        shaped(makeStack(GLASS_BOTTLE, 3), S{"# #", " # "}, {{'#', GLASS}});
        shaped(makeStack(GLISTERING_MELON), S{"###", "#X#", "###"}, {{'#', GOLD_NUGGET}, {'X', MELON}});
        shaped(makeStack(DISPENSER), S{"###", "#X#", "#R#"}, {{'#', COBBLE}, {'X', BOW}, {'R', REDSTONE}});
        shaped(makeStack(JUKEBOX), S{"###", "#X#", "###"}, {{'#', PLANKS}, {'X', DIAMOND}});

        // Красители и их смешивание (RecipesDyes)
        auto dye = [](int d) { return Ingredient(DYE, d); };
        shapeless(makeStack(DYE, 3, DYE_BONE_MEAL), {Ingredient(BONE)});
        shapeless(makeStack(DYE, 2, DYE_RED), {Ingredient(ROSE)});
        shapeless(makeStack(DYE, 2, DYE_YELLOW), {Ingredient(DANDELION)});
        shapeless(makeStack(DYE, 2, DYE_ORANGE), {dye(DYE_RED), dye(DYE_YELLOW)});
        shapeless(makeStack(DYE, 2, DYE_LIME), {dye(DYE_GREEN), dye(DYE_BONE_MEAL)});
        shapeless(makeStack(DYE, 2, DYE_GRAY), {dye(DYE_INK), dye(DYE_BONE_MEAL)});
        shapeless(makeStack(DYE, 2, DYE_LIGHT_GRAY), {dye(DYE_GRAY), dye(DYE_BONE_MEAL)});
        shapeless(makeStack(DYE, 3, DYE_LIGHT_GRAY), {dye(DYE_INK), dye(DYE_BONE_MEAL), dye(DYE_BONE_MEAL)});
        shapeless(makeStack(DYE, 2, DYE_LIGHT_BLUE), {dye(DYE_LAPIS), dye(DYE_BONE_MEAL)});
        shapeless(makeStack(DYE, 2, DYE_CYAN), {dye(DYE_LAPIS), dye(DYE_GREEN)});
        shapeless(makeStack(DYE, 2, DYE_PURPLE), {dye(DYE_LAPIS), dye(DYE_RED)});
        shapeless(makeStack(DYE, 2, DYE_MAGENTA), {dye(DYE_PURPLE), dye(DYE_PINK)});
        shapeless(makeStack(DYE, 3, DYE_MAGENTA), {dye(DYE_LAPIS), dye(DYE_RED), dye(DYE_PINK)});
        shapeless(makeStack(DYE, 4, DYE_MAGENTA), {dye(DYE_LAPIS), dye(DYE_RED), dye(DYE_RED), dye(DYE_BONE_MEAL)});
        shapeless(makeStack(DYE, 2, DYE_PINK), {dye(DYE_RED), dye(DYE_BONE_MEAL)});
        // Крашеная шерсть: краситель + белая шерсть (цвет шерсти = 15 - краситель)
        for (int d = 0; d < 16; ++d) shapeless(makeStack(WOOL, 1, (uint16_t)(15 - d)), {dye(d), Ingredient(WOOL, 0)});

        // 1.4.2 Recipes
        shaped(makeStack(EMERALD_BLOCK), S{"###", "###", "###"}, {{'#', EMERALD}});
        shaped(makeStack(EMERALD, 9), S{"#"}, {{'#', EMERALD_BLOCK}});
        shaped(makeStack(COBBLE_WALL, 6), S{"###", "###"}, {{'#', COBBLE}});
        shaped(makeStack(COBBLE_WALL, 6, 1), S{"###", "###"}, {{'#', MOSSY_COBBLE}});
        shaped(makeStack(WOOD_BUTTON), S{"#"}, {{'#', PLANKS}});
        shaped(makeStack(FLOWER_POT_ITEM), S{"# #", " # "}, {{'#', BRICK_ITEM}});
        shaped(makeStack(ANVIL), S{"BBB", " I ", "III"}, {{'B', IRON_BLOCK}, {'I', IRON_INGOT}});
        shaped(makeStack(BEACON), S{"GGG", "GSG", "OOO"}, {{'G', GLASS}, {'S', NETHER_STAR}, {'O', OBSIDIAN}});
        shaped(makeStack(ENDER_CHEST), S{"OOO", "OEO", "OOO"}, {{'O', OBSIDIAN}, {'E', EYE_OF_ENDER}});
        shaped(makeStack(ITEM_FRAME_ITEM), S{"###", "#L#", "###"}, {{'#', STICK}, {'L', LEATHER}});
        shaped(makeStack(GOLDEN_CARROT), S{"###", "#X#", "###"}, {{'#', GOLD_NUGGET}, {'X', CARROT}});
        shapeless(makeStack(CARROT_ON_A_STICK), {Ingredient(FISHING_ROD), Ingredient(CARROT)});
        shapeless(makeStack(PUMPKIN_PIE), {Ingredient(PUMPKIN), Ingredient(SUGAR), Ingredient(EGG)});
    }
};

const Book& book() {
    static Book b;
    return b;
}

} // namespace

ItemStack findRecipe(const ItemStack* grid, int width) {
    // Ограничивающий прямоугольник непустых клеток
    int minX = width, minY = width, maxX = -1, maxY = -1;
    for (int y = 0; y < width; ++y)
        for (int x = 0; x < width; ++x)
            if (!grid[y * width + x].empty()) {
                minX = std::min(minX, x); maxX = std::max(maxX, x);
                minY = std::min(minY, y); maxY = std::max(maxY, y);
            }
    if (maxX < 0) return {};
    int bw = maxX - minX + 1, bh = maxY - minY + 1;

    // Ремонт (1.0): два одинаковых изношенных предмета — прочности складываются плюс 10% от максимума
    {
        const ItemStack* a = nullptr;
        const ItemStack* b = nullptr;
        int n = 0;
        for (int i = 0; i < width * width; ++i)
            if (!grid[i].empty()) { ++n; (a ? b : a) = &grid[i]; }
        if (n == 2 && a->id == b->id && a->count == 1 && b->count == 1 && maxDamage(a->id) > 0) {
            int md = maxDamage(a->id);
            int left = (md - a->damage) + (md - b->damage) + md / 10;
            ItemStack r = makeStack(a->id);
            r.damage = (uint16_t)std::max(0, md - left);
            return r;
        }
    }

    for (const Recipe& r : book().recipes) {
        if (r.shapeless) {
            std::vector<const ItemStack*> items;
            for (int i = 0; i < width * width; ++i)
                if (!grid[i].empty()) items.push_back(&grid[i]);
            if (items.size() != r.parts.size()) continue;
            std::vector<bool> used(items.size(), false);
            bool ok = true;
            for (const Ingredient& ing : r.parts) {
                bool found = false;
                for (size_t i = 0; i < items.size() && !found; ++i)
                    if (!used[i] && ing.matches(*items[i])) { used[i] = true; found = true; }
                if (!found) { ok = false; break; }
            }
            if (ok) return r.result;
            continue;
        }
        if (r.w != bw || r.h != bh) continue;
        // Рецепты можно зеркалить по горизонтали, как в оригинале
        for (int mirror = 0; mirror < 2; ++mirror) {
            bool ok = true;
            for (int y = 0; y < bh && ok; ++y)
                for (int x = 0; x < bw && ok; ++x) {
                    int rx = mirror ? bw - 1 - x : x;
                    ok = r.cells[y * r.w + rx].matches(grid[(minY + y) * width + minX + x]);
                }
            if (ok) return r.result;
        }
    }
    return {};
}

std::vector<ItemStack> blockDrops(uint8_t block, uint8_t meta, uint32_t& rng) {
    auto roll = [&](int n) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (int)(rng % (uint32_t)n);
    };
    switch (block) {
    case AIR: case WATER: case LAVA: case BEDROCK: case GLASS: case ICE: case DEAD_BUSH: case MOB_SPAWNER:
    case GLASS_PANE: case FIRE: case CAKE: return {};
    case SLAB: return {makeStack(SLAB, 1, (uint16_t)(meta & 7))};
    case DOUBLE_SLAB: return {makeStack(SLAB, 2, (uint16_t)(meta & 7))};
    case WOOL: return {makeStack(WOOL, 1, (uint16_t)(meta & 15))};
    case WOOD_DOOR: if (meta & 8) return {}; return {makeStack(WOOD_DOOR_ITEM)};
    case IRON_DOOR: if (meta & 8) return {}; return {makeStack(IRON_DOOR_ITEM)};
    case LAPIS_ORE: return {makeStack(DYE, 4 + roll(5), DYE_LAPIS)};
    case BED: if (meta & 8) return {}; return {makeStack(BED_ITEM)};
    case PISTON_HEAD: case PORTAL: case END_PORTAL: case END_PORTAL_FRAME: case MONSTER_EGG: case VINE: return {};
    case MELON_BLOCK: return {makeStack(MELON, 3 + roll(5))};
    case PUMPKIN_STEM: return {makeStack(PUMPKIN_SEEDS, 1)};
    case MELON_STEM: return {makeStack(MELON_SEEDS, 1)};
    case STONE_BRICK: return {makeStack(STONE_BRICK, 1, (uint16_t)(meta & 3))};
    case BREWING_STAND: return {makeStack(BREWING_STAND_ITEM)};
    case CAULDRON: return {makeStack(CAULDRON_ITEM)};
    case NETHER_WART: return {makeStack(NETHER_WART_ITEM, (meta & 3) >= 3 ? 2 + roll(3) : 1)};
    case SIGN_POST: case WALL_SIGN: return {makeStack(SIGN_ITEM)};
    case REDSTONE_WIRE: return {makeStack(REDSTONE)};
    case RAIL: case POWERED_RAIL: case DETECTOR_RAIL: return {makeStack(block)};
    case REDSTONE_TORCH_OFF: case REDSTONE_TORCH_ON: return {makeStack(REDSTONE_TORCH_ON)};
    case REPEATER_OFF: case REPEATER_ON: return {makeStack(REPEATER_ITEM)};
    case REDSTONE_ORE: return {makeStack(REDSTONE, 4 + roll(2))};
    case SNOW_LAYER: return {makeStack(SNOWBALL)};
    case SNOW_BLOCK: return {makeStack(SNOWBALL, 4)};
    case MYCELIUM: return {makeStack(DIRT)};
    case REEDS: return {makeStack(REEDS_ITEM)};
    case COBWEB: return {makeStack(STRING)};
    case LOG: return {makeStack(LOG, 1, (uint16_t)(meta & 3))};
    case PLANKS: return {makeStack(PLANKS, 1, (uint16_t)(meta & 3))};
    case SANDSTONE: return {makeStack(SANDSTONE, 1, (uint16_t)(meta & 3))};
    case GRASS: case SNOW: case FARMLAND: return {makeStack(DIRT)};
    case STONE: return {makeStack(COBBLE)};
    case COAL_ORE: return {makeStack(COAL)};
    case DIAMOND_ORE: return {makeStack(DIAMOND)};
    case FURNACE_LIT: return {makeStack(FURNACE)};
    case CLAY: return {makeStack(CLAY_BALL, 4)};
    case GLOWSTONE: return {makeStack(GLOWSTONE_DUST, 2 + roll(3))};
    case BOOKSHELF: return {makeStack(BOOK, 3)};
    case GRAVEL: return {roll(10) == 0 ? makeStack(FLINT) : makeStack(GRAVEL)};
    case TALL_GRASS: if (roll(8) == 0) return {makeStack(SEEDS)}; return {};
    case LEAVES: {
        std::vector<ItemStack> d;
        if (roll(20) == 0) d.push_back(makeStack(SAPLING, 1, (uint16_t)(meta & 3)));
        if (roll(200) == 0) d.push_back(makeStack(APPLE));
        return d;
    }
    case WHEAT: {
        std::vector<ItemStack> d;
        if (meta >= 7) d.push_back(makeStack(WHEAT_ITEM));
        int seeds = 0;
        for (int i = 0; i < 3; ++i) if (roll(15) <= meta) ++seeds;
        if (seeds > 0) d.push_back(makeStack(SEEDS, seeds));
        return d;
    }
    case CARROTS: {
        return {makeStack(CARROT, meta >= 7 ? 1 + roll(4) : 1)};
    }
    case POTATOES: {
        std::vector<ItemStack> d;
        d.push_back(makeStack(POTATO, meta >= 7 ? 1 + roll(4) : 1));
        if (meta >= 7 && roll(50) == 0) d.push_back(makeStack(POISONOUS_POTATO));
        return d;
    }
    case EMERALD_ORE: return {makeStack(EMERALD)};
    case COBBLE_WALL: return {makeStack(COBBLE_WALL, 1, (uint16_t)(meta & 1))};
    case FLOWER_POT: {
        std::vector<ItemStack> d{makeStack(FLOWER_POT_ITEM)};
        uint8_t pb, pm;
        if (flowerPotPlant(meta, pb, pm)) d.push_back(makeStack(pb, 1, pm));
        return d;
    }
    case SKULL_BLOCK: return {makeStack(SKULL_ITEM, 1, (uint16_t)(meta & 7))};
    case ENDER_CHEST: return {makeStack(OBSIDIAN, 8)};
    case COMMAND_BLOCK: return {};
    case REDSTONE_LAMP_ON: return {makeStack(REDSTONE_LAMP_OFF)};
    default: return {makeStack(block)};
    }
}

ItemStack craftLeftover(const ItemStack& s) {
    if (s.id == MILK_BUCKET || s.id == WATER_BUCKET || s.id == LAVA_BUCKET) return makeStack(BUCKET);
    return {};
}

ItemStack smeltingResult(const ItemStack& in) {
    switch (in.id) {
    case IRON_ORE: return makeStack(IRON_INGOT);
    case GOLD_ORE: return makeStack(GOLD_INGOT);
    case DIAMOND_ORE: return makeStack(DIAMOND);
    case EMERALD_ORE: return makeStack(EMERALD);
    case POTATO: return makeStack(BAKED_POTATO);
    case SAND: return makeStack(GLASS);
    case COBBLE: return makeStack(STONE);
    case LOG: return makeStack(COAL, 1, 1); // древесный уголь
    case CLAY_BALL: return makeStack(BRICK_ITEM);
    case CACTUS: return makeStack(DYE, 1, DYE_GREEN);
    case RAW_PORKCHOP: return makeStack(COOKED_PORKCHOP);
    case RAW_BEEF: return makeStack(STEAK);
    case RAW_CHICKEN: return makeStack(COOKED_CHICKEN);
    case RAW_FISH: return makeStack(COOKED_FISH);
    default: return {};
    }
}

int fuelTicks(const ItemStack& f) {
    switch (f.id) {
    case COAL: return 1600;
    case LOG: case PLANKS: case CRAFTING_TABLE: case CHEST: case BOOKSHELF: return 300;
    case STICK: case SAPLING: case WOOD_BUTTON: return 100;
    case FENCE: case WOOD_STAIRS: case SPRUCE_STAIRS: case BIRCH_STAIRS: case JUNGLE_STAIRS: case TRAPDOOR: case FENCE_GATE: case NOTE_BLOCK: case JUKEBOX: case WOOD_PLATE: case CARROT_ON_A_STICK: return 300;
    case LAVA_BUCKET: return 20000;
    default: return 0;
    }
}

bool tickFurnace(TileEntity& te) {
    ItemStack& in = te.items[0];
    ItemStack& fuel = te.items[1];
    ItemStack& out = te.items[2];
    bool wasBurning = te.burnTime > 0;

    auto canSmelt = [&]() {
        if (in.empty()) return false;
        ItemStack r = smeltingResult(in);
        if (r.empty()) return false;
        if (out.empty()) return true;
        return out.sameItem(r) && out.count + r.count <= maxStackSize(out.id);
    };

    if (te.burnTime > 0) --te.burnTime;
    if (te.burnTime == 0 && canSmelt()) {
        te.burnMax = te.burnTime = fuelTicks(fuel);
        if (te.burnTime > 0) {
            if (fuel.id == LAVA_BUCKET) fuel = makeStack(BUCKET);
            else if (--fuel.count == 0) fuel.clear();
        }
    }
    if (te.burnTime > 0 && canSmelt()) {
        if (++te.cookTime >= FURNACE_COOK_TICKS) {
            te.cookTime = 0;
            ItemStack r = smeltingResult(in);
            if (out.empty()) out = r;
            else out.count = (uint8_t)(out.count + r.count);
            if (--in.count == 0) in.clear();
        }
    } else {
        te.cookTime = 0;
    }
    return wasBurning != (te.burnTime > 0);
}
