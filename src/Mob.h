#pragma once
// Мобы Minecraft 1.0: животные и монстры, их ИИ, бой, стрелы, взрывы, спавн.
#include <functional>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include "Entity.h"
#include "Model.h"
#include "Particles.h"
#include "Player.h"
#include "Vehicle.h"
#include "World.h"

struct MobDef {
    const char* name;
    float halfWidth, height;
    int maxHealth;
    float moveSpeed;   // «moveSpeed» из 1.0 (0.7 — обычный, зомби 0.5, паук 0.8)
    bool hostile;
    const char* soundDir; // mob/pig и т.п.
    int xp;
};
const MobDef& mobDef(MobType t);

struct Mob {
    MobType type = MobType::Pig;
    glm::vec3 pos{0.f}, prev{0.f}, motion{0.f};
    float yaw = 0.f, prevYaw = 0.f;
    int health = 10;
    int hurtTime = 0, invulnerable = 0, deathTime = -1; // deathTime >= 0 — умирает
    int age = 0;
    bool onGround = false, collidedH = false, inWater = false, inLava = false;
    float fallDistance = 0.f;
    float limbSwing = 0.f, limbAmount = 0.f, prevLimbAmount = 0.f;
    float walked = 0.f, nextStep = 1.f;
    // ИИ
    glm::vec3 wanderTarget{0.f};
    bool hasWander = false;
    std::vector<glm::ivec3> path; // путь (PathFinder 1.0): клетки, куда ставить ноги
    glm::ivec3 pathGoal{0};
    int pathIdx = 0, pathTimer = 0;
    int fleeTicks = 0, attackCooldown = 0, playerHitTicks = 0;
    bool angry = false;          // паук, которого ударили или стемнело
    int fuse = 0, prevFuse = 0;  // крипер
    bool sheared = false;
    int eggTimer = 6000;
    int fireTicks = 0;
    int livingSound = 0;
    float wingFlap = 0.f, prevWingFlap = 0.f, wingSpeed = 1.f;
    bool removed = false;
    bool recordDrop = false; // крипер убит стрелой скелета — выпадет пластинка
    uint32_t id = 0;         // для целей волков и големов
    float scale = 1.f;       // детёныш 0.5, слизень — его размер
    int size = 1;            // слизень / лавовый куб: 1, 2, 4
    int color = 0;           // цвет шерсти овцы (как мета шерсти)
    int growingAge = 0;      // < 0 — детёныш, > 0 — отдых после размножения
    int inLove = 0, breedTimer = 0;
    bool tamed = false, sitting = false, saddled = false;
    int angerTicks = 0;      // свинозомби злится ограниченное время
    uint8_t heldBlock = 0, heldMeta = 0; // эндермен несёт блок
    int jumpDelay = 0, attackCounter = 0;
    float squish = 0.f, prevSquish = 0.f, squishTarget = 0.f;
    float tentacle = 0.f, prevTentacle = 0.f, swimPhase = 0.f;
    glm::vec3 swimDir{0.f};
    // Спрут (EntitySquid): наклон и вращение тела, скорость рывка и вращения, темп цикла щупалец
    float squidPitch = 0.f, prevSquidPitch = 0.f, squidRoll = 0.f, prevSquidRoll = 0.f;
    float squidVel = 0.f, squidRollSpeed = 0.f, squidSpeed = 0.f;
    uint32_t targetId = 0;
    int looting = 0;         // уровень «Добычи» оружия, которым убит
    bool aiming = false;     // скелет целится в игрока (руки с луком по взгляду)
    int stareTicks = 0;      // эндермен: сколько тиков подряд на него смотрят
    int despawnAge = 0;
    bool ridden = false;     // на свинье сидит игрок
    bool charged = false;    // крипер после удара молнии (взрыв сильнее, светящаяся оболочка)
    uint32_t riderId = 0;    // сетевая игра: кто сидит на свинье
    uint32_t lastAttacker = 0; // сетевая игра: кто ударил последним (опыт и достижения ему)      // «возраст» для исчезновения (entityAge): растёт вдали от игрока, на свету вдвое быстрее
    int swingTicks = -1;     // взмах рукой при ударе: 0..8 тиков, -1 — не машет
    float swing = 0.f, prevSwing = 0.f; // фаза взмаха 0..1 (swingProgress)
    int teleportDelay = 0;   // эндермен: ожидание телепорта к далёкому игроку

