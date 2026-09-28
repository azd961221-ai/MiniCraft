#pragma once
// Выпавшие предметы (EntityItem)
#include <glm/glm.hpp>
#include <vector>
#include "Inventory.h"
#include "World.h"

struct ItemEntity {
    uint32_t id = 0; // номер для рассылки (сетевая игра)
    glm::vec3 pos, prev, motion;
    ItemStack stack;
    int age = 0;
    int pickupDelay = 10;
    float bobOffset = 0.f;  // сдвиг фазы покачивания
    bool onGround = false;
    bool dead = false;
};

// Шар опыта (EntityXPOrb 1.0): летит к игроку ближе 8 блоков, подбирается касанием, живёт 5 минут
struct XpOrb {
    uint32_t id = 0;
    glm::vec3 pos{0.f}, prev{0.f}, motion{0.f};
    int value = 1, age = 0, color = 0;
    bool onGround = false, dead = false;
};
// Размер одного шара из оставшегося опыта (getXPSplit) и значок в item/xporb.png (getTextureByXP)
int xpSplit(int xp);
int xpOrbIcon(int value);
// Рассыпать xp опыта шарами разных размеров
void spawnXpOrbs(std::vector<XpOrb>& orbs, const glm::vec3& at, int xp, uint32_t& rng);
// Физика шара: target — глаза ближайшего игрока (nullptr — никого ближе 8 блоков); false — лопнул в лаве
bool moveXpOrb(XpOrb& o, const World& w, const glm::vec3* target, uint32_t& rng);

// Картина (EntityPainting 1.0): висит на грани блока-стены, сюжет из art/kz.png
struct PaintingArt { const char* name; int w, h, u, v; };
constexpr int PAINTING_ART_COUNT = 25;
extern const PaintingArt PAINTING_ARTS[PAINTING_ART_COUNT];
struct Painting {
    uint32_t id = 0;
    glm::ivec3 wall{0}; // блок стены
    int dir = 0;        // куда смотрит: 0 — -Z, 1 — -X, 2 — +Z, 3 — +X (direction в 1.0)
    int art = 0;
    bool dead = false;
    // Рамка для предмета (EntityItemFrame 1.4.2): висит так же, 12x12 пикселей, держит предмет с поворотом 0..3
    bool frame = false;
    ItemStack item;
    int rotation = 0;
    glm::vec3 normal() const;
    glm::vec3 right() const;  // «вправо» для того, кто смотрит на картину
    glm::vec3 center() const; // середина (в 1/16 перед стеной)
    void bounds(glm::vec3& mn, glm::vec3& mx) const;
};
// Помещается ли картина: за ней сплошная стена, перед ней нет блоков и других картин
bool paintingFits(const Painting& p, const World& w, const std::vector<Painting>& others);
// Повесить на грань face (нормаль, только стены) блока wall: случайный из подходящих по месту сюжетов
bool placePainting(std::vector<Painting>& ps, const World& w, const glm::ivec3& wall, const glm::ivec3& face, uint32_t& rng);
// Повесить рамку для предмета на грань face блока wall
bool placeItemFrame(std::vector<Painting>& ps, const World& w, const glm::ivec3& wall, const glm::ivec3& face);

// Предмет, выпавший из блока: случайное смещение внутри блока и небольшой подброс
void dropFromBlock(std::vector<ItemEntity>& items, const glm::ivec3& block, const ItemStack& stack, uint32_t& rng);
// Предмет, выброшенный игроком по направлению взгляда
void throwFromPlayer(std::vector<ItemEntity>& items, const glm::vec3& eye, const glm::vec3& look, const ItemStack& stack, uint32_t& rng);
// Тик: падение, трение, лава; возвращает число предметов, сгоревших в лаве (для звука)
int tickItems(std::vector<ItemEntity>& items, const World& w);
