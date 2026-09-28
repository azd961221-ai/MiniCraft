#include "Saves.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

FILE* openFileUtf8(const std::string& path, const char* mode) {
    std::wstring wp = fs::u8path(path).wstring(), wm(mode, mode + std::strlen(mode));
    return _wfopen(wp.c_str(), wm.c_str());
}

bool commitFile(const std::string& tmpPath, const std::string& path) {
    std::error_code ec;
    fs::path t = fs::u8path(tmpPath), p = fs::u8path(path), b = fs::u8path(path + ".bak");
    if (fs::exists(p, ec)) {
        fs::remove(b, ec);
        fs::rename(p, b, ec);
        if (ec) { ec.clear(); fs::copy_file(p, b, fs::copy_options::overwrite_existing, ec); ec.clear(); }
    }
    fs::rename(t, p, ec);
    return !ec;
}

bool fileExistsUtf8(const std::string& path) {
    std::error_code ec;
    return fs::exists(fs::u8path(path), ec);
}

int64_t nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

uint32_t seedFromText(const std::string& text, uint32_t fallback) {
    std::string t = text;
    t.erase(0, t.find_first_not_of(" \t"));
    t.erase(t.find_last_not_of(" \t") + 1);
    if (t.empty()) return fallback;
    char* end = nullptr;
    long long v = std::strtoll(t.c_str(), &end, 10);
    if (end && *end == 0 && v != 0) return (uint32_t)v;
    int32_t h = 0;
    for (unsigned char c : t) h = 31 * h + c;
    return (uint32_t)h;
}

bool SaveManager::readInfo(const std::string& folder, WorldInfo& out) const {
    std::ifstream f(fs::u8path(path(folder) + "level.txt"));
    if (!f) return false;
    out = WorldInfo{};
    out.folder = folder;
    out.name = folder;
    std::string line;
    while (std::getline(f, line)) {
        size_t c = line.find('=');
        if (c == std::string::npos) continue;
        std::string k = line.substr(0, c), v = line.substr(c + 1);
        if (k == "name") out.name = v;
        else if (k == "seed") out.seed = (uint32_t)std::strtoul(v.c_str(), nullptr, 10);
        else if (k == "gameMode") out.gameMode = std::atoi(v.c_str());
        else if (k == "hardcore") out.hardcore = v == "true";
        else if (k == "lastPlayed") out.lastPlayed = std::atoll(v.c_str());
        else if (k == "dimension") out.dimension = std::atoi(v.c_str());
        else if (k == "achievements") out.achievements = (uint32_t)std::strtoul(v.c_str(), nullptr, 10);
        else if (k == "cartDistance") out.cartDistance = (float)std::atof(v.c_str());
        else if (k == "generator") out.generator = std::atoi(v.c_str());
        else if (k == "cheats") out.cheats = v == "true";
    }
    return true;
}

bool SaveManager::writeInfo(const WorldInfo& w) const {
    std::error_code ec;
    fs::create_directories(fs::u8path(path(w.folder)), ec);
    std::ofstream f(fs::u8path(path(w.folder) + "level.txt"));
    if (!f) return false;
    f << "name=" << w.name << "\nseed=" << w.seed << "\ngameMode=" << w.gameMode << "\nhardcore=" << (w.hardcore ? "true" : "false")
      << "\nlastPlayed=" << w.lastPlayed << "\ndimension=" << w.dimension << "\nachievements=" << w.achievements
      << "\ncartDistance=" << w.cartDistance << "\ngenerator=" << w.generator << "\ncheats=" << (w.cheats ? "true" : "false")
      << "\n";
    return true;
}

std::vector<WorldInfo> SaveManager::list() const {
    std::vector<WorldInfo> out;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(fs::u8path(root), ec)) {
        if (!e.is_directory()) continue;
        WorldInfo w;
        std::string folder = e.path().filename().u8string();
        if (readInfo(folder, w)) out.push_back(w);
    }
    std::sort(out.begin(), out.end(), [](const WorldInfo& a, const WorldInfo& b) { return a.lastPlayed > b.lastPlayed; });
    return out;
}

std::string SaveManager::uniqueFolder(const std::string& name) const {
    // Как в 1.0: недопустимые символы заменяются на «_», при совпадении добавляется «-»
    std::string base;
    for (char c : name) base += (std::isalnum((unsigned char)c) || c == ' ' || c == '-' || c == '_' || (unsigned char)c >= 0x80) ? c : '_';
    while (!base.empty() && base.back() == ' ') base.pop_back();
    if (base.empty()) base = "World";
    std::string f = base;
    std::error_code ec;
    while (fs::exists(fs::u8path(path(f)), ec)) f += "-";
    return f;
}

bool SaveManager::remove(const std::string& folder) const {
    if (folder.empty() || folder.find("..") != std::string::npos) return false;
    std::error_code ec;
    fs::remove_all(fs::u8path(path(folder)), ec);
    return !ec;
}