    bool dying() const { return deathTime >= 0; }
};

// Размер тела конкретного моба (с учётом детёныша и размера слизня)
inline float mobHalfW(const Mob& m) { return mobDef(m.type).halfWidth * m.scale; }
inline float mobHeight(const Mob& m) { return mobDef(m.type).height * m.scale; }
inline bool isSpiderLike(MobType t) { return t == MobType::Spider || t == MobType::CaveSpider; }
// Монстр для лимита спавна, исчезновения вдали и мирной сложности (как EntityMob в 1.0):
// враждебные плюс нейтральные эндермен и свинозомби (раньше они не считались и копились тысячами)
inline bool countsAsMonster(MobType t) {
    return (mobDef(t).hostile || t == MobType::Enderman || t == MobType::PigZombie || t == MobType::WitherSkeleton || t == MobType::Witch || t == MobType::ZombieVillager) &&
           t != MobType::EnderDragon && t != MobType::Wither;
}
inline bool isFireImmune(MobType t) {
    return t == MobType::PigZombie || t == MobType::Ghast || t == MobType::Blaze || t == MobType::MagmaCube ||
           t == MobType::WitherSkeleton || t == MobType::Wither;
}
inline bool canBreed(MobType t) {
    return t == MobType::Pig || t == MobType::Cow || t == MobType::Sheep || t == MobType::Chicken || t == MobType::Mooshroom ||
           t == MobType::Wolf || t == MobType::Ocelot || t == MobType::Cat;
}

// Огненный шар гаста (большой), ифрита (маленький) или череп иссушителя (wither)
struct Fireball {
    glm::vec3 pos{0.f}, prev{0.f}, accel{0.f}, motion{0.f};
    bool small = false, dead = false;
    bool deflected = false; // отбит игроком: летит обратно и может задеть гаста
    bool wither = false;    // череп иссушителя
    bool blue = false;      // синий череп (разрушает любые блоки)
    int age = 0;
    uint32_t id = 0, owner = 0; // owner — кто отбил (сетевая игра)
};

struct Arrow {
    glm::vec3 pos{0.f}, prev{0.f}, motion{0.f}, dir{0.f, 0.f, 1.f};
    bool fromPlayer = false, stuck = false, critical = false, dead = false;
    bool pickup = true;  // стрелы с «Бесконечностью» не подбираются
    int age = 0;
    uint32_t id = 0, owner = 0; // owner — стрелок (сетевая игра)
    float damage = 2.f;  // урон на единицу скорости (Сила добавляет 0.5*L + 0.5)
    int punch = 0;       // Отдача
    bool flame = false;  // Горящая стрела поджигает цель
};

// Брошенный снежок или яйцо (EntityThrowable)
struct Throwable {
    uint32_t id = 0, owner = 0; // owner — кто бросил (жемчуг переносит его)
    glm::vec3 pos{0.f}, prev{0.f}, motion{0.f};
    uint16_t item = 0;
    uint16_t damage = 0;     // значение зелья для взрывных зелий
    int age = 0;
    bool dead = false;
    bool seeking = false;    // око Края летит к крепости
    glm::vec3 target{0.f};
};

// Зажжённый динамит (EntityTNTPrimed): падает, через 80 тиков взрывается с силой 4
struct PrimedTnt {
    uint32_t id = 0;
    glm::vec3 pos{0.f}, prev{0.f}, motion{0.f}; // pos — низ по центру
    int fuse = 80;
    bool onGround = false, dead = false;
};

// Падающий песок/гравий (EntityFallingSand 1.0): 0.98 x 0.98, падает и снова становится блоком
struct FallingBlock {
    uint32_t id = 0;
    glm::vec3 pos{0.f}, prev{0.f}, motion{0.f}; // pos — низ по центру
    uint8_t block = SAND;
    int age = 0;
    bool onGround = false, dead = false;
};

// Спавнер в данже (TileEntityMobSpawner)
struct Spawner {
    int x = 0, y = 0, z = 0;
    MobType type = MobType::Zombie;
    int delay = 20;
    float spin = 0.f, prevSpin = 0.f; // вращение модели внутри клетки
};

