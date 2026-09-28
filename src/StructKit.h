#pragma once
// Общее для строений из кусков (крепость Края, адская крепость): разбор чертежей, повороты, размещение.
// Чертёж: слои снизу вверх, строки по Z, символы по X; вход куска — на последней строке (сторона +Z), кусок тянется к -Z.
// Направления 0..3: +X, +Z, -X, -Z.
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace skit {

inline const int DX[4] = {1, 0, -1, 0}, DZ[4] = {0, 1, 0, -1};

struct Blueprint {
    std::vector<std::vector<std::string>> layers;
    int w = 0, d = 0;
    char at(int l, int r, int c) const {
        if (l < 0 || l >= (int)layers.size() || r < 0 || r >= (int)layers[l].size() || c < 0 || c >= (int)layers[l][r].size()) return '.';
        return layers[l][r][c];
    }
    void set(int l, int r, int c, char ch) {
        while ((int)layers.size() <= l) layers.emplace_back();
        auto& L = layers[l];
        while ((int)L.size() <= r) L.emplace_back();
        auto& row = L[r];
        while ((int)row.size() <= c) row.push_back('.');
        row[c] = ch;
        w = std::max(w, c + 1);
        d = std::max(d, r + 1);
    }
    int h() const { return (int)layers.size(); }
    void fix() {
        for (auto& L : layers) {
            d = std::max(d, (int)L.size());
            for (auto& row : L) w = std::max(w, (int)row.size());
        }
    }
};

inline std::map<std::string, Blueprint> parse(const char* text) {
    std::map<std::string, Blueprint> out;
    std::istringstream in(text);
    std::string line;
    Blueprint* cur = nullptr;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("#####", 0) == 0) { cur = &out[line.substr(6)]; continue; }
        if (!cur) continue;
        if (line == "--") { cur->layers.emplace_back(); continue; }
        if (!cur->layers.empty()) cur->layers.back().push_back(line);
    }
    for (auto& [k, b] : out) b.fix();
    return out;
}

// Поворот куска: rot 0 — вход на +Z (как в чертеже), 1 — вход на -X, 2 — на -Z, 3 — на +X
struct Placement {
    int x0 = 0, y0 = 0, z0 = 0, rot = 0, w = 0, d = 0; // w, d — размеры в мире после поворота
    void toWorld(const Blueprint& b, int c, int r, int& x, int& z) const {
        switch (rot) {
        case 0: x = c; z = r; break;
        case 1: x = b.d - 1 - r; z = c; break;
        case 2: x = b.w - 1 - c; z = b.d - 1 - r; break;
        default: x = r; z = b.w - 1 - c; break;
        }
        x += x0;
        z += z0;
    }
    int rotDir(int k) const {
        int dx = DX[k], dz = DZ[k], rx, rz;
        switch (rot) {
        case 0: rx = dx; rz = dz; break;
        case 1: rx = -dz; rz = dx; break;
        case 2: rx = -dx; rz = -dz; break;
        default: rx = dz; rz = -dx; break;
        }
        return rx == 1 ? 0 : rz == 1 ? 1 : rx == -1 ? 2 : 3;
    }
};

// Поворот, при котором «вперёд» куска (-Z чертежа, направление 3) смотрит в мировое направление dir
inline int rotForForward(int dir) {
    for (int rot = 0; rot < 4; ++rot) {
        Placement p;
        p.rot = rot;
        if (p.rotDir(3) == dir) return rot;
    }
    return 0;
}

// Разместить кусок так, чтобы клетка входа (c, r) оказалась в мировой точке (wx, wz) на полу wy (слой le)
inline Placement placeAt(const Blueprint& b, int rot, int c, int r, int le, int wx, int wy, int wz) {
    Placement p;
    p.rot = rot;
    p.w = (rot & 1) ? b.d : b.w;
    p.d = (rot & 1) ? b.w : b.d;
    int x, z;
    p.toWorld(b, c, r, x, z); // при x0 = z0 = 0
    p.x0 = wx - x;
    p.z0 = wz - z;
    p.y0 = wy - le;
    return p;
}

inline uint32_t xs(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
inline int rr(uint32_t& s, int lo, int hi) { return hi <= lo ? lo : lo + (int)(xs(s) % (uint32_t)(hi - lo + 1)); }

} // namespace skit
