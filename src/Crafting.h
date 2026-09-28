#pragma once
// Рецепты верстака (как в 1.0), переплавка в печи, топливо.
#include <vector>
#include "Inventory.h"

// Что выпадает из сломанного блока (как в 1.0); rng — состояние xorshift
std::vector<ItemStack> blockDrops(uint8_t block, uint8_t meta, uint32_t& rng);

// grid — сетка width x width (2 для инвентаря, 3 для верстака), построчно
ItemStack findRecipe(const ItemStack* grid, int width);

ItemStack smeltingResult(const ItemStack& input);
// Что остаётся в сетке после крафта (ведро от молока)
ItemStack craftLeftover(const ItemStack& s);
int fuelTicks(const ItemStack& fuel);

constexpr int FURNACE_COOK_TICKS = 200;
// Один тик печи; возвращает true, если печь сменила состояние (горит/погасла)
bool tickFurnace(TileEntity& te);
