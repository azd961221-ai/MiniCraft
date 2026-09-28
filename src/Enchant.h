#pragma once
// Зачарования Minecraft 1.0: 21 чара, их веса, уровни и выбор чар столом (EnchantmentHelper).
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include "Item.h"

enum EnchId : int {
    ENCH_PROTECTION = 0, ENCH_FIRE_PROT = 1, ENCH_FEATHER_FALLING = 2, ENCH_BLAST_PROT = 3, ENCH_PROJ_PROT = 4,
    ENCH_RESPIRATION = 5, ENCH_AQUA_AFFINITY = 6,
    ENCH_SHARPNESS = 16, ENCH_SMITE = 17, ENCH_BANE = 18, ENCH_KNOCKBACK = 19, ENCH_FIRE_ASPECT = 20, ENCH_LOOTING = 21,
    ENCH_EFFICIENCY = 32, ENCH_SILK_TOUCH = 33, ENCH_UNBREAKING = 34, ENCH_FORTUNE = 35,
    ENCH_POWER = 48, ENCH_PUNCH = 49, ENCH_FLAME = 50, ENCH_INFINITY = 51
};

const char* enchName(int id);
std::string enchDescription(int id, int level); // «Sharpness III»
// Зачаровываемость предмета (0 — нельзя зачаровать)
int itemEnchantability(uint16_t id);
// Уровень кнопки стола 0..2 при данном числе книжных полок (calcItemStackEnchantability)
int enchantTableLevel(uint32_t& rng, int slot, int shelves, uint16_t itemId);
// Чары для предмета на уровне level (buildEnchantmentList)
std::vector<std::pair<int, int>> pickEnchantments(uint32_t& rng, uint16_t itemId, int level);
// Для наковальни (ContainerRepair 1.4.2): применима ли чара к предмету, макс. уровень, вес, совместимость
bool enchantApplies(int id, uint16_t item);
int enchMaxLevel(int id);
int enchWeight(int id);
bool enchCompatible(int a, int b);
// Снижение урона чарами брони (EnchantmentProtection): source 0 обычный, 1 огонь, 2 падение, 3 взрыв, 4 снаряд
int armorProtectionPoints(const ItemStack armor[4], int source);
