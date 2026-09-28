#include "Trade.h"
#include <algorithm>
#include <cmath>
#include "Blocks.h"
#include "Enchant.h"

namespace {

uint32_t nextRand(uint32_t& s) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return s;
}
int rint(uint32_t& s, int n) { return n <= 1 ? 0 : (int)(nextRand(s) % (uint32_t)n); }
float rfl(uint32_t& s) { return (nextRand(s) & 0xFFFFFF) / float(0x1000000); }

struct Range { uint16_t id; int8_t lo, hi; };

// villagerStockList 1.4.2: сколько штук житель берёт за 1 изумруд
const Range BUY[] = {
    {COAL, 16, 24}, {IRON_INGOT, 8, 10}, {GOLD_INGOT, 8, 10}, {DIAMOND, 4, 6}, {PAPER, 24, 36}, {BOOK, 11, 13},
    {ENDER_PEARL, 3, 4}, {EYE_OF_ENDER, 2, 3}, {RAW_PORKCHOP, 14, 18}, {RAW_BEEF, 14, 18}, {RAW_CHICKEN, 14, 18},
    {COOKED_FISH, 9, 13}, {SEEDS, 34, 48}, {MELON_SEEDS, 30, 38}, {PUMPKIN_SEEDS, 30, 38}, {WHEAT_ITEM, 18, 22},
    {WOOL, 14, 22}, {ROTTEN_FLESH, 36, 64},
};
// blacksmithSellingList 1.4.2: цена в изумрудах; отрицательная — столько штук за 1 изумруд
const Range SELL[] = {
    {IRON_SWORD, 7, 11}, {DIAMOND_SWORD, 12, 14}, {IRON_AXE, 6, 8}, {DIAMOND_AXE, 9, 12}, {IRON_PICKAXE, 7, 9},
    {DIAMOND_PICKAXE, 10, 12}, {IRON_SHOVEL, 4, 6}, {DIAMOND_SHOVEL, 7, 8}, {IRON_HOE, 4, 6}, {DIAMOND_HOE, 7, 8},
    {IRON_BOOTS, 4, 6}, {DIAMOND_BOOTS, 7, 8}, {IRON_HELMET, 4, 6}, {DIAMOND_HELMET, 7, 8}, {IRON_CHESTPLATE, 10, 14},
    {DIAMOND_CHESTPLATE, 16, 19}, {IRON_LEGGINGS, 8, 10}, {DIAMOND_LEGGINGS, 11, 14}, {CHAIN_BOOTS, 5, 7},
    {CHAIN_HELMET, 5, 7}, {CHAIN_CHESTPLATE, 11, 15}, {CHAIN_LEGGINGS, 9, 11}, {BREAD, -4, -2}, {MELON, -8, -4},
    {APPLE, -8, -4}, {COOKIE, -10, -7}, {GLASS, -5, -3}, {BOOKSHELF, 3, 4}, {LEATHER_CHESTPLATE, 4, 5},
    {LEATHER_BOOTS, 2, 4}, {LEATHER_HELMET, 2, 4}, {LEATHER_LEGGINGS, 2, 4}, {SADDLE, 6, 8}, {EXP_BOTTLE, -5, -1},
    {REDSTONE, -4, -1}, {COMPASS, 10, 12}, {CLOCK, 10, 12}, {SHEARS, 3, 4}, {FLINT_AND_STEEL, 3, 4},
    {COOKED_PORKCHOP, -7, -5}, {STEAK, -7, -5}, {COOKED_CHICKEN, -8, -6}, {EYE_OF_ENDER, 7, 11}, {ARROW, -12, -8},
    {GLOWSTONE, -3, -1},
};

int rangeCount(const Range* table, size_t n, uint16_t id, uint32_t& rng) {
    for (size_t i = 0; i < n; ++i)
        if (table[i].id == id) return table[i].lo >= table[i].hi ? table[i].lo : table[i].lo + rint(rng, table[i].hi - table[i].lo);
    return 1;
}

bool sameIds(const MerchantRecipe& a, const MerchantRecipe& b) {
    return a.buy1.id == b.buy1.id && a.sell.id == b.sell.id && a.buy2.empty() == b.buy2.empty() && (a.buy2.empty() || a.buy2.id == b.buy2.id);
}

// Подходят ли предметы к сделке (только по номеру предмета, как в 1.4.2; шерсть любого цвета)
bool fits(const MerchantRecipe& r, const ItemStack& a, const ItemStack& b) {
    if (a.empty() || a.id != r.buy1.id || a.count < r.buy1.count) return false;
    if (r.buy2.empty()) return b.empty();
    return !b.empty() && b.id == r.buy2.id && b.count >= r.buy2.count;
}

} // namespace

