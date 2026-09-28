#include "Enchant.h"
#include <algorithm>

namespace {

enum class Target { Armor, Helmet, Boots, Sword, Digger, Bow };

struct EnchDef {
    int id;
    const char* name;
    int weight, maxLevel;
    Target target;
    int minBase, minPerLevel, range; // мин. зачаровываемость = minBase + (L-1)*minPerLevel, макс. = мин + range
    int group;                       // чары одной группы (кроме 0) несовместимы
};

const EnchDef DEFS[] = {
    {ENCH_PROTECTION, "Protection", 10, 4, Target::Armor, 1, 11, 20, 1},
    {ENCH_FIRE_PROT, "Fire Protection", 5, 4, Target::Armor, 10, 8, 12, 1},
    {ENCH_FEATHER_FALLING, "Feather Falling", 5, 4, Target::Boots, 5, 6, 10, 0},
    {ENCH_BLAST_PROT, "Blast Protection", 2, 4, Target::Armor, 5, 8, 12, 1},
    {ENCH_PROJ_PROT, "Projectile Protection", 5, 4, Target::Armor, 3, 6, 15, 1},
    {ENCH_RESPIRATION, "Respiration", 2, 3, Target::Helmet, 10, 10, 30, 0},
    {ENCH_AQUA_AFFINITY, "Aqua Affinity", 2, 1, Target::Helmet, 1, 0, 40, 0},
    {ENCH_SHARPNESS, "Sharpness", 10, 5, Target::Sword, 1, 11, 20, 2},
    {ENCH_SMITE, "Smite", 5, 5, Target::Sword, 5, 8, 20, 2},
    {ENCH_BANE, "Bane of Arthropods", 5, 5, Target::Sword, 5, 8, 20, 2},
    {ENCH_KNOCKBACK, "Knockback", 5, 2, Target::Sword, 5, 20, 50, 0},
    {ENCH_FIRE_ASPECT, "Fire Aspect", 2, 2, Target::Sword, 10, 20, 50, 0},
    {ENCH_LOOTING, "Looting", 2, 3, Target::Sword, 15, 9, 50, 0},
    {ENCH_EFFICIENCY, "Efficiency", 10, 5, Target::Digger, 1, 10, 50, 0},
    {ENCH_SILK_TOUCH, "Silk Touch", 1, 1, Target::Digger, 15, 0, 50, 3},
    {ENCH_UNBREAKING, "Unbreaking", 5, 3, Target::Digger, 5, 8, 50, 0},
    {ENCH_FORTUNE, "Fortune", 2, 3, Target::Digger, 15, 9, 50, 3},
    {ENCH_POWER, "Power", 10, 5, Target::Bow, 1, 10, 15, 0},
    {ENCH_PUNCH, "Punch", 2, 2, Target::Bow, 12, 20, 25, 0},
    {ENCH_FLAME, "Flame", 2, 1, Target::Bow, 20, 0, 30, 0},
    {ENCH_INFINITY, "Infinity", 1, 1, Target::Bow, 20, 0, 30, 0},
};

const EnchDef* def(int id) {
    for (auto& d : DEFS)
        if (d.id == id) return &d;
    return nullptr;
}

int rnd(uint32_t& s, int n) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return n > 0 ? (int)(s % (uint32_t)n) : 0;
}
float rndf(uint32_t& s) { return rnd(s, 0x1000000) / float(0x1000000); }

bool applies(const EnchDef& d, uint16_t item) {
    ArmorInfo a = armorInfo(item);
    ToolInfo t = toolInfo(item);
    switch (d.target) {
    case Target::Armor: return a.slot >= 0;
    case Target::Helmet: return a.slot == 0;
    case Target::Boots: return a.slot == 3;
    case Target::Sword: return t.type == Tool::Sword;
    case Target::Digger: return t.type == Tool::Pickaxe || t.type == Tool::Axe || t.type == Tool::Shovel;
    case Target::Bow: return item == BOW;
    }
    return false;
}

} // namespace

bool enchantApplies(int id, uint16_t item) {
    const EnchDef* d = def(id);
    return d && applies(*d, item);
}
int enchMaxLevel(int id) {
    const EnchDef* d = def(id);
    return d ? d->maxLevel : 1;
}
int enchWeight(int id) {
    const EnchDef* d = def(id);
    return d ? d->weight : 1;
}
bool enchCompatible(int a, int b) {
    if (a == b) return true;
    const EnchDef *da = def(a), *db = def(b);
    return !da || !db || da->group == 0 || da->group != db->group;
}

const char* enchName(int id) {
    const EnchDef* d = def(id);
    return d ? d->name : "?";
}

