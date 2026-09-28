// Проверки логики мира без окна: вода и редстоун, энергорельсы, грядки, лава, сохранение мира и предметов (в т.ч. по
// русскому пути), прореживание монстров. Запуск: build\Release\worldtest.exe [папка для временных файлов]
#include <cstdio>
#include <filesystem>
#include <string>
#include <cstring>
#include "../src/Map.h"
#include "../src/Mob.h"
#include "../src/Saves.h"
#include "../src/World.h"

static int failures = 0;
#define CHECK(cond)                                                                                  \
    do {                                                                                             \
        if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; }     \
    } while (0)

namespace {
// Площадка из камня на высоте y (воздух над ней), чтобы сценарии не зависели от рельефа
void platform(World& w, int x0, int z0, int x1, int z1, int y) {
    for (int x = x0; x <= x1; ++x)
        for (int z = z0; z <= z1; ++z) {
            w.setBlock(x, y, z, STONE);
            for (int k = 1; k <= 4; ++k) w.setBlock(x, y + k, z, AIR);
        }
}
void runTicks(World& w, int64_t& t, int n) {
    for (int i = 0; i < n; ++i) w.tickUpdates(++t, 4000);
}
} // namespace

int main(int argc, char** argv) {
    std::string tmp = argc > 1 ? argv[1] : (std::filesystem::temp_directory_path() / "minicraft_worldtest").u8string();
    std::filesystem::create_directories(std::filesystem::u8path(tmp));
    if (tmp.back() != '/' && tmp.back() != '\\') tmp += '/';

    World w(12345, 0);
    w.setGenVersion(2);
    w.updateMulti({glm::vec3(8, 100, 8)}, 2, 1000);
    int64_t t = 0;
    const int Y = 110;

    // --- Вода смывает редстоун-пыль и рычаг, но не проходит сквозь тростник
    {
        // Площадка шире поиска спуска (4 блока), чтобы вода текла во все стороны, а не к краю
        platform(w, -6, -6, 18, 18, Y);
        w.setBlock(7, Y + 1, 6, REDSTONE_WIRE);
        w.setBlock(5, Y + 1, 6, REEDS);
        w.setBlock(6, Y + 1, 6, WATER, 0);
        runTicks(w, t, 40);
        CHECK(w.getBlock(7, Y + 1, 6) == WATER); // пыль смыта
        CHECK(w.getBlock(5, Y + 1, 6) == REEDS); // тростник держит воду
        w.popped.clear();
    }

    // --- Энергорельсы: рычаг у одного конца питает цепочку до 8 рельс дальше
    {
        platform(w, 20, 0, 40, 2, Y);
        for (int x = 21; x <= 35; ++x) { w.setBlock(x, Y + 1, 1, POWERED_RAIL, 1); }
        for (int x = 21; x <= 35; ++x) w.updateRailShape(x, Y + 1, 1, false);
        w.setBlock(20, Y + 1, 1, LEVER, 5 | 8); // включённый рычаг на полу рядом с первой рельсой
        w.redstoneChanged(20, Y + 1, 1);
        CHECK((w.getMeta(21, Y + 1, 1) & 8) != 0);
        CHECK((w.getMeta(29, Y + 1, 1) & 8) != 0);  // 8 от запитанной
        CHECK((w.getMeta(30, Y + 1, 1) & 8) == 0);  // дальше — нет
        w.setMeta(20, Y + 1, 1, 5);                 // выключили
        w.redstoneChanged(20, Y + 1, 1);
        CHECK((w.getMeta(25, Y + 1, 1) & 8) == 0);
    }

    // --- Грядка: прыжок вытаптывает, посев срывается с метой
    {
        platform(w, 0, 10, 3, 12, Y);
        w.setBlock(1, Y, 11, FARMLAND, 7);
        w.setBlock(1, Y + 1, 11, WHEAT, 7);
        w.popped.clear();
        w.trampleFarmland(1, Y, 11, 0.4f, 0.0f); // слишком низко — ничего
        CHECK(w.getBlock(1, Y, 11) == FARMLAND);
        w.trampleFarmland(1, Y, 11, 1.25f, 0.5f);
        CHECK(w.getBlock(1, Y, 11) == DIRT);
        CHECK(w.getBlock(1, Y + 1, 11) == AIR);
        bool wheatPopped = false;
        for (auto& pp : w.popped) wheatPopped |= pp.block == WHEAT && pp.meta == 7;
        CHECK(wheatPopped);
        w.popped.clear();
    }

    // --- Безопасная точка появления: не в воде
    {
        glm::vec3 p0(8.5f, 70.f, 8.5f);
        glm::vec3 s = w.safeSpawnNear(p0, 30, false);
        if (s != p0) {
            uint8_t below = w.getBlock((int)std::floor(s.x), (int)std::floor(s.y) - 1, (int)std::floor(s.z));
            uint8_t at = w.getBlock((int)std::floor(s.x), (int)std::floor(s.y), (int)std::floor(s.z));
            CHECK(!isLiquid(at) && !isSolid(at));
            CHECK(below == GRASS || below == SAND || below == DIRT || below == SNOW || below == MYCELIUM);
        } else {
            std::printf("note: no dry land near test spawn (ocean seed)\n");
        }
    }

    // --- Сохранение мира по русскому пути, запасная копия .bak
    {
        std::string dir = tmp + u8"Мир проверка/";
        std::filesystem::create_directories(std::filesystem::u8path(dir));
        World::SaveState st;
        st.health = 17;
        w.setBlock(5, Y + 3, 5, GOLD_BLOCK);
        CHECK(w.save(dir + "world.sav", st));
        CHECK(w.save(dir + "world.sav", st)); // второй раз — появляется .bak
        CHECK(fileExistsUtf8(dir + "world.sav.bak"));
        World w2(12345, 0);
        w2.setGenVersion(2);
        World::SaveState st2;
        CHECK(w2.load(dir + "world.sav", st2));
        CHECK(st2.health == 17);
        // Основной файл испорчен — грузится .bak
        FILE* f = openFileUtf8(dir + "world.sav", "wb");
        std::fputs("garbage", f);
        std::fclose(f);
        World w3(12345, 0);
        World::SaveState st3;
        CHECK(w3.load(dir + "world.sav", st3));
        CHECK(st3.health == 17);
    }

    // --- Выпавшие предметы сохраняются; тысячи эндерменов при загрузке прореживаются
    {
        std::string dir = tmp + u8"Сущности/";
        std::filesystem::create_directories(std::filesystem::u8path(dir));
        MobManager mm;
        for (int i = 0; i < 500; ++i) mm.spawn(MobType::Enderman, glm::vec3((float)(i % 50) * 3.f, 70.f, (float)(i / 50) * 3.f), 0.f);
        mm.spawn(MobType::Pig, glm::vec3(1, 70, 1), 0.f);
        std::vector<ItemEntity> items(3);
        for (int i = 0; i < 3; ++i) {
            items[i].pos = items[i].prev = glm::vec3((float)i, 70.f, 0.f);
            items[i].motion = glm::vec3(0.f);
            items[i].stack = makeStack(DIAMOND, i + 1);
        }
        Player p;
        p.pos = glm::vec3(0, 70, 0);
        CHECK(mm.save(dir + "entities.sav", p, &items));
        MobManager mm2;
        std::vector<ItemEntity> back;
        CHECK(mm2.load(dir + "entities.sav", p, &back));
        CHECK(back.size() == 3 && back[2].stack.id == DIAMOND && back[2].stack.count == 3);
        int endermen = 0, pigs = 0;
        for (auto& m : mm2.mobs) { endermen += m.type == MobType::Enderman; pigs += m.type == MobType::Pig; }
        CHECK(endermen <= 50);
        CHECK(pigs == 1);
    }

    // --- Оборванный entities.sav: берётся .bak, и прочитанное из битого файла не удваивает мобов
    {
        std::string dir = tmp + "broken_entities/";
        std::filesystem::create_directories(std::filesystem::u8path(dir));
        MobManager mm;
        for (int i = 0; i < 10; ++i) mm.spawn(MobType::Cow, glm::vec3((float)i * 2.f, 70.f, 0.f), 0.f);
        Player p;
        p.pos = glm::vec3(0, 70, 0);
        std::vector<ItemEntity> items;
        CHECK(mm.save(dir + "entities.sav", p, &items)); // первое сохранение
        CHECK(mm.save(dir + "entities.sav", p, &items)); // второе: прежнее стало .bak
        std::filesystem::resize_file(std::filesystem::u8path(dir + "entities.sav"),
                                     std::filesystem::file_size(std::filesystem::u8path(dir + "entities.sav")) / 2);
        MobManager mm2;
        std::vector<ItemEntity> back;
        CHECK(mm2.load(dir + "entities.sav", p, &back));
        CHECK(mm2.mobs.size() == 10);
    }

    // --- Лава рядом с досками рано или поздно поджигает (случайные тики)
    {
        platform(w, 0, 20, 6, 26, Y);
        w.setBlock(3, Y + 1, 23, LAVA, 0);
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz) w.setBlock(3 + dx, Y + 3, 23 + dz, PLANKS); // навес из досок над лавой
        uint32_t rng = 777;
        bool fire = false;
        for (int i = 0; i < 20000 && !fire; ++i) {
            w.randomTick(glm::vec3(3, Y, 23), rng, false);
            for (int dx = -2; dx <= 2 && !fire; ++dx)
                for (int dy = 1; dy <= 3 && !fire; ++dy)
                    for (int dz = -2; dz <= 2 && !fire; ++dz) fire = w.getBlock(3 + dx, Y + dy, 23 + dz) == FIRE;
        }
        CHECK(fire);
    }

    // --- Двойной сундук: пара, порядок половин, запрет третьего, крышку не поднять под камнем
    {
        platform(w, 0, 30, 6, 34, Y);
        w.setBlock(2, Y + 1, 32, CHEST, 3);
        glm::ivec3 a, b;
        CHECK(!w.chestPair(2, Y + 1, 32, a, b));
        CHECK(w.canPlaceChest(3, Y + 1, 32));
        w.setBlock(3, Y + 1, 32, CHEST, 3);
        CHECK(w.chestPair(3, Y + 1, 32, a, b));
        CHECK(a == glm::ivec3(2, Y + 1, 32) && b == glm::ivec3(3, Y + 1, 32));
        CHECK(!w.canPlaceChest(4, Y + 1, 32)); // рядом уже двойной
        CHECK(!w.canPlaceChest(2, Y + 1, 33)); // тоже сосед двойного
        CHECK(w.canPlaceChest(5, Y + 1, 32));
        CHECK(!w.chestBlocked(2, Y + 1, 32));
        w.setBlock(3, Y + 2, 32, STONE);
        CHECK(w.chestBlocked(2, Y + 1, 32)); // камень над второй половиной
        w.setBlock(3, Y + 2, 32, GLASS);
        CHECK(!w.chestBlocked(2, Y + 1, 32)); // стекло не мешает
    }

    // --- Шары опыта: разбиение как в 1.0, летят к игроку и дают весь опыт
    {
        CHECK(xpSplit(20000) == 2477 && xpSplit(5) == 3 && xpSplit(2) == 1);
        CHECK(xpOrbIcon(1) == 0 && xpOrbIcon(3) == 1 && xpOrbIcon(2477) == 10);
        platform(w, 40, 40, 52, 44, Y);
        MobManager mm;
        uint32_t rng = 99;
        spawnXpOrbs(mm.orbs, glm::vec3(41.5f, Y + 1.5f, 42.5f), 50, rng);
        int sum = 0;
        for (auto& o : mm.orbs) sum += o.value;
        CHECK(sum == 50 && mm.orbs.size() > 1);
        Player p;
        p.pos = glm::vec3(46.5f, (float)Y + 1, 42.5f); // 5 блоков — притянет
        int got = 0;
        MobHooks h;
        h.sound = [](const std::string&, float, float, const glm::vec3*) {};
        h.addXp = [&](int n) { got += n; };
        for (int i = 0; i < 400 && !mm.orbs.empty(); ++i) mm.tickOrbs(w, p, h, rng);
        CHECK(mm.orbs.empty());
        CHECK(got == 50);
        // Далеко (20 блоков) — не летит
        spawnXpOrbs(mm.orbs, glm::vec3(41.5f, Y + 1.5f, 42.5f), 5, rng);
        p.pos = glm::vec3(61.5f, (float)Y + 1, 42.5f);
        for (int i = 0; i < 200; ++i) mm.tickOrbs(w, p, h, rng);
        CHECK(!mm.orbs.empty() && mm.orbs[0].pos.x < 46.f); // разлетелись при появлении, но к игроку не тянутся
    }

    // --- Картины: висят только на стене, сюжет по месту, не налезают друг на друга, сохраняются
    {
        platform(w, -30, -30, -20, -26, Y);
        for (int x = -30; x <= -20; ++x)
            for (int y = Y + 1; y <= Y + 5; ++y) w.setBlock(x, y, -30, STONE); // стена 11x5, лицом к +Z
        MobManager mm;
        uint32_t rng = 5;
        CHECK(!placePainting(mm.paintings, w, glm::ivec3(-28, Y + 2, -30), glm::ivec3(0, 1, 0), rng)); // на пол/потолок нельзя
        CHECK(placePainting(mm.paintings, w, glm::ivec3(-28, Y + 2, -30), glm::ivec3(0, 0, 1), rng));
        CHECK(mm.paintings.size() == 1 && mm.paintings[0].dir == 2);
        CHECK(!placePainting(mm.paintings, w, glm::ivec3(-28, Y + 2, -30), glm::ivec3(0, 0, 1), rng)); // то же место занято
        // Одиночный блок: только 16x16
        w.setBlock(-24, Y + 3, -28, STONE);
        for (int i = 0; i < 10; ++i) {
            std::vector<Painting> one;
            if (!placePainting(one, w, glm::ivec3(-24, Y + 3, -28), glm::ivec3(1, 0, 0), rng)) { CHECK(false); break; }
            CHECK(PAINTING_ARTS[one[0].art].w == 16 && PAINTING_ARTS[one[0].art].h == 16);
        }
        std::string dir = tmp + u8"Картины/";
        std::filesystem::create_directories(std::filesystem::u8path(dir));
        Player p;
        p.pos = glm::vec3(-28, Y + 1, -27);
        CHECK(mm.save(dir + "entities.sav", p, nullptr));
        MobManager m2;
        std::vector<ItemEntity> its;
        CHECK(m2.load(dir + "entities.sav", p, &its));
        CHECK(m2.paintings.size() == 1 && m2.paintings[0].art == mm.paintings[0].art && m2.paintings[0].wall == mm.paintings[0].wall);
        // Стену убрали — картина падает предметом
        for (int x = -30; x <= -20; ++x)
            for (int y = Y + 1; y <= Y + 5; ++y) w.setBlock(x, y, -30, AIR);
        m2.checkPaintings(w, its, rng);
        CHECK(m2.paintings.empty());
        bool dropped = false;
        for (auto& e : its) dropped |= e.stack.id == PAINTING;
        CHECK(dropped);
    }

    // --- Рамки для предметов (1.4.2): только на стену, предмет и поворот сохраняются, без стены падают вместе с предметом
    {
        platform(w, -20, -32, -16, -28, Y);
        w.setBlock(-18, Y + 2, -32, STONE);
        MobManager mm;
        CHECK(!placeItemFrame(mm.paintings, w, glm::ivec3(-18, Y + 2, -32), glm::ivec3(0, 1, 0)));
        CHECK(!placeItemFrame(mm.paintings, w, glm::ivec3(-18, Y + 3, -32), glm::ivec3(0, 0, 1))); // в воздухе
        CHECK(placeItemFrame(mm.paintings, w, glm::ivec3(-18, Y + 2, -32), glm::ivec3(0, 0, 1)));
        CHECK(!placeItemFrame(mm.paintings, w, glm::ivec3(-18, Y + 2, -32), glm::ivec3(0, 0, 1))); // место занято
        if (mm.paintings.empty()) return 1;
        mm.paintings[0].item = makeStack(DIAMOND_SWORD);
        mm.paintings[0].rotation = 3;
        std::string dir = tmp + "frames/";
        std::filesystem::create_directories(std::filesystem::u8path(dir));
        Player p;
        p.pos = glm::vec3(-18, Y + 1, -30);
        p.enderChest.items[5] = makeStack(DIAMOND, 7); // эндер-сундук игрока сохраняется там же (хвост EC01)
        CHECK(mm.save(dir + "entities.sav", p, nullptr));
        MobManager m2;
        std::vector<ItemEntity> its;
        Player p2;
        CHECK(m2.load(dir + "entities.sav", p2, &its));
        CHECK(p2.enderChest.items[5].id == DIAMOND && p2.enderChest.items[5].count == 7 && p2.enderChest.items[0].empty());
        CHECK(m2.paintings.size() == 1 && m2.paintings[0].frame && m2.paintings[0].item.id == DIAMOND_SWORD &&
              m2.paintings[0].rotation == 3 && m2.paintings[0].dir == 2);
        uint32_t rng = 9;
        w.setBlock(-18, Y + 2, -32, AIR);
        m2.checkPaintings(w, its, rng);
        CHECK(m2.paintings.empty());
        bool frameDrop = false, swordDrop = false;
        for (auto& e : its) { frameDrop |= e.stack.id == ITEM_FRAME_ITEM; swordDrop |= e.stack.id == DIAMOND_SWORD; }
        CHECK(frameDrop && swordDrop);
    }

    // --- Карта: рисуется вокруг игрока, вода синяя, сохраняется
    {
        MapData md;
        md.xCenter = 8; md.zCenter = 8;
        for (int x = 0; x < 8; ++x)
            for (int z = 0; z < 8; ++z) w.setBlock(-16 + x, Y + 5, -16 + z, WATER, 0); // «озеро» на высоте
        for (int i = 0; i < 32; ++i) updateMap(md, w, glm::vec3(8, 70, 8), 0, false);
        int painted = 0;
        for (uint8_t c : md.colors) painted += c != 0;
        CHECK(painted > 40); // загружено только 5x5 чанков (~10x10 точек)
        uint8_t lake = md.colors[(64 + (-16 - 8) / 8) + (64 + (-16 - 8) / 8) * 128];
        CHECK(lake / 4 == blockMapColor(WATER));
        CHECK(md.dirty);
        std::string mp = tmp + u8"Карта.dat";
        CHECK(saveMap(md, mp));
        MapData m2;
        CHECK(loadMap(m2, mp));
        CHECK(m2.xCenter == 8 && std::memcmp(m2.colors, md.colors, sizeof md.colors) == 0);
        MapData other;
        other.dim = -1;
        updateMap(other, w, glm::vec3(8, 70, 8), 0, false); // карта Незера в обычном мире не рисуется
        int any = 0;
        for (uint8_t c : other.colors) any += c != 0;
        CHECK(any == 0);
    }

    // --- Генератор v3: родники в стенах пещер и большие дубы с ветками; v2 не меняется
    {
        auto features = [](int gen, int& springs, int& branches) {
            springs = branches = 0;
            // Сиды, где у начала координат лес (там и растут большие дубы)
            std::vector<uint32_t> seeds;
            for (uint32_t sd = 1; sd < 400 && seeds.size() < 3; ++sd) {
                World probe(sd, 0);
                probe.setGenVersion(2);
                if (probe.biomeAt(0, 0) == Biome::Forest) seeds.push_back(sd);
            }
            for (uint32_t sd : seeds) {
                World g(sd, 0);
                g.setGenVersion(gen);
                g.updateMulti({glm::vec3(0, 80, 0)}, 4, 100000);
                for (int x = -60; x < 60; ++x)
                    for (int z = -60; z < 60; ++z)
                        for (int y = 2; y < CH - 2; ++y) {
                            uint8_t b = g.getBlock(x, y, z);
                            if ((b == WATER || b == LAVA) && g.getMeta(x, y, z) == 0 && g.getBlock(x, y + 1, z) == STONE &&
                                g.getBlock(x, y - 1, z) == STONE) {
                                int st = (g.getBlock(x + 1, y, z) == STONE) + (g.getBlock(x - 1, y, z) == STONE) +
                                         (g.getBlock(x, y, z + 1) == STONE) + (g.getBlock(x, y, z - 1) == STONE);
                                springs += st == 3;
                            }
                            // Ветка: бревно дуба, под которым не бревно и не земля
                            if (b == LOG && g.getMeta(x, y, z) == 0) {
                                uint8_t below = g.getBlock(x, y - 1, z);
                                if (below != LOG && below != DIRT && below != GRASS) ++branches;
                            }
                        }
            }
        };
        {
            // Большой дуб на ровной поляне посреди чанка
            World g(1, 0);
            Chunk ch(50, 50);
            for (int x = 0; x < CW; ++x)
                for (int z = 0; z < CW; ++z) {
                    for (int y = 0; y < 60; ++y) ch.set(x, y, z, DIRT);
                    ch.set(x, 60, z, GRASS);
                }
            int ok = 0, br = 0;
            for (uint32_t sd = 1; sd <= 20; ++sd) {
                Chunk t(50, 50);
                t.blocks = ch.blocks;
                uint32_t st = sd * 7919u;
                if (!g.bigTreeInChunk(t, 7, 61, 8, 12, st)) continue;
                ++ok;
                for (int x = 0; x < CW; ++x)
                    for (int z = 0; z < CW; ++z)
                        for (int y = 62; y < CH; ++y)
                            if (t.get(x, y, z) == LOG && t.get(x, y - 1, z) != LOG) ++br;
            }
            std::printf("big trees: %d of 20 placed, %d branch logs\n", ok, br);
            CHECK(ok == 20 && br > 20);
        }
        int s2, b2, s3, b3;
        features(2, s2, b2);
        features(3, s3, b3);
        std::printf("gen2: springs %d, branch logs %d; gen3: springs %d, branch logs %d\n", s2, b2, s3, b3);
        CHECK(s3 > s2 + 5);
        CHECK(b3 > b2 && b2 == 0);
    }

    // --- Поиск пути: вокруг стены через проход, не через лаву, не с высокого обрыва; зомби доходит до игрока
    {
        const int PY = Y;
        platform(w, 10, -30, 30, -10, PY); // x 10..30, z -30..-10
        for (int z = -30; z <= -10; ++z)
            if (z != -12)
                for (int y = PY + 1; y <= PY + 3; ++y) w.setBlock(20, y, z, STONE); // стена по x=20, проход у z=-12
        std::vector<glm::ivec3> path;
        CHECK(findPath(w, glm::vec3(15.5f, PY + 1, -25.5f), glm::vec3(25.5f, PY + 1, -25.5f), 1.95f, 24, path));
        bool viaGap = false;
        for (auto& q : path) viaGap |= q.x == 20 && q.z == -12;
        CHECK(path.back() == glm::ivec3(25, PY + 1, -26)); // дошли до цели
        CHECK(viaGap);
        // Лава в проходе — пути нет (идёт к ближайшей точке, но не в лаву)
        w.setBlock(20, PY, -12, LAVA, 0);
        findPath(w, glm::vec3(15.5f, PY + 1, -25.5f), glm::vec3(25.5f, PY + 1, -25.5f), 1.95f, 24, path);
        bool intoLava = false;
        for (auto& q : path) intoLava |= q.x == 20 && q.z == -12;
        CHECK(!intoLava);
        w.setBlock(20, PY, -12, STONE);
        // Зомби за стеной приходит к игроку
        MobManager mm;
        Mob& z = mm.spawn(MobType::Zombie, glm::vec3(15.5f, PY + 1, -25.5f), 0.f);
        z.health = 20;
        Player pl;
        pl.pos = glm::vec3(25.5f, PY + 1, -25.5f);
        pl.mode = GameMode::Survival;
        TickEvents ev;
        std::vector<ItemEntity> its;
        std::vector<Particle> parts;
        MobHooks h;
        h.sound = [](const std::string&, float, float, const glm::vec3*) {};
        uint32_t rng = 3;
        float bestD = 1e9f;
        for (int i = 0; i < 600; ++i) {
            mm.tick(w, pl, ev, its, parts, h, 1.f, rng);
            pl.health = 20;
            if (mm.mobs.empty()) break;
            bestD = std::min(bestD, glm::length(mm.mobs[0].pos - pl.pos));
        }
        std::printf("zombie closest approach: %.2f\n", bestD);
        CHECK(bestD < 2.5f);
    }

    // --- Песок и гравий падают: встают на землю, на факеле выпадают предметом
    {
        platform(w, -10, 30, -4, 34, Y);
        w.setBlock(-8, Y + 6, 32, SAND);  // висит в воздухе
        w.setBlock(-6, Y + 1, 32, TORCH, 5);
        w.setBlock(-6, Y + 4, 32, GRAVEL); // упадёт на факел
        MobManager mm;
        std::vector<ItemEntity> its;
        uint32_t rng = 1;
        for (int i = 0; i < 80; ++i) { runTicks(w, t, 1); mm.tickFalling(w, its, rng); }
        CHECK(w.getBlock(-8, Y + 6, 32) == AIR);
        CHECK(w.getBlock(-8, Y + 1, 32) == SAND);
        CHECK(w.getBlock(-6, Y + 4, 32) == AIR);
        bool gravelItem = false;
        for (auto& e : its) gravelItem |= e.stack.id == GRAVEL;
        CHECK(gravelItem);
        CHECK(mm.falling.empty());
    }

    // --- Рельсы (RailLogic 1.0): параллельные линии не цепляются, уголок — поворот, квадрат 2x2 — кольцо
    {
        platform(w, -30, -8, -18, 8, Y);
        auto place = [&](int x, int z) { w.setBlock(x, Y + 1, z, RAIL, 0); w.updateRailShape(x, Y + 1, z, true); };
        for (int z = -6; z <= -2; ++z) place(-28, z); // линия вдоль Z
        for (int z = -6; z <= -2; ++z) place(-27, z); // вплотную параллельная
        for (int z = -5; z <= -3; ++z) { // концы присоединяются к концам соседней линии — так и в 1.0
            CHECK(w.getMeta(-28, Y + 1, z) == 0);
            CHECK(w.getMeta(-27, Y + 1, z) == 0);
        }
        // Уголок: юг + восток
        place(-24, 0); place(-24, 1); place(-23, 0);
        CHECK(w.getMeta(-24, Y + 1, 0) == 9 || w.getMeta(-24, Y + 1, 0) == 6); // конец линии повернул к соседу
        // Квадрат 2x2 — четыре поворота
        place(-20, 4); place(-19, 4); place(-19, 5); place(-20, 5);
        int curves = 0;
        for (int x = -20; x <= -19; ++x)
            for (int z = 4; z <= 5; ++z) curves += w.getMeta(x, Y + 1, z) >= 6;
        CHECK(curves == 4);

        // Тест вагонетки на прямом + поворот: после поворота едет вдоль Z (motion.x ≈ 0, motion.z > 0)
        {
            platform(w, -2, -2, 8, 8, Y);
            for (int x = 0; x <= 4; ++x) { w.setBlock(x, Y + 1, 0, RAIL, 0); w.updateRailShape(x, Y + 1, 0, false); }
            w.setBlock(5, Y + 1, 0, RAIL, 0);
            for (int z = 1; z <= 5; ++z) { w.setBlock(5, Y + 1, z, RAIL, 0); w.updateRailShape(5, Y + 1, z, false); }
            for (int x = 0; x <= 5; ++x) w.updateRailShape(x, Y + 1, 0, false);
            for (int z = 0; z <= 5; ++z) w.updateRailShape(5, Y + 1, z, false);

            MobManager mmCart;
            Vehicle cv;
            cv.kind = VehicleKind::Minecart;
            cv.pos = glm::vec3(4.0f, (float)(Y + 1), 0.5f);
            cv.prev = cv.pos;
            cv.motion = glm::vec3(0.4f, 0.f, 0.f);
            mmCart.vehicles.push_back(cv);
            Player dummyP; dummyP.pos = glm::vec3(100, 100, 100);
            std::vector<Particle> ps; std::vector<ItemEntity> items; uint32_t rng = 1;
            // Прокручиваем 20 тиков — вагонетка должна пройти поворот и ехать по Z
            for (int tick = 0; tick < 20; ++tick)
                mmCart.tickVehicles(w, dummyP, -1, 0.f, 0.f, ps, items, rng);
            CHECK(!mmCart.vehicles.empty());
            if (!mmCart.vehicles.empty()) {
                Vehicle& r = mmCart.vehicles[0];
                CHECK(r.onRail);
                CHECK(r.pos.x > 4.9f && r.pos.x < 6.1f); // осталась на x=5 (рельс по Z)
                CHECK(r.pos.z > 2.0f);                    // продвинулась вдоль Z
                CHECK(std::abs(r.motion.x) < 0.01f);      // движется только по Z
                CHECK(r.motion.z > 0.01f);
            }
        }

        // Тест вагонетки на 2x2 кольце из кривых рельсов: не дёргается, делает полный оборот
        {
            platform(w, -2, -2, 4, 4, Y);
            auto placeR = [&](int x, int z) { w.setBlock(x, Y + 1, z, RAIL, 0); w.updateRailShape(x, Y + 1, z, true); };
            placeR(0, 0); placeR(1, 0); placeR(1, 1); placeR(0, 1);
            // Проверяем что все 4 рельса — кривые (shape >= 6)
            CHECK(w.getMeta(0, Y + 1, 0) >= 6 && w.getMeta(1, Y + 1, 0) >= 6 &&
                  w.getMeta(1, Y + 1, 1) >= 6 && w.getMeta(0, Y + 1, 1) >= 6);

            MobManager mmCart;
            Vehicle cv;
            cv.kind = VehicleKind::Minecart;
            cv.pos  = glm::vec3(0.5f, (float)(Y + 1), 0.2f);
            cv.prev = cv.pos;
            cv.motion = glm::vec3(0.4f, 0.f, 0.f);
            mmCart.vehicles.push_back(cv);
            Player dummyP; dummyP.pos = glm::vec3(100, 100, 100);
            std::vector<Particle> ps; std::vector<ItemEntity> items; uint32_t rng = 1;

            float prevYaw = mmCart.vehicles[0].yaw;
            bool reversed = false;
            for (int tick = 0; tick < 40; ++tick) {
                mmCart.tickVehicles(w, dummyP, -1, 0.f, 0.f, ps, items, rng);
                if (mmCart.vehicles.empty()) break;
                float newYaw = mmCart.vehicles[0].yaw;
                // Дёрганье = изменение yaw более чем на 135° за один тик
                float dy = newYaw - prevYaw;
                while (dy >  180.f) dy -= 360.f;
                while (dy < -180.f) dy += 360.f;
                if (std::abs(dy) > 135.f) { reversed = true; break; }
                prevYaw = newYaw;
            }
            CHECK(!mmCart.vehicles.empty()); // вагонетка не упала
            CHECK(!reversed);                // yaw никогда не прыгал на 180°+ (не дёргалась назад)
        }
    }

    {
        World w(1690554512, 0);
        w.setGenVersion(3);
        glm::ivec3 shPos;
        bool found = w.locateStronghold10(glm::vec3(0, 64, 0), shPos);
        std::printf("locateStronghold10: found=%d pos=(%d, %d, %d)\n", found, shPos.x, shPos.y, shPos.z);
        CHECK(found);
        CHECK(shPos.y >= 10 && shPos.y <= 44);
    }

    // --- Проверка гарантии портала Края на 50 случайных сидах
    {
        int portalOk = 0;
        for (uint32_t s = 1000; s < 1050; ++s) {
            World sw(s, 0);
            sw.setGenVersion(3);
            glm::ivec3 shPos;
            if (sw.locateStronghold10(glm::vec3(0, 64, 0), shPos)) {
                if (shPos.y >= 10 && shPos.y <= 44) ++portalOk;
            }
        }
        std::printf("Stronghold portal check across 50 seeds: %d/50 passed\n", portalOk);
        CHECK(portalOk == 50);
    }

    // --- Проверка адской крепости (dimension -1, genVersion 3)
    {
        World nw(12345678, -1);
        nw.setGenVersion(3);
        glm::ivec3 fortPos;
        bool found = nw.locateFortress10(glm::vec3(0, 64, 0), fortPos);
        std::printf("locateFortress10: found=%d pos=(%d, %d, %d)\n", found, fortPos.x, fortPos.y, fortPos.z);
        CHECK(found);
        CHECK(fortPos.y >= 30 && fortPos.y <= 90);
    }

    std::printf(failures ? "\n%d check(s) FAILED\n" : "All world checks passed\n", failures);
    return failures ? 1 : 0;
}