void addVillagerOffers(std::vector<MerchantRecipe>& offers, int profession, uint32_t& rng, int count) {
    // Чем больше сделок уже открыто, тем выше шанс редких (field_82191_bN = sqrt(размер) * 0.2, отражается от 0.9)
    const float bonus = offers.empty() ? 0.f : std::sqrt((float)offers.size()) * 0.2f;
    auto chance = [&](float p) {
        float v = p + bonus;
        return v > 0.9f ? 0.9f - (v - 0.9f) : v;
    };
    std::vector<MerchantRecipe> list;
    auto buys = [&](uint16_t id, float p) { // житель покупает: N штук → 1 изумруд
        if (rfl(rng) >= chance(p)) return;
        MerchantRecipe r;
        r.buy1 = makeStack(id, rangeCount(BUY, sizeof BUY / sizeof *BUY, id, rng));
        r.sell = makeStack(EMERALD);
        list.push_back(r);
    };
    auto sells = [&](uint16_t id, float p) { // житель продаёт за изумруды
        if (rfl(rng) >= chance(p)) return;
        int n = rangeCount(SELL, sizeof SELL / sizeof *SELL, id, rng);
        MerchantRecipe r;
        if (n < 0) { r.buy1 = makeStack(EMERALD); r.sell = makeStack(id, -n); }
        else { r.buy1 = makeStack(EMERALD, n); r.sell = makeStack(id); }
        list.push_back(r);
    };
    switch (profession) {
    case 0: // фермер
        buys(WHEAT_ITEM, 0.9f); buys(WOOL, 0.5f); buys(RAW_CHICKEN, 0.5f); buys(COOKED_FISH, 0.4f);
        sells(BREAD, 0.9f); sells(MELON, 0.3f); sells(APPLE, 0.3f); sells(COOKIE, 0.3f); sells(SHEARS, 0.3f);
        sells(FLINT_AND_STEEL, 0.3f); sells(COOKED_CHICKEN, 0.3f); sells(ARROW, 0.5f);
        if (rfl(rng) < chance(0.5f)) {
            MerchantRecipe r;
            r.buy1 = makeStack(GRAVEL, 10);
            r.buy2 = makeStack(EMERALD);
            r.sell = makeStack(FLINT, 4 + rint(rng, 2));
            list.push_back(r);
        }
        break;
    case 1: // библиотекарь (книги с текстом в игре нет)
        buys(PAPER, 0.8f); buys(BOOK, 0.8f);
        sells(BOOKSHELF, 0.8f); sells(GLASS, 0.2f); sells(COMPASS, 0.2f); sells(CLOCK, 0.2f);
        break;
    case 2: { // священник: иногда зачарованное железо и алмаз за изумруды сверху
        sells(EYE_OF_ENDER, 0.3f); sells(EXP_BOTTLE, 0.2f); sells(REDSTONE, 0.4f); sells(GLOWSTONE, 0.3f);
        const uint16_t tools[] = {IRON_SWORD, DIAMOND_SWORD, IRON_CHESTPLATE, DIAMOND_CHESTPLATE, IRON_AXE, DIAMOND_AXE,
                                  IRON_PICKAXE, DIAMOND_PICKAXE};
        for (uint16_t id : tools) {
            if (rfl(rng) >= chance(0.05f)) continue;
            MerchantRecipe r;
            r.buy1 = makeStack(id);
            r.buy2 = makeStack(EMERALD, 2 + rint(rng, 3));
            r.sell = makeStack(id);
            for (auto& [e, l] : pickEnchantments(rng, id, 5 + rint(rng, 15))) r.sell.addEnch(e, l);
            list.push_back(r);
        }
        break;
    }
    case 3: // кузнец
        buys(COAL, 0.7f); buys(IRON_INGOT, 0.5f); buys(GOLD_INGOT, 0.5f); buys(DIAMOND, 0.5f);
        sells(IRON_SWORD, 0.5f); sells(DIAMOND_SWORD, 0.5f); sells(IRON_AXE, 0.3f); sells(DIAMOND_AXE, 0.3f);
        sells(IRON_PICKAXE, 0.5f); sells(DIAMOND_PICKAXE, 0.5f); sells(IRON_SHOVEL, 0.2f); sells(DIAMOND_SHOVEL, 0.2f);
        sells(IRON_HOE, 0.2f); sells(DIAMOND_HOE, 0.2f); sells(IRON_BOOTS, 0.2f); sells(DIAMOND_BOOTS, 0.2f);
        sells(IRON_HELMET, 0.2f); sells(DIAMOND_HELMET, 0.2f); sells(IRON_CHESTPLATE, 0.2f); sells(DIAMOND_CHESTPLATE, 0.2f);
        sells(IRON_LEGGINGS, 0.2f); sells(DIAMOND_LEGGINGS, 0.2f); sells(CHAIN_BOOTS, 0.1f); sells(CHAIN_HELMET, 0.1f);
        sells(CHAIN_CHESTPLATE, 0.1f); sells(CHAIN_LEGGINGS, 0.1f);
        break;
    default: // мясник
        buys(COAL, 0.7f); buys(RAW_PORKCHOP, 0.5f); buys(RAW_BEEF, 0.5f);
        sells(SADDLE, 0.1f); sells(LEATHER_CHESTPLATE, 0.3f); sells(LEATHER_BOOTS, 0.3f); sells(LEATHER_HELMET, 0.3f);
        sells(LEATHER_LEGGINGS, 0.3f); sells(COOKED_PORKCHOP, 0.3f); sells(STEAK, 0.3f);
        break;
    }
    if (list.empty()) {
        MerchantRecipe r;
        r.buy1 = makeStack(GOLD_INGOT, rangeCount(BUY, sizeof BUY / sizeof *BUY, GOLD_INGOT, rng));
        r.sell = makeStack(EMERALD);
        list.push_back(r);
    }
    for (size_t i = list.size(); i > 1; --i) std::swap(list[i - 1], list[(size_t)rint(rng, (int)i)]); // Collections.shuffle
    for (int i = 0; i < count && i < (int)list.size(); ++i) {
        const MerchantRecipe& r = list[(size_t)i];
        bool handled = false;
        for (auto& o : offers) {
            if (!sameIds(o, r)) continue;
            // addToListWithCheck: такая же, но дешевле — заменяет старую
            if (r.buy1.count < o.buy1.count || (!r.buy2.empty() && r.buy2.count < o.buy2.count)) o = r;
            handled = true;
            break;
        }
        if (!handled) offers.push_back(r);
    }
}

