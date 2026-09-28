#include "Gui.h"
#include <algorithm>
#include <string>
#include "Crafting.h"
#include <GLFW/glfw3.h>
#include <cstdio>
#include "Enchant.h"
#include "Potion.h"

// ---------------------------------------------------------------- Отрисовка предмета

void drawItemStack(UI& ui, const GuiTextures& tex, const ItemStack& s, float x, float y, float sc) {
    if (s.empty()) return;
    int col, row;
    if (s.id == POTION) {
        // Бутылка (или взрывная) и жидкость цвета зелья поверх
        bool splash = potionSplash(s.damage);
        ui.image(tex.items, x * sc, y * sc, 16 * sc, 16 * sc, splash ? 160.f : 192.f, splash ? 144.f : 128.f, 16, 16, 256, 256);
        uint32_t c = potionColor(s.damage);
        ui.image(tex.items, x * sc, y * sc, 16 * sc, 16 * sc, 208.f, 128.f, 16, 16, 256, 256,
                 {((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f, (c & 255) / 255.f, 1.f});
    } else if (itemIcon(s.id, col, row, s.damage)) {
        ui.image(tex.items, x * sc, y * sc, 16 * sc, 16 * sc, col * 16.f, row * 16.f, 16, 16, 256, 256);
    } else if (isBlockItem(s.id) && isFlatItem((uint8_t)s.id)) {
        int t = blockTex((uint8_t)s.id, 4, s.id == WHEAT ? 7 : (uint8_t)s.damage);
        ui.image(tex.terrain, x * sc, y * sc, 16 * sc, 16 * sc, (t % 16) * 16.f + 0.01f, (t / 16) * 16.f + 0.01f, 15.98f, 15.98f, 256, 256);
    } else if (isBlockItem(s.id)) {
        bool variant = blockHasVariants((uint8_t)s.id);
        ui.blockIcon((x + 8) * sc, (y + 8) * sc, 14 * sc, (uint8_t)s.id, variant ? (uint8_t)s.damage : 2);
    }
    // Блеск зачарованного предмета: переливающаяся фиолетовая полоса
    if (s.enchanted()) {
        float t = (float)(glfwGetTime() * 0.6);
        float ph = t - std::floor(t);
        int col2, row2;
        bool fi = false;
        if (itemIcon(s.id, col2, row2, s.damage)) fi = true;
        glm::vec4 glint(0.5f, 0.25f, 0.8f, 0.35f + 0.25f * std::sin(ph * 6.2831853f));
        if (fi) ui.image(tex.items, x * sc, y * sc, 16 * sc, 16 * sc, col2 * 16.f, row2 * 16.f, 16, 16, 256, 256, glint, UI::Blend::Add);
        else ui.rect(x * sc, y * sc, (x + 16) * sc, (y + 16) * sc, {0.5f, 0.25f, 0.8f, glint.a * 0.4f});
    }
    // Полоска прочности, как в 1.0: зелёная → красная
    int maxD = maxDamage(s.id);
    if (maxD > 0 && s.damage > 0) {
        float frac = 1.f - (float)s.damage / maxD;
        int w = (int)(13.f * frac + 0.5f);
        float c = frac;
        ui.rect((x + 2) * sc, (y + 13) * sc, (x + 15) * sc, (y + 15) * sc, {0, 0, 0, 1});
        ui.rect((x + 2) * sc, (y + 13) * sc, (x + 14) * sc, (y + 14) * sc, {(1 - c) * 0.25f, 0.25f, 0, 1});
        ui.rect((x + 2) * sc, (y + 13) * sc, (x + 2 + w) * sc, (y + 14) * sc, {1 - c, c, 0, 1});
    }
    if (s.count > 1) {
        std::string n = std::to_string(s.count);
        ui.text(n, (x + 17) * sc - ui.textWidth(n, sc), (y + 9) * sc, sc);
    }
}

// ---------------------------------------------------------------- Открытие/закрытие

void ContainerScreen::open(GuiKind k, TileEntity* te, TileEntity* te2) {
    kind = k;
    tile = te;
    tile2 = k == GuiKind::Chest ? te2 : nullptr;
    enderChest = false;
    craftW_ = k == GuiKind::Crafting ? 3 : 2;
    for (auto& c : craft_) c.clear();
    craftOut_.clear();
    scrollRow_ = 0;
    if (k == GuiKind::Creative) {
        if (palette_.empty()) initTabs();
    }
}

static const char* TAB_NAMES[12] = {
    "Building Blocks",
    "Decoration Blocks",
    "Redstone",
    "Transportation",
    "Miscellaneous",
    "Search Items",
    "Foodstuffs",
    "Tools",
    "Combat",
    "Brewing",
    "Materials",
    "Survival Inventory"
};

static ItemStack tabIcon(int tab) {
    switch (tab) {
    case 0: return makeStack(BRICK);
    case 1: return makeStack(ROSE);
    case 2: return makeStack(REDSTONE);
    case 3: return makeStack(POWERED_RAIL);
    case 4: return makeStack(LAVA_BUCKET);
    case 5: return makeStack(COMPASS);
    case 6: return makeStack(APPLE);
    case 7: return makeStack(IRON_AXE);
    case 8: return makeStack(GOLD_SWORD);
    case 9: return makeStack(POTION, 1, 8193);
    case 10: return makeStack(STICK);
    case 11: return makeStack(CHEST);
    default: return makeStack(BRICK);
    }
}

const std::vector<ItemStack>& ContainerScreen::currentPalette() const {
    if (selectedTab_ >= 0 && selectedTab_ < 12 && selectedTab_ != 5 && selectedTab_ != 11)
        return tabItems_[selectedTab_];
    return palette_;
}

void ContainerScreen::initTabs() {
    auto add = [&](int tab, uint16_t id, uint16_t damage = 0) {
        tabItems_[tab].push_back(makeStack(id, 1, damage));
    };

    // Tab 0: Building Blocks
    add(0, STONE);
    add(0, COBBLE);
    add(0, MOSSY_COBBLE);
    for (int d = 0; d < 4; ++d) add(0, STONE_BRICK, d);
    add(0, BRICK);
    add(0, CLAY);
    add(0, BEDROCK);
    add(0, OBSIDIAN);
    add(0, NETHERRACK);
    add(0, SOUL_SAND);
    add(0, NETHER_BRICK);
    add(0, END_STONE);
    add(0, EMERALD_BLOCK);
    add(0, DIAMOND_BLOCK);
    add(0, GOLD_BLOCK);
    add(0, IRON_BLOCK);
    add(0, LAPIS_BLOCK);
    add(0, DIRT);
    add(0, GRASS);
    add(0, SAND);
    add(0, GRAVEL);
    for (int d = 0; d < 3; ++d) add(0, SANDSTONE, d);
    for (int d = 0; d < 4; ++d) add(0, PLANKS, d);
    for (int d = 0; d < 4; ++d) add(0, LOG, d);
    for (int d = 0; d < 6; ++d) add(0, SLAB, d);
    add(0, WOOD_STAIRS);
    add(0, COBBLE_STAIRS);
    add(0, BRICK_STAIRS);
    add(0, STONEBRICK_STAIRS);
    add(0, NETHER_STAIRS);
    add(0, COBBLE_WALL, 0);
    add(0, COBBLE_WALL, 1);
    add(0, GLASS);
    add(0, ICE);
    add(0, SNOW_BLOCK);
    add(0, GLOWSTONE);

    // Tab 1: Decoration Blocks
    add(1, ROSE);
    add(1, DANDELION);
    for (int d = 0; d < 4; ++d) add(1, SAPLING, d);
    for (int d = 0; d < 4; ++d) add(1, LEAVES, d);
    add(1, TALL_GRASS, 1);
    add(1, TALL_GRASS, 2);
    add(1, DEAD_BUSH);
    add(1, BROWN_MUSHROOM);
    add(1, RED_MUSHROOM);
    add(1, CACTUS);
    add(1, MELON_BLOCK);
    add(1, PUMPKIN);
    add(1, JACK_O_LANTERN);
    add(1, VINE);
    add(1, LILY_PAD);
    add(1, MYCELIUM);
    add(1, TORCH);
    add(1, BOOKSHELF);
    add(1, CRAFTING_TABLE);
    add(1, FURNACE);
    add(1, CHEST);
    add(1, ENDER_CHEST);
    add(1, NOTE_BLOCK);
    add(1, JUKEBOX);
    add(1, ENCHANT_TABLE);
    add(1, BEACON);
    add(1, ANVIL);
    add(1, LADDER);
    add(1, FENCE);
    add(1, NETHER_FENCE);
    add(1, IRON_BARS);
    add(1, GLASS_PANE);
    add(1, COBWEB);
    add(1, FLOWER_POT_ITEM);
    add(1, ITEM_FRAME_ITEM);
    add(1, PAINTING);
    for (int d = 0; d < 5; ++d) add(1, SKULL_ITEM, d);
    for (int d = 0; d < 16; ++d) add(1, WOOL, d);

    // Tab 2: Redstone
    add(2, DISPENSER);
    add(2, NOTE_BLOCK);
    add(2, PISTON);
    add(2, STICKY_PISTON);
    add(2, TNT);
    add(2, TORCH);
    add(2, REDSTONE_TORCH_ON);
    add(2, LEVER);
    add(2, STONE_BUTTON);
    add(2, WOOD_BUTTON);
    add(2, STONE_PLATE);
    add(2, WOOD_PLATE);
    add(2, REDSTONE);
    add(2, REPEATER_ITEM);
    add(2, FENCE_GATE);
    add(2, TRAPDOOR);
    add(2, WOOD_DOOR_ITEM);
    add(2, IRON_DOOR_ITEM);
    add(2, POWERED_RAIL);
    add(2, DETECTOR_RAIL);

    // Tab 3: Transportation
    add(3, POWERED_RAIL);
    add(3, DETECTOR_RAIL);
    add(3, RAIL);
    add(3, BOAT);
    add(3, MINECART);
    add(3, CHEST_MINECART);
    add(3, FURNACE_MINECART);
    add(3, SADDLE);
    add(3, CARROT_ON_A_STICK);

    // Tab 4: Miscellaneous
    add(4, BEACON);
    add(4, BUCKET);
    add(4, WATER_BUCKET);
    add(4, LAVA_BUCKET);
    add(4, MILK_BUCKET);
    add(4, SNOWBALL);
    add(4, SLIME_BALL);
    add(4, BONE);
    add(4, STRING);
    add(4, FEATHER);
    add(4, GUNPOWDER);
    add(4, EGG);
    add(4, ENDER_PEARL);
    add(4, EYE_OF_ENDER);
    add(4, SIGN_ITEM);
    add(4, BED_ITEM);
    add(4, MOB_SPAWNER);
    add(4, MAP);
    for (int r = RECORD_13; r <= RECORD_11; ++r) add(4, (uint16_t)r);

    // Tab 6: Foodstuffs
    add(6, APPLE);
    add(6, GOLDEN_APPLE, 0);
    add(6, GOLDEN_APPLE, 1);
    add(6, MUSHROOM_STEW);
    add(6, BREAD);
    add(6, RAW_PORKCHOP);
    add(6, COOKED_PORKCHOP);
    add(6, RAW_FISH);
    add(6, COOKED_FISH);
    add(6, CAKE_ITEM);
    add(6, COOKIE);
    add(6, MELON);
    add(6, RAW_BEEF);
    add(6, STEAK);
    add(6, RAW_CHICKEN);
    add(6, COOKED_CHICKEN);
    add(6, ROTTEN_FLESH);
    add(6, SPIDER_EYE);
    add(6, CARROT);
    add(6, POTATO);
    add(6, BAKED_POTATO);
    add(6, POISONOUS_POTATO);
    add(6, GOLDEN_CARROT);
    add(6, PUMPKIN_PIE);

    // Tab 7: Tools
    static const uint16_t TOOLS[] = {
        IRON_SHOVEL, IRON_PICKAXE, IRON_AXE, IRON_HOE,
        DIAMOND_SHOVEL, DIAMOND_PICKAXE, DIAMOND_AXE, DIAMOND_HOE,
        GOLD_SHOVEL, GOLD_PICKAXE, GOLD_AXE, GOLD_HOE,
        STONE_SHOVEL, STONE_PICKAXE, STONE_AXE, STONE_HOE,
        WOOD_SHOVEL, WOOD_PICKAXE, WOOD_AXE, WOOD_HOE,
        FLINT_AND_STEEL, COMPASS, FISHING_ROD, CLOCK, SHEARS
    };
    for (uint16_t t : TOOLS) add(7, t);

    // Tab 8: Combat
    add(8, BOW);
    add(8, ARROW);
    add(8, DIAMOND_SWORD);
    add(8, IRON_SWORD);
    add(8, GOLD_SWORD);
    add(8, STONE_SWORD);
    add(8, WOOD_SWORD);
    static const uint16_t ARMORS[] = {
        LEATHER_HELMET, LEATHER_CHESTPLATE, LEATHER_LEGGINGS, LEATHER_BOOTS,
        CHAIN_HELMET, CHAIN_CHESTPLATE, CHAIN_LEGGINGS, CHAIN_BOOTS,
        IRON_HELMET, IRON_CHESTPLATE, IRON_LEGGINGS, IRON_BOOTS,
        DIAMOND_HELMET, DIAMOND_CHESTPLATE, DIAMOND_LEGGINGS, DIAMOND_BOOTS,
        GOLD_HELMET, GOLD_CHESTPLATE, GOLD_LEGGINGS, GOLD_BOOTS
    };
    for (uint16_t a : ARMORS) add(8, a);

    // Tab 9: Brewing
    add(9, GHAST_TEAR);
    add(9, FERMENTED_SPIDER_EYE);
    add(9, BLAZE_POWDER);
    add(9, MAGMA_CREAM);
    add(9, BREWING_STAND_ITEM);
    add(9, CAULDRON_ITEM);
    add(9, GLISTERING_MELON);
    add(9, GLASS_BOTTLE);
    static const uint16_t POTIONS[] = {
        0, 8193, 8257, 8225, 8194, 8258, 8226, 8195, 8259, 8196, 8260, 8228,
        8197, 8229, 8198, 8262, 8200, 8264, 8201, 8265, 8233, 8202, 8266,
        8204, 8236, 8206, 8270,
        16385, 16449, 16417, 16386, 16450, 16418, 16387, 16451, 16388, 16452, 16420,
        16389, 16421, 16390, 16454, 16392, 16456, 16393, 16457, 16425, 16394, 16458,
        16396, 16428, 16398, 16462
    };
    for (uint16_t p : POTIONS) add(9, POTION, p);

    // Tab 10: Materials
    add(10, COAL, 0);
    add(10, COAL, 1);
    add(10, DIAMOND);
    add(10, IRON_INGOT);
    add(10, GOLD_INGOT);
    add(10, EMERALD);
    add(10, NETHER_STAR);
    add(10, STICK);
    add(10, BOWL);
    add(10, STRING);
    add(10, FEATHER);
    add(10, GUNPOWDER);
    add(10, WHEAT_ITEM);
    add(10, SEEDS);
    add(10, PUMPKIN_SEEDS);
    add(10, MELON_SEEDS);
    add(10, FLINT);
    add(10, LEATHER);
    add(10, BRICK_ITEM);
    add(10, CLAY_BALL);
    add(10, REEDS_ITEM);
    add(10, PAPER);
    add(10, BOOK);
    add(10, SLIME_BALL);
    add(10, GLOWSTONE_DUST);
    for (int d = 0; d < 16; ++d) add(10, DYE, d);
    add(10, BONE);
    add(10, SUGAR);
    add(10, BLAZE_ROD);
    add(10, GOLD_NUGGET);
    add(10, NETHER_WART_ITEM);

    // Tab 5: Search (все предметы)
    palette_.clear();
    for (int t = 0; t < 12; ++t) {
        if (t == 5 || t == 11) continue;
        for (const auto& it : tabItems_[t]) {
            bool found = false;
            for (const auto& p : palette_)
                if (p.id == it.id && p.damage == it.damage) { found = true; break; }
            if (!found) palette_.push_back(it);
        }
    }
}

void ContainerScreen::close(GuiContext& ctx) {
    auto giveBack = [&](ItemStack& s) {
        if (s.empty()) return;
        ItemStack left = s;
        if (!ctx.inv.add(left)) ctx.throwItem(left);
        s.clear();
    };
    for (auto& c : craft_) giveBack(c);
    craftOut_.clear();
    giveBack(cursor);
    giveBack(enchantItem_);
    trashStack_.clear();
    kind = GuiKind::None;
    tile = nullptr;
    tile2 = nullptr;
}

// ---------------------------------------------------------------- Раскладка

float ContainerScreen::panelW() const {
    if (kind == GuiKind::Creative) return 195.f;
    return 176.f;
}

float ContainerScreen::panelH() const {
    switch (kind) {
    case GuiKind::Creative: return 136.f;
    case GuiKind::Chest: return 114.f + chestRows() * 18;
    default: return 166.f;
    }
}

int ContainerScreen::maxScroll() const {
    if (selectedTab_ == 11) return 0;
    int rows = ((int)currentPalette().size() + 8) / 9;
    return std::max(0, rows - 5);
}

void ContainerScreen::scroll(float dy) {
    if (kind != GuiKind::Creative) return;
    scrollRow_ = std::clamp(scrollRow_ - (dy > 0 ? 1 : -1), 0, maxScroll());
}

std::vector<ContainerScreen::Slot> ContainerScreen::slots(GuiContext& ctx) {
    std::vector<Slot> s;
    float px = (ctx.W - panelW()) / 2, py = (ctx.H - panelH()) / 2;
    auto add = [&](float x, float y, ItemStack* st, Role r, Group g, int i) { s.push_back({px + x, py + y, st, r, g, i}); };

    // Инвентарь игрока внизу окна
    auto playerInv = [&](float mainY, float hotbarY) {
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 9; ++c) add(8.f + c * 18, mainY + r * 18, &ctx.inv.slots[9 + r * 9 + c], Role::Normal, MAIN, 9 + r * 9 + c);
        for (int c = 0; c < 9; ++c) add(8.f + c * 18, hotbarY, &ctx.inv.slots[c], Role::Normal, HOTBAR, c);
    };

    switch (kind) {
    case GuiKind::Inventory:
        for (int i = 0; i < 4; ++i) add(8, 8.f + i * 18, &ctx.inv.armor[i], Role::Armor, ARMOR, i);
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 2; ++c) add(88.f + c * 18, 26.f + r * 18, &craft_[r * 2 + c], Role::Normal, CONTAINER, r * 2 + c);
        add(144, 36, &craftOut_, Role::CraftOut, OUTPUT, 0);
        playerInv(84, 142);
        break;
    case GuiKind::Crafting:
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) add(30.f + c * 18, 17.f + r * 18, &craft_[r * 3 + c], Role::Normal, CONTAINER, r * 3 + c);
        add(124, 35, &craftOut_, Role::CraftOut, OUTPUT, 0);
        playerInv(84, 142);
        break;
    case GuiKind::Furnace:
        if (tile) {
            add(56, 17, &tile->items[0], Role::Normal, CONTAINER, 0);
            add(56, 53, &tile->items[1], Role::Normal, CONTAINER, 1);
            add(116, 35, &tile->items[2], Role::FurnaceOut, OUTPUT, 2);
        }
        playerInv(84, 142);
        break;
    case GuiKind::Dispenser:
        if (tile)
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c) add(62.f + c * 18, 17.f + r * 18, &tile->items[r * 3 + c], Role::Normal, CONTAINER, r * 3 + c);
        playerInv(84, 142);
        break;
    case GuiKind::Enchant:
        add(25, 47, &enchantItem_, Role::Normal, CONTAINER, 0);
        playerInv(84, 142);
        break;
    case GuiKind::Brewing:
        if (tile) {
            add(79, 17, &tile->items[3], Role::Normal, CONTAINER, 3);
            add(56, 46, &tile->items[0], Role::Normal, CONTAINER, 0);
            add(79, 53, &tile->items[1], Role::Normal, CONTAINER, 1);
            add(102, 46, &tile->items[2], Role::Normal, CONTAINER, 2);
        }
        playerInv(84, 142);
        break;
    case GuiKind::Chest: {
        int rows = chestRows();
        if (tile)
            for (int r = 0; r < rows; ++r)
                for (int c = 0; c < 9; ++c) {
                    int i = r * 9 + c;
                    ItemStack* st = i < 27 ? &tile->items[i] : &tile2->items[i - 27];
                    add(8.f + c * 18, 18.f + r * 18, st, Role::Normal, CONTAINER, i);
                }
        float off = (rows - 4) * 18.f;
        playerInv(103 + off, 161 + off);
        break;
    }
    case GuiKind::Creative:
        if (selectedTab_ == 11) {
            for (int i = 0; i < 4; ++i)
                add(9.f, 6.f + i * 18.f, &ctx.inv.armor[i], Role::Armor, ARMOR, i);
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 9; ++c)
                    add(9.f + c * 18.f, 54.f + r * 18.f, &ctx.inv.slots[9 + r * 9 + c], Role::Normal, MAIN, 9 + r * 9 + c);
            for (int c = 0; c < 9; ++c)
                add(9.f + c * 18.f, 112.f, &ctx.inv.slots[c], Role::Normal, HOTBAR, c);
            add(173.f, 112.f, &trashStack_, Role::Normal, CONTAINER, 999);
        } else {
            const auto& pal = currentPalette();
            for (int r = 0; r < 5; ++r)
                for (int c = 0; c < 9; ++c) {
                    int idx = (scrollRow_ + r) * 9 + c;
                    if (idx < (int)pal.size())
                        add(9.f + c * 18.f, 18.f + r * 18.f, const_cast<ItemStack*>(&pal[idx]), Role::Palette, PALETTE, idx);
                }
            for (int c = 0; c < 9; ++c)
                add(9.f + c * 18.f, 112.f, &ctx.inv.slots[c], Role::Normal, HOTBAR, c);
        }
        break;
    default: break;
    }
    return s;
}

