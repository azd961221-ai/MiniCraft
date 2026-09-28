#pragma once
// Модели мобов из коробок, как ModelRenderer/ModelBox в Minecraft 1.0.
// Координаты модели — в пикселях, ось Y направлена вниз, «земля» на y = 24.
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include "World.h"

// Новые типы — только в конец (номер хранится в сохранении)
enum class MobType : uint8_t {
    Pig, Cow, Sheep, Chicken, Zombie, Skeleton, Spider, Creeper,
    Wolf, Squid, Slime, Enderman, Silverfish, CaveSpider, Mooshroom, SnowGolem, Villager,
    PigZombie, Ghast, Blaze, MagmaCube,
    EnderDragon, EnderCrystal,
    WitherSkeleton, Wither, Witch, Bat, IronGolem, Ocelot, Cat, ZombieVillager,
    COUNT
};

// Размер текстуры моба (старые 64x32, снеговик и житель 64x64, ведьма 64x128, голем 128x128)
inline glm::vec2 mobTexSize(MobType t) {
    if (t == MobType::EnderDragon) return glm::vec2(256.f, 256.f);
    if (t == MobType::EnderCrystal) return glm::vec2(64.f, 32.f);
    if (t == MobType::IronGolem) return glm::vec2(128.f, 128.f);
    if (t == MobType::Witch) return glm::vec2(64.f, 128.f);
    if (t == MobType::SnowGolem || t == MobType::Villager || t == MobType::Bat || t == MobType::Wither || t == MobType::ZombieVillager ||
        t == MobType::Zombie || t == MobType::PigZombie) // ModelZombie 1.4.2 — 64x64
        return glm::vec2(64.f, 64.f);
    return glm::vec2(64.f, 32.f);
}

struct ModelBox {
    float x, y, z;     // угол коробки относительно точки вращения части
    int w, h, d;       // размеры
    int u, v;          // смещение развёртки в текстуре
    float inflate = 0; // раздувание (шерсть овцы)
    bool mirror = false;
};

struct ModelPart {
    glm::vec3 pivot{0.f};
    glm::vec3 rot{0.f}; // углы в радианах, порядок применения как в оригинале: Z, Y, X
    std::vector<ModelBox> boxes;
    glm::mat4 pre{1.f}; // преобразование родителя (дочерние части: концы крыльев, суставы лап)
};

// Параметры позы
struct ModelPose {
    float limbSwing = 0.f, limbAmount = 0.f; // фаза и сила шага
    float age = 0.f;                          // тики жизни (с дробной частью) — для дыхания рук
    float headYaw = 0.f, headPitch = 0.f;     // поворот головы относительно тела, радианы
    bool sheared = false;
    float wingFlap = 0.f;                     // взмах крыльев курицы
    float tail = 0.f;                         // хвост волка (выше — здоровее)
    bool sitting = false;                     // сидящий волк
    bool angry = false;                       // эндермен с открытой пастью
    bool carrying = false;                    // эндермен несёт блок
    float squish = 0.f;                       // сплющивание слизня при прыжке
    float tentacle = 0.f;                     // щупальца спрута
    bool aimBow = false;                      // руки вытянуты по взгляду, левая придерживает тетиву
    float swing = 0.f;                        // фаза удара 0..1 (onGround в ModelBiped)
};

// layer 0 — основная модель, 1 — второй слой (шерсть овцы)
std::vector<ModelPart> buildMobModel(MobType type, const ModelPose& pose, int layer = 0);
// Дракон Края (ModelDragon 1.0): flap — фаза взмаха крыльев, age — тики
std::vector<ModelPart> buildDragonModel(float flap, float age, float jaw);
// Кристалл Края (ModelEnderCrystal): основание из бедрока, два вложенных стеклянных куба и ядро.
// Части уже в блоках относительно низа кристалла (без зеркалирования RenderLiving)
std::vector<ModelPart> buildCrystalModel(float age);
// Книга над столом зачарования (ModelBook): tick — тики, flipL/flipR — листаемые страницы 0..1, spread — раскрытие 0..1.
// Модель в пикселях без зеркала, текстура 64x32
std::vector<ModelPart> buildBookModel(float tick, float flipL, float flipR, float spread);
// Вагонетка (ModelMinecart, cart.png) и лодка (ModelBoat, boat.png): дно и четыре борта, текстура 64x32
std::vector<ModelPart> buildMinecartModel();
// Сундук (ModelChest, 64x64) или двойной (ModelLargeChest, 128x64): крышка, замок, низ. lid — открытие 0..1
std::vector<ModelPart> buildChestModel(bool large, float lid);
std::vector<ModelPart> buildBoatModel();
// Игрок (ModelBiped 1.0, char.png): части по порядку — голова, шляпа, тело, правая рука, левая рука, правая нога, левая нога.
// inflate — раздувание (броня: 1.0 для шлема/нагрудника/ботинок, 0.5 для поножей)
struct PlayerPose {
    float limbSwing = 0, limbAmount = 0, age = 0, headYaw = 0, headPitch = 0, swing = 0;
    bool sneak = false, holding = false, riding = false;
};
std::vector<ModelPart> buildPlayerModel(const PlayerPose& ps, float inflate = 0.f);
// Правая рука игрока (char.png) для вида от первого лица
std::vector<ModelPart> buildPlayerArm();

// Вершины модели: m переводит пиксельные координаты модели в мир.
void emitModel(std::vector<Vertex>& out, const std::vector<ModelPart>& parts, const glm::mat4& m,
               float sky, float block, float texW = 64.f, float texH = 32.f);

// Матрица сущности: позиция ног, поворот (градусы, как yaw игрока), угол падения при смерти, масштаб
glm::mat4 entityMatrix(const glm::vec3& feet, float yawDeg, float deathAngleDeg = 0.f, glm::vec3 scale = glm::vec3(1.f));