std::string enchDescription(int id, int level) {
    static const char* R[] = {"", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X"};
    return std::string(enchName(id)) + " " + (level >= 0 && level <= 10 ? R[level] : std::to_string(level));
}

int itemEnchantability(uint16_t id) {
    if (id == BOW) return 1;
    ToolInfo t = toolInfo(id);
    if (t.type != Tool::None && t.type != Tool::Hoe) {
        switch (t.tier) {
        case 0: return (id == GOLD_SWORD || id == GOLD_SHOVEL || id == GOLD_PICKAXE || id == GOLD_AXE) ? 22 : 15;
        case 1: return 5;
        case 2: return 14;
        case 3: return 10;
        }
    }
    ArmorInfo a = armorInfo(id);
    if (a.slot >= 0) {
        if (id >= LEATHER_HELMET && id <= LEATHER_BOOTS) return 15;
        if (id >= IRON_HELMET && id <= IRON_BOOTS) return 9;
        if (id >= DIAMOND_HELMET && id <= DIAMOND_BOOTS) return 10;
        if (id >= GOLD_HELMET && id <= GOLD_BOOTS) return 25;
        return 12;
    }
    return 0;
}

int enchantTableLevel(uint32_t& rng, int slot, int shelves, uint16_t itemId) {
    // 1.0: полок учитывается до 30 (кольцо в два яруса), нижняя кнопка — половина, средняя — две трети, верхняя — до 50
    if (itemEnchantability(itemId) <= 0) return 0;
    shelves = std::min(shelves, 30);
    int s = 1 + (shelves >> 1) + rnd(rng, shelves + 1);
    int l = rnd(rng, 5) + s;
    if (slot == 0) return (l >> 1) + 1;
    if (slot == 1) return l * 2 / 3 + 1;
    return l;
}

std::vector<std::pair<int, int>> pickEnchantments(uint32_t& rng, uint16_t itemId, int level) {
    std::vector<std::pair<int, int>> out;
    int e = itemEnchantability(itemId);
    if (e <= 0) return out;
    e = 1 + rnd(rng, e / 2 + 1) + rnd(rng, e / 2 + 1);
    int k = e + level;
    float f = (rndf(rng) + rndf(rng) - 1.f) * 0.25f;
    int l = (int)(k * (1.f + f) + 0.5f);

    // Кандидаты: для каждой подходящей чары — наибольший уровень, диапазон которого содержит l
    struct Cand { int id, level, weight, group; };
    std::vector<Cand> cands;
    for (auto& d : DEFS) {
        if (!applies(d, itemId)) continue;
        for (int L = d.maxLevel; L >= 1; --L) {
            int mn = d.minBase + (L - 1) * d.minPerLevel;
            if (l >= mn && l <= mn + d.range) { cands.push_back({d.id, L, d.weight, d.group}); break; }
        }
    }
    auto pick = [&]() -> bool {
        int total = 0;
        for (auto& c : cands) total += c.weight;
        if (total <= 0) return false;
        int r = rnd(rng, total);
        for (size_t i = 0; i < cands.size(); ++i) {
            r -= cands[i].weight;
            if (r < 0) {
                Cand c = cands[i];
                out.push_back({c.id, c.level});
                // Убираем выбранную и несовместимые с ней
                cands.erase(std::remove_if(cands.begin(), cands.end(),
                                           [&](const Cand& o) { return o.id == c.id || (c.group != 0 && o.group == c.group); }),
                            cands.end());
                return true;
            }
        }
        return false;
    };
    if (!pick()) return out;
    // Ещё чары: пока rand(50) не превысит уровень, уровень каждый раз делится пополам
    for (int i = l; rnd(rng, 50) <= i; i >>= 1)
        if (!pick()) break;
    return out;
}

int armorProtectionPoints(const ItemStack armor[4], int source) {
    int total = 0;
    for (int i = 0; i < 4; ++i) {
        const ItemStack& a = armor[i];
        if (a.empty()) continue;
        for (uint16_t e : a.ench) {
            if (!e) continue;
            int id = e >> 8, L = e & 0xFF;
            float factor = 0.f;
            if (id == ENCH_PROTECTION) factor = 0.75f;
            else if (id == ENCH_FIRE_PROT && source == 1) factor = 1.25f;
            else if (id == ENCH_FEATHER_FALLING && source == 2) factor = 2.5f;
            else if (id == ENCH_BLAST_PROT && source == 3) factor = 1.5f;
            else if (id == ENCH_PROJ_PROT && source == 4) factor = 1.5f;
            if (factor > 0.f) total += (int)((6 + L * L) * factor / 3.f);
        }
    }
    return total;
}