ContainerScreen::Slot* ContainerScreen::slotAt(std::vector<Slot>& s, GuiContext& ctx) {
    for (auto& sl : s)
        if (ctx.mx >= sl.x - 1 && ctx.mx < sl.x + 17 && ctx.my >= sl.y - 1 && ctx.my < sl.y + 17) return &sl;
    return nullptr;
}

// ---------------------------------------------------------------- Крафт

void ContainerScreen::updateCraft() {
    if (kind != GuiKind::Inventory && kind != GuiKind::Crafting) return;
    craftOut_ = findRecipe(craft_, craftW_);
}

void ContainerScreen::consumeCraft() {
    for (int i = 0; i < craftW_ * craftW_; ++i) {
        ItemStack& c = craft_[i];
        if (c.empty()) continue;
        ItemStack left = craftLeftover(c);
        if (--c.count == 0) c = left;
    }
    updateCraft();
}

// ---------------------------------------------------------------- Зачарование

void ContainerScreen::refreshEnchant(GuiContext&) {
    const ItemStack& it = enchantItem_;
    uint32_t key = it.empty() ? 0u : (uint32_t)it.id * 131u + it.damage * 7u + (it.enchanted() ? 1u : 0u) + 1u;
    if (key == enchantKey_) return;
    enchantKey_ = key;
    static const char* SYL[] = {"the", "elder", "scrolls", "klaatu", "berata", "niktu", "xyzzy", "bless", "curse", "light",
                                "darkness", "fire", "air", "earth", "water", "hot", "dry", "cold", "wet", "ignite", "snuff",
                                "embiggen", "twist", "shorten", "stretch", "fiddle", "destroy", "imbue", "galvanize",
                                "enchant", "free", "limited", "range", "of", "towards", "inside", "sphere", "cube", "self",
                                "other", "ball", "mental", "physical", "grow", "shrink", "demon", "elemental", "spirit",
                                "animal", "creature", "beast", "humanoid", "undead", "fresh", "stale"};
    for (int i = 0; i < 3; ++i) {
        enchantLevels_[i] = (it.empty() || it.enchanted()) ? 0 : enchantTableLevel(enchantRng_, i, enchantShelves, it.id);
        std::string w;
        int n = 2 + (int)((enchantRng_ >> (i * 3)) % 2);
        for (int k = 0; k < n; ++k) {
            enchantRng_ ^= enchantRng_ << 13; enchantRng_ ^= enchantRng_ >> 17; enchantRng_ ^= enchantRng_ << 5;
            if (k) w += " ";
            w += SYL[enchantRng_ % (sizeof(SYL) / sizeof(SYL[0]))];
        }
        enchantWords_[i] = w;
    }
}

