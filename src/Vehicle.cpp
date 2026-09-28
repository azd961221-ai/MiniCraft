#include "Vehicle.h"
#include <algorithm>
#include <cmath>
#include "Physics.h"
#include "World.h"

void railExits(int shape, glm::vec3& a, glm::vec3& b) {
    switch (shape) {
    case 1: a = {0, 0, 0.5f}; b = {1, 0, 0.5f}; break;
    case 2: a = {0, 0, 0.5f}; b = {1, 1, 0.5f}; break;
    case 3: a = {0, 1, 0.5f}; b = {1, 0, 0.5f}; break;
    case 4: a = {0.5f, 1, 0}; b = {0.5f, 0, 1}; break;
    case 5: a = {0.5f, 0, 0}; b = {0.5f, 1, 1}; break;
    case 6: a = {0.5f, 0, 1}; b = {1, 0, 0.5f}; break;
    case 7: a = {0.5f, 0, 1}; b = {0, 0, 0.5f}; break;
    case 8: a = {0.5f, 0, 0}; b = {0, 0, 0.5f}; break;
    case 9: a = {0.5f, 0, 0}; b = {1, 0, 0.5f}; break;
    default: a = {0.5f, 0, 0}; b = {0.5f, 0, 1}; break;
    }
}

// ---------------------------------------------------------------- Форма рельсов (RailLogic 1.0, упрощённо)

static bool isRailBlock(uint8_t b) { return b == RAIL || b == POWERED_RAIL || b == DETECTOR_RAIL; }

namespace {
const int RDX[4] = {0, 0, 1, -1}, RDZ[4] = {-1, 1, 0, 0}; // север, юг, восток, запад

int railShapeAt(const World& w, glm::ivec3 q) {
    return w.getBlock(q.x, q.y, q.z) == RAIL ? (w.getMeta(q.x, q.y, q.z) & 15) : (w.getMeta(q.x, q.y, q.z) & 7);
}

// Два выхода формы: направление (как RDX/RDZ) и «вверх» (подъём)
void shapeExits(int shape, int dir[2], bool up[2]) {
    glm::vec3 e[2];
    railExits(shape, e[0], e[1]);
    for (int i = 0; i < 2; ++i) {
        dir[i] = e[i].z == 0.f ? 0 : e[i].z == 1.f ? 1 : e[i].x == 1.f ? 2 : 3;
        up[i] = e[i].y > 0.5f;
    }
}

// Рельс за выходом dir: при подъёме — на блок выше, иначе на том же уровне или ниже
bool railThroughExit(const World& w, glm::ivec3 p, int dir, bool upExit, glm::ivec3& out) {
    int nx = p.x + RDX[dir], nz = p.z + RDZ[dir];
    const int ys[2] = {upExit ? 1 : 0, upExit ? 0 : -1};
    for (int dy : ys)
        if (isRailBlock(w.getBlock(nx, p.y + dy, nz))) { out = glm::ivec3(nx, p.y + dy, nz); return true; }
    return false;
}

// Рельс q своей формой смотрит на соседнюю клетку p
bool railLinksTo(const World& w, glm::ivec3 q, glm::ivec3 p) {
    int d[2];
    bool u[2];
    shapeExits(railShapeAt(w, q), d, u);
    for (int i = 0; i < 2; ++i)
        if (q.x + RDX[d[i]] == p.x && q.z + RDZ[d[i]] == p.z && std::abs(p.y - q.y) <= 1) return true;
    return false;
}

// Сколько концов рельса q уже взаимно соединены с другими рельсами (RailLogic.getAdjacentTracks)
int railLinkCount(const World& w, glm::ivec3 q) {
    int d[2];
    bool u[2];
    shapeExits(railShapeAt(w, q), d, u);
    int n = 0;
    for (int i = 0; i < 2; ++i) {
        glm::ivec3 t;
        if (railThroughExit(w, q, d[i], u[i], t) && railLinksTo(w, t, q)) ++n;
    }
    return n;
}
} // namespace

