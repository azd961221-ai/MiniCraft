#pragma once
#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>
#include <glm/glm.hpp>

// ID блоков. Новые — только в конец списка, иначе сломаются сохранения.
enum Block : uint8_t {
    AIR, GRASS, DIRT, STONE, SAND, WATER, LOG, LEAVES,
    PLANKS, COBBLE, GLASS, BEDROCK, BRICK, SNOW,
    GRAVEL, COAL_ORE, IRON_ORE, GOLD_ORE, DIAMOND_ORE,
    TALL_GRASS, ROSE, DANDELION,
    BOOKSHELF, MOSSY_COBBLE, OBSIDIAN, STONE_BRICK, CRAFTING_TABLE, WOOL,
    GOLD_BLOCK, IRON_BLOCK, DIAMOND_BLOCK, TNT, ICE, CLAY, SANDSTONE,
    TORCH, GLOWSTONE, LAVA,
    FURNACE, FURNACE_LIT, CHEST, SAPLING, FARMLAND, WHEAT,
    SNOW_LAYER, SNOW_BLOCK, CACTUS, DEAD_BUSH, REEDS, PUMPKIN, JACK_O_LANTERN,
    BROWN_MUSHROOM, RED_MUSHROOM, MYCELIUM, MOB_SPAWNER, COBWEB, FENCE, RAIL,
    SLAB, DOUBLE_SLAB, WOOD_STAIRS, COBBLE_STAIRS, BRICK_STAIRS, STONEBRICK_STAIRS,
    LADDER, WOOD_DOOR, IRON_DOOR, TRAPDOOR, FENCE_GATE, GLASS_PANE, IRON_BARS,
    LAPIS_ORE, LAPIS_BLOCK, REDSTONE_ORE, FIRE, CAKE,
    BED, SIGN_POST, WALL_SIGN, REDSTONE_WIRE, REDSTONE_TORCH_OFF, REDSTONE_TORCH_ON, LEVER, STONE_BUTTON,
    STONE_PLATE, WOOD_PLATE, REPEATER_OFF, REPEATER_ON, NOTE_BLOCK, JUKEBOX,
    PISTON, STICKY_PISTON, PISTON_HEAD, DISPENSER,
    NETHERRACK, SOUL_SAND, NETHER_BRICK, NETHER_FENCE, NETHER_STAIRS, NETHER_WART, PORTAL,
    ENCHANT_TABLE, BREWING_STAND, CAULDRON,
    END_STONE, END_PORTAL_FRAME, END_PORTAL, DRAGON_EGG, MONSTER_EGG,
    PUMPKIN_STEM, MELON_STEM, MELON_BLOCK, VINE, LILY_PAD,
    POWERED_RAIL, DETECTOR_RAIL,
    EMERALD_ORE, EMERALD_BLOCK, COMMAND_BLOCK, BEACON, ANVIL,
    COBBLE_WALL, FLOWER_POT, CARROTS, POTATOES, WOOD_BUTTON, SKULL_BLOCK, ENDER_CHEST,
    REDSTONE_LAMP_OFF, REDSTONE_LAMP_ON, SANDSTONE_STAIRS, SPRUCE_STAIRS, BIRCH_STAIRS, JUNGLE_STAIRS,
    BLOCK_COUNT
};

enum class Sound : uint8_t { None, Stone, Grass, Gravel, Sand, Wood, Snow, Cloth, Glass };

// Как блок рисуется
enum class Shape : uint8_t {
    None,        // воздух
    Cube,        // обычный непрозрачный куб
    Cutout,      // куб с дырками (листва, стекло) — alpha test
    Translucent, // полупрозрачный (вода, лёд) — рисуется вторым проходом с блендингом
    Cross,       // растение из двух диагональных плоскостей
    Torch,       // факел (на полу или на стене — по метаданным)
    Crop,        // посевы: четыре плоскости решёткой «#»
    Cactus,      // кактус: бока утоплены на 1/16
    SnowLayer,   // снежный покров высотой 1/8
    Fence,       // забор: столб и перекладины к соседям
    Rail,        // рельсы: плоскость над полом
    Slab,        // полублок (мета — материал)
    Stairs,      // ступеньки (мета 0..3 — куда поднимаются: +X, -X, +Z, -Z)
    Ladder,      // лестница на стене (мета — сторона стены)
    Door,        // дверь в два блока (биты 0-1 направление, 2 открыта, 3 верхняя половина)
    Trapdoor,    // люк (биты 0-1 стена с петлями, 2 открыт)
    FenceGate,   // калитка (биты 0-1 направление, 2 открыта)
    Pane,        // стеклянная панель, решётка: соединяются с соседями
    Fire,        // огонь
    Cake,        // торт (мета — съеденные куски)
    Bed,         // кровать (биты 0-1 направление от ног к голове, 3 — изголовье)
    Sign,        // табличка (рисуется вместе с текстом отдельно, в мире — только выделение)
    Wire,        // редстоун-пыль (мета — сила 0..15)
    Lever,       // рычаг (биты 0-2 крепление 1..5, бит 3 включён)
    Button,      // кнопка (биты 0-2 стена 1..4, бит 3 нажата)
    Plate,       // нажимная плита (мета 1 — нажата)
    Repeater,    // повторитель (биты 0-1 направление выхода, 2-3 задержка-1)
    Piston,      // поршень (биты 0-2 направление 0..5 как DIRS, бит 3 выдвинут)
    PistonHead,  // головка поршня (биты 0-2 направление, бит 3 липкая)
    Portal,      // портал в Незер (мета 0 — плоскость вдоль X, 1 — вдоль Z)
    Table,       // стол зачарования (3/4 блока)
    Brewing,     // стойка зельеварения (мета — какие бутылки стоят)
    Cauldron,    // котёл (мета — уровень воды 0..3)
    Frame,       // рамка портала Края (биты 0-1 направление, бит 2 — вставлено око)
    EndPortal,   // портал в Край (тонкая звёздная пластина)
    Egg,         // яйцо дракона
    Stem,        // стебель тыквы/арбуза (мета — возраст 0..7)
    Vine,        // лоза (мета — стороны: 1 юг, 2 запад, 4 север, 8 восток)
    LilyPad,     // кувшинка на воде
    Chest,       // сундук: рисуется моделью с крышкой (item/chest.png, largechest.png), в чанке — ничего
    Anvil,       // наковальня 1.4.2: четыре коробки (биты 0-1 — поворот, 2-3 — износ)
    Beacon,      // маяк 1.4.2: стеклянная оболочка, обсидиановое основание, ядро
    FlowerPot,   // цветочный горшок 1.4.2
    Skull        // голова моба 1.4.2: рисуется моделью с текстурой моба (мета — вид), в чанке — ничего
};

// Какой инструмент ускоряет добычу
enum class Tool : uint8_t { None, Pickaxe, Axe, Shovel, Sword, Hoe };