int findTradeRecipe(const std::vector<MerchantRecipe>& offers, const ItemStack& a0, const ItemStack& b0, int selected) {
    ItemStack a = a0, b = b0;
    if (a.empty()) { a = b; b.clear(); }
    if (a.empty()) return -1;
    auto lookup = [&](const ItemStack& x, const ItemStack& y) {
        // Как canRecipeBeUsed 1.4.2: выбранная (с номером > 0) проверяется одна, иначе — первая подходящая
        if (selected > 0 && selected < (int)offers.size()) return fits(offers[(size_t)selected], x, y) ? selected : -1;
        for (size_t i = 0; i < offers.size(); ++i)
            if (fits(offers[i], x, y)) return (int)i;
        return -1;
    };
    int i = lookup(a, b);
    if (i >= 0 && !offers[(size_t)i].disabled()) return i;
    if (!b.empty()) {
        i = lookup(b, a);
        if (i >= 0 && !offers[(size_t)i].disabled()) return i;
    }
    return -1;
}

bool payForTrade(const MerchantRecipe& r, ItemStack& a, ItemStack& b) {
    auto pay = [&](ItemStack& x, ItemStack& y) {
        if (!fits(r, x.empty() ? y : x, x.empty() ? ItemStack{} : y)) return false;
        ItemStack& first = x.empty() ? y : x;
        first.count = (uint8_t)(first.count - r.buy1.count);
        if (first.count == 0) first.clear();
        if (!r.buy2.empty()) {
            y.count = (uint8_t)(y.count - r.buy2.count);
            if (y.count == 0) y.clear();
        }
        return true;
    };
    if (pay(a, b)) return true;
    return pay(b, a);
}

void useTradeRecipe(std::vector<MerchantRecipe>& offers, TradeState& st, int idx) {
    if (idx < 0 || idx >= (int)offers.size()) return;
    ++offers[(size_t)idx].uses;
    if (sameIds(offers[(size_t)idx], offers.back())) {
        st.timer = 40;
        st.refresh = true;
    }
}

bool tickTradeState(std::vector<MerchantRecipe>& offers, TradeState& st, int profession, uint32_t& rng) {
    if (st.timer <= 0) return false;
    if (--st.timer > 0) return false;
    if (!st.refresh) return false;
    if (offers.size() > 1)
        for (auto& r : offers)
            if (r.disabled()) r.maxUses += rint(rng, 6) + rint(rng, 6) + 2;
    addVillagerOffers(offers, profession, rng, 1);
    st.refresh = false;
    return true;
}
