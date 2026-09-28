#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct Image {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba; // строки сверху вниз, 4 байта на пиксель

    bool empty() const { return rgba.empty(); }
    uint8_t* at(int x, int y) { return &rgba[(size_t(y) * width + x) * 4]; }
    const uint8_t* at(int x, int y) const { return &rgba[(size_t(y) * width + x) * 4]; }
};

// Загрузка PNG/JPG/BMP через Windows Imaging Component
bool loadImage(const std::string& path, Image& out);

// Папка, где лежит exe (с завершающим '/')
std::string exeDirectory();
