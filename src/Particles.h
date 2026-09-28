#pragma once
// Частицы как EntityFX в Minecraft 1.0: осколки блоков, дым и огонь факелов, взрывы, капли дождя, лава.
// Обновляются по тикам. Спрайты — клетки 8x8 в particles.png с номерами как в 1.0.
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <random>
#include <vector>
#include "World.h"

enum class PType { Digging, Smoke, Flame, Explode, Rain, Lava, Crit, ItemCrack, Note, Portal, LargeExplode, Spell, COUNT };

struct Particle {
    PType type;
    glm::vec3 pos, prev, vel;
    int age, maxAge;
    float scale;           // particleScale: половина размера квадрата = 0.1 * scale
    glm::vec3 color{1.f};  // particleRed/Green/Blue
    int tex = 0;           // номер спрайта в particles.png (для осколков не используется)
    float u = 0, v = 0;    // осколки: угол кусочка тайла в terrain.png (ItemCrack — в items.png)
    bool onGround = false;
    glm::vec3 origin{0.f}; // частица портала: точка, к которой она летит (EntityPortalFX)
};

inline float rnd() {
    static std::mt19937 rng(12345);
    return std::uniform_real_distribution<float>(0.f, 1.f)(rng);
}

// Общая часть конструктора EntityFX: случайный разлёт с нормировкой скорости
inline Particle makeParticle(PType t, glm::vec3 pos, glm::vec3 speed) {
    Particle p;
    p.type = t;
    p.pos = p.prev = pos;
    glm::vec3 m = speed + glm::vec3(rnd() * 2 - 1, rnd() * 2 - 1, rnd() * 2 - 1) * 0.4f;
    float f = (rnd() + rnd() + 1.f) * 0.15f;
    float len = std::max(glm::length(m), 1e-4f);
    p.vel = m / len * f * 0.4f;
    p.vel.y += 0.1f;
    p.scale = (rnd() * 0.5f + 0.5f) * 2.f;
    p.age = 0;
    p.maxAge = (int)(4.f / (rnd() * 0.9f + 0.1f));
    return p;
}

// Разрушение блока (addBlockDestroyEffects): 4x4x4 осколка с текстуры нижней грани
inline void spawnBreakParticles(std::vector<Particle>& ps, const glm::ivec3& b, uint8_t block, uint8_t meta = 0) {
    int tex = blockTex(block, 3, meta);
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 4; ++k) {
                glm::vec3 pos = glm::vec3(b) + glm::vec3((i + 0.5f) / 4, (j + 0.5f) / 4, (k + 0.5f) / 4);
                Particle p = makeParticle(PType::Digging, pos, pos - glm::vec3(b) - 0.5f);
                p.color = glm::vec3(0.6f);
                p.scale /= 2.f;
                // Кусочек 4x4 пикселя со случайным сдвигом внутри тайла
                p.u = ((tex % 16) + rnd() * 3.f / 4.f) / 16.f;
                p.v = ((tex / 16) + rnd() * 3.f / 4.f) / 16.f;
                ps.push_back(p);
            }
}

// Удар по блоку во время копания (addBlockHitEffects): один осколок с грани
inline void spawnHitParticle(std::vector<Particle>& ps, const glm::vec3& pos, uint8_t block, uint8_t meta = 0) {
    int tex = blockTex(block, 3, meta);
    Particle p = makeParticle(PType::Digging, pos, glm::vec3(0.f));
    p.vel *= glm::vec3(0.2f, 0.6f, 0.2f);
    p.color = glm::vec3(0.6f);
    p.scale *= 0.6f / 2.f;
    p.u = ((tex % 16) + rnd() * 3.f / 4.f) / 16.f;
    p.v = ((tex / 16) + rnd() * 3.f / 4.f) / 16.f;
    ps.push_back(p);
}