// Номер тайла в terrain.png (16x16), col + row * 16
constexpr uint8_t T(int col, int row) { return (uint8_t)(col + row * 16); }

struct BlockInfo {
    const char* name;
    Shape shape;
    bool solid;       // есть коллизия
    float hardness;   // прочность как в 1.0 (камень 1.5), < 0 — неразрушаемый
    Sound sound;
    uint8_t top, bottom, side; // тайлы граней
    uint8_t front;             // передняя грань (по метаданным), 255 — как side
    Tool tool;                 // «правильный» инструмент
    int8_t harvestTier;        // -1: добывается чем угодно; 0..3: нужна кирка уровня (дерево..алмаз)
};

inline const BlockInfo& blockInfo(uint8_t b) {
    using S = Shape; using So = Sound; using Tl = Tool;
    static const BlockInfo INFO[BLOCK_COUNT] = {
        /* AIR          */ {"Air",            S::None,        false,  0.0f, So::None,   0, 0, 0, 255, Tl::None, -1},
        /* GRASS        */ {"Grass",          S::Cube,        true,   0.6f, So::Grass,  T(0, 0), T(2, 0), T(3, 0), 255, Tl::Shovel, -1},
        /* DIRT         */ {"Dirt",           S::Cube,        true,   0.5f, So::Gravel, T(2, 0), T(2, 0), T(2, 0), 255, Tl::Shovel, -1},
        /* STONE        */ {"Stone",          S::Cube,        true,   1.5f, So::Stone,  T(1, 0), T(1, 0), T(1, 0), 255, Tl::Pickaxe, 0},
        /* SAND         */ {"Sand",           S::Cube,        true,   0.5f, So::Sand,   T(2, 1), T(2, 1), T(2, 1), 255, Tl::Shovel, -1},
        /* WATER        */ {"Water",          S::Translucent, false, -1.0f, So::None,   T(13, 12), T(13, 12), T(13, 12), 255, Tl::None, -1},
        /* LOG          */ {"Wood",           S::Cube,        true,   2.0f, So::Wood,   T(5, 1), T(5, 1), T(4, 1), 255, Tl::Axe, -1},
        /* LEAVES       */ {"Leaves",         S::Cutout,      true,   0.2f, So::Grass,  T(4, 3), T(4, 3), T(4, 3), 255, Tl::None, -1},
        /* PLANKS       */ {"Wooden Planks",  S::Cube,        true,   2.0f, So::Wood,   T(4, 0), T(4, 0), T(4, 0), 255, Tl::Axe, -1},
        /* COBBLE       */ {"Cobblestone",    S::Cube,        true,   2.0f, So::Stone,  T(0, 1), T(0, 1), T(0, 1), 255, Tl::Pickaxe, 0},
        /* GLASS        */ {"Glass",          S::Cutout,      true,   0.3f, So::Glass,  T(1, 3), T(1, 3), T(1, 3), 255, Tl::None, -1},
        /* BEDROCK      */ {"Bedrock",        S::Cube,        true,  -1.0f, So::Stone,  T(1, 1), T(1, 1), T(1, 1), 255, Tl::None, -1},
        /* BRICK        */ {"Bricks",         S::Cube,        true,   2.0f, So::Stone,  T(7, 0), T(7, 0), T(7, 0), 255, Tl::Pickaxe, 0},
        /* SNOW         */ {"Snowy Grass",    S::Cube,        true,   0.6f, So::Snow,   T(2, 4), T(2, 0), T(4, 4), 255, Tl::Shovel, -1},
        /* GRAVEL       */ {"Gravel",         S::Cube,        true,   0.6f, So::Gravel, T(3, 1), T(3, 1), T(3, 1), 255, Tl::Shovel, -1},
        /* COAL_ORE     */ {"Coal Ore",       S::Cube,        true,   3.0f, So::Stone,  T(2, 2), T(2, 2), T(2, 2), 255, Tl::Pickaxe, 0},
        /* IRON_ORE     */ {"Iron Ore",       S::Cube,        true,   3.0f, So::Stone,  T(1, 2), T(1, 2), T(1, 2), 255, Tl::Pickaxe, 1},
        /* GOLD_ORE     */ {"Gold Ore",       S::Cube,        true,   3.0f, So::Stone,  T(0, 2), T(0, 2), T(0, 2), 255, Tl::Pickaxe, 2},
        /* DIAMOND_ORE  */ {"Diamond Ore",    S::Cube,        true,   3.0f, So::Stone,  T(2, 3), T(2, 3), T(2, 3), 255, Tl::Pickaxe, 2},
        /* TALL_GRASS   */ {"Tall Grass",     S::Cross,       false,  0.0f, So::Grass,  T(7, 2), T(7, 2), T(7, 2), 255, Tl::None, -1},
        /* ROSE         */ {"Rose",           S::Cross,       false,  0.0f, So::Grass,  T(12, 0), T(12, 0), T(12, 0), 255, Tl::None, -1},
        /* DANDELION    */ {"Dandelion",      S::Cross,       false,  0.0f, So::Grass,  T(13, 0), T(13, 0), T(13, 0), 255, Tl::None, -1},
        /* BOOKSHELF    */ {"Bookshelf",      S::Cube,        true,   1.5f, So::Wood,   T(4, 0), T(4, 0), T(3, 2), 255, Tl::Axe, -1},
        /* MOSSY_COBBLE */ {"Moss Stone",     S::Cube,        true,   2.0f, So::Stone,  T(4, 2), T(4, 2), T(4, 2), 255, Tl::Pickaxe, 0},
        /* OBSIDIAN     */ {"Obsidian",       S::Cube,        true,  10.0f, So::Stone,  T(5, 2), T(5, 2), T(5, 2), 255, Tl::Pickaxe, 3},
        /* STONE_BRICK  */ {"Stone Bricks",   S::Cube,        true,   1.5f, So::Stone,  T(6, 3), T(6, 3), T(6, 3), 255, Tl::Pickaxe, 0},
        /* CRAFTING     */ {"Crafting Table", S::Cube,        true,   2.5f, So::Wood,   T(11, 2), T(4, 0), T(11, 3), T(12, 3), Tl::Axe, -1},
        /* WOOL         */ {"Wool",           S::Cube,        true,   0.8f, So::Cloth,  T(0, 4), T(0, 4), T(0, 4), 255, Tl::None, -1},
        /* GOLD_BLOCK   */ {"Block of Gold",  S::Cube,        true,   3.0f, So::Stone,  T(7, 1), T(7, 1), T(7, 1), 255, Tl::Pickaxe, 2},
        /* IRON_BLOCK   */ {"Block of Iron",  S::Cube,        true,   5.0f, So::Stone,  T(6, 1), T(6, 1), T(6, 1), 255, Tl::Pickaxe, 1},
        /* DIAMOND_BLK  */ {"Block of Diamond", S::Cube,      true,   5.0f, So::Stone,  T(8, 1), T(8, 1), T(8, 1), 255, Tl::Pickaxe, 2},
        /* TNT          */ {"TNT",            S::Cube,        true,   0.0f, So::Grass,  T(9, 0), T(10, 0), T(8, 0), 255, Tl::None, -1},
        /* ICE          */ {"Ice",            S::Translucent, true,   0.5f, So::Glass,  T(3, 4), T(3, 4), T(3, 4), 255, Tl::Pickaxe, -1},
        /* CLAY         */ {"Clay",           S::Cube,        true,   0.6f, So::Gravel, T(8, 4), T(8, 4), T(8, 4), 255, Tl::Shovel, -1},
        /* SANDSTONE    */ {"Sandstone",      S::Cube,        true,   0.8f, So::Stone,  T(0, 11), T(0, 13), T(0, 12), 255, Tl::Pickaxe, 0},
        /* TORCH        */ {"Torch",          S::Torch,       false,  0.0f, So::Wood,   T(0, 5), T(0, 5), T(0, 5), 255, Tl::None, -1},
        /* GLOWSTONE    */ {"Glowstone",      S::Cube,        true,   0.3f, So::Glass,  T(9, 6), T(9, 6), T(9, 6), 255, Tl::None, -1},
        /* LAVA         */ {"Lava",           S::Cube,        false, -1.0f, So::None,   T(13, 14), T(13, 14), T(13, 14), 255, Tl::None, -1},
        /* FURNACE      */ {"Furnace",        S::Cube,        true,   3.5f, So::Stone,  T(14, 3), T(14, 3), T(13, 2), T(12, 2), Tl::Pickaxe, 0},
        /* FURNACE_LIT  */ {"Furnace",        S::Cube,        true,   3.5f, So::Stone,  T(14, 3), T(14, 3), T(13, 2), T(13, 3), Tl::Pickaxe, 0},
        /* CHEST        */ {"Chest",          S::Chest,        true,   2.5f, So::Wood,   T(10, 2), T(10, 2), T(10, 1), T(11, 1), Tl::Axe, -1},
        /* SAPLING      */ {"Sapling",        S::Cross,       false,  0.0f, So::Grass,  T(15, 0), T(15, 0), T(15, 0), 255, Tl::None, -1},
        /* FARMLAND     */ {"Farmland",       S::Cube,        true,   0.6f, So::Gravel, T(7, 5), T(2, 0), T(2, 0), 255, Tl::Shovel, -1},
        /* WHEAT        */ {"Crops",          S::Crop,        false,  0.0f, So::Grass,  T(8, 5), T(8, 5), T(8, 5), 255, Tl::None, -1},
        /* SNOW_LAYER   */ {"Snow",           S::SnowLayer,   false,  0.1f, So::Snow,   T(2, 4), T(2, 4), T(2, 4), 255, Tl::Shovel, -1},
        /* SNOW_BLOCK   */ {"Snow",           S::Cube,        true,   0.2f, So::Snow,   T(2, 4), T(2, 4), T(2, 4), 255, Tl::Shovel, -1},
        /* CACTUS       */ {"Cactus",         S::Cactus,      true,   0.4f, So::Cloth,  T(5, 4), T(7, 4), T(6, 4), 255, Tl::None, -1},
        /* DEAD_BUSH    */ {"Dead Bush",      S::Cross,       false,  0.0f, So::Grass,  T(7, 3), T(7, 3), T(7, 3), 255, Tl::None, -1},
        /* REEDS        */ {"Reeds",          S::Cross,       false,  0.0f, So::Grass,  T(9, 4), T(9, 4), T(9, 4), 255, Tl::None, -1},
        /* PUMPKIN      */ {"Pumpkin",        S::Cube,        true,   1.0f, So::Wood,   T(6, 6), T(6, 6), T(6, 7), T(7, 7), Tl::Axe, -1},
        /* JACK         */ {"Jack 'o' Lantern", S::Cube,      true,   1.0f, So::Wood,   T(6, 6), T(6, 6), T(6, 7), T(8, 7), Tl::Axe, -1},
        /* BROWN_MUSH   */ {"Mushroom",       S::Cross,       false,  0.0f, So::Grass,  T(13, 1), T(13, 1), T(13, 1), 255, Tl::None, -1},
        /* RED_MUSH     */ {"Mushroom",       S::Cross,       false,  0.0f, So::Grass,  T(12, 1), T(12, 1), T(12, 1), 255, Tl::None, -1},
        /* MYCELIUM     */ {"Mycelium",       S::Cube,        true,   0.6f, So::Grass,  T(14, 4), T(2, 0), T(13, 4), 255, Tl::Shovel, -1},
        /* MOB_SPAWNER  */ {"Monster Spawner", S::Cutout,     true,   5.0f, So::Stone,  T(1, 4), T(1, 4), T(1, 4), 255, Tl::Pickaxe, 0},
        /* COBWEB       */ {"Cobweb",         S::Cross,       false,  4.0f, So::Cloth,  T(11, 0), T(11, 0), T(11, 0), 255, Tl::Sword, -1},
        /* FENCE        */ {"Fence",          S::Fence,       true,   2.0f, So::Wood,   T(4, 0), T(4, 0), T(4, 0), 255, Tl::Axe, -1},
        /* RAIL         */ {"Rail",           S::Rail,        false,  0.7f, So::Stone,  T(0, 8), T(0, 8), T(0, 8), 255, Tl::Pickaxe, -1},
        // Полублоки в 1.0 все «каменные» (и деревянный тоже добывается киркой)
        /* SLAB         */ {"Stone Slab",     S::Slab,        true,   2.0f, So::Stone,  T(6, 0), T(6, 0), T(5, 0), 255, Tl::Pickaxe, 0},
        /* DOUBLE_SLAB  */ {"Double Stone Slab", S::Cube,     true,   2.0f, So::Stone,  T(6, 0), T(6, 0), T(5, 0), 255, Tl::Pickaxe, 0},
        /* WOOD_STAIRS  */ {"Wooden Stairs",  S::Stairs,      true,   2.0f, So::Wood,   T(4, 0), T(4, 0), T(4, 0), 255, Tl::Axe, -1},
        /* COBBLE_STAIRS*/ {"Stone Stairs",   S::Stairs,      true,   2.0f, So::Stone,  T(0, 1), T(0, 1), T(0, 1), 255, Tl::Pickaxe, 0},
        /* BRICK_STAIRS */ {"Brick Stairs",   S::Stairs,      true,   2.0f, So::Stone,  T(7, 0), T(7, 0), T(7, 0), 255, Tl::Pickaxe, 0},
        /* SBRICK_STAIRS*/ {"Stone Brick Stairs", S::Stairs,  true,   1.5f, So::Stone,  T(6, 3), T(6, 3), T(6, 3), 255, Tl::Pickaxe, 0},
        /* LADDER       */ {"Ladder",         S::Ladder,      false,  0.4f, So::Wood,   T(3, 5), T(3, 5), T(3, 5), 255, Tl::None, -1},
        /* WOOD_DOOR    */ {"Wooden Door",    S::Door,        true,   3.0f, So::Wood,   T(1, 5), T(1, 6), T(1, 6), 255, Tl::Axe, -1},
        /* IRON_DOOR    */ {"Iron Door",      S::Door,        true,   5.0f, So::Stone,  T(2, 5), T(2, 6), T(2, 6), 255, Tl::Pickaxe, 0},
        /* TRAPDOOR     */ {"Trapdoor",       S::Trapdoor,    true,   3.0f, So::Wood,   T(4, 5), T(4, 5), T(4, 5), 255, Tl::Axe, -1},
        /* FENCE_GATE   */ {"Fence Gate",     S::FenceGate,   true,   2.0f, So::Wood,   T(4, 0), T(4, 0), T(4, 0), 255, Tl::Axe, -1},
        /* GLASS_PANE   */ {"Glass Pane",     S::Pane,        true,   0.3f, So::Glass,  T(1, 3), T(1, 3), T(1, 3), 255, Tl::None, -1},
        /* IRON_BARS    */ {"Iron Bars",      S::Pane,        true,   5.0f, So::Stone,  T(5, 5), T(5, 5), T(5, 5), 255, Tl::Pickaxe, 0},
        /* LAPIS_ORE    */ {"Lapis Lazuli Ore", S::Cube,      true,   3.0f, So::Stone,  T(0, 10), T(0, 10), T(0, 10), 255, Tl::Pickaxe, 1},
        /* LAPIS_BLOCK  */ {"Lapis Lazuli Block", S::Cube,    true,   3.0f, So::Stone,  T(0, 9), T(0, 9), T(0, 9), 255, Tl::Pickaxe, 1},
        /* REDSTONE_ORE */ {"Redstone Ore",   S::Cube,        true,   3.0f, So::Stone,  T(3, 3), T(3, 3), T(3, 3), 255, Tl::Pickaxe, 2},
        /* FIRE         */ {"Fire",           S::Fire,        false,  0.0f, So::None,   T(15, 1), T(15, 1), T(15, 1), 255, Tl::None, -1},
        /* CAKE         */ {"Cake",           S::Cake,        true,   0.5f, So::Cloth,  T(9, 7), T(12, 7), T(10, 7), 255, Tl::None, -1},
        /* BED          */ {"Bed",            S::Bed,         true,   0.2f, So::Wood,   T(6, 8), T(4, 0), T(6, 9), 255, Tl::None, -1},
        /* SIGN_POST    */ {"Sign",           S::Sign,        false,  1.0f, So::Wood,   T(4, 0), T(4, 0), T(4, 0), 255, Tl::Axe, -1},
        /* WALL_SIGN    */ {"Sign",           S::Sign,        false,  1.0f, So::Wood,   T(4, 0), T(4, 0), T(4, 0), 255, Tl::Axe, -1},
        /* WIRE         */ {"Redstone Dust",  S::Wire,        false,  0.0f, So::Stone,  T(4, 10), T(4, 10), T(4, 10), 255, Tl::None, -1},
        /* RS_TORCH_OFF */ {"Redstone Torch", S::Torch,       false,  0.0f, So::Wood,   T(3, 7), T(3, 7), T(3, 7), 255, Tl::None, -1},
        /* RS_TORCH_ON  */ {"Redstone Torch", S::Torch,       false,  0.0f, So::Wood,   T(3, 6), T(3, 6), T(3, 6), 255, Tl::None, -1},
        /* LEVER        */ {"Lever",          S::Lever,       false,  0.5f, So::Wood,   T(0, 6), T(0, 6), T(0, 6), 255, Tl::None, -1},
        /* STONE_BUTTON */ {"Button",         S::Button,      false,  0.5f, So::Stone,  T(1, 0), T(1, 0), T(1, 0), 255, Tl::None, -1},
        /* STONE_PLATE  */ {"Stone Pressure Plate", S::Plate, false,  0.5f, So::Stone,  T(1, 0), T(1, 0), T(1, 0), 255, Tl::Pickaxe, 0},
        /* WOOD_PLATE   */ {"Wooden Pressure Plate", S::Plate, false, 0.5f, So::Wood,   T(4, 0), T(4, 0), T(4, 0), 255, Tl::Axe, -1},
        /* REPEATER_OFF */ {"Redstone Repeater", S::Repeater, false,  0.0f, So::Wood,   T(3, 8), T(6, 0), T(6, 0), 255, Tl::None, -1},
        /* REPEATER_ON  */ {"Redstone Repeater", S::Repeater, false,  0.0f, So::Wood,   T(3, 9), T(6, 0), T(6, 0), 255, Tl::None, -1},
        /* NOTE_BLOCK   */ {"Note Block",     S::Cube,        true,   0.8f, So::Wood,   T(10, 4), T(10, 4), T(10, 4), 255, Tl::Axe, -1},
        /* JUKEBOX      */ {"Jukebox",        S::Cube,        true,   2.0f, So::Stone,  T(11, 4), T(10, 4), T(10, 4), 255, Tl::Axe, -1},
        /* PISTON       */ {"Piston",         S::Piston,      true,   0.5f, So::Stone,  T(11, 6), T(13, 6), T(12, 6), 255, Tl::None, -1},
        /* STICKY_PIST  */ {"Sticky Piston",  S::Piston,      true,   0.5f, So::Stone,  T(10, 6), T(13, 6), T(12, 6), 255, Tl::None, -1},
        /* PISTON_HEAD  */ {"Piston",         S::PistonHead,  true,   0.5f, So::Stone,  T(11, 6), T(11, 6), T(12, 6), 255, Tl::None, -1},
        /* DISPENSER    */ {"Dispenser",      S::Cube,        true,   3.5f, So::Stone,  T(14, 3), T(14, 3), T(13, 2), T(14, 2), Tl::Pickaxe, 0},
        /* NETHERRACK   */ {"Netherrack",     S::Cube,        true,   0.4f, So::Stone,  T(7, 6), T(7, 6), T(7, 6), 255, Tl::Pickaxe, 0},
        /* SOUL_SAND    */ {"Soul Sand",      S::Cube,        true,   0.5f, So::Sand,   T(8, 6), T(8, 6), T(8, 6), 255, Tl::Shovel, -1},
        /* NETHER_BRICK */ {"Nether Brick",   S::Cube,        true,   2.0f, So::Stone,  T(0, 14), T(0, 14), T(0, 14), 255, Tl::Pickaxe, 0},
        /* NETHER_FENCE */ {"Nether Brick Fence", S::Fence,   true,   2.0f, So::Stone,  T(0, 14), T(0, 14), T(0, 14), 255, Tl::Pickaxe, 0},
        /* NETHER_STAIRS*/ {"Nether Brick Stairs", S::Stairs, true,   2.0f, So::Stone,  T(0, 14), T(0, 14), T(0, 14), 255, Tl::Pickaxe, 0},
        /* NETHER_WART  */ {"Nether Wart",    S::Crop,        false,  0.0f, So::Grass,  T(2, 14), T(2, 14), T(2, 14), 255, Tl::None, -1},
        /* PORTAL       */ {"Portal",         S::Portal,      false, -1.0f, So::Glass,  T(14, 0), T(14, 0), T(14, 0), 255, Tl::None, -1},
        /* ENCHANT_TABLE*/ {"Enchantment Table", S::Table,    true,   5.0f, So::Stone,  T(6, 10), T(7, 11), T(6, 11), 255, Tl::Pickaxe, 0},
        /* BREWING      */ {"Brewing Stand",  S::Brewing,     true,   0.5f, So::Stone,  T(13, 9), T(12, 9), T(13, 9), 255, Tl::Pickaxe, 0},
        /* CAULDRON     */ {"Cauldron",       S::Cauldron,    true,   2.0f, So::Stone,  T(10, 8), T(11, 9), T(10, 9), 255, Tl::Pickaxe, 0},
        /* END_STONE    */ {"End Stone",      S::Cube,        true,   3.0f, So::Stone,  T(15, 10), T(15, 10), T(15, 10), 255, Tl::Pickaxe, 0},
        /* PORTAL_FRAME */ {"End Portal Frame", S::Frame,     true,  -1.0f, So::Glass,  T(14, 9), T(15, 10), T(15, 9), 255, Tl::None, -1},
        /* END_PORTAL   */ {"End Portal",     S::EndPortal,   false, -1.0f, So::None,   T(8, 11), T(8, 11), T(8, 11), 255, Tl::None, -1},
        /* DRAGON_EGG   */ {"Dragon Egg",     S::Egg,         true,   3.0f, So::Stone,  T(7, 10), T(7, 10), T(7, 10), 255, Tl::None, -1},
        /* MONSTER_EGG  */ {"Monster Egg",    S::Cube,        true,   0.75f, So::Stone, T(1, 0), T(1, 0), T(1, 0), 255, Tl::None, -1},
        /* PUMPKIN_STEM */ {"Pumpkin Stem",   S::Stem,        false,  0.0f, So::Grass,  T(15, 6), T(15, 6), T(15, 6), 255, Tl::None, -1},
        /* MELON_STEM   */ {"Melon Stem",     S::Stem,        false,  0.0f, So::Grass,  T(15, 6), T(15, 6), T(15, 6), 255, Tl::None, -1},
        /* MELON_BLOCK  */ {"Melon",          S::Cube,        true,   1.0f, So::Wood,   T(9, 8), T(9, 8), T(8, 8), 255, Tl::Axe, -1},
        /* VINE         */ {"Vines",          S::Vine,        false,  0.2f, So::Grass,  T(15, 8), T(15, 8), T(15, 8), 255, Tl::None, -1},
        /* LILY_PAD     */ {"Lily Pad",       S::LilyPad,     true,   0.0f, So::Grass,  T(12, 4), T(12, 4), T(12, 4), 255, Tl::None, -1},
        /* POWERED_RAIL */ {"Powered Rail",   S::Rail,        false,  0.7f, So::Stone,  T(3, 10), T(3, 10), T(3, 10), 255, Tl::Pickaxe, -1},
        /* DETECTOR_RAIL*/ {"Detector Rail",  S::Rail,        false,  0.7f, So::Stone,  T(3, 12), T(3, 12), T(3, 12), 255, Tl::Pickaxe, -1},
        /* EMERALD_ORE  */ {"Emerald Ore",    S::Cube,        true,   3.0f, So::Stone,  T(11, 10), T(11, 10), T(11, 10), 255, Tl::Pickaxe, 2},
        /* EMERALD_BLOCK*/ {"Block of Emerald", S::Cube,      true,   5.0f, So::Stone,  T(9, 1), T(9, 1), T(9, 1), 255, Tl::Pickaxe, 2},
        /* COMMAND_BLOCK*/ {"Command Block",  S::Cube,        true,  -1.0f, So::Stone,  T(8, 11), T(8, 11), T(8, 11), 255, Tl::None, -1},
        /* BEACON       */ {"Beacon",         S::Beacon,      true,   3.0f, So::Glass,  T(9, 2), T(9, 2), T(9, 2), 255, Tl::Pickaxe, 0},
        /* ANVIL        */ {"Anvil",          S::Anvil,       true,   5.0f, So::Stone,  T(7, 14), T(7, 13), T(7, 13), 255, Tl::Pickaxe, 0},
        /* COBBLE_WALL  */ {"Cobblestone Wall", S::Fence,     true,   2.0f, So::Stone,  T(0, 1), T(0, 1), T(0, 1), 255, Tl::Pickaxe, 0},
        /* FLOWER_POT   */ {"Flower Pot",     S::FlowerPot,   false,  0.0f, So::Stone,  T(2, 0), T(10, 11), T(10, 11), 255, Tl::None, -1},
        /* CARROTS      */ {"Carrots",        S::Crop,        false,  0.0f, So::Grass,  T(8, 12), T(8, 12), T(8, 12), 255, Tl::None, -1},
        /* POTATOES     */ {"Potatoes",       S::Crop,        false,  0.0f, So::Grass,  T(12, 12), T(12, 12), T(12, 12), 255, Tl::None, -1},
        /* WOOD_BUTTON  */ {"Button",         S::Button,      false,  0.5f, So::Wood,   T(4, 0), T(4, 0), T(4, 0), 255, Tl::Axe, -1},
        /* SKULL_BLOCK  */ {"Mob Head",       S::Skull,       false,  1.0f, So::Stone,  T(8, 6), T(8, 6), T(8, 6), 255, Tl::Pickaxe, 0},
        /* ENDER_CHEST  */ {"Ender Chest",    S::Chest,       true,  22.5f, So::Stone,  T(10, 3), T(10, 3), T(9, 3), T(11, 11), Tl::Pickaxe, 0},
        /* LAMP_OFF     */ {"Redstone Lamp",  S::Cube,        true,   0.3f, So::Glass,  T(3, 13), T(3, 13), T(3, 13), 255, Tl::None, -1},
        /* LAMP_ON      */ {"Redstone Lamp",  S::Cube,        true,   0.3f, So::Glass,  T(4, 13), T(4, 13), T(4, 13), 255, Tl::None, -1},
        /* SAND_STAIRS  */ {"Sandstone Stairs", S::Stairs,    true,   0.8f, So::Stone,  T(0, 11), T(0, 13), T(0, 12), 255, Tl::Pickaxe, 0},
        /* SPRUCE_STAIRS*/ {"Spruce Wood Stairs", S::Stairs,  true,   2.0f, So::Wood,   T(6, 12), T(6, 12), T(6, 12), 255, Tl::Axe, -1},
        /* BIRCH_STAIRS */ {"Birch Wood Stairs", S::Stairs,   true,   2.0f, So::Wood,   T(6, 13), T(6, 13), T(6, 13), 255, Tl::Axe, -1},
        /* JUNGLE_STAIRS*/ {"Jungle Wood Stairs", S::Stairs,  true,   2.0f, So::Wood,   T(7, 12), T(7, 12), T(7, 12), 255, Tl::Axe, -1},
    };
    return INFO[b < BLOCK_COUNT ? b : 0];
}