// ---------------------------------------------------------------- Клики

bool ContainerScreen::moveInto(ItemStack& src, ItemStack* dst, int n) {
    // Сначала дополняем такие же стопки, потом пустые слоты
    int maxS = maxStackSize(src.id);
    for (int pass = 0; pass < 2 && !src.empty(); ++pass)
        for (int i = 0; i < n && !src.empty(); ++i) {
            ItemStack& d = dst[i];
            if (pass == 0 && !d.empty() && d.sameItem(src) && d.count < maxS) {
                int k = std::min<int>(src.count, maxS - d.count);
                d.count = (uint8_t)(d.count + k);
                src.count = (uint8_t)(src.count - k);
            } else if (pass == 1 && d.empty()) {
                d = src;
                src.clear();
            }
        }
    if (src.count == 0) src.clear();
    return src.empty();
}

void ContainerScreen::shiftMove(GuiContext& ctx, Slot& sl) {
    ItemStack& s = *sl.stack;
    if (s.empty()) return;
    Inventory& inv = ctx.inv;
    if (sl.group == CONTAINER || sl.group == ARMOR) {
        // В инвентарь игрока: сначала основной, потом хотбар (как в 1.0)
        moveInto(s, inv.slots + 9, 27);
        moveInto(s, inv.slots, 9);
    } else if (kind == GuiKind::Chest && tile) {
        moveInto(s, tile->items, 27);
        if (tile2) moveInto(s, tile2->items, 27);
    } else if (kind == GuiKind::Dispenser && tile) {
        moveInto(s, tile->items, 9);
    } else if (kind == GuiKind::Brewing && tile && isBrewingIngredient(s.id)) {
        moveInto(s, &tile->items[3], 1);
    } else if (kind == GuiKind::Brewing && tile && (s.id == POTION || s.id == GLASS_BOTTLE)) {
        for (int i = 0; i < 3 && !s.empty(); ++i) // в каждый слот по одной бутылке
            if (tile->items[i].empty()) {
                tile->items[i] = s;
                tile->items[i].count = 1;
                if (--s.count == 0) s.clear();
            }
    } else if (kind == GuiKind::Enchant && enchantItem_.empty() && itemEnchantability(s.id) > 0) {
        enchantItem_ = s;
        enchantItem_.count = 1;
        if (--s.count == 0) s.clear();
    } else if (kind == GuiKind::Furnace && tile && !smeltingResult(s).empty()) {
        moveInto(s, &tile->items[0], 1);
    } else if (kind == GuiKind::Furnace && tile && fuelTicks(s) > 0) {
        moveInto(s, &tile->items[1], 1);
    } else if (kind == GuiKind::Inventory && s.id == PUMPKIN && sl.group != ARMOR && inv.armor[0].empty()) {
        inv.armor[0] = s;
        inv.armor[0].count = 1;
        if (--s.count == 0) s.clear();
    } else if (kind == GuiKind::Inventory && armorInfo(s.id).slot >= 0 && inv.armor[armorInfo(s.id).slot].empty()) {
        inv.armor[armorInfo(s.id).slot] = s;
        s.clear();
    } else if (sl.group == HOTBAR) {
        moveInto(s, inv.slots + 9, 27);
    } else if (sl.group == MAIN) {
        moveInto(s, inv.slots, 9);
    }
}

