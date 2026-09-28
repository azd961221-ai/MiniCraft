#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <array>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "Biome.h"
#include "Blocks.h"
#include "Inventory.h"
#include "Noise.h"

constexpr int CW = 16;   // ширина чанка (X, Z)
constexpr int CH = 128;  // высота мира, как в 1.0
constexpr int SEA = 63;  // верхний блок воды (уровень моря 64, как в 1.0)

inline int floorDiv(int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); }
inline int64_t chunkKey(int cx, int cz) { return (int64_t)((uint64_t)(int64_t)cx << 32) ^ (uint32_t)cz; } // без сдвига отрицательного
inline int64_t posKey(int x, int y, int z) {
    return ((int64_t)(x & 0x3FFFFFF) << 38) | ((int64_t)(y & 0xFFF) << 26) | (int64_t)(z & 0x3FFFFFF);
}

inline glm::ivec3 posFromKey(int64_t k) {
    auto sx = [](int64_t v, int bits) { int64_t m = 1LL << (bits - 1); return (int)((v ^ m) - m); };
    return {sx((k >> 38) & 0x3FFFFFF, 26), (int)((k >> 26) & 0xFFF), sx(k & 0x3FFFFFF, 26)};
}

// shade — затенение грани и AO; sky/block — освещённость 0..1 (уровень / 15); r,g,b — подкраска биома
struct Vertex {
    float x, y, z, u, v, shade, sky, block;
    float r = 1.f, g = 1.f, b = 1.f;
};
// Настройка атрибутов Vertex для текущего VAO (0 — позиция, 1 — UV, 2 — свет, 3 — цвет)
void setupVertexAttribs();
// Блок может загореться (canBlockCatchFire) — по нему «ползёт» пламя
bool fireCanCatch(uint8_t b);

enum { MESH_SOLID = 0, MESH_TRANSLUCENT = 1, MESH_CUTOUT = 2, MESH_COUNT = 3 };

// Метаданные факела (как в 1.0): к какой стене прикреплён
enum TorchMeta : uint8_t { TORCH_WEST_WALL = 1, TORCH_EAST_WALL = 2, TORCH_NORTH_WALL = 3, TORCH_SOUTH_WALL = 4, TORCH_FLOOR = 5 };
// Бит «поставлено игроком» у листвы — такая листва не опадает
constexpr uint8_t LEAVES_PLAYER = 4;

struct Chunk {
    int cx, cz;
    std::vector<uint8_t> blocks;
    std::vector<uint8_t> meta;    // 4 бита данных на блок (ориентация, уровень жидкости и т.п.)
    std::vector<uint8_t> light;   // старшие 4 бита — небо, младшие — блоки
    uint8_t height[CW * CW];      // первый снизу блок над самым верхним поглощающим свет
    uint8_t biome[CW * CW];       // биом каждой колонки
    bool dirty = true;   // нужно перестроить меш
    bool meshed = false;
    std::vector<glm::ivec3> chests; // сундуки в чанке (собираются при построении меша, рисуются моделью)
    std::vector<glm::ivec3> endPortals; // блоки портала Края (рисуются отдельным шейдером с глубиной)
    GLuint vao[MESH_COUNT] = {}, vbo[MESH_COUNT] = {};
    int count[MESH_COUNT] = {};

    Chunk(int x, int z) : cx(x), cz(z), blocks(CW * CH * CW, AIR), meta(CW * CH * CW, 0), light(CW * CH * CW, 0) {}
    ~Chunk();
    Chunk(const Chunk&) = delete;
    Chunk& operator=(const Chunk&) = delete;

    static int index(int x, int y, int z) { return (y * CW + z) * CW + x; }
    uint8_t get(int x, int y, int z) const { return blocks[index(x, y, z)]; }
    void set(int x, int y, int z, uint8_t b, uint8_t m = 0) { int i = index(x, y, z); blocks[i] = b; meta[i] = m; }
    void recomputeHeight(int x, int z);
};

