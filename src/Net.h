#pragma once
// Сеть для мультиплеера: TCP (Winsock), неблокирующие сокеты, кадры «длина u32 + тип u16 + данные».
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace net {

bool init();

// Запись данных пакета (little-endian)
struct Writer {
    std::vector<uint8_t> b;
    void u8(uint8_t v) { b.push_back(v); }
    void u16(uint16_t v) { raw(&v, 2); }
    void u32(uint32_t v) { raw(&v, 4); }
    void i32(int32_t v) { raw(&v, 4); }
    void i64(int64_t v) { raw(&v, 8); }
    void f32(float v) { raw(&v, 4); }
    void str(const std::string& s) { u16((uint16_t)s.size()); raw(s.data(), s.size()); }
    void raw(const void* p, size_t n) { const uint8_t* c = (const uint8_t*)p; b.insert(b.end(), c, c + n); }
};

// Чтение; при выходе за границу ok = false и возвращаются нули
struct Reader {
    const uint8_t* p = nullptr;
    size_t n = 0, pos = 0;
    bool ok = true;
    Reader(const std::vector<uint8_t>& v) : p(v.data()), n(v.size()) {}
    bool get(void* dst, size_t k) {
        if (pos + k > n) { ok = false; std::memset(dst, 0, k); return false; }
        std::memcpy(dst, p + pos, k);
        pos += k;
        return true;
    }
    uint8_t u8() { uint8_t v; get(&v, 1); return v; }
    uint16_t u16() { uint16_t v; get(&v, 2); return v; }
    uint32_t u32() { uint32_t v; get(&v, 4); return v; }
    int32_t i32() { int32_t v; get(&v, 4); return v; }
    int64_t i64() { int64_t v; get(&v, 8); return v; }
    float f32() { float v; get(&v, 4); return v; }
    std::string str() {
        uint16_t k = u16();
        if (pos + k > n) { ok = false; return {}; }
        std::string s((const char*)p + pos, k);
        pos += k;
        return s;
    }
    bool more() const { return pos < n; }
};

// Соединение: копит входящие байты и отдаёт целые пакеты, исходящие — буферизует
class Conn {
public:
    uintptr_t sock = ~(uintptr_t)0;
    bool alive = false;
    std::string peer; // адрес собеседника (для журнала сервера)

    // Подключиться к host:port (ждёт не больше timeoutMs); err — текст ошибки
    bool connect(const std::string& host, int port, int timeoutMs, std::string& err);
    void adopt(uintptr_t s, const std::string& peerName); // принятый сервером сокет
    void send(uint16_t type, const Writer& w);
    void send(uint16_t type) { send(type, Writer{}); }
    void flush();                     // отправить накопленное (без блокировки)
    void poll();                      // принять всё, что пришло
    bool next(uint16_t& type, std::vector<uint8_t>& payload);
    void close();
    size_t pendingOut() const { return out_.size() - outPos_; }

private:
    std::vector<uint8_t> in_, out_;
    size_t outPos_ = 0, inPos_ = 0;
};

class Listener {
public:
    uintptr_t sock = ~(uintptr_t)0;
    bool listen(int port, std::string& err);
    bool accept(Conn& c); // true — новый игрок
    void close();
};

// Адреса этого компьютера в локальной сети (для подсказки «подключайтесь по ...»)
std::vector<std::string> localAddresses();
// То же с именем сетевого адаптера: Wi-Fi, кабель или виртуальный (VirtualBox, VMware, Hyper-V — другу не подойдёт)
struct LocalAddr {
    std::string ip, adapter;
    bool wireless = false, virtualAdapter = false;
};
std::vector<LocalAddr> localAddressList();

} // namespace net
