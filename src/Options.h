#pragma once
// Настройки игры (GameSettings 1.0), хранятся в options.txt как «ключ:значение».
#include <string>

// Переназначаемые клавиши (GuiControls 1.0); коды — GLFW
enum KeyBind { KB_FORWARD, KB_LEFT, KB_BACK, KB_RIGHT, KB_JUMP, KB_INVENTORY, KB_DROP, KB_SNEAK, KB_COUNT };
inline const char* keyBindName(int i) {
    static const char* N[KB_COUNT] = {"Forward", "Left", "Back", "Right", "Jump", "Inventory", "Drop", "Sneak"};
    return N[i];
}
std::string keyDisplayName(int glfwKey); // «W», «SPACE», «LSHIFT»…

struct Options {
    // Звук и управление
    float music = 1.f, sound = 1.f;
    bool invertMouse = false;
    float sensitivity = 0.5f;
    int difficulty = 2;       // 0 Peaceful, 1 Easy, 2 Normal, 3 Hard
    float fov = 0.f;          // 0 — Normal (70°), 1 — Quake Pro (110°)
    // Графика
    bool fancy = true;        // Fancy/Fast: прозрачная листва, радиус дождя
    int renderDistance = 1;   // 0 Far, 1 Normal, 2 Short, 3 Tiny
    bool smoothLighting = false;
    float gamma = 0.25f;      // 0 Moody .. 1 Bright
    bool viewBobbing = true;
    int guiScale = 0;         // 0 Auto, 1 Small, 2 Normal, 3 Large
    bool clouds = true;
    int particles = 0;        // 0 All, 1 Decreased, 2 Minimal
    std::string lastServer = "localhost";   // последний адрес сервера
    std::string playerName = "Player";      // имя в сетевой игре
    int performance = 1;      // 0 Max FPS (без вертикальной синхронизации), 1 Balanced (синхронизация), 2 Power saver (40 FPS)
    int keys[KB_COUNT] = {87 /*W*/, 65 /*A*/, 83 /*S*/, 68 /*D*/, 32 /*SPACE*/, 69 /*E*/, 81 /*Q*/, 340 /*LSHIFT*/};

    int chunkDistance() const {
        static const int d[4] = {16, 8, 4, 2};
        return d[renderDistance & 3];
    }
    float fovDegrees() const { return 70.f + fov * 40.f; }
    // Чувствительность мыши как в 1.0: градусов на пиксель
    float mouseDegreesPerPixel() const {
        float f = sensitivity * 0.6f + 0.2f;
        return f * f * f * 8.f * 0.15f;
    }

    bool load(const std::string& path);
    bool save(const std::string& path) const;
};