// Случайное смещение травы и цветов внутри блока (зависит только от координат)
inline bool hasPlantOffset(uint8_t b) { return b == TALL_GRASS || b == ROSE || b == DANDELION || b == DEAD_BUSH; }
inline glm::vec3 plantOffset(uint8_t b, int x, int z) {
    int64_t h = (int64_t)(x * 3129871) ^ ((int64_t)z * 116129781LL);
    h = h * h * 42317861LL + h * 11LL;
    float ox = (((h >> 16) & 15) / 15.f - 0.5f) * 0.5f, oz = (((h >> 24) & 15) / 15.f - 0.5f) * 0.5f;
    float oy = b == TALL_GRASS ? (((h >> 20) & 15) / 15.f - 1.f) * 0.2f : 0.f;
    return glm::vec3(ox, oy, oz);
}

// Границы блока внутри клетки: для рамки выделения, трещин и луча выбора
inline void blockBounds(uint8_t b, uint8_t meta, int x, int z, glm::vec3& mn, glm::vec3& mx) {
    mn = glm::vec3(0.f);
    mx = glm::vec3(1.f);
    switch (b) {
    case TORCH:
        switch (meta) {
        case TORCH_WEST_WALL: mn = {0.f, 0.2f, 0.35f}; mx = {0.3f, 0.8f, 0.65f}; break;
        case TORCH_EAST_WALL: mn = {0.7f, 0.2f, 0.35f}; mx = {1.f, 0.8f, 0.65f}; break;
        case TORCH_NORTH_WALL: mn = {0.35f, 0.2f, 0.f}; mx = {0.65f, 0.8f, 0.3f}; break;
        case TORCH_SOUTH_WALL: mn = {0.35f, 0.2f, 0.7f}; mx = {0.65f, 0.8f, 1.f}; break;
        default: mn = {0.4f, 0.f, 0.4f}; mx = {0.6f, 0.6f, 0.6f}; break;
        }
        break;
    case ROSE: case DANDELION: mn = {0.3f, 0.f, 0.3f}; mx = {0.7f, 0.6f, 0.7f}; break;
    case BROWN_MUSHROOM: case RED_MUSHROOM: mn = {0.3f, 0.f, 0.3f}; mx = {0.7f, 0.4f, 0.7f}; break;
    case TALL_GRASS: case DEAD_BUSH: case SAPLING: mn = {0.1f, 0.f, 0.1f}; mx = {0.9f, 0.8f, 0.9f}; break;
    case REEDS: mn = {0.125f, 0.f, 0.125f}; mx = {0.875f, 1.f, 0.875f}; break;
    case WHEAT: mx = {1.f, 0.25f, 1.f}; break;
    case SNOW_LAYER: case RAIL: case POWERED_RAIL: case DETECTOR_RAIL: mx = {1.f, 0.125f, 1.f}; break;
    case NETHER_WART: mx = {1.f, 0.25f, 1.f}; break;
    case END_PORTAL: mx = {1.f, 0.75f, 1.f}; break;
    case PORTAL: // пластина по оси рамки (мета 1 — рамка вдоль Z)
        if (meta & 1) { mn.x = 0.375f; mx.x = 0.625f; } else { mn.z = 0.375f; mx.z = 0.625f; }
        break;
    case VINE: {
        // Лоза: тонкая пластина у каждой опоры (объединение); под потолком — у верха
        const float t = 1.f / 16.f;
        if (meta == 0) { mn.y = 1.f - t; break; }
        mn = glm::vec3(1.f); mx = glm::vec3(0.f);
        auto add = [&](glm::vec3 a, glm::vec3 b) { mn = glm::min(mn, a); mx = glm::max(mx, b); };
        if (meta & 1) add({0, 0, 1 - t}, {1, 1, 1});
        if (meta & 2) add({0, 0, 0}, {t, 1, 1});
        if (meta & 4) add({0, 0, 0}, {1, 1, t});
        if (meta & 8) add({1 - t, 0, 0}, {1, 1, 1});
        break;
    }
    case CACTUS: mn = {0.0625f, 0.f, 0.0625f}; mx = {0.9375f, 1.f, 0.9375f}; break;
    case FENCE: case NETHER_FENCE: mn = {0.375f, 0.f, 0.375f}; mx = {0.625f, 1.f, 0.625f}; break;
    case SLAB: if (meta & 8) mn.y = 0.5f; else mx.y = 0.5f; break;
    case LILY_PAD: mx = {1.f, 1 / 64.f, 1.f}; break;
    case PUMPKIN_STEM: case MELON_STEM: mn = {0.375f, 0.f, 0.375f}; mx = {0.625f, 0.25f, 0.625f}; break;
    case ENCHANT_TABLE: mx = {1.f, 0.75f, 1.f}; break;
    case BREWING_STAND: mn = {0.125f, 0.f, 0.125f}; mx = {0.875f, 0.875f, 0.875f}; break;
    case STONE_PLATE: case WOOD_PLATE: mn = {1 / 16.f, 0.f, 1 / 16.f}; mx = {15 / 16.f, meta ? 1 / 32.f : 1 / 16.f, 15 / 16.f}; break;
    case REDSTONE_WIRE: mx = {1.f, 1 / 16.f, 1.f}; break;
    case REPEATER_OFF: case REPEATER_ON: mx = {1.f, 2 / 16.f, 1.f}; break;
    case SIGN_POST: mn = {0.25f, 0.f, 0.25f}; mx = {0.75f, 1.f, 0.75f}; break;
    case REDSTONE_TORCH_ON: case REDSTONE_TORCH_OFF: blockBounds(TORCH, meta, x, z, mn, mx); break;
    case WALL_SIGN: case STONE_BUTTON: case LEVER: {
        int s = supportDir(b, meta);
        if (b == LEVER && s == 3) { mn = {0.25f, 0.f, 0.25f}; mx = {0.75f, 0.6f, 0.75f}; break; }
        float t = b == WALL_SIGN ? 2 / 16.f : b == STONE_BUTTON ? 2 / 16.f : 6 / 16.f;
        float y0 = b == WALL_SIGN ? 0.28f : b == STONE_BUTTON ? 6 / 16.f : 0.2f, y1 = b == WALL_SIGN ? 0.78f : b == STONE_BUTTON ? 10 / 16.f : 0.8f;
        float a0 = b == STONE_BUTTON ? 5 / 16.f : b == LEVER ? 0.3f : 0.f, a1 = 1.f - a0;
        mn.y = y0; mx.y = y1;
        if (s == 0) { mn.x = 1.f - t; mn.z = a0; mx.z = a1; }
        else if (s == 1) { mx.x = t; mn.z = a0; mx.z = a1; }
        else if (s == 4) { mn.z = 1.f - t; mn.x = a0; mx.x = a1; }
        else { mx.z = t; mn.x = a0; mx.x = a1; }
        break;
    }
    case CAKE: mn = {(1 + 2 * (meta & 7)) / 16.f, 0.f, 1 / 16.f}; mx = {15 / 16.f, 0.5f, 15 / 16.f}; break;
    case LADDER: case TRAPDOOR: case WOOD_DOOR: case IRON_DOOR: {
        float t = b == LADDER ? 2 / 16.f : 3 / 16.f;
        int s = b == LADDER ? meta : isDoor(b) ? ((meta & 4) ? (meta + 3) & 3 : (meta + 2) & 3) : (meta & 3);
        if (b == TRAPDOOR && !(meta & 4)) { mx = {1.f, t, 1.f}; break; }
        switch (s & 3) {
        case 0: mn.x = 1.f - t; break;
        case 1: mn.z = 1.f - t; break;
        case 2: mx.x = t; break;
        default: mx.z = t; break;
        }
        break;
    }
    case FENCE_GATE:
        if ((meta & 1) == 0) { mn.x = 0.375f; mx.x = 0.625f; }
        else { mn.z = 0.375f; mx.z = 0.625f; }
        break;
    // 1.4.2: сундуки ниже и уже куба, наковальня — по повороту, горшок и голова — маленькие
    case CHEST: case ENDER_CHEST: mn = {1 / 16.f, 0.f, 1 / 16.f}; mx = {15 / 16.f, 14 / 16.f, 15 / 16.f}; break;
    case ANVIL:
        if ((meta & 1) == 0) { mn.x = 0.125f; mx.x = 0.875f; }
        else { mn.z = 0.125f; mx.z = 0.875f; }
        break;
    case FLOWER_POT: mn = {5 / 16.f, 0.f, 5 / 16.f}; mx = {11 / 16.f, 6 / 16.f, 11 / 16.f}; break;
    case SKULL_BLOCK:
        if (meta & 8) {
            // На стене: полблока от стены, по высоте 0.25..0.75 (BlockSkull.setBlockBoundsBasedOnState)
            mn = {0.25f, 0.25f, 0.25f}; mx = {0.75f, 0.75f, 0.75f};
            switch ((meta >> 4) & 3) {
            case 0: mn.x = 0.f; mx.x = 0.5f; break; // смотрит в +X, стена с -X
            case 1: mn.z = 0.f; mx.z = 0.5f; break;
            case 2: mn.x = 0.5f; mx.x = 1.f; break;
            default: mn.z = 0.5f; mx.z = 1.f; break;
            }
        } else {
            mn = {0.25f, 0.f, 0.25f}; mx = {0.75f, 0.5f, 0.75f};
        }
        break;
    default: break;
    }
    if (hasPlantOffset(b)) {
        glm::vec3 o = plantOffset(b, x, z);
        mn += o;
        mx += o;
    }
}

