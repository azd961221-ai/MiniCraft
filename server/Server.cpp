// MiniCraftServer.exe — выделенный сервер MiniCraft (мультиплеер по локальной сети / Wi-Fi или по внешнему адресу).
// Сервер ведёт всё: три измерения (обычный мир, Незер, Край), блоки с полной логикой, воду, огонь, редстоун, рост,
// мобов, выпавшие предметы, стрелы, снаряды, динамит и взрывы, транспорт, печи и сундуки, время и погоду.
// Игроки присылают свои действия; урон по ним сервер пересылает им самим (здоровье и инвентарь — у клиента,
// сохраняются на сервере в players/<имя>.dat). Настройки — server.properties рядом с exe.
#include <algorithm>
#include <cctype>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "../src/Crafting.h"
#include "../src/GameLogic.h"
#include "../src/Net.h"
#include "../src/NetCodec.h"
#include "../src/Particles.h"
#include "../src/Protocol.h"
#include "../src/Saves.h"
#include "../src/World.h"

namespace fs = std::filesystem;

namespace {

std::atomic<bool> g_stop{false};
std::mutex g_consoleMx;
std::vector<std::string> g_consoleLines;

BOOL WINAPI onCtrl(DWORD) { g_stop = true; return TRUE; }

void logf(const std::string& s) {
    auto t = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&t));
    std::printf("[%s] %s\n", buf, s.c_str());
    std::fflush(stdout);
}

std::string exeDir() {
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    return fs::path(p).parent_path().u8string() + "/";
}

struct Props {
    int port = DEFAULT_PORT;
    std::string level = "world";
    std::string seed;
    int gamemode = 0, difficulty = 2, maxPlayers = 8;
    bool hardcore = false;
    bool pvp = true, spawnMonsters = true, spawnAnimals = true;
    std::string motd = "A MiniCraft Server";
    std::vector<std::string> ops; // кому можно серверные команды (игроку на этом же компьютере можно всегда)
};

Props loadProps(const std::string& path) {
    Props p;
    std::ifstream f(fs::u8path(path));
    if (!f) {
        std::ofstream o(fs::u8path(path));
        o << "# MiniCraft server settings\n"
          << "server-port=25565\nlevel-name=world\nlevel-seed=\ngamemode=0\ndifficulty=2\nmax-players=8\n"
          << "hardcore=false\n"
          << "pvp=true\nspawn-monsters=true\nspawn-animals=true\nmotd=A MiniCraft Server\n"
          << "# Players allowed to use server commands, comma separated (the player on this PC always may)\nops=\n";
        return p;
    }
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t e = line.find('=');
        if (e == std::string::npos) continue;
        std::string k = line.substr(0, e), v = line.substr(e + 1);
        if (k == "server-port") p.port = std::atoi(v.c_str());
        else if (k == "level-name") p.level = v.empty() ? "world" : v;
        else if (k == "level-seed") p.seed = v;
        else if (k == "gamemode") p.gamemode = std::atoi(v.c_str());
        else if (k == "difficulty") p.difficulty = std::clamp(std::atoi(v.c_str()), 0, 3);
        else if (k == "hardcore") p.hardcore = v == "true";
        else if (k == "max-players") p.maxPlayers = std::max(1, std::atoi(v.c_str()));
        else if (k == "pvp") p.pvp = v == "true";
        else if (k == "spawn-monsters") p.spawnMonsters = v == "true";
        else if (k == "spawn-animals") p.spawnAnimals = v == "true";
        else if (k == "motd") p.motd = v;
        else if (k == "ops") {
            std::string cur;
            for (char ch : v + ",") {
                if (ch == ',') {
                    while (!cur.empty() && cur.back() == ' ') cur.pop_back();
                    if (!cur.empty()) p.ops.push_back(cur);
                    cur.clear();
                } else if (ch != ' ' || !cur.empty()) {
                    cur += ch;
                }
            }
        }
    }
    return p;
}

// Дневной свет 0..1 (как яркость неба в 1.0) и дождь его приглушает
float skyBrightness(int64_t t, bool rain) {
    float f = (float)(t % 24000) / 24000.f - 0.25f;
    if (f < 0.f) f += 1.f;
    f += (1.f - (std::cos(f * 3.14159265f) + 1.f) / 2.f - f) / 3.f;
    float b = std::clamp(std::cos(f * 6.2831853f) * 2.f + 0.5f, 0.f, 1.f);
    if (rain) b *= 1.f - 5.f / 16.f;
    return b;
}

struct Client {
    net::Conn conn;
    uint32_t id = 0;
    std::string name;
    bool logged = false;
    std::chrono::steady_clock::time_point connectedAt = std::chrono::steady_clock::now();
    PlayerNet st;
    Player proxy;           // «тень» игрока на сервере: по ней целятся мобы и считаются попадания
    int dim = 0;
    bool moved = false;
    uint32_t vehicle = 0;   // на каком транспорте сидит
    float forward = 0.f, strafe = 0.f;
    bool sleeping = false;
    int sleepTicks = 0;
    // Открытое окно контейнера
    int openKind = -1;      // -1 нет, 0 блок, 1 вагонетка с сундуком
    glm::ivec3 openPos{0};
    uint32_t openVehicle = 0;
    // Проверка связи (S_KEEPALIVE / C_KEEPALIVE): пинг для списка по TAB и отключение «пропавших»
    uint32_t kaToken = 0;
    bool kaWaiting = false;
    std::chrono::steady_clock::time_point kaSent;
    int ping = 0;
};

struct Dim {
    int id = 0;
    std::unique_ptr<World> world;
    MobManager mobs;
    std::vector<ItemEntity> items;
    std::vector<Particle> particles; // сервер частицы не рисует — копятся и чистятся
    World::SaveState st;
    std::string worldPath, entPath;
    Player idle;                      // «игрок» для тиков без людей (мобы его не видят)
};

std::string dimPrefix(int d) { return d == -1 ? "nether_" : d == 1 ? "end_" : ""; }

// Координаты из пакета: без NaN/бесконечностей и в пределах мира. Иначе приведение к int при поиске чанков —
// неопределённое поведение, а сущность с NaN-позицией живёт вечно
bool sanePos(const glm::vec3& p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
           std::abs(p.x) < 3.0e7f && std::abs(p.z) < 3.0e7f && std::abs(p.y) < 1.0e5f;
}
// Направление: конечное и ненулевое (glm::normalize от нуля даёт NaN)
bool saneDir(const glm::vec3& d, float maxLen = 1.0e3f) {
    if (!std::isfinite(d.x) || !std::isfinite(d.y) || !std::isfinite(d.z)) return false;
    float l = glm::length(d);
    return l > 1e-6f && l < maxLen;
}

} // namespace