inline const char* blockName(uint8_t b) { return blockInfo(b).name; }
inline Shape blockShape(uint8_t b) { return blockInfo(b).shape; }
inline bool isSolid(uint8_t b) { return blockInfo(b).solid; }
// Полностью закрывает соседнюю грань (лава ниже уровня блока, поэтому не закрывает)
inline bool isOpaque(uint8_t b) { return blockShape(b) == Shape::Cube && b != LAVA; }
inline bool isFence(uint8_t b) { return b == FENCE || b == NETHER_FENCE || b == COBBLE_WALL; }
inline bool isLiquid(uint8_t b) { return b == WATER || b == LAVA; }
inline bool isPlant(uint8_t b) { Shape s = blockShape(b); return (s == Shape::Cross || s == Shape::Crop) && b != COBWEB; }
// Держится только на блоке снизу (срывается, если опору убрать)
inline bool needsFloor(uint8_t b) {
    return isPlant(b) || b == SNOW_LAYER || b == RAIL || b == CACTUS || b == PUMPKIN_STEM || b == MELON_STEM || b == LILY_PAD ||
           b == POWERED_RAIL || b == DETECTOR_RAIL || b == FLOWER_POT || b == SKULL_BLOCK;
}
// Заменяется при установке блока (трава, снег, мёртвый куст)
inline bool isReplaceable(uint8_t b) {
    return b == AIR || b == TALL_GRASS || b == DEAD_BUSH || b == SNOW_LAYER || b == WATER || b == LAVA || b == FIRE;
}
inline bool isStairs(uint8_t b) {
    return b == WOOD_STAIRS || b == COBBLE_STAIRS || b == BRICK_STAIRS || b == STONEBRICK_STAIRS || b == NETHER_STAIRS ||
           b == SANDSTONE_STAIRS || b == SPRUCE_STAIRS || b == BIRCH_STAIRS || b == JUNGLE_STAIRS;
}
inline bool isDoor(uint8_t b) { return b == WOOD_DOOR || b == IRON_DOOR; }
// Сторона 0..3 (+X, +Z, -X, -Z) -> индекс направления (0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z)
inline int sideToDir(int s) { static const int D[4] = {0, 4, 1, 5}; return D[s & 3]; }
// На каком соседе держится блок (индекс направления), -1 — ни на каком
inline int supportDir(uint8_t b, uint8_t meta);
inline bool isTranslucent(uint8_t b) { return blockShape(b) == Shape::Translucent; }
// Даёт тень в ambient occlusion
inline bool occludesAO(uint8_t b) { return isOpaque(b) || b == LEAVES; }
// Можно ли навести курсор (вода и воздух — нет)
inline bool isTargetable(uint8_t b) { return b != AIR && !isLiquid(b) && b != FIRE && b != PORTAL && b != END_PORTAL; }
// Блок с интерфейсом (открывается правой кнопкой)
inline bool hasGui(uint8_t b) {
    return b == CRAFTING_TABLE || b == FURNACE || b == FURNACE_LIT || b == CHEST || b == DISPENSER || b == ENCHANT_TABLE || b == BREWING_STAND ||
           b == ENDER_CHEST;
}
inline bool isPiston(uint8_t b) { return b == PISTON || b == STICKY_PISTON; }
inline bool isRedstoneTorch(uint8_t b) { return b == REDSTONE_TORCH_ON || b == REDSTONE_TORCH_OFF; }
inline bool isRepeater(uint8_t b) { return b == REPEATER_ON || b == REPEATER_OFF; }
// Блок участвует в редстоун-схемах (источник, провод или приёмник)
inline bool isRedstoneBlock(uint8_t b) {
    return b == REDSTONE_WIRE || isRedstoneTorch(b) || isRepeater(b) || b == LEVER || b == STONE_BUTTON || b == WOOD_BUTTON || b == STONE_PLATE ||
           b == WOOD_PLATE || b == NOTE_BLOCK || b == TNT || b == WOOD_DOOR || b == IRON_DOOR || b == TRAPDOOR ||
           b == PISTON || b == STICKY_PISTON || b == DISPENSER || b == POWERED_RAIL || b == DETECTOR_RAIL || b == REDSTONE_LAMP_OFF ||
           b == REDSTONE_LAMP_ON;
}
// Предмет-«блок», который игрок не держит в руках (ставится предметом)
inline bool isItemOnlyBlock(uint8_t b) {
    return b == BED || b == SIGN_POST || b == WALL_SIGN || b == REDSTONE_WIRE || b == REDSTONE_TORCH_OFF || isRepeater(b) ||
           b == PISTON_HEAD || b == BREWING_STAND || b == CAULDRON || b == END_PORTAL || b == CARROTS || b == POTATOES ||
           b == FLOWER_POT || b == SKULL_BLOCK;
}
// Блок рисуется в инвентаре плоской иконкой, а не кубиком
inline bool isFlatItem(uint8_t b) {
    Shape s = blockShape(b);
    return s == Shape::Cross || s == Shape::Torch || s == Shape::Crop || s == Shape::Rail || s == Shape::Ladder ||
           s == Shape::Pane || s == Shape::Fire || s == Shape::Lever || s == Shape::Wire || s == Shape::Vine || s == Shape::LilyPad ||
           b == WOOD_BUTTON;
}