// Модель блока в кубе [0,1]^3 (для руки игрока и выпавших предметов)
void appendBlockModel(std::vector<Vertex>& out, uint8_t b, float sky = 1.f, float block = 0.f, uint8_t meta = 3);

struct Gen10; // генератор 1.0 (WorldGen10.cpp)

class World {
public:
    // dimension: 0 — обычный мир, -1 — Незер, 1 — Край
    explicit World(uint32_t seed, int dimension = 0);
    int dimension() const { return dimension_; }
    // Версия генератора обычного мира: 1 — прежний (старые миры), 2 — как в Minecraft 1.0.
    // Задаётся до генерации первого чанка; мир хранит только правки поверх генерации
    void setGenVersion(int v) { genVersion_ = v; }
    // Большой дуб (WorldGenBigTree 1.0) высотой heightLimit; крона не выходит за чанк. false — не поместился
    bool bigTreeInChunk(Chunk& c, int x, int y, int z, int heightLimit, uint32_t& rng);
    int genVersion() const { return genVersion_; } // 1 — старый, 2 — как в 1.0, 3 — + родники и большие дубы
    // Портал: огонь внутри обсидиановой рамки 4x5 зажигает его
    bool tryCreatePortal(int x, int y, int z);
    // Центры трёх крепостей (строгхолдов) — туда летит око Края
    std::vector<glm::ivec3> strongholdCenters() const;
    // Ближайшее строение (для /locate): village, stronghold, mineshaft, fortress, dungeon. Считается по сиду теми же
    // правилами, что и генерация (данжи — только в загруженных чанках). false — рядом нет / не в этом измерении
    bool locateStructure(const std::string& kind, const glm::vec3& from, glm::ivec3& out) const;
    // Вставлено око: если кольцо из 12 рамок заполнено — открыть портал в Край
    bool tryOpenEndPortal(int x, int y, int z);
    // Столбы Края: верхушки (там стоят кристаллы)
    std::vector<glm::ivec3> endPillarTops() const;
    bool dragonDefeated = false;
    // Жители деревень, найденные при генерации (появляются один раз, когда чанк впервые заселяется)
    std::vector<glm::vec4> villagerSpawns; // xyz — место, w — профессия (-1 — случайная)
    // Центр ближайшей деревни (для отладки/витрины); false — рядом нет
    bool findVillage(int nearX, int nearZ, glm::ivec3& out) const;

