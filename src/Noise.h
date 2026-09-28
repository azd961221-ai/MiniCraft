#pragma once
#include <cstdint>
#include <cmath>
#include <utility>

inline uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

inline uint32_t hash3i(int x, int y, int z, uint32_t seed) {
    return hash32((uint32_t)x * 0x8da6b343U ^ (uint32_t)y * 0xd8163841U ^
                  (uint32_t)z * 0xcb1ab31fU ^ hash32(seed));
}

// Случайное число [0, 1] для целочисленной координаты
inline float hashf(int x, int y, int z, uint32_t seed) {
    return (hash3i(x, y, z, seed) & 0xFFFFFF) / float(0xFFFFFF);
}

// Improved Perlin noise (Ken Perlin, 2002)
class Perlin {
    uint8_t p[512];

    static float fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
    static float lerp(float a, float b, float t) { return a + t * (b - a); }
    static float grad(int h, float x, float y, float z) {
        int hh = h & 15;
        float u = hh < 8 ? x : y;
        float v = hh < 4 ? y : (hh == 12 || hh == 14 ? x : z);
        return ((hh & 1) ? -u : u) + ((hh & 2) ? -v : v);
    }

public:
    explicit Perlin(uint32_t seed = 0) {
        for (int i = 0; i < 256; ++i) p[i] = (uint8_t)i;
        for (int i = 255; i > 0; --i) {
            int j = hash32(seed + (uint32_t)i * 7919U) % (i + 1);
            std::swap(p[i], p[j]);
        }
        for (int i = 0; i < 256; ++i) p[256 + i] = p[i];
    }

    float noise(float x, float y, float z) const {
        float fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
        int X = (int)fx & 255, Y = (int)fy & 255, Z = (int)fz & 255;
        x -= fx; y -= fy; z -= fz;
        float u = fade(x), v = fade(y), w = fade(z);
        int A = p[X] + Y, AA = p[A] + Z, AB = p[A + 1] + Z;
        int B = p[X + 1] + Y, BA = p[B] + Z, BB = p[B + 1] + Z;
        return lerp(lerp(lerp(grad(p[AA], x, y, z), grad(p[BA], x - 1, y, z), u),
                         lerp(grad(p[AB], x, y - 1, z), grad(p[BB], x - 1, y - 1, z), u), v),
                    lerp(lerp(grad(p[AA + 1], x, y, z - 1), grad(p[BA + 1], x - 1, y, z - 1), u),
                         lerp(grad(p[AB + 1], x, y - 1, z - 1), grad(p[BB + 1], x - 1, y - 1, z - 1), u), v),
                    w);
    }

    // Фрактальный шум (сумма октав), результат примерно в [-1, 1]
    float fbm2(float x, float y, int octaves) const {
        float sum = 0, amp = 1, freq = 1, norm = 0;
        for (int i = 0; i < octaves; ++i) {
            sum += amp * noise(x * freq, y * freq, 0.37f * i + 0.5f);
            norm += amp;
            amp *= 0.5f;
            freq *= 2.0f;
        }
        return sum / norm;
    }
};
