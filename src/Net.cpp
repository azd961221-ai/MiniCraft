#include "Net.h"
#include <algorithm>
#include <cctype>
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

namespace net {

namespace {
const uint32_t MAX_PACKET = 8u << 20; // защита от мусора: пакет не больше 8 МБ

// Список адаптеров: если буфера мало (много виртуальных адаптеров), Windows сообщает нужный размер — повторяем
bool adapterAddresses(std::vector<uint8_t>& buf) {
    ULONG size = 32 * 1024;
    for (int attempt = 0; attempt < 4; ++attempt) {
        buf.assign(size, 0);
        ULONG r = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr,
                                       (IP_ADAPTER_ADDRESSES*)buf.data(), &size);
        if (r == NO_ERROR) return true;
        if (r != ERROR_BUFFER_OVERFLOW) return false;
    }
    return false;
}

void nonBlocking(SOCKET s) {
    u_long on = 1;
    ioctlsocket(s, FIONBIO, &on);
    BOOL nd = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&nd, sizeof(nd));
}
} // namespace

bool init() {
    static bool done = false;
    if (done) return true;
    WSADATA d;
    done = WSAStartup(MAKEWORD(2, 2), &d) == 0;
    return done;
}

bool Conn::connect(const std::string& host, int port, int timeoutMs, std::string& err) {
    init();
    close();
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) {
        err = "Unknown host";
        return false;
    }
    SOCKET s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == INVALID_SOCKET) { freeaddrinfo(res); err = "Socket error"; return false; }
    nonBlocking(s);
    ::connect(s, res->ai_addr, (int)res->ai_addrlen);
    freeaddrinfo(res);
    fd_set wr, ex;
    FD_ZERO(&wr); FD_ZERO(&ex);
    FD_SET(s, &wr); FD_SET(s, &ex);
    timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    int r = select(0, nullptr, &wr, &ex, &tv);
    if (r <= 0 || FD_ISSET(s, &ex)) {
        closesocket(s);
        err = r == 0 ? "Connection timed out" : "Connection refused";
        return false;
    }
    sock = (uintptr_t)s;
    alive = true;
    peer = host;
    in_.clear(); out_.clear(); outPos_ = 0; inPos_ = 0;
    return true;
}

void Conn::adopt(uintptr_t s, const std::string& peerName) {
    close();
    sock = s;
    alive = true;
    peer = peerName;
    nonBlocking((SOCKET)s);
    in_.clear(); out_.clear(); outPos_ = 0; inPos_ = 0;
}

void Conn::send(uint16_t type, const Writer& w) {
    if (!alive) return;
    uint32_t len = (uint32_t)w.b.size() + 2;
    const uint8_t* l = (const uint8_t*)&len;
    const uint8_t* t = (const uint8_t*)&type;
    out_.insert(out_.end(), l, l + 4);
    out_.insert(out_.end(), t, t + 2);
    out_.insert(out_.end(), w.b.begin(), w.b.end());
}

void Conn::flush() {
    if (!alive) return;
    while (outPos_ < out_.size()) {
        int n = ::send((SOCKET)sock, (const char*)out_.data() + outPos_, (int)std::min<size_t>(out_.size() - outPos_, 1 << 16), 0);
        if (n > 0) { outPos_ += (size_t)n; continue; }
        if (n == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) break;
        close();
        return;
    }
    if (outPos_ == out_.size()) { out_.clear(); outPos_ = 0; }
    else if (outPos_ > (1u << 20)) { out_.erase(out_.begin(), out_.begin() + (long)outPos_); outPos_ = 0; }
}

void Conn::poll() {
    if (!alive) return;
    char buf[65536];
    for (;;) {
        int n = recv((SOCKET)sock, buf, sizeof(buf), 0);
        if (n > 0) { in_.insert(in_.end(), buf, buf + n); continue; }
        if (n == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) break;
        close(); // 0 — собеседник закрыл соединение
        break;
    }
}