// Связь с остальной игрой: звук, опыт, инвентарь для подбора стрел
struct MobHooks {
    std::function<void(const std::string&, float, float, const glm::vec3*)> sound;
    std::function<void(int)> addXp;
    std::function<bool(ItemStack&)> giveItem; // true — всё поместилось
    std::function<void(MobType)> onKill;      // игрок убил моба (достижения)
    std::function<void(int)> achievement;     // особые достижения (свинья, снайпер, ответный шар)
    // Сетевая игра (сервер): то же, но конкретному игроку
    std::function<void(Player&, int)> achievementFor;
    std::function<void(Player&, int)> addXpTo;
    std::function<void(Player&, MobType)> onKillBy;
    std::function<bool(Player&, ItemStack&)> giveItemTo;
    std::function<void(Player&, const glm::vec3&)> teleport;       // жемчуг Края
    std::function<void(Player&, int, int, int)> potionOn;          // эффект взрывного зелья: id, сила, длительность
    std::function<void(const glm::vec3&, float)> explosionFx;      // взрыв (частицы у клиентов)
};

// Поиск пути по блокам (A*, как PathFinder 1.0): 4 направления, шаг вверх на 1, спуск до 3, мимо лавы, огня и кактусов.
// Если цель недостижима — путь до ближайшей к ней клетки. out — клетки без стартовой
bool findPath(const World& w, const glm::vec3& from, const glm::vec3& to, float height, int maxDist, std::vector<glm::ivec3>& out);

class MobManager {
public:
    std::vector<Mob> mobs;
    std::vector<Arrow> arrows;
    std::vector<Throwable> throwables;
    std::vector<PrimedTnt> tnts;
    std::vector<Fireball> fireballs;
    std::vector<Vehicle> vehicles;
    std::vector<XpOrb> orbs; // шары опыта
    std::vector<FallingBlock> falling; // падающий песок и гравий
    void tickFalling(World& w, std::vector<ItemEntity>& items, uint32_t& rng);
    std::vector<Painting> paintings;
    int paintingCheck_ = 0;
    void checkPaintings(World& w, std::vector<ItemEntity>& items, uint32_t& rng);
    // Шары опыта: притяжение к ближайшему живому игроку, подбор касанием (опыт — через hooks.addXp / addXpTo)
    void tickOrbs(World& w, Player& p0, const MobHooks& hooks, uint32_t& rng);
    std::vector<glm::ivec3> pressedDetectors;
    // Вагонетки и лодки: riding — индекс, на котором сидит игрок (-1 — ни на чём), forward — ввод W/S
    void tickVehicles(World& w, Player& p, int riding, float forward, float strafe, std::vector<Particle>& ps, std::vector<ItemEntity>& items,
                      uint32_t& rng);
    uint32_t nextId = 1;
    bool inTick_ = false;
    uint32_t playerTargetId = 0;   // кого последним ударил игрок (цель для прирученных волков)
    uint32_t playerAttackerId = 0; // кто последним ударил игрока
    std::vector<Mob> pending;      // новые мобы, появившиеся во время тика (деление слизней, детёныши)
    std::vector<glm::vec3> crystalBlasts; // взорвавшиеся кристаллы Края
    bool dragonKilled = false;     // дракон умер в этом тике — игра строит портал выхода
    int dragonXp = 0;
    std::unordered_set<int64_t> populated; // чанки, где уже заспавнены животные при генерации
    std::unordered_map<int64_t, Spawner> spawners;
    bool raining = false; // дождь тушит мобов, нежить не загорается
    bool playerPumpkin = false; // на игроке тыква — эндермены не злятся от взгляда
    int difficulty = 2;   // 0 Peaceful .. 3 Hard: урон по игроку и спавн монстров
    // ---- Сетевая игра (сервер): все игроки мира; пусто — одиночная игра (игрок из параметров)
    std::vector<Player*> netPlayers;
    uint32_t currentAttacker = 0; // выставляется перед hurt() от удара игрока
    // Седоки транспорта (сервер): номер транспорта -> игрок и его «вперёд/вбок»
    struct RiderInput { Player* player; float forward, strafe; };
    std::unordered_map<uint32_t, RiderInput> riders;
    uint32_t nextEntityId = 1;    // номера стрел, предметов и прочего для рассылки
    Player* nearestPlayer(const glm::vec3& pos, Player& p0, bool vulnerableOnly = true);
    Player* playerById(uint32_t id, Player& p0);
    // Исчезновение далёких монстров (despawnEntity 1.0)
    void despawn(const Player& p, World& w, float skyFactor);
    uint32_t rngDespawn_ = 12345u;
    int scaleDamage(int dmg) const {
        switch (difficulty) {
        case 0: return 0;
        case 1: return dmg / 2 + 1;
        case 3: return dmg * 3 / 2;
        default: return dmg;
        }
    }