    uint8_t getBlock(int x, int y, int z) const;
    uint8_t getMeta(int x, int y, int z) const;
    void setBlock(int x, int y, int z, uint8_t b, uint8_t meta = 0);

    int getSkyLight(int x, int y, int z) const;
    int getBlockLight(int x, int y, int z) const;

    bool isChunkLoaded(int cx, int cz) const { return chunks.count(chunkKey(cx, cz)) != 0; }
    Chunk* chunkAt(int cx, int cz) const;

    // ---- Биомы и рельеф
    Biome biomeAt(int x, int z) const;        // по шуму (работает и вне загруженных чанков)
    Biome loadedBiome(int x, int z) const;    // из чанка, если загружен
    int terrainHeight(int x, int z) const;    // высота поверхности без пещер и деревьев
    int topBlockY(int x, int z) const;        // верхний непрозрачный или жидкий блок (для дождя)
    // Точка появления на суше рядом с p (только загруженные чанки): ноги над травой/песком/землёй, сверху два
    // свободных блока без воды. keepIfSafe — оставить p, если там уже можно стоять (старые миры, кровать)
    glm::vec3 safeSpawnNear(const glm::vec3& p, int radius, bool keepIfSafe) const;

    // Подгрузка/выгрузка чанков вокруг игрока и перестройка мешей
    void update(const glm::vec3& center, int maxGen, int maxMesh);