// Осколки предмета (EntityBreakingFX, «snowballpoof»): кусочки иконки из items.png
inline void spawnItemCrack(std::vector<Particle>& ps, const glm::vec3& pos, int col, int row, int count = 8) {
    for (int i = 0; i < count; ++i) {
        Particle p = makeParticle(PType::ItemCrack, pos, glm::vec3(0.f));
        p.scale /= 2.f;
        p.u = (col + rnd() * 3.f / 4.f) / 16.f;
        p.v = (row + rnd() * 3.f / 4.f) / 16.f;
        ps.push_back(p);
    }
}

// Нота над нотным блоком (EntityNoteFX): цвет по высоте тона 0..24
inline void spawnNote(std::vector<Particle>& ps, const glm::vec3& pos, int note) {
    Particle p = makeParticle(PType::Note, pos, glm::vec3(0.f));
    p.vel = glm::vec3(0.f, 0.2f, 0.f) * 0.01f;
    float f = note / 24.f * 6.2831853f;
    p.color = glm::vec3(std::sin(f) * 0.65f + 0.35f, std::sin(f + 2.0943951f) * 0.65f + 0.35f, std::sin(f + 4.1887902f) * 0.65f + 0.35f);
    p.color = glm::clamp(p.color, 0.f, 1.f);
    p.scale *= 0.75f * 2.f;
    p.maxAge = 6;
    p.tex = 64;
    ps.push_back(p);
}

// Сердечко (размножение, приручение): как нота, но красное и поднимается
inline void spawnHeart(std::vector<Particle>& ps, const glm::vec3& pos) {
    Particle p = makeParticle(PType::Note, pos, glm::vec3(0.f));
    p.vel = glm::vec3(0.f, 0.02f, 0.f);
    p.color = glm::vec3(1.f);
    p.scale *= 1.5f;
    p.maxAge = 16;
    p.tex = 80;
    ps.push_back(p);
}

// Дым факела (EntitySmokeFX) или огонёк (EntityFlameFX)
inline void spawnSmoke(std::vector<Particle>& ps, glm::vec3 pos, bool flame) {
    if (flame) {
        Particle p = makeParticle(PType::Flame, pos, glm::vec3(0.f));
        p.vel *= 0.01f;
        p.maxAge = (int)(8.f / (rnd() * 0.8f + 0.2f)) + 4;
        p.tex = 48;
        ps.push_back(p);
    } else {
        Particle p = makeParticle(PType::Smoke, pos, glm::vec3(0.f));
        p.vel *= 0.1f;
        float c = rnd() * 0.3f;
        p.color = glm::vec3(c);
        p.scale *= 0.75f;
        p.maxAge = (int)(8.f / (rnd() * 0.8f + 0.2f));
        ps.push_back(p);
    }
}

// Облачко взрыва (EntityExplodeFX): смерть моба, взрыв крипера. count частиц в кубе spread
inline void spawnPoof(std::vector<Particle>& ps, glm::vec3 center, int count, float spread, float = 0.f) {
    for (int i = 0; i < count; ++i) {
        glm::vec3 pos = center + glm::vec3(rnd() - 0.5f, rnd() - 0.5f, rnd() - 0.5f) * spread;
        Particle p;
        p.type = PType::Explode;
        p.pos = p.prev = pos;
        p.vel = glm::vec3(rnd() * 2 - 1, rnd() * 2 - 1, rnd() * 2 - 1) * 0.05f;
        p.color = glm::vec3(rnd() * 0.3f + 0.7f);
        p.scale = rnd() * rnd() * 6.f + 1.f;
        p.age = 0;
        p.maxAge = (int)(16.f / (rnd() * 0.8f + 0.2f)) + 2;
        ps.push_back(p);
    }
}