void ContainerScreen::clickSlot(GuiContext& ctx, Slot& sl, int button, bool shift) {
    ItemStack& s = *sl.stack;

    if (kind == GuiKind::Creative && selectedTab_ == 11 && sl.index == 999) {
        if (shift) {
            for (auto& st : ctx.inv.slots) st.clear();
            for (auto& a : ctx.inv.armor) a.clear();
        }
        cursor.clear();
        trashStack_.clear();
        return;
    }

    if (sl.role == Role::Palette) {
        if (shift) {
            ItemStack stack = s;
            stack.count = (uint8_t)maxStackSize(s.id);
            ctx.inv.add(stack);
            return;
        }
        if (!cursor.empty()) { cursor.clear(); return; } // творческий: клик по палитре удаляет предмет
        cursor = s;
        cursor.count = (uint8_t)(button == 0 ? maxStackSize(s.id) : 1);
        return;
    }

    if (sl.role == Role::CraftOut || sl.role == Role::FurnaceOut) {
        if (s.empty()) return;
        if (shift) {
            if (sl.role == Role::CraftOut) {
                // Крафтим, пока рецепт тот же и всё помещается
                ItemStack first = s;
                for (int guard = 0; guard < 64 && !craftOut_.empty() && craftOut_.sameItem(first); ++guard) {
                    ItemStack r = craftOut_;
                    Inventory probe = ctx.inv;
                    if (!probe.add(r)) break;
                    r = craftOut_;
                    ctx.inv.add(r);
                    consumeCraft();
                }
            } else {
                ItemStack r = s;
                ctx.inv.add(r);
                s = r;
            }
            return;
        }
        if (cursor.empty()) {
            cursor = s;
        } else if (cursor.sameItem(s) && cursor.count + s.count <= maxStackSize(s.id)) {
            cursor.count = (uint8_t)(cursor.count + s.count);
        } else {
            return;
        }
        if (sl.role == Role::CraftOut) consumeCraft();
        else s.clear();
        return;
    }

    // В слот брони — только своя часть; тыкву можно надеть на голову (как в 1.0)
    if (sl.role == Role::Armor && !cursor.empty() && armorInfo(cursor.id).slot != sl.index && !(sl.index == 0 && cursor.id == PUMPKIN))
        return;
    // Тыквы складываются стопкой, а на голову надевается одна
    if (sl.role == Role::Armor && !cursor.empty() && cursor.count > 1 && !shift) {
        if (s.empty()) {
            s = cursor;
            s.count = 1;
            if (--cursor.count == 0) cursor.clear();
        }
        return;
    }

    // Особые слоты 1.0: у стола зачарования — один предмет; у стойки зельеварения — бутылки/зелья по одному,
    // а сверху — только ингредиент
    if (!shift && !cursor.empty() && sl.group == CONTAINER && (kind == GuiKind::Enchant || kind == GuiKind::Brewing)) {
        bool single = kind == GuiKind::Enchant || sl.index < 3;
        bool fits = kind == GuiKind::Enchant || (sl.index == 3 ? isBrewingIngredient(cursor.id) : (cursor.id == POTION || cursor.id == GLASS_BOTTLE));
        if (!fits) return;
        if (single) {
            if (s.empty()) {
                s = cursor;
                s.count = 1;
                if (--cursor.count == 0) cursor.clear();
            } else if (cursor.count == 1) {
                std::swap(s, cursor);
            }
            return;
        }
    }
    if (shift) {
        shiftMove(ctx, sl);
    } else if (button == 0) {
        int maxS = maxStackSize(cursor.empty() ? s.id : cursor.id);
        if (cursor.empty()) { cursor = s; s.clear(); }
        else if (s.empty()) { s = cursor; cursor.clear(); }
        else if (s.sameItem(cursor) && maxS > 1) {
            int k = std::min<int>(cursor.count, maxS - s.count);
            s.count = (uint8_t)(s.count + k);
            cursor.count = (uint8_t)(cursor.count - k);
            if (cursor.count == 0) cursor.clear();
        } else {
            std::swap(s, cursor);
        }
    } else {
        if (cursor.empty()) {
            if (!s.empty()) {
                int half = (s.count + 1) / 2;
                cursor = s;
                cursor.count = (uint8_t)half;
                s.count = (uint8_t)(s.count - half);
                if (s.count == 0) s.clear();
            }
        } else if (s.empty() || (s.sameItem(cursor) && s.count < maxStackSize(s.id))) {
            if (s.empty()) { s = cursor; s.count = 0; }
            s.count++;
            if (--cursor.count == 0) cursor.clear();
        } else {
            std::swap(s, cursor);
        }
    }
    if (sl.group == CONTAINER) updateCraft();
}