void World::updateRailShape(int x, int y, int z, bool updateNeighbors) {
    uint8_t self = getBlock(x, y, z);
    if (!isRailBlock(self)) return;
    // Соседние рельсы (RailLogic 1.0): на том же уровне, выше (подъём) или ниже. Подключаемся только к соседу,
    // у которого свободен конец или который уже смотрит на нас, — параллельные линии друг за друга не цепляются
    const int* DX = RDX;
    const int* DZ = RDZ;
    const glm::ivec3 me(x, y, z);
    bool has[4] = {}, up[4] = {};
    int ny[4] = {};
    for (int i = 0; i < 4; ++i) {
        int nx = x + DX[i], nz = z + DZ[i];
        for (int dy : {0, 1, -1}) {
            if (!isRailBlock(getBlock(nx, y + dy, nz))) continue;
            glm::ivec3 q(nx, y + dy, nz);
            if (railLinksTo(*this, q, me) || railLinkCount(*this, q) < 2) {
                has[i] = true;
                up[i] = dy == 1;
                ny[i] = y + dy;
            }
            break;
        }
    }
    bool curves = self == RAIL;
    bool powered = inputPower(x, y, z) > 0;
    int shape = getMeta(x, y, z) & (curves ? 15 : 7);
    if (curves && has[0] && has[1] && has[2]) shape = powered ? 9 : (up[0] ? 4 : up[1] ? 5 : 0);
    else if (curves && has[0] && has[1] && has[3]) shape = powered ? 8 : (up[0] ? 4 : up[1] ? 5 : 0);
    else if (curves && has[2] && has[3] && has[0]) shape = powered ? 9 : (up[2] ? 2 : up[3] ? 3 : 1);
    else if (curves && has[2] && has[3] && has[1]) shape = powered ? 6 : (up[2] ? 2 : up[3] ? 3 : 1);
    else if (has[0] && has[1]) shape = up[0] ? 4 : up[1] ? 5 : 0;
    else if (has[2] && has[3]) shape = up[2] ? 2 : up[3] ? 3 : 1;
    else if (curves && has[1] && has[2]) shape = 6;
    else if (curves && has[1] && has[3]) shape = 7;
    else if (curves && has[0] && has[3]) shape = 8;
    else if (curves && has[0] && has[2]) shape = 9;
    else if (has[0] || has[1]) shape = up[0] ? 4 : up[1] ? 5 : 0;
    else if (has[2] || has[3]) shape = up[2] ? 2 : up[3] ? 3 : 1;
    uint8_t m = getMeta(x, y, z);
    uint8_t nm = curves ? (uint8_t)shape : (uint8_t)((m & 8) | shape);
    if (nm != m) setMeta(x, y, z, nm);
    if (updateNeighbors)
        for (int i = 0; i < 4; ++i)
            if (has[i]) updateRailShape(x + DX[i], ny[i], z + DZ[i], false);
}

// ---------------------------------------------------------------- Физика вагонеток и лодок

#include "Mob.h"