// Поворот точки блока [0,1]^3: локальная +Y переходит в направление f (индекс как DIRS)
inline glm::vec3 orientPoint(glm::vec3 L, int f) {
    switch (f) {
    case 3: return {L.x, 1 - L.y, 1 - L.z};
    case 4: return {L.x, 1 - L.z, L.y};
    case 5: return {L.x, L.z, 1 - L.y};
    case 0: return {L.y, 1 - L.x, L.z};
    case 1: return {1 - L.y, L.x, L.z};
    default: return L;
    }
}
inline glm::vec3 orientDir(glm::vec3 v, int f) {
    switch (f) {
    case 3: return {v.x, -v.y, -v.z};
    case 4: return {v.x, -v.z, v.y};
    case 5: return {v.x, v.z, -v.y};
    case 0: return {v.y, -v.x, v.z};
    case 1: return {-v.y, v.x, v.z};
    default: return v;
    }
}

// Модель блока-предмета из коробок в [0,1]^3 (иконка в инвентаре, предмет в руке и на земле)
inline void itemModelBoxes(uint8_t b, std::vector<std::pair<glm::vec3, glm::vec3>>& out) {
    const float k = 1.f / 16.f;
    switch (blockShape(b)) {
    case Shape::Slab: out.push_back({{0, 0, 0}, {1, 0.5f, 1}}); return;
    case Shape::Stairs:
        out.push_back({{0, 0, 0}, {1, 0.5f, 1}});
        out.push_back({{0, 0.5f, 0}, {0.5f, 1, 1}});
        return;
    case Shape::Trapdoor: out.push_back({{0, 0, 0}, {1, 3 * k, 1}}); return;
    case Shape::Egg: {
        // Яйцо дракона и в руке/инвентаре — стопка сужающихся слоёв (как renderBlockDragonEgg)
        const float rows[8][2] = {{0, 6}, {1, 8}, {2, 10}, {3, 12}, {5, 14}, {8, 14}, {11, 12}, {13, 8}};
        for (int i = 0; i < 8; ++i) {
            float w = rows[i][1] * k * 0.5f, y0 = rows[i][0] * k, y1 = (i < 7 ? rows[i + 1][0] : 16) * k;
            out.push_back({{0.5f - w, y0, 0.5f - w}, {0.5f + w, y1, 0.5f + w}});
        }
        return;
    }
    case Shape::Fence:
        out.push_back({{6 * k, 0, 0}, {10 * k, 1, 4 * k}});
        out.push_back({{6 * k, 0, 12 * k}, {10 * k, 1, 1}});
        out.push_back({{7 * k, 12 * k, 4 * k}, {9 * k, 15 * k, 12 * k}});
        out.push_back({{7 * k, 6 * k, 4 * k}, {9 * k, 9 * k, 12 * k}});
        return;
    case Shape::FenceGate:
        out.push_back({{7 * k, 5 * k, 0}, {9 * k, 1, 2 * k}});
        out.push_back({{7 * k, 5 * k, 14 * k}, {9 * k, 1, 1}});
        out.push_back({{7 * k, 12 * k, 2 * k}, {9 * k, 15 * k, 14 * k}});
        out.push_back({{7 * k, 6 * k, 2 * k}, {9 * k, 9 * k, 14 * k}});
        out.push_back({{7 * k, 9 * k, 6 * k}, {9 * k, 12 * k, 10 * k}});
        return;
    case Shape::Cake: out.push_back({{k, 0, k}, {1 - k, 0.5f, 1 - k}}); return;
    case Shape::Cactus: out.push_back({{k, 0, k}, {1 - k, 1, 1 - k}}); return;
    case Shape::SnowLayer: out.push_back({{0, 0, 0}, {1, 2 * k, 1}}); return;
    case Shape::Plate: out.push_back({{k, 0, k}, {1 - k, k, 1 - k}}); return;
    case Shape::Table: out.push_back({{0, 0, 0}, {1, 0.75f, 1}}); return;
    case Shape::Frame: out.push_back({{0, 0, 0}, {1, 13 * k, 1}}); return;
    case Shape::Button: out.push_back({{5 * k, 6 * k, 6 * k}, {11 * k, 10 * k, 10 * k}}); return;
    case Shape::Repeater: out.push_back({{0, 0, 0}, {1, 2 * k, 1}}); return;
    case Shape::Anvil:
        // RenderBlocks.renderBlockAnvilOrient 1.4.2: основание, плита, шейка, верх
        out.push_back({{2 * k, 0, 2 * k}, {14 * k, 4 * k, 14 * k}});
        out.push_back({{4 * k, 4 * k, 3 * k}, {12 * k, 5 * k, 13 * k}});
        out.push_back({{6 * k, 5 * k, 4 * k}, {10 * k, 10 * k, 12 * k}});
        out.push_back({{3 * k, 10 * k, 0}, {13 * k, 1, 1}});
        return;
    case Shape::FlowerPot: out.push_back({{5 * k, 0, 5 * k}, {11 * k, 6 * k, 11 * k}}); return;
    case Shape::Skull: out.push_back({{4 * k, 0, 4 * k}, {12 * k, 8 * k, 12 * k}}); return;
    default: out.push_back({{0, 0, 0}, {1, 1, 1}}); return;
    }
}
// Блок-предмет с вариантами в damage (древесина, шерсть, полублоки)
inline bool blockHasVariants(uint8_t b) {
    return b == LOG || b == LEAVES || b == SAPLING || b == WOOL || b == SLAB || b == DOUBLE_SLAB || b == STONE_BRICK || b == MONSTER_EGG ||
           b == PLANKS || b == SANDSTONE || b == COBBLE_WALL;
}