void ContainerScreen::mouseDown(GuiContext& ctx, int button, bool shift) {
    if (kind == GuiKind::Enchant && button == 0) {
        float px = (ctx.W - panelW()) / 2, py = (ctx.H - panelH()) / 2;
        for (int i = 0; i < 3; ++i) {
            float by = py + 14.f + 19 * i;
            if (ctx.mx < px + 60 || ctx.mx >= px + 168 || ctx.my < by || ctx.my >= by + 19) continue;
            int lvl = enchantLevels_[i];
            if (lvl <= 0 || enchantItem_.empty() || enchantItem_.enchanted()) return;
            if (!ctx.creative && (!ctx.xpLevel || *ctx.xpLevel < lvl)) return;
            for (auto& [id, l] : pickEnchantments(enchantRng_, enchantItem_.id, lvl)) enchantItem_.addEnch(id, l);
            if (!ctx.creative && ctx.xpLevel) *ctx.xpLevel -= lvl;
            enchantKey_ = 0xFFFFFFFFu;
            return;
        }
    }
    std::vector<Slot> s = slots(ctx);
    float px = (ctx.W - panelW()) / 2, py = (ctx.H - panelH()) / 2;

    if (kind == GuiKind::Creative) {
        // Проверка клика по вкладкам
        for (int i = 0; i < 12; ++i) {
            bool top = i < 6;
            int col = top ? (i == 5 ? 5 : i) : (i == 11 ? 5 : i - 6);
            float tx = (col == 5) ? 168.f : (col * 28.f);
            float ty = top ? -28.f : 136.f;
            float th = (selectedTab_ == i) ? 32.f : 28.f;
            if (!top && selectedTab_ == i) ty = 132.f;
            if (ctx.mx >= px + tx && ctx.mx < px + tx + 28.f && ctx.my >= py + ty && ctx.my < py + ty + th) {
                selectedTab_ = i;
                scrollRow_ = 0;
                return;
            }
        }
        // Полоса прокрутки творческого инвентаря
        if (selectedTab_ != 11 && ctx.mx >= px + 174 && ctx.mx < px + 188 && ctx.my >= py + 18 && ctx.my < py + 128) {
            scrolling_ = true;
            int ms = maxScroll();
            float rel = std::clamp((ctx.my - py - 18.f - 7.5f) / 94.f, 0.f, 1.f);
            scrollRow_ = (int)(rel * ms + 0.5f);
            return;
        }
    }
    if (Slot* sl = slotAt(s, ctx)) {
        clickSlot(ctx, *sl, button, shift);
        return;
    }
    // Клик за пределами окна — выбросить предмет с курсора
    bool outside = ctx.mx < px || ctx.my < py || ctx.mx >= px + panelW() || ctx.my >= py + panelH();
    if (outside && kind == GuiKind::Creative) {
        if ((ctx.my >= py - 32.f && ctx.my < py && ctx.mx >= px && ctx.mx < px + 196.f) ||
            (ctx.my >= py + 132.f && ctx.my <= py + 168.f && ctx.mx >= px && ctx.mx < px + 196.f)) {
            outside = false;
        }
    }
    if (outside && !cursor.empty()) {
        if (button == 0) { ctx.throwItem(cursor); cursor.clear(); }
        else {
            ItemStack one = cursor;
            one.count = 1;
            ctx.throwItem(one);
            if (--cursor.count == 0) cursor.clear();
        }
    }
}

