#pragma once
// Карта (ItemMap / MapData 1.0): 128x128 точек, масштаб 1:8 (1024x1024 блока), север сверху.
// Номер карты — damage предмета. Рисуется, пока карта в руке: за тик обновляется 1/16 столбцов вокруг игрока.
#include <cstdint>
#include <string>
#include <glm/glm.hpp>
#include "World.h"

struct MapData {
    int xCenter = 0, zCenter = 0;
    int scale = 3; // 1 << scale блоков на точку
    int dim = 0;
    uint8_t colors[128 * 128] = {}; // цвет*4 + яркость (0 — пусто)
    int ticks = 0;
    bool dirty = false;    // изменилась с последнего сохранения
    bool texDirty = true;  // изменилась с последней заливки в текстуру
};

// Цвет материала блока на карте (MapColor 1.0): 0 — не рисуется (воздух, стекло, факелы, провода)
int blockMapColor(uint8_t block);
// RGBA точки карты (index = цвет*4 + яркость); 0 — прозрачная
uint32_t mapPixelRGBA(uint8_t index);
// Дорисовать карту вокруг игрока (как ItemMap.updateMapData); noSky — Незер (серый «шум» вместо рельефа)
void updateMap(MapData& m, const World& w, const glm::vec3& playerPos, int dim, bool noSky);
bool saveMap(const MapData& m, const std::string& path);
bool loadMap(MapData& m, const std::string& path);