// Сколько света поглощает блок (0..15), как lightOpacity в 1.0
inline int lightOpacity(uint8_t b) {
    if (b == LEAVES) return 1;
    if (b == WATER || b == ICE) return 3;
    if (b == LAVA) return 15;
    return isOpaque(b) ? 15 : 0;
}
// Собственное свечение блока (0..15)
inline int lightEmission(uint8_t b) {
    switch (b) {
    case TORCH: return 14;
    case GLOWSTONE: case LAVA: return 15;
    case FURNACE_LIT: return 13;
    case JACK_O_LANTERN: case FIRE: return 15;
    case REDSTONE_TORCH_ON: case REPEATER_ON: return 7;
    case PORTAL: return 11;
    case END_PORTAL: return 15;
    case DRAGON_EGG: return 1;
    case BROWN_MUSHROOM: return 1;
    case BEACON: return 15;
    case ENDER_CHEST: return 7;
    case REDSTONE_LAMP_ON: return 15;
    default: return 0;
    }
}
// Скольжение по блоку (лёд — скользкий)
inline float slipperiness(uint8_t b) { return b == ICE ? 0.98f : 0.6f; }

// Направление «лица» блока (печь, сундук) в метаданных, как в 1.0: 2 -Z, 3 +Z, 4 -X, 5 +X
inline int facingToDir(uint8_t meta) {
    switch (meta) {
    case 2: return 5;
    case 3: return 4;
    case 4: return 1;
    case 5: return 0;
    default: return 5;
    }
}

