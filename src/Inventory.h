#pragma once
#include <algorithm>
#include "Item.h"

// Инвентарь игрока: слоты 0..8 — хотбар, 9..35 — основной, плюс 4 слота брони
struct Inventory {
    static constexpr int SIZE = 36;
    ItemStack slots[SIZE];
    ItemStack armor[4]; // шлем, нагрудник, поножи, ботинки

    // Добавляет предмет: сначала дополняет такие же стопки, затем занимает пустые слоты.
    // Возвращает true, если поместилось всё; в stack остаётся не поместившийся остаток.
    bool add(ItemStack& stack) {
        if (stack.empty()) return true;
        int maxS = maxStackSize(stack.id);
        if (maxS > 1) {
            for (auto& s : slots) {
                if (!s.empty() && s.sameItem(stack) && s.count < maxS) {
                    int n = std::min<int>(stack.count, maxS - s.count);
                    s.count = (uint8_t)(s.count + n);
                    stack.count = (uint8_t)(stack.count - n);
                    if (stack.count == 0) { stack.clear(); return true; }
                }
            }
        }
        for (auto& s : slots) {
            if (s.empty()) {
                s = stack;
                stack.clear();
                return true;
            }
        }
        return false;
    }

    int count(uint16_t id) const {
        int n = 0;
        for (auto& s : slots) if (s.id == id) n += s.count;
        return n;
    }

    // Очки брони 0..20
    int armorValue() const {
        int v = 0;
        for (auto& a : armor) if (!a.empty()) v += armorInfo(a.id).points;
        return v;
    }

    void clear() {
        for (auto& s : slots) s.clear();
        for (auto& a : armor) a.clear();
    }
};

// Блок с содержимым: сундук (27 слотов) или печь (0 — сырьё, 1 — топливо, 2 — результат)
struct TileEntity {
    enum Type : uint8_t { Chest = 0, Furnace = 1, Dispenser = 2, Brewing = 3 } type = Chest;
    int x = 0, y = 0, z = 0;
    ItemStack items[27];
    int burnTime = 0, burnMax = 0, cookTime = 0;

    int size() const { return type == Chest ? 27 : type == Dispenser ? 9 : type == Brewing ? 4 : 3; }
};
