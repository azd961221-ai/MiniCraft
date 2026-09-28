#include "Entity.h"
#include <algorithm>
#include <cmath>
#include "Physics.h"

static float frand(uint32_t& s) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return (s & 0xFFFFFF) / float(0xFFFFFF);
}

void dropFromBlock(std::vector<ItemEntity>& items, const glm::ivec3& b, const ItemStack& stack, uint32_t& rng) {
    if (stack.empty()) return;
    ItemEntity e;
    e.pos = glm::vec3(b) + glm::vec3(frand(rng) * 0.7f + 0.15f, frand(rng) * 0.7f + 0.15f, frand(rng) * 0.7f + 0.15f);
    e.prev = e.pos;
    e.motion = glm::vec3(frand(rng) * 0.2f - 0.1f, 0.2f, frand(rng) * 0.2f - 0.1f);
    e.stack = stack;
    e.bobOffset = frand(rng) * 6.28f;
    items.push_back(e);
}

void throwFromPlayer(std::vector<ItemEntity>& items, const glm::vec3& eye, const glm::vec3& look, const ItemStack& stack, uint32_t& rng) {
    if (stack.empty()) return;
    ItemEntity e;
    e.pos = eye - glm::vec3(0, 0.3f, 0);
    e.prev = e.pos;
    e.motion = look * 0.3f + glm::vec3(0, 0.1f, 0);
    float a = frand(rng) * 6.2832f, f = 0.02f * frand(rng);
    e.motion += glm::vec3(std::cos(a) * f, (frand(rng) - frand(rng)) * 0.1f, std::sin(a) * f);
    e.stack = stack;
    e.pickupDelay = 40;
    e.bobOffset = frand(rng) * 6.28f;
    items.push_back(e);
}

const PaintingArt PAINTING_ARTS[PAINTING_ART_COUNT] = {
    {"Kebab", 16, 16, 0, 0},       {"Aztec", 16, 16, 16, 0},      {"Alban", 16, 16, 32, 0},
    {"Aztec2", 16, 16, 48, 0},     {"Bomb", 16, 16, 64, 0},       {"Plant", 16, 16, 80, 0},
    {"Wasteland", 16, 16, 96, 0},  {"Pool", 32, 16, 0, 32},       {"Courbet", 32, 16, 32, 32},
    {"Sea", 32, 16, 64, 32},       {"Sunset", 32, 16, 96, 32},    {"Creebet", 32, 16, 128, 32},
    {"Wanderer", 16, 32, 0, 64},   {"Graham", 16, 32, 16, 64},    {"Match", 32, 32, 0, 128},
    {"Bust", 32, 32, 32, 128},     {"Stage", 32, 32, 64, 128},    {"Void", 32, 32, 96, 128},
    {"SkullAndRoses", 32, 32, 128, 128}, {"Fighters", 64, 32, 0, 96}, {"Pointer", 64, 64, 0, 192},
    {"Pigscene", 64, 64, 64, 192}, {"BurningSkull", 64, 64, 128, 192}, {"Skeleton", 64, 48, 192, 64},
    {"DonkeyKong", 64, 48, 192, 112},
};

namespace {
const glm::vec3 PAINT_N[4] = {{0, 0, -1}, {-1, 0, 0}, {0, 0, 1}, {1, 0, 0}};
float paintOff(int size) { return (size == 32 || size == 64) ? 0.5f : 0.f; } // func_411_c: чётные размеры сдвинуты на полблока
} // namespace

glm::vec3 Painting::normal() const { return PAINT_N[dir & 3]; }
glm::vec3 Painting::right() const { return glm::cross(-normal(), glm::vec3(0, 1, 0)); }
glm::vec3 Painting::center() const {
    const PaintingArt& a = PAINTING_ARTS[art];
    return glm::vec3(wall) + 0.5f + normal() * 0.5625f + right() * paintOff(a.w) + glm::vec3(0, paintOff(a.h), 0);
}
void Painting::bounds(glm::vec3& mn, glm::vec3& mx) const {
    const PaintingArt& a = PAINTING_ARTS[art];
    glm::vec3 e = glm::abs(right()) * (a.w / 32.f - 0.00625f) + glm::vec3(0, a.h / 32.f - 0.00625f, 0) +
                  glm::abs(normal()) * (1.f / 32.f - 0.00625f);
    glm::vec3 c = center();
    mn = c - e;
    mx = c + e;
}

bool paintingFits(const Painting& p, const World& w, const std::vector<Painting>& others) {
    glm::vec3 mn, mx;
    p.bounds(mn, mx);
    if (anyCollision(w, AABB{mn, mx})) return false;
    const PaintingArt& a = PAINTING_ARTS[p.art];
    glm::vec3 r = p.right(), c = p.center() - p.normal() * 0.5625f; // плоскость стены (центры её блоков)
    glm::vec3 corner = c - r * (a.w / 32.f) - glm::vec3(0, a.h / 32.f, 0);
    for (int i = 0; i < a.w / 16; ++i)
        for (int j = 0; j < a.h / 16; ++j) {
            glm::vec3 q = corner + r * (i + 0.5f) + glm::vec3(0, j + 0.5f, 0);
            uint8_t b = w.getBlock((int)std::floor(q.x), (int)std::floor(q.y), (int)std::floor(q.z));
            if (!isSolid(b)) return false;
        }
    for (const Painting& o : others) {
        if (o.dead || &o == &p) continue;
        glm::vec3 omn, omx;
        o.bounds(omn, omx);
        if (AABB{mn, mx}.overlaps(AABB{omn, omx})) return false;
    }
    return true;
}