// dir: 0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z
inline int blockTex(uint8_t b, int dir, uint8_t meta = 2) {
    const BlockInfo& i = blockInfo(b);
    if (b == SLAB || b == DOUBLE_SLAB) {
        switch (meta & 7) {
        case 1: return dir == 2 ? T(0, 11) : dir == 3 ? T(0, 13) : T(0, 12); // песчаник
        case 2: return T(4, 0);                                              // доски
        case 3: return T(0, 1);                                              // булыжник
        case 4: return T(7, 0);                                              // кирпич
        case 5: return T(6, 3);                                              // каменный кирпич
        default: return (dir == 2 || dir == 3) ? T(6, 0) : T(5, 0);
        }
    }
    if (b == WOOL && meta != 0) {
        // BlockCloth.getBlockTextureFromSideAndMetadata
        int j = ~(int)(meta & 15);
        return 113 + ((j & 8) >> 3) + (j & 7) * 16;
    }
    if (isDoor(b)) return (meta & 8) ? i.top : i.side;
    if (b == STONE_BRICK) return meta == 1 ? T(4, 6) : meta == 2 ? T(5, 6) : meta == 3 ? T(5, 13) : T(6, 3); // мшистый, треснутый, резной
    // Доски 1.4.2: дуб, ель, берёза, джунгли
    if (b == PLANKS) return (meta & 3) == 1 ? T(6, 12) : (meta & 3) == 2 ? T(6, 13) : (meta & 3) == 3 ? T(7, 12) : T(4, 0);
    // Песчаник 1.4.2 (BlockSandStone): верх общий; у резного (1) и гладкого (2) низ как верх, свои бока
    if (b == SANDSTONE) {
        int m = meta & 3;
        if (dir == 2 || (dir == 3 && (m == 1 || m == 2))) return T(0, 11);
        if (dir == 3) return T(0, 13);
        return m == 1 ? T(5, 14) : m == 2 ? T(6, 14) : T(0, 12);
    }
    if (b == MONSTER_EGG) return meta == 1 ? T(0, 1) : meta == 2 ? T(6, 3) : T(1, 0);   // чешуйница в камне
    if (b == BED) {
        bool head = meta & 8;
        if (dir == 2) return head ? T(7, 8) : T(6, 8);
        if (dir == 3) return T(4, 0);
        return head ? T(7, 9) : T(6, 9);
    }
    if (b == CAKE) {
        if (dir == 2) return T(9, 7);
        if (dir == 3) return T(12, 7);
        return (dir == 1 && meta > 0) ? T(11, 7) : T(10, 7);
    }
    if (b == WHEAT) return T(8 + (meta & 7), 5);
    if (b == POWERED_RAIL) return (meta & 8) ? T(3, 11) : T(3, 10);
    if (b == RAIL && meta >= 6) return T(0, 7);
    if (b == NETHER_WART) return T(2 + std::min(2, (meta & 3) == 3 ? 2 : (meta & 3) >= 1 ? 1 : 0), 14);
    if (b == LOG && dir != 2 && dir != 3) return (meta & 3) == 1 ? T(4, 7) : (meta & 3) == 2 ? T(5, 7) : (meta & 3) == 3 ? T(9, 9) : T(4, 1);
    if (b == LEAVES) return (meta & 3) == 1 ? T(4, 8) : (meta & 3) == 3 ? T(4, 12) : T(4, 3);
    if (b == SAPLING) return (meta & 3) == 1 ? T(15, 3) : (meta & 3) == 2 ? T(15, 4) : (meta & 3) == 3 ? T(14, 1) : T(15, 0);
    if (b == TALL_GRASS) return meta == 2 ? T(8, 3) : T(7, 2);
    if (b == FARMLAND && dir == 2) return meta > 0 ? T(6, 5) : T(7, 5);
    if (b == COBBLE_WALL) return (meta & 1) ? T(4, 2) : T(0, 1);
    if (b == CARROTS || b == POTATOES) {
        int m = meta & 7;
        if (m == 7) return b == CARROTS ? T(11, 12) : T(12, 12);
        return T(8 + (std::min(m, 5) >> 1), 12);
    }
    // Наковальня: верх по износу (биты 2-3: целая, повреждённая, сильно повреждённая), остальное — основание
    if (b == ANVIL) {
        if (dir != 2) return T(7, 13);
        int dmg = (meta >> 2) & 3;
        return dmg == 1 ? T(8, 13) : dmg == 2 ? T(8, 14) : T(7, 14);
    }
    if (dir == 2) return i.top;
    if (dir == 3) return i.bottom;
    if (i.front != 255 && dir == facingToDir(meta)) return i.front;
    return i.side;
}

