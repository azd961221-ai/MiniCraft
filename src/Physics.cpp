#include "Physics.h"
#include <algorithm>
#include <cmath>

namespace {

// Тонкая пластина у стороны s (0 +X, 1 +Z, 2 -X, 3 -Z) толщиной t
AABB sidePlate(int s, float t, float h = 1.f) {
    switch (s & 3) {
    case 0: return {{1.f - t, 0.f, 0.f}, {1.f, h, 1.f}};
    case 1: return {{0.f, 0.f, 1.f - t}, {1.f, h, 1.f}};
    case 2: return {{0.f, 0.f, 0.f}, {t, h, 1.f}};
    default: return {{0.f, 0.f, 0.f}, {1.f, h, t}};
    }
}

bool paneConnects(uint8_t b) { return isOpaque(b) || b == GLASS_PANE || b == IRON_BARS || b == GLASS; }

// Отсечение сдвига d по оси a коробкой b (как calculateXOffset)
float clipAxis(const AABB& b, const AABB& e, int a, float d) {
    int o1 = (a + 1) % 3, o2 = (a + 2) % 3;
    if (e.mx[o1] <= b.mn[o1] || e.mn[o1] >= b.mx[o1] || e.mx[o2] <= b.mn[o2] || e.mn[o2] >= b.mx[o2]) return d;
    if (d > 0.f && e.mx[a] <= b.mn[a]) d = std::min(d, b.mn[a] - e.mx[a]);
    else if (d < 0.f && e.mn[a] >= b.mx[a]) d = std::max(d, b.mx[a] - e.mn[a]);
    return d;
}

float moveAxis(const std::vector<AABB>& boxes, AABB& e, int a, float d) {
    for (const AABB& b : boxes) d = clipAxis(b, e, a, d);
    e.mn[a] += d;
    e.mx[a] += d;
    return d;
}

AABB expand(const AABB& b, const glm::vec3& m) {
    AABB r = b;
    for (int a = 0; a < 3; ++a) {
        if (m[a] < 0.f) r.mn[a] += m[a];
        else r.mx[a] += m[a];
    }
    return r;
}

} // namespace

