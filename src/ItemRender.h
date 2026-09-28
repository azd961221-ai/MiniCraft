#pragma once
// Модели предметов: «выдавленная» из иконки объёмная модель (как предмет в руке в 1.0).
#include <unordered_map>
#include <vector>
#include "Image.h"
#include "Item.h"
#include "World.h"

// Иконка предмета: тайл (col, row) и из какой текстуры (items.png или terrain.png)
inline bool itemTile(const ItemStack& s, int& col, int& row, bool& fromItems) {
    if (itemIcon(s.id, col, row, s.damage)) { fromItems = true; return true; }
    if (isBlockItem(s.id)) {
        int t = blockTex((uint8_t)s.id, 4, s.id == WHEAT ? 7 : (uint8_t)s.damage);
        col = t % 16; row = t / 16;
        fromItems = false;
        return true;
    }
    return false;
}

// Предмет рисуется кубиком (обычные блоки) или плоской иконкой (предметы, факелы, растения)
inline bool itemIsCube(const ItemStack& s) { return isBlockItem(s.id) && !isFlatItem((uint8_t)s.id); }

class ItemModels {
public:
    const Image* items = nullptr;
    const Image* terrain = nullptr;

    // Модель в [0,1]x[0,1]x[-1/16,0]: лицевая и задняя грани плюс боковые стенки по контуру пикселей
    const std::vector<Vertex>& extruded(const ItemStack& s, bool& fromItems) {
        int col = 0, row = 0;
        itemTile(s, col, row, fromItems);
        uint32_t key = (uint32_t)(col | (row << 4) | (fromItems ? 256 : 0));
        auto it = cache_.find(key);
        if (it != cache_.end()) return it->second;
        return cache_[key] = build(fromItems ? *items : *terrain, col, row);
    }

    // Модель по тайлу напрямую (натянутый лук: иконки стадий натяжения)
    const std::vector<Vertex>& extrudedTile(int col, int row, bool fromItems) {
        uint32_t key = (uint32_t)(col | (row << 4) | (fromItems ? 256 : 0));
        auto it = cache_.find(key);
        if (it != cache_.end()) return it->second;
        return cache_[key] = build(fromItems ? *items : *terrain, col, row);
    }

private:
    std::unordered_map<uint32_t, std::vector<Vertex>> cache_;

    static std::vector<Vertex> build(const Image& img, int col, int row) {
        std::vector<Vertex> v;
        const float px = 1.f / 256.f, u0 = col * 16 * px, v0 = row * 16 * px, d = 1.f / 16.f;
        auto opaque = [&](int x, int y) {
            if (x < 0 || y < 0 || x > 15 || y > 15 || img.empty()) return false;
            return img.at(col * 16 + x, row * 16 + y)[3] > 0;
        };
        auto quad = [&](Vertex a, Vertex b, Vertex c, Vertex e) { v.insert(v.end(), {a, b, c, a, c, e}); };
        // Лицевая (z=0) и задняя (z=-d) грани целиком — прозрачные пиксели отсечёт alpha test
        quad({0, 0, 0, u0, v0 + 16 * px, 1, 1, 0}, {1, 0, 0, u0 + 16 * px, v0 + 16 * px, 1, 1, 0},
             {1, 1, 0, u0 + 16 * px, v0, 1, 1, 0}, {0, 1, 0, u0, v0, 1, 1, 0});
        quad({0, 0, -d, u0, v0 + 16 * px, 0.8f, 1, 0}, {0, 1, -d, u0, v0, 0.8f, 1, 0},
             {1, 1, -d, u0 + 16 * px, v0, 0.8f, 1, 0}, {1, 0, -d, u0 + 16 * px, v0 + 16 * px, 0.8f, 1, 0});
        // Стенки по краям непрозрачных пикселей
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) {
                if (!opaque(x, y)) continue;
                float uc = u0 + (x + 0.5f) * px, vc = v0 + (y + 0.5f) * px;
                float x0 = x / 16.f, x1 = (x + 1) / 16.f, yT = 1 - y / 16.f, yB = 1 - (y + 1) / 16.f;
                if (!opaque(x - 1, y)) quad({x0, yB, 0, uc, vc, 0.6f, 1, 0}, {x0, yT, 0, uc, vc, 0.6f, 1, 0}, {x0, yT, -d, uc, vc, 0.6f, 1, 0}, {x0, yB, -d, uc, vc, 0.6f, 1, 0});
                if (!opaque(x + 1, y)) quad({x1, yB, -d, uc, vc, 0.6f, 1, 0}, {x1, yT, -d, uc, vc, 0.6f, 1, 0}, {x1, yT, 0, uc, vc, 0.6f, 1, 0}, {x1, yB, 0, uc, vc, 0.6f, 1, 0});
                if (!opaque(x, y - 1)) quad({x0, yT, 0, uc, vc, 0.9f, 1, 0}, {x1, yT, 0, uc, vc, 0.9f, 1, 0}, {x1, yT, -d, uc, vc, 0.9f, 1, 0}, {x0, yT, -d, uc, vc, 0.9f, 1, 0});
                if (!opaque(x, y + 1)) quad({x0, yB, -d, uc, vc, 0.5f, 1, 0}, {x1, yB, -d, uc, vc, 0.5f, 1, 0}, {x1, yB, 0, uc, vc, 0.5f, 1, 0}, {x0, yB, 0, uc, vc, 0.5f, 1, 0});
            }
        return v;
    }
};