// Брызги дождя (EntityRainFX): капля подпрыгивает и падает, спрайты 19..22
inline void spawnSplash(std::vector<Particle>& ps, glm::vec3 pos) {
    Particle p = makeParticle(PType::Rain, pos, glm::vec3(0.f));
    p.vel.x *= 0.3f;
    p.vel.z *= 0.3f;
    p.vel.y = rnd() * 0.2f + 0.1f;
    p.tex = 19 + (int)(rnd() * 4);
    p.maxAge = (int)(8.f / (rnd() * 0.8f + 0.2f));
    ps.push_back(p);
}

// Искра лавы (EntityLavaFX), спрайт 49
inline void spawnLava(std::vector<Particle>& ps, glm::vec3 pos) {
    Particle p = makeParticle(PType::Lava, pos, glm::vec3(0.f));
    p.vel *= 0.8f;
    p.vel.y = rnd() * 0.4f + 0.05f;
    p.scale *= rnd() * 2.f + 0.2f;
    p.maxAge = (int)(16.f / (rnd() * 0.8f + 0.2f));
    p.tex = 49;
    ps.push_back(p);
}

// Критический удар (EntityCrit2FX), спрайт 65
inline void spawnCrit(std::vector<Particle>& ps, glm::vec3 center, float spread) {
    for (int i = 0; i < 16; ++i) {
        glm::vec3 d(rnd() * 2 - 1, rnd() * 2 - 1, rnd() * 2 - 1);
        if (glm::length(d) > 1.f) continue;
        Particle p = makeParticle(PType::Crit, center + d * spread * 0.5f, d);
        p.vel = d * 0.25f + glm::vec3(0, 0.1f, 0);
        float c = rnd() * 0.3f + 0.6f;
        p.color = glm::vec3(c, c * 0.9f, c * 0.6f);
        p.scale *= 0.75f;
        p.maxAge = (int)(6.f / (rnd() * 0.8f + 0.6f));
        p.tex = 65;
        ps.push_back(p);
    }
}

// Частица портала (EntityPortalFX 1.0): вылетает из точки pos на motion и возвращается к ней по дуге,
// фиолетовая, руны 225..250, живёт 40..50 тиков
inline void spawnPortalParticle(std::vector<Particle>& ps, glm::vec3 pos, glm::vec3 motion) {
    Particle p = makeParticle(PType::Portal, pos, glm::vec3(0.f));
    p.origin = pos;
    p.vel = motion;
    float f = rnd() * 0.6f + 0.4f;
    p.color = glm::vec3(f * 0.9f, f * 0.3f, f);
    p.scale = rnd() * 0.2f + 0.5f;
    p.maxAge = (int)(rnd() * 10.f) + 40;
    p.tex = (int)(rnd() * 8.f);
    ps.push_back(p);
}

// Зелье / заклинание (EntitySpellParticleFX): завихрение 144..151
inline void spawnSpell(std::vector<Particle>& ps, glm::vec3 pos, glm::vec3 color, glm::vec3 speed = glm::vec3(0.f)) {
    Particle p = makeParticle(PType::Spell, pos, speed);
    p.color = color;
    p.scale *= 0.8f;
    p.tex = 144;
    p.maxAge = (int)(8.f / (rnd() * 0.8f + 0.2f)) + 2;
    ps.push_back(p);
}

// Большой взрыв (EntityLargeExplodeFX 1.0): 16 кадров из misc/explosion.png (4x4), серый, 6..9 тиков, не двигается
inline void spawnLargeExplode(std::vector<Particle>& ps, glm::vec3 pos, float size = 1.f) {
    Particle p = makeParticle(PType::LargeExplode, pos, glm::vec3(0.f));
    p.vel = glm::vec3(0.f);
    float c = rnd() * 0.6f + 0.4f;
    p.color = glm::vec3(c);
    p.scale = 2.f * (1.f - size * 0.5f) * 10.f; // halfSize = 0.1 * scale — около 2 блоков
    p.maxAge = 6 + (int)(rnd() * 4.f);
    ps.push_back(p);
}