// Локальные коробки блока в [0,1]^3 (заборы и калитки выше — 1.5)
static void localBoxes(const World& w, uint8_t b, uint8_t meta, int x, int y, int z, std::vector<AABB>& out) {
    const float k = 1.f / 16.f;
    const Shape s0 = blockShape(b);
    switch (s0) {
    case Shape::Slab: out.push_back({{0, 0, 0}, {1, 0.5f, 1}}); return;
    case Shape::Bed: out.push_back({{0, 0, 0}, {1, 9 * k, 1}}); return;
    case Shape::Table: out.push_back({{0, 0, 0}, {1, 0.75f, 1}}); return;
    case Shape::LilyPad: out.push_back({{0, 0, 0}, {1, 1.f / 64.f, 1}}); return;
    case Shape::Frame: out.push_back({{0, 0, 0}, {1, 13 * k, 1}}); if (meta & 4) out.push_back({{5 * k, 13 * k, 5 * k}, {11 * k, 1, 11 * k}}); return;
    case Shape::Egg: out.push_back({{k, 0, k}, {1 - k, 1, 1 - k}}); return;
    case Shape::Brewing:
        out.push_back({{0, 0, 0}, {1, 2 * k, 1}});
        out.push_back({{7 * k, 0, 7 * k}, {9 * k, 14 * k, 9 * k}});
        return;
    case Shape::Cauldron:
        out.push_back({{0, 0, 0}, {1, 5 * k, 1}});
        out.push_back({{0, 0, 0}, {2 * k, 1, 1}});
        out.push_back({{1 - 2 * k, 0, 0}, {1, 1, 1}});
        out.push_back({{0, 0, 0}, {1, 1, 2 * k}});
        out.push_back({{0, 0, 1 - 2 * k}, {1, 1, 1}});
        return;
    case Shape::Cube:
        if (isSolid(b)) out.push_back({{0, 0, 0}, {1, b == SOUL_SAND ? 14 * k : 1.f, 1}});
        return;
    case Shape::Piston: case Shape::PistonHead: {
        int f = meta & 7;
        auto add = [&](glm::vec3 a, glm::vec3 c) {
            glm::vec3 p = orientPoint(a, f), q = orientPoint(c, f);
            out.push_back({glm::min(p, q), glm::max(p, q)});
        };
        if (s0 == Shape::Piston) {
            if (meta & 8) add({0, 0, 0}, {1, 12 * k, 1});
            else add({0, 0, 0}, {1, 1, 1});
        } else {
            add({0, 12 * k, 0}, {1, 1, 1});
            add({6 * k, -4 * k, 6 * k}, {10 * k, 12 * k, 10 * k});
        }
        return;
    }
    case Shape::Stairs: {
        bool up = (meta & 4) != 0;
        out.push_back({{0, up ? 0.5f : 0.f, 0}, {1, up ? 1.f : 0.5f, 1}});
        float y0 = up ? 0.f : 0.5f, y1 = up ? 0.5f : 1.f;
        switch (meta & 3) {
        case 0: out.push_back({{0.5f, y0, 0}, {1, y1, 1}}); break;
        case 1: out.push_back({{0, y0, 0}, {0.5f, y1, 1}}); break;
        case 2: out.push_back({{0, y0, 0.5f}, {1, y1, 1}}); break;
        default: out.push_back({{0, y0, 0}, {1, y1, 0.5f}}); break;
        }
        return;
    }
    case Shape::Ladder: out.push_back(sidePlate(meta, 2 * k)); return;
    case Shape::Door: {
        int f = meta & 3;
        bool open = meta & 4;
        out.push_back(sidePlate(open ? (f + 3) & 3 : (f + 2) & 3, 3 * k));
        return;
    }
    case Shape::Trapdoor:
        if (meta & 4) out.push_back(sidePlate(meta & 3, 3 * k));
        else out.push_back({{0, 0, 0}, {1, 3 * k, 1}});
        return;
    case Shape::FenceGate:
        if (meta & 4) return;
        if ((meta & 1) == 0) out.push_back({{0.375f, 0, 0}, {0.625f, 1.5f, 1}});
        else out.push_back({{0, 0, 0.375f}, {1, 1.5f, 0.625f}});
        return;
    case Shape::Fence: {
        auto connects = [&](int nx, int ny, int nz) {
            uint8_t n = w.getBlock(nx, ny, nz);
            return n == b || isFence(n) || n == FENCE_GATE || isOpaque(n);
        };
        bool px = connects(x + 1, y, z);
        bool nx = connects(x - 1, y, z);
        bool pz = connects(x, y, z + 1);
        bool nz = connects(x, y, z - 1);
        const float a = 6.f * k, bb = 10.f * k;
        out.push_back({{a, 0.f, a}, {bb, 1.5f, bb}});
        if (nx) out.push_back({{0.f, 0.f, a}, {a, 1.5f, bb}});
        if (px) out.push_back({{bb, 0.f, a}, {1.f, 1.5f, bb}});
        if (nz) out.push_back({{a, 0.f, 0.f}, {bb, 1.5f, a}});
        if (pz) out.push_back({{a, 0.f, bb}, {bb, 1.5f, 1.f}});
        return;
    }
    case Shape::Pane: {
        bool px = paneConnects(w.getBlock(x + 1, y, z)), nx = paneConnects(w.getBlock(x - 1, y, z));
        bool pz = paneConnects(w.getBlock(x, y, z + 1)), nz = paneConnects(w.getBlock(x, y, z - 1));
        const float a = 7 * k, c = 9 * k;
        if (!px && !nx && !pz && !nz) px = nx = pz = nz = true;
        if (px || nx) out.push_back({{nx ? 0.f : a, 0, a}, {px ? 1.f : c, 1, c}});
        if (pz || nz) out.push_back({{a, 0, nz ? 0.f : a}, {c, 1, pz ? 1.f : c}});
        return;
    }
    case Shape::Cactus: out.push_back({{k, 0, k}, {1 - k, 1 - k, 1 - k}}); return;
    case Shape::Cake: out.push_back({{(1 + 2 * (meta & 7)) * k, 0, k}, {1 - k, 0.5f, 1 - k}}); return;
    default:
        if (isSolid(b)) out.push_back({{0, 0, 0}, {1, 1, 1}});
        return;
    }
}

void selectionBoxes(const World& w, int x, int y, int z, std::vector<AABB>& out) {
    uint8_t b = w.getBlock(x, y, z);
    uint8_t meta = w.getMeta(x, y, z);
    Shape s = blockShape(b);
    size_t first = out.size();
    if (s == Shape::Slab || s == Shape::Stairs || s == Shape::Bed || s == Shape::Piston || s == Shape::PistonHead || s == Shape::Table ||
        s == Shape::Frame || s == Shape::Egg || s == Shape::Pane || s == Shape::Cake || s == Shape::Door ||
        s == Shape::Trapdoor || s == Shape::Ladder || s == Shape::Fence || (s == Shape::FenceGate && !(meta & 4))) {
        localBoxes(w, b, meta, x, y, z, out);
        for (size_t i = first; i < out.size(); ++i) out[i].mx.y = std::min(out[i].mx.y, 1.f); // калитка и забор — до верха блока
    }
    if (out.size() == first) {
        AABB a;
        blockBounds(b, meta, x, z, a.mn, a.mx);
        out.push_back(a);
    }
}