    // Спавнер из данжа (метаданные блока: 0 зомби, 1 скелет, 2 паук)
    void addSpawner(const glm::ivec3& p, uint8_t meta);
    void tickSpawners(World& w, const Player& p, std::vector<Particle>& particles, uint32_t& rng);

    Mob& spawn(MobType t, const glm::vec3& pos, float yaw);
    // Животные в только что сгенерированном чанке (как спавн при генерации мира в 1.0)
    void populateChunk(World& w, int cx, int cz, uint32_t& rng);
    // Монстры в темноте вокруг игрока; skySub — насколько ночь снижает свет неба (0..11)
    void spawnHostiles(World& w, const Player& p, int skySub, int renderDistance, uint32_t& rng);

    void tick(World& w, Player& p, TickEvents& pev, std::vector<ItemEntity>& items, std::vector<Particle>& particles,
              const MobHooks& hooks, float skyFactor, uint32_t& rng);

    // Удар по мобу; from — откуда пришёл удар (для отбрасывания)
    bool hurt(Mob& m, int dmg, const glm::vec3& from, float knockback, bool byPlayer, const MobHooks& hooks);
    // Ближайший моб на луче (для удара и ПКМ)
    Mob* raycast(const glm::vec3& o, const glm::vec3& d, float maxDist, float& dist);
    Arrow& shootArrow(const glm::vec3& from, const glm::vec3& dir, float speed, float inaccuracy, bool fromPlayer,
                      bool critical, uint32_t& rng);
    // Удар по огненному шару: ближайший на луче (до maxDist) разворачивается по взгляду игрока
    bool deflectFireball(const glm::vec3& eye, const glm::vec3& look, float maxDist);
    // Бросок снежка/яйца: скорость 1.5, разброс 1 (как EntityThrowable в 1.0)
    void throwItem(const glm::vec3& from, const glm::vec3& dir, uint16_t item, uint32_t& rng, uint16_t damage = 0);
    // Поджечь динамит в блоке b (блок уже убран). От взрыва — короткий фитиль 10..29 тиков
    void igniteTnt(const glm::ivec3& b, bool fromExplosion, uint32_t& rng);
    void explode(World& w, const glm::vec3& c, float power, Player& p, TickEvents& pev, std::vector<ItemEntity>& items,
                 std::vector<Particle>& particles, const MobHooks& hooks, uint32_t& rng, bool incendiary = false);
    // ПКМ по мобу предметом: пшеница, кость, краситель, ножницы, миска, седло, мясо. true — действие выполнено;
    // consume — сколько предметов потрачено, replace — чем заменить предмет в руке (миска -> суп)
    bool interact(Mob& m, const ItemStack& held, Player& p, std::vector<ItemEntity>& items, std::vector<Particle>& particles,
                  const MobHooks& hooks, uint32_t& rng, int& consume, ItemStack& replace);

    // Животные раз в 400 тиков (лимит 15 на 256 чанков) и спруты (лимит 5) вокруг игрока, как SpawnerAnimals 1.0
    void spawnPassive(World& w, const Player& p, bool animals, bool water, uint32_t& rng);
    // Удар молнии в точку: урон и поджог рядом, крипер заряжается, свинья становится свинозомби, огонь на земле
    void strikeLightning(World& w, const glm::vec3& at, Player& p0, TickEvents& pev, const MobHooks& hooks, uint32_t& rng);
    int countHostile() const;
    int countPassive() const;

    // items — выпавшие предметы (сохраняются хвостом «IT01»; раньше пропадали при выходе)
    bool save(const std::string& path, const Player& p, const std::vector<ItemEntity>* items = nullptr) const;
    bool load(const std::string& path, Player& p, std::vector<ItemEntity>* items = nullptr);
    bool loadFrom(const std::string& path, Player& p, std::vector<ItemEntity>* items);
};