bool placePainting(std::vector<Painting>& ps, const World& w, const glm::ivec3& wall, const glm::ivec3& face, uint32_t& rng) {
    if (face.y != 0) return false;
    Painting p;
    p.wall = wall;
    p.dir = face.z < 0 ? 0 : face.x < 0 ? 1 : face.z > 0 ? 2 : 3;
    std::vector<int> ok;
    for (int i = 0; i < PAINTING_ART_COUNT; ++i) {
        p.art = i;
        if (paintingFits(p, w, ps)) ok.push_back(i);
    }
    if (ok.empty()) return false;
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    p.art = ok[rng % ok.size()];
    ps.push_back(p);
    return true;
}

int xpSplit(int xp) {
    static const int S[] = {2477, 1237, 617, 307, 149, 73, 37, 17, 7, 3};
    for (int s : S)
        if (xp >= s) return s;
    return 1;
}

int xpOrbIcon(int value) {
    static const int S[] = {2477, 1237, 617, 307, 149, 73, 37, 17, 7, 3};
    for (int i = 0; i < 10; ++i)
        if (value >= S[i]) return 10 - i;
    return 0;
}

namespace {
float orbRnd(uint32_t& rng) {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (rng & 0xFFFFFF) / float(0x1000000);
}
} // namespace

void spawnXpOrbs(std::vector<XpOrb>& orbs, const glm::vec3& at, int xp, uint32_t& rng) {
    while (xp > 0) {
        XpOrb o;
        o.value = xpSplit(xp);
        xp -= o.value;
        o.pos = o.prev = at;
        o.motion = glm::vec3((orbRnd(rng) * 0.2f - 0.1f) * 2.f, orbRnd(rng) * 0.2f * 2.f, (orbRnd(rng) * 0.2f - 0.1f) * 2.f);
        o.color = (int)(orbRnd(rng) * 60.f);
        orbs.push_back(o);
    }
}

bool moveXpOrb(XpOrb& o, const World& w, const glm::vec3* target, uint32_t& rng) {
    const float r = 0.25f; // шар 0.5 x 0.5
    o.prev = o.pos;
    o.motion.y -= 0.03f;
    uint8_t in = w.getBlock((int)std::floor(o.pos.x), (int)std::floor(o.pos.y), (int)std::floor(o.pos.z));
    if (in == LAVA) {
        // В лаве подпрыгивает и шипит; через пару прыжков сгорает
        o.motion.y = 0.2f;
        o.motion.x = (orbRnd(rng) - orbRnd(rng)) * 0.2f;
        o.motion.z = (orbRnd(rng) - orbRnd(rng)) * 0.2f;
        o.age += 1000;
    }
    if (anyCollision(w, bodyBox(o.pos, r, 2 * r))) {
        o.pos.y = std::floor(o.pos.y + r) + 1.001f;
        o.motion.y = std::max(o.motion.y, 0.f);
    }
    if (target) {
        glm::vec3 d = (*target - (o.pos + glm::vec3(0, r, 0))) / 8.f;
        float len = glm::length(d), k = 1.f - len;
        if (k > 0.f && len > 1e-4f) o.motion += d / len * (k * k * 0.1f);
    }
    BodyState st;
    st.onGround = o.onGround;
    moveBody(w, o.pos, r, 2 * r, o.motion, 0.f, false, st);
    o.onGround = st.onGround;
    float friction = o.onGround ? 0.6f * 0.98f : 0.98f;
    o.motion.x *= friction;
    o.motion.z *= friction;
    o.motion.y *= 0.98f;
    if (o.onGround) o.motion.y *= -0.9f;
    ++o.color;
    if (++o.age >= 6000 || o.pos.y < -64.f) o.dead = true;
    return !(o.dead && in == LAVA);
}

int tickItems(std::vector<ItemEntity>& items, const World& w) {
    int burned = 0;
    const float r = 0.125f; // половина размера предмета
    for (auto& e : items) {
        e.prev = e.pos;
        ++e.age;
        if (e.pickupDelay > 0) --e.pickupDelay;

        // Застрял в блоке — выталкиваем вверх
        if (anyCollision(w, bodyBox(e.pos, r, 2 * r))) {
            e.pos.y = std::floor(e.pos.y + r) + 1.001f;
            e.motion = glm::vec3(0);
        }

        uint8_t in = w.getBlock((int)std::floor(e.pos.x), (int)std::floor(e.pos.y + r), (int)std::floor(e.pos.z));
        if (in == LAVA || in == FIRE) { e.dead = true; ++burned; continue; }
        e.motion.y -= 0.04f;
        if (in == WATER) {
            // Течение сносит предмет (handleWaterMovement 1.0) — работают водяные сборщики
            glm::ivec3 c((int)std::floor(e.pos.x), (int)std::floor(e.pos.y + r), (int)std::floor(e.pos.z));
            glm::vec3 fl = w.flowVector(c.x, c.y, c.z);
            if (glm::length(fl) > 1e-4f) e.motion += glm::normalize(fl) * 0.014f;
            e.motion.y *= 0.8f;
        }

        BodyState st;
        st.onGround = e.onGround;
        moveBody(w, e.pos, r, 2 * r, e.motion, 0.f, false, st);
        e.onGround = st.onGround;

        float friction = e.onGround ? 0.6f * 0.98f : 0.98f;
        e.motion.x *= friction;
        e.motion.z *= friction;
        e.motion.y *= 0.98f;
        if (e.age >= 6000 || e.pos.y < -64.f) e.dead = true; // исчезает через 5 минут или упав в пустоту
    }
    items.erase(std::remove_if(items.begin(), items.end(), [](const ItemEntity& e) { return e.dead; }), items.end());
    return burned;
}