void MobManager::tickVehicles(World& w, Player& p, int riding, float forward, float strafe, std::vector<Particle>& ps, std::vector<ItemEntity>& items,
                              uint32_t& rng) {
    std::vector<glm::ivec3> detectors;
    const glm::vec3 look = p.look();
    glm::vec2 lookXZ = glm::length(glm::vec2(look.x, look.z)) > 1e-4f ? glm::normalize(glm::vec2(look.x, look.z)) : glm::vec2(1, 0);
    Player& p0 = p;
    const float forward0 = forward, strafe0 = strafe;
    for (size_t i = 0; i < vehicles.size(); ++i) {
        Vehicle& v = vehicles[i];
        v.prev = v.pos;
        v.prevYaw = v.yaw;
        if (v.hurtTime > 0) --v.hurtTime;
        if (v.damage > 0 && v.hurtTime == 0) --v.damage;
        bool ridden = (int)i == riding;
        // Сервер: у транспорта свой седок со своим управлением
        Player* rp = &p0;
        forward = forward0;
        strafe = strafe0;
        auto ri = riders.find(v.id);
        if (ri != riders.end()) { ridden = true; rp = ri->second.player; forward = ri->second.forward; strafe = ri->second.strafe; }
        Player& p = *rp;
        glm::vec3 look = p.look();
        glm::vec2 lookXZ = glm::length(glm::vec2(look.x, look.z)) > 1e-4f ? glm::normalize(glm::vec2(look.x, look.z)) : glm::vec2(1, 0);

        // Игрок толкает незанятую вагонетку/лодку
        if (!ridden && !p.dead) {
            glm::vec2 d(v.pos.x - p.pos.x, v.pos.z - p.pos.z);
            float dl = glm::length(d);
            if (dl < 1.0f && std::abs(v.pos.y - p.pos.y) < 1.5f && dl > 1e-3f) {
                v.motion.x += d.x / dl * 0.02f;
                v.motion.z += d.y / dl * 0.02f;
            }
        }

        if (v.kind == VehicleKind::Boat) {
            // EntityBoat 1.0: доля корпуса (5 срезов высотой 0.6) в воде
            float frac = 0.f;
            for (int k = 0; k < 5; ++k) {
                float y0 = v.pos.y + 0.6f * k / 5.f - 0.125f, y1 = v.pos.y + 0.6f * (k + 1) / 5.f - 0.125f;
                bool wet = false;
                for (float yy : {y0, y1 - 0.01f})
                    for (float ox : {-0.7f, 0.7f})
                        for (float oz : {-0.7f, 0.7f})
                            wet |= w.getBlock((int)std::floor(v.pos.x + ox), (int)std::floor(yy), (int)std::floor(v.pos.z + oz)) == WATER;
                if (wet) frac += 0.2f;
            }
            float speedBefore = glm::length(glm::vec2(v.motion.x, v.motion.z));
            // Брызги по бортам на ходу
            if (speedBefore > 0.15f && frac > 0.f) {
                rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                glm::vec2 dir = glm::normalize(glm::vec2(v.motion.x, v.motion.z));
                glm::vec2 side(-dir.y, dir.x);
                float sgn = (rng & 1) ? 1.f : -1.f;
                spawnSplash(ps, v.pos + glm::vec3(dir.x * 0.8f + side.x * 0.6f * sgn, 0.5f, dir.y * 0.8f + side.y * 0.6f * sgn));
            }
            if (frac < 1.f) v.motion.y += 0.04f * (frac * 2.f - 1.f); // полупогружена — держится у поверхности
            else {
                if (v.motion.y < 0.f) v.motion.y /= 2.f;               // целиком под водой — медленно всплывает
                v.motion.y += 0.007f;
            }
            if (ridden) {
                // Гребёт игрок: его шаг (вперёд и вбок, ускорение в воздухе 0.02) передаётся лодке с коэффициентом 0.2
                float yr = glm::radians(p.yaw);
                glm::vec2 fw(std::cos(yr), std::sin(yr)), rt(-std::sin(yr), std::cos(yr));
                glm::vec2 mv = fw * forward + rt * strafe;
                if (glm::length(mv) > 1.f) mv = glm::normalize(mv);
                v.motion.x += mv.x * 0.02f * 0.2f;
                v.motion.z += mv.y * 0.02f * 0.2f;
            }
            v.motion.x = std::clamp(v.motion.x, -0.4f, 0.4f);
            v.motion.z = std::clamp(v.motion.z, -0.4f, 0.4f);
            if (v.onGround) v.motion *= 0.5f;
            BodyState st;
            st.onGround = v.onGround;
            moveBody(w, v.pos, 0.75f, 0.6f, v.motion, 0.f, false, st);
            v.onGround = st.onGround;
            if (st.collidedH && speedBefore > 0.2f) {
                // Разбилась о берег: 3 доски и 2 палки
                v.dead = true;
                glm::ivec3 b((int)std::floor(v.pos.x), (int)std::floor(v.pos.y), (int)std::floor(v.pos.z));
                for (int k = 0; k < 3; ++k) dropFromBlock(items, b, makeStack(PLANKS), rng);
                for (int k = 0; k < 2; ++k) dropFromBlock(items, b, makeStack(STICK), rng);
                continue;
            }
            v.motion.x *= 0.99f;
            v.motion.y *= 0.95f;
            v.motion.z *= 0.99f;
            // Нос разворачивается по ходу, не больше 20° за тик
            glm::vec2 moved(v.pos.x - v.prev.x, v.pos.z - v.prev.z);
            if (glm::dot(moved, moved) > 0.001f) {
                float want = glm::degrees(std::atan2(moved.y, moved.x));
                float dy = want - v.yaw;
                while (dy > 180.f) dy -= 360.f;
                while (dy < -180.f) dy += 360.f;
                v.yaw += std::clamp(dy, -20.f, 20.f);
            }
            // Лодка сминает снег вокруг себя
            for (int k = 0; k < 4; ++k) {
                int sx = (int)std::floor(v.pos.x + ((k % 2) * 0.8f - 0.4f)), sz = (int)std::floor(v.pos.z + ((k / 2) * 0.8f - 0.4f));
                int sy = (int)std::floor(v.pos.y);
                if (w.getBlock(sx, sy, sz) == SNOW_LAYER) w.setBlock(sx, sy, sz, AIR);
            }
            if (v.pos.y < -64.f) v.dead = true;
            continue;
        } else {
            glm::ivec3 c((int)std::floor(v.pos.x), (int)std::floor(v.pos.y + 0.01f), (int)std::floor(v.pos.z));
            uint8_t b = w.getBlock(c.x, c.y, c.z);
            if (!isRailBlock(b)) {
                if (isRailBlock(w.getBlock(c.x, c.y + 1, c.z))) { ++c.y; b = w.getBlock(c.x, c.y, c.z); }
                else if (isRailBlock(w.getBlock(c.x, c.y - 1, c.z))) { --c.y; b = w.getBlock(c.x, c.y, c.z); }
            }
            v.onRail = isRailBlock(b);
            if (v.onRail) {
                uint8_t meta = w.getMeta(c.x, c.y, c.z);
                int shape = b == RAIL ? meta : (meta & 7);
                glm::vec3 A, B;
                railExits(shape, A, B);
                A += glm::vec3(c);
                B += glm::vec3(c);
                glm::vec3 L = B - A;
                glm::vec3 Ln = glm::normalize(L);
                float spd = glm::dot(v.motion, Ln);
                float mag = glm::length(glm::vec2(v.motion.x, v.motion.z));
                if (mag > 0.01f && std::abs(spd) < mag) {
                    float sgn;
                    if (spd > 1e-5f) {
                        sgn = 1.f;
                    } else if (spd < -1e-5f) {
                        sgn = -1.f;
                    } else {
                        // Скорость перпендикулярна рельсу (переход с прямого на кривой):
                        // знак из dot-произведения ненадёжен — используем позицию вдоль сегмента.
                        // t < 0.5 — вагонетка ближе к A → едем к B (положительно);
                        // t >= 0.5 — ближе к B → едем к A (отрицательно).
                        float t0 = glm::dot(v.pos - A, L) / glm::dot(L, L);
                        sgn = (t0 < 0.5f) ? 1.f : -1.f;
                    }
                    spd = sgn * mag;
                }
                if (L.y != 0.f) spd += (L.y > 0.f ? -1.f : 1.f) * 0.0078125f * 1.5f; // скатывается под уклон
                if (ridden && forward != 0.f) spd += glm::dot(glm::vec2(Ln.x, Ln.z), lookXZ) * forward * 0.035f;
                if (b == POWERED_RAIL) {
                    if (meta & 8) {
                        if (std::abs(spd) > 0.01f) spd += (spd > 0.f ? 0.06f : -0.06f);
                        else {
                            glm::vec3 behindA = A - glm::vec3(Ln.x, 0, Ln.z) * 0.5f, behindB = B + glm::vec3(Ln.x, 0, Ln.z) * 0.5f;
                            if (isSolid(w.getBlock((int)std::floor(behindA.x), c.y, (int)std::floor(behindA.z)))) spd = 0.02f;
                            else if (isSolid(w.getBlock((int)std::floor(behindB.x), c.y, (int)std::floor(behindB.z)))) spd = -0.02f;
                        }
                    } else {
                        spd *= 0.5f;
                        if (std::abs(spd) < 0.03f) spd = 0.f;
                    }
                }
                if (v.kind == VehicleKind::FurnaceCart && v.fuel > 0) {
                    --v.fuel;
                    float dir = glm::dot(glm::vec2(Ln.x, Ln.z), v.push);
                    if (std::abs(dir) > 0.1f) spd += (dir > 0.f ? 1.f : -1.f) * 0.04f;
                    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                    if (rng % 4 == 0) spawnSmoke(ps, v.pos + glm::vec3(0, 0.8f, 0), false);
                }
                spd *= ridden ? 0.997f : 0.96f;
                spd = std::clamp(spd, -0.4f, 0.4f);
                float t = glm::dot(v.pos - A, L) / glm::dot(L, L);
                float tl = glm::length(L);
                glm::vec3 np = A + L * (t + spd / tl);
                v.pos = np;
                v.motion = Ln * spd;
                if (b == DETECTOR_RAIL) detectors.push_back(c);
            } else {
                v.motion.y -= 0.04f;
                BodyState st;
                st.onGround = v.onGround;
                moveBody(w, v.pos, 0.49f, 0.7f, v.motion, 0.f, false, st);
                v.onGround = st.onGround;
                float fr = v.onGround ? 0.5f : 0.95f;
                v.motion.x *= fr;
                v.motion.z *= fr;
            }
        }
        float hsp = glm::length(glm::vec2(v.motion.x, v.motion.z));
        if (hsp > 0.01f) {
            float want = glm::degrees(std::atan2(v.motion.z, v.motion.x));
            float dy = want - v.yaw;
            while (dy > 180.f) dy -= 360.f;
            while (dy < -180.f) dy += 360.f;
            if (v.kind != VehicleKind::Boat && std::abs(dy) > 90.f) dy += dy > 0 ? -180.f : 180.f; // вагонетке всё равно, куда «передом»
            v.yaw += dy * (v.kind == VehicleKind::Boat ? 0.2f : 1.f);
        }
        if (v.pos.y < -64.f) v.dead = true;
    }
    vehicles.erase(std::remove_if(vehicles.begin(), vehicles.end(), [](const Vehicle& v) { return v.dead; }), vehicles.end());

    // Детекторные рельсы: нажаты, пока на них вагонетка
    for (auto it = pressedDetectors.begin(); it != pressedDetectors.end();) {
        bool still = std::find(detectors.begin(), detectors.end(), *it) != detectors.end();
        if (!still) {
            glm::ivec3 d = *it;
            if (w.getBlock(d.x, d.y, d.z) == DETECTOR_RAIL) {
                w.setMeta(d.x, d.y, d.z, (uint8_t)(w.getMeta(d.x, d.y, d.z) & 7));
                w.redstoneChanged(d.x, d.y, d.z);
                w.redstoneChanged(d.x, d.y - 1, d.z);
            }
            it = pressedDetectors.erase(it);
        } else {
            ++it;
        }
    }
    for (auto& d : detectors)
        if (std::find(pressedDetectors.begin(), pressedDetectors.end(), d) == pressedDetectors.end()) {
            pressedDetectors.push_back(d);
            w.setMeta(d.x, d.y, d.z, (uint8_t)(w.getMeta(d.x, d.y, d.z) | 8));
            w.redstoneChanged(d.x, d.y, d.z);
            w.redstoneChanged(d.x, d.y - 1, d.z);
        }
}
