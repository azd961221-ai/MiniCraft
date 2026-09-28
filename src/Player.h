#pragma once
// Игрок: физика движения по тикам (20 в секунду) с константами Minecraft 1.0,
// здоровье, голод, воздух, урон.
#include <glm/glm.hpp>
#include <cmath>
#include <vector>
#include "World.h"

constexpr float PLAYER_HALF_W = 0.3f;
constexpr float PLAYER_H = 1.8f;
constexpr float EYE_H = 1.62f;
constexpr int TICKS_PER_SECOND = 20;

enum class GameMode : uint8_t { Survival = 0, Creative = 1 };

struct MoveInput {
    float forward = 0.f, strafe = 0.f; // -1..1
    bool jump = false, sneak = false;
    bool sprintRequest = false;          // Ctrl или двойное W
};

// Что произошло за тик — для звуков и эффектов
struct TickEvents {
    bool step = false;        // шаг (звук по блоку stepBlock)
    uint8_t stepBlock = AIR;
    bool landed = false;      // приземление
    float fallDistance = 0.f;
    bool splash = false;      // вход в воду
    bool swim = false;
    int damage = 0;           // полученный урон
    int armorDamage = 0;      // сколько урона приняла броня (для износа)
    bool died = false;
};

// Действующий эффект зелья (id как в Potion.h, amp: 0 — I, 1 — II)
struct ActiveEffect {
    int id = 0, amp = 0, ticks = 0;
};

struct Player {
    uint32_t netId = 0;               // номер игрока на сервере (0 — одиночная игра)
    glm::vec3 pos{0.5f, 80.f, 0.5f}; // центр ступней
    glm::vec3 prevPos{0.5f, 80.f, 0.5f};
    glm::vec3 motion{0.f};           // блоков за тик
    float yaw = -90.f, pitch = 0.f;

    GameMode mode = GameMode::Survival;
    bool onGround = false, collidedH = false;
    bool inWater = false, inLava = false, eyeInWater = false;
    bool fly = false, sprinting = false, sneaking = false;
    float fallDistance = 0.f;
    int armor = 0, armorCarry = 0; // очки брони (ставит игра из инвентаря) и остаток деления
    int starveFloor = 1;           // голод не опускает здоровье ниже (Easy 10, Normal 1, Hard 0)

    int health = 20, food = 20, air = 300;
    float saturation = 5.f, exhaustion = 0.f;
    int foodTimer = 0;
    int invulnerable = 0, hurtTime = 0, fireTicks = 0;
    int xpCooldown = 0; // пауза между подбором шаров опыта (2 тика)
    int poisonTicks = 0; // отравление (укус пещерного паука): 1 урон раз в 25 тиков, но не до смерти
    std::vector<ActiveEffect> effects;
    // Источник следующего урона: -1 — без защиты (голод, удушье), 0 — обычный, 1 — огонь, 2 — падение, 3 — взрыв, 4 — снаряд
    int damageSource = 0;
    int enchProt[5] = {0, 0, 0, 0, 0}; // очки защиты чар брони по источникам (считает игра)
    int respiration = 0;
    bool aquaAffinity = false;
    int effectAmp(int id) const {
        for (auto& e : effects)
            if (e.id == id) return e.amp;
        return -1;
    }
    bool hasEffect(int id) const { return effectAmp(id) >= 0; }
    bool dead = false;
    int deathTicks = 0;

    int xpLevel = 0, xpTotal = 0;
    float xpProgress = 0.f; // доля до следующего уровня

    float walkDist = 0.f, prevWalkDist = 0.f, nextStep = 1.f;
    float eyeOffset = 0.f, prevEyeOffset = 0.f; // приседание

    bool creative() const { return mode == GameMode::Creative; }
    glm::vec3 interpPos(float a) const { return glm::mix(prevPos, pos, a); }
    glm::vec3 eye(float a = 1.f) const {
        return interpPos(a) + glm::vec3(0, EYE_H - glm::mix(prevEyeOffset, eyeOffset, a), 0);
    }
    glm::vec3 look() const {
        float cy = std::cos(glm::radians(yaw)), sy = std::sin(glm::radians(yaw));
        float cp = std::cos(glm::radians(pitch)), sp = std::sin(glm::radians(pitch));
        return glm::normalize(glm::vec3(cy * cp, sp, sy * cp));
    }
    void resetStats();
};

bool collides(const World& w, const glm::vec3& feet);
void tickPlayer(Player& p, World& w, const MoveInput& in, TickEvents& ev);
// unblockable — урон, который броня не снижает (утопление, голод, огонь, удушье)
bool hurtPlayer(Player& p, int amount, TickEvents& ev, bool unblockable = false);
void addExhaustion(Player& p, float amount);
void eatFood(Player& p, int food, float saturationModifier);
// Наложить эффект (заменяет более слабый или короткий такой же)
void addEffect(Player& p, int id, int amp, int ticks);