inline int supportDir(uint8_t b, uint8_t meta) {
    switch (b) {
    case TORCH:
        switch (meta) {
        case 1: return 1; // TORCH_WEST_WALL — стена с -X
        case 2: return 0;
        case 3: return 5;
        case 4: return 4;
        default: return 3;
        }
    case LADDER: case WALL_SIGN: return sideToDir(meta);
    case REDSTONE_TORCH_ON: case REDSTONE_TORCH_OFF:
        return supportDir(TORCH, meta);
    case LEVER: case STONE_BUTTON: case WOOD_BUTTON:
        return (meta & 7) == 5 ? 3 : supportDir(TORCH, meta & 7);
    case BED: case SIGN_POST: case REDSTONE_WIRE: case STONE_PLATE: case WOOD_PLATE: case REPEATER_ON: case REPEATER_OFF:
    case FLOWER_POT:
        return 3;
    case SKULL_BLOCK: // бит 3 — на стене, биты 4-5 — сторона (куда смотрит голова), стена — с обратной стороны
        return (meta & 8) ? (sideToDir((meta >> 4) & 3) ^ 1) : 3;
    case TRAPDOOR: return sideToDir(meta & 3);
    case WOOD_DOOR: case IRON_DOOR: case CAKE: return 3;
    default: return needsFloor(b) ? 3 : -1;
    }
}