bool Conn::next(uint16_t& type, std::vector<uint8_t>& payload) {
    // Прочитанное не стираем после каждого пакета (при большой очереди это квадратичная работа) — сдвигаем изредка
    size_t avail = in_.size() - inPos_;
    if (avail < 6) {
        if (inPos_ > 0) { in_.erase(in_.begin(), in_.begin() + (long)inPos_); inPos_ = 0; }
        return false;
    }
    uint32_t len;
    std::memcpy(&len, in_.data() + inPos_, 4);
    if (len < 2 || len > MAX_PACKET) { close(); in_.clear(); inPos_ = 0; return false; }
    if (avail < 4 + (size_t)len) {
        if (inPos_ > 0) { in_.erase(in_.begin(), in_.begin() + (long)inPos_); inPos_ = 0; }
        return false;
    }
    std::memcpy(&type, in_.data() + inPos_ + 4, 2);
    payload.assign(in_.begin() + (long)inPos_ + 6, in_.begin() + (long)inPos_ + 4 + len);
    inPos_ += 4 + (size_t)len;
    if (inPos_ >= in_.size()) { in_.clear(); inPos_ = 0; }
    return true;
}

void Conn::close() {
    if (sock != ~(uintptr_t)0) closesocket((SOCKET)sock);
    sock = ~(uintptr_t)0;
    alive = false;
}

bool Listener::listen(int port, std::string& err) {
    init();
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { err = "socket() failed"; return false; }
    BOOL yes = TRUE;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY); // все сетевые карты: и Wi-Fi, и провод
    a.sin_port = htons((u_short)port);
    if (bind(s, (sockaddr*)&a, sizeof(a)) != 0) { closesocket(s); err = "Port " + std::to_string(port) + " is busy"; return false; }
    if (::listen(s, 16) != 0) { closesocket(s); err = "listen() failed"; return false; }
    u_long on = 1;
    ioctlsocket(s, FIONBIO, &on);
    sock = (uintptr_t)s;
    return true;
}

bool Listener::accept(Conn& c) {
    if (sock == ~(uintptr_t)0) return false;
    sockaddr_in a{};
    int al = sizeof(a);
    SOCKET s = ::accept((SOCKET)sock, (sockaddr*)&a, &al);
    if (s == INVALID_SOCKET) return false;
    char ip[64] = {};
    inet_ntop(AF_INET, &a.sin_addr, ip, sizeof(ip));
    c.adopt((uintptr_t)s, ip);
    return true;
}

void Listener::close() {
    if (sock != ~(uintptr_t)0) closesocket((SOCKET)sock);
    sock = ~(uintptr_t)0;
}

std::vector<LocalAddr> localAddressList() {
    std::vector<LocalAddr> out;
    std::vector<uint8_t> buf;
    if (!adapterAddresses(buf)) return out;
    auto* aa = (IP_ADAPTER_ADDRESSES*)buf.data();
    auto narrow = [](const wchar_t* w) {
        std::string s;
        if (!w) return s;
        int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
        if (n > 1) { s.resize((size_t)n - 1); WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr); }
        return s;
    };
    for (auto* a = aa; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        std::string desc = narrow(a->Description), name = narrow(a->FriendlyName);
        std::string low = desc + " " + name;
        for (auto& ch : low) ch = (char)std::tolower((unsigned char)ch);
        bool virt = low.find("virtual") != std::string::npos || low.find("vmware") != std::string::npos ||
                    low.find("hyper-v") != std::string::npos || low.find("vethernet") != std::string::npos ||
                    low.find("vpn") != std::string::npos || low.find("tap-") != std::string::npos ||
                    low.find("loopback") != std::string::npos;
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            char ip[64] = {};
            inet_ntop(AF_INET, &((sockaddr_in*)u->Address.lpSockaddr)->sin_addr, ip, sizeof(ip));
            LocalAddr la;
            la.ip = ip;
            la.adapter = name.empty() ? desc : name;
            la.wireless = a->IfType == IF_TYPE_IEEE80211;
            la.virtualAdapter = virt;
            out.push_back(la);
        }
    }
    // Настоящие адаптеры (Wi-Fi, кабель) — первыми
    std::stable_sort(out.begin(), out.end(), [](const LocalAddr& x, const LocalAddr& y) { return x.virtualAdapter < y.virtualAdapter; });
    return out;
}

std::vector<std::string> localAddresses() {
    std::vector<std::string> out;
    std::vector<uint8_t> buf;
    if (!adapterAddresses(buf)) return out;
    auto* aa = (IP_ADAPTER_ADDRESSES*)buf.data();
    for (auto* a = aa; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            char ip[64] = {};
            inet_ntop(AF_INET, &((sockaddr_in*)u->Address.lpSockaddr)->sin_addr, ip, sizeof(ip));
            out.push_back(ip);
        }
    }
    return out;
}

} // namespace net
