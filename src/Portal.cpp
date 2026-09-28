// Портал в Незер (BlockPortal 1.0): рамка из обсидиана 4x5 с пустым проёмом 2x3,
// зажигается огнём внутри проёма. Мета портала — ось: 0 — рамка вдоль X, 1 — вдоль Z.
#include "World.h"

bool World::tryCreatePortal(int x, int y, int z) {
    for (int axis = 0; axis < 2; ++axis) {
        int dx = axis == 0 ? 1 : 0, dz = axis == 0 ? 0 : 1;
        for (int ox = 0; ox >= -1; --ox)
            for (int oy = 0; oy >= -2; --oy) {
                int x0 = x + dx * ox, y0 = y + oy, z0 = z + dz * ox;
                auto at = [&](int i, int j) { return getBlock(x0 + dx * i, y0 + j, z0 + dz * i); };
                bool ok = true;
                for (int i = 0; i < 2 && ok; ++i)
                    for (int j = 0; j < 3 && ok; ++j) ok = at(i, j) == AIR || at(i, j) == FIRE;
                for (int i = 0; i < 2 && ok; ++i) ok = at(i, -1) == OBSIDIAN && at(i, 3) == OBSIDIAN;
                for (int j = 0; j < 3 && ok; ++j) ok = at(-1, j) == OBSIDIAN && at(2, j) == OBSIDIAN;
                if (!ok) continue;
                breakingPortal_ = true;
                for (int i = 0; i < 2; ++i)
                    for (int j = 0; j < 3; ++j) setBlock(x0 + dx * i, y0 + j, z0 + dz * i, PORTAL, (uint8_t)axis);
                breakingPortal_ = false;
                return true;
            }
    }
    return false;
}
