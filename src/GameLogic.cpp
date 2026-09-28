#include "GameLogic.h"
#include <algorithm>
#include "Crafting.h"
#include "Enchant.h"
#include "Potion.h"

namespace {
float frand(uint32_t& s) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return (s & 0xFFFFFF) / float(0x1000000);
}
} // namespace

void harvestBlock(World& w, MobManager& mm, std::vector<ItemEntity>& items, const glm::ivec3& p, const ItemStack& held,
                  bool creative, int dimension, uint32_t& rng) {
    uint8_t b = w.getBlock(p.x, p.y, p.z);
    uint8_t meta = w.getMeta(p.x, p.y, p.z);
    if (b == AIR) return;
    if (TileEntity* te = w.tileAt(p.x, p.y, p.z)) {
        for (int i = 0; i < te->size(); ++i) dropFromBlock(items, p, te->items[i], rng);
        w.removeTile(p.x, p.y, p.z);
    }
    if (b == MONSTER_EGG && !creative) mm.spawn(MobType::Silverfish, glm::vec3(p) + glm::vec3(0.5f, 0.f, 0.5f), frand(rng) * 360.f);
    if (b == JUKEBOX && meta > 0) dropFromBlock(items, p, makeStack((uint16_t)(RECORD_13 + meta - 1)), rng);
    // Лёд над опорой тает в воду (в Незере — нет: вода там испаряется)
    uint8_t below = w.getBlock(p.x, p.y - 1, p.z);
    bool iceToWater = b == ICE && !creative && dimension != -1 && (isSolid(below) || isLiquid(below));
    w.setBlock(p.x, p.y, p.z, iceToWater ? WATER : AIR);
    if (creative) return;
    if (held.id == SHEARS && (b == LEAVES || b == TALL_GRASS || b == VINE)) {
        dropFromBlock(items, p, makeStack(b, 1, b == LEAVES ? (uint16_t)(meta & 3) : b == TALL_GRASS ? 1 : 0), rng);
        return;
    }
    if (!canHarvest(b, held)) return;
    int silk = held.enchLevel(ENCH_SILK_TOUCH), fortune = held.enchLevel(ENCH_FORTUNE);
    bool silkable = (isOpaque(b) || b == GLASS || b == ICE || b == GLASS_PANE) && !hasGui(b) && b != BEDROCK && b != MOB_SPAWNER &&
                    b != FURNACE_LIT && b != JUKEBOX && b != NOTE_BLOCK && b != PISTON && b != STICKY_PISTON;
    if (silk > 0 && silkable) {
        dropFromBlock(items, p, makeStack(b, 1, blockHasVariants(b) ? (uint16_t)(b == SLAB || b == DOUBLE_SLAB ? meta & 7 : meta & 15) : 0), rng);
        return;
    }
    for (ItemStack d : blockDrops(b, meta, rng)) {
        if (fortune > 0 && (b == COAL_ORE || b == DIAMOND_ORE || b == LAPIS_ORE)) {
            int mul = std::max(0, (int)(frand(rng) * (fortune + 2)) - 1) + 1;
            d.count = (uint8_t)std::min(64, d.count * mul);
        } else if (fortune > 0 && b == REDSTONE_ORE) {
            d.count = (uint8_t)(d.count + (int)(frand(rng) * (fortune + 1)));
        }
        dropFromBlock(items, p, d, rng);
    }
}

void tickTileEntities(World& w) {
    for (auto& [key, te] : w.tiles) {
        if (te.type == TileEntity::Brewing) {
            // Варка 20 секунд: ингредиент меняет все подходящие бутылки
            ItemStack& ing = te.items[3];
            bool can = !ing.empty() && isBrewingIngredient(ing.id);
            if (can) {
                can = false;
                for (int i = 0; i < 3; ++i)
                    if (te.items[i].id == POTION && brewResult(ing.id, te.items[i].damage) != te.items[i].damage) can = true;
            }
            if (te.cookTime > 0) {
                if (!can) te.cookTime = 0;
                else if (--te.cookTime == 0) {
                    for (int i = 0; i < 3; ++i)
                        if (te.items[i].id == POTION) te.items[i].damage = brewResult(ing.id, te.items[i].damage);
                    if (--ing.count == 0) ing.clear();
                }
            } else if (can) {
                te.cookTime = 400;
            }
            continue;
        }
        if (te.type != TileEntity::Furnace) continue;
        if (tickFurnace(te)) {
            uint8_t m = w.getMeta(te.x, te.y, te.z);
            w.setBlock(te.x, te.y, te.z, te.burnTime > 0 ? FURNACE_LIT : FURNACE, m);
        }
    }
}

void processDispense(World& w, MobManager& mm, std::vector<ItemEntity>& items, uint32_t& rng, const SoundFn& sound) {
    for (auto& de : w.dispenseEvents) {
        static const float DV[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        glm::vec3 dir(DV[de.dir][0], DV[de.dir][1], DV[de.dir][2]);
        glm::vec3 from = glm::vec3(de.pos) + 0.5f + dir * 0.6f;
        glm::vec3 sp = glm::vec3(de.pos) + 0.5f;
        if (de.item.id == ARROW) {
            mm.shootArrow(from, dir + glm::vec3(0, 0.1f, 0), 1.1f, 6.f, true, false, rng);
            sound("random/bow", 1.f, 1.2f, sp);
        } else if (de.item.id == SNOWBALL || de.item.id == EGG) {
            mm.throwItem(from, dir + glm::vec3(0, 0.1f, 0), de.item.id, rng);
            sound("random/bow", 1.f, 1.2f, sp);
        } else {
            ItemEntity e;
            e.pos = e.prev = from - glm::vec3(0, 0.15f, 0);
            float spd = frand(rng) * 0.1f + 0.2f;
            e.motion = dir * spd + glm::vec3(frand(rng) - 0.5f, frand(rng) - 0.5f, frand(rng) - 0.5f) * 0.0225f;
            e.motion.y += 0.2f;
            e.stack = de.item;
            e.bobOffset = frand(rng) * 6.28f;
            items.push_back(e);
            sound("random/click", 1.f, 1.f, sp);
        }
    }
    w.dispenseEvents.clear();
}

void dropPopped(World& w, std::vector<ItemEntity>& items, bool creative, uint32_t& rng) {
    if (!creative)
        for (auto& pp : w.popped)
            for (const ItemStack& d : blockDrops(pp.block, pp.meta, rng)) dropFromBlock(items, pp.pos, d, rng);
}