// Цветочный горшок 1.4.2 (BlockFlowerPot): мета 1..11 — что в нём растёт
inline bool flowerPotPlant(uint8_t potMeta, uint8_t& b, uint8_t& m) {
    static const uint8_t P[12][2] = {{AIR, 0},     {ROSE, 0},         {DANDELION, 0},    {SAPLING, 0},
                                     {SAPLING, 1}, {SAPLING, 2},      {SAPLING, 3},      {RED_MUSHROOM, 0},
                                     {BROWN_MUSHROOM, 0}, {CACTUS, 0}, {DEAD_BUSH, 0},    {TALL_GRASS, 2}};
    if (potMeta == 0 || potMeta >= 12) return false;
    b = P[potMeta][0];
    m = P[potMeta][1];
    return true;
}
// Мета горшка для растения (0 — в горшок не сажается)
inline uint8_t flowerPotMetaFor(uint8_t b, uint8_t m) {
    for (uint8_t i = 1; i < 12; ++i) {
        uint8_t pb, pm;
        flowerPotPlant(i, pb, pm);
        if (pb == b && (b != SAPLING && b != TALL_GRASS ? true : (m & 3) == pm)) return i;
    }
    return 0;
}

// Подкраска грани по биому: 0 — нет, 1 — цвет травы, 2 — цвет листвы, 3 — ель, 4 — берёза
inline int tintType(uint8_t b, int dir, uint8_t meta) {
    if (b == GRASS && dir == 2) return 1;
    if (b == VINE) return 2;
    if (b == TALL_GRASS) return 1;
    if (b == LEAVES) return (meta & 3) == 1 ? 3 : (meta & 3) == 2 ? 4 : 2;
    return 0;
}

// Тайлы, которые в terrain.png серые и красятся под биом
inline bool tileNeedsGrassTint(int t) { return t == T(0, 0) || t == T(7, 2) || t == T(8, 3); }
inline bool tileNeedsFoliageTint(int t) {
    return t == T(4, 3) || t == T(5, 3) || t == T(4, 8) || t == T(5, 8) || t == T(15, 8) || t == T(4, 12) || t == T(5, 12); // + листва джунглей
}
inline bool isClimbable(uint8_t b) { return b == LADDER || b == VINE; }