void ContainerScreen::hotkey(GuiContext& ctx, int hotbarSlot) {
    std::vector<Slot> s = slots(ctx);
    Slot* sl = slotAt(s, ctx);
    if (!sl) return;
    ItemStack& hb = ctx.inv.slots[hotbarSlot];
    if (sl->role == Role::Palette) { hb = *sl->stack; hb.count = (uint8_t)maxStackSize(hb.id); return; }
    if (sl->role != Role::Normal) return;
    if (sl->group == CONTAINER && !hb.empty() && (kind == GuiKind::Enchant || kind == GuiKind::Brewing)) {
        bool fits = kind == GuiKind::Enchant ? hb.count == 1
                  : sl->index == 3 ? isBrewingIngredient(hb.id) : (hb.count == 1 && (hb.id == POTION || hb.id == GLASS_BOTTLE));
        if (!fits) return;
    }
    std::swap(*sl->stack, hb);
    if (sl->group == CONTAINER) updateCraft();
}

// ---------------------------------------------------------------- Отрисовка

void ContainerScreen::draw(GuiContext& ctx) {
    UI& ui = ctx.ui;
    const float sc = ctx.sc;
    float px = (ctx.W - panelW()) / 2, py = (ctx.H - panelH()) / 2;
    auto img = [&](GLuint t, float x, float y, float u, float v, float w, float h) {
        ui.image(t, (px + x) * sc, (py + y) * sc, w * sc, h * sc, u, v, w, h, 256, 256);
    };
    const glm::vec4 titleCol(0.25f, 0.25f, 0.25f, 1);
    auto title = [&](const char* s, float x, float y) { ui.text(s, (px + x) * sc, (py + y) * sc, sc, titleCol, false); };

    // Затемнение фона
    ui.rect(0, 0, ctx.W * sc, ctx.H * sc, {0.06f, 0.06f, 0.06f, 0.6f});

    switch (kind) {
    case GuiKind::Inventory:
        img(ctx.tex.inventory, 0, 0, 0, 0, 176, 166);
        title("Crafting", 86, 16);
        if (ctx.drawPlayer) ctx.drawPlayer(px + 51.f, py + 75.f, 30.f);
        break;
    case GuiKind::Crafting:
        img(ctx.tex.crafting, 0, 0, 0, 0, 176, 166);
        title("Crafting", 28, 6);
        title("Inventory", 8, 72);
        break;
    case GuiKind::Furnace:
        img(ctx.tex.furnace, 0, 0, 0, 0, 176, 166);
        title("Furnace", 60, 6);
        title("Inventory", 8, 72);
        if (tile) {
            if (tile->burnTime > 0 && tile->burnMax > 0) {
                int h = tile->burnTime * 12 / tile->burnMax;
                img(ctx.tex.furnace, 56, 36.f + 12 - h, 176, 12.f - h, 14, h + 2.f);
            }
            int w = tile->cookTime * 24 / FURNACE_COOK_TICKS;
            if (w > 0) img(ctx.tex.furnace, 79, 34, 176, 14, (float)w + 1, 16);
        }
        break;
    case GuiKind::Dispenser:
        img(ctx.tex.trap, 0, 0, 0, 0, 176, 166);
        title("Dispenser", 60, 6);
        title("Inventory", 8, 72);
        break;
    case GuiKind::Enchant: {
        img(ctx.tex.enchant, 0, 0, 0, 0, 176, 166);
        title("Enchant", 12, 5);
        title("Inventory", 8, 72);
        refreshEnchant(ctx);
        for (int i = 0; i < 3; ++i) {
            float by = 14.f + 19 * i;
            int lvl = enchantLevels_[i];
            bool can = lvl > 0 && (ctx.creative || (ctx.xpLevel && *ctx.xpLevel >= lvl));
            bool hover = ctx.mx >= px + 60 && ctx.mx < px + 168 && ctx.my >= py + by && ctx.my < py + by + 19;
            img(ctx.tex.enchant, 60, by, 0, lvl == 0 || !can ? 185.f : hover ? 204.f : 166.f, 108, 19);
            if (lvl == 0) continue;
            ui.text(enchantWords_[i], (px + 62) * sc, (py + by + 2) * sc, sc * 0.75f,
                    can ? glm::vec4(0.41f, 0.38f, 0.3f, 1) : glm::vec4(0.27f, 0.24f, 0.19f, 1), false);
            std::string ls = std::to_string(lvl);
            float lw = ui.textWidth(ls, sc) / sc;
            ui.text(ls, (px + 60 + 108 - 2 - lw) * sc, (py + by + 9) * sc, sc,
                    can ? glm::vec4(0.5f, 1.f, 0.125f, 1) : glm::vec4(0.25f, 0.5f, 0.06f, 1));
        }
        break;
    }
    case GuiKind::Brewing:
        img(ctx.tex.alchemy, 0, 0, 0, 0, 176, 166);
        title("Brewing Stand", 56, 6);
        title("Inventory", 8, 72);
        if (tile && tile->cookTime > 0) {
            int h = (int)(28.f * (1.f - tile->cookTime / 400.f));
            if (h > 0) img(ctx.tex.alchemy, 97, 16, 176, 0, 9, (float)h);
            static const int BUB[7] = {29, 24, 20, 16, 11, 6, 0};
            int bh = BUB[(tile->cookTime / 2) % 7];
            if (bh > 0) img(ctx.tex.alchemy, 65, 14.f + 29 - bh, 185, 29.f - bh, 12, (float)bh);
        }
        break;
    case GuiKind::Chest: {
        int rows = chestRows();
        img(ctx.tex.container, 0, 0, 0, 0, 176, rows * 18.f + 17);
        img(ctx.tex.container, 0, rows * 18.f + 17, 0, 126, 176, 96);
        title(enderChest ? "Ender Chest" : tile2 ? "Large chest" : "Chest", 8, 6);
        title("Inventory", 8, panelH() - 96 + 2);
        break;
    }
    case GuiKind::Creative: {
        // 1. Отрисовка невыбранных вкладок (сзади окна)
        for (int i = 0; i < 12; ++i) {
            if (i == selectedTab_) continue;
            bool top = i < 6;
            int col = top ? (i == 5 ? 5 : i) : (i == 11 ? 5 : i - 6);
            float tabX = (col == 5) ? 168.f : (col * 28.f);
            float tabU = col * 28.f;
            if (top) {
                ui.image(ctx.tex.allitems, (px + tabX) * sc, (py - 28.f) * sc, 28 * sc, 28 * sc, tabU, 0, 28, 28, 256, 256);
            } else {
                ui.image(ctx.tex.allitems, (px + tabX) * sc, (py + 136.f) * sc, 28 * sc, 28 * sc, tabU, 64, 28, 28, 256, 256);
            }
        }

        // 2. Фон окна креатива (195x136)
        GLuint bgTex = ctx.tex.creativeList;
        if (selectedTab_ == 11 && ctx.tex.creativeSurvival) bgTex = ctx.tex.creativeSurvival;
        else if (selectedTab_ == 5 && ctx.tex.creativeSearch) bgTex = ctx.tex.creativeSearch;
        if (!bgTex) bgTex = ctx.tex.allitems;
        ui.image(bgTex, px * sc, py * sc, 195 * sc, 136 * sc, 0, 0, 195, 136, 256, 256);

        // 3. Отрисовка выбранной вкладки (поверх рамки окна)
        {
            int i = selectedTab_;
            bool top = i < 6;
            int col = top ? (i == 5 ? 5 : i) : (i == 11 ? 5 : i - 6);
            float tabX = (col == 5) ? 168.f : (col * 28.f);
            float tabU = col * 28.f;
            if (top) {
                ui.image(ctx.tex.allitems, (px + tabX) * sc, (py - 28.f) * sc, 28 * sc, 32 * sc, tabU, 32, 28, 32, 256, 256);
            } else {
                ui.image(ctx.tex.allitems, (px + tabX) * sc, (py + 132.f) * sc, 28 * sc, 32 * sc, tabU, 96, 28, 32, 256, 256);
            }
        }

        // 4. Иконки вкладок
        for (int i = 0; i < 12; ++i) {
            bool top = i < 6;
            int col = top ? (i == 5 ? 5 : i) : (i == 11 ? 5 : i - 6);
            float tabX = (col == 5) ? 168.f : (col * 28.f);
            float iconY = top ? (i == selectedTab_ ? -20.f : -18.f) : (i == selectedTab_ ? 140.f : 138.f);
            drawItemStack(ui, ctx.tex, tabIcon(i), px + tabX + 6.f, py + iconY, sc);
        }

        // 5. Полоса прокрутки (только для каталогов)
        if (selectedTab_ != 11) {
            int ms = maxScroll();
            float t = ms > 0 ? (float)scrollRow_ / ms : 0.f;
            if (scrolling_ && ms > 0) {
                float rel = std::clamp((ctx.my - py - 18.f - 7.5f) / 94.f, 0.f, 1.f);
                scrollRow_ = (int)(rel * ms + 0.5f);
                t = (float)scrollRow_ / ms;
            }
            float thumbU = (ms > 0) ? 232.f : 244.f;
            ui.image(ctx.tex.allitems, (px + 175.f) * sc, (py + 18.f + t * 94.f) * sc, 12 * sc, 15 * sc, thumbU, 0, 12, 15, 256, 256);
        }

        // 6. Стив в превью (вкладка выживания: окошко x:28..59, y:6..48, ноги на py+45, масштаб 20)
        if (selectedTab_ == 11 && ctx.drawPlayer) {
            ctx.drawPlayer(px + 43.f, py + 45.f, 20.f);
        }

        title(TAB_NAMES[selectedTab_], 8, 6);
        break;
    }
    default: return;
    }

    if ((kind == GuiKind::Inventory) && ctx.effects && !ctx.effects->empty()) {
        // Эффекты зелий слева от инвентаря (как GuiInventory 1.0)
        float ey = py;
        for (const ActiveEffect& e : *ctx.effects) {
            float ex = px - 124;
            ui.image(ctx.tex.inventory, ex * sc, ey * sc, 120 * sc, 32 * sc, 0, 166, 120, 32, 256, 256);
            int u, v;
            if (effectIcon(e.id, u, v)) ui.image(ctx.tex.inventory, (ex + 6) * sc, (ey + 7) * sc, 18 * sc, 18 * sc, (float)u, (float)v, 18, 18, 256, 256);
            std::string n = effectName(e.id);
            if (e.id == EFF_SPEED) n = "Speed";
            if (e.amp > 0) n += " II";
            int secs = e.ticks / 20;
            char tbuf[16];
            std::snprintf(tbuf, sizeof tbuf, "%d:%02d", secs / 60, secs % 60);
            ui.text(n, (ex + 28) * sc, (ey + 6) * sc, sc);
            ui.text(tbuf, (ex + 28) * sc, (ey + 16) * sc, sc, {0.5f, 0.5f, 0.5f, 1});
            ey += 33;
        }
    }

    std::vector<Slot> s = slots(ctx);
    for (auto& sl : s) drawItemStack(ui, ctx.tex, *sl.stack, sl.x, sl.y, sc);

    Slot* hover = slotAt(s, ctx);
    if (hover) ui.rect(hover->x * sc, hover->y * sc, (hover->x + 16) * sc, (hover->y + 16) * sc, {1, 1, 1, 0.5f});

    // Предмет на курсоре
    drawItemStack(ui, ctx.tex, cursor, ctx.mx - 8, ctx.my - 8, sc);

    // Подсказка с названием
    if (hover && cursor.empty() && !hover->stack->empty()) {
        const ItemStack& hs = *hover->stack;
        std::vector<std::pair<std::string, glm::vec4>> lines;
        lines.push_back({itemDisplayName(hs), hs.enchanted() ? glm::vec4(0.33f, 1.f, 1.f, 1) : glm::vec4(1)});
        for (uint16_t e : hs.ench)
            if (e) lines.push_back({enchDescription(e >> 8, e & 0xFF), glm::vec4(0.66f, 0.66f, 0.66f, 1)});
        if (hs.id == POTION && potionEffect(hs.damage) != EFF_NONE) {
            int e = potionEffect(hs.damage);
            std::string l = std::string(effectName(e)) + (potionAmp(hs.damage) ? " II" : "");
            int d = potionDuration(hs.damage) / 20;
            if (d > 0) { char b[16]; std::snprintf(b, sizeof b, " (%d:%02d)", d / 60, d % 60); l += b; }
            bool bad = e == EFF_POISON || e == EFF_HARM || e == EFF_WEAKNESS || e == EFF_SLOWNESS;
            lines.push_back({l, bad ? glm::vec4(1.f, 0.33f, 0.33f, 1) : glm::vec4(0.33f, 0.33f, 1.f, 1)});
        }
        float tw = 0.f;
        for (auto& l : lines) tw = std::max(tw, ui.textWidth(l.first, sc) / sc);
        float tx = ctx.mx + 12, ty = ctx.my - 12, th = 8.f + (lines.size() - 1) * 10.f + (lines.size() > 1 ? 2.f : 0.f);
        auto r = [&](float x0, float y0, float x1, float y1, glm::vec4 c) { ui.rect(x0 * sc, y0 * sc, x1 * sc, y1 * sc, c); };
        r(tx - 3, ty - 3, tx + tw + 3, ty + th + 3, {0.06f, 0.f, 0.06f, 0.94f});
        r(tx - 2, ty - 2, tx + tw + 2, ty + th + 2, {0.31f, 0.f, 1.f, 0.3f});
        r(tx - 1, ty - 1, tx + tw + 1, ty + th + 1, {0.06f, 0.f, 0.06f, 1.f});
        float ly = ty;
        for (size_t i = 0; i < lines.size(); ++i) {
            ui.text(lines[i].first, tx * sc, ly * sc, sc, lines[i].second);
            ly += i == 0 ? 12.f : 10.f;
        }
    }

    // Подсказка вкладки при наведении курсора
    if (kind == GuiKind::Creative && cursor.empty() && !hover) {
        for (int i = 0; i < 12; ++i) {
            bool top = i < 6;
            int col = top ? (i == 5 ? 5 : i) : (i == 11 ? 5 : i - 6);
            float tabX = (col == 5) ? 168.f : (col * 28.f);
            float tabY = top ? -28.f : 136.f;
            float tabH = (selectedTab_ == i) ? 32.f : 28.f;
            if (!top && selectedTab_ == i) tabY = 132.f;
            if (ctx.mx >= px + tabX && ctx.mx < px + tabX + 28.f && ctx.my >= py + tabY && ctx.my < py + tabY + tabH) {
                std::string tname = TAB_NAMES[i];
                float tw = ui.textWidth(tname, sc) / sc;
                float tx = ctx.mx + 12, ty = ctx.my - 12, th = 8.f;
                auto r = [&](float x0, float y0, float x1, float y1, glm::vec4 c) { ui.rect(x0 * sc, y0 * sc, x1 * sc, y1 * sc, c); };
                r(tx - 3, ty - 3, tx + tw + 3, ty + th + 3, {0.06f, 0.f, 0.06f, 0.94f});
                r(tx - 2, ty - 2, tx + tw + 2, ty + th + 2, {0.31f, 0.f, 1.f, 0.3f});
                r(tx - 1, ty - 1, tx + tw + 1, ty + th + 1, {0.06f, 0.f, 0.06f, 1.f});
                ui.text(tname, tx * sc, ty * sc, sc, glm::vec4(1));
                break;
            }
        }
    }
}
