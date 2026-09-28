#include "Player.h"
#include <algorithm>
#include <cmath>
#include "Physics.h"

void Player::resetStats() {
    health = 20; food = 20; air = 300;
    saturation = 5.f; exhaustion = 0.f; foodTimer = 0;
    invulnerable = hurtTime = fireTicks = poisonTicks = 0;
    effects.clear();
    dead = false; deathTicks = 0;
    fallDistance = 0.f; motion = glm::vec3(0.f);
}

bool collides(const World& w, const glm::vec3& p) { return anyCollision(w, bodyBox(p, PLAYER_HALF_W, PLAYER_H)); }

// Есть ли в объёме тела жидкость данного типа
static bool bodyInLiquid(const World& w, const glm::vec3& p, uint8_t liquid) {
    int x0 = (int)std::floor(p.x - PLAYER_HALF_W + 0.001f), x1 = (int)std::floor(p.x + PLAYER_HALF_W - 0.001f);
    int y0 = (int)std::floor(p.y + 0.4f), y1 = (int)std::floor(p.y + PLAYER_H - 0.4f);
    int z0 = (int)std::floor(p.z - PLAYER_HALF_W + 0.001f), z1 = (int)std::floor(p.z + PLAYER_HALF_W - 0.001f);
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x)
                if (w.getBlock(x, y, z) == liquid) return true;
    return false;
}

void addEffect(Player& p, int id, int amp, int ticks) {
    for (auto& e : p.effects)
        if (e.id == id) {
            if (amp > e.amp || (amp == e.amp && ticks > e.ticks)) { e.amp = amp; e.ticks = ticks; }
            return;
        }
    p.effects.push_back({id, amp, ticks});
}

static uint32_t protRng = 0x2545F491u;

void addExhaustion(Player& p, float amount) {
    if (!p.creative()) p.exhaustion = std::min(p.exhaustion + amount, 40.f);
}

void eatFood(Player& p, int food, float satMod) {
    p.food = std::min(p.food + food, 20);
    p.saturation = std::min(p.saturation + food * satMod * 2.f, (float)p.food);
}

bool hurtPlayer(Player& p, int amount, TickEvents& ev, bool unblockable) {
    if (p.creative() || p.dead || amount <= 0) return false;
    if (p.invulnerable > 0) return false;
    // Чары брони (EnchantmentProtection): до 20 из 25 долей урона, со случайным разбросом
    int src = p.damageSource;
    p.damageSource = 0;
    if (src >= 0 && !(unblockable && src == 0)) {
        int total = std::min(25, p.enchProt[std::min(src, 4)]);
        if (total > 0) {
            protRng ^= protRng << 13; protRng ^= protRng >> 17; protRng ^= protRng << 5;
            total = std::min(20, (total + 1) / 2 + (int)(protRng % (uint32_t)(total / 2 + 1)));
            amount = (amount * (25 - total) + 12) / 25;
            if (amount <= 0) return false;
        }
    }
    if (!unblockable && p.armor > 0) {
        // Броня 1.0: урон * (25 - броня) / 25, остаток деления копится
        ev.armorDamage += amount;
        int total = amount * (25 - p.armor) + p.armorCarry;
        amount = total / 25;
        p.armorCarry = total % 25;
    }
    p.health -= amount;
    p.invulnerable = 10;
    p.hurtTime = 10;
    addExhaustion(p, 0.3f);
    ev.damage += amount;
    if (p.health <= 0) {
        p.health = 0;
        p.dead = true;
        ev.died = true;
    }
    return true;
}

// Перемещение на motion за тик (moveEntity 1.0): коробки блоков, подъём на 0.5, край при приседании
static void moveEntity(const World& w, Player& p) {
    BodyState st;
    st.onGround = p.onGround;
    moveBody(w, p.pos, PLAYER_HALF_W, PLAYER_H, p.motion, 0.5f, p.sneaking && !p.fly, st);
    p.onGround = st.onGround;
    p.collidedH = st.collidedH;
}

// Разгон в направлении ввода (moveFlying в оригинале)
static void accelerate(Player& p, float strafe, float forward, float accel) {
    float d = strafe * strafe + forward * forward;
    if (d < 1e-4f) return;
    d = std::sqrt(d);
    if (d < 1.f) d = 1.f;
    d = accel / d;
    strafe *= d;
    forward *= d;
    float s = std::sin(glm::radians(p.yaw)), c = std::cos(glm::radians(p.yaw));
    glm::vec3 fwd(c, 0, s), right(-s, 0, c);
    p.motion += fwd * forward + right * strafe;
}

