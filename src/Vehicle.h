#pragma once
// Вагонетки (обычная, с сундуком, с печкой) и лодки — как EntityMinecart и EntityBoat из 1.0.
#include <glm/glm.hpp>
#include <vector>
#include "Inventory.h"

enum class VehicleKind : uint8_t { Minecart = 0, ChestCart = 1, FurnaceCart = 2, Boat = 3 };

struct Vehicle {
    VehicleKind kind = VehicleKind::Minecart;
    glm::vec3 pos{0.f}, prev{0.f}, motion{0.f};
    float yaw = 0.f, prevYaw = 0.f;
    int damage = 0, hurtTime = 0;
    int fuel = 0;                 // вагонетка с печкой: тиков тяги
    glm::vec2 push{0.f};          // направление тяги печки
    TileEntity chest;             // вагонетка с сундуком (27 слотов)
    bool onGround = false, dead = false;
    bool onRail = false;
    uint32_t id = 0;
};

// Рельсы: концы участка для формы (мета 0..9), в долях блока (y — 0 или 1)
void railExits(int shape, glm::vec3& a, glm::vec3& b);