void blockCollision(const World& w, int x, int y, int z, std::vector<AABB>& out) {
    uint8_t b = w.getBlock(x, y, z);
    if (b == AIR) return;
    size_t first = out.size();
    localBoxes(w, b, w.getMeta(x, y, z), x, y, z, out);
    glm::vec3 o((float)x, (float)y, (float)z);
    for (size_t i = first; i < out.size(); ++i) {
        out[i].mn += o;
        out[i].mx += o;
    }
}

void collectCollision(const World& w, const AABB& r, std::vector<AABB>& out) {
    int x0 = (int)std::floor(r.mn.x), x1 = (int)std::floor(r.mx.x);
    int y0 = (int)std::floor(r.mn.y) - 1, y1 = (int)std::floor(r.mx.y); // -1: заборы высотой 1.5
    int z0 = (int)std::floor(r.mn.z), z1 = (int)std::floor(r.mx.z);
    std::vector<AABB> tmp;
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                tmp.clear();
                blockCollision(w, x, y, z, tmp);
                for (auto& b : tmp)
                    if (b.overlaps(r)) out.push_back(b);
            }
}

bool anyCollision(const World& w, const AABB& box) {
    std::vector<AABB> v;
    collectCollision(w, box, v);
    return !v.empty();
}

bool pointCollides(const World& w, const glm::vec3& p) {
    std::vector<AABB> v;
    blockCollision(w, (int)std::floor(p.x), (int)std::floor(p.y), (int)std::floor(p.z), v);
    for (auto& b : v)
        if (b.contains(p)) return true;
    return false;
}

void moveBody(const World& w, glm::vec3& pos, float hw, float h, glm::vec3& motion, float stepHeight, bool sneak,
              BodyState& st) {
    AABB box = bodyBox(pos, hw, h);
    glm::vec3 m = motion;

    // Крадущийся не сходит с края: уменьшаем сдвиг, пока под ногами есть опора
    if (sneak && st.onGround) {
        const float s = 0.05f;
        auto noFloor = [&](float dx, float dz) {
            AABB t = box;
            t.mn += glm::vec3(dx, -1.f, dz);
            t.mx += glm::vec3(dx, -1.f, dz);
            t.mx.y = box.mn.y - 0.001f; // только то, что ниже ступней
            t.mn.y = box.mn.y - 1.f;
            return !anyCollision(w, t);
        };
        auto shrink = [&](float& v) { v = (v < s && v >= -s) ? 0.f : v > 0.f ? v - s : v + s; };
        while (m.x != 0.f && noFloor(m.x, 0.f)) shrink(m.x);
        while (m.z != 0.f && noFloor(0.f, m.z)) shrink(m.z);
        while (m.x != 0.f && m.z != 0.f && noFloor(m.x, m.z)) { shrink(m.x); shrink(m.z); }
    }

    const glm::vec3 want = m;
    const AABB start = box;
    std::vector<AABB> boxes;
    collectCollision(w, expand(box, m), boxes);
    glm::vec3 d;
    d.y = moveAxis(boxes, box, 1, m.y);
    d.x = moveAxis(boxes, box, 0, m.x);
    d.z = moveAxis(boxes, box, 2, m.z);

    bool grounded = st.onGround || (d.y != want.y && want.y < 0.f);
    bool stepped = false;
    if (stepHeight > 0.f && grounded && want.y <= 0.f && (d.x != want.x || d.z != want.z)) {
        // Попытка подняться на уступ: вверх, вбок, обратно вниз
        AABB sb = start;
        std::vector<AABB> sboxes;
        collectCollision(w, expand(expand(start, glm::vec3(want.x, stepHeight, want.z)), glm::vec3(0, -stepHeight, 0)), sboxes);
        glm::vec3 sd;
        float up = moveAxis(sboxes, sb, 1, stepHeight);
        sd.x = moveAxis(sboxes, sb, 0, want.x);
        sd.z = moveAxis(sboxes, sb, 2, want.z);
        float down = moveAxis(sboxes, sb, 1, -up);
        sd.y = up + down;
        if (sd.x * sd.x + sd.z * sd.z > d.x * d.x + d.z * d.z) {
            box = sb;
            d = sd;
            stepped = true;
        }
    }

    pos = glm::vec3((box.mn.x + box.mx.x) * 0.5f, box.mn.y, (box.mn.z + box.mx.z) * 0.5f);
    st.collidedH = want.x != d.x || want.z != d.z;
    st.collidedV = stepped || want.y != d.y;
    st.onGround = stepped || (st.collidedV && want.y < 0.f);
    if (want.x != d.x) motion.x = 0.f;
    if (want.z != d.z) motion.z = 0.f;
    if (st.collidedV) motion.y = 0.f;
}