    struct SaveState {
        glm::vec3 pos{0.f};
        glm::vec3 spawn{0.f};
        float yaw = -90.f, pitch = 0.f;
        int64_t worldTime = 0;       // тики (24000 на сутки)
        int health = 20, food = 20, air = 300;
        float saturation = 5.f;
        uint8_t gameMode = 0;        // 0 — выживание, 1 — творческий
        Inventory inventory;
        // Погода, как в 1.0: таймеры дождя и грозы
        uint8_t raining = 0, thundering = 0;
        int rainTime = 0, thunderTime = 0;
    };
    bool save(const std::string& path, const SaveState& st) const;
    bool load(const std::string& path, SaveState& st); // при сбое — из path + ".bak"
    bool loadFrom(const std::string& path, SaveState& st);

    uint32_t seed() const { return seed_; }

    // ---- Блоки с содержимым (сундуки, печи)
    TileEntity* tileAt(int x, int y, int z);
    TileEntity& createTile(int x, int y, int z, TileEntity::Type type);
    void removeTile(int x, int y, int z);
    std::unordered_map<int64_t, TileEntity> tiles;
    // Двойной сундук: true, если рядом по X или Z стоит второй сундук. first — половина с меньшей координатой
    // (верхние 27 слотов окна, как InventoryLargeChest в 1.0), second — другая
    bool chestPair(int x, int y, int z, glm::ivec3& first, glm::ivec3& second) const;
    // Сундук не открывается, если над ним (или над его парой) непрозрачный блок
    bool chestBlocked(int x, int y, int z) const;
    // Можно ли поставить сундук: не больше одного соседа-сундука, и тот ещё не двойной
    bool canPlaceChest(int x, int y, int z) const;

    // ---- Случайные тики вокруг игрока: рост растений, трава, грядки, опадание листвы, снег.
    // precipitation — идёт дождь/снег (в холодных биомах копится снег и мёрзнет вода)
    void randomTick(const glm::vec3& center, uint32_t& rng, bool precipitation);
    // Дерево из саженца (type: 0 дуб, 1 ель, 2 берёза); false, если не хватает места
    bool growTree(int x, int y, int z, uint32_t& rng, int type = 0);

    // ---- Запланированные обновления (течение жидкостей)
    void scheduleUpdate(int x, int y, int z, int delay);
    void tickUpdates(int64_t now, int budget);
    // Направление течения в клетке (для сноса игрока, мобов и предметов)
    glm::vec3 flowVector(int x, int y, int z) const;

    std::unordered_map<int64_t, std::unique_ptr<Chunk>> chunks;
    int renderDistance = 8;
    // Плавное освещение с затенением углов (F8). По умолчанию — «квадратное», как в 1.0 без сглаживания
    bool smoothLighting = false;
    // Fancy — прозрачная листва; Fast — сплошная, с «быстрым» тайлом (как в 1.0)
    bool fancyGraphics = true;
    // Блоки, сорванные из-за потери опоры (факелы, цветы) — игра забирает их для звука/частиц/дропа
    struct Popped { glm::ivec3 pos; uint8_t block; uint8_t meta = 0; };
    std::vector<Popped> popped;
    // Звуки от мира (шипение лавы и т.п.)
    std::vector<std::pair<glm::vec3, const char*>> soundEvents;
    // Чанки, сгенерированные с прошлого опроса (для спавна животных)
    std::vector<std::pair<int, int>> generated;
    // Идёт дождь (гасит огонь под открытым небом)
    bool raining = false;
    // Динамит, подожжённый миром (огнём): позиция и «от взрыва» (короткий фитиль)
    std::vector<std::pair<glm::ivec3, bool>> ignitedTnt;
    // Песок/гравий сорвались (BlockSand.tryToFall): клетка уже воздух, сущность создаёт MobManager
    std::vector<std::pair<glm::ivec3, uint8_t>> fallingStarts;
    // Можно ли поставить огонь (опора снизу или горючее рядом)
    bool canPlaceFire(int x, int y, int z) const;
    // Прыжок/падение на грядку: с шансом (высота - 0.5) она становится землёй, посев срывается (BlockFarmland 1.0)
    void trampleFarmland(int x, int y, int z, float fallDistance, float roll);

