#include "Options.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include "Saves.h"
#include <string>

bool Options::load(const std::string& path) {
    std::ifstream f(std::filesystem::u8path(path));
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        size_t c = line.find(':');
        if (c == std::string::npos) continue;
        std::string k = line.substr(0, c), v = line.substr(c + 1);
        auto fl = [&]() { return std::clamp((float)std::atof(v.c_str()), 0.f, 1.f); };
        auto in = [&](int lo, int hi) { return std::clamp(std::atoi(v.c_str()), lo, hi); };
        auto bo = [&]() { return v == "true"; };
        if (k == "music") music = fl();
        else if (k == "sound") sound = fl();
        else if (k == "invertYMouse") invertMouse = bo();
        else if (k == "mouseSensitivity") sensitivity = fl();
        else if (k == "difficulty") difficulty = in(0, 3);
        else if (k == "fov") fov = fl();
        else if (k == "fancyGraphics") fancy = bo();
        else if (k == "viewDistance") renderDistance = in(0, 3);
        else if (k == "ao") smoothLighting = bo();
        else if (k == "gamma") gamma = fl();
        else if (k == "bobView") viewBobbing = bo();
        else if (k == "guiScale") guiScale = in(0, 3);
        else if (k == "clouds") clouds = bo();
        else if (k == "particles") particles = in(0, 2);
        else if (k == "performance") performance = in(0, 2);
        else if (k == "lastServer") lastServer = v;
        else if (k == "playerName") playerName = v.empty() ? "Player" : v;
        else if (k.rfind("key_", 0) == 0)
            for (int i = 0; i < KB_COUNT; ++i)
                if (k == std::string("key_") + keyBindName(i)) keys[i] = std::atoi(v.c_str());
    }
    return true;
}

bool Options::save(const std::string& path) const {
    FILE* f = openFileUtf8(path, "w");
    if (!f) return false;
    auto b = [](bool x) { return x ? "true" : "false"; };
    std::fprintf(f, "music:%g\nsound:%g\ninvertYMouse:%s\nmouseSensitivity:%g\ndifficulty:%d\nfov:%g\n", music, sound,
                 b(invertMouse), sensitivity, difficulty, fov);
    std::fprintf(f, "fancyGraphics:%s\nviewDistance:%d\nao:%s\ngamma:%g\nbobView:%s\nguiScale:%d\nclouds:%s\nparticles:%d\n",
                 b(fancy), renderDistance, b(smoothLighting), gamma, b(viewBobbing), guiScale, b(clouds), particles);
    std::fprintf(f, "performance:%d\n", performance);
    std::fprintf(f, "lastServer:%s\nplayerName:%s\n", lastServer.c_str(), playerName.c_str());
    for (int i = 0; i < KB_COUNT; ++i) std::fprintf(f, "key_%s:%d\n", keyBindName(i), keys[i]);
    std::fclose(f);
    return true;
}

std::string keyDisplayName(int k) {
    if (k >= 'A' && k <= 'Z') return std::string(1, (char)k);
    if (k >= '0' && k <= '9') return std::string(1, (char)k);
    switch (k) {
    case 32: return "SPACE";
    case 340: return "LSHIFT";
    case 344: return "RSHIFT";
    case 341: return "LCONTROL";
    case 345: return "RCONTROL";
    case 342: return "LMENU";
    case 346: return "RMENU";
    case 258: return "TAB";
    case 257: return "RETURN";
    case 259: return "BACK";
    case 265: return "UP";
    case 264: return "DOWN";
    case 263: return "LEFT";
    case 262: return "RIGHT";
    case 280: return "CAPITAL";
    case 96: return "GRAVE";
    case 44: return "COMMA";
    case 46: return "PERIOD";
    case 47: return "SLASH";
    case 59: return "SEMICOLON";
    case 39: return "APOSTROPHE";
    case 91: return "LBRACKET";
    case 93: return "RBRACKET";
    default:
        if (k >= 290 && k <= 301) return "F" + std::to_string(k - 289);
        if (k >= 320 && k <= 329) return "NUMPAD" + std::to_string(k - 320);
        return "KEY " + std::to_string(k);
    }
}
