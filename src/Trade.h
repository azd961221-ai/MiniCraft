#pragma once
// Торговля с жителями 1.4.2 (EntityVillager, MerchantRecipe, MerchantRecipeList, InventoryMerchant)
#include <cstdint>
#include <vector>
#include "Item.h"

// Сделка: отдать buy1 (и buy2, если есть) — получить sell. После maxUses использований закрывается (красный крест),
// пока житель не «обновит товар»
struct MerchantRecipe {
    ItemStack buy1, buy2, sell;
    int32_t uses = 0, maxUses = 7;
    bool disabled() const { return uses >= maxUses; }
};

// Профессия жителя 0..4: фермер, библиотекарь, священник, кузнец, мясник
// Добавить count новых сделок по профессии (addDefaultEquipmentAndRecipies): список перемешивается,
// берутся первые count; похожая сделка не дублируется, более выгодная заменяет старую
void addVillagerOffers(std::vector<MerchantRecipe>& offers, int profession, uint32_t& rng, int count);

// Какая сделка подходит к предметам в двух слотах (canRecipeBeUsed + resetRecipeAndSlots): сначала выбранная,
// потом любая; слоты можно поменять местами. -1 — ни одна (или подходящая закрыта)
int findTradeRecipe(const std::vector<MerchantRecipe>& offers, const ItemStack& a, const ItemStack& b, int selected);

// Забрать результат: списать плату из слотов (SlotMerchantResult). false — предметов не хватает
bool payForTrade(const MerchantRecipe& r, ItemStack& a, ItemStack& b);

// Житель после сделки (useRecipe): счётчик; сделка была последней в списке — через 40 тиков добавится новая,
// а закрытые получат ещё 2..12 использований
struct TradeState {
    int timer = 0;
    bool refresh = false;
};
void useTradeRecipe(std::vector<MerchantRecipe>& offers, TradeState& st, int idx);
// Тик жителя, который сейчас не торгует; true — товар обновился (частицы, регенерация)
bool tickTradeState(std::vector<MerchantRecipe>& offers, TradeState& st, int profession, uint32_t& rng);