    // ---- Редстоун (Redstone.cpp)
    void setMeta(int x, int y, int z, uint8_t m);         // без пересчёта света
    void updateRailShape(int x, int y, int z, bool updateNeighbors); // повернуть рельсы к соседям (Vehicle.cpp)
    void redstoneChanged(int x, int y, int z);            // источник/провод изменился — пересчитать сеть вокруг
    int inputPower(int x, int y, int z) const;            // питание механизма в клетке (0..15)
    bool wireConnects(int x, int y, int z, int d) const;  // пыль соединена с соседом по направлению d
    std::vector<glm::ivec3> noteEvents;                   // сработавшие нотные блоки (звук играет игра)
    // Раздатчик выбросил предмет: позиция, направление (индекс DIRS), предмет
    struct DispenseEvent { glm::ivec3 pos; int dir; ItemStack item; };
    std::vector<DispenseEvent> dispenseEvents;

    // Тексты табличек (4 строки) и точки появления: мира и у кровати
    std::unordered_map<int64_t, std::array<std::string, 4>> signs;
    glm::vec3 worldSpawn{0.f};
    bool hasBedSpawn = false;
    glm::ivec3 bedSpawn{0};
    // ---- Мультиплеер
    // Клиент сетевой игры: мир не живёт сам (тики выключены), правки рисуются сразу и уходят на сервер
    bool remote = false;
    std::function<void(int, int, int, uint8_t, uint8_t, bool)> sendEdit; // x, y, z, блок, мета, только мета
    // Сервер: каждое изменение блока (для рассылки игрокам)
    std::function<void(int, int, int, uint8_t, uint8_t)> onChange;
    // Применить правку, пришедшую с сервера: без логики (опоры, пары, редстоун уже посчитал сервер)
    void applyRemote(int x, int y, int z, uint8_t b, uint8_t meta);
    // Правки чанка (индекс в чанке -> блок | мета << 8): выдать для отправки и применить полученные
    std::vector<std::pair<uint16_t, uint16_t>> chunkEdits(int cx, int cz) const;
    void applyChunkEdits(int cx, int cz, const std::vector<std::pair<uint16_t, uint16_t>>& e);
    // Сервер: держать загруженными чанки вокруг всех игроков (без мешей)
    void updateMulti(const std::vector<glm::vec3>& centers, int radius, int maxGen);

    // Столы зачарования (над ними рисуется книга); позиции posKey, лишние отсеиваются при отрисовке
    std::unordered_set<int64_t> enchantTables;
    // Спавнеры в данжах, найденные при генерации: позиция и вид моба (метаданные)
    std::vector<std::pair<glm::ivec3, uint8_t>> newSpawners;

private:
    uint32_t seed_;
    Perlin noise_;
    int dimension_ = 0;
    int genVersion_ = 1;
    mutable std::shared_ptr<Gen10> gen10_;
    mutable std::unordered_map<int64_t, std::array<uint8_t, CW * CW>> heightCache_; // высоты рельефа 1.0 по чанкам
    Gen10& gen10() const;
    Biome biomeAt10(int x, int z) const;
    int terrainHeight10(int x, int z) const;
    void generateOverworld10(Chunk& c);
    bool breakingPortal_ = false;
    void generateOverworld(Chunk& c);
    void generateNether(Chunk& c);
    void generateEnd(Chunk& c);
    void placeFortress(Chunk& c);
    void placeStronghold(Chunk& c);
    void placeVillage(Chunk& c);
    void placeVillage10(Chunk& c);           // деревни как в 1.0 (генератор 3+)
    const void* villageLayout10(int X, int Z) const;
    void placeStronghold10(Chunk& c);         // крепость Края как в 1.0 (генератор 3+)
    const void* strongholdLayout10(int X, int Z) const;
public:
    bool locateStronghold10(const glm::vec3& from, glm::ivec3& out) const;
    bool fortressCenter10(int rx, int rz, int& X, int& Z) const;
    bool locateFortress10(const glm::vec3& from, glm::ivec3& out) const;
private:
    void placeFortress10(Chunk& c);           // адская крепость как в 1.0 (генератор 3+)
    const void* fortressLayout10(int X, int Z) const;
public:
    bool villageCenter10(int rx, int rz, int& X, int& Z) const;
    bool locateVillage10(const glm::vec3& from, glm::ivec3& out) const;
private:
    // Изменения игрока (блок | мета << 8), переживают выгрузку чанков и сохраняются на диск
    std::unordered_map<int64_t, std::unordered_map<int, uint16_t>> edits_;