// Спрайт и размер частицы в момент отрисовки
inline void particleSprite(const Particle& p, float partial, int& tex, float& halfSize) {
    float life = (p.age + partial) / std::max(1, p.maxAge);
    tex = p.tex;
    halfSize = 0.1f * p.scale;
    switch (p.type) {
    case PType::Smoke:
        tex = 7 - std::clamp((int)(p.age * 8 / std::max(1, p.maxAge)), 0, 7);
        halfSize *= std::clamp(life * 32.f, 0.f, 1.f);
        break;
    case PType::Explode:
        tex = 7 - std::clamp((int)(p.age * 8 / std::max(1, p.maxAge)), 0, 7);
        break;
    case PType::Flame: halfSize *= 1.f - life * life * 0.5f; break;
    case PType::Lava: halfSize *= 1.f - life * life; break;
    case PType::Crit: halfSize *= std::clamp(life * 32.f, 0.f, 1.f); break;
    case PType::LargeExplode: tex = std::clamp((int)((p.age + partial) * 15.f / p.maxAge), 0, 15); break;
    case PType::Spell:
        tex = 144 + std::clamp((int)(p.age * 8 / std::max(1, p.maxAge)), 0, 7);
        halfSize *= std::clamp(life * 32.f, 0.f, 1.f);
        break;
    case PType::Portal: {
        float f = 1.f - life;
        f *= f;
        halfSize *= 1.f - f;
        break;
    }
    default: break;
    }
}

inline void tickParticles(std::vector<Particle>& ps, const World& w) {
    auto solid = [&](const glm::vec3& q) {
        return isSolid(w.getBlock((int)std::floor(q.x), (int)std::floor(q.y), (int)std::floor(q.z)));
    };
    for (auto& p : ps) {
        p.prev = p.pos;
        if (++p.age >= p.maxAge) continue;
        if (p.type == PType::LargeExplode) continue; // только анимация кадров
        if (p.type == PType::Portal) {
            // Позиция считается от точки вылета: наружу и обратно, чуть вверх
            float f = (float)p.age / p.maxAge, f1 = f;
            f = -f + f * f * 2.f;
            f = 1.f - f;
            p.pos = p.origin + p.vel * f;
            p.pos.y += 1.f - f1;
            continue;
        }
        switch (p.type) {
        case PType::Digging: case PType::ItemCrack: p.vel.y -= 0.04f; break;
        case PType::Rain: p.vel.y -= 0.06f; break;
        case PType::Lava: p.vel.y -= 0.03f; break;
        case PType::Smoke: case PType::Explode: case PType::Spell: p.vel.y += 0.004f; break;
        case PType::Crit: p.vel.y -= 0.02f; break;
        case PType::Note: p.vel *= 0.66f; break;
        default: break;
        }
        // Движение с остановкой о блоки по осям (огонь проходит сквозь всё)
        p.onGround = false;
        for (int a : {1, 0, 2}) {
            glm::vec3 next = p.pos;
            next[a] += p.vel[a];
            if (p.type != PType::Flame && solid(next)) {
                if (a == 1 && p.vel.y < 0) p.onGround = true;
                p.vel[a] = 0.f;
            } else {
                p.pos = next;
            }
        }
        if (p.type == PType::Smoke && p.pos.y == p.prev.y) { p.vel.x *= 1.1f; p.vel.z *= 1.1f; }
        float drag = p.type == PType::Explode ? 0.9f : (p.type == PType::Smoke || p.type == PType::Flame || p.type == PType::Crit || p.type == PType::Spell) ? 0.96f
                   : p.type == PType::Lava ? 0.999f : 0.98f;
        p.vel *= drag;
        if (p.onGround) {
            p.vel.x *= 0.7f;
            p.vel.z *= 0.7f;
            if (p.type == PType::Rain && rnd() < 0.5f) p.age = p.maxAge;
        }
    }
    ps.erase(std::remove_if(ps.begin(), ps.end(), [](const Particle& p) { return p.age >= p.maxAge; }), ps.end());
}
