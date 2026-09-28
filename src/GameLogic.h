#pragma once
// Игровая логика, общая для одиночной игры и сервера: ломание блока с дропом, печи и варка,
// раздатчик, сорванные без опоры блоки.
#include <functional>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "Entity.h"
#include "Mob.h"
#include "World.h"

using SoundFn = std::function<void(const std::string& name, float volume, float pitch, const glm::vec3& pos)>;

// Сломать блок игроком: содержимое сундука/печи, пластинка из проигрывателя, чешуйница из яйца,
// лёд в воду, дроп по инструменту (ножницы, шёлковое касание, удача). В творческом — без дропа.
void harvestBlock(World& w, MobManager& mm, std::vector<ItemEntity>& items, const glm::ivec3& p, const ItemStack& tool,
                  bool creative, int dimension, uint32_t& rng);

// Печи и варочные стойки работают, даже если окно закрыто
void tickTileEntities(World& w);

// Выстрелы раздатчиков (стрелы, снежки, яйца, прочие предметы)
void processDispense(World& w, MobManager& mm, std::vector<ItemEntity>& items, uint32_t& rng, const SoundFn& sound);

// Блоки, сорванные без опоры: дроп (звук и частицы — у вызывающего)
void dropPopped(World& w, std::vector<ItemEntity>& items, bool creative, uint32_t& rng);
