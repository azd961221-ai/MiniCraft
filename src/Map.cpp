#include "Map.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include "Saves.h"

namespace {
enum MapColor { MC_AIR, MC_GRASS, MC_SAND, MC_CLOTH, MC_TNT, MC_ICE, MC_IRON, MC_FOLIAGE, MC_SNOW, MC_CLAY, MC_DIRT, MC_STONE, MC_WATER, MC_WOOD, MC_COUNT };
// RGB цветов материалов (MapColor 1.0)
const uint32_t MAP_RGB[MC_COUNT] = {0x000000, 0x7FB238, 0xF7E9A3, 0xA7A7A7, 0xFF0000, 0xA0A0FF, 0xA7A7A7,
                                    0x007C00, 0xFFFFFF, 0xA4A8B8, 0xB76A2F, 0x707070, 0x4040FF, 0x685332};
constexpr int MAP_W = 128;
} // namespace

int blockMapColor(uint8_t b) {
    switch (b) {
    case AIR: case GLASS: case GLASS_PANE: case TORCH: case GLOWSTONE: case FIRE: case RAIL: case POWERED_RAIL: case DETECTOR_RAIL:
    case REDSTONE_WIRE: case REDSTONE_TORCH_OFF: case REDSTONE_TORCH_ON: case LEVER: case STONE_BUTTON: case LADDER: case PORTAL:
    case END_PORTAL: case REPEATER_OFF: case REPEATER_ON: case CAKE:
        return MC_AIR;
    case GRASS: case MYCELIUM: return MC_GRASS;
    case SAND: case SANDSTONE: case GRAVEL: case SOUL_SAND: return MC_SAND;
    case WOOL: case BED: case COBWEB: return MC_CLOTH;
    case TNT: case LAVA: return MC_TNT;
    case ICE: return MC_ICE;
    case IRON_BLOCK: case GOLD_BLOCK: case DIAMOND_BLOCK: case LAPIS_BLOCK: case IRON_DOOR: case IRON_BARS: case CAULDRON:
    case BREWING_STAND:
        return MC_IRON;
    case LEAVES: case TALL_GRASS: case ROSE: case DANDELION: case SAPLING: case WHEAT: case DEAD_BUSH: case REEDS:
    case BROWN_MUSHROOM: case RED_MUSHROOM: case CACTUS: case PUMPKIN: case JACK_O_LANTERN: case MELON_BLOCK: case PUMPKIN_STEM:
    case MELON_STEM: case VINE: case LILY_PAD: case NETHER_WART: case DRAGON_EGG:
        return MC_FOLIAGE;
    case SNOW_LAYER: case SNOW_BLOCK: return MC_SNOW;
    case CLAY: case MONSTER_EGG: return MC_CLAY;
    case DIRT: case FARMLAND: return MC_DIRT;
    case WATER: return MC_WATER;
    case LOG: case PLANKS: case BOOKSHELF: case CRAFTING_TABLE: case CHEST: case WOOD_STAIRS: case SPRUCE_STAIRS: case BIRCH_STAIRS:
    case JUNGLE_STAIRS: case WOOD_SLAB: case DOUBLE_WOOD_SLAB: case FENCE: case WOOD_DOOR:
    case TRAPDOOR: case FENCE_GATE: case SIGN_POST: case WALL_SIGN: case NOTE_BLOCK: case JUKEBOX: case WOOD_PLATE:
        return MC_WOOD;
    default: return isSolid(b) ? MC_STONE : MC_AIR;
    }
}

uint32_t mapPixelRGBA(uint8_t index) {
    int c = index / 4;
    if (c <= 0 || c >= MC_COUNT) return 0;
    static const int MUL[4] = {180, 220, 255, 135};
    int k = MUL[index & 3];
    uint32_t rgb = MAP_RGB[c];
    uint32_t r = ((rgb >> 16) & 255) * k / 255, g = ((rgb >> 8) & 255) * k / 255, b = (rgb & 255) * k / 255;
    return r | (g << 8) | (b << 16) | 0xFF000000u; // байты в памяти: R, G, B, A
}