int main() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCtrlHandler(onCtrl, TRUE);
    const std::string dir = exeDir();
    Props props = loadProps(dir + "server.properties");
    logf("Starting MiniCraft server (Minecraft 1.0 port), protocol " + std::to_string(PROTOCOL_VERSION));

    // ---- Мир и измерения
    SaveManager saves;
    saves.root = dir;
    WorldInfo info;
    if (!saves.readInfo(props.level, info)) {
        info = WorldInfo{};
        info.folder = info.name = props.level;
        info.seed = seedFromText(props.seed, (uint32_t)std::chrono::high_resolution_clock::now().time_since_epoch().count());
        info.gameMode = props.gamemode;
        info.generator = 4;
    }
    const std::string worldDir = saves.path(props.level);
    fs::create_directories(fs::u8path(worldDir + "players"));
    uint32_t seed = info.seed;
    uint32_t rng = seed | 1u;
    auto rnd = [&]() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng & 0xFFFFFF) / float(0x1000000); };

    std::vector<std::unique_ptr<Client>> clients;
    std::map<int, std::unique_ptr<Dim>> dims;
    int64_t worldTime = 0;
    bool raining = false, thundering = false;
    int rainTime = 0, thunderTime = 0;
    glm::vec3 spawnPoint(0.f);
    struct Change { int dim, x, y, z; uint8_t b, m; };
    std::vector<Change> changes;
    uint32_t nextEntity = 1;

    auto clientsIn = [&](int d) {
        std::vector<Client*> out;
        for (auto& c : clients) if (c->logged && c->dim == d) out.push_back(c.get());
        return out;
    };
    auto sendNear = [&](int d, const glm::vec3* pos, float radius, uint16_t type, const net::Writer& w, const Client* except = nullptr) {
        for (Client* c : clientsIn(d)) {
            if (c == except) continue;
            if (pos && glm::length(glm::vec3(c->st.x, c->st.y, c->st.z) - *pos) > radius) continue;
            c->conn.send(type, w);
        }
    };
    auto soundAt = [&](int d, const std::string& name, float vol, float pitch, const glm::vec3* pos) {
        net::Writer w;
        w.str(name);
        glm::vec3 p = pos ? *pos : glm::vec3(NAN);
        w.f32(p.x); w.f32(p.y); w.f32(p.z); w.f32(vol); w.f32(pitch);
        sendNear(d, pos, std::max(16.f, vol * 16.f), S_SOUND, w);
    };
    auto clientOf = [&](const Player& p) -> Client* {
        for (auto& c : clients) if (c->logged && c->id == p.netId) return c.get();
        return nullptr;
    };

    auto getDim = [&](int d) -> Dim& {
        auto it = dims.find(d);
        if (it != dims.end()) return *it->second;
        auto dm = std::make_unique<Dim>();
        dm->id = d;
        dm->world = std::make_unique<World>(seed, d);
        dm->world->setGenVersion(info.generator);
        dm->worldPath = worldDir + dimPrefix(d) + "world.sav";
        dm->entPath = worldDir + dimPrefix(d) + "entities.sav";
        bool loaded = dm->world->load(dm->worldPath, dm->st);
        if (d == 0) {
            seed = dm->world->seed();
            info.seed = seed;
            if (!loaded) {
                int sx = 0, sz = 0;
                for (int i = 0; i < 400; ++i) {
                    int x = (int)(hash32(seed + i * 2) % 2000) - 1000, z = (int)(hash32(seed + i * 2 + 1) % 2000) - 1000;
                    int h = dm->world->terrainHeight(x, z);
                    if (h > SEA + 2 && h < SEA + 20) { sx = x; sz = z; break; }
                }
                dm->st.spawn = glm::vec3(sx + 0.5f, dm->world->terrainHeight(sx, sz) + 1.f, sz + 0.5f);
            }
            spawnPoint = dm->st.spawn;
            worldTime = dm->st.worldTime;
            raining = dm->st.raining != 0;
            thundering = dm->st.thundering != 0;
            rainTime = dm->st.rainTime;
            thunderTime = dm->st.thunderTime;
        }
        dm->idle.dead = true; // мобы не видят «пустого» игрока
        dm->mobs.load(dm->entPath, dm->idle, &dm->items);
        dm->idle.dead = true;
        dm->mobs.difficulty = props.hardcore ? 3 : props.difficulty;
        Dim* raw = dm.get();
        dm->world->onChange = [&changes, raw](int x, int y, int z, uint8_t b, uint8_t m) { changes.push_back({raw->id, x, y, z, b, m}); };
        dm->world->renderDistance = 8;
        glm::vec3 center = d == 0 ? dm->st.spawn : d == 1 ? glm::vec3(100.f, 49.f, 0.f) : glm::vec3(0.f, 70.f, 0.f);
        dm->world->updateMulti({center}, d == 0 ? 4 : 2, 1000);
        // Список сгенерированных чанков не чистим: в первом же тике в них появятся животные
        if (d == 0) {
            // Точка появления — на суше (раньше могла оказаться в озере или в пещере)
            glm::vec3 safe = dm->world->safeSpawnNear(dm->st.spawn, 60, loaded);
            if (safe != dm->st.spawn) {
                char b[128];
                std::snprintf(b, sizeof b, "Spawn point moved to dry land: %.0f %.0f %.0f", safe.x, safe.y, safe.z);
                logf(b);
                dm->st.spawn = safe;
            }
            spawnPoint = dm->st.spawn;
        }
        if (d == 1 && !dm->world->dragonDefeated) {
            // Край: дракон и кристаллы на столбах, если их ещё нет
            bool haveDragon = false, haveCrystals = false;
            for (auto& m : dm->mobs.mobs) { haveDragon |= m.type == MobType::EnderDragon; haveCrystals |= m.type == MobType::EnderCrystal; }
            if (!haveDragon) dm->mobs.spawn(MobType::EnderDragon, glm::vec3(0.f, 110.f, 0.f), 0.f);
            if (!haveCrystals)
                for (const glm::ivec3& t : dm->world->endPillarTops()) dm->mobs.spawn(MobType::EnderCrystal, glm::vec3(t) + glm::vec3(0.5f, 1.f, 0.5f), 0.f);
        }
        logf(std::string("Dimension ") + (d == 0 ? "Overworld" : d == -1 ? "Nether" : "The End") + (loaded ? " loaded" : " created"));
        dims[d] = std::move(dm);
        return *dims[d];
    };

    auto saveAll = [&]() {
        for (auto& [d, dm] : dims) {
            World::SaveState s = dm->st;
            if (d == 0) {
                s.spawn = spawnPoint;
                s.worldTime = worldTime;
                s.raining = raining; s.thundering = thundering; s.rainTime = rainTime; s.thunderTime = thunderTime;
            }
            dm->world->save(dm->worldPath, s);
            dm->idle.dead = true;
            dm->mobs.save(dm->entPath, dm->idle, &dm->items);
        }
        info.lastPlayed = (int64_t)std::time(nullptr);
        saves.writeInfo(info);
    };

    getDim(0);
    saves.writeInfo(info);
    logf("World '" + props.level + "' seed " + std::to_string((int32_t)seed));

    // ---- Сеть
    net::Listener listener;
    std::string err;
    if (!listener.listen(props.port, err)) {
        logf("FAILED TO BIND TO PORT: " + err);
        std::this_thread::sleep_for(std::chrono::seconds(5));
        return 1;
    }
    logf("Listening on port " + std::to_string(props.port) + ".");
    const std::string portSuffix = props.port == DEFAULT_PORT ? "" : ":" + std::to_string(props.port);
    std::vector<std::string> ownIps{"127.0.0.1"};
    logf("You (on this PC) connect to:  localhost" + portSuffix);
    logf("Your friend (same Wi-Fi / network) connects to:");
    for (auto& a : net::localAddressList()) {
        ownIps.push_back(a.ip);
        std::string kind = a.virtualAdapter ? "virtual adapter - will NOT work for a friend" : a.wireless ? "Wi-Fi" : "network cable";
        logf("    " + a.ip + portSuffix + "   (" + a.adapter + ", " + kind + ")");
    }
    logf("    <your public IP>" + (props.port == DEFAULT_PORT ? std::string(":25565") : portSuffix) +
         "   (over the Internet - forward this port on your router)");
    auto isOp = [&](const Client& c) {
        return std::find(ownIps.begin(), ownIps.end(), c.conn.peer) != ownIps.end() ||
               std::find(props.ops.begin(), props.ops.end(), c.name) != props.ops.end();
    };
    logf("If your friend gets 'Connection timed out': allow MiniCraftServer in Windows Firewall");
    logf("    (run allow-firewall.bat from this folder once, as administrator).");
    logf("Type 'help' for console commands.");

    std::thread console([] {
        std::string line;
        while (!g_stop && std::getline(std::cin, line)) {
            std::lock_guard<std::mutex> lk(g_consoleMx);
            g_consoleLines.push_back(line);
        }
    });
    console.detach();

    uint32_t nextId = 1;
    auto broadcast = [&](uint16_t type, const net::Writer& w, const Client* except = nullptr) {
        for (auto& c : clients)
            if (c->logged && c.get() != except) c->conn.send(type, w);
    };
    auto chatAll = [&](const std::string& msg) {
        net::Writer w;
        w.str(msg);
        broadcast(S_CHAT, w);
        logf("[CHAT] " + msg);
    };
    auto sendTime = [&](Client* only) {
        net::Writer w;
        w.i64(worldTime); w.u8(raining); w.u8(thundering);
        if (only) only->conn.send(S_TIME, w);
        else broadcast(S_TIME, w);
    };
    auto playerFile = [&](const std::string& name) {
        std::string f;
        for (unsigned char ch : name) f += (std::isalnum(ch) || ch == '_' || ch == '-' || ch >= 0x80) ? (char)ch : '_';
        std::string up;
        for (char ch : f) up += (char)std::toupper((unsigned char)ch);
        static const char* RESERVED[] = {"CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
                                         "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
        for (auto* res : RESERVED)
            if (up == res) f += "_";
        return worldDir + "players/" + f + ".dat";
    };

    // Окно контейнера у игрока: содержимое и прогресс печи
    auto containerOf = [&](Client& c) -> TileEntity* {
        Dim& dm = getDim(c.dim);
        if (c.openKind == 0) return dm.world->tileAt(c.openPos.x, c.openPos.y, c.openPos.z);
        if (c.openKind == 1)
            for (auto& v : dm.mobs.vehicles)
                if (v.id == c.openVehicle && v.kind == VehicleKind::ChestCart) { v.chest.type = TileEntity::Chest; return &v.chest; }
        return nullptr;
    };
    // Двойной сундук: половины в порядке окна (сверху — с меньшей координатой)
    auto chestHalves = [&](Client& c, TileEntity*& a, TileEntity*& b) -> bool {
        if (c.openKind != 0) return false;
        World& w0 = *getDim(c.dim).world;
        glm::ivec3 pa, pb;
        if (w0.getBlock(c.openPos.x, c.openPos.y, c.openPos.z) != CHEST || !w0.chestPair(c.openPos.x, c.openPos.y, c.openPos.z, pa, pb))
            return false;
        a = w0.tileAt(pa.x, pa.y, pa.z);
        if (!a) a = &w0.createTile(pa.x, pa.y, pa.z, TileEntity::Chest);
        b = w0.tileAt(pb.x, pb.y, pb.z);
        if (!b) b = &w0.createTile(pb.x, pb.y, pb.z, TileEntity::Chest);
        return true;
    };
    // Сделки жителя клиенту (S_TRADE): открыть окно или обновить после сделки
    auto sendTrade = [&](Client& c, const Mob& m) {
        net::Writer w;
        w.u32(m.id);
        size_t n = std::min<size_t>(m.offers.size(), 255);
        w.u8((uint8_t)n);
        for (size_t i = 0; i < n; ++i) {
            writeItem(w, m.offers[i].buy1);
            writeItem(w, m.offers[i].buy2);
            writeItem(w, m.offers[i].sell);
            w.u8(m.offers[i].disabled() ? 1 : 0);
        }
        c.conn.send(S_TRADE, w);
    };
    auto sendContainer = [&](Client& c) {
        TileEntity* te = containerOf(c);
        if (!te) return;
        TileEntity *ha = nullptr, *hb = nullptr;
        bool dbl = chestHalves(c, ha, hb);
        net::Writer w;
        w.u8((uint8_t)c.openKind);
        if (c.openKind == 0) { w.i32(c.openPos.x); w.u8((uint8_t)c.openPos.y); w.i32(c.openPos.z); }
        else w.u32(c.openVehicle);
        w.u8((uint8_t)te->type);
        if (dbl) {
            w.u8(54);
            for (int i = 0; i < 27; ++i) writeItem(w, ha->items[i]);
            for (int i = 0; i < 27; ++i) writeItem(w, hb->items[i]);
        } else {
            w.u8((uint8_t)te->size());
            for (int i = 0; i < te->size(); ++i) writeItem(w, te->items[i]);
        }
        w.u16((uint16_t)std::max(0, te->burnTime)); w.u16((uint16_t)std::max(0, te->burnMax)); w.u16((uint16_t)std::max(0, te->cookTime));
        c.conn.send(S_CONTAINER, w);
    };

    // Команды (из консоли или от игрока с «/»); ответ — в reply
    auto command = [&](const std::string& line, const std::function<void(const std::string&)>& reply) {
        std::vector<std::string> a;
        std::string cur;
        for (char ch : line) { if (ch == ' ') { if (!cur.empty()) a.push_back(cur); cur.clear(); } else cur += ch; }
        if (!cur.empty()) a.push_back(cur);
        if (a.empty()) return;
        std::string c = a[0];
        if (!c.empty() && c[0] == '/') c = c.substr(1);
        if (c == "help") reply("Server commands: stop, list, say <msg>, time set|add <n|day|night>, weather <clear|rain|thunder>, difficulty <0-3>, save, kick <name>");
        else if (c == "stop") g_stop = true;
        else if (c == "save") { saveAll(); reply("World saved"); }
        else if (c == "list") {
            std::string s;
            int n = 0;
            for (auto& cl : clients) if (cl->logged) { s += (n++ ? ", " : "") + cl->name; }
            reply("Players online (" + std::to_string(n) + "): " + s);
        } else if (c == "say" && a.size() > 1) {
            chatAll("[Server] " + line.substr(line.find(' ') + 1));
        } else if (c == "kick" && a.size() > 1) {
            for (auto& cl : clients)
                if (cl->logged && cl->name == a[1]) { net::Writer w; w.str("Kicked by an operator"); cl->conn.send(S_KICK, w); cl->conn.flush(); cl->conn.close(); }
        } else if (c == "time" && a.size() >= 3) {
            int t = a[2] == "day" ? 1000 : a[2] == "night" ? 13000 : std::atoi(a[2].c_str());
            if (a[1] == "set") worldTime = worldTime - worldTime % 24000 + t;
            else worldTime += t;
            sendTime(nullptr);
            reply("Time " + a[1] + " " + std::to_string(t));
        } else if ((c == "weather" && a.size() >= 2) || c == "toggledownfall") {
            std::string v = c == "toggledownfall" ? (raining ? "clear" : "rain") : a[1];
            raining = v != "clear";
            thundering = v == "thunder";
            rainTime = thunderTime = 12000;
            sendTime(nullptr);
            reply("Weather: " + v);
        } else if (c == "difficulty" && a.size() >= 2) {
            props.difficulty = std::clamp(std::atoi(a[1].c_str()), 0, 3);
            for (auto& [d, dm] : dims) dm->mobs.difficulty = props.difficulty;
            reply("Difficulty set to " + a[1]);
        } else reply("Unknown server command: " + c);
    };

    // ---- Хуки мобов: звуки, опыт, достижения, подбор — конкретному игроку
    auto makeHooks = [&](Dim& dm) {
        MobHooks h;
        int d = dm.id;
        h.sound = [&, d](const std::string& n, float v, float p, const glm::vec3* pos) { soundAt(d, n, v, p, pos); };
        h.addXp = [](int) {};
        h.giveItem = [](ItemStack&) { return false; };
        h.addXpTo = [&](Player& p, int xp) { if (Client* c = clientOf(p)) { net::Writer w; w.u16((uint16_t)xp); c->conn.send(S_XP, w); } };
        h.onKillBy = [&](Player& p, MobType t) { if (Client* c = clientOf(p)) { net::Writer w; w.u8((uint8_t)t); c->conn.send(S_KILL, w); } };
        h.achievementFor = [&](Player& p, int bit) { if (Client* c = clientOf(p)) { net::Writer w; w.u8((uint8_t)bit); c->conn.send(S_ACHIEVE, w); } };
        h.giveItemTo = [&](Player& p, ItemStack& s) {
            Client* c = clientOf(p);
            if (!c) return false;
            net::Writer w;
            writeItem(w, s);
            c->conn.send(S_GIVE, w);
            s.clear();
            return true;
        };
        h.teleport = [&](Player& p, const glm::vec3& to) { if (Client* c = clientOf(p)) { net::Writer w; w.f32(to.x); w.f32(to.y); w.f32(to.z); c->conn.send(S_TELEPORT, w); } };
        h.potionOn = [&](Player& p, int e, int amp, int dur) {
            if (Client* c = clientOf(p)) { net::Writer w; w.u8((uint8_t)e); w.u8((uint8_t)amp); w.u16((uint16_t)std::max(0, dur)); c->conn.send(S_POTION, w); }
        };
        h.explosionFx = [&, d](const glm::vec3& pos, float power) {
            net::Writer w; w.f32(pos.x); w.f32(pos.y); w.f32(pos.z); w.f32(power);
            sendNear(d, &pos, 96.f, S_EXPLODE, w);
        };
        return h;
    };

    // ---- Снимок сущностей вокруг игрока
    auto writeEntities = [&](Client& c, net::Writer& w) {
        Dim& dm = getDim(c.dim);
        glm::vec3 me(c.st.x, c.st.y, c.st.z);
        const float R = 96.f;
        auto inRange = [&](const glm::vec3& p) { return glm::length(p - me) < R; };
        // Мобы
        std::vector<const Mob*> ms;
        for (auto& m : dm.mobs.mobs) if (!m.removed && (inRange(m.pos) || m.type == MobType::EnderDragon)) ms.push_back(&m);
        w.u16((uint16_t)ms.size());
        for (const Mob* mp : ms) {
            const Mob& m = *mp;
            w.u32(m.id); w.u8((uint8_t)m.type);
            w.f32(m.pos.x); w.f32(m.pos.y); w.f32(m.pos.z); w.f32(m.yaw);
            uint16_t f = (m.dying() ? 1 : 0) | (m.angry ? 2 : 0) | (m.sitting ? 4 : 0) | (m.saddled ? 8 : 0) | (m.sheared ? 16 : 0) |
                         (m.tamed ? 32 : 0) | (m.aiming ? 64 : 0) | (m.fireTicks > 0 ? 128 : 0) | (m.ridden ? 256 : 0) | (m.growingAge < 0 ? 512 : 0) | (m.charged ? 1024 : 0);
            w.u16(f);
            w.u8((uint8_t)std::clamp(m.hurtTime, 0, 255)); w.u8((uint8_t)std::clamp(m.deathTime, 0, 255));
            w.u16((uint16_t)std::max(0, m.health));
            w.u8((uint8_t)m.color); w.u8((uint8_t)m.size); w.u8(m.heldBlock); w.u8(m.heldMeta);
            w.u8((uint8_t)std::clamp(m.fuse, 0, 255)); w.u8((uint8_t)(m.swingTicks + 1));
            w.u8((uint8_t)std::clamp(m.attackCooldown, 0, 255)); w.u8((uint8_t)(int8_t)std::clamp(m.attackCounter, -128, 127));
            w.f32(m.wingFlap); w.f32(m.squish); w.f32(m.tentacle); w.f32(m.squidPitch); w.f32(m.squidRoll);
            w.u32(m.targetId);
        }
        // Выпавшие предметы
        uint16_t n = 0;
        for (auto& e : dm.items) n += !e.dead && inRange(e.pos);
        w.u16(n);
        for (auto& e : dm.items) {
            if (e.dead || !inRange(e.pos)) continue;
            w.u32(e.id); w.f32(e.pos.x); w.f32(e.pos.y); w.f32(e.pos.z); w.u16((uint16_t)e.age); w.f32(e.bobOffset);
            writeItem(w, e.stack);
        }
        // Стрелы
        n = 0;
        for (auto& a : dm.mobs.arrows) n += !a.dead && inRange(a.pos);
        w.u16(n);
        for (auto& a : dm.mobs.arrows) {
            if (a.dead || !inRange(a.pos)) continue;
            w.u32(a.id); w.f32(a.pos.x); w.f32(a.pos.y); w.f32(a.pos.z); w.f32(a.dir.x); w.f32(a.dir.y); w.f32(a.dir.z);
        }
        // Огненные шары
        n = 0;
        for (auto& f : dm.mobs.fireballs) n += !f.dead && inRange(f.pos);
        w.u16(n);
        for (auto& f : dm.mobs.fireballs) {
            if (f.dead || !inRange(f.pos)) continue;
            w.u32(f.id); w.f32(f.pos.x); w.f32(f.pos.y); w.f32(f.pos.z); w.u8(f.small);
        }
        // Брошенные предметы
        n = 0;
        for (auto& t : dm.mobs.throwables) n += !t.dead && inRange(t.pos);
        w.u16(n);
        for (auto& t : dm.mobs.throwables) {
            if (t.dead || !inRange(t.pos)) continue;
            w.u32(t.id); w.f32(t.pos.x); w.f32(t.pos.y); w.f32(t.pos.z); w.u16(t.item); w.u16(t.damage);
        }
        // Динамит
        n = 0;
        for (auto& t : dm.mobs.tnts) n += !t.dead && inRange(t.pos);
        w.u16(n);
        for (auto& t : dm.mobs.tnts) {
            if (t.dead || !inRange(t.pos)) continue;
            w.u32(t.id); w.f32(t.pos.x); w.f32(t.pos.y); w.f32(t.pos.z); w.u16((uint16_t)std::max(0, t.fuse));
        }
        // Транспорт
        n = 0;
        for (auto& v : dm.mobs.vehicles) n += !v.dead && inRange(v.pos);
        w.u16(n);
        for (auto& v : dm.mobs.vehicles) {
            if (v.dead || !inRange(v.pos)) continue;
            w.u32(v.id); w.u8((uint8_t)v.kind); w.f32(v.pos.x); w.f32(v.pos.y); w.f32(v.pos.z); w.f32(v.yaw);
            w.u8((uint8_t)std::clamp(v.hurtTime, 0, 255)); w.u8(v.fuel > 0);
        }
        // Картины
        n = 0;
        for (auto& pt : dm.mobs.paintings) n += !pt.dead && inRange(pt.center());
        w.u16(n);
        for (auto& pt : dm.mobs.paintings) {
            if (pt.dead || !inRange(pt.center())) continue;
            w.u32(pt.id); w.i32(pt.wall.x); w.u8((uint8_t)pt.wall.y); w.i32(pt.wall.z); w.u8((uint8_t)pt.dir); w.u8((uint8_t)pt.art);
            w.u8(pt.frame ? 1 : 0);
            if (pt.frame) { w.u8((uint8_t)pt.rotation); writeItem(w, pt.item); }
        }
        // Падающий песок и гравий
        n = 0;
        for (auto& f : dm.mobs.falling) n += !f.dead && inRange(f.pos);
        w.u16(n);
        for (auto& f : dm.mobs.falling) {
            if (f.dead || !inRange(f.pos)) continue;
            w.u32(f.id); w.f32(f.pos.x); w.f32(f.pos.y); w.f32(f.pos.z); w.u8(f.block);
        }
        // Шары опыта
        n = 0;
        for (auto& o : dm.mobs.orbs) n += !o.dead && inRange(o.pos);
        w.u16(n);
        for (auto& o : dm.mobs.orbs) {
            if (o.dead || !inRange(o.pos)) continue;
            w.u32(o.id); w.f32(o.pos.x); w.f32(o.pos.y); w.f32(o.pos.z); w.u16((uint16_t)std::min(o.value, 65535));
        }
    };

    logf("Done! Server is running.");
    auto nextTick = std::chrono::steady_clock::now();
    int64_t ticks = 0;
    while (!g_stop) {
        // ---- Новые подключения
        for (;;) {
            auto cl = std::make_unique<Client>();
            if (!listener.accept(cl->conn)) break;
            logf("Connection from " + cl->conn.peer);
            clients.push_back(std::move(cl));
        }
        // ---- Команды консоли
        {
            std::vector<std::string> lines;
            { std::lock_guard<std::mutex> lk(g_consoleMx); lines.swap(g_consoleLines); }
            for (auto& l : lines) command(l, [](const std::string& s) { logf(s); });
        }

        // ---- Пакеты игроков
        for (auto& cp : clients) {
            Client& c = *cp;
            c.conn.poll();
            uint16_t type;
            std::vector<uint8_t> data;
            while (c.conn.alive && c.conn.next(type, data)) {
                net::Reader r(data);
                if (!c.logged && type != C_LOGIN) continue;
                Dim* dmp = c.logged ? &getDim(c.dim) : nullptr;
                switch (type) {
                case C_LOGIN: {
                    uint16_t ver = r.u16();
                    std::string name;
                    for (char ch : validUtf8(r.str()))
                        if ((unsigned char)ch >= 32 && ch != 127) name += ch; // без управляющих символов и битого UTF-8
                    while (!name.empty() && name.back() == ' ') name.pop_back();
                    if (name.empty() || name.size() > 32) name = "Player";
                    int online = 0;
                    for (auto& o : clients) online += o->logged;
                    auto kick = [&](const std::string& why) {
                        net::Writer w;
                        w.str(why);
                        c.conn.send(S_KICK, w);
                        c.conn.flush();
                        c.conn.close();
                        logf(c.conn.peer + " kicked: " + why);
                    };
                    if (ver != PROTOCOL_VERSION) { kick("Outdated " + std::string(ver < PROTOCOL_VERSION ? "client" : "server") + "!"); break; }
                    if (online >= props.maxPlayers) { kick("The server is full!"); break; }
                    for (auto& o : clients)
                        if (o->logged && o->name == name) { kick("A player named " + name + " is already online"); break; }
                    if (!c.conn.alive) break;
                    c.id = nextId++;
                    c.name = name;
                    c.logged = true;
                    c.dim = 0;
                    c.st.x = spawnPoint.x; c.st.y = spawnPoint.y; c.st.z = spawnPoint.z;
                    c.proxy.netId = c.id;
                    c.proxy.pos = c.proxy.prevPos = spawnPoint;
                    net::Writer w;
                    w.u32(c.id); w.u32(seed); w.u8((uint8_t)info.generator); w.i64(worldTime);
                    w.u8(raining); w.u8(thundering);
                    w.f32(spawnPoint.x); w.f32(spawnPoint.y); w.f32(spawnPoint.z);
                    w.u8((uint8_t)props.gamemode); w.u8((uint8_t)props.difficulty);
                    w.str(props.motd);
                    w.u8(props.pvp);
                    w.u8(isOp(c) ? 1 : 0);
                    w.u8(props.hardcore ? 1 : 0);
                    c.conn.send(S_LOGIN_OK, w);
                    // Сохранённое состояние игрока (инвентарь, место, здоровье)
                    {
                        std::ifstream pf(fs::u8path(playerFile(name)), std::ios::binary);
                        if (pf) {
                            std::vector<uint8_t> blob((std::istreambuf_iterator<char>(pf)), std::istreambuf_iterator<char>());
                            net::Writer b;
                            b.raw(blob.data(), blob.size());
                            c.conn.send(S_PLAYERDATA, b);
                        }
                    }
                    for (auto& o : clients) {
                        if (!o->logged || o.get() == &c) continue;
                        net::Writer a; a.u32(o->id); a.str(o->name);
                        c.conn.send(S_PLAYER_ADD, a);
                        net::Writer p; p.u32(o->id); writePlayerNet(p, o->st);
                        c.conn.send(S_PLAYER, p);
                    }
                    net::Writer a; a.u32(c.id); a.str(c.name);
                    broadcast(S_PLAYER_ADD, a, &c);
                    chatAll(c.name + " joined the game");
                    break;
                }
                case C_CHUNK_REQ: {
                    int cx = r.i32(), cz = r.i32();
                    World& w0 = *dmp->world;
                    auto e = w0.chunkEdits(cx, cz);
                    net::Writer w;
                    w.i32(cx); w.i32(cz); w.u32((uint32_t)e.size());
                    for (auto& [i, v] : e) { w.u16(i); w.u16(v); }
                    std::vector<std::pair<int64_t, const std::array<std::string, 4>*>> signs;
                    for (auto& [k, lines] : w0.signs) {
                        glm::ivec3 p = posFromKey(k);
                        if (floorDiv(p.x, CW) == cx && floorDiv(p.z, CW) == cz) signs.push_back({k, &lines});
                    }
                    w.u16((uint16_t)signs.size());
                    for (auto& [k, lines] : signs) {
                        glm::ivec3 p = posFromKey(k);
                        w.i32(p.x); w.u8((uint8_t)p.y); w.i32(p.z);
                        for (auto& l : *lines) w.str(l);
                    }
                    c.conn.send(S_CHUNK, w);
                    break;
                }
                case C_SET_BLOCK: {
                    int x = r.i32(), y = r.u8(), z = r.i32();
                    uint8_t b = r.u8(), m = r.u8(), onlyMeta = r.u8();
                    World& w0 = *dmp->world;
                    if (!r.ok || !w0.isChunkLoaded(floorDiv(x, CW), floorDiv(z, CW))) break;
                    if (onlyMeta) {
                        w0.setMeta(x, y, z, m);
                        w0.redstoneChanged(x, y, z);
                    } else {
                        uint8_t old = w0.getBlock(x, y, z);
                        w0.setBlock(x, y, z, b, m);
                        if (isLiquid(b)) w0.scheduleUpdate(x, y, z, b == LAVA ? 30 : 5); // налитая жидкость начинает течь
                        if (b == FURNACE || b == CHEST || b == DISPENSER || b == BREWING_STAND) {
                            if (!w0.tileAt(x, y, z))
                                w0.createTile(x, y, z, b == CHEST ? TileEntity::Chest : b == DISPENSER ? TileEntity::Dispenser
                                                     : b == BREWING_STAND ? TileEntity::Brewing : TileEntity::Furnace);
                        }
                        if (b == MOB_SPAWNER && old != MOB_SPAWNER) dmp->mobs.addSpawner({x, y, z}, m);
                    }
                    break;
                }
                case C_DIG: {
                    int x = r.i32(), y = r.u8(), z = r.i32();
                    ItemStack tool;
                    tool.id = r.u16(); tool.damage = r.u16();
                    for (auto& e : tool.ench) e = r.u16();
                    tool.count = tool.id ? 1 : 0;
                    bool creative = r.u8() != 0;
                    World& w0 = *dmp->world;
                    if (!w0.isChunkLoaded(floorDiv(x, CW), floorDiv(z, CW))) break;
                    uint8_t b = w0.getBlock(x, y, z), meta = w0.getMeta(x, y, z);
                    if (b == AIR) break;
                    { // звук и осколки — остальным
                        net::Writer fx; fx.i32(x); fx.u8((uint8_t)y); fx.i32(z); fx.u8(b); fx.u8(meta);
                        glm::vec3 pos(x + 0.5f, y + 0.5f, z + 0.5f);
                        sendNear(c.dim, &pos, 48.f, S_DIGFX, fx, &c);
                    }
                    harvestBlock(w0, dmp->mobs, dmp->items, {x, y, z}, tool, creative, c.dim, rng);
                    break;
                }
                case C_PLAYER: {
                    PlayerNet st = readPlayerNet(r);
                    uint32_t vehicle = r.u32();
                    float forward = r.f32(), strafe = r.f32();
                    if (!r.ok || !sanePos(glm::vec3(st.x, st.y, st.z)) || !std::isfinite(st.yaw) || !std::isfinite(st.pitch) ||
                        !std::isfinite(forward) || !std::isfinite(strafe))
                        break;
                    c.st = st;
                    c.vehicle = vehicle;
                    c.forward = std::clamp(forward, -1.f, 1.f);
                    c.strafe = std::clamp(strafe, -1.f, 1.f);
                    c.moved = true;
                    Player& px = c.proxy;
                    px.prevPos = px.pos;
                    px.pos = glm::vec3(c.st.x, c.st.y, c.st.z);
                    px.yaw = c.st.yaw; px.pitch = c.st.pitch;
                    px.onGround = c.st.flags & 1;
                    px.sneaking = c.st.flags & 2;
                    px.dead = c.st.flags & 8;
                    px.mode = (c.st.flags & 16) ? GameMode::Creative : GameMode::Survival;
                    c.sleeping = (c.st.flags & 128) != 0;
                    if (c.st.dim < -1 || c.st.dim > 1) c.st.dim = (int8_t)c.dim;
                    if (c.st.dim != c.dim) { c.dim = c.st.dim; getDim(c.dim); }
                    break;
                }
                case C_ATTACK: {
                    uint32_t mid = r.u32();
                    int dmg = r.u16();
                    float knock = r.f32();
                    int fire = r.u8(), looting = r.u8();
                    MobHooks hk = makeHooks(*dmp);
                    for (auto& m : dmp->mobs.mobs) {
                        if (m.id != mid || m.dying()) continue;
                        m.looting = looting;
                        dmp->mobs.currentAttacker = c.id;
                        dmp->mobs.hurt(m, dmg, c.proxy.pos, knock, true, hk);
                        dmp->mobs.currentAttacker = 0;
                        if (fire && !isFireImmune(m.type)) m.fireTicks = fire * 20;
                        break;
                    }
                    break;
                }
                case C_ATTACK_PLAYER: {
                    uint32_t tid = r.u32();
                    int dmg = r.u16();
                    float knock = r.f32();
                    if (!props.pvp) break;
                    for (auto& o : clients) {
                        if (!o->logged || o->id != tid || o->dim != c.dim || (o->st.flags & (8 | 16))) continue;
                        glm::vec2 d(o->st.x - c.st.x, o->st.z - c.st.z);
                        if (glm::length(d) > 6.f || o->proxy.invulnerable > 0) break; // после удара полсекунды неуязвим, как в 1.0
                        o->proxy.invulnerable = 10;
                        d = glm::length(d) > 1e-4f ? glm::normalize(d) : glm::vec2(1, 0);
                        net::Writer w;
                        w.u16((uint16_t)dmg); w.u8((uint8_t)(int8_t)5);
                        w.f32(d.x * 0.4f * knock); w.f32(0.4f); w.f32(d.y * 0.4f * knock);
                        w.u16(0); w.u16(0); w.u8(0);
                        o->conn.send(S_DAMAGE, w);
                        break;
                    }
                    break;
                }
                case C_INTERACT: {
                    uint32_t mid = r.u32();
                    ItemStack held = readItem(r);
                    net::Writer w;
                    int consume = 0;
                    ItemStack rep;
                    uint8_t ride = 0;
                    uint32_t rideId = 0;
                    MobHooks hk = makeHooks(*dmp);
                    for (auto& m : dmp->mobs.mobs) {
                        if (m.id != mid || m.dying()) continue;
                        if (m.type == MobType::Pig && m.saddled && held.id != SADDLE && !m.ridden) {
                            m.ridden = true;
                            m.riderId = c.id;
                            ride = 2;
                            rideId = m.id;
                        } else if (m.type == MobType::Villager && m.growingAge >= 0 && !m.tradingWith && !c.proxy.dead) {
                            // Торговля: житель занят этим игроком, клиенту — список сделок (он откроет окно)
                            if (m.offers.empty()) addVillagerOffers(m.offers, m.color % 5, rng, 1);
                            m.tradingWith = c.id;
                            sendTrade(c, m);
                        } else {
                            dmp->mobs.interact(m, held, c.proxy, dmp->items, dmp->particles, hk, rng, consume, rep);
                        }
                        break;
                    }
                    w.u8((uint8_t)consume);
                    writeItem(w, rep);
                    w.u8(ride);
                    w.u32(rideId);
                    c.conn.send(S_USE_RESULT, w);
                    break;
                }
                case C_SHOOT: {
                    glm::vec3 from = readVec3(r);
                    glm::vec3 dir = readVec3(r);
                    float speed = r.f32();
                    bool crit = r.u8() != 0;
                    float damage = r.f32();
                    int punch = r.u8();
                    bool flame = r.u8() != 0, pickup = r.u8() != 0;
                    if (!r.ok || !sanePos(from) || !saneDir(dir) || !std::isfinite(speed) || !std::isfinite(damage)) break;
                    speed = std::clamp(speed, 0.f, 10.f);
                    Arrow& a = dmp->mobs.shootArrow(from, dir, speed, 1.f, true, crit, rng);
                    a.damage = damage; a.punch = punch; a.flame = flame; a.pickup = pickup; a.owner = c.id;
                    glm::vec3 sp = from;
                    soundAt(c.dim, "random/bow", 1.f, 1.f / (rnd() * 0.4f + 1.2f) + speed / 6.f, &sp);
                    break;
                }
                case C_THROW: {
                    glm::vec3 from = readVec3(r);
                    glm::vec3 dir = readVec3(r);
                    uint16_t item = r.u16(), dmgv = r.u16();
                    if (!r.ok || !sanePos(from) || !saneDir(dir)) break;
                    size_t before = dmp->mobs.throwables.size();
                    // Око Края летит к ближайшей крепости (сервер знает мир)
                    dmp->mobs.throwItem(from, dir, item, rng, dmgv);
                    if (item == EYE_OF_ENDER && dmp->mobs.throwables.size() > before) {
                        Throwable& t = dmp->mobs.throwables.back();
                        t.seeking = true;
                        t.motion = glm::vec3(0, 0.1f, 0);
                        glm::ivec3 shPos;
                        bool hasSH = dmp->world->locateStronghold10(from, shPos);
                        if (!hasSH) {
                            float bd = 1e18f;
                            for (const glm::ivec3& sc : dmp->world->strongholdCenters()) {
                                float d = glm::length(glm::vec2(sc.x - from.x, sc.z - from.z));
                                if (d < bd) { bd = d; shPos = sc; }
                            }
                        }
                        float dx = (float)shPos.x - from.x, dz = (float)shPos.z - from.z;
                        float distXZ = std::hypot(dx, dz);
                        if (distXZ > 12.f) {
                            t.target.x = from.x + (dx / distXZ) * 12.f;
                            t.target.z = from.z + (dz / distXZ) * 12.f;
                            t.target.y = from.y + 8.f;
                        } else {
                            t.target = glm::vec3(shPos.x + 0.5f, (float)shPos.y, shPos.z + 0.5f);
                        }
                    }
                    for (size_t i = before; i < dmp->mobs.throwables.size(); ++i) dmp->mobs.throwables[i].owner = c.id;
                    glm::vec3 sp = from;
                    soundAt(c.dim, "random/bow", 0.5f, 0.4f / (rnd() * 0.4f + 0.8f), &sp);
                    break;
                }
                case C_DROP: {
                    glm::vec3 eye = readVec3(r);
                    glm::vec3 look = readVec3(r);
                    ItemStack s = readItem(r);
                    if (!r.ok || !sanePos(eye) || !std::isfinite(look.x) || !std::isfinite(look.y) || !std::isfinite(look.z) ||
                        glm::length(look) > 10.f)
                        break;
                    if (!s.empty()) throwFromPlayer(dmp->items, eye, look, s, rng);
                    break;
                }
                case C_TNT: {
                    int x = r.i32(), y = r.u8(), z = r.i32();
                    if (dmp->world->getBlock(x, y, z) != TNT) break;
                    dmp->world->setBlock(x, y, z, AIR);
                    dmp->mobs.igniteTnt({x, y, z}, false, rng);
                    glm::vec3 sp(x + 0.5f, y + 0.5f, z + 0.5f);
                    soundAt(c.dim, "random/fuse", 1.f, 1.f, &sp);
                    break;
                }
                case C_VEHICLE_PLACE: {
                    Vehicle v;
                    v.kind = (VehicleKind)std::min<uint8_t>(r.u8(), 3);
                    v.pos = v.prev = readVec3(r);
                    v.yaw = v.prevYaw = r.f32();
                    if (!r.ok || !sanePos(v.pos) || !std::isfinite(v.yaw)) break;
                    v.id = dmp->mobs.nextId++;
                    v.chest.type = TileEntity::Chest;
                    dmp->mobs.vehicles.push_back(v);
                    break;
                }
                case C_VEHICLE_HIT: {
                    uint32_t vid = r.u32();
                    bool creative = r.u8() != 0;
                    for (size_t i = 0; i < dmp->mobs.vehicles.size(); ++i) {
                        Vehicle& v = dmp->mobs.vehicles[i];
                        if (v.id != vid) continue;
                        v.damage += creative ? 100 : 10;
                        v.hurtTime = 10;
                        if (v.damage > 40) {
                            glm::ivec3 b((int)std::floor(v.pos.x), (int)std::floor(v.pos.y), (int)std::floor(v.pos.z));
                            if (!creative) {
                                if (v.kind == VehicleKind::Boat) {
                                    dropFromBlock(dmp->items, b, makeStack(BOAT), rng);
                                } else {
                                    dropFromBlock(dmp->items, b, makeStack(MINECART), rng);
                                    if (v.kind == VehicleKind::ChestCart) dropFromBlock(dmp->items, b, makeStack(CHEST), rng);
                                    if (v.kind == VehicleKind::FurnaceCart) dropFromBlock(dmp->items, b, makeStack(FURNACE), rng);
                                }
                            }
                            if (v.kind == VehicleKind::ChestCart)
                                for (auto& s : v.chest.items) dropFromBlock(dmp->items, b, s, rng);
                            dmp->mobs.vehicles.erase(dmp->mobs.vehicles.begin() + (long)i);
                        }
                        break;
                    }
                    break;
                }
                case C_VEHICLE_USE: {
                    uint32_t vid = r.u32();
                    ItemStack held = readItem(r);
                    net::Writer w;
                    uint8_t consume = 0, ride = 0;
                    for (auto& v : dmp->mobs.vehicles) {
                        if (v.id != vid) continue;
                        if (v.kind == VehicleKind::FurnaceCart && held.id == COAL) {
                            v.fuel += 3600;
                            glm::vec2 pd(v.pos.x - c.st.x, v.pos.z - c.st.z);
                            if (glm::length(pd) > 1e-3f) v.push = glm::normalize(pd);
                            consume = 1;
                        } else if (v.kind == VehicleKind::Minecart || v.kind == VehicleKind::Boat) {
                            bool taken = false;
                            for (auto& o : clients) taken |= o->logged && o.get() != &c && o->vehicle == vid;
                            if (!taken) ride = 1;
                        }
                        break;
                    }
                    w.u8(consume);
                    writeItem(w, ItemStack{});
                    w.u8(ride);
                    w.u32(vid);
                    c.conn.send(S_USE_RESULT, w);
                    break;
                }
                case C_DISMOUNT: {
                    for (auto& m : dmp->mobs.mobs)
                        if (m.riderId == c.id) { m.ridden = false; m.riderId = 0; }
                    c.vehicle = 0;
                    break;
                }
                case C_PAINTING: {
                    int op = r.u8();
                    if (op == 0) {
                        glm::ivec3 wp; wp.x = r.i32(); wp.y = r.u8(); wp.z = r.i32();
                        glm::ivec3 face((int8_t)r.u8(), 0, (int8_t)r.u8());
                        if (r.ok && std::abs(face.x) + std::abs(face.z) == 1 && glm::length(glm::vec3(wp) - glm::vec3(c.st.x, c.st.y, c.st.z)) < 8.f)
                            placePainting(dmp->mobs.paintings, *dmp->world, wp, face, rng);
                    } else if (op == 2) {
                        // Рамка для предмета (1.4.2)
                        glm::ivec3 wp; wp.x = r.i32(); wp.y = r.u8(); wp.z = r.i32();
                        glm::ivec3 face((int8_t)r.u8(), 0, (int8_t)r.u8());
                        if (r.ok && glm::length(glm::vec3(wp) - glm::vec3(c.st.x, c.st.y, c.st.z)) < 8.f)
                            placeItemFrame(dmp->mobs.paintings, *dmp->world, wp, face);
                    } else if (op == 3) {
                        // ПКМ по рамке: пустая — предмет из руки, с предметом — поворот
                        uint32_t pid = r.u32();
                        ItemStack held = readItem(r);
                        for (auto& pt : dmp->mobs.paintings)
                            if (r.ok && pt.id == pid && pt.frame && !pt.dead) {
                                if (pt.item.empty() && !held.empty() && isValidItem(held.id)) { pt.item = held; pt.item.count = 1; pt.rotation = 0; }
                                else if (!pt.item.empty()) pt.rotation = (pt.rotation + 1) & 3;
                            }
                    } else {
                        uint32_t pid = r.u32();
                        bool creative = (c.st.flags & 16) != 0;
                        for (auto& pt : dmp->mobs.paintings)
                            if (pt.id == pid && !pt.dead) {
                                glm::vec3 pc = pt.center();
                                glm::ivec3 cb((int)std::floor(pc.x), (int)std::floor(pc.y), (int)std::floor(pc.z));
                                if (pt.frame && !pt.item.empty()) { // сначала выпадает предмет из рамки
                                    if (!creative) dropFromBlock(dmp->items, cb, pt.item, rng);
                                    pt.item.clear();
                                    pt.rotation = 0;
                                    continue;
                                }
                                pt.dead = true;
                                if (!pt.frame || !creative) dropFromBlock(dmp->items, cb, makeStack(pt.frame ? ITEM_FRAME_ITEM : PAINTING), rng);
                            }
                        dmp->mobs.paintings.erase(std::remove_if(dmp->mobs.paintings.begin(), dmp->mobs.paintings.end(),
                                                                 [](const Painting& q) { return q.dead; }), dmp->mobs.paintings.end());
                    }
                    break;
                }
                case C_OPEN: {
                    c.openKind = r.u8();
                    if (c.openKind == 0) {
                        c.openPos.x = r.i32(); c.openPos.y = r.u8(); c.openPos.z = r.i32();
                        World& w0 = *dmp->world;
                        uint8_t b = w0.getBlock(c.openPos.x, c.openPos.y, c.openPos.z);
                        if (!w0.tileAt(c.openPos.x, c.openPos.y, c.openPos.z) &&
                            (b == CHEST || b == FURNACE || b == FURNACE_LIT || b == DISPENSER || b == BREWING_STAND))
                            w0.createTile(c.openPos.x, c.openPos.y, c.openPos.z,
                                          b == CHEST ? TileEntity::Chest : b == DISPENSER ? TileEntity::Dispenser
                                          : b == BREWING_STAND ? TileEntity::Brewing : TileEntity::Furnace);
                        if (b == CHEST) { glm::vec3 sp = glm::vec3(c.openPos) + 0.5f; soundAt(c.dim, "random/chestopen", 0.5f, rnd() * 0.1f + 0.9f, &sp); }
                    } else {
                        c.openVehicle = r.u32();
                    }
                    sendContainer(c);
                    break;
                }
                case C_CONTAINER: {
                    int kind = r.u8();
                    glm::ivec3 pos(0);
                    uint32_t vid = 0;
                    if (kind == 0) { pos.x = r.i32(); pos.y = r.u8(); pos.z = r.i32(); }
                    else vid = r.u32();
                    int n = r.u8();
                    std::vector<ItemStack> its(n);
                    for (auto& s : its) s = readItem(r);
                    if (!r.ok || kind != c.openKind || (kind == 0 && pos != c.openPos) || (kind == 1 && vid != c.openVehicle)) break;
                    TileEntity* te = containerOf(c);
                    if (!te) break;
                    TileEntity *ha = nullptr, *hb = nullptr;
                    if (chestHalves(c, ha, hb)) {
                        if (n != 54) break; // клиент видит одиночный сундук — не портим пару
                        for (int i = 0; i < 27; ++i) { ha->items[i] = its[i]; hb->items[i] = its[27 + i]; }
                    } else {
                        for (int i = 0; i < std::min(n, te->size()); ++i) te->items[i] = its[i];
                    }
                    // Остальным, у кого открыт этот же контейнер (или другая половина того же двойного сундука)
                    for (auto& o : clients) {
                        if (o.get() == &c || !o->logged || o->dim != c.dim || o->openKind != c.openKind) continue;
                        bool same = kind == 0 ? o->openPos == c.openPos : o->openVehicle == c.openVehicle;
                        if (!same && kind == 0 && ha) {
                            TileEntity* ot = dmp->world->tileAt(o->openPos.x, o->openPos.y, o->openPos.z);
                            same = ot == ha || ot == hb;
                        }
                        if (same) sendContainer(*o);
                    }
                    break;
                }
                case C_TRADE: {
                    uint32_t mid = r.u32();
                    int idx = r.u16();
                    if (!r.ok) break;
                    for (auto& m : dmp->mobs.mobs) {
                        if (m.id != mid || m.type != MobType::Villager || m.tradingWith != c.id) continue;
                        useTradeRecipe(m.offers, m.trade, idx);
                        sendTrade(c, m);
                        break;
                    }
                    break;
                }
                case C_CLOSE: {
                    for (auto& m : dmp->mobs.mobs)
                        if (m.tradingWith == c.id) m.tradingWith = 0;
                    if (c.openKind == 0 && dmp->world->getBlock(c.openPos.x, c.openPos.y, c.openPos.z) == CHEST) {
                        glm::vec3 sp = glm::vec3(c.openPos) + 0.5f;
                        soundAt(c.dim, "random/chestclosed", 0.5f, rnd() * 0.1f + 0.9f, &sp);
                    }
                    c.openKind = -1;
                    break;
                }
                case C_SIGN: {
                    int x = r.i32(), y = r.u8(), z = r.i32();
                    std::array<std::string, 4> lines;
                    for (auto& l : lines) { l = r.str(); if (l.size() > 15) l.resize(15); }
                    dmp->world->signs[posKey(x, y, z)] = lines;
                    net::Writer w;
                    w.i32(x); w.u8((uint8_t)y); w.i32(z);
                    for (auto& l : lines) w.str(l);
                    for (Client* o : clientsIn(c.dim)) if (o != &c) o->conn.send(S_SIGN, w);
                    break;
                }
                case C_DIM: {
                    int nd = (int8_t)r.u8();
                    if (nd < -1 || nd > 1) break;
                    c.dim = nd;
                    c.st.dim = (int8_t)nd;
                    c.vehicle = 0;
                    c.openKind = -1;
                    getDim(nd);
                    break;
                }
                case C_SAVE: {
                    std::ofstream pf(fs::u8path(playerFile(c.name)), std::ios::binary);
                    pf.write((const char*)data.data(), (std::streamsize)data.size());
                    break;
                }
                case C_SLEEP: c.sleeping = r.u8() != 0; break;
                case C_KEEPALIVE: {
                    uint32_t token = r.u32();
                    if (c.kaWaiting && token == c.kaToken) {
                        int ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - c.kaSent).count();
                        c.ping = c.ping == 0 ? ms : (c.ping * 3 + ms) / 4;
                        c.kaWaiting = false;
                    }
                    break;
                }
                case C_CHAT: {
                    std::string msg = r.str();
                    if (msg.size() > 100) msg.resize(100);
                    if (!msg.empty() && msg[0] == '/') {
                        if (!isOp(c)) {
                            net::Writer w;
                            w.str("You are not allowed to use server commands (the host can add you to 'ops' in server.properties)");
                            c.conn.send(S_CHAT, w);
                            logf(c.name + " tried a server command: " + msg);
                            break;
                        }
                        logf(c.name + " issued server command: " + msg);
                        Client* who = &c;
                        command(msg, [who](const std::string& s) { net::Writer w; w.str(s); who->conn.send(S_CHAT, w); });
                    } else if (!msg.empty()) {
                        chatAll("<" + c.name + "> " + msg);
                    }
                    break;
                }
                default: break;
                }
            }
        }
        // ---- Не вошедшие за 30 с и те, кто не забирает данные (очередь отправки растёт без предела), — отключаем
        for (auto& cp : clients) {
            Client& c = *cp;
            if (!c.conn.alive) continue;
            if (!c.logged && std::chrono::steady_clock::now() - c.connectedAt > std::chrono::seconds(30)) {
                logf(c.conn.peer + " did not log in, disconnected");
                c.conn.close();
            } else if (c.conn.pendingOut() > (64u << 20)) {
                logf((c.logged ? c.name : c.conn.peer) + " is not receiving data, disconnected");
                c.conn.close();
            }
        }
        // ---- Отключившиеся
        for (auto it = clients.begin(); it != clients.end();) {
            Client& c = **it;
            if (c.conn.alive) { ++it; continue; }
            if (c.logged) {
                for (auto& [d, dm] : dims)
                    for (auto& m : dm->mobs.mobs) {
                        if (m.riderId == c.id) { m.ridden = false; m.riderId = 0; }
                        if (m.tradingWith == c.id) m.tradingWith = 0;
                    }
                net::Writer w; w.u32(c.id);
                broadcast(S_PLAYER_DEL, w, &c);
                chatAll(c.name + " left the game");
            }
            it = clients.erase(it);
        }

        // ---- Время, погода, сон (все спят — утро)
        ++worldTime;
        if (thunderTime <= 0) thunderTime = thundering ? 3600 + (int)(rnd() * 12000) : 12000 + (int)(rnd() * 168000);
        else if (--thunderTime <= 0) { thundering = !thundering; sendTime(nullptr); }
        if (rainTime <= 0) rainTime = raining ? 12000 + (int)(rnd() * 12000) : 12000 + (int)(rnd() * 168000);
        else if (--rainTime <= 0) { raining = !raining; sendTime(nullptr); }
        {
            int online = 0, asleep = 0;
            for (auto& c : clients) if (c->logged) { ++online; if (c->sleeping) { ++asleep; ++c->sleepTicks; } else c->sleepTicks = 0; }
            bool allLong = online > 0 && asleep == online;
            for (auto& c : clients) if (c->logged && c->sleeping && c->sleepTicks < 100) allLong = false;
            if (allLong) {
                worldTime += 24000 - worldTime % 24000;
                raining = thundering = false;
                rainTime = 0;
                for (auto& c : clients) c->sleepTicks = 0;
                sendTime(nullptr);
            }
        }

        // ---- Тик измерений, где есть игроки (обычный мир — всегда)
        std::vector<int> active{0};
        for (auto& c : clients) if (c->logged && std::find(active.begin(), active.end(), c->dim) == active.end()) active.push_back(c->dim);
        for (int d : active) {
            Dim& dm = getDim(d);
            World& w0 = *dm.world;
            std::vector<Client*> here = clientsIn(d);
            std::vector<Player*> ps;
            std::vector<glm::vec3> centers;
            for (Client* c : here) {
                Player& px = c->proxy;
                px.health = 20;
                px.dead = (c->st.flags & 8) != 0;
                px.motion = glm::vec3(0.f);
                px.fireTicks = 0;
                px.poisonTicks = 0;
                px.damageSource = 0;
                if (px.invulnerable > 0) --px.invulnerable;
                ps.push_back(&px);
                centers.push_back(px.pos);
            }
            if (centers.empty()) centers.push_back(d == 0 ? spawnPoint : glm::vec3(0.f, 70.f, 0.f));
            w0.updateMulti(centers, 8, 6);
            w0.raining = d == 0 && raining;
            dm.mobs.raining = w0.raining;
            dm.mobs.netPlayers = ps;
            dm.mobs.riders.clear();
            for (Client* c : here)
                if (c->vehicle) dm.mobs.riders[c->vehicle] = {&c->proxy, c->forward, c->strafe};
            Player& p0 = ps.empty() ? dm.idle : *ps[0];
            float sky = d == 0 ? skyBrightness(worldTime, raining) : 0.f;
            int skySub = (int)((1.f - (sky - 0.05f) / 0.95f) * 11.f + 0.5f);
            MobHooks hooks = makeHooks(dm);
            TickEvents ev;

            w0.tickUpdates(worldTime, 400);
            for (auto& cpos : centers) w0.randomTick(cpos, rng, w0.raining);
            if (!ps.empty()) {
                dm.mobs.tick(w0, p0, ev, dm.items, dm.particles, hooks, sky, rng);
                if (props.spawnMonsters)
                    for (Player* q : ps) dm.mobs.spawnHostiles(w0, *q, skySub, 8, rng);
                dm.mobs.despawn(p0, w0, sky);
                dm.mobs.tickSpawners(w0, p0, dm.particles, rng);
            }
            if (props.spawnAnimals) {
                for (auto [gx, gz] : w0.generated) dm.mobs.populateChunk(w0, gx, gz, rng);
                for (Player* q : ps) dm.mobs.spawnPassive(w0, *q, worldTime % 400 == 0, worldTime % 20 == 0, rng);
            }
            // Молния в грозу — рядом со случайным игроком этого мира (как у одиночной игры: раз в ~15 с)
            if (d == 0 && raining && thundering && !ps.empty() && rnd() < 1.f / 300.f) {
                Player& q = *ps[(size_t)(rnd() * ps.size()) % ps.size()];
                float ang = rnd() * 6.2831853f, dist = 8.f + rnd() * 56.f;
                int lx = (int)std::floor(q.pos.x + std::cos(ang) * dist), lz = (int)std::floor(q.pos.z + std::sin(ang) * dist);
                if (w0.isChunkLoaded(floorDiv(lx, CW), floorDiv(lz, CW)) && !biomeInfo(w0.loadedBiome(lx, lz)).dry &&
                    !biomeInfo(w0.loadedBiome(lx, lz)).snowy) {
                    glm::vec3 at(lx + 0.5f, (float)w0.topBlockY(lx, lz), lz + 0.5f);
                    dm.mobs.strikeLightning(w0, at, p0, ev, hooks, rng);
                    net::Writer lw;
                    lw.f32(at.x); lw.f32(at.y); lw.f32(at.z);
                    sendNear(d, nullptr, 0.f, S_LIGHTNING, lw);
                }
            }
            w0.generated.clear();
            w0.villagerSpawns.clear();
            for (auto& [sp, sm] : w0.newSpawners) dm.mobs.addSpawner(sp, sm);
            w0.newSpawners.clear();
            dm.mobs.tickVehicles(w0, p0, -1, 0.f, 0.f, dm.particles, dm.items, rng);

            // Дракон убит: портал выхода с яйцом, опыт — всем в Краю
            if (dm.mobs.dragonKilled) {
                dm.mobs.dragonKilled = false;
                w0.dragonDefeated = true;
                for (Client* c : here) { net::Writer a; a.u8(23); c->conn.send(S_ACHIEVE, a); } // опыт дракон высыпал шарами
                int top = 64;
                for (int y = CH - 2; y > 1; --y)
                    if (w0.getBlock(0, y, 0) == END_STONE) { top = y; break; }
                int y0 = top + 1;
                for (int dx = -4; dx <= 4; ++dx)
                    for (int dz = -4; dz <= 4; ++dz) {
                        int d2 = dx * dx + dz * dz;
                        if (d2 > 16) continue;
                        w0.setBlock(dx, y0 - 1, dz, BEDROCK);
                        for (int dy = 0; dy <= 4; ++dy) w0.setBlock(dx, y0 + dy, dz, AIR);
                        if (d2 >= 9) w0.setBlock(dx, y0, dz, BEDROCK);
                        else if (d2 > 0) w0.setBlock(dx, y0, dz, END_PORTAL);
                    }
                for (int dy = 0; dy < 4; ++dy) w0.setBlock(0, y0 + dy, 0, BEDROCK);
                w0.setBlock(1, y0 + 2, 0, TORCH, TORCH_WEST_WALL);
                w0.setBlock(-1, y0 + 2, 0, TORCH, TORCH_EAST_WALL);
                w0.setBlock(0, y0 + 2, 1, TORCH, TORCH_NORTH_WALL);
                w0.setBlock(0, y0 + 2, -1, TORCH, TORCH_SOUTH_WALL);
                w0.setBlock(0, y0 + 4, 0, DRAGON_EGG);
            }

            // Выпавшие предметы: физика и подбор ближайшим игроком
            tickItems(dm.items, w0);
            for (auto& e : dm.items) {
                if (e.pickupDelay > 0 || e.dead) continue;
                for (Client* c : here) {
                    if (c->st.flags & 8) continue;
                    glm::vec3 dd = e.pos - c->proxy.pos;
                    if (std::abs(dd.x) < 1.3f && std::abs(dd.z) < 1.3f && dd.y > -0.6f && dd.y < 2.4f) {
                        net::Writer w;
                        writeItem(w, e.stack);
                        c->conn.send(S_GIVE, w);
                        e.dead = true;
                        glm::vec3 sp = e.pos;
                        soundAt(d, "random/pop", 0.2f, ((rnd() - rnd()) * 0.7f + 1.f) * 2.f, &sp);
                        break;
                    }
                }
            }
            dm.items.erase(std::remove_if(dm.items.begin(), dm.items.end(), [](const ItemEntity& e) { return e.dead; }), dm.items.end());

            tickTileEntities(w0);
            processDispense(w0, dm.mobs, dm.items, rng, [&, d](const std::string& n, float v, float pt, const glm::vec3& sp) { soundAt(d, n, v, pt, &sp); });
            for (auto& pp : w0.popped) {
                net::Writer fx; fx.i32(pp.pos.x); fx.u8((uint8_t)pp.pos.y); fx.i32(pp.pos.z); fx.u8(pp.block); fx.u8(pp.meta);
                glm::vec3 sp = glm::vec3(pp.pos) + 0.5f;
                sendNear(d, &sp, 48.f, S_DIGFX, fx);
            }
            dropPopped(w0, dm.items, false, rng);
            w0.popped.clear();
            for (auto& [tp, fromExplosion] : w0.ignitedTnt) {
                dm.mobs.igniteTnt(tp, fromExplosion, rng);
                glm::vec3 sp = glm::vec3(tp) + 0.5f;
                soundAt(d, "random/fuse", 1.f, 1.f, &sp);
            }
            w0.ignitedTnt.clear();
            for (auto& [sp, name] : w0.soundEvents) {
                std::string n = name;
                glm::vec3 p = sp;
                if (n == "random/click") soundAt(d, n, 0.3f, 0.5f, &p);
                else if (n == "random/click_fail") soundAt(d, "random/click", 1.f, 1.2f, &p);
                else if (n.rfind("tile/piston", 0) == 0) soundAt(d, n, 0.5f, rnd() * 0.25f + 0.6f, &p);
                else if (n.rfind("random/door", 0) == 0) soundAt(d, n, 1.f, rnd() * 0.1f + 0.9f, &p);
                else soundAt(d, n, 0.5f, 2.6f + (rnd() - rnd()) * 0.8f, &p);
            }
            w0.soundEvents.clear();
            for (auto& np : w0.noteEvents) {
                net::Writer w; w.i32(np.x); w.u8((uint8_t)np.y); w.i32(np.z);
                glm::vec3 sp = glm::vec3(np) + 0.5f;
                sendNear(d, &sp, 64.f, S_NOTE, w);
            }
            w0.noteEvents.clear();
            dm.particles.clear();

            // Урон по игрокам (от мобов, стрел, взрывов, зелий) — им самим
            for (Client* c : here) {
                Player& px = c->proxy;
                int dmg = 20 - px.health;
                bool pushed = glm::length(px.motion) > 1e-4f;
                if (dmg <= 0 && !pushed && px.fireTicks == 0 && px.poisonTicks == 0) continue;
                net::Writer w;
                w.u16((uint16_t)std::max(0, dmg)); w.u8((uint8_t)(int8_t)px.damageSource);
                w.f32(px.motion.x); w.f32(px.motion.y); w.f32(px.motion.z);
                w.u16((uint16_t)std::max(0, px.fireTicks)); w.u16((uint16_t)std::max(0, px.poisonTicks));
                w.u8(px.damageSource == -1 ? 1 : 0);
                c->conn.send(S_DAMAGE, w);
            }

            // Номера новым сущностям
            for (auto& e : dm.items) if (!e.id) e.id = nextEntity++;
            for (auto& o : dm.mobs.orbs) if (!o.id) o.id = nextEntity++;
            for (auto& f : dm.mobs.falling) if (!f.id) f.id = nextEntity++;
            for (auto& pt : dm.mobs.paintings) if (!pt.id) pt.id = nextEntity++;
            for (auto& a : dm.mobs.arrows) if (!a.id) a.id = nextEntity++;
            for (auto& f : dm.mobs.fireballs) if (!f.id) f.id = nextEntity++;
            for (auto& t : dm.mobs.throwables) if (!t.id) t.id = nextEntity++;
            for (auto& t : dm.mobs.tnts) if (!t.id) t.id = nextEntity++;
        }

        // ---- Рассылка: изменения блоков, сущности, игроки, время, окна контейнеров
        if (!changes.empty()) {
            for (auto& c : clients) {
                if (!c->logged) continue;
                std::vector<const Change*> mine;
                for (auto& ch : changes) if (ch.dim == c->dim) mine.push_back(&ch);
                for (size_t i0 = 0; i0 < mine.size(); i0 += 4096) {
                    size_t n = std::min<size_t>(4096, mine.size() - i0);
                    net::Writer w;
                    w.u32((uint32_t)n);
                    for (size_t i = i0; i < i0 + n; ++i) {
                        w.i32(mine[i]->x); w.u8((uint8_t)mine[i]->y); w.i32(mine[i]->z); w.u8(mine[i]->b); w.u8(mine[i]->m);
                    }
                    c->conn.send(S_BLOCKS, w);
                }
            }
            changes.clear();
        }
        for (auto& c : clients) {
            if (!c->logged) continue;
            net::Writer w;
            writeEntities(*c, w);
            c->conn.send(S_ENTITIES, w);
            if (c->openKind >= 0 && ticks % 5 == 0) {
                TileEntity* te = containerOf(*c);
                if (te && (te->type == TileEntity::Furnace || te->type == TileEntity::Brewing)) sendContainer(*c);
            }
        }
        for (auto& cp : clients) {
            if (!cp->logged || !cp->moved) continue;
            net::Writer w;
            w.u32(cp->id);
            writePlayerNet(w, cp->st);
            broadcast(S_PLAYER, w, cp.get());
            cp->moved = false;
        }
        if (++ticks % 20 == 0) {
            sendTime(nullptr);
            static uint32_t kaCounter = 1;
            auto nowT = std::chrono::steady_clock::now();
            for (auto& cp : clients) {
                Client& c = *cp;
                if (!c.logged) continue;
                if (c.kaWaiting) {
                    if (nowT - c.kaSent > std::chrono::seconds(30)) {
                        net::Writer w;
                        w.str("Timed out");
                        c.conn.send(S_KICK, w);
                        c.conn.flush();
                        c.conn.close();
                        logf(c.name + " timed out");
                    }
                    continue;
                }
                c.kaToken = kaCounter++;
                c.kaSent = nowT;
                c.kaWaiting = true;
                net::Writer w;
                w.u32(c.kaToken);
                c.conn.send(S_KEEPALIVE, w);
            }
            net::Writer pl;
            uint16_t np = 0;
            for (auto& cp : clients) np += cp->logged;
            pl.u16(np);
            for (auto& cp : clients) {
                if (!cp->logged) continue;
                pl.u32(cp->id);
                pl.str(cp->name);
                pl.u16((uint16_t)std::clamp(cp->ping, 0, 65535));
                pl.u8((uint8_t)(int8_t)cp->dim);
            }
            broadcast(S_PLAYER_LIST, pl);
        }
        if (ticks % 900 == 0) saveAll();
        for (auto& cp : clients) cp->conn.flush();

        nextTick += std::chrono::milliseconds(50);
        auto now = std::chrono::steady_clock::now();
        if (nextTick > now) std::this_thread::sleep_until(nextTick);
        else if (now - nextTick > std::chrono::seconds(2)) nextTick = now; // сервер не успевает — не копим долг
    }

    logf("Stopping server, saving world...");
    for (auto& cp : clients) {
        if (!cp->logged) continue;
        net::Writer w; w.str("Server closed");
        cp->conn.send(S_KICK, w);
        cp->conn.flush();
    }
    saveAll();
    logf("Saved. Bye!");
    return 0;
}
