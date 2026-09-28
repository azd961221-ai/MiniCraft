// Тестовый бот для мультиплеера: подключается к MiniCraftServer, стоит/ходит, выбрасывает предметы, бьёт мобов,
// ломает и ставит блоки, торгует с жителем рядом и печатает, что прислал сервер. Запуск: mpbot [адрес[:порт]] [имя] [секунд]
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <thread>
#include <vector>
#include "../src/Net.h"
#include "../src/NetCodec.h"
#include "../src/Protocol.h"

namespace {
struct BotMob { uint32_t id; int type; float x, y, z; int health; bool dying; };
struct BotItem { uint32_t id; float x, y, z; ItemStack s; };
const char* MOB_NAMES[] = {"pig", "cow", "sheep", "chicken", "zombie", "skeleton", "spider", "creeper", "wolf", "squid", "slime",
                           "enderman", "silverfish", "cavespider", "mooshroom", "snowgolem", "villager", "pigzombie", "ghast",
                           "blaze", "magmacube", "dragon", "crystal", "witherskeleton", "wither", "witch", "bat",
                           "irongolem", "ocelot", "cat", "zombievillager"};
double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
} // namespace

int main(int argc, char** argv) {
    std::string addr = argc > 1 ? argv[1] : "localhost";
    std::string name = argc > 2 ? argv[2] : "Bot";
    double runFor = argc > 3 ? std::atof(argv[3]) : 20.0;
    int port = DEFAULT_PORT;
    if (size_t c = addr.rfind(':'); c != std::string::npos) { port = std::atoi(addr.c_str() + c + 1); addr = addr.substr(0, c); }
    net::Conn conn;
    std::string err;
    if (!conn.connect(addr, port, 4000, err)) { std::printf("CONNECT FAILED: %s\n", err.c_str()); return 1; }
    net::Writer lw;
    lw.u16(PROTOCOL_VERSION);
    lw.str(name);
    conn.send(C_LOGIN, lw);
    conn.flush();

    uint32_t myId = 0;
    float px = 0, py = 0, pz = 0;
    bool logged = false;
    std::vector<BotMob> mobs;
    std::vector<BotItem> items;
    std::map<uint16_t, int> pktCount;
    std::map<uint16_t, size_t> pktBytes;
    std::map<uint32_t, std::string> others;
    int gives = 0, damages = 0, blocks = 0, chunks = 0;
    const double t0 = now();
    double nextTick = t0, lastReport = t0;
    int phase = 0;
    int tradeState = 0; // 0 ищем жителя, 1 ждём окно, 2 ждём подтверждения сделки, 3 пауза, 4 открыли снова, 5 готово
    double tradeAt = 0;
    bool reopen = false;
    uint32_t attackId = 0;
    int attackHealthBefore = -1;
    int maxMobs = 0, maxItems = 0;
    auto log = [&](const std::string& s) { std::printf("[%5.1f] %s\n", now() - t0, s.c_str()); std::fflush(stdout); };

    while (conn.alive && now() - t0 < runFor) {
        conn.poll();
        uint16_t type;
        std::vector<uint8_t> data;
        while (conn.next(type, data)) {
            ++pktCount[type];
            pktBytes[type] += data.size() + 6;
            net::Reader r(data);
            switch (type) {
            case S_LOGIN_OK: {
                myId = r.u32();
                uint32_t seed = r.u32();
                int gen = r.u8();
                int64_t wt = r.i64();
                r.u8(); r.u8();
                px = r.f32(); py = r.f32(); pz = r.f32();
                logged = true;
                char b[200];
                std::snprintf(b, sizeof b, "LOGIN OK id=%u seed=%u gen=%d time=%lld spawn=(%.1f %.1f %.1f)", myId, seed, gen, (long long)wt, px, py, pz);
                log(b);
                break;
            }
            case S_KICK: log("KICKED: " + r.str()); break;
            case S_KEEPALIVE: { net::Writer w; w.u32(r.u32()); conn.send(C_KEEPALIVE, w); break; }
            case S_PLAYER_LIST: {
                int n = r.u16();
                std::string s = "PLAYER_LIST:";
                for (int i = 0; i < n && r.ok; ++i) {
                    r.u32();
                    std::string nm = r.str();
                    int ping = r.u16();
                    r.u8();
                    s += " " + nm + "(" + std::to_string(ping) + "ms)";
                }
                if (pktCount[S_PLAYER_LIST] <= 1 || pktCount[S_PLAYER_LIST] % 10 == 0) log(s);
                break;
            }
            case S_CHAT: log("CHAT: " + r.str()); break;
            case S_PLAYER_ADD: { uint32_t id = r.u32(); others[id] = r.str(); log("PLAYER_ADD " + others[id]); break; }
            case S_GIVE: { ItemStack s = readItem(r); ++gives; log("GIVE id=" + std::to_string(s.id) + " x" + std::to_string(s.count)); break; }
            case S_DAMAGE: {
                int dmg = r.u16(); int src = (int8_t)r.u8();
                ++damages;
                log("DAMAGE " + std::to_string(dmg) + " src=" + std::to_string(src));
                break;
            }
            case S_TRADE: {
                // Торговля: первое S_TRADE — окно открылось (берём сделку 0), второе — обновление после сделки
                uint32_t mid = r.u32();
                int n = r.u8();
                std::string s = "TRADE villager=" + std::to_string(mid) + " offers=" + std::to_string(n) + ":";
                for (int i = 0; i < n && r.ok; ++i) {
                    ItemStack b1 = readItem(r), b2 = readItem(r), sl = readItem(r);
                    int closed = r.u8();
                    s += " [" + std::to_string(b1.id) + "x" + std::to_string(b1.count) + (b2.empty() ? "" : "+" + std::to_string(b2.id)) + "->" +
                         std::to_string(sl.id) + "x" + std::to_string(sl.count) + (closed ? " closed" : "") + "]";
                }
                log(r.ok ? s : "!!! S_TRADE parse error");
                if (tradeState == 1) {
                    net::Writer w; w.u32(mid); w.u16(0);
                    conn.send(C_TRADE, w);
                    log("sent C_TRADE offer 0");
                    tradeState = 2;
                } else if (tradeState == 2) {
                    conn.send(C_CLOSE);
                    log("trade confirmed by server, sent C_CLOSE");
                    tradeState = 3;
                    tradeAt = now();
                } else if (tradeState == 4) {
                    conn.send(C_CLOSE);
                    log("reopened: villager restocked after the last offer (expect one more offer)");
                    tradeState = 5;
                }
                break;
            }
            case S_BLOCKS: blocks += (int)r.u32(); break;
            case S_CHUNK: ++chunks; break;
            case S_ENTITIES: {
                mobs.clear();
                int n = r.u16();
                for (int i = 0; i < n && r.ok; ++i) {
                    BotMob m{};
                    m.id = r.u32(); m.type = r.u8(); m.x = r.f32(); m.y = r.f32(); m.z = r.f32(); r.f32();
                    uint16_t f = r.u16(); r.u8(); r.u8(); m.health = r.u16();
                    for (int k = 0; k < 8; ++k) r.u8();
                    for (int k = 0; k < 5; ++k) r.f32();
                    r.u32();
                    m.dying = f & 1;
                    mobs.push_back(m);
                }
                items.clear();
                n = r.u16();
                for (int i = 0; i < n && r.ok; ++i) {
                    BotItem it{};
                    it.id = r.u32(); it.x = r.f32(); it.y = r.f32(); it.z = r.f32(); r.u16(); r.f32();
                    it.s = readItem(r);
                    items.push_back(it);
                }
                if (!r.ok) log("!!! S_ENTITIES parse error");
                maxMobs = std::max(maxMobs, (int)mobs.size());
                maxItems = std::max(maxItems, (int)items.size());
                break;
            }
            default: break;
            }
        }
        if (!logged) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); continue; }

        double t = now() - t0;
        if (now() >= nextTick) {
            nextTick += 0.05;
            PlayerNet me;
            me.x = px; me.y = py; me.z = pz; me.flags = 1;
            net::Writer w;
            writePlayerNet(w, me);
            w.u32(0); w.f32(0); w.f32(0);
            conn.send(C_PLAYER, w);
        }
        auto nearestMob = [&]() -> const BotMob* {
            const BotMob* best = nullptr;
            float bd = 1e9f;
            for (auto& m : mobs) {
                if (m.dying || m.type >= 21) continue;
                float d = std::hypot(m.x - px, m.z - pz);
                if (d < bd) { bd = d; best = &m; }
            }
            return best;
        };
        if (tradeState == 3 && now() - tradeAt > 3.0) { // через 40 тиков после сделки с последней позицией — новый товар
            tradeState = 0;
            reopen = true;
        }
        if (tradeState == 0 && t > 3.0) {
            for (auto& m : mobs)
                if (m.type == 16 && !m.dying && std::hypot(m.x - px, m.z - pz) < 10.f) {
                    net::Writer w; w.u32(m.id); writeItem(w, ItemStack{});
                    conn.send(C_INTERACT, w);
                    log("right-click villager " + std::to_string(m.id));
                    tradeState = reopen ? 4 : 1;
                    break;
                }
        }
        if (phase == 0 && t > 4.0) {
            phase = 1;
            std::map<int, int> byType;
            for (auto& m : mobs) ++byType[m.type];
            std::string s = "MOBS in snapshot: " + std::to_string(mobs.size()) + " (";
            for (auto& [k, v] : byType) s += std::string(MOB_NAMES[k < 31 ? k : 0]) + "=" + std::to_string(v) + " ";
            log(s + ")  items=" + std::to_string(items.size()));
            if (const BotMob* m = nearestMob()) {
                char b[160];
                std::snprintf(b, sizeof b, "nearest mob %s at dist %.1f", MOB_NAMES[m->type], std::hypot(m->x - px, m->z - pz));
                log(b);
            }
            // Выбросить 5 земли перед собой
            net::Writer w;
            w.f32(px); w.f32(py + 1.62f); w.f32(pz); w.f32(1); w.f32(0); w.f32(0);
            writeItem(w, makeStack(DIRT, 5));
            conn.send(C_DROP, w);
            log("sent C_DROP 5 dirt");
        }
        if (name == "Bot2" && t > 6.0 && t < 6.2 && !others.empty()) {
            net::Writer w;
            w.u32(others.begin()->first); w.u16(4); w.f32(1.f);
            conn.send(C_ATTACK_PLAYER, w);
            log("PvP: hit " + others.begin()->second);
        }
        if (phase == 1 && t > 5.0) {
            phase = 2;
            bool seen = false;
            for (auto& it : items) seen |= it.s.id == DIRT && std::hypot(it.x - px, it.z - pz) < 6.f;
            log(std::string("dropped dirt visible in snapshot: ") + (seen ? "YES" : "NO") + " (items=" + std::to_string(items.size()) + ")");
        }
        if (phase == 2 && t > 8.0) {
            phase = 3;
            log("gives so far: " + std::to_string(gives) + " (expect dirt back after pickup delay)");
            if (const BotMob* m = nearestMob()) {
                // Встать рядом с мобом и ударить
                px = m->x + 1.5f; py = m->y; pz = m->z;
                attackId = m->id;
                attackHealthBefore = m->health;
                log(std::string("teleport next to ") + MOB_NAMES[m->type] + " hp=" + std::to_string(m->health));
            }
        }
        if (phase == 3 && t > 9.0) {
            phase = 4;
            if (attackId) {
                net::Writer w;
                w.u32(attackId); w.u16(3); w.f32(1.f); w.u8(0); w.u8(0);
                conn.send(C_ATTACK, w);
                log("sent C_ATTACK dmg=3");
            }
        }
        if (phase == 4 && t > 10.0) {
            phase = 5;
            for (auto& m : mobs)
                if (m.id == attackId) log("mob hp after attack: " + std::to_string(m.health) + " (was " + std::to_string(attackHealthBefore) + ")");
            // Сломать блок под ногами
            net::Writer w;
            int bx = (int)std::floor(px), by = (int)std::floor(py) - 1, bz = (int)std::floor(pz);
            w.i32(bx); w.u8((uint8_t)by); w.i32(bz);
            w.u16(0); w.u16(0); for (int k = 0; k < 4; ++k) w.u16(0);
            w.u8(0);
            conn.send(C_DIG, w);
            log("sent C_DIG under feet (" + std::to_string(bx) + "," + std::to_string(by) + "," + std::to_string(bz) + ")");
        }
        if (phase == 5 && t > 11.0) {
            phase = 6;
            bool seen = false;
            for (auto& it : items) seen |= std::hypot(it.x - px, it.z - pz) < 4.f;
            log(std::string("drop from dug block visible: ") + (seen ? "YES" : "NO") + ", block changes received: " + std::to_string(blocks));
        }
        if (phase == 6 && t > 12.0) {
            phase = 7;
            // Встать рядом с зомби/пауком/скелетом и подождать удара
            const BotMob* best = nullptr;
            float bd = 1e9f;
            for (auto& m : mobs) {
                if (m.dying || (m.type != 4 && m.type != 6 && m.type != 13)) continue; // зомби, паук, пещерный паук
                float d = std::hypot(m.x - px, m.z - pz);
                if (d < bd) { bd = d; best = &m; }
            }
            if (best) {
                px = best->x + 0.8f; py = best->y; pz = best->z;
                log(std::string("waiting next to ") + MOB_NAMES[best->type] + ", damages so far " + std::to_string(damages));
            } else {
                log("no hostile mob to test damage");
            }
        }
        if (phase == 7 && t > runFor - 1.0) {
            phase = 8;
            log("damages received while standing next to a monster: " + std::to_string(damages));
        }
        if (t - (lastReport - t0) > 5.0) {
            lastReport = now();
            size_t total = 0;
            for (auto& [k, v] : pktBytes) total += v;
            log("traffic so far: " + std::to_string(total / 1024) + " KB, mobs=" + std::to_string(mobs.size()) + " items=" +
                std::to_string(items.size()) + " chunks=" + std::to_string(chunks) + " out-queue=" + std::to_string(conn.pendingOut()));
        }
        conn.flush();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    double dur = now() - t0;
    std::printf("=== SUMMARY (%.1fs, alive=%d)\n", dur, conn.alive);
    std::printf("max mobs=%d max items=%d gives=%d damages=%d blockchanges=%d chunks=%d others=%zu\n", maxMobs, maxItems, gives,
                damages, blocks, chunks, others.size());
    for (auto& [k, v] : pktCount) std::printf("  pkt %3u: %6d packets %8zu bytes\n", k, v, pktBytes[k]);
    return 0;
}