void updateMap(MapData& m, const World& w, const glm::vec3& pp, int dim, bool noSky) {
    if (dim != m.dim) return;
    const int s = 1 << m.scale;
    const int px = (int)std::floor((pp.x - m.xCenter) / s) + MAP_W / 2;
    const int pz = (int)std::floor((pp.z - m.zCenter) / s) + MAP_W / 2;
    int radius = 128 / s;
    if (noSky) radius /= 2;
    ++m.ticks;
    for (int x = px - radius + 1; x < px + radius; ++x) {
        if ((x & 15) != (m.ticks & 15)) continue; // за тик — каждый 16-й столбец
        double prevH = 0.0;
        for (int z = pz - radius - 1; z < pz + radius; ++z) {
            if (x < 0 || z < -1 || x >= MAP_W || z >= MAP_W) continue;
            int dx = x - px, dz = z - pz;
            bool edge = dx * dx + dz * dz > (radius - 2) * (radius - 2);
            int wx = (m.xCenter / s + x - MAP_W / 2) * s, wz = (m.zCenter / s + z - MAP_W / 2) * s;
            int votes[MC_COUNT] = {};
            double h = 0.0;
            int depth = 0;
            bool loaded = w.isChunkLoaded(floorDiv(wx, CW), floorDiv(wz, CW));
            if (!loaded) { prevH = 0.0; continue; }
            if (noSky) {
                // Незер: крыша сверху не видна — пятнистый серо-коричневый «шум»
                uint32_t k = (uint32_t)(wx + wz * 231871);
                k = k * k * 0x1d2e5b17u + k * 11u;
                if (((k >> 20) & 1) == 0) votes[MC_DIRT] += 10;
                else votes[MC_STONE] += 100;
                h = 100.0;
            } else {
                const Chunk* ch = w.chunkAt(floorDiv(wx, CW), floorDiv(wz, CW));
                for (int i = 0; i < s; ++i)
                    for (int j = 0; j < s; ++j) {
                        int bx = wx + i, bz = wz + j;
                        int y = CH - 1;
                        const Chunk* cc = (floorDiv(bx, CW) == ch->cx && floorDiv(bz, CW) == ch->cz) ? ch : w.chunkAt(floorDiv(bx, CW), floorDiv(bz, CW));
                        if (cc) y = cc->height[(bz - cc->cz * CW) * CW + (bx - cc->cx * CW)];
                        int col = MC_AIR;
                        uint8_t b = AIR;
                        while (y > 0) {
                            b = w.getBlock(bx, y - 1, bz);
                            col = blockMapColor(b);
                            if (col != MC_AIR) break;
                            --y;
                        }
                        if (y > 0 && isLiquid(b)) {
                            int yy = y - 1;
                            while (yy > 0 && isLiquid(w.getBlock(bx, yy - 1, bz)) && depth < 1000) { --yy; ++depth; }
                            ++depth;
                        }
                        h += (double)y / (s * s);
                        ++votes[col];
                    }
            }
            depth /= s * s;
            int best = 0, bestN = 0;
            for (int c = 0; c < MC_COUNT; ++c)
                if (votes[c] > bestN) { bestN = votes[c]; best = c; }
            double d = (h - prevH) * 4.0 / (s + 4) + (((x + z) & 1) - 0.5) * 0.4;
            int shade = 1;
            if (d > 0.6) shade = 2;
            if (d < -0.6) shade = 0;
            if (best == MC_WATER) {
                d = depth * 0.1 + ((x + z) & 1) * 0.2;
                shade = 1;
                if (d < 0.5) shade = 2;
                if (d > 0.9) shade = 0;
            }
            prevH = h;
            if (z >= 0 && dx * dx + dz * dz < radius * radius && (!edge || ((x + z) & 1) != 0)) {
                uint8_t nv = (uint8_t)(best * 4 + shade);
                if (m.colors[x + z * MAP_W] != nv) {
                    m.colors[x + z * MAP_W] = nv;
                    m.dirty = m.texDirty = true;
                }
            }
        }
    }
}

bool saveMap(const MapData& m, const std::string& path) {
    FILE* f = openFileUtf8(path + ".tmp", "wb");
    if (!f) return false;
    const uint32_t magic = 0x3150414D; // "MAP1"
    int32_t hdr[4] = {m.xCenter, m.zCenter, m.scale, m.dim};
    std::fwrite(&magic, 4, 1, f);
    std::fwrite(hdr, 4, 4, f);
    std::fwrite(m.colors, 1, sizeof m.colors, f);
    bool ok = !std::ferror(f);
    ok = std::fclose(f) == 0 && ok;
    return ok && commitFile(path + ".tmp", path);
}

bool loadMap(MapData& m, const std::string& path) {
    FILE* f = openFileUtf8(path, "rb");
    if (!f) return false;
    uint32_t magic = 0;
    int32_t hdr[4];
    bool ok = std::fread(&magic, 4, 1, f) == 1 && magic == 0x3150414D && std::fread(hdr, 4, 4, f) == 4 &&
              std::fread(m.colors, 1, sizeof m.colors, f) == sizeof m.colors;
    std::fclose(f);
    if (!ok) return false;
    m.xCenter = hdr[0]; m.zCenter = hdr[1]; m.scale = hdr[2] & 7; m.dim = hdr[3];
    m.dirty = false;
    m.texDirty = true;
    return true;
}