void tickPlayer(Player& p, World& w, const MoveInput& in, TickEvents& ev) {
    p.prevPos = p.pos;
    p.prevWalkDist = p.walkDist;
    p.prevEyeOffset = p.eyeOffset;
    if (p.invulnerable > 0) --p.invulnerable;
    if (p.hurtTime > 0) --p.hurtTime;
    if (p.dead) { ++p.deathTicks; return; }

    bool wasInWater = p.inWater;
    p.inWater = bodyInLiquid(w, p.pos, WATER);
    p.inLava = bodyInLiquid(w, p.pos, LAVA);
    bool inWeb = bodyInLiquid(w, p.pos, COBWEB);
    // Течение воды сносит игрока
    if (p.inWater && !p.fly) {
        glm::vec3 fl = w.flowVector((int)std::floor(p.pos.x), (int)std::floor(p.pos.y + 0.4f), (int)std::floor(p.pos.z)) +
                       w.flowVector((int)std::floor(p.pos.x), (int)std::floor(p.pos.y + 1.2f), (int)std::floor(p.pos.z));
        if (glm::length(fl) > 1e-4f) p.motion += glm::normalize(fl) * 0.014f;
    }
    glm::vec3 eye = p.eye();
    p.eyeInWater = w.getBlock((int)std::floor(eye.x), (int)std::floor(eye.y), (int)std::floor(eye.z)) == WATER;
    if (p.inWater && !wasInWater && p.fallDistance > 1.f) ev.splash = true;

    // ---- Режимы бега и приседания
    float forward = in.forward, strafe = in.strafe;
    p.sneaking = in.sneak && !p.fly;
    if (p.sneaking) { forward *= 0.3f; strafe *= 0.3f; }
    bool canSprint = (p.food > 6 || p.creative()) && !p.sneaking && forward > 0.8f;
    if (in.sprintRequest && canSprint) p.sprinting = true;
    if (!canSprint || p.collidedH) p.sprinting = false;

    // ---- Движение
    glm::vec3 before = p.pos;
    if (p.fly) {
        float vy = (in.jump ? 1.f : 0.f) - (in.sneak ? 1.f : 0.f);
        p.motion.y += (vy * 0.375f - p.motion.y) * 0.6f;
        accelerate(p, strafe, forward, p.sprinting ? 0.1f : 0.05f);
        if (inWeb) { p.motion.x *= 0.25f; p.motion.z *= 0.25f; p.motion.y *= 0.05f; p.fallDistance = 0.f; }
        moveEntity(w, p);
        p.motion.x *= 0.91f;
        p.motion.z *= 0.91f;
        if (p.onGround && !p.creative()) p.fly = false;
    } else if (p.inWater || p.inLava) {
        if (in.jump) p.motion.y += 0.04f;
        accelerate(p, strafe, forward, 0.02f);
        if (inWeb) { p.motion.x *= 0.25f; p.motion.z *= 0.25f; p.motion.y *= 0.05f; p.fallDistance = 0.f; }
        moveEntity(w, p);
        float drag = p.inWater ? 0.8f : 0.5f;
        p.motion *= drag;
        p.motion.y -= 0.02f;
        // Выбраться из воды на берег
        if (p.collidedH && in.jump) p.motion.y = 0.3f;
    } else {
        uint8_t below = w.getBlock((int)std::floor(p.pos.x), (int)std::floor(p.pos.y - 0.5f), (int)std::floor(p.pos.z));
        float slip = p.onGround ? slipperiness(below) * 0.91f : 0.91f;
        float speed = 0.1f * (p.sprinting ? 1.3f : 1.f);
        if (p.hasEffect(2)) speed *= 1.f + 0.2f * (p.effectAmp(2) + 1);   // скорость
        if (p.hasEffect(10)) speed *= 1.f - 0.15f * (p.effectAmp(10) + 1); // замедление
        float accel = p.onGround ? speed * (0.16277136f / (slip * slip * slip)) : (p.sprinting ? 0.026f : 0.02f);
        if (p.onGround && in.jump) {
            p.motion.y = 0.42f;
            if (p.sprinting) {
                float s = std::sin(glm::radians(p.yaw)), c = std::cos(glm::radians(p.yaw));
                p.motion += glm::vec3(c, 0, s) * 0.2f;
                addExhaustion(p, 0.8f);
            } else {
                addExhaustion(p, 0.2f);
            }
        }
        accelerate(p, strafe, forward, accel);
        // Лестница (isOnLadder): медленный спуск, на приседании — держимся, упёрлись — лезем вверх
        bool ladder = isClimbable(w.getBlock((int)std::floor(p.pos.x), (int)std::floor(p.pos.y), (int)std::floor(p.pos.z)));
        if (ladder) {
            p.motion.x = std::clamp(p.motion.x, -0.15f, 0.15f);
            p.motion.z = std::clamp(p.motion.z, -0.15f, 0.15f);
            p.fallDistance = 0.f;
            if (p.motion.y < -0.15f) p.motion.y = -0.15f;
            if (p.sneaking && p.motion.y < 0.f) p.motion.y = 0.f;
        }
        if (inWeb) { p.motion.x *= 0.25f; p.motion.z *= 0.25f; p.motion.y *= 0.05f; p.fallDistance = 0.f; }
        moveEntity(w, p);
        if (ladder && p.collidedH) p.motion.y = 0.2f;
        p.motion.y -= 0.08f;
        p.motion.y *= 0.98f;
        p.motion.x *= slip;
        p.motion.z *= slip;
    }

    // ---- Пройденное расстояние: шаги и голод
    glm::vec3 moved = p.pos - before;
    float horiz = std::sqrt(moved.x * moved.x + moved.z * moved.z);
    if (p.onGround && !p.fly) {
        p.walkDist += horiz * 0.6f;
        if (p.walkDist > p.nextStep && !p.sneaking) {
            p.nextStep = std::floor(p.walkDist) + 1.f;
            ev.step = true;
            ev.stepBlock = w.getBlock((int)std::floor(p.pos.x), (int)std::floor(p.pos.y - 0.2f), (int)std::floor(p.pos.z));
        }
    }
    if (p.inWater) {
        addExhaustion(p, 0.015f * glm::length(moved));
        ev.swim = horiz > 0.05f; // частоту звука ограничивает игра
    } else if (p.onGround) {
        addExhaustion(p, (p.sprinting ? 0.1f : 0.01f) * horiz);
    }

    // ---- Падение
    if (p.fly || p.inWater) {
        p.fallDistance = 0.f;
    } else if (p.onGround) {
        if (p.fallDistance > 0.f) {
            ev.landed = true;
            ev.fallDistance = p.fallDistance;
            static uint32_t trampleRng = 0x6F1D3A5Bu;
            trampleRng ^= trampleRng << 13; trampleRng ^= trampleRng >> 17; trampleRng ^= trampleRng << 5;
            w.trampleFarmland((int)std::floor(p.pos.x), (int)std::floor(p.pos.y - 0.2f), (int)std::floor(p.pos.z), p.fallDistance,
                              (trampleRng & 0xFFFFFF) / float(0x1000000));
            int dmg = (int)std::ceil(p.fallDistance - 3.f);
            if (dmg > 0) { p.damageSource = 2; hurtPlayer(p, dmg, ev, true); }
        }
        p.fallDistance = 0.f;
    } else if (moved.y < 0.f) {
        p.fallDistance -= moved.y;
    }

    float targetEye = p.sneaking ? 0.08f : 0.f;
    p.eyeOffset += (targetEye - p.eyeOffset) * 0.5f;

    if (p.pos.y < -64.f) { p.damageSource = -1; hurtPlayer(p, 4, ev, true); } // пустота под миром

    // Кактус колется при касании
    {
        const float e = 0.05f;
        int x0 = (int)std::floor(p.pos.x - PLAYER_HALF_W - e), x1 = (int)std::floor(p.pos.x + PLAYER_HALF_W + e);
        int y0 = (int)std::floor(p.pos.y - e), y1 = (int)std::floor(p.pos.y + PLAYER_H);
        int z0 = (int)std::floor(p.pos.z - PLAYER_HALF_W - e), z1 = (int)std::floor(p.pos.z + PLAYER_HALF_W + e);
        bool cactus = false;
        for (int y = y0; y <= y1 && !cactus; ++y)
            for (int z = z0; z <= z1 && !cactus; ++z)
                for (int x = x0; x <= x1 && !cactus; ++x) cactus = w.getBlock(x, y, z) == CACTUS;
        if (cactus) hurtPlayer(p, 1, ev);
    }

    if (p.creative()) { p.air = 300; p.fireTicks = 0; return; }

    // ---- Воздух под водой: 15 секунд, затем 2 урона в секунду
    static uint32_t airRng = 0x9E3779B1u;
    airRng ^= airRng << 13; airRng ^= airRng >> 17; airRng ^= airRng << 5;
    if (p.eyeInWater && p.respiration > 0 && airRng % (uint32_t)(p.respiration + 1) > 0) {
        // Подводное дыхание: воздух тратится реже
    } else if (p.eyeInWater) {
        if (--p.air <= -20) {
            p.air = 0;
            p.invulnerable = 0;
            p.damageSource = -1;
            hurtPlayer(p, 2, ev, true);
        }
    } else {
        p.air = 300;
    }

    // ---- Удушье в блоке
    glm::vec3 e2 = p.eye();
    uint8_t atEye = w.getBlock((int)std::floor(e2.x), (int)std::floor(e2.y), (int)std::floor(e2.z));
    if (isOpaque(atEye) && isSolid(atEye)) { p.damageSource = -1; hurtPlayer(p, 1, ev, true); }

    // ---- Лава и огонь
    bool fireRes = p.hasEffect(3);
    if (p.inLava) {
        if (!fireRes) { p.damageSource = 1; hurtPlayer(p, 4, ev); }
        p.fireTicks = 300;
    }
    if (bodyInLiquid(w, p.pos, FIRE)) {
        if (!fireRes) { p.damageSource = 1; hurtPlayer(p, 1, ev); }
        p.fireTicks = std::max(p.fireTicks, 160);
    }
    if (p.fireTicks > 0) {
        if (p.inWater) p.fireTicks = 0;
        else if (--p.fireTicks % 20 == 0 && !fireRes) { p.invulnerable = 0; p.damageSource = 1; hurtPlayer(p, 1, ev, true); }
    }

    // ---- Яд
    if (p.poisonTicks > 0) {
        if (p.poisonTicks % 25 == 0 && p.health > 1) { p.invulnerable = 0; p.damageSource = -1; hurtPlayer(p, 1, ev, true); }
        --p.poisonTicks;
    }
    // ---- Эффекты зелий: регенерация, яд, иссушение, отсчёт времени
    for (auto& e : p.effects) {
        if (e.id == 1 && (e.ticks % std::max(1, 50 >> e.amp)) == 0 && p.health < 20) ++p.health;
        if (e.id == 4 && (e.ticks % std::max(1, 25 >> e.amp)) == 0 && p.health > 1) { p.invulnerable = 0; p.damageSource = -1; hurtPlayer(p, 1, ev, true); }
        if (e.id == 20 && (e.ticks % std::max(1, 40 >> e.amp)) == 0) { p.invulnerable = 0; p.damageSource = -1; hurtPlayer(p, 1, ev, true); } // Иссушение может убить
        if (e.id == 17) addExhaustion(p, 0.025f * (e.amp + 1)); // Голод (сырая курица, гнилая плоть)
        --e.ticks;
    }
    p.effects.erase(std::remove_if(p.effects.begin(), p.effects.end(), [](const ActiveEffect& e) { return e.ticks <= 0; }), p.effects.end());

    // ---- Голод (FoodStats)
    if (p.exhaustion > 4.f) {
        p.exhaustion -= 4.f;
        if (p.saturation > 0.f) p.saturation = std::max(p.saturation - 1.f, 0.f);
        else p.food = std::max(p.food - 1, 0);
    }
    if (p.food >= 18 && p.health < 20) {
        if (++p.foodTimer >= 80) {
            p.health = std::min(20, p.health + 1);
            addExhaustion(p, 3.f);
            p.foodTimer = 0;
        }
    } else if (p.food <= 0) {
        if (++p.foodTimer >= 80) {
            if (p.health > p.starveFloor) { p.invulnerable = 0; p.damageSource = -1; hurtPlayer(p, 1, ev, true); }
            p.foodTimer = 0;
        }
    } else {
        p.foodTimer = 0;
    }
}
