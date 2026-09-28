#pragma once
// Столкновения по коробкам (AABB), как getCollisionBoundingBox и Entity.moveEntity в Minecraft 1.0:
// полублоки, ступеньки, двери, заборы высотой 1.5 и подъём на ступеньку высотой 0.5.
#include <glm/glm.hpp>
#include <vector>
#include "World.h"

struct AABB {
    glm::vec3 mn{0.f}, mx{0.f};
    bool overlaps(const AABB& o) const {
        return mn.x < o.mx.x && mx.x > o.mn.x && mn.y < o.mx.y && mx.y > o.mn.y && mn.z < o.mx.z && mx.z > o.mn.z;
    }
    bool contains(const glm::vec3& p) const {
        return p.x > mn.x && p.x < mx.x && p.y > mn.y && p.y < mx.y && p.z > mn.z && p.z < mx.z;
    }
};

inline AABB bodyBox(const glm::vec3& feet, float hw, float h) {
    return {feet - glm::vec3(hw, 0.f, hw), feet + glm::vec3(hw, h, hw)};
}

// Коробки столкновений блока в мировых координатах (добавляются в out)
void blockCollision(const World& w, int x, int y, int z, std::vector<AABB>& out);
// Все коробки блоков, задевающие область
void collectCollision(const World& w, const AABB& region, std::vector<AABB>& out);
bool anyCollision(const World& w, const AABB& box);
// Коробки выделения блока в локальных координатах [0,1]^3: по форме (ступеньки, панели, калитка...),
// для остальных — границы blockBounds. Для рамки и луча выбора
void selectionBoxes(const World& w, int x, int y, int z, std::vector<AABB>& out);
// Точка внутри твёрдой части блока (стрелы, снежки, частицы)
bool pointCollides(const World& w, const glm::vec3& p);

struct BodyState {
    bool onGround = false, collidedH = false, collidedV = false;
};
// Сдвиг тела (ступни в pos, полуширина hw, высота h) на motion с отсечением по блокам.
// stepHeight — подъём на уступ; sneak — не сходить с края. Обнуляет motion по упёршимся осям.
void moveBody(const World& w, glm::vec3& pos, float hw, float h, glm::vec3& motion, float stepHeight, bool sneak,
              BodyState& st);
