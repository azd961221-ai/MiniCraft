#include "../src/Potion.h"
#include "../src/Enchant.h"
// Проверки игровой логики без окна: рецепты, печь, дроп, инвентарь, скорость добычи.
#include <cmath>
#include <cstdio>
#include <string>
#include "../src/Crafting.h"
#include "../src/Inventory.h"

static int failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } \
    } while (0)

static ItemStack craft3(std::initializer_list<uint16_t> ids) {
    ItemStack g[9];
    int i = 0;
    for (uint16_t id : ids) { if (id) g[i] = makeStack(id); ++i; }
    return findRecipe(g, 3);
}

int main(int argc, char** argv) {
    // selftest --icons: список иконок всех предметов (id, тайл, из какой текстуры) — для проверки пустых иконок
    if (argc > 1 && std::string(argv[1]) == "--icons") {
        for (int id = 1; id < 2300; ++id) {
            if (!isValidItem((uint16_t)id)) continue;
            int col = 0, row = 0;
            const char* nm = itemName(makeStack((uint16_t)id));
            if (itemIcon((uint16_t)id, col, row, 0)) std::printf("%d items %d %d %s\n", id, col, row, nm);
            else if (isBlockItem((uint16_t)id)) {
                int t = blockTex((uint8_t)id, 4, id == WHEAT ? 7 : 0);
                std::printf("%d terrain %d %d %s\n", id, t % 16, t / 16, nm);
            } else std::printf("%d none 0 0 %s\n", id, nm);
        }
        return 0;
    }
    // --- Рецепты
    {
        ItemStack g[4] = {makeStack(LOG)};
        ItemStack r = findRecipe(g, 2);
        CHECK(r.id == PLANKS && r.count == 4);
        ItemStack g2[4] = {{}, {}, {}, makeStack(LOG)}; // в любом месте сетки
        CHECK(findRecipe(g2, 2).id == PLANKS);
    }
    CHECK(craft3({COBBLE, COBBLE, COBBLE, 0, STICK, 0, 0, STICK, 0}).id == STONE_PICKAXE);
    CHECK(craft3({PLANKS, PLANKS, 0, PLANKS, STICK, 0, 0, STICK, 0}).id == WOOD_AXE);
    CHECK(craft3({0, PLANKS, PLANKS, 0, STICK, PLANKS, 0, STICK, 0}).id == WOOD_AXE); // зеркальный
    CHECK(craft3({COBBLE, COBBLE, COBBLE, COBBLE, 0, COBBLE, COBBLE, COBBLE, COBBLE}).id == FURNACE);
    CHECK(craft3({IRON_INGOT, 0, IRON_INGOT, 0, IRON_INGOT, 0, 0, 0, 0}).id == BUCKET);
    CHECK(craft3({WHEAT_ITEM, WHEAT_ITEM, WHEAT_ITEM, 0, 0, 0, 0, 0, 0}).id == BREAD);
    CHECK(craft3({DIAMOND, DIAMOND, DIAMOND, DIAMOND, 0, DIAMOND, 0, 0, 0}).id == DIAMOND_HELMET);
    CHECK(craft3({COBBLE, COBBLE, 0, 0, 0, 0, 0, 0, 0}).empty());
    {
        ItemStack g[4] = {makeStack(COAL, 1, 1), {}, makeStack(STICK), {}}; // древесный уголь тоже годится
        ItemStack r = findRecipe(g, 2);
        CHECK(r.id == TORCH && r.count == 4);
    }
    // 3x3-рецепт не собирается в сетке 2x2
    {
        ItemStack g[4] = {makeStack(PLANKS), makeStack(PLANKS), makeStack(PLANKS), makeStack(PLANKS)};
        CHECK(findRecipe(g, 2).id == CRAFTING_TABLE);
    }

    // --- Печь: железная руда + уголь
    {
        TileEntity f;
        f.type = TileEntity::Furnace;
        f.items[0] = makeStack(IRON_ORE, 2);
        f.items[1] = makeStack(COAL, 1);
        bool toggled = false;
        for (int t = 0; t < FURNACE_COOK_TICKS * 2 + 5; ++t) toggled |= tickFurnace(f);
        CHECK(toggled);
        CHECK(f.items[2].id == IRON_INGOT && f.items[2].count == 2);
        CHECK(f.items[0].empty());
        CHECK(f.items[1].empty());
        CHECK(f.burnTime > 0); // уголь горит 1600 тиков
    }

    // --- Дроп
    {
        uint32_t rng = 12345;
        auto d = blockDrops(STONE, 0, rng);
        CHECK(d.size() == 1 && d[0].id == COBBLE);
        d = blockDrops(COAL_ORE, 0, rng);
        CHECK(d.size() == 1 && d[0].id == COAL);
        d = blockDrops(GLASS, 0, rng);
        CHECK(d.empty());
        int flint = 0;
        for (int i = 0; i < 1000; ++i) { d = blockDrops(GRAVEL, 0, rng); flint += d[0].id == FLINT; }
        CHECK(flint > 50 && flint < 160); // ~10%
    }

    // --- Добыча: камень рукой ~7.5 с, деревянной киркой ~1.15 с
    {
        float hand = breakStrength(STONE, {}, false, true);
        float pick = breakStrength(STONE, makeStack(WOOD_PICKAXE), false, true);
        CHECK(std::fabs(1.f / hand - 150.f) < 1.f);
        CHECK(std::ceil(1.f / pick) == 23.f);
        CHECK(!canHarvest(STONE, {}));
        CHECK(canHarvest(STONE, makeStack(WOOD_PICKAXE)));
        CHECK(!canHarvest(DIAMOND_ORE, makeStack(STONE_PICKAXE)));
        CHECK(canHarvest(DIAMOND_ORE, makeStack(IRON_PICKAXE)));
        CHECK(breakStrength(TORCH, {}, false, true) == 1.f);
    }

    // --- Инвентарь
    {
        Inventory inv;
        ItemStack s = makeStack(DIRT, 100);
        // 100 не влезает в одну стопку — add кладёт всё за раз в первый пустой слот, поэтому проверяем по частям
        ItemStack a = makeStack(DIRT, 60), b = makeStack(DIRT, 10);
        CHECK(inv.add(a));
        CHECK(inv.add(b));
        CHECK(inv.slots[0].count == 64 && inv.slots[1].count == 6);
        CHECK(inv.count(DIRT) == 70);
        ItemStack tool = makeStack(IRON_PICKAXE);
        CHECK(inv.add(tool));
        CHECK(inv.slots[2].id == IRON_PICKAXE);
        (void)s;
        inv.armor[1] = makeStack(IRON_CHESTPLATE);
        inv.armor[0] = makeStack(DIAMOND_HELMET);
        CHECK(inv.armorValue() == 9);
        CHECK(maxDamage(IRON_CHESTPLATE) == 240);
    }

    // --- Новые рецепты: без формы, красители, торт, полублоки
    {
        ItemStack g[9];
        g[4] = makeStack(FLINT); g[0] = makeStack(IRON_INGOT);
        CHECK(findRecipe(g, 3).id == FLINT_AND_STEEL);
        ItemStack g2[4];
        g2[0] = makeStack(DYE, 1, DYE_RED); g2[3] = makeStack(WOOL, 1, 0);
        ItemStack w = findRecipe(g2, 2);
        CHECK(w.id == WOOL && w.damage == 14);
        g2[3] = makeStack(WOOL, 1, 3); // крашеная шерсть не перекрашивается
        CHECK(findRecipe(g2, 2).empty());
        ItemStack g3[9];
        for (int i = 0; i < 3; ++i) { g3[i] = makeStack(MILK_BUCKET); g3[6 + i] = makeStack(WHEAT_ITEM); }
        g3[3] = makeStack(SUGAR); g3[5] = makeStack(SUGAR); g3[4] = makeStack(EGG);
        CHECK(findRecipe(g3, 3).id == CAKE_ITEM);
        CHECK(craftLeftover(makeStack(MILK_BUCKET)).id == BUCKET);
        ItemStack g4[9];
        for (int i = 0; i < 3; ++i) g4[3 + i] = makeStack(COBBLE);
        ItemStack sl = findRecipe(g4, 3);
        CHECK(sl.id == SLAB && sl.damage == 3 && sl.count == 6);
        for (int i = 0; i < 3; ++i) g4[3 + i] = makeStack(PLANKS, 1, 2);
        ItemStack ws = findRecipe(g4, 3);
        CHECK(ws.id == WOOD_SLAB && ws.damage == 2 && ws.count == 6); // берёзовые доски → берёзовые плиты
        uint32_t r0 = 1;
        CHECK(blockDrops(DOUBLE_WOOD_SLAB, 3, r0)[0].id == WOOD_SLAB && blockDrops(DOUBLE_WOOD_SLAB, 3, r0)[0].count == 2);
        uint32_t rng = 7;
        CHECK(blockDrops(DOUBLE_SLAB, 1, rng)[0].count == 2);
        CHECK(blockDrops(WOOD_DOOR, 8, rng).empty());
        CHECK(blockDrops(WOOD_DOOR, 2, rng)[0].id == WOOD_DOOR_ITEM);
        CHECK(smeltingResult(makeStack(CACTUS)).id == DYE);
    }

    // --- Зельеварение и зачарование
    {
        CHECK(brewResult(NETHER_WART_ITEM, 0) == 16);
        CHECK(brewResult(SUGAR, 16) == (8192 | 2));
        CHECK(brewResult(FERMENTED_SPIDER_EYE, 8192 | 2) == (8192 | 10));
        CHECK(brewResult(GUNPOWDER, 8192 | 5) == (16384 | 5));
        CHECK(brewResult(GLOWSTONE_DUST, 8192 | 9) == (8192 | 32 | 9));
        CHECK(potionName(8192 | 2) == "Potion of Swiftness");
        uint32_t rng = 99;
        int total = 0;
        for (int i = 0; i < 50; ++i) total += (int)pickEnchantments(rng, DIAMOND_SWORD, 30).size();
        CHECK(total >= 50);
        CHECK(pickEnchantments(rng, STICK, 30).empty());
        // 1.0: 30 полок — верхняя кнопка 16..50, без полок — не выше 5
        for (int i = 0; i < 20; ++i) {
            int top = enchantTableLevel(rng, 2, 30, IRON_PICKAXE), bare = enchantTableLevel(rng, 2, 0, IRON_PICKAXE);
            CHECK(top >= 16 && top <= 50);
            CHECK(bare >= 1 && bare <= 5);
        }
        CHECK(!pickEnchantments(rng, BOW, 20).empty());
        ItemStack a[4];
        a[3] = makeStack(DIAMOND_BOOTS);
        a[3].addEnch(ENCH_FEATHER_FALLING, 4);
        CHECK(armorProtectionPoints(a, 2) == 18);
        CHECK(armorProtectionPoints(a, 0) == 0);
    }

    std::printf(failures ? "\n%d check(s) FAILED\n" : "All checks passed\n", failures);
    return failures ? 1 : 0;
}
