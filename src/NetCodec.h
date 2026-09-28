#pragma once
// Кодирование общих структур для сети (клиент и сервер пишут/читают одинаково).
#include "Inventory.h"
#include "Net.h"
#include "Protocol.h"

// Вектор из пакета. Только так: в «glm::vec3(r.f32(), r.f32(), r.f32())» порядок вычисления аргументов
// не определён, MSVC читает справа налево — X и Z менялись местами
inline glm::vec3 readVec3(net::Reader& r) {
    float x = r.f32();
    float y = r.f32();
    float z = r.f32();
    return {x, y, z};
}
inline glm::ivec3 readIVec3(net::Reader& r) {
    int x = r.i32();
    int y = r.i32();
    int z = r.i32();
    return {x, y, z};
}

inline void writeItem(net::Writer& w, const ItemStack& s) {
    w.u16(s.id);
    w.u8(s.count);
    w.u16(s.damage);
    for (uint16_t e : s.ench) w.u16(e);
}

inline ItemStack readItem(net::Reader& r) {
    ItemStack s;
    s.id = r.u16();
    s.count = r.u8();
    s.damage = r.u16();
    for (auto& e : s.ench) e = r.u16();
    if (s.count == 0) s.clear();
    return s;
}

inline void writePlayerNet(net::Writer& w, const PlayerNet& s) {
    w.f32(s.x); w.f32(s.y); w.f32(s.z); w.f32(s.yaw); w.f32(s.pitch);
    w.u8(s.flags); w.u16(s.held); w.u16(s.heldDamage);
    for (uint16_t a : s.armor) w.u16(a);
    w.u8((uint8_t)s.dim);
}

inline PlayerNet readPlayerNet(net::Reader& r) {
    PlayerNet s;
    s.x = r.f32(); s.y = r.f32(); s.z = r.f32(); s.yaw = r.f32(); s.pitch = r.f32();
    s.flags = r.u8(); s.held = r.u16(); s.heldDamage = r.u16();
    for (auto& a : s.armor) a = r.u16();
    s.dim = (int8_t)r.u8();
    return s;
}