    // Кэш последнего чанка — ускоряет обход соседей при расчёте света
    mutable Chunk* cacheChunk_ = nullptr;
    mutable int64_t cacheKey_ = 0;

    // Очередь обновлений: время -> позиция
    std::multimap<int64_t, glm::ivec3> updates_;
    std::unordered_set<int64_t> pending_;
    int64_t now_ = 0;
    bool loadingEdits_ = false;

    struct LightNode { int x, y, z; };
    struct RemoveNode { int x, y, z, level; };

    // Генерация (WorldGen.cpp)
    struct ColumnShape { float base, variation; };
    ColumnShape biomeShape(int x, int z) const;
    void generate(Chunk& c);
    void decorate(Chunk& c, const int heights[CW][CW]);
    void placeDungeons(Chunk& c);
    void placeMineshafts(Chunk& c);
    void treeInChunk(Chunk& c, int x, int y, int z, int type, uint32_t& rng);

    void initLighting(Chunk& c);
    void buildMesh(Chunk& c); // Mesher.cpp

    // Редстоун
    mutable bool wiresPower_ = true;
    int inRedstone_ = 0;
    std::unordered_set<int64_t> poweredComps_; // запитанные механизмы (реагируют на фронт сигнала)
    int weakPower(int x, int y, int z, int d) const;
    int strongPower(int x, int y, int z, int d) const;
    int blockPowerInto(int x, int y, int z) const;
    int wireTarget(int x, int y, int z) const;
    void updateWires(const std::vector<glm::ivec3>& start);
    void updateComponentsNear(const glm::ivec3& c);
    void updateComponent(int x, int y, int z);
    void redstoneTick(int x, int y, int z);
    // Энергорельсы: питание передаётся по прямой цепочке энергорельс до 8 штук (BlockRail 1.0)
    std::vector<glm::ivec3> poweredRailLine(int x, int y, int z) const;
    void updatePoweredRails(int x, int y, int z);
    // Поршни и раздатчик (Piston.cpp)
    bool pistonPowered(int x, int y, int z, int facing) const;
    bool pistonExtend(int x, int y, int z);
    void pistonRetract(int x, int y, int z);
    void dispense(int x, int y, int z);

    // Огонь (Fire.cpp)
    uint32_t fireRng_ = 0x9E3779B9u;
    int fireRand(int n);
    bool rainsAt(int x, int y, int z) const;
    bool flammableNear(int x, int y, int z) const;
    void tryCatchFire(int x, int y, int z, int chance, int age);
    void updateFire(int x, int y, int z);

    // Жидкости (Liquid.cpp)
    void notifyNeighbors(int x, int y, int z);
    void updateLiquid(int x, int y, int z);
    bool liquidBlocks(int x, int y, int z, uint8_t liquid) const;
    int flowCost(int x, int y, int z, int depth, int fromDir, uint8_t liquid) const;
    void flowInto(int x, int y, int z, uint8_t liquid, uint8_t meta);

    uint8_t* lightCell(int x, int y, int z) const;
    void markDirtyAt(int x, int z);
    void spreadLight(std::deque<LightNode>& q, bool sky);
    void removeLight(std::deque<RemoveNode>& rq, std::deque<LightNode>& aq, bool sky);
    void relight(int x, int y, int z);
};
