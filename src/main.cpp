// MiniCraft — порт атмосферы и геймплея Minecraft 1.0 на C++ / OpenGL 3.3
//
// Управление:
//   WASD — ходьба, Space — прыжок/плыть, двойное W или Ctrl — бег, Shift — красться
//   Мышь — обзор, ЛКМ (удерживать) — ломать, ПКМ — ставить/использовать/есть, СКМ — выбрать блок
//   1-9 / колесо — слот, E — инвентарь, Q — выбросить (Ctrl+Q — стопку), G — режим (выживание/творческий),
//   двойной Space в творческом — полёт, F3 — отладка, F6 — поставить моба (отладка), T — чат, F9 — +6 часов, -/= — дальность, Esc — меню
//   Мир сохраняется в world.sav рядом с exe.

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "Audio.h"
#include "Blocks.h"
#include "Crafting.h"
#include "Entity.h"
#include "Gui.h"
#include "Image.h"
#include "ItemRender.h"
#include "Menu.h"
#include "Options.h"
#include "Net.h"
#include "NetCodec.h"
#include "GameLogic.h"
#include "Protocol.h"
#include <thread>
#include "Saves.h"
#include "Map.h"
#include "Mob.h"
#include "Model.h"
#include "Particles.h"
#include "Enchant.h"
#include "Physics.h"
#include "Potion.h"
#include "Player.h"
#include "UI.h"
#include "World.h"

// ---------------------------------------------------------------- Шейдеры

static const char* CHUNK_VS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec3 aLight; // затенение грани, свет неба, свет блоков
layout(location=3) in vec3 aTint;  // подкраска биома (трава, листва)
uniform mat4 uVP;
uniform vec3 uOffset;
uniform vec3 uCam;
out vec2 vUV;
out vec3 vLight;
out float vDist;
out vec3 vTint;
void main() {
    vec3 wp = aPos + uOffset;
    vTint = aTint;
    gl_Position = uVP * vec4(wp, 1.0);
    vUV = aUV;
    vLight = aLight;
    vDist = length(wp - uCam);
}
)";

// Собирается в двух вариантах: с ALPHA_TEST (листва, стекло, растения) и без.
// Шейдер без discard позволяет видеокарте отбрасывать скрытые пиксели до их расчёта (early-Z).
// Освещение повторяет lightmap 1.0: кривая яркости уровней 0..15, свет неба зависит
// от времени суток, свет блоков тёплый и слегка мерцает.
static const char* CHUNK_FS_BODY = R"(
in vec2 vUV;
in vec3 vLight;
in float vDist;
in vec3 vTint;
uniform sampler2D uTex;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform vec3 uTint;
uniform float uAlpha;
uniform float uSkyFactor;
uniform float uFlicker;
uniform float uGamma;
uniform float uAmbient;
uniform float uAlphaRef; // порог альфа-теста сверх 0.1 (растворение дракона по shuffle.png)
out vec4 FragColor;

float lightTable(float level) {
    float f = 1.0 - level / 15.0;
    return (1.0 - f) / (f * 3.0 + 1.0);
}

vec3 lightColor(float sky, float blk) {
    float s = lightTable(sky * 15.0) * uSkyFactor;
    float b = lightTable(blk * 15.0) * uFlicker;
    vec3 bc = vec3(b, b * ((b * 0.6 + 0.4) * 0.6 + 0.4), b * (b * b * 0.6 + 0.4));
    vec3 c = clamp(vec3(s) + bc, 0.0, 1.0);
    c = c * (1.0 - uAmbient) + uAmbient; // в Незере темнота не полная (0.1, как в 1.0)
    c = c * 0.96 + 0.03;
    vec3 g = 1.0 - pow(1.0 - c, vec3(4.0));
    return mix(c, g, uGamma);
}

void main() {
    vec4 c = texture(uTex, vUV);
#ifdef ALPHA_TEST
    if (c.a < max(0.1, uAlphaRef)) discard;
#endif
    vec3 col = c.rgb * vTint * vLight.x * lightColor(vLight.y, vLight.z) * uTint;
    float f = clamp((vDist - uFogStart) / (uFogEnd - uFogStart), 0.0, 1.0);
    FragColor = vec4(mix(col, uFogColor, f), c.a * uAlpha);
}
)";

static const char* SPRITE_VS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aUV;
uniform mat4 uVP;
out vec2 vUV;
void main() { gl_Position = uVP * vec4(aPos, 1.0); vUV = aUV; }
)";

static const char* SPRITE_FS = R"(#version 330 core
in vec2 vUV;
uniform sampler2D uTex;
uniform vec4 uColor;
out vec4 FragColor;
void main() {
    vec4 c = texture(uTex, vUV) * uColor;
    if (c.a < 0.01) discard;
    FragColor = c;
}
)";

// Портал Края (TileEntityEndPortalRenderer 1.0): слой — плоскость на глубине uDepth под пластиной, видимая сквозь неё;
// текстурные координаты — точка пересечения луча взгляда с этой плоскостью, масштаб, поворот и медленная прокрутка
static const char* ENDPORTAL_VS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aUV;
uniform mat4 uVP;
out vec3 vWorld;
void main() { gl_Position = uVP * vec4(aPos, 1.0); vWorld = aPos; }
)";

static const char* ENDPORTAL_FS = R"(#version 330 core
in vec3 vWorld;
uniform sampler2D uTex;
uniform vec3 uCam;
uniform float uDepth, uScale, uAngle, uScroll;
uniform vec4 uColor;
out vec4 FragColor;
void main() {
    vec3 d = normalize(vWorld - uCam);
    float t = uDepth / max(-d.y, 0.05);
    vec3 hit = vWorld + d * t;
    vec2 uv = hit.xz * uScale;
    float c = cos(uAngle), s = sin(uAngle);
    uv = mat2(c, s, -s, c) * (uv - 0.5) + 0.5;
    uv.y += uScroll;
    FragColor = texture(uTex, uv) * uColor;
}
)";

static const char* LINE_VS = R"(#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uVP;
uniform vec3 uOffset;
uniform vec3 uBoxMin;
uniform vec3 uBoxSize;
void main() { gl_Position = uVP * vec4(uOffset + uBoxMin + aPos * uBoxSize, 1.0); }
)";

static const char* LINE_FS = R"(#version 330 core
uniform vec4 uColor;
out vec4 FragColor;
void main() { FragColor = uColor; }
)";

static const char* UI_VS = R"(#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec4 aCol;
uniform mat4 uProj;
out vec2 vUV;
out vec4 vCol;
void main() { gl_Position = uProj * vec4(aPos, 0.0, 1.0); vUV = aUV; vCol = aCol; }
)";

static const char* UI_FS = R"(#version 330 core
in vec2 vUV;
in vec4 vCol;
uniform sampler2D uTex;
out vec4 FragColor;
void main() {
    vec4 c = texture(uTex, vUV) * vCol;
    if (c.a < 0.01) discard;
    FragColor = c;
}
)";

static GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Shader compile error:\n%s\n", log);
    }
    return s;
}

static GLuint makeProgram(const char* vs, const char* fs) {
    GLuint v = compileShader(GL_VERTEX_SHADER, vs), f = compileShader(GL_FRAGMENT_SHADER, fs);
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Program link error:\n%s\n", log);
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

// ---------------------------------------------------------------- Ресурсы

// Окно с ошибкой запуска: без консоли иначе игра просто молча закрывается (user32 без windows.h)
extern "C" __declspec(dllimport) int __stdcall MessageBoxW(void*, const wchar_t*, const wchar_t*, unsigned);
static int fatal(const wchar_t* text) {
    MessageBoxW(nullptr, text, L"MiniCraft", 0x10 /* MB_ICONERROR */);
    return 1;
}

static std::string findAssetRoot() {
    // assets/ в корне проекта — полный набор 1.4.2 (текстуры + sounds/); в src/1.4.2 звуков нет,
    // поэтому сборка из build/Release раньше находила src/1.4.2 и играла без звука
    std::vector<std::string> candidates = {
        exeDirectory() + "assets/",
        exeDirectory() + "../../assets/",
        exeDirectory() + "../../src/1.4.2/",
        exeDirectory() + "../../src/",
        "assets/",
        "src/1.4.2/",
        "src/",
#ifdef MC_ASSET_DIR
        MC_ASSET_DIR "/",
#endif
    };
    for (auto& c : candidates)
        if (std::filesystem::exists(std::filesystem::u8path(c + "terrain.png"))) return c;
    return "";
}

// Огонь в 1.0 рисуется процедурно (в terrain.png на его месте заглушка). Здесь — сетка «жара» 16x20:
// снизу случайные вспышки, каждая клетка остывает, усредняя соседей снизу; цвет — от красного к жёлтому.
struct FireFX {
    float cur[16 * 20] = {}, nxt[16 * 20] = {};
    uint32_t rng;
    uint8_t px[16 * 16 * 4] = {};
    explicit FireFX(uint32_t seed) : rng(seed | 1u) {}
    float rnd() {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (rng & 0xFFFFFF) / float(0x1000000);
    }
    void step() {
        for (int y = 0; y < 20; ++y)
            for (int x = 0; x < 16; ++x) {
                if (y == 19) {
                    // Как в TextureFlamesFX: произведение ТРЁХ независимых случайных (среднее 0.75), а не куб одного
                    nxt[x + y * 16] = rnd() * rnd() * rnd() * 4.f + rnd() * 0.1f + 0.2f;
                    continue;
                }
                int n = 18;
                float s = cur[x + (y + 1) * 16] * n;
                for (int i = x - 1; i <= x + 1; ++i)
                    for (int j = y; j <= y + 1; ++j)
                        if (i >= 0 && i < 16) { s += cur[i + j * 16]; ++n; }
                nxt[x + y * 16] = s / (n * 1.06f);
            }
        std::copy(std::begin(nxt), std::end(nxt), std::begin(cur));
        for (int i = 0; i < 256; ++i) {
            float f = std::clamp(cur[i] * 1.8f, 0.f, 1.f);
            px[i * 4 + 0] = (uint8_t)(f * 155.f + 100.f);
            px[i * 4 + 1] = (uint8_t)(f * f * 255.f);
            px[i * 4 + 2] = (uint8_t)(std::pow(f, 10.f) * 255.f);
            px[i * 4 + 3] = f < 0.5f ? 0 : 255;
        }
    }
};

// Вода и лава в 1.0 тоже процедурные (TextureWaterFX, TextureWaterFlowFX, TextureLavaFX, TextureLavaFlowFX):
// сетка «волн» 16x16 с затухающими случайными всплесками. У «потока» картинка сдвигается вниз каждый тик
// и заливается в четыре тайла 2x2 — бока и повёрнутый по течению верх текущей жидкости.
struct LiquidFX {
    bool lava, flow;
    float cur[256] = {}, nxt[256] = {}, heat[256] = {}, vel[256] = {};
    int ticks = 0;
    uint32_t rng;
    uint8_t px[16 * 16 * 4] = {};
    LiquidFX(bool isLava, bool isFlow, uint32_t seed) : lava(isLava), flow(isFlow), rng(seed | 1u) {}
    float rnd() {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (rng & 0xFFFFFF) / float(0x1000000);
    }
    void step() {
        ++ticks;
        if (!lava) {
            for (int i = 0; i < 16; ++i)
                for (int j = 0; j < 16; ++j) {
                    float f = 0.f;
                    if (flow)
                        for (int k = j - 2; k <= j; ++k) f += cur[(i & 15) + (k & 15) * 16];
                    else
                        for (int k = i - 1; k <= i + 1; ++k) f += cur[(k & 15) + (j & 15) * 16];
                    nxt[i + j * 16] = f / (flow ? 3.2f : 3.3f) + heat[i + j * 16] * 0.8f;
                }
            for (int n = 0; n < 256; ++n) {
                heat[n] = std::max(0.f, heat[n] + vel[n] * 0.05f);
                vel[n] -= flow ? 0.3f : 0.1f;
                if (rnd() < (flow ? 0.2f : 0.05f)) vel[n] = 0.5f;
            }
        } else {
            for (int i = 0; i < 16; ++i)
                for (int j = 0; j < 16; ++j) {
                    float f = 0.f;
                    int l = (int)(std::sin(j * 3.14159265f * 2.f / 16.f) * 1.2f);
                    int m = (int)(std::sin(i * 3.14159265f * 2.f / 16.f) * 1.2f);
                    for (int k = i - 1; k <= i + 1; ++k)
                        for (int q = j - 1; q <= j + 1; ++q) f += cur[((k + l) & 15) + ((q + m) & 15) * 16];
                    float h4 = heat[(i & 15) + (j & 15) * 16] + heat[((i + 1) & 15) + (j & 15) * 16] +
                               heat[((i + 1) & 15) + ((j + 1) & 15) * 16] + heat[(i & 15) + ((j + 1) & 15) * 16];
                    nxt[i + j * 16] = f / 10.f + h4 / 4.f * 0.8f;
                    int n = i + j * 16;
                    heat[n] = std::max(0.f, heat[n] + vel[n] * 0.01f);
                    vel[n] -= 0.06f;
                    if (rnd() < 0.005f) vel[n] = 1.5f;
                }
        }
        std::swap(cur, nxt);
        int shift = flow ? (lava ? ticks / 3 : ticks) * 16 : 0;
        for (int n = 0; n < 256; ++n) {
            float f = cur[(n - shift) & 255];
            uint8_t* p = &px[n * 4];
            if (!lava) {
                f = std::clamp(f, 0.f, 1.f);
                float f2 = f * f;
                p[0] = (uint8_t)(32.f + f2 * 32.f);
                p[1] = (uint8_t)(50.f + f2 * 64.f);
                p[2] = 255;
                p[3] = (uint8_t)(146.f + f2 * 50.f);
            } else {
                f = std::clamp(f * 2.f, 0.f, 1.f);
                p[0] = (uint8_t)(f * 100.f + 155.f);
                p[1] = (uint8_t)(f * f * 255.f);
                p[2] = (uint8_t)(f * f * f * f * 128.f);
                p[3] = 255;
            }
        }
    }
};

// Портал в 1.0 тоже процедурный: закрученные фиолетовые спирали, вращающиеся навстречу
// Точно по TexturePortalFX 1.0: 32 заранее посчитанных кадра — две спирали, закрученные навстречу друг другу,
// плюс немного шума; кадр меняется каждый тик
struct PortalFX {
    uint8_t px[16 * 16 * 4] = {};
    uint8_t frames[32][16 * 16 * 4] = {};
    int tick = 0;
    PortalFX() {
        uint32_t rng = 100u;
        auto rf = [&]() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng & 0xFFFFFF) / float(0x1000000); };
        const float PI2 = 3.141593f * 2.f;
        for (int i = 0; i < 32; ++i)
            for (int j = 0; j < 16; ++j)
                for (int k = 0; k < 16; ++k) {
                    float f = 0.f;
                    for (int l = 0; l < 2; ++l) {
                        float c = l * 16 * 0.5f;
                        float f3 = (j - c) / 16.f * 2.f, f4 = (k - c) / 16.f * 2.f;
                        if (f3 < -1.f) f3 += 2.f;
                        if (f3 >= 1.f) f3 -= 2.f;
                        if (f4 < -1.f) f4 += 2.f;
                        if (f4 >= 1.f) f4 -= 2.f;
                        float f5 = f3 * f3 + f4 * f4;
                        float f6 = std::atan2(f4, f3) + ((i / 32.f) * PI2 - f5 * 10.f + (float)(l * 2)) * (float)(l * 2 - 1);
                        f6 = (std::sin(f6) + 1.f) / 2.f;
                        f6 /= f5 + 1.f;
                        f += f6 * 0.5f;
                    }
                    f += rf() * 0.1f;
                    uint8_t* p = &frames[i][(k * 16 + j) * 4];
                    p[0] = (uint8_t)std::clamp(f * f * 200.f + 55.f, 0.f, 255.f);
                    p[1] = (uint8_t)std::clamp(f * f * f * f * 255.f, 0.f, 255.f);
                    p[2] = (uint8_t)std::clamp(f * 100.f + 155.f, 0.f, 255.f);
                    p[3] = (uint8_t)std::clamp(f * 100.f + 155.f, 0.f, 255.f);
                }
    }
    void step(float) {
        ++tick;
        std::copy(std::begin(frames[tick & 31]), std::end(frames[tick & 31]), std::begin(px));
    }
};

// Прозрачные пиксели получают цвет соседних непрозрачных (в пределах тайла), иначе при уменьшении
// их чёрный цвет подмешивается к краям травы, цветов и листвы — тёмная кайма и прожилки вдали
static void bleedTransparent(Image& img, int tile) {
    std::vector<uint8_t> filled(size_t(img.width) * img.height);
    for (size_t i = 0; i < filled.size(); ++i) filled[i] = img.rgba[i * 4 + 3] > 0;
    for (int pass = 0; pass < tile; ++pass) {
        bool changed = false;
        std::vector<uint8_t> next = filled;
        for (int y = 0; y < img.height; ++y)
            for (int x = 0; x < img.width; ++x) {
                size_t i = size_t(y) * img.width + x;
                if (filled[i]) continue;
                int sum[3] = {0, 0, 0}, n = 0;
                static const int D[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                for (auto& d : D) {
                    int nx = x + d[0], ny = y + d[1];
                    if (nx < 0 || ny < 0 || nx >= img.width || ny >= img.height) continue;
                    if (nx / tile != x / tile || ny / tile != y / tile) continue; // соседний тайл атласа не трогаем
                    size_t j = size_t(ny) * img.width + nx;
                    if (!filled[j]) continue;
                    for (int c = 0; c < 3; ++c) sum[c] += img.rgba[j * 4 + c];
                    ++n;
                }
                if (!n) continue;
                for (int c = 0; c < 3; ++c) img.rgba[i * 4 + c] = (uint8_t)(sum[c] / n);
                next[i] = 1;
                changed = true;
            }
        filled.swap(next);
        if (!changed) break;
    }
}

// Уменьшение вдвое: цвет усредняется только по видимым пикселям, прозрачность — по всем
static Image downsample(const Image& src) {
    Image dst;
    dst.width = std::max(1, src.width / 2);
    dst.height = std::max(1, src.height / 2);
    dst.rgba.assign(size_t(dst.width) * dst.height * 4, 0);
    for (int y = 0; y < dst.height; ++y)
        for (int x = 0; x < dst.width; ++x) {
            float rgb[3] = {0, 0, 0}, wsum = 0.f, asum = 0.f, plain[3] = {0, 0, 0};
            for (int k = 0; k < 4; ++k) {
                const uint8_t* p = src.at(std::min(src.width - 1, x * 2 + (k & 1)), std::min(src.height - 1, y * 2 + (k >> 1)));
                float a = p[3] / 255.f;
                for (int c = 0; c < 3; ++c) { rgb[c] += p[c] * a; plain[c] += p[c]; }
                wsum += a;
                asum += p[3];
            }
            uint8_t* d = dst.at(x, y);
            for (int c = 0; c < 3; ++c) d[c] = (uint8_t)(wsum > 0.f ? rgb[c] / wsum : plain[c] / 4.f);
            d[3] = (uint8_t)(asum / 4.f);
        }
    return dst;
}

static GLuint makeTexture(const Image& img, bool repeat = false, bool mipmaps = false) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.width, img.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    GLint wrap = repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    if (mipmaps) {
        // До уровня 4 (1 пиксель на тайл) соседние тайлы атласа не смешиваются
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 4);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
        Image lvl = img;
        int tile = 16;
        bleedTransparent(lvl, tile);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, lvl.width, lvl.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, lvl.rgba.data());
        for (int level = 1; level <= 4; ++level) {
            lvl = downsample(lvl);
            tile = std::max(1, tile / 2);
            bleedTransparent(lvl, tile);
            glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, lvl.width, lvl.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, lvl.rgba.data());
        }
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    }
    return t;
}

// Воду делаем менее фиолетовой; для интерфейса (biomeTiles) ещё и красим серые тайлы травы и листвы
// в цвет равнин — в мире их красит биом через цвет вершин
static void tintTerrain(Image& img, bool biomeTiles) {
    const glm::vec3 grass(0.57f, 0.74f, 0.35f), foliage(0.47f, 0.67f, 0.18f), water(0.45f, 0.9f, 0.85f);
    for (int t = 0; t < 256; ++t) {
        glm::vec3 c;
        if (biomeTiles && tileNeedsGrassTint(t)) c = grass;
        else if (biomeTiles && tileNeedsFoliageTint(t)) c = foliage;
        else if (t == T(13, 12) || t == T(14, 12) || t == T(13, 13) || t == T(14, 13)) c = water;
        else continue;
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) {
                uint8_t* p = img.at((t % 16) * 16 + x, (t / 16) * 16 + y);
                for (int i = 0; i < 3; ++i) p[i] = (uint8_t)(p[i] * c[i]);
            }
    }
}

// В присланном particles.png только 8 кадров дыма. Дорисовываем остальные спрайты 1.0
// в тех же клетках 8x8: 19..22 капли, 32 пузырь, 48 огонь, 49 лава, 65 крит
static void paintParticleSprites(Image& img) {
    if (img.width < 128 || img.height < 128) return;
    int cw = img.width / 16, ch = img.height / 16;
    int scale = std::max(1, cw / 8);
    auto sprite = [&](int index, const char* rows[8], auto colorOf) {
        int cx = (index % 16) * cw, cy = (index / 16) * ch;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) {
                glm::vec4 c = colorOf(rows[y][x]);
                for (int dy = 0; dy < scale; ++dy)
                    for (int dx = 0; dx < scale; ++dx) {
                        uint8_t* px = img.at(cx + x * scale + dx, cy + y * scale + dy);
                        px[0] = (uint8_t)(c.r * 255); px[1] = (uint8_t)(c.g * 255); px[2] = (uint8_t)(c.b * 255); px[3] = (uint8_t)(c.a * 255);
                    }
            }
    };
    auto fire = [](char ch) {
        switch (ch) {
        case 'o': return glm::vec4(1.f, 0.45f, 0.f, 1.f);
        case 'y': return glm::vec4(1.f, 0.8f, 0.15f, 1.f);
        case 'w': return glm::vec4(1.f, 1.f, 0.75f, 1.f);
        default: return glm::vec4(0.f);
        }
    };
    auto water = [](char ch) {
        switch (ch) {
        case 'b': return glm::vec4(0.25f, 0.4f, 0.85f, 1.f);
        case 'l': return glm::vec4(0.6f, 0.75f, 1.f, 1.f);
        default: return glm::vec4(0.f);
        }
    };
    const char* flame[8] = {"........", "...o....", "..oo....", "..oyo...", ".oyyo...", ".oywyo..", ".oywyo..", "..oyyo.."};
    const char* lava[8] = {"........", "........", "...oo...", "..oyyo..", "..oywo..", "...oo...", "........", "........"};
    const char* crit[8] = {"........", "...w....", "...w....", ".wwyww..", "...w....", "...w....", "........", "........"};
    const char* rain0[8] = {"........", "........", "........", "...l....", "..bl....", "..bb....", "........", "........"};
    const char* rain1[8] = {"........", "........", "..l..l..", "..b..b..", "........", "........", "........", "........"};
    const char* rain2[8] = {"........", "........", "........", "....l...", "...lb...", "...bb...", "..b..b..", "........"};
    const char* rain3[8] = {"........", "........", ".l....l.", ".b....b.", "........", "...l....", "...b....", "........"};
    const char* bubble[8] = {"........", "..llll..", ".l....l.", ".l.l..l.", ".l....l.", "..llll..", "........", "........"};
    sprite(48, flame, fire);
    sprite(49, lava, fire);
    sprite(65, crit, fire);
    sprite(19, rain0, water);
    sprite(20, rain1, water);
    sprite(21, rain2, water);
    sprite(22, rain3, water);
    sprite(32, bubble, water);
    // Нота (EntityNoteFX): белая, красится цветом высоты тона
    const char* note[8] = {"........", "...ww...", "...w.w..", "...w..w.", "...w....", ".www....", "wwww....", ".ww....."};
    sprite(64, note, [](char ch) { return ch == 'w' ? glm::vec4(1.f) : glm::vec4(0.f); });
    const char* heart[8] = {"........", ".rr.rr..", "rwrrrrr.", "rrrrrrr.", ".rrrrr..", "..rrr...", "...r....", "........"};
    sprite(80, heart, [](char ch) {
        return ch == 'r' ? glm::vec4(0.9f, 0.1f, 0.15f, 1.f) : ch == 'w' ? glm::vec4(1.f, 0.7f, 0.7f, 1.f) : glm::vec4(0.f);
    });
}

// ---------------------------------------------------------------- Луч выбора блока (DDA)

// liquids — останавливаться на воде и лаве (для ведра)
static bool raycast(const World& w, glm::vec3 o, glm::vec3 d, float maxDist, glm::ivec3& hit, glm::ivec3& prev,
                    bool liquids = false) {
    glm::ivec3 p((int)std::floor(o.x), (int)std::floor(o.y), (int)std::floor(o.z));
    glm::ivec3 step;
    glm::vec3 tMax, tDelta;
    for (int i = 0; i < 3; ++i) {
        if (d[i] > 0) { step[i] = 1; tMax[i] = (p[i] + 1 - o[i]) / d[i]; tDelta[i] = 1 / d[i]; }
        else if (d[i] < 0) { step[i] = -1; tMax[i] = (o[i] - p[i]) / -d[i]; tDelta[i] = -1 / d[i]; }
        else { step[i] = 0; tMax[i] = 1e30f; tDelta[i] = 1e30f; }
    }
    prev = p;
    float t = 0;
    while (t <= maxDist) {
        uint8_t b = w.getBlock(p.x, p.y, p.z);
        if (isTargetable(b) || (liquids && isLiquid(b) && w.getMeta(p.x, p.y, p.z) == 0)) {
            std::vector<AABB> boxes;
            selectionBoxes(w, p.x, p.y, p.z, boxes);
            if (boxes.size() == 1 && boxes[0].mn == glm::vec3(0.f) && boxes[0].mx == glm::vec3(1.f)) { hit = p; return true; }
            // Пересечение луча с каждой коробкой формы (slab-тест)
            for (const AABB& bx : boxes) {
                glm::vec3 bmin = glm::vec3(p) + bx.mn, bmax = glm::vec3(p) + bx.mx;
                float t0 = 0.f, t1 = maxDist;
                bool ok = true;
                for (int i = 0; i < 3 && ok; ++i) {
                    if (std::abs(d[i]) < 1e-7f) { ok = o[i] >= bmin[i] && o[i] <= bmax[i]; continue; }
                    float a = (bmin[i] - o[i]) / d[i], c = (bmax[i] - o[i]) / d[i];
                    if (a > c) std::swap(a, c);
                    t0 = std::max(t0, a);
                    t1 = std::min(t1, c);
                    ok = t0 <= t1;
                }
                if (ok) { hit = p; return true; }
            }
        }
        prev = p;
        int a = (tMax.x < tMax.y) ? (tMax.x < tMax.z ? 0 : 2) : (tMax.y < tMax.z ? 1 : 2);
        p[a] += step[a];
        t = tMax[a];
        tMax[a] += tDelta[a];
    }
    return false;
}

// ---------------------------------------------------------------- Отсечение по пирамиде видимости

struct Frustum {
    glm::vec4 planes[6];
    explicit Frustum(const glm::mat4& m) {
        auto row = [&](int i) { return glm::vec4(m[0][i], m[1][i], m[2][i], m[3][i]); };
        planes[0] = row(3) + row(0); planes[1] = row(3) - row(0);
        planes[2] = row(3) + row(1); planes[3] = row(3) - row(1);
        planes[4] = row(3) + row(2); planes[5] = row(3) - row(2);
    }
    bool boxVisible(const glm::vec3& mn, const glm::vec3& mx) const {
        for (const auto& p : planes) {
            glm::vec3 v(p.x > 0 ? mx.x : mn.x, p.y > 0 ? mx.y : mn.y, p.z > 0 ? mx.z : mn.z);
            if (p.x * v.x + p.y * v.y + p.z * v.z + p.w < 0) return false;
        }
        return true;
    }
};

// ---------------------------------------------------------------- Ввод

enum class Screen { Playing, Paused, Container, Dead, Menu, Sign, Sleep, Credits, Achievements, Chat };

struct Input {
    Screen screen = Screen::Playing;
    int selected = 0;
    bool debug = false;
    bool placeClick = false, pickClick = false, menuClick = false, attackClick = false;
    // Ввод для меню: набранный текст и служебные клавиши
    std::string typed;
    bool keyBackspace = false, keyEnter = false, keyEscape = false, keyTab = false, keyUp = false, keyDown = false;
    bool doubleClick = false;
    double lastClickTime = -1.0;
    int clickButton = -1;        // клик в окне с предметами: 0 — левая, 1 — правая
    bool clickShift = false;
    bool mouseReleased = false;
    float scrollDelta = 0.f;
    bool openInventory = false, closeGui = false;
    bool dropKey = false, dropStack = false;
    bool timeSkip = false, toggleMode = false, debugSpawn = false, toggleWeather = false, toggleSmooth = false;
    int renderDelta = 0;         // -/= меняют дальность прорисовки
    int invNumber = -1;          // цифра, нажатая в окне с предметами
    bool ignoreLmb = false;      // клик, вернувший курсор, не должен ломать блок
    bool firstMouse = true;
    double lastX = 0, lastY = 0;
    // Двойные нажатия (в тиках)
    int64_t tick = 0, lastW = -100, lastSpace = -100;
    int lastKey = -1;            // последняя нажатая клавиша в меню (назначение клавиш)
    bool cycleCamera = false;    // F5: вид от первого лица / сзади / спереди
    int openChat = 0;            // 1 — T (пустая строка), 2 — «/» (сразу команда)
    bool chatSkipChar = false;   // символ клавиши, открывшей чат, не печатаем
    bool sprintTap = false, flyTap = false;
};

static Input g_in;

static void setCapture(GLFWwindow* win, bool cap) {
    glfwSetInputMode(win, GLFW_CURSOR, cap ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    g_in.firstMouse = true;
}

static void openScreen(GLFWwindow* win, Screen s) {
    g_in.screen = s;
    setCapture(win, s == Screen::Playing);
    if (s == Screen::Playing) g_in.ignoreLmb = true;
}

// Назначенные клавиши (Options::keys); обновляются в applyOptions
static int g_keys[KB_COUNT] = {GLFW_KEY_W, GLFW_KEY_A, GLFW_KEY_S, GLFW_KEY_D, GLFW_KEY_SPACE, GLFW_KEY_E, GLFW_KEY_Q, GLFW_KEY_LEFT_SHIFT};

static void keyCallback(GLFWwindow* win, int key, int, int action, int mods) {
    if (action == GLFW_RELEASE) return;
    Screen s = g_in.screen;
    if (s == Screen::Menu) {
        if (key == GLFW_KEY_BACKSPACE) g_in.keyBackspace = true;
        if (action != GLFW_PRESS) return;
        g_in.lastKey = key;
        if (key == GLFW_KEY_ESCAPE) g_in.keyEscape = true;
        if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) g_in.keyEnter = true;
        if (key == GLFW_KEY_TAB) g_in.keyTab = true;
        return;
    }
    if (s == Screen::Chat) {
        if (key == GLFW_KEY_BACKSPACE) g_in.keyBackspace = true;
        if (key == GLFW_KEY_TAB) g_in.keyTab = true; // автодополнение (с повтором — следующий вариант)
        if (action != GLFW_PRESS) return;
        if (key == GLFW_KEY_ESCAPE) g_in.keyEscape = true;
        if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) g_in.keyEnter = true;
        if (key == GLFW_KEY_UP) g_in.keyUp = true;
        if (key == GLFW_KEY_DOWN) g_in.keyDown = true;
        return;
    }
    if (s == Screen::Sign) {
        if (key == GLFW_KEY_BACKSPACE) g_in.keyBackspace = true;
        if (action != GLFW_PRESS) return;
        if (key == GLFW_KEY_ESCAPE) g_in.keyEscape = true;
        if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER || key == GLFW_KEY_DOWN || key == GLFW_KEY_TAB) g_in.keyDown = true;
        if (key == GLFW_KEY_UP) g_in.keyUp = true;
        return;
    }
    if (action != GLFW_PRESS) return;
    // Переназначаемые клавиши
    if (key == g_keys[KB_INVENTORY]) {
        if (s == Screen::Container) g_in.closeGui = true;
        else if (s == Screen::Playing) g_in.openInventory = true;
        return;
    }
    if (key == g_keys[KB_DROP]) {
        if (s == Screen::Playing) { g_in.dropKey = true; g_in.dropStack = (mods & GLFW_MOD_CONTROL) != 0; }
        return;
    }
    if (key == g_keys[KB_FORWARD] && s == Screen::Playing) {
        if (g_in.tick - g_in.lastW <= 7) g_in.sprintTap = true;
        g_in.lastW = g_in.tick;
    }
    if (key == g_keys[KB_JUMP] && s == Screen::Playing) {
        if (g_in.tick - g_in.lastSpace <= 7) { g_in.flyTap = true; g_in.lastSpace = -100; }
        else g_in.lastSpace = g_in.tick;
    }
    switch (key) {
    case GLFW_KEY_F5: if (s == Screen::Playing) g_in.cycleCamera = true; break;
    case GLFW_KEY_ESCAPE:
        if (s == Screen::Playing) openScreen(win, Screen::Paused);
        else if (s == Screen::Container) g_in.closeGui = true;
        else if (s == Screen::Paused) openScreen(win, Screen::Playing);
        else if (s == Screen::Sleep || s == Screen::Credits) g_in.closeGui = true;
        else if (s == Screen::Achievements) openScreen(win, Screen::Paused);
        break;
    case GLFW_KEY_G: if (s == Screen::Playing) g_in.toggleMode = true; break;
    case GLFW_KEY_F3: g_in.debug = !g_in.debug; break;
    case GLFW_KEY_F6: if (s == Screen::Playing) g_in.debugSpawn = true; break;
    case GLFW_KEY_F7: if (s == Screen::Playing) g_in.toggleWeather = true; break;
    case GLFW_KEY_F8: g_in.toggleSmooth = true; break;
    case GLFW_KEY_T: if (s == Screen::Playing) { g_in.openChat = 1; g_in.chatSkipChar = true; } break;
    case GLFW_KEY_SLASH: if (s == Screen::Playing) { g_in.openChat = 2; g_in.chatSkipChar = true; } break;
    case GLFW_KEY_F9: if (s == Screen::Playing) g_in.timeSkip = true; break;
    case GLFW_KEY_MINUS: g_in.renderDelta = -1; break;
    case GLFW_KEY_EQUAL: g_in.renderDelta = +1; break;
    default:
        if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9) {
            if (s == Screen::Container) g_in.invNumber = key - GLFW_KEY_1;
            else if (s == Screen::Playing) g_in.selected = key - GLFW_KEY_1;
        }
    }
}

static void mouseButtonCallback(GLFWwindow*, int button, int action, int mods) {
    if (action == GLFW_RELEASE) { g_in.mouseReleased = true; return; }
    if (action != GLFW_PRESS) return;
    if (g_in.screen != Screen::Playing) {
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            g_in.menuClick = true;
            double t = glfwGetTime();
            g_in.doubleClick = t - g_in.lastClickTime < 0.3;
            g_in.lastClickTime = t;
        }
        if (button == GLFW_MOUSE_BUTTON_LEFT || button == GLFW_MOUSE_BUTTON_RIGHT) {
            g_in.clickButton = button == GLFW_MOUSE_BUTTON_LEFT ? 0 : 1;
            g_in.clickShift = (mods & GLFW_MOD_SHIFT) != 0;
        }
        return;
    }
    if (button == GLFW_MOUSE_BUTTON_LEFT) g_in.attackClick = true;
    if (button == GLFW_MOUSE_BUTTON_RIGHT) g_in.placeClick = true;
    if (button == GLFW_MOUSE_BUTTON_MIDDLE) g_in.pickClick = true;
}

static void charCallback(GLFWwindow*, unsigned int cp) {
    if (g_in.chatSkipChar) { g_in.chatSkipChar = false; return; }
    if (g_in.screen == Screen::Chat) {
        if (cp >= 32 && cp < 127) g_in.typed += (char)cp; // шрифт 1.0 — только ASCII
        return;
    }
    if (g_in.screen == Screen::Sign) {
        if (cp >= 32 && cp < 127) g_in.typed += (char)cp; // шрифт 1.0 — только ASCII
        return;
    }
    if (g_in.screen != Screen::Menu) return;
    // Кодируем символ в UTF-8
    if (cp < 0x80) g_in.typed += (char)cp;
    else if (cp < 0x800) { g_in.typed += (char)(0xC0 | (cp >> 6)); g_in.typed += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) {
        g_in.typed += (char)(0xE0 | (cp >> 12));
        g_in.typed += (char)(0x80 | ((cp >> 6) & 0x3F));
        g_in.typed += (char)(0x80 | (cp & 0x3F));
    }
}

static void scrollCallback(GLFWwindow*, double, double yoff) {
    if (g_in.screen == Screen::Container || g_in.screen == Screen::Menu) { g_in.scrollDelta += (float)yoff; return; }
    if (g_in.screen != Screen::Playing) return;
    if (yoff < 0) g_in.selected = (g_in.selected + 1) % 9;
    else if (yoff > 0) g_in.selected = (g_in.selected + 8) % 9;
}

// ---------------------------------------------------------------- Небо

struct Sky {
    glm::vec3 fog;       // цвет неба/тумана
    float skyFactor;     // яркость света неба (день 1.0, ночь ~0.05)
    float celestial;     // угол небесных тел, 0..1 (0 — полдень)
    float starAlpha;
};

// Время в тиках: 0 — рассвет (6:00), 6000 — полдень, 18000 — полночь; сутки 24000 тиков
static Sky computeSky(int64_t worldTime, float partial, const glm::vec3& viewDir, float rain = 0.f, float thunder = 0.f,
                      float flash = 0.f) {
    Sky s;
    float t = ((worldTime % 24000) + partial) / 24000.f - 0.25f;
    if (t < 0) t += 1.f;
    // Небесный угол, как в оригинале: чуть дольше день, чуть короче сумерки
    float ang = t + ((1.f - (std::cos(t * glm::pi<float>()) + 1.f) / 2.f) - t) / 3.f;
    s.celestial = ang;
    float c = std::cos(ang * glm::two_pi<float>());
    float day = std::clamp(c * 2.f + 0.5f, 0.f, 1.f);
    s.skyFactor = day * 0.95f + 0.05f;
    s.starAlpha = std::clamp(1.f - (c * 2.f + 0.75f), 0.f, 1.f) * 0.75f;

    glm::vec3 daySky(0.53f, 0.71f, 1.0f), fog = glm::vec3(0.753f, 0.847f, 1.0f);
    s.fog = glm::mix(fog * 0.03f, glm::mix(fog, daySky, 0.25f), day);
    // Закат/рассвет: оранжевое зарево со стороны солнца
    float sunX = std::sin(ang * glm::two_pi<float>());
    glm::vec2 v2(viewDir.x, viewDir.z);
    float facing = glm::length(v2) > 0.001f ? std::max(0.f, -glm::normalize(v2).x * (sunX > 0 ? 1.f : -1.f)) : 0.f;
    float glow = std::max(0.f, 1.f - std::abs(c) / 0.4f) * facing * 0.7f;
    s.fog = glm::mix(s.fog, glm::vec3(0.95f, 0.45f, 0.2f), glow * (1.f - rain));
    // Дождь и гроза темнят небо и свет (как в 1.0: до 5/16 каждый), вспышка молнии освещает
    s.skyFactor *= (1.f - rain * 5.f / 16.f) * (1.f - thunder * 5.f / 16.f);
    float lum = glm::dot(s.fog, glm::vec3(0.3f, 0.59f, 0.11f));
    s.fog = glm::mix(s.fog, glm::vec3(lum * 0.6f), rain * 0.6f);
    s.fog *= 1.f - thunder * 0.5f;
    s.starAlpha *= 1.f - rain;
    if (flash > 0.f) {
        s.skyFactor = glm::mix(s.skyFactor, 1.f, flash);
        s.fog = glm::mix(s.fog, glm::vec3(0.7f, 0.7f, 0.8f), flash * 0.5f);
    }
    return s;
}

// Вершины модели, перенесённые матрицей в мир, с освещением точки
static void appendTransformed(std::vector<Vertex>& out, const std::vector<Vertex>& model, const glm::mat4& m, float sky, float bl) {
    for (const Vertex& v : model) {
        glm::vec4 p = m * glm::vec4(v.x, v.y, v.z, 1.f);
        out.push_back({p.x, p.y, p.z, v.u, v.v, v.shade, sky, bl});
    }
}

// ---------------------------------------------------------------- main

int main(int argc, char** argv) {
    // --save-dir <папка>: где хранить мир (по умолчанию рядом с exe) — удобно для тестов
    // Для разработки: --world <папка> — сразу войти в мир (новый — творческий),
    // --showcase — поставить у спавна витрину блоков (проверка отрисовки)
    std::string saveDir = exeDirectory(), autoWorld;
    bool showcase = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--save-dir" && i + 1 < argc) {
            saveDir = argv[++i];
            if (!saveDir.empty() && saveDir.back() != '/' && saveDir.back() != '\\') saveDir += '/';
        } else if (a == "--world" && i + 1 < argc) {
            autoWorld = argv[++i];
        } else if (a == "--showcase") {
            showcase = true;
        }
    }

    const std::string assets = findAssetRoot();
    if (assets.empty()) {
        std::fprintf(stderr, "Assets not found (terrain.png). Put the 'assets' folder next to the exe.\n");
        return fatal(L"Не найдены ресурсы игры (assets\\terrain.png).\n\n"
                     L"Папка assets должна лежать рядом с MiniCraft.exe.");
    }

    if (!glfwInit()) { std::fprintf(stderr, "glfwInit failed\n"); return fatal(L"Не удалось инициализировать GLFW."); }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* win = glfwCreateWindow(1280, 720, "MiniCraft", nullptr, nullptr);
    if (!win) {
        std::fprintf(stderr, "Failed to create window\n");
        glfwTerminate();
        return fatal(L"Не удалось создать окно с OpenGL 3.3.\n\n"
                     L"Видеокарта или драйвер не поддерживают OpenGL 3.3 — обновите драйвер видеокарты.");
    }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::fprintf(stderr, "Failed to load OpenGL\n");
        return fatal(L"Не удалось загрузить функции OpenGL. Обновите драйвер видеокарты.");
    }

    glfwSetKeyCallback(win, keyCallback);
    glfwSetMouseButtonCallback(win, mouseButtonCallback);
    glfwSetScrollCallback(win, scrollCallback);
    glfwSetCharCallback(win, charCallback);
    if (glfwRawMouseMotionSupported()) glfwSetInputMode(win, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    setCapture(win, true);

    const std::string fsCutout = std::string("#version 330 core\n#define ALPHA_TEST\n") + CHUNK_FS_BODY;
    const std::string fsSolid = std::string("#version 330 core\n") + CHUNK_FS_BODY;
    GLuint chunkProg = makeProgram(CHUNK_VS, fsCutout.c_str()); // с alpha test
    GLuint solidProg = makeProgram(CHUNK_VS, fsSolid.c_str());  // без discard
    GLuint spriteProg = makeProgram(SPRITE_VS, SPRITE_FS);
    GLuint endPortalProg = makeProgram(ENDPORTAL_VS, ENDPORTAL_FS);
    GLuint lineProg = makeProgram(LINE_VS, LINE_FS);
    GLuint uiProg = makeProgram(UI_VS, UI_FS);

    // ---- Текстуры
    auto load = [&](const char* rel, Image& img) {
        if (!loadImage(assets + rel, img)) std::fprintf(stderr, "Missing texture: %s\n", rel);
        if (img.empty()) { img.width = img.height = 1; img.rgba = {255, 0, 255, 255}; }
    };
    Image terrainImg, guiImg, iconsImg, fontImg, sunImg, moonImg, cloudsImg, particlesImg;
    Image itemsImg, inventoryImg, craftingImg, furnaceImg, containerImg, allitemsImg;
    Image creativeListImg, creativeSearchImg, creativeSurvivalImg;
    load("terrain.png", terrainImg);
    load("gui/gui.png", guiImg);
    load("gui/icons.png", iconsImg);
    bool hasDefaultFont = std::filesystem::exists(std::filesystem::u8path(assets + "font/default.png"));
    load(hasDefaultFont ? "font/default.png" : "gui/font.png", fontImg);
    // Полный particles.png 1.0 (128x128) лежит в корне ассетов; старый неполный — запасной
    bool fullParticles = std::filesystem::exists(std::filesystem::u8path(assets + "particles.png"));
    load(fullParticles ? "particles.png" : "gui/particles.png", particlesImg);
    load("gui/items.png", itemsImg);
    load("gui/inventory.png", inventoryImg);
    load("gui/crafting.png", craftingImg);
    load("gui/furnace.png", furnaceImg);
    load("gui/container.png", containerImg);
    load("gui/allitems.png", allitemsImg);
    load("gui/creative_inv/list_items.png", creativeListImg);
    load("gui/creative_inv/search.png", creativeSearchImg);
    load("gui/creative_inv/survival_inv.png", creativeSurvivalImg);
    // 1.4.2: солнце и фазы Луны лежат в terrain/ (moon_phases.png — 8 фаз сеткой 4x2); старые ассеты — в textures/
    auto assetExists = [&](const char* rel) { return std::filesystem::exists(std::filesystem::u8path(assets + rel)); };
    load(assetExists("terrain/sun.png") ? "terrain/sun.png" : "textures/sun.png", sunImg);
    const bool moonPhases = assetExists("terrain/moon_phases.png");
    load(moonPhases ? "terrain/moon_phases.png" : assetExists("terrain/moon.png") ? "terrain/moon.png" : "textures/moon.png", moonImg);
    load("environment/clouds.png", cloudsImg);
    // В terrain.png 1.4.2 тайлов сундука нет (на (9,1) — блок изумруда, соседние клетки пустые), а иконка сундука
    // в инвентаре, в руке и частицы берутся из атласа. Собираем верх, бок и перед из развёртки модели (item/chest.png,
    // item/enderchest.png) в свободные клетки: сундук — (10,2), (10,1), (11,1); эндер-сундук — (10,3), (9,3), (11,11)
    {
        auto composeChestTiles = [&](const Image& src, int topT, int sideT, int frontT) {
            if (src.width < 56 || src.height < 43 || terrainImg.width < 256) return;
            auto put = [&](int t, auto&& pixel) {
                for (int y = 0; y < 16; ++y)
                    for (int x = 0; x < 16; ++x) {
                        const uint8_t* s = pixel(x, y);
                        uint8_t* d = terrainImg.at((t % 16) * 16 + x, (t / 16) * 16 + y);
                        std::copy(s, s + 4, d);
                    }
            };
            // Верх крышки 14x14 в (14,0) растягиваем на 16x16
            put(topT, [&](int x, int y) { return src.at(14 + x * 14 / 16, y * 14 / 16); });
            // Бок и перед: полоса крышки (5 строк, v=14) над стенкой основания (10 строк, v=33), 14x15 -> 16x16
            auto sideAt = [&](int u0, int x, int y, bool knob) {
                int sx = x * 14 / 16, sy = y * 15 / 16;
                if (knob && sx >= 6 && sx <= 7 && sy >= 3 && sy <= 6) return src.at(1 + (sx - 6), 1 + (sy - 3)); // замок (2x4 в (1,1))
                return sy < 5 ? src.at(u0 + sx, 14 + sy) : src.at(u0 + sx, 33 + (sy - 5));
            };
            put(sideT, [&](int x, int y) { return sideAt(28, x, y, false); });
            put(frontT, [&](int x, int y) { return sideAt(14, x, y, true); });
        };
        Image chestSrc;
        if (loadImage(assets + "item/chest.png", chestSrc)) composeChestTiles(chestSrc, T(10, 2), T(10, 1), T(11, 1));
        if (loadImage(assets + "item/enderchest.png", chestSrc)) composeChestTiles(chestSrc, T(10, 3), T(9, 3), T(11, 11));
    }
    Image terrainUiImg = terrainImg;
    tintTerrain(terrainImg, false);
    tintTerrain(terrainUiImg, true);
    Image rainImg, snowImg;
    load("environment/rain.png", rainImg);
    load("environment/snow.png", snowImg);

    GLuint terrainTex = makeTexture(terrainImg, false, true);       // мир: трава и листва серые, красит биом
    // Обновить анимированный тайл атласа мира (привязанного) вместе с его мип-уровнями 1..4
    auto uploadAnimTile = [](int tx, int ty, const uint8_t* px) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, tx * 16, ty * 16, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, px);
        std::vector<uint8_t> cur(px, px + 16 * 16 * 4), nxt;
        for (int level = 1, size = 16; level <= 4; ++level) {
            int ns = size / 2;
            nxt.assign((size_t)ns * ns * 4, 0);
            for (int y = 0; y < ns; ++y)
                for (int x = 0; x < ns; ++x) {
                    float rgb[3] = {0, 0, 0}, plain[3] = {0, 0, 0}, wsum = 0.f, asum = 0.f;
                    for (int k = 0; k < 4; ++k) {
                        const uint8_t* q = &cur[((size_t)(y * 2 + (k >> 1)) * size + (x * 2 + (k & 1))) * 4];
                        float a = q[3] / 255.f;
                        for (int c = 0; c < 3; ++c) { rgb[c] += q[c] * a; plain[c] += q[c]; }
                        wsum += a;
                        asum += q[3];
                    }
                    uint8_t* d = &nxt[((size_t)y * ns + x) * 4];
                    for (int c = 0; c < 3; ++c) d[c] = (uint8_t)(wsum > 0.f ? rgb[c] / wsum : plain[c] / 4.f);
                    d[3] = (uint8_t)(asum / 4.f);
                }
            glTexSubImage2D(GL_TEXTURE_2D, level, (tx * 16) >> level, (ty * 16) >> level, ns, ns, GL_RGBA, GL_UNSIGNED_BYTE, nxt.data());
            cur.swap(nxt);
            size = ns;
        }
    };
    FireFX fireA(0x1234567u), fireB(0x7654321u);
    LiquidFX waterFx(false, false, 0x51u), waterFlowFx(false, true, 0x52u), lavaFx(true, false, 0x53u), lavaFlowFx(true, true, 0x54u);
    PortalFX portalFx;
    // Базовые пиксели компаса (6,3) и часов (6,4) из items.png — поверх рисуются стрелка и диск
    std::vector<uint8_t> compassBase(16 * 16 * 4), clockBase(16 * 16 * 4);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
            for (int ch = 0; ch < 4; ++ch) {
                compassBase[(y * 16 + x) * 4 + ch] = itemsImg.at(6 * 16 + x, 3 * 16 + y)[ch];
                clockBase[(y * 16 + x) * 4 + ch] = itemsImg.at(6 * 16 + x, 4 * 16 + y)[ch];
            }
    float compassAngle = 0.f, compassVel = 0.f, clockAngle = 0.f, clockVel = 0.f;
    Image dialImg; // циферблат часов 1.0 (misc/dial.png, 16x16)
    bool haveDial = loadImage(assets + "misc/dial.png", dialImg) && dialImg.width >= 16 && dialImg.height >= 16;
    GLuint terrainUiTex = makeTexture(terrainUiImg, false, true);   // интерфейс, рука, предметы
    GLuint rainTex = makeTexture(rainImg, true), snowTex = makeTexture(snowImg, true);
    GLuint guiTex = makeTexture(guiImg);
    GLuint iconsTex = makeTexture(iconsImg);
    Image achBgImg;
    bool haveAchBg = loadImage(assets + "achievement/bg.png", achBgImg);
    GLuint achBgTex = haveAchBg ? makeTexture(achBgImg) : 0;
    GLuint fontTex = makeTexture(fontImg);
    if (!fullParticles) paintParticleSprites(particlesImg);
    GLuint particlesTex = makeTexture(particlesImg);
    GLuint itemsTex = makeTexture(itemsImg);
    // Текстура книги (раскладка item/book.png 1.0, 64x32; файла нет в ассетах — рисуем):
    // обложки и корешок — коричневая кожа, страницы — светлые с «строчками»
    GLuint bookTex;
    Image bookFile;
    if (loadImage(assets + "item/book.png", bookFile)) bookTex = makeTexture(bookFile);
    else {
        Image bi;
        bi.width = 64; bi.height = 32;
        bi.rgba.assign(64 * 32 * 4, 0);
        auto put = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
            uint8_t* p = bi.at(x, y);
            p[0] = r; p[1] = g; p[2] = b; p[3] = 255;
        };
        for (int y = 0; y < 10; ++y)
            for (int x = 0; x < 28; ++x) {
                bool spine = x >= 12 && x < 16;
                bool edge = y == 0 || y == 9 || x % 6 == 0 || x % 6 == 5 || spine;
                uint8_t k = (uint8_t)((x * 7 + y * 13) % 3 * 6);
                if (spine) put(x, y, 70 + k, 38, 20);
                else if (edge) put(x, y, 88 + k, 48, 24);
                else put(x, y, 120 + k, 68, 34);
            }
        for (int y = 10; y < 19; ++y)
            for (int x = 0; x < 34; ++x) {
                bool line = (y % 2 == 0) && (x % 6 != 0) && y > 11 && y < 18;
                if (line) put(x, y, 170, 160, 140);
                else put(x, y, 238, 230, 208);
            }
        bookTex = makeTexture(bi);
    }
    // Карта в руке: подложка misc/mapbg.png, значки misc/mapicons.png (8x8 в сетке 4x4), сама карта 128x128
    GLuint mapBgTex = 0, mapIconsTex = 0, mapTex = 0;
    int mapTexId = -1;
    {
        Image mi;
        if (loadImage(assets + "misc/mapbg.png", mi)) mapBgTex = makeTexture(mi);
        if (loadImage(assets + "misc/mapicons.png", mi)) mapIconsTex = makeTexture(mi);
        glGenTextures(1, &mapTex);
        glBindTexture(GL_TEXTURE_2D, mapTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 128, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    // Картины: art/kz.png (сюжеты и деревянная изнанка в 192,0)
    GLuint paintingTex = 0;
    {
        Image ki;
        if (loadImage(assets + "art/kz.png", ki)) paintingTex = makeTexture(ki);
    }
    // Шары опыта: item/xporb.png, 4x4 значка по 16 пикселей
    GLuint xpOrbTex = 0;
    {
        Image xi;
        if (loadImage(assets + "item/xporb.png", xi)) xpOrbTex = makeTexture(xi);
    }
    // Портал Края: misc/tunnel.png (нижний слой) и misc/particlefield.png (цветные слои), с повтором
    GLuint tunnelTex = 0, fieldTex = 0;
    {
        Image ti;
        auto rep = [](GLuint t) {
            glBindTexture(GL_TEXTURE_2D, t);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        };
        if (loadImage(assets + "misc/tunnel.png", ti)) { tunnelTex = makeTexture(ti); rep(tunnelTex); }
        if (loadImage(assets + "misc/particlefield.png", ti)) { fieldTex = makeTexture(ti); rep(fieldTex); }
    }
    // Сундуки: item/chest.png (64x64) и item/largechest.png (128x64)
    GLuint chestTex = 0, largeChestTex = 0, enderChestTex = 0;
    {
        Image ci;
        if (loadImage(assets + "item/chest.png", ci)) chestTex = makeTexture(ci);
        if (loadImage(assets + "item/largechest.png", ci)) largeChestTex = makeTexture(ci);
        if (loadImage(assets + "item/enderchest.png", ci)) enderChestTex = makeTexture(ci);
    }
    // Текстура стрелы в раскладке arrows.png 1.0 (32x32): бок 16x5 в (0,0), торец 5x5 в (0,5).
    // Файла в ассетах нет — рисуем: оперение (W/F), древко (b), наконечник (D/L)
    // Вагонетка и лодка 1.0 (item/cart.png, item/boat.png); без файлов — прежние коробки из блоков
    Image cartFile, boatFile;
    bool haveCart = loadImage(assets + "item/cart.png", cartFile), haveBoat = loadImage(assets + "item/boat.png", boatFile);
    GLuint cartTex = haveCart ? makeTexture(cartFile) : 0, boatTex = haveBoat ? makeTexture(boatFile) : 0;
    GLuint arrowTex;
    Image arrowFile;
    if (loadImage(assets + "item/arrows.png", arrowFile)) arrowTex = makeTexture(arrowFile);
    else {
        Image ai;
        ai.width = ai.height = 32;
        ai.rgba.assign(32 * 32 * 4, 0);
        const char* side[5] = {"WWF.........D...", ".WWF........LD..", "..bbbbbbbbbbLLLD", ".WWF........LD..", "WWF.........D..."};
        const char* back[5] = {"W...W", ".W.W.", "..b..", ".W.W.", "W...W"};
        auto put = [&](int x, int y, char ch) {
            glm::ivec3 c;
            switch (ch) {
            case 'W': c = {232, 232, 232}; break;
            case 'F': c = {178, 178, 178}; break;
            case 'b': c = {134, 101, 60}; break;
            case 'L': c = {150, 150, 150}; break;
            case 'D': c = {90, 90, 90}; break;
            default: return;
            }
            uint8_t* p = ai.at(x, y);
            p[0] = (uint8_t)c.r; p[1] = (uint8_t)c.g; p[2] = (uint8_t)c.b; p[3] = 255;
        };
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 16; ++x) put(x, y, side[y][x]);
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 5; ++x) put(x, 5 + y, back[y][x]);
        arrowTex = makeTexture(ai);
    }
    GLuint sunTex = makeTexture(sunImg);
    Image pumpkinBlurImg;
    GLuint pumpkinBlurTex = loadImage(assets + "misc/pumpkinblur.png", pumpkinBlurImg) ? makeTexture(pumpkinBlurImg) : 0;
    GLuint moonTex = makeTexture(moonImg);
    GLuint cloudsTex = makeTexture(cloudsImg, true);

    // Мобы
    GLuint mobTex[(int)MobType::COUNT];
    // Высота развёртки = ширина модели * пропорции файла: в 1.4.2 zombie.png и pigzombie.png стали 64x64 (было 64x32),
    // и со старой высотой 32 на модель ложилась растянутая вдвое текстура
    float mobTexAspect[(int)MobType::COUNT];
    {
        const char* names[(int)MobType::COUNT] = {
            "pig", "cow", "sheep", "chicken", "zombie", "skeleton", "spider", "creeper",
            "wolf", "squid", "slime", "enderman", "silverfish", "cavespider", "redcow",
            "snowman", "villager", "pigzombie", "ghast", "fire", "lava",
            "enderdragon/ender", "enderdragon/crystal",
            "skeleton_wither", "wither", "villager/witch", "bat", "villager_golem", "ozelot", "cat_black", "zombie_villager"
        };
        for (int i = 0; i < (int)MobType::COUNT; ++i) {
            Image img;
            load((std::string("mob/") + names[i] + ".png").c_str(), img);
            glm::vec2 ts = mobTexSize((MobType)i);
            mobTexAspect[i] = img.width > 1 ? (float)img.height / (float)img.width : ts.y / ts.x;
            mobTex[i] = makeTexture(img);
        }
    }
    Image furImg, eyesImg, charImg;
    load("mob/sheep_fur.png", furImg);
    load("mob/spider_eyes.png", eyesImg);
    load("mob/char.png", charImg);
    GLuint furTex = makeTexture(furImg), eyesTex = makeTexture(eyesImg), charTex = makeTexture(charImg);
    auto loadTex = [&](const char* rel) { Image im; load(rel, im); return makeTexture(im); };
    GLuint wolfTameTex = loadTex("mob/wolf_tame.png"), wolfAngryTex = loadTex("mob/wolf_angry.png");
    GLuint wolfCollarTex = loadTex("mob/wolf_collar.png");
    GLuint catRedTex = loadTex("mob/cat_red.png"), catSiameseTex = loadTex("mob/cat_siamese.png");
    GLuint witherInvulTex = loadTex("mob/wither_invul.png");
    Image witherArmorImg;
    GLuint witherArmorTex = loadImage(assets + "armor/witherarmor.png", witherArmorImg) ? makeTexture(witherArmorImg, true) : 0;
    GLuint ghastFireTex = loadTex("mob/ghast_fire.png"), enderEyesTex = loadTex("mob/enderman_eyes.png");
    GLuint saddleTex = loadTex("mob/saddle.png");
    Image powerImg;
    GLuint powerTex = loadImage(assets + "armor/power.png", powerImg) ? makeTexture(powerImg, true) : 0; // оболочка заряженного крипера
    GLuint dragonEyesTex = loadTex("mob/enderdragon/ender_eyes.png"), beamTex = loadTex("mob/enderdragon/beam.png");
    GLuint shuffleTex = loadTex("mob/enderdragon/shuffle.png"), explosionTex = loadTex("misc/explosion.png");
    glBindTexture(GL_TEXTURE_2D, beamTex); // луч ползёт по длине — нужен повтор
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    GLuint villagerTex[5] = {loadTex("mob/villager/farmer.png"), loadTex("mob/villager/librarian.png"), loadTex("mob/villager/priest.png"),
                             loadTex("mob/villager/smith.png"), loadTex("mob/villager/butcher.png")};

    GuiTextures guiTex2;
    guiTex2.gui = guiTex;
    guiTex2.inventory = makeTexture(inventoryImg);
    guiTex2.crafting = makeTexture(craftingImg);
    guiTex2.furnace = makeTexture(furnaceImg);
    guiTex2.container = makeTexture(containerImg);
    {
        Image trapImg, enchImg, alchImg, repairImg;
        load("gui/trap.png", trapImg);
        load("gui/enchant.png", enchImg);
        load("gui/alchemy.png", alchImg);
        load("gui/repair.png", repairImg);
        guiTex2.trap = makeTexture(trapImg);
        guiTex2.enchant = makeTexture(enchImg);
        guiTex2.alchemy = makeTexture(alchImg);
        guiTex2.repair = makeTexture(repairImg);
    }
    guiTex2.allitems = makeTexture(allitemsImg);
    guiTex2.creativeList = makeTexture(creativeListImg);
    guiTex2.creativeSearch = makeTexture(creativeSearchImg);
    guiTex2.creativeSurvival = makeTexture(creativeSurvivalImg);
    guiTex2.items = itemsTex;
    guiTex2.terrain = terrainUiTex;

    UI ui;
    ui.init(uiProg);
    ui.terrainTex = terrainUiTex;
    if (fontImg.width == 128) ui.setFont(fontTex, fontImg);

    ItemModels itemModels;
    itemModels.items = &itemsImg;
    itemModels.terrain = &terrainUiImg;
    // Броня на модели игрока (armor/<материал>_1.png — шлем, нагрудник, ботинки; _2.png — поножи)
    GLuint armorTex[5][2] = {};
    {
        static const char* MAT[5] = {"cloth", "chain", "iron", "diamond", "gold"};
        for (int m = 0; m < 5; ++m)
            for (int l = 0; l < 2; ++l) {
                Image ai;
                if (loadImage(assets + "armor/" + MAT[m] + "_" + std::to_string(l + 1) + ".png", ai)) armorTex[m][l] = makeTexture(ai);
            }
    }
    auto armorMaterial = [](uint16_t id) -> int {
        if (id >= LEATHER_HELMET && id <= LEATHER_BOOTS) return 0;
        if (id >= CHAIN_HELMET && id <= CHAIN_BOOTS) return 1; // кольчуга
        if (id >= IRON_HELMET && id <= IRON_BOOTS) return 2;
        if (id >= DIAMOND_HELMET && id <= DIAMOND_BOOTS) return 3;
        if (id >= GOLD_HELMET && id <= GOLD_BOOTS) return 4;
        return -1;
    };
    // Вид от третьего лица (F5): 0 — глаза, 1 — сзади, 2 — спереди; анимация модели игрока
    int camMode = 0;
    float plLimbSwing = 0.f, plLimbAmount = 0.f, plPrevLimbAmount = 0.f, plBodyYaw = 0.f, plPrevBodyYaw = 0.f;

    // ---- Звук
    Audio audio;
    audio.init(assets + "sounds");

    // ---- Буферы: рамка блока
    GLuint lineVao, lineVbo;
    {
        const float a = 0.f, b = 1.f;
        float c[8][3] = {{a, a, a}, {b, a, a}, {b, b, a}, {a, b, a}, {a, a, b}, {b, a, b}, {b, b, b}, {a, b, b}};
        int e[24] = {0, 1, 1, 2, 2, 3, 3, 0, 4, 5, 5, 6, 6, 7, 7, 4, 0, 4, 1, 5, 2, 6, 3, 7};
        std::vector<float> v;
        for (int i : e) v.insert(v.end(), {c[i][0], c[i][1], c[i][2]});
        glGenVertexArrays(1, &lineVao);
        glGenBuffers(1, &lineVbo);
        glBindVertexArray(lineVao);
        glBindBuffer(GL_ARRAY_BUFFER, lineVbo);
        glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    }

    // Динамический буфер формата Vertex (облака, частицы, рука, предметы)
    GLuint dynVao, dynVbo;
    glGenVertexArrays(1, &dynVao);
    glGenBuffers(1, &dynVbo);
    glBindVertexArray(dynVao);
    glBindBuffer(GL_ARRAY_BUFFER, dynVbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(8 * sizeof(float)));
    auto drawDynamic = [&](const std::vector<Vertex>& v) {
        if (v.empty()) return;
        glBindVertexArray(dynVao);
        glBindBuffer(GL_ARRAY_BUFFER, dynVbo);
        glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(Vertex), v.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)v.size());
    };

    // Динамический буфер для спрайтов (pos3 + uv2): солнце, луна, трещины
    GLuint spriteVao, spriteVbo;
    glGenVertexArrays(1, &spriteVao);
    glGenBuffers(1, &spriteVbo);
    glBindVertexArray(spriteVao);
    glBindBuffer(GL_ARRAY_BUFFER, spriteVbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    auto drawSprite = [&](const std::vector<float>& v) {
        glBindVertexArray(spriteVao);
        glBindBuffer(GL_ARRAY_BUFFER, spriteVbo);
        glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(v.size() / 5));
    };

    // Звёзды: 1500 точек на сфере, как в оригинале (статический буфер)
    GLuint starVao, starVbo;
    GLsizei starCount = 0;
    {
        std::mt19937 srng(10842);
        std::uniform_real_distribution<float> u(-1.f, 1.f), u01(0.f, 1.f);
        std::vector<float> v;
        for (int i = 0; i < 1500; ++i) {
            glm::vec3 d(u(srng), u(srng), u(srng));
            float len = glm::length(d);
            if (len < 0.01f || len > 1.f) continue;
            d /= len;
            float size = 0.15f + u01(srng) * 0.1f;
            glm::vec3 c = d * 100.f;
            glm::vec3 t1 = glm::normalize(glm::cross(d, std::abs(d.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
            glm::vec3 t2 = glm::cross(d, t1);
            float rot = u01(srng) * glm::two_pi<float>();
            glm::vec3 a1 = (t1 * std::cos(rot) + t2 * std::sin(rot)) * size;
            glm::vec3 a2 = (-t1 * std::sin(rot) + t2 * std::cos(rot)) * size;
            glm::vec3 p[4] = {c - a1 - a2, c + a1 - a2, c + a1 + a2, c - a1 + a2};
            for (int k : {0, 1, 2, 0, 2, 3}) v.insert(v.end(), {p[k].x, p[k].y, p[k].z, 0.5f, 0.5f});
        }
        starCount = (GLsizei)(v.size() / 5);
        glGenVertexArrays(1, &starVao);
        glGenBuffers(1, &starVbo);
        glBindVertexArray(starVao);
        glBindBuffer(GL_ARRAY_BUFFER, starVbo);
        glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    }

    // ---- Настройки и сохранения
    Options opt;
    const std::string optionsPath = saveDir + "options.txt";
    opt.load(optionsPath);
    SaveManager saves;
    saves.root = saveDir + "saves/";
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::create_directories(fs::u8path(saves.root), ec);
        // Мир прошлых версий (world.sav рядом с exe) переносим в saves/World
        fs::path oldWorld = fs::u8path(saveDir + "world.sav");
        if (fs::exists(oldWorld, ec) && saves.list().empty()) {
            WorldInfo wi;
            wi.folder = saves.uniqueFolder("World");
            wi.name = "World";
            wi.lastPlayed = nowSeconds();
            saves.writeInfo(wi);
            fs::rename(oldWorld, fs::u8path(saves.path(wi.folder) + "world.sav"), ec);
            fs::path oldEnt = fs::u8path(saveDir + "entities.sav");
            if (fs::exists(oldEnt, ec)) fs::rename(oldEnt, fs::u8path(saves.path(wi.folder) + "entities.sav"), ec);
        }
    }

    // ---- Меню
    MenuSystem menu;
    {
        static const char* splashes[] = {
            "Now in C++!", "Punch the trees!", "Beware of creepers!", "100% blocks!", "OpenGL 3.3!", "Ported by hand!",
            "Look behind you!", "Mind the lava!", "Nights are dark!", "Craft it yourself!", "Also try the original!",
            "Pixel perfect!", "Made on a laptop!", "Twenty ticks per second!", "Sixteen by sixteen!", "Dig straight down? No!",
            "Now with weather!", "Spiders climb walls!", "Also has pigs!", "Hello, world!",
        };
        std::mt19937 srng((uint32_t)std::chrono::high_resolution_clock::now().time_since_epoch().count());
        menu.splash = splashes[srng() % (sizeof(splashes) / sizeof(splashes[0]))];
    }
    menu.clickSound = [&]() { audio.play("random/click", 1.f, 1.f); };
    Image logoImg, bgImg;
    GLuint panoTex[6];
    for (int i = 0; i < 6; ++i) {
        Image pi;
        std::string p142 = "title/bg/panorama" + std::to_string(i) + ".png";
        std::string pOld = "bg/panorama" + std::to_string(i) + ".png";
        load(std::filesystem::exists(std::filesystem::u8path(assets + p142)) ? p142.c_str() : pOld.c_str(), pi);
        panoTex[i] = makeTexture(pi);
        glBindTexture(GL_TEXTURE_2D, panoTex[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    load("gui/crash_logo.png", logoImg);
    load("gui/background.png", bgImg);
    MenuTextures menuTex;
    menuTex.gui = guiTex;
    menuTex.logo = makeTexture(logoImg);
    menuTex.logoW = (float)logoImg.width;
    menuTex.logoH = (float)logoImg.height;
    menuTex.background = makeTexture(bgImg, true);
    // Панорама рисуется в маленькую текстуру 256x256 и растягивается — получается размытие, как в 1.0
    GLuint panoFbo = 0, panoFboTex = 0;
    {
        glGenTextures(1, &panoFboTex);
        glBindTexture(GL_TEXTURE_2D, panoFboTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1, &panoFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, panoFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, panoFboTex, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // ---- Мир (создаётся при входе из меню)
    bool inGame = false, loading = false;
    double loadingStart = 0.0;
    WorldInfo currentWorld;
    std::string savePath, entitiesPath;
    int dimension = 0;           // 0 — обычный мир, -1 — Незер, 1 — Край
    int portalTimer = 0, portalCooldown = 0;
    bool pendingPortal = false;  // после перехода найти или построить портал рядом
    bool pendingEnd = false;     // прибыли в Край: площадка, дракон и кристаллы
    bool pendingRespawn = false; // вернулись из Края: к кровати или на точку появления
    bool pendingSpawnCheck = false; // новый мир: сдвинуть точку появления на сушу, когда чанки загрузятся
    int creditsTime = -1;        // титры после портала выхода
    bool pendingCredits = false; // титры покажем, когда загрузится обычный мир
    std::string worldFolder;
    MobManager mobMgr;
    Player player;
    Inventory inv;
    World::SaveState st;
    uint32_t seed = 0;
    std::unique_ptr<World> world;
    int64_t worldTime = 0;
    // Погода (WorldInfo 1.0): таймеры дождя и грозы, плавная сила
    bool raining = false, thundering = false;
    int rainTime = 0, thunderTime = 0;
    float rainStrength = 0.f, prevRain = 0.f, thunderStrength = 0.f, prevThunder = 0.f;
    int lightningTicks = 0;
    glm::vec3 lightningPos(0.f);
    uint32_t lightningSeed = 1;
    glm::vec3 spawnPoint(0.f);
    uint32_t gameRng = 1; // ГСЧ для дропа и случайных тиков

    std::vector<Particle> particles;
    std::vector<ItemEntity> items;
    std::vector<ItemEntity> pickupFx; // подобранные предметы летят к игроку 3 тика (age — тики полёта, prev — откуда)
    ContainerScreen gui;

    struct Mining { glm::ivec3 pos{0}; float progress = 0; bool active = false; int ticks = 0; } mining;
    int breakDelay = 0, placeDelay = 0, useTicks = 0, bowCharge = 0;
    float bossFrac = -1.f; // здоровье дракона / визера в кадре (0..1), -1 — полоску не рисовать
    const char* bossName = "Boss health";
    // ---- Сетевая игра (клиент)
    net::Conn netConn;
    bool mp = false;
    uint32_t myId = 0;
    std::unordered_set<int64_t> mpPendingChunks; // запрошенные у сервера правки чанков (ещё не пришли)
    struct RemotePlayer {
        std::string name;
        PlayerNet net;                          // последнее присланное состояние
        glm::vec3 pos{0.f}, prevPos{0.f};       // сглаженная позиция
        float yaw = 0, pitch = 0, bodyYaw = 0, prevBodyYaw = 0;
        float limbSwing = 0, limbAmount = 0, prevLimbAmount = 0;
        float swing = 1.f, prevSwing = 1.f;
        bool has = false;
    };
    std::map<uint32_t, RemotePlayer> others;
    bool mpPvp = true;
    bool mpOp = false; // сервер разрешил этому игроку команды (читы)
    struct ListEntry { uint32_t id; std::string name; int ping; int dim; };
    std::vector<ListEntry> playerList; // список игроков по TAB (присылает сервер раз в секунду)
    // Автодополнение команд по TAB: варианты для текущего слова и строка, которую мы подставили
    std::vector<std::string> tabCands;
    size_t tabIdx = 0;
    std::string tabBase, tabLast;
    int mpSaveTimer = 0;
    // Открытый контейнер в сетевой игре: вид (0 блок, 1 вагонетка), ключ и последнее отправленное содержимое
    int mpOpenKind = -1;
    glm::ivec3 mpOpenPos(0);
    uint32_t mpOpenVehicle = 0;
    std::vector<ItemStack> mpOpenSent;
    std::map<uint32_t, TileEntity> mpCartChests;
    std::function<void()> savePlayerFn; // сохранение игрока на сервере (задаётся ниже)
    glm::mat4 lastVP(1.f);                      // для подписей имён над головами
    // Книги над столами зачарования (TileEntityEnchantmentTable): поворот к игроку, раскрытие, листание
    struct BookState {
        float rot = 0, prevRot = 0, rotTarget = 0;
        float spread = 0, prevSpread = 0;
        float flip = 0, prevFlip = 0, flipTarget = 0, flipVel = 0;
        int ticks = 0;
    };
    std::unordered_map<int64_t, BookState> books;
    // Крышки сундуков (TileEntityChest.lidAngle): ключ — половина с меньшей координатой; {прошлый, текущий} угол 0..1
    std::unordered_map<int64_t, std::pair<float, float>> chestLids;
    // Карты (номер = damage предмета): в одиночной игре лежат в папке мира maps/map_N.dat, в сетевой — только в памяти
    std::unordered_map<int, MapData> maps;
    std::string mapsWorld = "?"; // для какого мира загружены
    int nextMapId = -1;
    auto mapsDir = [&]() { return saves.path(worldFolder) + "maps/"; };
    auto mapFor = [&](int id) -> MapData* {
        std::string key = mp ? std::string("#mp") : worldFolder;
        if (key != mapsWorld) { maps.clear(); nextMapId = -1; mapsWorld = key; }
        auto it = maps.find(id);
        if (it != maps.end()) return &it->second;
        if (mp || worldFolder.empty()) return nullptr;
        MapData md;
        if (!loadMap(md, mapsDir() + "map_" + std::to_string(id) + ".dat")) return nullptr;
        return &(maps[id] = md);
    };
    auto newMapId = [&]() {
        if (nextMapId < 0) {
            // Следующий свободный номер: больше всех уже сохранённых
            nextMapId = 1;
            std::error_code ec;
            if (!mp && !worldFolder.empty())
                for (auto& e : std::filesystem::directory_iterator(std::filesystem::u8path(mapsDir()), ec)) {
                    std::string n = e.path().filename().u8string();
                    if (n.rfind("map_", 0) == 0) nextMapId = std::max(nextMapId, std::atoi(n.c_str() + 4) + 1);
                }
            for (auto& [id, md] : maps) nextMapId = std::max(nextMapId, id + 1);
        }
        return nextMapId++;
    };
    auto saveMaps = [&]() {
        if (mp || worldFolder.empty()) return;
        for (auto& [id, md] : maps) {
            if (!md.dirty) continue;
            std::error_code ec;
            std::filesystem::create_directories(std::filesystem::u8path(mapsDir()), ec);
            if (saveMap(md, mapsDir() + "map_" + std::to_string(id) + ".dat")) md.dirty = false;
        }
    };
    int64_t showcaseLidKey = INT64_MIN; // --showcase: эта крышка всегда открыта
    int showcaseHold = 0;        // первые тики витрины взгляд фиксирован (рывок курсора при фокусе окна)
    uint32_t ridingId = 0;       // транспорт, в котором сидит игрок (0 — пешком)
    uint32_t ridingPigId = 0;    // свинья под седлом, на которой сидит игрок
    bool bobber = false;         // заброшенная удочка (EntityFishHook)
    glm::vec3 bobPos(0.f), bobPrev(0.f), bobVel(0.f);
    int biteWindow = 0;
    uint32_t bobHookedId = 0;    // моб на крючке (0 — нет)
    // Чат (GuiChat): строка ввода, сообщения (текст, время появления), история отправленного
    std::string chatLine;
    std::vector<std::pair<std::string, double>> chatLog;
    std::vector<std::string> chatHistory;
    int chatHistPos = -1;
    bool bobInGround = false;    // крючок воткнулся в блок
    glm::ivec3 signEdit(0);      // редактируемая табличка
    int signLine = 0;
    bool sleeping = false;       // игрок лежит в кровати
    int sleepTimer = 0;
    glm::ivec3 sleepBed(0);
    std::unordered_map<int64_t, std::pair<glm::ivec3, int>> plateTimers; // нажатые плиты: позиция и тиков до отпускания
    float swing = 1.f, prevSwing = 1.f; // анимация руки: 0..1, 1 — покой
    auto startSwing = [&]() { if (swing >= 0.5f) swing = 0.f; };
    float nameTimer = 0.f;
    int lastSelected = g_in.selected;
    std::string infoText;
    float infoTimer = 0.f;
    int swimCooldown = 0;
    float fov = 70.f, bobAmp = 0.f, prevBobAmp = 0.f;
    float flicker = 1.f, flickerTarget = 1.f;
    float gamma = opt.gamma; // «Яркость»: 0 — Moody, 1 — Bright

    double lastTime = glfwGetTime(), titleTimer = 0, tickAcc = 0;
    int frames = 0;
    float fps = 0;
    // Размеры GUI с прошлого кадра (для закрытия окон из тика)
    float lastSc = 2.f, lastW = 640.f, lastH = 360.f;

    auto blockAt = [&](glm::vec3 p) {
        return world->getBlock((int)std::floor(p.x), (int)std::floor(p.y), (int)std::floor(p.z));
    };

    auto currentSave = [&]() {
        st.pos = player.pos;
        st.yaw = player.yaw;
        st.pitch = player.pitch;
        st.worldTime = worldTime;
        st.health = player.health;
        st.food = player.food;
        st.air = player.air;
        st.saturation = player.saturation;
        st.gameMode = (uint8_t)player.mode;
        st.spawn = spawnPoint;
        st.inventory = inv;
        st.raining = raining;
        st.thundering = thundering;
        st.rainTime = rainTime;
        st.thunderTime = thunderTime;
        return st;
    };

    auto showInfo = [&](const std::string& s) { infoText = s; infoTimer = 2.5f; };
    // Вспышка и гром молнии в точке (урон и огонь считает MobManager::strikeLightning — здесь или на сервере)
    auto showLightning = [&](const glm::vec3& at) {
        lightningPos = at;
        lightningTicks = 8;
        lightningSeed = (uint32_t)(rnd() * 1e9f) | 1u;
        audio.play("ambient/weather/thunder", 1.f, 0.8f + rnd() * 0.2f);
        audio.play("random/explode", 2.f, 0.5f + rnd() * 0.2f, &lightningPos);
    };
    // Читы: в одиночной игре — настройка мира, в сетевой — если сервер сделал игрока оператором
    auto cheatsAllowed = [&]() { return mp ? mpOp : currentWorld.cheats; };

    // Опыт как в 1.0: до следующего уровня нужно 7 + level*7/2 очков
    auto addXp = [&](int n) {
        player.xpTotal += n;
        auto cap = [&]() { return (float)(7 + ((player.xpLevel * 7) >> 1)); };
        player.xpProgress += n / cap();
        while (player.xpProgress >= 1.f) {
            player.xpProgress = (player.xpProgress - 1.f) * cap();
            ++player.xpLevel;
            player.xpProgress /= cap();
            if (player.xpLevel % 5 == 0) audio.play("random/levelup", 0.75f, 1.f);
        }
        audio.play("random/orb", 0.1f, 0.5f * ((rnd() - rnd()) * 0.7f + 1.8f));
    };

    // Достижения 1.0: бит, название, описание, значок, родитель (бит, -1 — корень), место на карте (клетки по 24 px), особая рамка
    struct AchDef { int bit; const char* name; const char* desc; uint16_t icon; int parent; int col, row; bool special; };
    static const AchDef ACH[] = {
        {0, "Taking Inventory", "Press 'E' to open your inventory.", BOOK, -1, 0, 0, false},
        {1, "Getting Wood", "Attack a tree until a block of wood pops out", LOG, 0, 2, 1, false},
        {2, "Benchmarking", "Craft a workbench with four blocks of planks", CRAFTING_TABLE, 1, 4, -1, false},
        {3, "Time to Mine!", "Use planks and sticks to make a pickaxe", WOOD_PICKAXE, 2, 4, 2, false},
        {4, "Hot Topic", "Construct a furnace out of eight cobblestone blocks", FURNACE, 3, 3, 4, false},
        {5, "Acquire Hardware", "Smelt an iron ingot", IRON_INGOT, 4, 1, 4, false},
        {6, "Time to Farm!", "Use planks and sticks to make a hoe", WOOD_HOE, 2, 2, -3, false},
        {7, "Bake Bread", "Turn wheat into bread", BREAD, 6, -1, -3, false},
        {8, "The Lie", "Wheat, sugar, milk and eggs!", CAKE_ITEM, 6, 0, -5, false},
        {9, "Getting an Upgrade", "Construct a better pickaxe", STONE_PICKAXE, 3, 6, 2, false},
        {10, "Delicious Fish", "Catch and cook fish!", COOKED_FISH, 4, 2, 6, false},
        {11, "On A Rail", "Travel by minecart at least 1 km from where you started", RAIL, 5, 2, 3, true},
        {12, "Time to Strike!", "Use planks and sticks to make a sword", WOOD_SWORD, 2, 6, -1, false},
        {13, "Monster Hunter", "Attack and destroy a monster", BONE, 12, 8, -1, false},
        {14, "Cow Tipper", "Harvest some leather", LEATHER, 12, 7, -3, false},
        {15, "When Pigs Fly", "Fly a pig off a cliff", SADDLE, 14, 8, -4, true},
        {16, "Sniper Duel", "Kill a skeleton with an arrow from more than 50 meters", BOW, 13, 7, 0, true},
        {17, "DIAMONDS!", "Acquire diamonds with your iron tools", DIAMOND, 5, -1, 5, false},
        {18, "We Need to Go Deeper", "Build a portal to the Nether", OBSIDIAN, 17, -1, 7, false},
        {19, "Return to Sender", "Destroy a Ghast with a fireball", GHAST_TEAR, 18, -4, 8, true},
        {20, "Into Fire", "Relieve a Blaze of its rod", BLAZE_ROD, 18, 0, 9, false},
        {21, "Local Brewery", "Brew a potion", POTION, 20, 2, 8, false},
        {22, "The End?", "Locate the End", EYE_OF_ENDER, 20, 3, 10, true},
        {23, "The End.", "Defeat the Ender Dragon", DRAGON_EGG, 22, 4, 13, true},
        {24, "Enchanter", "Use a book, obsidian and diamonds to construct an enchantment table", ENCHANT_TABLE, 17, -4, 4, false},
        {25, "Overkill", "Deal nine hearts of damage in a single hit", DIAMOND_SWORD, 24, -4, 1, true},
        {26, "Librarian", "Build some bookshelves to improve your enchantment table", BOOKSHELF, 24, -3, 6, false},
    };
    const int ACH_N = (int)(sizeof(ACH) / sizeof(ACH[0]));
    auto achIndex = [&](int bit) { for (int i = 0; i < ACH_N; ++i) if (ACH[i].bit == bit) return i; return -1; };
    auto achHas = [&](int bit) { return bit < 0 || ((currentWorld.achievements >> bit) & 1u) != 0; };
    float achMapX = -100.f, achMapY = -66.f;  // прокрутка (в начале «Taking Inventory» в центре); карты достижений (в пикселях GUI)
    bool achDragging = false;
    float achDragX = 0.f, achDragY = 0.f;
    std::vector<std::pair<int, double>> achToasts; // номер в ACH и время появления
    auto unlockAch = [&](int bit) {
        if (currentWorld.achievements & (1u << bit)) return;
        int ai = achIndex(bit);
        if (ai >= 0 && !achHas(ACH[ai].parent)) return; // как в 1.0: сначала нужно предыдущее по дереву
        currentWorld.achievements |= 1u << bit;
        if (ai >= 0) achToasts.push_back({ai, glfwGetTime()});
        audio.play("random/levelup", 0.6f, 1.2f);
    };

    MobHooks mobHooks;
    mobHooks.sound = [&](const std::string& g, float v, float pt, const glm::vec3* pos) { audio.play(g, v, pt, pos); };
    mobHooks.addXp = addXp;
    mobHooks.achievement = [&](int bit) { unlockAch(bit); };
    mobHooks.giveItem = [&](ItemStack& s) { return inv.add(s); };
    mobHooks.onKill = [&](MobType t) { if (countsAsMonster(t)) unlockAch(13); };

    auto throwItem = [&](const ItemStack& s) {
        if (mp) { // сетевая игра: предмет появляется на сервере
            if (s.empty()) return;
            net::Writer w;
            glm::vec3 e = player.eye(), l = player.look();
            w.f32(e.x); w.f32(e.y); w.f32(e.z); w.f32(l.x); w.f32(l.y); w.f32(l.z);
            writeItem(w, s);
            netConn.send(C_DROP, w);
            return;
        }
        throwFromPlayer(items, player.eye(), player.look(), s, gameRng);
    };

    glm::ivec3 anvilPos(0); // наковальня, чьё окно открыто
    auto makeCtx = [&](float mxG, float myG) {
        GuiContext c{ui, guiTex2, lastSc, lastW, lastH, mxG, myG, inv, throwItem};
        c.xpLevel = &player.xpLevel;
        c.creative = player.creative();
        c.effects = &player.effects;
        c.onAnvilUse = [&]() {
            // BlockAnvil 1.4.2: после работы с шансом 12% изнашивается (целая → повреждённая → сильно → ломается)
            glm::vec3 sp = glm::vec3(anvilPos) + 0.5f;
            if (world && world->getBlock(anvilPos.x, anvilPos.y, anvilPos.z) == ANVIL && !player.creative() && rnd() < 0.12f) {
                uint8_t m = world->getMeta(anvilPos.x, anvilPos.y, anvilPos.z);
                int dmg = ((m >> 2) & 3) + 1;
                if (dmg > 2) {
                    world->setBlock(anvilPos.x, anvilPos.y, anvilPos.z, AIR);
                    audio.play("random/anvil_break", 1.f, rnd() * 0.1f + 0.9f, &sp); // окно закроется в основном цикле
                    return;
                }
                world->setBlock(anvilPos.x, anvilPos.y, anvilPos.z, ANVIL, (uint8_t)((m & 3) | (dmg << 2)));
            }
            audio.play("random/anvil_use", 1.f, rnd() * 0.1f + 0.9f, &sp);
        };
        return c;
    };

    auto openGui = [&](GuiKind k, TileEntity* te, TileEntity* te2 = nullptr) {
        gui.open(k, te, te2);
        openScreen(win, Screen::Container);
        mining.active = false;
        if (k == GuiKind::Chest) audio.play("random/chestopen", 0.5f, rnd() * 0.1f + 0.9f);
    };

    auto closeGui = [&]() {
        if (mp && mpOpenKind >= 0) { netConn.send(C_CLOSE); mpOpenKind = -1; }
        GuiContext ctx = makeCtx(0, 0);
        if (gui.kind == GuiKind::Chest) audio.play("random/chestclosed", 0.5f, rnd() * 0.1f + 0.9f);
        gui.close(ctx);
        openScreen(win, Screen::Playing);
    };

    // Износ предмета в руке; сломался — звук и пустой слот
    auto damageHeld = [&](int amount) {
        ItemStack& h = inv.slots[g_in.selected];
        if (player.creative() || maxDamage(h.id) == 0) return;
        int ub = h.enchLevel(ENCH_UNBREAKING);
        if (ub > 0 && rnd() * (ub + 1) >= 1.f) return; // шанс 1/(N+1) потратить прочность
        h.damage = (uint16_t)(h.damage + amount);
        if (h.damage >= maxDamage(h.id)) {
            h.clear();
            audio.play("random/break", 0.8f, 0.8f + rnd() * 0.4f);
        }
    };
    auto consumeHeld = [&]() {
        ItemStack& h = inv.slots[g_in.selected];
        if (player.creative()) return;
        if (--h.count == 0) h.clear();
    };

    auto dropAll = [&]() {
        if (mp) { // сетевая игра: всё выпадает на сервере
            for (auto& s : inv.slots) { if (!s.empty()) { net::Writer w; glm::vec3 e = player.pos + glm::vec3(0, 1.2f, 0);
                glm::vec3 l(rnd() - 0.5f, 0.3f, rnd() - 0.5f);
                w.f32(e.x); w.f32(e.y); w.f32(e.z); w.f32(l.x); w.f32(l.y); w.f32(l.z); writeItem(w, s); netConn.send(C_DROP, w); } s.clear(); }
            for (auto& s : inv.armor) { if (!s.empty()) { net::Writer w; glm::vec3 e = player.pos + glm::vec3(0, 1.2f, 0);
                glm::vec3 l(rnd() - 0.5f, 0.3f, rnd() - 0.5f);
                w.f32(e.x); w.f32(e.y); w.f32(e.z); w.f32(l.x); w.f32(l.y); w.f32(l.z); writeItem(w, s); netConn.send(C_DROP, w); } s.clear(); }
            return;
        }
        glm::ivec3 p((int)std::floor(player.pos.x), (int)std::floor(player.pos.y), (int)std::floor(player.pos.z));
        for (auto& s : inv.slots) { dropFromBlock(items, p, s, gameRng); s.clear(); }
        for (auto& s : inv.armor) { dropFromBlock(items, p, s, gameRng); s.clear(); }
        for (auto& it : items) it.motion += glm::vec3(rnd() - 0.5f, 0.2f, rnd() - 0.5f) * 0.4f;
    };

    // Нотный блок: инструмент по блоку снизу, высота по мете (0..24), нота-частица
    auto playNote = [&](const glm::ivec3& p) {
        if (world->getBlock(p.x, p.y, p.z) != NOTE_BLOCK || world->getBlock(p.x, p.y + 1, p.z) != AIR) return;
        int note = world->getMeta(p.x, p.y, p.z) % 25;
        uint8_t below = world->getBlock(p.x, p.y - 1, p.z);
        Sound mat = blockInfo(below).sound;
        const char* inst = "note/harp";
        if (mat == Sound::Glass || below == GLASS) inst = "note/hat";
        else if (mat == Sound::Sand || mat == Sound::Gravel) inst = "note/snare";
        else if (mat == Sound::Wood) inst = "note/bassattack";
        else if (mat == Sound::Stone && isOpaque(below)) inst = "note/bd";
        glm::vec3 sp = glm::vec3(p) + 0.5f;
        audio.play(inst, 3.f, std::pow(2.f, (note - 12) / 12.f), &sp);
        spawnNote(particles, glm::vec3(p) + glm::vec3(0.5f, 1.2f, 0.5f), note);
    };

    // Переключить рычаг / нажать кнопку и пересчитать редстоун вокруг
    auto useRedstoneSource = [&](const glm::ivec3& p) -> bool {
        uint8_t b = world->getBlock(p.x, p.y, p.z);
        uint8_t m = world->getMeta(p.x, p.y, p.z);
        glm::vec3 sp = glm::vec3(p) + 0.5f;
        if (b == LEVER) {
            m ^= 8;
            world->setMeta(p.x, p.y, p.z, m);
            audio.play("random/click", 0.3f, (m & 8) ? 0.6f : 0.5f, &sp);
        } else if (b == STONE_BUTTON) {
            if (m & 8) return true;
            m |= 8;
            world->setMeta(p.x, p.y, p.z, m);
            world->scheduleUpdate(p.x, p.y, p.z, 20);
            audio.play("random/click", 0.3f, 0.6f, &sp);
        } else {
            return false;
        }
        static const int D6[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        int s = supportDir(b, m);
        world->redstoneChanged(p.x, p.y, p.z);
        world->redstoneChanged(p.x + D6[s][0], p.y + D6[s][1], p.z + D6[s][2]);
        return true;
    };

    // Свободное место рядом с кроватью, чтобы встать (или на ней самой)
    auto standNearBed = [&](const glm::ivec3& bed) -> glm::vec3 {
        for (int r = 1; r <= 2; ++r)
            for (int dz = -r; dz <= r; ++dz)
                for (int dx = -r; dx <= r; ++dx) {
                    glm::ivec3 q = bed + glm::ivec3(dx, 0, dz);
                    if (world->getBlock(q.x, q.y, q.z) == BED) continue;
                    if (isSolid(world->getBlock(q.x, q.y - 1, q.z)) && !isSolid(world->getBlock(q.x, q.y, q.z)) &&
                        !isSolid(world->getBlock(q.x, q.y + 1, q.z)))
                        return glm::vec3(q) + glm::vec3(0.5f, 0.f, 0.5f);
                }
        return glm::vec3(bed) + glm::vec3(0.5f, 0.5625f, 0.5f);
    };
    auto respawnPos = [&]() -> glm::vec3 {
        if (world->hasBedSpawn) {
            glm::ivec3 b = world->bedSpawn;
            if (world->getBlock(b.x, b.y, b.z) == BED) return standNearBed(b);
            showInfo("Your home bed was missing or obstructed");
            world->hasBedSpawn = false;
        }
        glm::vec3 p = world->safeSpawnNear(spawnPoint, 16, true);
        for (int i = 0; i < 64 && p.y < CH && collides(*world, p); ++i) p.y += 1.f; // не в стене, если на спавне что-то построили
        return p;
    };
    auto wakeUp = [&]() {
        sleeping = false;
        sleepTimer = 0;
        player.pos = player.prevPos = standNearBed(sleepBed);
        player.eyeOffset = player.prevEyeOffset = 0.f;
        if (g_in.screen == Screen::Sleep) openScreen(win, Screen::Playing);
    };

    // Сломать блок: звук, частицы, содержимое сундука/печи, дроп и износ инструмента
    auto breakBlock = [&](const glm::ivec3& p) {
        uint8_t b = world->getBlock(p.x, p.y, p.z);
        uint8_t meta = world->getMeta(p.x, p.y, p.z);
        audio.playDig(b, glm::vec3(p) + 0.5f);
        spawnBreakParticles(particles, p, b, meta);
        if (b == JUKEBOX && meta > 0) audio.stopRecord();
        const ItemStack heldNow = inv.slots[g_in.selected];
        if (mp) {
            // Сетевая игра: дроп и последствия считает сервер; блок убираем сразу, не дожидаясь ответа
            net::Writer w;
            w.i32(p.x); w.u8((uint8_t)p.y); w.i32(p.z);
            w.u16(heldNow.id); w.u16(heldNow.damage);
            for (uint16_t e : heldNow.ench) w.u16(e);
            w.u8(player.creative() ? 1 : 0);
            netConn.send(C_DIG, w);
            world->applyRemote(p.x, p.y, p.z, AIR, 0);
        } else {
            harvestBlock(*world, mobMgr, items, p, heldNow, player.creative(), dimension, gameRng);
        }
        if (!player.creative()) {
            if (blockInfo(b).hardness > 0.f) damageHeld(toolInfo(heldNow.id).type == Tool::Sword ? 2 : 1);
            addExhaustion(player, 0.025f);
        }
    };

    // ================================================================ Один тик (1/20 секунды)
    std::function<void(int)> switchDimensionFn; // задаётся ниже (переход через портал)
    auto tick = [&]() {
        ++g_in.tick;
        const bool playing = g_in.screen == Screen::Playing;
        ++worldTime;

        // ---- Погода: дождь идёт 0.5–1 день, перерыв 0.5–7.5 дней; гроза — отдельный таймер
        if (g_in.toggleWeather && (mp || !cheatsAllowed())) {
            g_in.toggleWeather = false;
            showInfo(mp ? "Weather is controlled by the server" : "Cheats are not enabled on this world");
        }
        if (g_in.toggleWeather) {
            raining = !raining;
            thundering = raining && !thundering ? false : thundering;
            rainTime = raining ? 12000 : 12000 + (int)(rnd() * 168000);
            showInfo(raining ? "Weather: rain" : "Weather: clear");
            g_in.toggleWeather = false;
        }
        if (!mp) { // в сетевой игре погоду присылает сервер
            if (thunderTime <= 0) thunderTime = thundering ? 3600 + (int)(rnd() * 12000) : 12000 + (int)(rnd() * 168000);
            else if (--thunderTime <= 0) thundering = !thundering;
            if (rainTime <= 0) rainTime = raining ? 12000 + (int)(rnd() * 12000) : 12000 + (int)(rnd() * 168000);
            else if (--rainTime <= 0) raining = !raining;
        }
        prevRain = rainStrength;
        prevThunder = thunderStrength;
        rainStrength = std::clamp(rainStrength + (raining ? 0.01f : -0.01f), 0.f, 1.f);
        thunderStrength = std::clamp(thunderStrength + (raining && thundering ? 0.01f : -0.01f), 0.f, 1.f);
        if (lightningTicks > 0) --lightningTicks;
        if (dimension != 0) { rainStrength = prevRain = thunderStrength = prevThunder = 0.f; }
        mobMgr.raining = rainStrength > 0.5f;
        mobMgr.playerPumpkin = inv.armor[0].id == PUMPKIN;
        world->raining = rainStrength > 0.5f;
        if (showcaseHold > 0) {
            --showcaseHold;
            const char* sy = std::getenv("MC_SHOW_YAW");
            player.yaw = sy ? (float)std::atof(sy) : 0.f;
            { const char* sp = std::getenv("MC_SHOW_PITCH"); player.pitch = sp ? (float)std::atof(sp) : -35.f; }
        }

        // Сон: 100 тиков — и наступает утро, погода проясняется (как в 1.0)
        if (sleeping) {
            ++sleepTimer;
            player.motion = glm::vec3(0.f);
            player.prevPos = player.pos;
            if (world->getBlock(sleepBed.x, sleepBed.y, sleepBed.z) != BED) wakeUp();
            else if (mp) {
                // Сетевая игра: утро наступает, когда спят все — время пришлёт сервер
                if (sleepTimer >= 100 && worldTime % 24000 < 1000) wakeUp();
            } else if (sleepTimer >= 100) {
                worldTime += 24000 - (worldTime % 24000);
                raining = thundering = false;
                rainTime = 0;
                wakeUp();
            }
        }

        // Нажимные плиты: игрок и мобы (деревянные — ещё и предметы); отпускаются через 20 тиков
        {
            std::vector<glm::ivec3> on;
            auto probe = [&](const glm::vec3& feet, bool heavy) {
                glm::ivec3 c((int)std::floor(feet.x), (int)std::floor(feet.y + 0.01f), (int)std::floor(feet.z));
                uint8_t pb = world->getBlock(c.x, c.y, c.z);
                if (pb == WOOD_PLATE || (pb == STONE_PLATE && heavy)) on.push_back(c);
            };
            if (!player.dead && !sleeping) probe(player.pos, true);
            for (auto& m : mobMgr.mobs)
                if (!m.dying()) probe(m.pos, true);
            for (auto& it : items) probe(it.pos, false);
            for (auto& c : on) {
                int64_t k = posKey(c.x, c.y, c.z);
                auto& e = plateTimers[k];
                e.first = c;
                e.second = 20;
                if (world->getMeta(c.x, c.y, c.z) == 0) {
                    world->setMeta(c.x, c.y, c.z, 1);
                    world->redstoneChanged(c.x, c.y, c.z);
                    world->redstoneChanged(c.x, c.y - 1, c.z);
                    glm::vec3 sp = glm::vec3(c) + 0.5f;
                    audio.play("random/click", 0.3f, 0.6f, &sp);
                }
            }
            for (auto it = plateTimers.begin(); it != plateTimers.end();) {
                glm::ivec3 c = it->second.first;
                uint8_t pb = world->getBlock(c.x, c.y, c.z);
                if (pb != STONE_PLATE && pb != WOOD_PLATE) { it = plateTimers.erase(it); continue; }
                if (--it->second.second <= 0) {
                    world->setMeta(c.x, c.y, c.z, 0);
                    world->redstoneChanged(c.x, c.y, c.z);
                    world->redstoneChanged(c.x, c.y - 1, c.z);
                    glm::vec3 sp = glm::vec3(c) + 0.5f;
                    audio.play("random/click", 0.3f, 0.5f, &sp);
                    it = plateTimers.erase(it);
                } else {
                    ++it;
                }
            }
        }

        // Портал: постоять 4 секунды (в творческом — сразу) — и переход в другое измерение
        if (portalCooldown > 0) --portalCooldown;
        {
            glm::vec3 e = player.eye();
            bool inPortal = !player.dead &&
                            (world->getBlock((int)std::floor(player.pos.x), (int)std::floor(player.pos.y + 0.2f), (int)std::floor(player.pos.z)) == PORTAL ||
                             world->getBlock((int)std::floor(e.x), (int)std::floor(e.y), (int)std::floor(e.z)) == PORTAL);
            if (inPortal && portalCooldown == 0) { // из Края портал ведёт в Незер, как в 1.0
                if (portalTimer == 0) audio.play("portal/trigger", 1.f, rnd() * 0.4f + 0.8f);
                portalTimer += player.creative() ? 80 : 1;
                if (portalTimer >= 80) {
                    audio.play("portal/travel", 1.f, rnd() * 0.4f + 0.8f);
                    switchDimensionFn(dimension == -1 ? 0 : -1);
                    return;
                }
            } else if (inPortal) {
                portalCooldown = 100; // стоим в портале после перехода — ждём, пока выйдут
            } else {
                portalTimer = std::max(0, portalTimer - 4);
            }
        }

        // Портал Края: мгновенный переход; из Края — титры и домой
        if (creditsTime < 0 && !player.dead && portalCooldown == 0 &&
            world->getBlock((int)std::floor(player.pos.x), (int)std::floor(player.pos.y + 0.1f), (int)std::floor(player.pos.z)) == END_PORTAL) {
            if (dimension == 0) {
                audio.play("portal/travel", 1.f, 1.f);
                switchDimensionFn(1);
                return;
            }
            if (dimension == 1) {
                audio.play("portal/travel", 1.f, 1.f);
                pendingCredits = true;
                switchDimensionFn(0);
                return;
            }
        }
        if (mobMgr.dragonKilled) {
            mobMgr.dragonKilled = false;
            world->dragonDefeated = true;
            // Портал выхода: бедроковая чаша с порталом, столб с факелами и яйцо дракона
            int top = 64;
            for (int y = CH - 2; y > 1; --y)
                if (world->getBlock(0, y, 0) == END_STONE) { top = y; break; }
            int y0 = top + 1;
            for (int dx = -4; dx <= 4; ++dx)
                for (int dz = -4; dz <= 4; ++dz) {
                    int d2 = dx * dx + dz * dz;
                    if (d2 > 16) continue;
                    world->setBlock(dx, y0 - 1, dz, BEDROCK);
                    for (int dy = 0; dy <= 4; ++dy) world->setBlock(dx, y0 + dy, dz, AIR);
                    if (d2 >= 9) world->setBlock(dx, y0, dz, BEDROCK);
                    else if (d2 > 0) world->setBlock(dx, y0, dz, END_PORTAL);
                }
            for (int dy = 0; dy < 4; ++dy) world->setBlock(0, y0 + dy, 0, BEDROCK);
            world->setBlock(1, y0 + 2, 0, TORCH, TORCH_WEST_WALL);
            world->setBlock(-1, y0 + 2, 0, TORCH, TORCH_EAST_WALL);
            world->setBlock(0, y0 + 2, 1, TORCH, TORCH_NORTH_WALL);
            world->setBlock(0, y0 + 2, -1, TORCH, TORCH_SOUTH_WALL);
            world->setBlock(0, y0 + 4, 0, DRAGON_EGG);
        }

        // Достижения по предметам в инвентаре
        if (g_in.tick % 10 == 0)
            for (const ItemStack& s : inv.slots) {
                if (s.empty()) continue;
                switch (s.id) {
                case LOG: unlockAch(1); break;
                case CRAFTING_TABLE: unlockAch(2); break;
                case WOOD_PICKAXE: unlockAch(3); break;
                case FURNACE: unlockAch(4); break;
                case IRON_INGOT: unlockAch(5); break;
                case WOOD_HOE: case STONE_HOE: case IRON_HOE: case DIAMOND_HOE: case GOLD_HOE: unlockAch(6); break;
                case BREAD: unlockAch(7); break;
                case CAKE_ITEM: unlockAch(8); break;
                case STONE_PICKAXE: unlockAch(9); break;
                case COOKED_FISH: unlockAch(10); break;
                case WOOD_SWORD: case STONE_SWORD: case IRON_SWORD: case DIAMOND_SWORD: case GOLD_SWORD: unlockAch(12); break;
                case LEATHER: unlockAch(14); break;
                case DIAMOND: unlockAch(17); break;
                case BLAZE_ROD: unlockAch(20); break;
                case POTION: if (potionEffect(s.damage) != EFF_NONE) unlockAch(21); break;
                case ENCHANT_TABLE: unlockAch(24); break;
                case BOOKSHELF: unlockAch(26); break;
                default: break;
                }
            }
        if (dimension == -1) unlockAch(18);
        if (dimension == 1) unlockAch(22);
        if (world->dragonDefeated) unlockAch(23);

        // Огонь поджёг динамит
        for (auto& [tp, fromExplosion] : world->ignitedTnt) {
            mobMgr.igniteTnt(tp, fromExplosion, gameRng);
            glm::vec3 sp = glm::vec3(tp) + 0.5f;
            audio.play("random/fuse", 1.f, 1.f, &sp);
        }
        world->ignitedTnt.clear();

        // Анимация пламени: два тайла огня в атласе мира
        fireA.step();
        fireB.step();
        glBindTexture(GL_TEXTURE_2D, terrainTex);
        uploadAnimTile(15, 1, fireA.px);
        uploadAnimTile(15, 2, fireB.px);
        // Вода и лава: неподвижная — один тайл, текущая — квадрат 2x2 справа от неё
        for (LiquidFX* fx : {&waterFx, &waterFlowFx, &lavaFx, &lavaFlowFx}) fx->step();
        uploadAnimTile(13, 12, waterFx.px);
        uploadAnimTile(13, 14, lavaFx.px);
        for (int k = 0; k < 4; ++k) {
            uploadAnimTile(14 + (k & 1), 12 + (k >> 1), waterFlowFx.px);
            uploadAnimTile(14 + (k & 1), 14 + (k >> 1), lavaFlowFx.px);
        }
        portalFx.step(g_in.tick * 0.05f);
        {
            // Компас (TextureCompassFX 1.0): стрелка к точке появления с «инерцией»; в Незере и Крае крутится как попало.
            // Угол в системе 1.0: (yaw_mc - 90°) - atan2(dz, dx), где yaw_mc = наш yaw - 90°
            float target;
            if (dimension == 0) {
                float dx = spawnPoint.x - player.pos.x, dz = spawnPoint.z - player.pos.z;
                target = glm::radians(player.yaw - 180.f) - std::atan2(dz, dx);
            } else {
                target = rnd() * glm::two_pi<float>();
            }
            float diff = target - compassAngle;
            while (diff < -glm::pi<float>()) diff += glm::two_pi<float>();
            while (diff >= glm::pi<float>()) diff -= glm::two_pi<float>();
            diff = std::clamp(diff, -1.f, 1.f);
            compassVel += diff * 0.1f;
            compassVel *= 0.8f;
            compassAngle += compassVel;
            std::vector<uint8_t> px = compassBase;
            float s = std::sin(compassAngle), c = std::cos(compassAngle);
            auto put = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
                if (x < 0 || y < 0 || x > 15 || y > 15) return;
                uint8_t* p = &px[(y * 16 + x) * 4];
                p[0] = r; p[1] = g; p[2] = b; p[3] = 255;
            };
            for (int i = -4; i <= 4; ++i) put((int)(8.5f + c * i * 0.3f), (int)(7.5f - s * i * 0.3f * 0.5f), 100, 100, 100);
            for (int i = -8; i <= 16; ++i) {
                int x = (int)(8.5f + s * i * 0.3f), y = (int)(7.5f + c * i * 0.3f * 0.5f);
                if (i >= 0) put(x, y, 255, 20, 20);
                else put(x, y, 100, 100, 100);
            }
            glBindTexture(GL_TEXTURE_2D, itemsTex);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 6 * 16, 3 * 16, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, px.data());

            // Часы (TextureWatchFX 1.0): в пиксели-метки иконки (красный = синий, зелёный = 0) подставляется
            // повёрнутый по времени суток циферблат dial.png, с яркостью метки
            float targetC = dimension == 0 ? computeSky(worldTime, 0.f, player.look()).celestial * glm::two_pi<float>() : rnd() * glm::two_pi<float>();
            float dc = targetC - clockAngle;
            while (dc < -glm::pi<float>()) dc += glm::two_pi<float>();
            while (dc >= glm::pi<float>()) dc -= glm::two_pi<float>();
            dc = std::clamp(dc, -1.f, 1.f);
            clockVel += dc * 0.1f;
            clockVel *= 0.8f;
            clockAngle += clockVel;
            std::vector<uint8_t> cp = clockBase;
            if (haveDial) {
                float cs = std::cos(clockAngle), sn = std::sin(clockAngle);
                for (int i = 0; i < 256; ++i) {
                    uint8_t* p = &cp[i * 4];
                    if (!(p[0] == p[2] && p[1] == 0 && p[2] > 0)) continue;
                    float u = -((i % 16) / 15.f - 0.5f), v = (i / 16) / 15.f - 0.5f;
                    int k = p[0];
                    int dx = (int)((u * cs + v * sn + 0.5f) * 16.f), dy = (int)((v * cs - u * sn + 0.5f) * 16.f);
                    const uint8_t* d = dialImg.at(dx & 15, dy & 15);
                    p[0] = (uint8_t)(d[0] * k / 255);
                    p[1] = (uint8_t)(d[1] * k / 255);
                    p[2] = (uint8_t)(d[2] * k / 255);
                    p[3] = d[3];
                }
            }
            glTexSubImage2D(GL_TEXTURE_2D, 0, 6 * 16, 4 * 16, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, cp.data());
            glBindTexture(GL_TEXTURE_2D, terrainTex);
        }
        {
            static uint8_t stars[16 * 16 * 4];
            for (int i = 0; i < 256; ++i) {
                int x = i % 16, y = i / 16;
                uint32_t h = (uint32_t)(x * 73856093) ^ (uint32_t)(((y + g_in.tick / 4) % 16) * 19349663);
                h = h * 2654435761u;
                bool star = (h >> 24) < 14;
                float tw = 0.6f + 0.4f * std::sin(g_in.tick * 0.2f + x + y);
                uint8_t* s = &stars[i * 4];
                s[0] = (uint8_t)(star ? 120 * tw + 60 : 8 + (h >> 28));
                s[1] = (uint8_t)(star ? 200 * tw + 40 : 12 + (h >> 28));
                s[2] = (uint8_t)(star ? 180 * tw + 70 : 22 + (h >> 27));
                s[3] = 255;
            }
            uploadAnimTile(8, 11, stars);
        }
        uploadAnimTile(14, 0, portalFx.px);

        // Молния в грозу: удар рядом с игроком, гром и урон вокруг
        if (!mp && thunderStrength > 0.9f && rnd() < 1.f / 300.f) { // в сетевой игре молнию присылает сервер
            float ang = rnd() * glm::two_pi<float>(), dist = 8.f + rnd() * 56.f;
            int lx = (int)std::floor(player.pos.x + std::cos(ang) * dist), lz = (int)std::floor(player.pos.z + std::sin(ang) * dist);
            if (!biomeInfo(world->loadedBiome(lx, lz)).dry && !biomeInfo(world->loadedBiome(lx, lz)).snowy) {
                showLightning(glm::vec3(lx + 0.5f, (float)world->topBlockY(lx, lz), lz + 0.5f));
                TickEvents lev;
                mobMgr.strikeLightning(*world, lightningPos, player, lev, mobHooks, gameRng);
                if (lev.damage > 0) audio.play("damage/hit", 1.f, 1.f);
            }
        }

        // Брызги дождя на земле вокруг игрока: 100 * сила² попыток за тик
        if (rainStrength > 0.f) {
            int n = (int)(100.f * rainStrength * rainStrength * (opt.particles == 0 ? 1.f : opt.particles == 1 ? 0.5f : 0.f));
            for (int i = 0; i < n; ++i) {
                int rx = (int)std::floor(player.pos.x) + (int)(rnd() * 21) - 10, rz = (int)std::floor(player.pos.z) + (int)(rnd() * 21) - 10;
                const BiomeInfo& rb = biomeInfo(world->loadedBiome(rx, rz));
                if (rb.dry || rb.snowy) continue;
                int ty = world->topBlockY(rx, rz);
                if (ty > player.pos.y + 10 || ty < player.pos.y - 10) continue;
                uint8_t top = world->getBlock(rx, ty - 1, rz);
                if (top == AIR || top == LAVA) continue;
                spawnSplash(particles, glm::vec3(rx + rnd(), (float)ty + 0.1f, rz + rnd()));
            }
        }

        // Шум дождя: капли по земле вокруг игрока
        if (rainStrength > 0.2f && g_in.tick % 3 == 0) {
            int rx = (int)std::floor(player.pos.x) + (int)(rnd() * 21) - 10, rz = (int)std::floor(player.pos.z) + (int)(rnd() * 21) - 10;
            const BiomeInfo& rb = biomeInfo(world->loadedBiome(rx, rz));
            if (!rb.dry && !rb.snowy) {
                glm::vec3 rp(rx + 0.5f, (float)world->topBlockY(rx, rz), rz + 0.5f);
                if (rp.y <= player.pos.y + 10.f)
                    audio.play("ambient/weather/rain", 0.2f * rainStrength, rp.y > player.pos.y + 1.f ? 0.5f : 1.f, &rp);
            }
        }
        if (static bool burn = std::getenv("MC_SHOW_BURN") != nullptr; burn) { // dev: всё горит (проверка пламени)
            for (auto& m : mobMgr.mobs) m.fireTicks = std::max(m.fireTicks, 40);
            player.fireTicks = std::max(player.fireTicks, 40);
        }
        // Дождь гасит горящего игрока под открытым небом
        if (rainStrength > 0.5f && player.fireTicks > 0) {
            glm::ivec3 e((int)std::floor(player.pos.x), (int)std::floor(player.pos.y + 1.6f), (int)std::floor(player.pos.z));
            if (world->getSkyLight(e.x, e.y, e.z) == 15 && !biomeInfo(world->loadedBiome(e.x, e.z)).dry) player.fireTicks = 0;
        }

        // Мерцание света факелов
        flickerTarget += (rnd() - rnd()) * rnd() * rnd() * 0.1f;
        flickerTarget *= 0.9f;
        flicker = 1.f + flickerTarget * 1.5f;

        int pcx = floorDiv((int)std::floor(player.pos.x), CW), pcz = floorDiv((int)std::floor(player.pos.z), CW);
        Chunk* here = world->chunkAt(pcx, pcz);
        bool ready = here && here->meshed;

        // ---- Ввод движения
        MoveInput in;
        if (playing) {
            auto key = [&](int k) { return glfwGetKey(win, k) == GLFW_PRESS; };
            if (key(g_keys[KB_FORWARD])) in.forward += 1.f;
            if (key(g_keys[KB_BACK])) in.forward -= 1.f;
            if (key(g_keys[KB_RIGHT])) in.strafe += 1.f;
            if (key(g_keys[KB_LEFT])) in.strafe -= 1.f;
            in.jump = key(g_keys[KB_JUMP]);
            in.sneak = key(g_keys[KB_SNEAK]);
            in.sprintRequest = g_in.sprintTap || key(GLFW_KEY_LEFT_CONTROL);
        }
        g_in.sprintTap = false;
        if (g_in.flyTap) {
            if (player.creative()) { player.fly = !player.fly; player.motion.y = 0; }
            g_in.flyTap = false;
        }
        if (g_in.toggleMode && !cheatsAllowed()) {
            g_in.toggleMode = false;
            showInfo("Cheats are not enabled on this world");
        }
        if (g_in.toggleMode) {
            g_in.toggleMode = false;
            player.mode = player.creative() ? GameMode::Survival : GameMode::Creative;
            if (!player.creative()) player.fly = false;
            showInfo(player.creative() ? "Game mode: Creative" : "Game mode: Survival");
        }

        // ---- Игрок
        player.armor = inv.armorValue();
        for (int src = 0; src < 5; ++src) player.enchProt[src] = armorProtectionPoints(inv.armor, src);
        player.respiration = inv.armor[0].enchLevel(ENCH_RESPIRATION);
        player.aquaAffinity = inv.armor[0].enchLevel(ENCH_AQUA_AFFINITY) > 0;
        if (mobMgr.difficulty == 0 && player.health < 20 && !player.dead && worldTime % 20 == 0) ++player.health;
        TickEvents ev;
        int ridingIdx = -1;
        for (size_t vi = 0; vi < mobMgr.vehicles.size(); ++vi)
            if (ridingId && mobMgr.vehicles[vi].id == ridingId) ridingIdx = (int)vi;
        if (ridingId && (ridingIdx < 0 || player.dead)) ridingId = 0, ridingIdx = -1;
        if (ridingIdx >= 0 && in.sneak) {
            if (mp) netConn.send(C_DISMOUNT);
            // Shift — выйти
            Vehicle& rv = mobMgr.vehicles[ridingIdx];
            player.pos = player.prevPos = rv.pos + glm::vec3(0, rv.kind == VehicleKind::Boat ? 1.0f : 0.8f, 0);
            player.motion = glm::vec3(0.f);
            player.eyeOffset = player.prevEyeOffset = 0.f;
            ridingId = 0;
            ridingIdx = -1;
        }
        // Верхом на свинье: игрок сидит на ней (ноги ниже спины), физика игрока не работает
        Mob* pig = nullptr;
        if (ridingPigId) {
            for (auto& m : mobMgr.mobs) if (m.id == ridingPigId && !m.dying()) pig = &m;
            if (!pig || player.dead || in.sneak) {
                if (mp) netConn.send(C_DISMOUNT);
                if (pig) { pig->ridden = false; player.pos = player.prevPos = pig->pos + glm::vec3(0, 0.9f, 0); }
                ridingPigId = 0;
                pig = nullptr;
            }
        }
        if (pig) {
            player.prevPos = player.pos;
            player.pos = pig->pos + glm::vec3(0.f, 0.175f, 0.f);
            player.motion = glm::vec3(0.f);
            player.fallDistance = 0.f;
            player.onGround = true;
        }
        // Застрял внутри полного непрозрачного куба (pushOutOfBlocks 1.0) — поднимаем. Двери, люки, калитки и прочие
        // тонкие блоки не считаются: закрытая дверь в игроке не должна подбрасывать его наверх
        auto inFullCube = [&](const glm::vec3& p) {
            AABB bb = bodyBox(p, PLAYER_HALF_W, PLAYER_H);
            for (int x = (int)std::floor(bb.mn.x); x <= (int)std::floor(bb.mx.x - 1e-4f); ++x)
                for (int y = (int)std::floor(bb.mn.y); y <= (int)std::floor(bb.mx.y - 1e-4f); ++y)
                    for (int z = (int)std::floor(bb.mn.z); z <= (int)std::floor(bb.mx.z - 1e-4f); ++z) {
                        uint8_t b = world->getBlock(x, y, z);
                        if (isOpaque(b) && isSolid(b)) return true;
                    }
            return false;
        };
        if (ready && !sleeping && ridingIdx < 0 && !pig && !player.fly && inFullCube(player.pos)) {
            glm::vec3 up = player.pos;
            for (int i = 0; i < 8 && collides(*world, up); ++i) up.y = std::floor(up.y) + 1.f;
            if (!collides(*world, up)) player.pos = player.prevPos = up;
        }
        if (g_in.screen == Screen::Credits) player.invulnerable = std::max(player.invulnerable, 2);
        if (ready && !sleeping && ridingIdx < 0 && !pig) tickPlayer(player, *world, in, ev);
        else player.prevPos = player.pos;
        if (g_in.cycleCamera) { camMode = (camMode + 1) % 3; g_in.cycleCamera = false; }
        {
            // Шаг и поворот тела модели игрока (limbSwing, renderYawOffset): тело идёт за движением,
            // но не отстаёт от головы больше чем на 50°
            glm::vec2 mv(player.pos.x - player.prevPos.x, player.pos.z - player.prevPos.z);
            float dist = glm::length(mv);
            plPrevLimbAmount = plLimbAmount;
            plLimbAmount += (std::min(dist * 4.f, 1.f) - plLimbAmount) * 0.4f;
            plLimbSwing += plLimbAmount;
            plPrevBodyYaw = plBodyYaw;
            auto wrap = [](float a) { while (a > 180.f) a -= 360.f; while (a < -180.f) a += 360.f; return a; };
            float target = dist > 0.02f ? glm::degrees(std::atan2(mv.y, mv.x)) : player.yaw;
            if (dist > 0.02f && std::abs(wrap(target - player.yaw)) > 95.f) target += 180.f; // пятится назад
            plBodyYaw += wrap(target - plBodyYaw) * 0.3f;
            float d = wrap(player.yaw - plBodyYaw);
            if (d > 50.f) plBodyYaw = player.yaw - 50.f;
            if (d < -50.f) plBodyYaw = player.yaw + 50.f;
        }
        if (ready && !mp) mobMgr.tickVehicles(*world, player, ridingIdx, in.forward, in.strafe, particles, items, gameRng);
        ridingIdx = -1;
        for (size_t vi = 0; vi < mobMgr.vehicles.size(); ++vi)
            if (ridingId && mobMgr.vehicles[vi].id == ridingId) ridingIdx = (int)vi;
        if (ridingIdx >= 0) {
            // Сидим: ноги на сиденье, глаза ниже
            Vehicle& rv = mobMgr.vehicles[ridingIdx];
            float seat = rv.kind == VehicleKind::Boat ? 0.3f : 0.35f;
            player.prevPos = rv.prev + glm::vec3(0, seat, 0);
            player.pos = rv.pos + glm::vec3(0, seat, 0);
            player.motion = glm::vec3(0.f);
            player.fallDistance = 0.f;
            player.onGround = true;
            player.eyeOffset = player.prevEyeOffset = 0.6f;
            if (rv.kind != VehicleKind::Boat) {
                currentWorld.cartDistance += glm::length(glm::vec2(rv.pos.x - rv.prev.x, rv.pos.z - rv.prev.z));
                if (currentWorld.cartDistance >= 1000.f) unlockAch(11);
            }
        } else if (ridingId) {
            ridingId = 0;
            player.eyeOffset = player.prevEyeOffset = 0.f;
        }

        // ---- Мобы: ИИ, стрелы, спавн монстров в темноте, животные в новых чанках
        float skyFactorNow = dimension == 0 ? computeSky(worldTime, 0.f, player.look(), rainStrength, thunderStrength).skyFactor : 0.f;
        int skySub = (int)((1.f - (skyFactorNow - 0.05f) / 0.95f) * 11.f + 0.5f);
        if (ready && !mp) {
            mobMgr.tick(*world, player, ev, items, particles, mobHooks, skyFactorNow, gameRng);
            mobMgr.spawnHostiles(*world, player, skySub, world->renderDistance, gameRng);
            mobMgr.spawnPassive(*world, player, worldTime % 400 == 0, worldTime % 20 == 0, gameRng);
            mobMgr.despawn(player, *world, skyFactorNow);
        }
        // F6 (отладка): поставить моба следующего вида в 3 блоках перед собой
        if (g_in.debugSpawn && (mp || !cheatsAllowed())) g_in.debugSpawn = false;
        if (g_in.debugSpawn) {
            static int nextType = 0;
            glm::vec3 f = player.look();
            f.y = 0;
            glm::vec3 at = player.pos + glm::normalize(f + glm::vec3(1e-4f)) * 3.f;
            MobType t = (MobType)(nextType++ % (int)MobType::COUNT);
            mobMgr.spawn(t, at, player.yaw + 180.f);
            showInfo(std::string("Spawned ") + mobDef(t).name);
            g_in.debugSpawn = false;
        }
        for (auto [gx, gz] : world->generated) {
            if (mp) { // сетевая игра: рельеф свой (тот же сид), правки чанка — с сервера
                net::Writer w;
                w.i32(gx); w.i32(gz);
                netConn.send(C_CHUNK_REQ, w);
                mpPendingChunks.insert(chunkKey(gx, gz));
            } else {
                mobMgr.populateChunk(*world, gx, gz, gameRng);
            }
        }
        world->villagerSpawns.clear();
        world->generated.clear();
        for (auto& [sp, sm] : world->newSpawners) mobMgr.addSpawner(sp, sm);
        world->newSpawners.clear();
        if (ready && !mp) mobMgr.tickSpawners(*world, player, particles, gameRng);

        if (mp) {
            // Своё состояние — серверу; чужих игроков сглаживаем и анимируем
            PlayerNet me;
            me.x = player.pos.x; me.y = player.pos.y; me.z = player.pos.z;
            me.yaw = player.yaw; me.pitch = player.pitch;
            me.flags = (player.onGround ? 1 : 0) | (player.sneaking ? 2 : 0) | (swing < prevSwing || (swing == 0.f) ? 4 : 0) | (player.dead ? 8 : 0) |
                       (player.creative() ? 16 : 0) | (player.hurtTime > 0 ? 32 : 0) | (player.fireTicks > 0 ? 64 : 0) | (sleeping ? 128 : 0);
            me.dim = (int8_t)dimension;
            const ItemStack& hs = inv.slots[g_in.selected];
            me.held = hs.empty() ? 0 : hs.id;
            me.heldDamage = hs.damage;
            for (int i = 0; i < 4; ++i) me.armor[i] = inv.armor[i].empty() ? 0 : inv.armor[i].id;
            net::Writer w;
            writePlayerNet(w, me);
            uint32_t rideId = ridingId ? ridingId : 0;
            w.u32(rideId);
            w.f32(ridingId ? in.forward : 0.f);
            w.f32(ridingId ? in.strafe : 0.f);
            netConn.send(C_PLAYER, w);
            if (++mpSaveTimer >= 100) { mpSaveTimer = 0; if (savePlayerFn) savePlayerFn(); }
            // Содержимое открытого контейнера изменилось — серверу
            if (mpOpenKind >= 0 && g_in.screen == Screen::Container && gui.tile) {
                std::vector<ItemStack> now(gui.tile->items, gui.tile->items + gui.tile->size());
                if (gui.tile2) now.insert(now.end(), gui.tile2->items, gui.tile2->items + gui.tile2->size());
                bool changed = now.size() != mpOpenSent.size();
                for (size_t i = 0; !changed && i < now.size(); ++i)
                    changed = now[i].id != mpOpenSent[i].id || now[i].count != mpOpenSent[i].count || now[i].damage != mpOpenSent[i].damage;
                if (changed) {
                    net::Writer cw;
                    cw.u8((uint8_t)mpOpenKind);
                    if (mpOpenKind == 0) { cw.i32(mpOpenPos.x); cw.u8((uint8_t)mpOpenPos.y); cw.i32(mpOpenPos.z); }
                    else cw.u32(mpOpenVehicle);
                    cw.u8((uint8_t)now.size());
                    for (auto& s : now) writeItem(cw, s);
                    netConn.send(C_CONTAINER, cw);
                    mpOpenSent = now;
                }
            }
            for (auto& [oid, rp] : others) {
                if (!rp.has) continue;
                glm::vec3 target(rp.net.x, rp.net.y, rp.net.z);
                rp.prevPos = rp.pos;
                if (glm::length(target - rp.pos) > 8.f) rp.prevPos = rp.pos = target; // телепорт
                else rp.pos += (target - rp.pos) * 0.5f;
                float dist = glm::length(glm::vec2(rp.pos.x - rp.prevPos.x, rp.pos.z - rp.prevPos.z));
                rp.prevLimbAmount = rp.limbAmount;
                rp.limbAmount += (std::min(dist * 4.f, 1.f) - rp.limbAmount) * 0.4f;
                rp.limbSwing += rp.limbAmount;
                auto wrap = [](float a) { while (a > 180.f) a -= 360.f; while (a < -180.f) a += 360.f; return a; };
                rp.yaw += wrap(rp.net.yaw - rp.yaw) * 0.5f;
                rp.pitch += (rp.net.pitch - rp.pitch) * 0.5f;
                rp.prevBodyYaw = rp.bodyYaw;
                float tgt = dist > 0.02f ? glm::degrees(std::atan2(rp.pos.z - rp.prevPos.z, rp.pos.x - rp.prevPos.x)) : rp.yaw;
                if (dist > 0.02f && std::abs(wrap(tgt - rp.yaw)) > 95.f) tgt += 180.f;
                rp.bodyYaw += wrap(tgt - rp.bodyYaw) * 0.3f;
                float d = wrap(rp.yaw - rp.bodyYaw);
                if (d > 50.f) rp.bodyYaw = rp.yaw - 50.f;
                if (d < -50.f) rp.bodyYaw = rp.yaw + 50.f;
                rp.prevSwing = rp.swing;
                if (rp.swing < 1.f) rp.swing = std::min(1.f, rp.swing + 1.f / 6.f);
            }
        }

        // ---- Течение жидкостей и звуки мира
        world->tickUpdates(worldTime, 400);
        // Карта в руке дорисовывается вокруг игрока (новая — с центром там, где её впервые взяли в руки)
        if (ready && inv.slots[g_in.selected].id == MAP && !player.dead) {
            ItemStack& ms = inv.slots[g_in.selected];
            if (ms.damage == 0) ms.damage = (uint16_t)newMapId();
            MapData* md = mapFor(ms.damage);
            if (!md) {
                MapData nm;
                nm.xCenter = (int)std::floor(player.pos.x);
                nm.zCenter = (int)std::floor(player.pos.z);
                nm.dim = dimension;
                nm.dirty = true;
                md = &(maps[ms.damage] = nm);
            }
            updateMap(*md, *world, player.pos, dimension, dimension != 0);
            if (worldTime % 600 == 0) saveMaps();
        }
        // Крышка открытого сундука поднимается, остальные опускаются (по 0.1 за тик)
        {
            int64_t openKey = INT64_MIN;
            if (g_in.screen == Screen::Container && gui.kind == GuiKind::Anvil && world &&
                world->getBlock(anvilPos.x, anvilPos.y, anvilPos.z) != ANVIL)
                closeGui(); // наковальня сломалась или её убрали
            if (g_in.screen == Screen::Container && gui.kind == GuiKind::Chest && gui.tile &&
                (world->getBlock(gui.tile->x, gui.tile->y, gui.tile->z) == CHEST || world->getBlock(gui.tile->x, gui.tile->y, gui.tile->z) == ENDER_CHEST))
                openKey = posKey(gui.tile->x, gui.tile->y, gui.tile->z);
            if (openKey == INT64_MIN) openKey = showcaseLidKey;
            if (openKey != INT64_MIN) chestLids[openKey];
            for (auto it = chestLids.begin(); it != chestLids.end();) {
                auto& [prevLid, lid] = it->second;
                prevLid = lid;
                bool open = it->first == openKey;
                lid = open ? std::min(1.f, lid + 0.1f) : std::max(0.f, lid - 0.1f);
                if (!open && lid <= 0.f && prevLid <= 0.f) it = chestLids.erase(it);
                else ++it;
            }
        }
        // Книги над столами: рядом игрок (3 блока) — поворачивается к нему, раскрывается и листает; иначе закрывается
        for (auto it = world->enchantTables.begin(); it != world->enchantTables.end();) {
            int64_t key = *it;
            ++it;
            BookState& bs = books[key];
            glm::ivec3 tp = posFromKey(key);
            glm::vec3 c = glm::vec3(tp) + glm::vec3(0.5f);
            bs.prevSpread = bs.spread;
            bs.prevFlip = bs.flip;
            ++bs.ticks;
            glm::vec3 d = player.pos - c;
            if (!player.dead && glm::length(glm::vec3(d.x, player.pos.y + 1.f - c.y, d.z)) < 3.f) {
                bs.rotTarget = std::atan2(d.z, d.x);
                bs.spread += 0.1f;
                if (bs.spread < 0.5f || rnd() * 40.f < 1.f) {
                    float was = bs.flipTarget;
                    do bs.flipTarget += (float)((int)(rnd() * 4) - (int)(rnd() * 4)); while (was == bs.flipTarget);
                }
            } else {
                bs.rotTarget += 0.02f;
                bs.spread -= 0.1f;
            }
            const float TWO_PI = 6.2831853f;
            while (bs.rot >= glm::pi<float>()) bs.rot -= TWO_PI;
            while (bs.rot < -glm::pi<float>()) bs.rot += TWO_PI;
            while (bs.rotTarget >= glm::pi<float>()) bs.rotTarget -= TWO_PI;
            while (bs.rotTarget < -glm::pi<float>()) bs.rotTarget += TWO_PI;
            float dr = bs.rotTarget - bs.rot;
            while (dr >= glm::pi<float>()) dr -= TWO_PI;
            while (dr < -glm::pi<float>()) dr += TWO_PI;
            bs.rot += dr * 0.4f;
            bs.prevRot = bs.rot - dr * 0.4f;
            bs.spread = std::clamp(bs.spread, 0.f, 1.f);
            float f = std::clamp((bs.flipTarget - bs.flip) * 0.4f, -0.2f, 0.2f);
            bs.flipVel += (f - bs.flipVel) * 0.9f;
            bs.flip += bs.flipVel;
        }
        for (auto& [sp, name] : world->soundEvents) {
            std::string n = name;
            if (n == "random/click") audio.play(n, 0.3f, 0.5f, &sp);
            else if (n == "random/click_fail") audio.play("random/click", 1.f, 1.2f, &sp);
            else if (n.rfind("tile/piston", 0) == 0) audio.play(n, 0.5f, rnd() * 0.25f + 0.6f, &sp);
            else if (n.rfind("random/door", 0) == 0) audio.play(n, 1.f, rnd() * 0.1f + 0.9f, &sp);
            else audio.play(n, 0.5f, 2.6f + (rnd() - rnd()) * 0.8f, &sp);
        }
        world->soundEvents.clear();
        for (auto& np : world->noteEvents) playNote(np);
        world->noteEvents.clear();
        processDispense(*world, mobMgr, items, gameRng,
                        [&](const std::string& n, float v, float pt, const glm::vec3& sp) { audio.play(n, v, pt, &sp); });

        if (ev.step) audio.playStep(ev.stepBlock);
        if (ev.landed) {
            uint8_t under = blockAt(player.pos - glm::vec3(0, 0.2f, 0));
            if (ev.fallDistance > 7.f) audio.play("damage/fallbig", 0.8f);
            else if (ev.fallDistance > 3.f) audio.play("damage/fallsmall", 0.8f);
            else if (ev.fallDistance > 1.f) audio.playStep(under);
        }
        if (ev.splash) audio.play("liquid/splash", 0.5f, 1.f + (rnd() - rnd()) * 0.4f);
        if (--swimCooldown < 0 && ev.swim) { audio.play("liquid/swim", 0.25f); swimCooldown = 14; }
        if (ev.damage > 0) audio.play("damage/hit", 1.f, (rnd() - rnd()) * 0.2f + 1.f);
        // Износ брони: каждая часть теряет урон/4 (минимум 1)
        if (ev.armorDamage > 0) {
            for (auto& a : inv.armor) {
                if (a.empty() || maxDamage(a.id) == 0) continue; // тыква на голове не изнашивается
                int ub = a.enchLevel(ENCH_UNBREAKING);
                if (ub > 0 && rnd() * (ub + 1) >= 1.f) continue;
                a.damage = (uint16_t)(a.damage + std::max(1, ev.armorDamage / 4));
                if (a.damage >= maxDamage(a.id)) { a.clear(); audio.play("random/break", 0.8f, 0.9f); }
            }
        }
        if (ev.died) {
            if (g_in.screen == Screen::Container) { GuiContext ctx = makeCtx(0, 0); gui.close(ctx); }
            dropAll();
            openScreen(win, Screen::Dead);
            mining.active = false;
        }

        // ---- Руки: ломание, использование, установка
        prevSwing = swing;
        swing = std::min(1.f, swing + 1.f / 8.f); // взмах длится 8 тиков, как в 1.0
        if (breakDelay > 0) --breakDelay;
        if (placeDelay > 0) --placeDelay;

        ItemStack& held = inv.slots[g_in.selected];
        const float reach = player.creative() ? 5.f : 4.5f;
        glm::ivec3 hit, prev, lhit, lprev;
        bool hasHit = playing && raycast(*world, player.eye(), player.look(), reach, hit, prev);
        bool hasLiquid = playing && raycast(*world, player.eye(), player.look(), reach, lhit, lprev, true);
        uint8_t target = hasHit ? world->getBlock(hit.x, hit.y, hit.z) : AIR;
        bool lmb = playing && glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        bool rmb = playing && glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        if (!lmb) g_in.ignoreLmb = false;

        // Картина или рамка под прицелом (ближе блока): индекс в mobMgr.paintings или -1
        auto paintingUnderCursor = [&]() -> int {
            int best = -1;
            float bt = reach;
            glm::vec3 o = player.eye(), d = player.look();
            for (size_t pi = 0; pi < mobMgr.paintings.size(); ++pi) {
                glm::vec3 mn, mx;
                mobMgr.paintings[pi].bounds(mn, mx);
                float t0 = 0.f, t1 = bt;
                bool okr = true;
                for (int a = 0; a < 3 && okr; ++a) {
                    if (std::abs(d[a]) < 1e-6f) { okr = o[a] >= mn[a] && o[a] <= mx[a]; continue; }
                    float ta = (mn[a] - o[a]) / d[a], tb = (mx[a] - o[a]) / d[a];
                    if (ta > tb) std::swap(ta, tb);
                    t0 = std::max(t0, ta);
                    t1 = std::min(t1, tb);
                    okr = t0 <= t1;
                }
                if (okr && t0 < bt) { bt = t0; best = (int)pi; }
            }
            float blockDist = hasHit ? glm::length(glm::vec3(hit) + 0.5f - player.eye()) : 1e9f;
            return best >= 0 && bt <= blockDist ? best : -1;
        };
        // Удар снимает картину со стены (выпадает предметом, как в 1.0 — и в творческом);
        // у рамки сначала выпадает предмет из неё, потом сама рамка (EntityItemFrame 1.4.2)
        if (g_in.attackClick && !mobMgr.paintings.empty()) {
            int best = paintingUnderCursor();
            if (best >= 0) {
                Painting& pt = mobMgr.paintings[best];
                if (mp) {
                    net::Writer w; w.u8(1); w.u32(pt.id);
                    netConn.send(C_PAINTING, w);
                } else {
                    glm::vec3 pc = pt.center();
                    glm::ivec3 cb((int)std::floor(pc.x), (int)std::floor(pc.y), (int)std::floor(pc.z));
                    if (pt.frame && !pt.item.empty()) {
                        if (!player.creative()) dropFromBlock(items, cb, pt.item, gameRng);
                        pt.item.clear();
                        pt.rotation = 0;
                    } else {
                        if (!pt.frame || !player.creative()) dropFromBlock(items, cb, makeStack(pt.frame ? ITEM_FRAME_ITEM : PAINTING), gameRng);
                        mobMgr.paintings.erase(mobMgr.paintings.begin() + best);
                    }
                }
                startSwing();
                g_in.attackClick = false;
                g_in.ignoreLmb = true;
            }
        }

        // Транспорт под прицелом (для ПКМ и ЛКМ)
        auto vehicleRay = [&](float& dist) -> int {
            int best = -1;
            float bt = 3.f;
            for (size_t vi = 0; vi < mobMgr.vehicles.size(); ++vi) {
                const Vehicle& v = mobMgr.vehicles[vi];
                if (v.id == ridingId) continue;
                float hw = v.kind == VehicleKind::Boat ? 0.75f : 0.5f, hh = v.kind == VehicleKind::Boat ? 0.6f : 0.7f;
                glm::vec3 mn = v.pos - glm::vec3(hw, 0, hw), mx = v.pos + glm::vec3(hw, hh, hw), o = player.eye(), d = player.look();
                float t0 = 0.f, t1 = bt;
                bool okr = true;
                for (int a = 0; a < 3 && okr; ++a) {
                    if (std::abs(d[a]) < 1e-6f) { okr = o[a] >= mn[a] && o[a] <= mx[a]; continue; }
                    float ta = (mn[a] - o[a]) / d[a], tb = (mx[a] - o[a]) / d[a];
                    if (ta > tb) std::swap(ta, tb);
                    t0 = std::max(t0, ta);
                    t1 = std::min(t1, tb);
                    okr = t0 <= t1;
                }
                if (okr && t0 < bt) { bt = t0; best = (int)vi; }
            }
            dist = bt;
            return best;
        };
        {
            float vd;
            int vi = (g_in.placeClick || g_in.attackClick) ? vehicleRay(vd) : -1;
            float blockDist = hasHit ? glm::length(glm::vec3(hit) + 0.5f - player.eye()) : 1e9f;
            if (vi >= 0 && vd <= blockDist) {
                Vehicle& v = mobMgr.vehicles[vi];
                if (mp) {
                    // Сетевая игра: транспорт на сервере
                    if (g_in.placeClick) {
                        if (v.kind == VehicleKind::ChestCart) {
                            mpOpenKind = 1; mpOpenVehicle = v.id;
                            net::Writer w; w.u8(1); w.u32(v.id);
                            netConn.send(C_OPEN, w);
                        } else if (!ridingId) {
                            net::Writer w; w.u32(v.id); writeItem(w, held);
                            netConn.send(C_VEHICLE_USE, w);
                        }
                        g_in.placeClick = false;
                    } else {
                        net::Writer w; w.u32(v.id); w.u8(player.creative() ? 1 : 0);
                        netConn.send(C_VEHICLE_HIT, w);
                        startSwing();
                        g_in.attackClick = false;
                        g_in.ignoreLmb = true;
                    }
                } else if (g_in.placeClick) {
                    if (v.kind == VehicleKind::ChestCart) {
                        v.chest.type = TileEntity::Chest;
                        openGui(GuiKind::Chest, &v.chest);
                    } else if (v.kind == VehicleKind::FurnaceCart) {
                        if (held.id == COAL) {
                            v.fuel += 3600;
                            glm::vec2 pd(v.pos.x - player.pos.x, v.pos.z - player.pos.z);
                            if (glm::length(pd) > 1e-3f) v.push = glm::normalize(pd);
                            consumeHeld();
                        }
                    } else if (!ridingId) {
                        ridingId = v.id;
                    }
                    g_in.placeClick = false;
                } else {
                    // Удары ломают транспорт: вагонетка (с сундуком/печкой) или лодка выпадают предметом
                    v.damage += player.creative() ? 100 : 10;
                    v.hurtTime = 10;
                    startSwing();
                    if (v.damage > 40) {
                        glm::ivec3 b((int)std::floor(v.pos.x), (int)std::floor(v.pos.y), (int)std::floor(v.pos.z));
                        if (!player.creative()) {
                            if (v.kind == VehicleKind::Boat) dropFromBlock(items, b, makeStack(BOAT), gameRng);
                            else dropFromBlock(items, b, makeStack(MINECART), gameRng);
                            if (v.kind == VehicleKind::ChestCart) dropFromBlock(items, b, makeStack(CHEST), gameRng);
                            if (v.kind == VehicleKind::FurnaceCart) dropFromBlock(items, b, makeStack(FURNACE), gameRng);
                        }
                        if (v.kind == VehicleKind::ChestCart)
                            for (auto& s : v.chest.items) dropFromBlock(items, b, s, gameRng);
                        v.dead = true;
                        mobMgr.vehicles.erase(mobMgr.vehicles.begin() + vi);
                    }
                    g_in.attackClick = false;
                    g_in.ignoreLmb = true;
                }
            }
        }
        if (g_in.placeClick && (held.id == MINECART || held.id == CHEST_MINECART || held.id == FURNACE_MINECART) && hasHit &&
            (target == RAIL || target == POWERED_RAIL || target == DETECTOR_RAIL)) {
            Vehicle v;
            v.kind = held.id == MINECART ? VehicleKind::Minecart : held.id == CHEST_MINECART ? VehicleKind::ChestCart : VehicleKind::FurnaceCart;
            v.pos = v.prev = glm::vec3(hit) + glm::vec3(0.5f, 0.f, 0.5f);
            v.id = mobMgr.nextId++;
            v.chest.type = TileEntity::Chest;
            v.yaw = player.yaw;
            if (mp) { net::Writer w; w.u8((uint8_t)v.kind); w.f32(v.pos.x); w.f32(v.pos.y); w.f32(v.pos.z); w.f32(v.yaw); netConn.send(C_VEHICLE_PLACE, w); }
            else mobMgr.vehicles.push_back(v);
            consumeHeld();
            g_in.placeClick = false;
        }
        if (g_in.placeClick && held.id == BOAT && hasLiquid && world->getBlock(lhit.x, lhit.y, lhit.z) == WATER) {
            Vehicle v;
            v.kind = VehicleKind::Boat;
            v.pos = v.prev = glm::vec3(lhit) + glm::vec3(0.5f, 0.6f, 0.5f);
            v.id = mobMgr.nextId++;
            v.yaw = player.yaw;
            if (mp) { net::Writer w; w.u8((uint8_t)v.kind); w.f32(v.pos.x); w.f32(v.pos.y); w.f32(v.pos.z); w.f32(v.yaw); netConn.send(C_VEHICLE_PLACE, w); }
            else mobMgr.vehicles.push_back(v);
            consumeHeld();
            g_in.placeClick = false;
        }

        // ПКМ по рамке: пустая — вставить предмет из руки, с предметом — повернуть его на 90°
        if (g_in.placeClick && !mobMgr.paintings.empty()) {
            int fi = paintingUnderCursor();
            if (fi >= 0 && mobMgr.paintings[fi].frame) {
                Painting& fr = mobMgr.paintings[fi];
                if (mp) {
                    net::Writer w; w.u8(3); w.u32(fr.id); writeItem(w, held);
                    netConn.send(C_PAINTING, w);
                    if (fr.item.empty() && !held.empty() && !player.creative()) consumeHeld();
                } else if (fr.item.empty() && !held.empty()) {
                    fr.item = held;
                    fr.item.count = 1;
                    fr.rotation = 0;
                    if (!player.creative()) consumeHeld();
                } else if (!fr.item.empty()) {
                    fr.rotation = (fr.rotation + 1) & 3;
                }
                startSwing();
                g_in.placeClick = false;
            }
        }
        if (g_in.placeClick && held.id == ITEM_FRAME_ITEM && hasHit) {
            glm::ivec3 face = prev - hit;
            if (face.y == 0 && std::abs(face.x) + std::abs(face.z) == 1) {
                std::vector<Painting> probe = mobMgr.paintings;
                if (mp && placeItemFrame(probe, *world, hit, face)) {
                    net::Writer w; w.u8(2); w.i32(hit.x); w.u8((uint8_t)hit.y); w.i32(hit.z); w.u8((uint8_t)(int8_t)face.x); w.u8((uint8_t)(int8_t)face.z);
                    netConn.send(C_PAINTING, w);
                    if (!player.creative()) consumeHeld();
                } else if (!mp && placeItemFrame(mobMgr.paintings, *world, hit, face)) {
                    mobMgr.paintings.back().id = mobMgr.nextId++;
                    if (!player.creative()) consumeHeld();
                }
                startSwing();
            }
            g_in.placeClick = false;
        }
        if (g_in.placeClick && held.id == PAINTING && hasHit) {
            glm::ivec3 face = prev - hit;
            if (face.y == 0 && std::abs(face.x) + std::abs(face.z) == 1) {
                std::vector<Painting> probe = mobMgr.paintings; // поместится ли (на сервере то же правило)
                if (mp && placePainting(probe, *world, hit, face, gameRng)) {
                    net::Writer w; w.u8(0); w.i32(hit.x); w.u8((uint8_t)hit.y); w.i32(hit.z); w.u8((uint8_t)(int8_t)face.x); w.u8((uint8_t)(int8_t)face.z);
                    netConn.send(C_PAINTING, w);
                    consumeHeld();
                } else if (!mp && placePainting(mobMgr.paintings, *world, hit, face, gameRng)) {
                    mobMgr.paintings.back().id = mobMgr.nextId++;
                    consumeHeld();
                }
                startSwing();
            }
            g_in.placeClick = false;
        }

        // Удар по огненному шару гаста отбивает его обратно
        if (g_in.attackClick && playing && mobMgr.deflectFireball(player.eye(), player.look(), 4.f)) {
            startSwing();
            g_in.attackClick = false;
            g_in.ignoreLmb = true;
        }
        // Удар по мобу (клик ЛКМ): если он ближе блока и не дальше 3 блоков
        // Удержание ЛКМ тоже бьёт моба — повтор раз в 10 тиков (время неуязвимости)
        static int holdAttackCd = 0;
        if (holdAttackCd > 0) --holdAttackCd;
        if (!lmb) holdAttackCd = 0;
        bool holdAttack = lmb && !g_in.attackClick && holdAttackCd == 0;
        // Сетевая игра: удар по другому игроку (если на сервере разрешено PvP)
        if ((g_in.attackClick || holdAttack) && playing && mp && mpPvp && !player.dead) {
            float best = 3.f;
            uint32_t hitId = 0;
            glm::vec3 o = player.eye(), d = player.look();
            for (auto& [oid, rp] : others) {
                if (!rp.has || rp.net.dim != dimension || (rp.net.flags & (8 | 16))) continue;
                glm::vec3 mn = rp.pos - glm::vec3(0.3f, 0.f, 0.3f), mx = rp.pos + glm::vec3(0.3f, 1.8f, 0.3f);
                float t0 = 0.f, t1 = best;
                bool ok = true;
                for (int a = 0; a < 3 && ok; ++a) {
                    if (std::abs(d[a]) < 1e-6f) { ok = o[a] >= mn[a] && o[a] <= mx[a]; continue; }
                    float ta = (mn[a] - o[a]) / d[a], tb = (mx[a] - o[a]) / d[a];
                    if (ta > tb) std::swap(ta, tb);
                    t0 = std::max(t0, ta);
                    t1 = std::min(t1, tb);
                    ok = t0 <= t1;
                }
                float blockDist = hasHit ? glm::length(glm::vec3(hit) + 0.5f - o) : 1e9f;
                if (ok && t0 < best && t0 <= blockDist) { best = t0; hitId = oid; }
            }
            if (hitId) {
                ToolInfo t = toolInfo(held.id);
                int dmg = t.attack;
                if (int e = held.enchLevel(ENCH_SHARPNESS)) dmg += (int)(rnd() * (e * 3 + 1));
                if (player.hasEffect(EFF_STRENGTH)) dmg += 3 << player.effectAmp(EFF_STRENGTH);
                bool crit = player.fallDistance > 0.f && !player.onGround && !player.inWater;
                if (crit) dmg += (int)(rnd() * (dmg / 2 + 2));
                float knock = (player.sprinting ? 2.f : 1.f) + held.enchLevel(ENCH_KNOCKBACK);
                net::Writer w;
                w.u32(hitId); w.u16((uint16_t)std::max(1, dmg)); w.f32(knock);
                netConn.send(C_ATTACK_PLAYER, w);
                if (t.type == Tool::Sword) damageHeld(1);
                else if (t.type != Tool::None) damageHeld(2);
                if (player.sprinting) { player.sprinting = false; player.motion.x *= 0.6f; player.motion.z *= 0.6f; }
                startSwing();
                holdAttackCd = 10;
                g_in.attackClick = false;
                g_in.ignoreLmb = true;
                mining.active = false;
            }
        }
        if ((g_in.attackClick || holdAttack) && playing) {
            float md;
            Mob* mob = mobMgr.raycast(player.eye(), player.look(), 3.f, md);
            float blockDist = hasHit ? glm::length(glm::vec3(hit) + 0.5f - player.eye()) : 1e9f;
            if (mob && md <= blockDist) {
                ToolInfo t = toolInfo(held.id);
                int dmg = t.attack;
                bool undead = mob->type == MobType::Zombie || mob->type == MobType::Skeleton || mob->type == MobType::PigZombie;
                bool arthropod = isSpiderLike(mob->type) || mob->type == MobType::Silverfish;
                if (int e = held.enchLevel(ENCH_SHARPNESS)) dmg += (int)(rnd() * (e * 3 + 1));
                if (int e = held.enchLevel(ENCH_SMITE); e && undead) dmg += (int)(rnd() * (e * 4 + 1));
                if (int e = held.enchLevel(ENCH_BANE); e && arthropod) dmg += (int)(rnd() * (e * 4 + 1));
                if (player.hasEffect(EFF_STRENGTH)) dmg += 3 << player.effectAmp(EFF_STRENGTH);
                if (player.hasEffect(EFF_WEAKNESS)) dmg = std::max(0, dmg - (2 << player.effectAmp(EFF_WEAKNESS)));
                mob->looting = held.enchLevel(ENCH_LOOTING);
                // Критический удар в падении, как в 1.0
                bool crit = player.fallDistance > 0.f && !player.onGround && !player.inWater;
                if (crit) dmg += (int)(rnd() * (dmg / 2 + 2));
                float knock = (player.sprinting ? 2.f : 1.f) + held.enchLevel(ENCH_KNOCKBACK);
                glm::vec3 mobCenter = mob->pos + glm::vec3(0, mobDef(mob->type).height * 0.7f, 0);
                bool landed;
                if (mp) {
                    // Сетевая игра: удар засчитывает сервер
                    net::Writer w;
                    w.u32(mob->id); w.u16((uint16_t)std::max(0, dmg)); w.f32(knock);
                    w.u8((uint8_t)(held.enchLevel(ENCH_FIRE_ASPECT) * 4)); w.u8((uint8_t)held.enchLevel(ENCH_LOOTING));
                    netConn.send(C_ATTACK, w);
                    landed = true;
                } else {
                    landed = mobMgr.hurt(*mob, dmg, player.pos, knock, true, mobHooks);
                }
                if (landed) {
                    if (dmg >= 18) unlockAch(25); // «Overkill»: 9 сердец за удар
                    if (player.sprinting) { player.sprinting = false; player.motion.x *= 0.6f; player.motion.z *= 0.6f; }
                    if (t.type == Tool::Sword) damageHeld(1);
                    else if (t.type != Tool::None) damageHeld(2);
                    addExhaustion(player, 0.3f);
                    if (crit) spawnCrit(particles, mobCenter, 0.8f);
                    if (int fa = held.enchLevel(ENCH_FIRE_ASPECT); fa && !isFireImmune(mob->type) && !mp) mob->fireTicks = fa * 80;
                }
                startSwing();
                holdAttackCd = 10;
                g_in.ignoreLmb = true; // не ломать блок позади моба
                mining.active = false;
            }
        }
        if (g_in.attackClick && playing && hasHit && !g_in.ignoreLmb) {
            if (world->getBlock(prev.x, prev.y, prev.z) == FIRE) {
                // Удар по огню гасит его (extinguishFire)
                world->setBlock(prev.x, prev.y, prev.z, AIR);
                glm::vec3 sp = glm::vec3(prev) + 0.5f;
                audio.play("random/fizz", 0.5f, 2.6f + (rnd() - rnd()) * 0.8f, &sp);
                startSwing();
                g_in.ignoreLmb = true;
                mining.active = false;
            } else if (target == NOTE_BLOCK) {
                playNote(hit);
            } else if (target == LEVER || target == STONE_BUTTON) {
                useRedstoneSource(hit);
                startSwing();
                g_in.ignoreLmb = true;
                mining.active = false;
            } else if (target == TNT && held.id == FLINT_AND_STEEL) {
                // Как в 1.0: удар по динамиту с огнивом в руке взводит его
                if (mp) { net::Writer w; w.i32(hit.x); w.u8((uint8_t)hit.y); w.i32(hit.z); netConn.send(C_TNT, w); world->applyRemote(hit.x, hit.y, hit.z, AIR, 0); }
                else { world->setBlock(hit.x, hit.y, hit.z, AIR); mobMgr.igniteTnt(hit, false, gameRng); }
                glm::vec3 sp = glm::vec3(hit) + 0.5f;
                audio.play("random/fuse", 1.f, 1.f, &sp);
                startSwing();
                g_in.ignoreLmb = true;
                mining.active = false;
            }
        }
        if (g_in.attackClick && playing && !hasHit) startSwing(); // взмах в воздух
        g_in.attackClick = false;

        // Лук: удерживать ПКМ, отпустить — выстрел (сила как в 1.0)
        bool bowReady = held.id == BOW && (player.creative() || inv.count(ARROW) > 0);
        bool infinity = held.id == BOW && held.enchLevel(ENCH_INFINITY) > 0;
        if (bowReady && rmb) {
            ++bowCharge;
        } else {
            if (bowCharge > 0 && bowReady && !rmb) {
                float f = bowCharge / 20.f;
                f = (f * f + f * 2.f) / 3.f;
                if (f >= 0.1f) {
                    f = std::min(f, 1.f);
                    float adm = 2.f + (held.enchLevel(ENCH_POWER) ? held.enchLevel(ENCH_POWER) * 0.5f + 0.5f : 0.f);
                    if (mp) {
                        net::Writer w;
                        glm::vec3 e = player.eye(), l = player.look();
                        w.f32(e.x); w.f32(e.y); w.f32(e.z); w.f32(l.x); w.f32(l.y); w.f32(l.z);
                        w.f32(f * 3.f); w.u8(f >= 1.f ? 1 : 0); w.f32(adm);
                        w.u8((uint8_t)held.enchLevel(ENCH_PUNCH)); w.u8(held.enchLevel(ENCH_FLAME) > 0 ? 1 : 0);
                        w.u8(!infinity && !player.creative() ? 1 : 0);
                        netConn.send(C_SHOOT, w);
                    } else {
                        Arrow& ar = mobMgr.shootArrow(player.eye(), player.look(), f * 3.f, 1.f, true, f >= 1.f, gameRng);
                        // Чары лука: Сила (+0.5 урона за уровень и 0.5), Отдача, Горящая стрела, Бесконечность
                        ar.damage = adm;
                        ar.punch = held.enchLevel(ENCH_PUNCH);
                        ar.flame = held.enchLevel(ENCH_FLAME) > 0;
                        ar.pickup = !infinity && !player.creative();
                        audio.play("random/bow", 1.f, 1.f / (rnd() * 0.4f + 1.2f) + f * 0.5f);
                    }
                    if (!player.creative() && !infinity) {
                        for (auto& s2 : inv.slots)
                            if (s2.id == ARROW) { if (--s2.count == 0) s2.clear(); break; }
                    }
                    damageHeld(1);
                    startSwing();
                }
            }
            bowCharge = 0;
        }

        // ПКМ по мобу: размножение, приручение, краситель, седло, миска, ножницы у грибной коровы
        if (g_in.placeClick) {
            float md;
            Mob* mob = mobMgr.raycast(player.eye(), player.look(), 3.f, md);
            float blockDist = hasHit ? glm::length(glm::vec3(hit) + 0.5f - player.eye()) : 1e9f;
            if (mob && md <= blockDist) {
                int consume = 0;
                ItemStack rep;
                if (mp) {
                    // Сетевая игра: приручение, размножение, седло, ножницы и прочее решает сервер
                    if (!ridingPigId && !ridingId && !player.dead) {
                        net::Writer w; w.u32(mob->id); writeItem(w, held);
                        netConn.send(C_INTERACT, w);
                        if (held.id == SHEARS && mob->type == MobType::Sheep) damageHeld(1);
                        startSwing();
                    }
                    g_in.placeClick = false;
                } else if (mob->type == MobType::Pig && mob->saddled && held.id != SADDLE && !ridingPigId && !ridingId && !player.dead) {
                    // Сесть на осёдланную свинью (1.0: рулить нельзя, свинья бродит сама; слезть — Shift)
                    ridingPigId = mob->id;
                    mob->ridden = true;
                    g_in.placeClick = false;
                } else if (mobMgr.interact(*mob, held, player, items, particles, mobHooks, gameRng, consume, rep)) {
                    if (held.id == SHEARS) damageHeld(1);
                    for (int i = 0; i < consume; ++i) consumeHeld();
                    if (!rep.empty()) {
                        if (held.empty()) held = rep;
                        else if (!inv.add(rep)) throwItem(rep);
                    }
                    startSwing();
                    g_in.placeClick = false;
                }
            }
        }

        // Ножницы по овце — шерсть
        if (g_in.placeClick && held.id == SHEARS && !mp) {
            float md;
            Mob* mob = mobMgr.raycast(player.eye(), player.look(), 3.f, md);
            if (mob && mob->type == MobType::Sheep && !mob->sheared && mob->growingAge >= 0) {
                mob->sheared = true;
                glm::ivec3 b((int)std::floor(mob->pos.x), (int)std::floor(mob->pos.y + 0.5f), (int)std::floor(mob->pos.z));
                int n = 1 + (int)(rnd() * 3);
                for (int i = 0; i < n; ++i) dropFromBlock(items, b, makeStack(WOOL, 1, (uint16_t)mob->color), gameRng);
                damageHeld(1);
                glm::vec3 sp = mob->pos + glm::vec3(0, 0.6f, 0);
                audio.play("mob/sheep/shear", 1.f, 1.f, &sp);
                startSwing();
                g_in.placeClick = false;
            }
        }

        // Ведро по корове — молоко
        if (g_in.placeClick && held.id == BUCKET) {
            float md;
            Mob* mob = mobMgr.raycast(player.eye(), player.look(), 3.f, md);
            if (mob && mob->type == MobType::Cow) {
                ItemStack milk = makeStack(MILK_BUCKET);
                if (held.count == 1) held = milk;
                else { consumeHeld(); if (!inv.add(milk)) throwItem(milk); }
                g_in.placeClick = false;
            }
        }

        // Еда: удерживать ПКМ 32 тика (1.6 с)
        FoodInfo food = foodInfo(held.id);
        bool aimingGui = hasHit && hasGui(target) && !player.sneaking;
        bool drinking = held.id == MILK_BUCKET || (held.id == POTION && !potionSplash(held.damage));
        bool eating = rmb && !aimingGui &&
                      (drinking || (food.hunger > 0 && (player.food < 20 || player.creative() || held.id == GOLDEN_APPLE)));
        if (eating) {
            ++useTicks;
            if (useTicks > 8 && useTicks % 4 == 0) {
                if (drinking) audio.play("random/drink", 0.5f, rnd() * 0.1f + 0.9f);
                else audio.play("random/eat", 0.5f + 0.5f * rnd(), (rnd() - rnd()) * 0.2f + 1.f);
            }
            if (useTicks >= 32 && drinking) {
                if (held.id == MILK_BUCKET) {
                    player.effects.clear();
                    player.poisonTicks = 0;
                    if (!player.creative()) held = makeStack(BUCKET);
                } else {
                    int e = potionEffect(held.damage), amp = potionAmp(held.damage);
                    if (e == EFF_HEAL) player.health = std::min(20, player.health + (4 << amp));
                    else if (e == EFF_HARM) { player.invulnerable = 0; player.damageSource = -1; TickEvents pe; hurtPlayer(player, 6 << amp, pe, true); }
                    else if (e != EFF_NONE) addEffect(player, e, amp, potionDuration(held.damage));
                    if (!player.creative()) held = makeStack(GLASS_BOTTLE);
                }
                useTicks = 0;
            } else if (useTicks >= 32) {
                eatFood(player, food.hunger, food.saturation);
                const uint16_t ate = held.id;
                if (ate == GOLDEN_APPLE) addEffect(player, EFF_REGEN, 3, 600);                        // Регенерация IV, 30 с
                else if (ate == RAW_CHICKEN && rnd() < 0.3f) addEffect(player, EFF_HUNGER, 0, 600);   // Голод, 30 с
                else if (ate == ROTTEN_FLESH && rnd() < 0.8f) addEffect(player, EFF_HUNGER, 0, 600);
                else if (ate == SPIDER_EYE) addEffect(player, EFF_POISON, 0, 100);                    // Яд, 5 с
                consumeHeld();
                if (food.leftover) {
                    ItemStack lo = makeStack(food.leftover);
                    if (held.empty()) held = lo;
                    else if (!inv.add(lo)) throwItem(lo);
                }
                audio.play("random/burp", 0.5f, rnd() * 0.1f + 0.9f);
                useTicks = 0;
            }
        } else {
            useTicks = 0;
        }

        // Снежок и яйцо: бросок по ПКМ (при удержании — раз в 4 тика, как в 1.0)
        if (held.id == EYE_OF_ENDER && g_in.placeClick && dimension == 0 && !(hasHit && target == END_PORTAL_FRAME)) {
            // Бросок ока: летит к ближайшей крепости (к комнате с порталом)
            glm::ivec3 shPos;
            bool hasSH = world->locateStronghold10(player.pos, shPos);
            if (!hasSH) {
                float bd = 1e18f;
                for (const glm::ivec3& c : world->strongholdCenters()) {
                    float d = glm::length(glm::vec2(c.x - player.pos.x, c.z - player.pos.z));
                    if (d < bd) { bd = d; shPos = c; }
                }
            }
            if (mp) {
                net::Writer w;
                glm::vec3 e = player.eye(), l = player.look();
                w.f32(e.x); w.f32(e.y); w.f32(e.z); w.f32(l.x); w.f32(l.y); w.f32(l.z); w.u16(EYE_OF_ENDER); w.u16(0);
                netConn.send(C_THROW, w);
            } else {
                mobMgr.throwItem(player.eye(), player.look(), EYE_OF_ENDER, gameRng);
                Throwable& t = mobMgr.throwables.back();
                t.seeking = true;
                t.motion = glm::vec3(0, 0.1f, 0);
                float dx = (float)shPos.x - player.pos.x, dz = (float)shPos.z - player.pos.z;
                float distXZ = std::hypot(dx, dz);
                if (distXZ > 12.f) {
                    t.target.x = player.pos.x + (dx / distXZ) * 12.f;
                    t.target.z = player.pos.z + (dz / distXZ) * 12.f;
                    t.target.y = player.eye().y + 8.f;
                } else {
                    t.target = glm::vec3(shPos.x + 0.5f, (float)shPos.y, shPos.z + 0.5f);
                }
            }
            audio.play("random/bow", 0.5f, 0.4f / (rnd() * 0.4f + 0.8f));
            consumeHeld();
            startSwing();
            g_in.placeClick = false;
        }
        // Удочка (EntityFishHook 1.0): заброс, поплавок держится на воде по доле погружения, клёв, подсечка;
        // крючок цепляет мобов (подсечка подтягивает их), втыкается в блоки
        auto hookedMob = [&]() -> Mob* {
            if (!bobHookedId) return nullptr;
            for (auto& m : mobMgr.mobs) if (m.id == bobHookedId && !m.dying()) return &m;
            return nullptr;
        };
        if (held.id == FISHING_ROD && g_in.placeClick) {
            if (bobber) {
                int wear = 0;
                if (Mob* hm = hookedMob()) {
                    // Подтянуть моба к игроку
                    glm::vec3 d = player.pos - hm->pos;
                    float dl = glm::length(d);
                    hm->motion += glm::vec3(d.x * 0.1f, d.y * 0.1f + std::sqrt(dl) * 0.08f, d.z * 0.1f);
                    wear = 3;
                } else if (biteWindow > 0) {
                    ItemEntity e;
                    e.pos = e.prev = bobPos;
                    glm::vec3 d = player.eye() - bobPos;
                    float dl = glm::length(d);
                    e.motion = glm::vec3(d.x * 0.1f, d.y * 0.1f + std::sqrt(dl) * 0.08f, d.z * 0.1f);
                    e.stack = makeStack(RAW_FISH);
                    items.push_back(e);
                    wear = 1;
                } else if (bobInGround) {
                    wear = 2;
                }
                if (wear) damageHeld(wear);
                bobber = false;
            } else {
                bobber = true;
                bobHookedId = 0;
                bobInGround = false;
                float yr = glm::radians(player.yaw);
                bobPos = bobPrev = player.eye() - glm::vec3(std::cos(yr) * 0.16f, 0.1f, std::sin(yr) * 0.16f);
                glm::vec3 dir = glm::normalize(player.look() * 0.4f);
                dir += glm::vec3(rnd() - rnd(), rnd() - rnd(), rnd() - rnd()) * 0.0075f * 1.7f;
                bobVel = dir * 1.5f;
                biteWindow = 0;
                audio.play("random/bow", 0.5f, 0.4f / (rnd() * 0.4f + 0.8f));
            }
            startSwing();
            g_in.placeClick = false;
        }
        if (bobber) {
            bobPrev = bobPos;
            if (held.id != FISHING_ROD || glm::length(bobPos - player.pos) > 32.f || player.dead) bobber = false;
            if (Mob* hm = hookedMob()) {
                bobPos = hm->pos + glm::vec3(0, mobHeight(*hm) * 0.8f, 0); // висит на мобе
            } else if (bobHookedId) {
                bobHookedId = 0; // моб умер — крючок падает
            } else if (!bobInGround) {
                glm::ivec3 bc((int)std::floor(bobPos.x), (int)std::floor(bobPos.y), (int)std::floor(bobPos.z));
                // Доля погружения: 5 срезов высоты поплавка
                float sub = 0.f;
                for (int i = 0; i < 5; ++i) {
                    float yy = bobPos.y - 0.125f + 0.25f * i / 5.f + 0.025f;
                    uint8_t b = world->getBlock(bc.x, (int)std::floor(yy), bc.z);
                    if (b == WATER) {
                        float surf = (int)std::floor(yy) + 1.f - world->getMeta(bc.x, (int)std::floor(yy), bc.z) / 9.f;
                        if (yy < surf) sub += 0.2f;
                    }
                }
                if (sub > 0.f) {
                    if (biteWindow > 0) --biteWindow;
                    else {
                        bool rainy = rainStrength > 0.5f && world->getSkyLight(bc.x, bc.y + 1, bc.z) == 15;
                        if (rnd() * (rainy ? 300.f : 500.f) < 1.f) {
                            biteWindow = 10 + (int)(rnd() * 30);
                            bobVel.y -= 0.2f;
                            audio.play("random/splash", 0.25f, 1.f + (rnd() - rnd()) * 0.4f, &bobPos);
                            for (int i = 0; i < 8; ++i) spawnSplash(particles, bobPos + glm::vec3(rnd() - 0.5f, 0.1f, rnd() - 0.5f));
                        }
                    }
                    if (biteWindow > 0) bobVel.y -= rnd() * rnd() * rnd() * 0.2f; // поплавок дёргается
                }
                float damp = 0.92f;
                bobVel.y += 0.04f * (sub * 2.f - 1.f);
                if (sub > 0.f) { damp *= 0.9f; bobVel.y *= 0.8f; }
                bobVel *= damp;
                // Полёт: моб на пути — цепляем, блок — втыкаемся
                glm::vec3 np = bobPos + bobVel;
                float md;
                glm::vec3 seg = bobVel;
                float sl = glm::length(seg);
                Mob* hit = sl > 1e-4f ? mobMgr.raycast(bobPos, seg / sl, sl, md) : nullptr;
                if (hit && sub == 0.f) {
                    bobHookedId = hit->id;
                } else if (pointCollides(*world, np)) {
                    if (sub == 0.f && sl > 0.05f) bobInGround = true;
                    bobVel = glm::vec3(0.f);
                } else {
                    bobPos = np;
                }
            }
        }

        bool splashPotion = held.id == POTION && potionSplash(held.damage);
        if ((held.id == SNOWBALL || held.id == EGG || held.id == ENDER_PEARL || splashPotion || held.id == EXP_BOTTLE) && !aimingGui &&
            (g_in.placeClick || (rmb && placeDelay == 0))) {
            float yr = glm::radians(player.yaw);
            glm::vec3 from = player.eye() - glm::vec3(-std::sin(yr) * 0.16f, 0.1f, std::cos(yr) * 0.16f);
            glm::vec3 dir = player.look();
            if (splashPotion || held.id == EXP_BOTTLE) dir.y += 0.3f; // зелье и пузырёк опыта бросаются чуть вверх (-20°)
            if (mp) {
                net::Writer w;
                w.f32(from.x); w.f32(from.y); w.f32(from.z); w.f32(dir.x); w.f32(dir.y); w.f32(dir.z); w.u16(held.id); w.u16(held.damage);
                netConn.send(C_THROW, w);
            } else {
                mobMgr.throwItem(from, dir, held.id, gameRng, held.damage);
                audio.play("random/bow", 0.5f, 0.4f / (rnd() * 0.4f + 0.8f));
            }
            if (!player.creative()) consumeHeld();
            startSwing();
            placeDelay = 4;
            g_in.placeClick = false;
        }

        // Ломание: скорость по формуле 1.0 (прочность блока, инструмент, вода, прыжок)
        if (lmb && !g_in.ignoreLmb && hasHit && breakDelay == 0 && useTicks == 0) {
            if (!mining.active || mining.pos != hit) mining = {hit, 0.f, true, 0};
            if (swing >= 1.f) swing = 0.f; // при копании взмахи идут один за другим, без обрыва
            float strength = player.creative() ? 1.f : breakStrength(target, held, player.eyeInWater && !player.aquaAffinity, player.onGround);
            mining.progress += strength;
            if (mining.ticks++ % 4 == 0 && mining.progress < 1.f && strength > 0.f) audio.playHit(target, glm::vec3(hit) + 0.5f);
            if (mining.progress < 1.f && !player.creative()) {
                glm::vec3 face = glm::vec3(hit) + 0.5f + glm::vec3(prev - hit) * 0.55f;
                glm::vec3 jitter(rnd() - 0.5f, rnd() - 0.5f, rnd() - 0.5f);
                glm::vec3 n = glm::vec3(prev - hit);
                spawnHitParticle(particles, face + jitter * (glm::vec3(1.f) - glm::abs(n)) * 0.9f, target, world->getMeta(hit.x, hit.y, hit.z));
            }
            if (mining.progress >= 1.f) {
                breakBlock(hit);
                mining.active = false;
                breakDelay = 5;
            }
        } else if (!lmb || !hasHit) {
            mining.active = false;
        }

        // Правая кнопка: окно блока, мотыга, ведро, семена, установка блока
        if ((g_in.placeClick || (rmb && placeDelay == 0)) && !eating) {
            placeDelay = 4;
            glm::ivec3 n = prev - hit; // нормаль грани, по которой кликнули
            ToolInfo tool = toolInfo(held.id);
            if (aimingGui) {
                if (g_in.placeClick) {
                    if (target == CRAFTING_TABLE) openGui(GuiKind::Crafting, nullptr);
                    else if (target == ANVIL) {
                        anvilPos = hit;
                        openGui(GuiKind::Anvil, nullptr);
                    } else if (target == ENDER_CHEST) {
                        // Эндер-сундук: личный инвентарь игрока (раньше открывалось окно печи и на месте появлялась «печь»)
                        if (!world->chestBlocked(hit.x, hit.y, hit.z)) {
                            player.enderChest.type = TileEntity::Chest;
                            player.enderChest.x = hit.x; player.enderChest.y = hit.y; player.enderChest.z = hit.z; // для крышки
                            openGui(GuiKind::Chest, &player.enderChest);
                            gui.enderChest = true;
                        }
                    }
                    else if (target == ENCHANT_TABLE) {
                        // Книжные полки на расстоянии 2 (на уровне стола и выше), проём между ними пустой
                        int shelves = 0;
                        for (int dz = -1; dz <= 1; ++dz)
                            for (int dx = -1; dx <= 1; ++dx) {
                                if ((dx == 0 && dz == 0) || world->getBlock(hit.x + dx, hit.y, hit.z + dz) != AIR ||
                                    world->getBlock(hit.x + dx, hit.y + 1, hit.z + dz) != AIR)
                                    continue;
                                for (int dy = 0; dy <= 1; ++dy) {
                                    if (world->getBlock(hit.x + dx * 2, hit.y + dy, hit.z + dz * 2) == BOOKSHELF) ++shelves;
                                    if (dx != 0 && dz != 0) {
                                        if (world->getBlock(hit.x + dx * 2, hit.y + dy, hit.z + dz) == BOOKSHELF) ++shelves;
                                        if (world->getBlock(hit.x + dx, hit.y + dy, hit.z + dz * 2) == BOOKSHELF) ++shelves;
                                    }
                                }
                            }
                        gui.enchantShelves = shelves;
                        openGui(GuiKind::Enchant, nullptr);
                    } else {
                        TileEntity::Type tt = target == CHEST ? TileEntity::Chest : target == DISPENSER ? TileEntity::Dispenser
                                            : target == BREWING_STAND ? TileEntity::Brewing : TileEntity::Furnace;
                        if (target == CHEST && world->chestBlocked(hit.x, hit.y, hit.z)) {
                            // Сверху непрозрачный блок — крышку не поднять (как в 1.0)
                        } else if (mp) {
                            // Сетевая игра: содержимое на сервере — окно откроется, когда оно придёт
                            mpOpenKind = 0; mpOpenPos = hit;
                            net::Writer w; w.u8(0); w.i32(hit.x); w.u8((uint8_t)hit.y); w.i32(hit.z);
                            netConn.send(C_OPEN, w);
                        } else {
                            TileEntity* te = world->tileAt(hit.x, hit.y, hit.z);
                            if (!te) te = &world->createTile(hit.x, hit.y, hit.z, tt);
                            TileEntity* te2 = nullptr;
                            glm::ivec3 ca, cb;
                            if (target == CHEST && world->chestPair(hit.x, hit.y, hit.z, ca, cb)) {
                                // Двойной сундук: сверху половина с меньшей координатой
                                te = world->tileAt(ca.x, ca.y, ca.z);
                                if (!te) te = &world->createTile(ca.x, ca.y, ca.z, TileEntity::Chest);
                                te2 = world->tileAt(cb.x, cb.y, cb.z);
                                if (!te2) te2 = &world->createTile(cb.x, cb.y, cb.z, TileEntity::Chest);
                            }
                            openGui(target == CHEST ? GuiKind::Chest : target == DISPENSER ? GuiKind::Dispenser
                                    : target == BREWING_STAND ? GuiKind::Brewing : GuiKind::Furnace, te, te2);
                        }
                    }
                }
            } else if (hasHit && !player.sneaking && g_in.placeClick && (target == WOOD_DOOR || target == TRAPDOOR || target == FENCE_GATE)) {
                // Открыть/закрыть (железную дверь рукой не открыть — нужен редстоун)
                uint8_t m = world->getMeta(hit.x, hit.y, hit.z);
                bool opening = !(m & 4);
                if (target == WOOD_DOOR) {
                    int ly = (m & 8) ? hit.y - 1 : hit.y;
                    uint8_t lm = (uint8_t)((world->getMeta(hit.x, ly, hit.z) & 7) ^ 4);
                    world->setBlock(hit.x, ly, hit.z, WOOD_DOOR, lm);
                    if (world->getBlock(hit.x, ly + 1, hit.z) == WOOD_DOOR) world->setBlock(hit.x, ly + 1, hit.z, WOOD_DOOR, (uint8_t)(lm | 8));
                } else if (target == TRAPDOOR) {
                    world->setBlock(hit.x, hit.y, hit.z, TRAPDOOR, (uint8_t)(m ^ 4));
                } else {
                    // Калитка открывается от игрока
                    glm::vec3 l = player.look();
                    uint8_t f = std::abs(l.x) > std::abs(l.z) ? (l.x > 0 ? 0 : 2) : (l.z > 0 ? 1 : 3);
                    uint8_t nm = opening ? (uint8_t)(((m & 1) == (f & 1) ? f : (m & 3)) | 4) : (uint8_t)(m & 3);
                    world->setBlock(hit.x, hit.y, hit.z, FENCE_GATE, nm);
                }
                glm::vec3 sp = glm::vec3(hit) + 0.5f;
                audio.play(opening ? "random/door_open" : "random/door_close", 1.f, rnd() * 0.1f + 0.9f, &sp);
                startSwing();
            } else if (hasHit && !player.sneaking && g_in.placeClick && (target == LEVER || target == STONE_BUTTON)) {
                useRedstoneSource(hit);
                startSwing();
            } else if (hasHit && !player.sneaking && g_in.placeClick && target == JUKEBOX &&
                       (world->getMeta(hit.x, hit.y, hit.z) > 0 || isRecord(held.id))) {
                uint8_t m = world->getMeta(hit.x, hit.y, hit.z);
                glm::vec3 sp = glm::vec3(hit) + 0.5f;
                if (m > 0) {
                    // Достать пластинку
                    dropFromBlock(items, hit + glm::ivec3(0, 1, 0), makeStack((uint16_t)(RECORD_13 + m - 1)), gameRng);
                    world->setMeta(hit.x, hit.y, hit.z, 0);
                    audio.stopRecord();
                } else {
                    world->setMeta(hit.x, hit.y, hit.z, (uint8_t)(held.id - RECORD_13 + 1));
                    audio.playRecord(recordName(held.id), sp);
                    showInfo(std::string("Now playing: C418 - ") + recordName(held.id));
                    consumeHeld();
                }
            } else if (hasHit && !player.sneaking && g_in.placeClick && isRepeater(target)) {
                uint8_t m = world->getMeta(hit.x, hit.y, hit.z);
                world->setMeta(hit.x, hit.y, hit.z, (uint8_t)((m & 3) | ((((m >> 2) + 1) & 3) << 2)));
            } else if (hasHit && !player.sneaking && g_in.placeClick && target == NOTE_BLOCK) {
                uint8_t m = world->getMeta(hit.x, hit.y, hit.z);
                world->setMeta(hit.x, hit.y, hit.z, (uint8_t)((m + 1) % 25));
                playNote(hit);
            } else if (hasHit && !player.sneaking && g_in.placeClick && target == BED) {
                // Сон: только ночью (или в грозу) и без монстров рядом; кровать становится точкой возрождения
                uint8_t m = world->getMeta(hit.x, hit.y, hit.z);
                static const int SD[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
                glm::ivec3 head = (m & 8) ? hit : hit + glm::ivec3(SD[m & 3][0], 0, SD[m & 3][1]);
                int64_t tod = worldTime % 24000;
                bool night = (tod >= 12541 && tod <= 23458) || (raining && thundering);
                if (dimension != 0) {
                    // В Незере и Крае кровать взрывается (сила 5, с огнём)
                    world->setBlock(hit.x, hit.y, hit.z, AIR);
                    mobMgr.explode(*world, glm::vec3(head) + 0.5f, 5.f, player, ev, items, particles, mobHooks, gameRng, true);
                    night = false;
                    m = 0xFF;
                }
                bool monsters = false;
                for (auto& mb : mobMgr.mobs)
                    if (mobDef(mb.type).hostile && !mb.dying() && std::abs(mb.pos.x - head.x - 0.5f) < 8.f &&
                        std::abs(mb.pos.y - head.y) < 5.f && std::abs(mb.pos.z - head.z - 0.5f) < 8.f)
                        monsters = true;
                if (m == 0xFF) {}
                else if (!night) showInfo("You can only sleep at night");
                else if (monsters) showInfo("You may not rest now, there are monsters nearby");
                else {
                    world->hasBedSpawn = true;
                    world->bedSpawn = head;
                    sleeping = true;
                    sleepTimer = 0;
                    sleepBed = head;
                    int f = m & 3;
                    player.pos = player.prevPos = glm::vec3(head) + glm::vec3(0.5f, 0.5625f, 0.5f);
                    player.motion = glm::vec3(0.f);
                    player.yaw = f == 0 ? 180.f : f == 1 ? 270.f : f == 2 ? 0.f : 90.f; // лицом к ногам
                    player.pitch = -20.f;
                    player.eyeOffset = player.prevEyeOffset = EYE_H - 0.25f;
                    mining.active = false;
                    openScreen(win, Screen::Sleep);
                }
            } else if (held.id == SIGN_ITEM && hasHit && n.y >= 0 && isSolid(target) &&
                       isReplaceable(world->getBlock(prev.x, prev.y, prev.z))) {
                glm::ivec3 p = prev;
                if (n.y == 1) {
                    // Табличка на столбике — повёрнута к игроку (16 положений)
                    glm::vec3 l = -player.look();
                    float ang = std::atan2(l.z, l.x) / 6.2831853f * 16.f;
                    world->setBlock(p.x, p.y, p.z, SIGN_POST, (uint8_t)(((int)std::lround(ang) % 16 + 16) % 16));
                } else {
                    world->setBlock(p.x, p.y, p.z, WALL_SIGN, (uint8_t)(n.x == -1 ? 0 : n.z == -1 ? 1 : n.x == 1 ? 2 : 3));
                }
                world->signs[posKey(p.x, p.y, p.z)] = {"", "", "", ""};
                audio.playDig(SIGN_POST, glm::vec3(p) + 0.5f);
                consumeHeld();
                startSwing();
                signEdit = p;
                signLine = 0;
                g_in.typed.clear();
                openScreen(win, Screen::Sign);
            } else if (held.id == BED_ITEM && hasHit && n.y == 1) {
                glm::vec3 l = player.look();
                uint8_t f = std::abs(l.x) > std::abs(l.z) ? (l.x > 0 ? 0 : 2) : (l.z > 0 ? 1 : 3);
                static const int SD[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
                glm::ivec3 foot = prev, head = prev + glm::ivec3(SD[f][0], 0, SD[f][1]);
                auto freeCell = [&](const glm::ivec3& c) {
                    return isReplaceable(world->getBlock(c.x, c.y, c.z)) && isOpaque(world->getBlock(c.x, c.y - 1, c.z));
                };
                if (freeCell(foot) && freeCell(head)) {
                    world->setBlock(foot.x, foot.y, foot.z, BED, f);
                    world->setBlock(head.x, head.y, head.z, BED, (uint8_t)(f | 8));
                    audio.playDig(BED, glm::vec3(foot) + 0.5f);
                    consumeHeld();
                    startSwing();
                }
            } else if (hasHit && g_in.placeClick && target == CAULDRON && (held.id == WATER_BUCKET || held.id == GLASS_BOTTLE)) {
                uint8_t m = world->getMeta(hit.x, hit.y, hit.z);
                if (held.id == WATER_BUCKET) {
                    world->setMeta(hit.x, hit.y, hit.z, 3);
                    if (!player.creative()) held = makeStack(BUCKET);
                } else if (m > 0) {
                    world->setMeta(hit.x, hit.y, hit.z, (uint8_t)(m - 1));
                    ItemStack wb = makeStack(POTION, 1, 0);
                    if (!player.creative()) {
                        if (held.count == 1) held = wb;
                        else { consumeHeld(); if (!inv.add(wb)) throwItem(wb); }
                    } else if (!inv.add(wb)) throwItem(wb);
                }
            } else if (held.id == GLASS_BOTTLE && hasLiquid && world->getBlock(lhit.x, lhit.y, lhit.z) == WATER && g_in.placeClick) {
                // Набрать воды в бутылку
                ItemStack wb = makeStack(POTION, 1, 0);
                if (held.count == 1 && !player.creative()) held = wb;
                else { consumeHeld(); if (!inv.add(wb)) throwItem(wb); }
                audio.play("liquid/splash", 0.3f, 1.2f);
            } else if (held.id == EYE_OF_ENDER && hasHit && g_in.placeClick && target == END_PORTAL_FRAME &&
                       !(world->getMeta(hit.x, hit.y, hit.z) & 4)) {
                world->setMeta(hit.x, hit.y, hit.z, (uint8_t)(world->getMeta(hit.x, hit.y, hit.z) | 4));
                consumeHeld();
                for (int i = 0; i < 12; ++i) spawnSmoke(particles, glm::vec3(hit) + glm::vec3(rnd(), 1.f, rnd()), false);
                if (world->tryOpenEndPortal(hit.x, hit.y, hit.z)) audio.play("portal/trigger", 1.f, 1.2f);
            } else if (hasHit && g_in.placeClick && target == FLOWER_POT && world->getMeta(hit.x, hit.y, hit.z) == 0 &&
                       isBlockItem(held.id) && flowerPotMetaFor((uint8_t)held.id, (uint8_t)held.damage) != 0) {
                // Посадить растение в пустой горшок (BlockFlowerPot.onBlockActivated)
                world->setBlock(hit.x, hit.y, hit.z, FLOWER_POT, flowerPotMetaFor((uint8_t)held.id, (uint8_t)held.damage));
                if (!player.creative()) consumeHeld();
                startSwing();
            } else if (hasHit && g_in.placeClick && target == CAKE) {
                // Кусок торта: 2 единицы сытости (BlockCake.eatCakeSlice)
                if (player.food < 20) {
                    eatFood(player, 2, 0.1f);
                    uint8_t m = (uint8_t)(world->getMeta(hit.x, hit.y, hit.z) + 1);
                    if (m >= 6) world->setBlock(hit.x, hit.y, hit.z, AIR);
                    else world->setBlock(hit.x, hit.y, hit.z, CAKE, m);
                }
            } else if (held.id == FLINT_AND_STEEL && hasHit && target == TNT) {
                if (mp) { net::Writer w; w.i32(hit.x); w.u8((uint8_t)hit.y); w.i32(hit.z); netConn.send(C_TNT, w); world->applyRemote(hit.x, hit.y, hit.z, AIR, 0); }
                else { world->setBlock(hit.x, hit.y, hit.z, AIR); mobMgr.igniteTnt(hit, false, gameRng); }
                glm::vec3 sp = glm::vec3(hit) + 0.5f;
                audio.play("fire/ignite", 1.f, rnd() * 0.4f + 0.8f, &sp);
                audio.play("random/fuse", 1.f, 1.f, &sp);
                damageHeld(1);
                startSwing();
            } else if (held.id == FIRE_CHARGE && hasHit && g_in.placeClick) {
                // Огненный шар (ItemFireball 1.4.2): поджигает, как огниво, и тратится
                glm::ivec3 p = prev;
                if (world->getBlock(p.x, p.y, p.z) == AIR) {
                    if (!world->tryCreatePortal(p.x, p.y, p.z)) world->setBlock(p.x, p.y, p.z, FIRE);
                    glm::vec3 sp = glm::vec3(p) + 0.5f;
                    audio.play("mob/ghast/fireball", 1.f, (rnd() - rnd()) * 0.2f + 1.f, &sp);
                    if (!player.creative()) consumeHeld();
                }
                startSwing();
            } else if (held.id == FLINT_AND_STEEL && hasHit) {
                glm::ivec3 p = prev;
                if (world->getBlock(p.x, p.y, p.z) == AIR && world->tryCreatePortal(p.x, p.y, p.z)) {
                    glm::vec3 sp = glm::vec3(p) + 0.5f;
                    audio.play("fire/ignite", 1.f, rnd() * 0.4f + 0.8f, &sp);
                } else if (world->getBlock(p.x, p.y, p.z) == AIR && world->canPlaceFire(p.x, p.y, p.z)) {
                    world->setBlock(p.x, p.y, p.z, FIRE, 0);
                    glm::vec3 sp = glm::vec3(p) + 0.5f;
                    audio.play("fire/ignite", 1.f, rnd() * 0.4f + 0.8f, &sp);
                }
                damageHeld(1);
                startSwing();
            } else if (held.id == DYE && held.damage == DYE_COCOA && hasHit && n.y == 0 && target == LOG &&
                       (world->getMeta(hit.x, hit.y, hit.z) & 3) == 3 && isReplaceable(world->getBlock(prev.x, prev.y, prev.z)) &&
                       !isLiquid(world->getBlock(prev.x, prev.y, prev.z))) {
                // Какао-бобы сажаются на бок ствола джунглей (ItemDye 1.4.2); мета — где ствол: 0 юг, 1 запад, 2 север, 3 восток
                uint8_t cm = n.z < 0 ? 0 : n.x > 0 ? 1 : n.z > 0 ? 2 : 3;
                world->setBlock(prev.x, prev.y, prev.z, COCOA, cm);
                audio.playDig(COCOA, glm::vec3(prev) + 0.5f);
                consumeHeld();
                startSwing();
            } else if (held.id == DYE && held.damage == DYE_BONE_MEAL && hasHit && (target == SAPLING || target == WHEAT || target == GRASS ||
                                                                                               target == PUMPKIN_STEM || target == MELON_STEM ||
                                                                                               target == CARROTS || target == POTATOES ||
                                                                                               (target == COCOA && ((world->getMeta(hit.x, hit.y, hit.z) >> 2) & 3) < 2))) {
                // Костная мука: мгновенный рост дерева, пшеницы и стеблей, трава и цветы вокруг
                if (target == SAPLING) {
                    world->growTree(hit.x, hit.y, hit.z, gameRng, world->getMeta(hit.x, hit.y, hit.z) & 3);
                } else if (target == WHEAT || target == PUMPKIN_STEM || target == MELON_STEM || target == CARROTS || target == POTATOES) {
                    world->setBlock(hit.x, hit.y, hit.z, target, 7);
                } else if (target == COCOA) {
                    world->setBlock(hit.x, hit.y, hit.z, COCOA, (uint8_t)((world->getMeta(hit.x, hit.y, hit.z) & 3) | 8));
                } else if (world->getBlock(hit.x, hit.y + 1, hit.z) == AIR) {
                    for (int i = 0; i < 128; ++i) {
                        glm::ivec3 q = hit + glm::ivec3(0, 1, 0);
                        bool ok = true;
                        for (int j = 0; j < i / 16; ++j) {
                            q += glm::ivec3((int)(rnd() * 3) - 1, ((int)(rnd() * 3) - 1) * (int)(rnd() * 3) / 2, (int)(rnd() * 3) - 1);
                            if (world->getBlock(q.x, q.y - 1, q.z) != GRASS || isOpaque(world->getBlock(q.x, q.y, q.z))) { ok = false; break; }
                        }
                        if (!ok || world->getBlock(q.x, q.y, q.z) != AIR) continue;
                        if (rnd() < 0.9f) world->setBlock(q.x, q.y, q.z, TALL_GRASS, 1);
                        else world->setBlock(q.x, q.y, q.z, rnd() < 0.75f ? DANDELION : ROSE, 0);
                    }
                }
                consumeHeld();
                startSwing();
            } else if ((held.id == WOOD_DOOR_ITEM || held.id == IRON_DOOR_ITEM) && hasHit && n.y == 1) {
                glm::ivec3 p = prev;
                uint8_t door = held.id == WOOD_DOOR_ITEM ? WOOD_DOOR : IRON_DOOR;
                glm::vec3 l = player.look();
                uint8_t f = std::abs(l.x) > std::abs(l.z) ? (l.x > 0 ? 0 : 2) : (l.z > 0 ? 1 : 3);
                glm::vec3 mn = player.pos - glm::vec3(PLAYER_HALF_W, 0, PLAYER_HALF_W), mx = player.pos + glm::vec3(PLAYER_HALF_W, PLAYER_H, PLAYER_HALF_W);
                bool inter = mn.x < p.x + 1 && mx.x > p.x && mn.y < p.y + 2 && mx.y > p.y && mn.z < p.z + 1 && mx.z > p.z;
                if (p.y + 1 < CH && isReplaceable(world->getBlock(p.x, p.y, p.z)) && isReplaceable(world->getBlock(p.x, p.y + 1, p.z)) &&
                    isOpaque(world->getBlock(p.x, p.y - 1, p.z)) && !inter) {
                    world->setBlock(p.x, p.y, p.z, door, f);
                    world->setBlock(p.x, p.y + 1, p.z, door, (uint8_t)((world->getMeta(p.x, p.y, p.z) & 7) | 8));
                    audio.playDig(door, glm::vec3(p) + 0.5f);
                    consumeHeld();
                    startSwing();
                }
            } else if (held.id < 256 && isSlab((uint8_t)held.id) && hasHit &&
                       ((target == held.id && (world->getMeta(hit.x, hit.y, hit.z) & 7) == held.damage &&
                         (n.y == ((world->getMeta(hit.x, hit.y, hit.z) & 8) ? -1 : 1))) ||
                        (world->getBlock(prev.x, prev.y, prev.z) == held.id && (world->getMeta(prev.x, prev.y, prev.z) & 7) == held.damage))) {
                // Второй полублок того же материала к первому (на нижний сверху, под верхний снизу) — двойной
                bool intoHit = target == held.id && (world->getMeta(hit.x, hit.y, hit.z) & 7) == held.damage &&
                               n.y == ((world->getMeta(hit.x, hit.y, hit.z) & 8) ? -1 : 1);
                glm::ivec3 p = intoHit ? hit : prev;
                world->setBlock(p.x, p.y, p.z, doubleSlabOf((uint8_t)held.id), (uint8_t)held.damage);
                audio.playDig((uint8_t)held.id, glm::vec3(p) + 0.5f);
                consumeHeld();
                startSwing();
            } else if (tool.type == Tool::Hoe && hasHit && (target == GRASS || target == DIRT) && n.y >= 0 &&
                       world->getBlock(hit.x, hit.y + 1, hit.z) == AIR) {
                world->setBlock(hit.x, hit.y, hit.z, FARMLAND, 0);
                audio.play("step/gravel", 0.5f, 0.8f, nullptr);
                damageHeld(1);
                startSwing();
            } else if (held.id == BUCKET && hasLiquid && isLiquid(world->getBlock(lhit.x, lhit.y, lhit.z))) {
                uint8_t liq = world->getBlock(lhit.x, lhit.y, lhit.z);
                world->setBlock(lhit.x, lhit.y, lhit.z, AIR);
                ItemStack full = makeStack(liq == WATER ? WATER_BUCKET : LAVA_BUCKET);
                if (player.creative()) {}
                else if (held.count == 1) held = full;
                else { consumeHeld(); if (!inv.add(full)) throwItem(full); }
                startSwing();
            } else if ((held.id == WATER_BUCKET || held.id == LAVA_BUCKET) && hasHit) {
                glm::ivec3 p = prev;
                uint8_t cur = world->getBlock(p.x, p.y, p.z);
                if ((cur == AIR || isPlant(cur)) && held.id == WATER_BUCKET && dimension == -1) {
                    glm::vec3 sp = glm::vec3(p) + 0.5f;
                    audio.play("random/fizz", 0.5f, 2.6f + (rnd() - rnd()) * 0.8f, &sp);
                    for (int i = 0; i < 8; ++i) spawnSmoke(particles, glm::vec3(p) + glm::vec3(rnd(), rnd(), rnd()), false);
                    if (!player.creative()) held = makeStack(BUCKET);
                } else if (cur == AIR || isPlant(cur)) {
                    world->setBlock(p.x, p.y, p.z, held.id == WATER_BUCKET ? WATER : LAVA);
                    if (!player.creative()) held = makeStack(BUCKET);
                    startSwing();
                }
            } else if ((held.id == SEEDS || held.id == PUMPKIN_SEEDS || held.id == MELON_SEEDS) && hasHit && target == FARMLAND && n.y == 1 &&
                       world->getBlock(hit.x, hit.y + 1, hit.z) == AIR) {
                world->setBlock(hit.x, hit.y + 1, hit.z, held.id == SEEDS ? WHEAT : placedBlock(held.id), 0);
                audio.playDig(WHEAT, glm::vec3(hit) + glm::vec3(0.5f, 1.5f, 0.5f));
                consumeHeld();
                startSwing();
            } else if (held.id == LILY_PAD && hasLiquid && world->getBlock(lhit.x, lhit.y, lhit.z) == WATER &&
                       world->getBlock(lhit.x, lhit.y + 1, lhit.z) == AIR) {
                // Кувшинка кладётся на поверхность воды
                world->setBlock(lhit.x, lhit.y + 1, lhit.z, LILY_PAD);
                audio.playDig(LILY_PAD, glm::vec3(lhit) + glm::vec3(0.5f, 1.f, 0.5f));
                consumeHeld();
                startSwing();
            } else if (hasHit && !held.empty() && (isBlockItem(held.id) || (placedBlock(held.id) != AIR && held.id != SEEDS &&
                                                                             held.id != PUMPKIN_SEEDS && held.id != MELON_SEEDS))) {
                uint8_t block = isBlockItem(held.id) ? (uint8_t)held.id : placedBlock(held.id);
                glm::ivec3 p = isReplaceable(target) ? hit : prev;
                uint8_t cur = world->getBlock(p.x, p.y, p.z);
                uint8_t meta = 0;
                bool ok = isReplaceable(cur) && p.y >= 0 && p.y < CH;
                uint8_t below = world->getBlock(p.x, p.y - 1, p.z);
                auto waterNear = [&](int wx, int wy, int wz) {
                    return world->getBlock(wx + 1, wy, wz) == WATER || world->getBlock(wx - 1, wy, wz) == WATER ||
                           world->getBlock(wx, wy, wz + 1) == WATER || world->getBlock(wx, wy, wz - 1) == WATER;
                };
                if (block == LOG || block == SAPLING) meta = (uint8_t)(held.damage & 3);
                if (block == LEAVES) meta = (uint8_t)((held.damage & 3) | LEAVES_PLAYER);
                if (block == TALL_GRASS) meta = 1;
                if (block == WOOL || isSlab(block) || block == STONE_BRICK || block == MONSTER_EGG || block == SKULL_BLOCK) meta = (uint8_t)held.damage;
                if (block == PLANKS || block == SANDSTONE) meta = (uint8_t)(held.damage & 3);
                if (block == ANVIL) {
                    // Длинной стороной поперёк взгляда (BlockAnvil.onBlockPlacedBy); биты 2-3 — износ
                    glm::vec3 lk0 = player.look();
                    meta = (uint8_t)((std::abs(lk0.x) > std::abs(lk0.z) ? 0 : 1) | ((held.damage & 3) << 2));
                }
                if (block == SKULL_BLOCK) {
                    if (n.y == 0 && p == prev) {
                        // На стене (ItemSkull 1.4.2): бит 3, биты 4-5 — куда смотрит голова (от стены)
                        int side = n.x == 1 ? 0 : n.z == 1 ? 1 : n.x == -1 ? 2 : 3;
                        meta = (uint8_t)((held.damage & 7) | 8 | (side << 4));
                        ok = ok && isSolid(target);
                    } else {
                        int rot = (int)std::floor((player.yaw + 180.f) / 22.5f + 0.5f) & 15; // лицом к игроку
                        meta = (uint8_t)((held.damage & 7) | (rot << 4));
                        ok = ok && n.y != -1 && isSolid(below); // на потолок в 1.4.2 не ставится
                    }
                }
                if (block == COBBLE_WALL) meta = (uint8_t)(held.damage & 1);
                glm::vec3 lk = player.look();
                uint8_t facing = std::abs(lk.x) > std::abs(lk.z) ? (lk.x > 0 ? 0 : 2) : (lk.z > 0 ? 1 : 3); // +X, +Z, -X, -Z
                if (isStairs(block)) meta = std::abs(lk.x) > std::abs(lk.z) ? (lk.x > 0 ? 0 : 1) : (lk.z > 0 ? 2 : 3);
                // Верхняя половина (1.3+): клик по нижней грани или по верхней половине боковой — полублок наверх,
                // ступени вверх ногами
                if ((isSlab(block) || isStairs(block)) && !isReplaceable(target)) {
                    bool upper = n.y == -1;
                    if (n.y == 0) {
                        glm::vec3 eo = player.eye(), ed = player.look();
                        int ax = n.x != 0 ? 0 : 2;
                        float plane = (float)hit[ax] + (n[ax] > 0 ? 1.f : 0.f);
                        if (std::abs(ed[ax]) > 1e-6f) {
                            float fy = eo.y + ed.y * ((plane - eo[ax]) / ed[ax]) - (float)hit.y;
                            upper = fy > 0.5f;
                        }
                    }
                    if (upper) meta = (uint8_t)(meta | (isSlab(block) ? 8 : 4));
                }
                if (block == FENCE_GATE) meta = facing;
                if (block == LADDER || block == TRAPDOOR) {
                    // Только на боковую грань непрозрачного блока; мета — сторона стены
                    ok = ok && n.y == 0 && isOpaque(target) && p == prev;
                    meta = n.x == -1 ? 0 : n.z == -1 ? 1 : n.x == 1 ? 2 : 3;
                }
                if (block == CAKE) ok = ok && isSolid(below);
                if (block == LILY_PAD) ok = ok && below == WATER;
                if (block == VINE) {
                    // Лоза цепляется к боковой грани сплошного блока
                    ok = ok && n.y == 0 && isOpaque(target) && p == prev;
                    meta = n.z == -1 ? 1 : n.x == 1 ? 2 : n.z == 1 ? 4 : 8;
                }
                if (block == REDSTONE_WIRE || block == STONE_PLATE || block == WOOD_PLATE || isRepeater(block)) ok = ok && isOpaque(below);
                if (isRepeater(block)) meta = facing;
                if (block == LEVER || block == STONE_BUTTON) {
                    ok = ok && isOpaque(target) && p == prev && n.y >= 0 && !(block == STONE_BUTTON && n.y == 1);
                    if (n.y == 1) meta = 5;
                    else if (n.x == 1) meta = TORCH_WEST_WALL;
                    else if (n.x == -1) meta = TORCH_EAST_WALL;
                    else if (n.z == 1) meta = TORCH_NORTH_WALL;
                    else meta = TORCH_SOUTH_WALL;
                }
                if (block == DEAD_BUSH) ok = ok && below == SAND;
                if (block == BROWN_MUSHROOM || block == RED_MUSHROOM)
                    ok = ok && isOpaque(below) && std::max(world->getSkyLight(p.x, p.y, p.z), world->getBlockLight(p.x, p.y, p.z)) < 13;
                if (block == REEDS)
                    ok = ok && (below == REEDS || ((below == GRASS || below == DIRT || below == SAND) && waterNear(p.x, p.y - 1, p.z)));
                if (block == CACTUS)
                    ok = ok && (below == SAND || below == CACTUS) && !isSolid(world->getBlock(p.x + 1, p.y, p.z)) &&
                         !isSolid(world->getBlock(p.x - 1, p.y, p.z)) && !isSolid(world->getBlock(p.x, p.y, p.z + 1)) &&
                         !isSolid(world->getBlock(p.x, p.y, p.z - 1));
                if (block == SNOW_LAYER || block == RAIL || block == POWERED_RAIL || block == DETECTOR_RAIL) ok = ok && isOpaque(below);
                if (block == RAIL || block == POWERED_RAIL || block == DETECTOR_RAIL) {
                    glm::vec3 l = player.look();
                    meta = std::abs(l.x) > std::abs(l.z) ? 1 : 0;
                }

                if (block == TORCH || isRedstoneTorch(block)) {
                    ok = ok && isOpaque(target) && n.y >= 0;
                    if (n.y == 1) meta = TORCH_FLOOR;
                    else if (n.x == 1) meta = TORCH_WEST_WALL;
                    else if (n.x == -1) meta = TORCH_EAST_WALL;
                    else if (n.z == 1) meta = TORCH_NORTH_WALL;
                    else if (n.z == -1) meta = TORCH_SOUTH_WALL;
                }
                if (block == SAPLING || block == ROSE || block == DANDELION || block == TALL_GRASS)
                    ok = ok && (below == GRASS || below == DIRT);
                if (isPiston(block)) {
                    glm::vec3 e = player.eye();
                    glm::vec3 c = glm::vec3(p) + 0.5f;
                    if (std::abs(e.x - c.x) < 2.f && std::abs(e.z - c.z) < 2.f && e.y - p.y > 2.f) meta = 2;
                    else if (std::abs(e.x - c.x) < 2.f && std::abs(e.z - c.z) < 2.f && p.y - (player.pos.y + 1.82f) + 1.f > 0.f) meta = 3;
                    else {
                        glm::vec3 l = -player.look();
                        meta = std::abs(l.x) > std::abs(l.z) ? (l.x > 0 ? 0 : 1) : (l.z > 0 ? 4 : 5);
                    }
                }
                if (block == FURNACE || block == CHEST || block == FURNACE_LIT || block == PUMPKIN || block == JACK_O_LANTERN ||
                    block == DISPENSER || block == ENDER_CHEST) {
                    // «Лицом» к игроку
                    glm::vec3 l = player.look();
                    if (std::abs(l.x) > std::abs(l.z)) meta = l.x > 0 ? 4 : 5;
                    else meta = l.z > 0 ? 2 : 3;
                }
                if (block == CHEST) {
                    ok = ok && world->canPlaceChest(p.x, p.y, p.z);
                    // Рядом сундук — обе половины смотрят в одну сторону, поперёк пары
                    glm::vec3 l = player.look();
                    bool pairX = world->getBlock(p.x - 1, p.y, p.z) == CHEST || world->getBlock(p.x + 1, p.y, p.z) == CHEST;
                    bool pairZ = world->getBlock(p.x, p.y, p.z - 1) == CHEST || world->getBlock(p.x, p.y, p.z + 1) == CHEST;
                    if (pairX) meta = l.z > 0 ? 2 : 3;
                    else if (pairZ) meta = l.x > 0 ? 4 : 5;
                }
                if (isSolid(block)) {
                    glm::vec3 mn = player.pos - glm::vec3(PLAYER_HALF_W, 0, PLAYER_HALF_W);
                    glm::vec3 mx = player.pos + glm::vec3(PLAYER_HALF_W, PLAYER_H, PLAYER_HALF_W);
                    bool inter = mn.x < p.x + 1 && mx.x > p.x && mn.y < p.y + 1 && mx.y > p.y && mn.z < p.z + 1 && mx.z > p.z;
                    // И не внутрь моба (canPlaceEntityOnSide 1.0)
                    for (const Mob& mb : mobMgr.mobs) {
                        if (inter) break;
                        if (mb.dying() || mb.type == MobType::EnderDragon || mb.type == MobType::EnderCrystal) continue;
                        float hw = mobHalfW(mb), hh = mobHeight(mb);
                        inter = mb.pos.x - hw < p.x + 1 && mb.pos.x + hw > p.x && mb.pos.y < p.y + 1 && mb.pos.y + hh > p.y &&
                                mb.pos.z - hw < p.z + 1 && mb.pos.z + hw > p.z;
                    }
                    ok = ok && !inter;
                }
                if (ok) {
                    world->setBlock(p.x, p.y, p.z, block, meta);
                    if (block == CHEST) {
                        world->createTile(p.x, p.y, p.z, TileEntity::Chest);
                        glm::ivec3 ca, cb;
                        if (world->chestPair(p.x, p.y, p.z, ca, cb)) {
                            glm::ivec3 o = ca == p ? cb : ca;
                            if (world->getMeta(o.x, o.y, o.z) != meta) world->setBlock(o.x, o.y, o.z, CHEST, meta);
                        }
                    }
                    if (block == RAIL || block == POWERED_RAIL || block == DETECTOR_RAIL) world->updateRailShape(p.x, p.y, p.z, true);
                    if (block == FURNACE) world->createTile(p.x, p.y, p.z, TileEntity::Furnace);
                    if (block == DISPENSER) world->createTile(p.x, p.y, p.z, TileEntity::Dispenser);
                    if (block == BREWING_STAND) world->createTile(p.x, p.y, p.z, TileEntity::Brewing);
                    if ((block == PUMPKIN || block == JACK_O_LANTERN) && world->getBlock(p.x, p.y - 1, p.z) == SNOW_BLOCK &&
                        world->getBlock(p.x, p.y - 2, p.z) == SNOW_BLOCK) {
                        for (int dy = 0; dy < 3; ++dy) {
                            spawnBreakParticles(particles, p - glm::ivec3(0, dy, 0), world->getBlock(p.x, p.y - dy, p.z));
                            world->setBlock(p.x, p.y - dy, p.z, AIR);
                        }
                        mobMgr.spawn(MobType::SnowGolem, glm::vec3(p.x + 0.5f, (float)p.y - 1.95f, p.z + 0.5f), player.yaw + 180.f);
                    }
                    if ((block == PUMPKIN || block == JACK_O_LANTERN) &&
                        world->getBlock(p.x, p.y - 1, p.z) == IRON_BLOCK && world->getBlock(p.x, p.y - 2, p.z) == IRON_BLOCK) {
                        bool golemX = world->getBlock(p.x - 1, p.y - 1, p.z) == IRON_BLOCK && world->getBlock(p.x + 1, p.y - 1, p.z) == IRON_BLOCK;
                        bool golemZ = world->getBlock(p.x, p.y - 1, p.z - 1) == IRON_BLOCK && world->getBlock(p.x, p.y - 1, p.z + 1) == IRON_BLOCK;
                        if (golemX || golemZ) {
                            glm::ivec3 arm1 = golemX ? glm::ivec3(p.x - 1, p.y - 1, p.z) : glm::ivec3(p.x, p.y - 1, p.z - 1);
                            glm::ivec3 arm2 = golemX ? glm::ivec3(p.x + 1, p.y - 1, p.z) : glm::ivec3(p.x, p.y - 1, p.z + 1);
                            glm::ivec3 blks[5] = {p, p - glm::ivec3(0, 1, 0), p - glm::ivec3(0, 2, 0), arm1, arm2};
                            for (const auto& bp : blks) {
                                spawnBreakParticles(particles, bp, world->getBlock(bp.x, bp.y, bp.z));
                                world->setBlock(bp.x, bp.y, bp.z, AIR);
                            }
                            mobMgr.spawn(MobType::IronGolem, glm::vec3(p.x + 0.5f, (float)p.y - 1.95f, p.z + 0.5f), player.yaw + 180.f);
                        }
                    }
                    if (block == SKULL_BLOCK && (meta & 7) == 1) {
                        for (int axis = 0; axis < 2; ++axis) {
                            for (int ox = -1; ox <= 1; ++ox) {
                                int cx = (axis == 0) ? p.x - ox : p.x;
                                int cz = (axis == 1) ? p.z - ox : p.z;
                                int cy = p.y - 1;
                                auto getB = [&](int dx, int dy, int dz) { return world->getBlock(cx + dx, cy + dy, cz + dz); };
                                auto getM = [&](int dx, int dy, int dz) { return world->getMeta(cx + dx, cy + dy, cz + dz); };
                                if (axis == 0) {
                                    if (getB(0, 0, 0) == SOUL_SAND && getB(0, -1, 0) == SOUL_SAND &&
                                        getB(-1, 0, 0) == SOUL_SAND && getB(1, 0, 0) == SOUL_SAND &&
                                        getB(-1, 1, 0) == SKULL_BLOCK && (getM(-1, 1, 0) & 7) == 1 &&
                                        getB(0, 1, 0) == SKULL_BLOCK && (getM(0, 1, 0) & 7) == 1 &&
                                        getB(1, 1, 0) == SKULL_BLOCK && (getM(1, 1, 0) & 7) == 1) {
                                        glm::ivec3 blks[7] = {
                                            {cx, cy, cz}, {cx, cy - 1, cz}, {cx - 1, cy, cz}, {cx + 1, cy, cz},
                                            {cx - 1, cy + 1, cz}, {cx, cy + 1, cz}, {cx + 1, cy + 1, cz}
                                        };
                                        for (const auto& bp : blks) {
                                            spawnBreakParticles(particles, bp, world->getBlock(bp.x, bp.y, bp.z));
                                            world->setBlock(bp.x, bp.y, bp.z, AIR);
                                        }
                                        Mob& wm = mobMgr.spawn(MobType::Wither, glm::vec3(cx + 0.5f, (float)cy, cz + 0.5f), player.yaw + 180.f);
                                        wm.invulnerable = 220;
                                        wm.health = 1;
                                        goto witherDone;
                                    }
                                } else {
                                    if (getB(0, 0, 0) == SOUL_SAND && getB(0, -1, 0) == SOUL_SAND &&
                                        getB(0, 0, -1) == SOUL_SAND && getB(0, 0, 1) == SOUL_SAND &&
                                        getB(0, 1, -1) == SKULL_BLOCK && (getM(0, 1, -1) & 7) == 1 &&
                                        getB(0, 1, 0) == SKULL_BLOCK && (getM(0, 1, 0) & 7) == 1 &&
                                        getB(0, 1, 1) == SKULL_BLOCK && (getM(0, 1, 1) & 7) == 1) {
                                        glm::ivec3 blks[7] = {
                                            {cx, cy, cz}, {cx, cy - 1, cz}, {cx, cy, cz - 1}, {cx, cy, cz + 1},
                                            {cx, cy + 1, cz - 1}, {cx, cy + 1, cz}, {cx, cy + 1, cz + 1}
                                        };
                                        for (const auto& bp : blks) {
                                            spawnBreakParticles(particles, bp, world->getBlock(bp.x, bp.y, bp.z));
                                            world->setBlock(bp.x, bp.y, bp.z, AIR);
                                        }
                                        Mob& wm = mobMgr.spawn(MobType::Wither, glm::vec3(cx + 0.5f, (float)cy, cz + 0.5f), player.yaw + 180.f);
                                        wm.invulnerable = 220;
                                        wm.health = 1;
                                        goto witherDone;
                                    }
                                }
                            }
                        }
                        witherDone:;
                    }
                    audio.playDig(block, glm::vec3(p) + 0.5f);
                    consumeHeld();
                    startSwing();
                }
            }
        }
        g_in.placeClick = false;

        // Средняя кнопка: выбрать блок (в творческом — взять в руку)
        if (g_in.pickClick && hasHit) {
            uint16_t want = target == FURNACE_LIT ? FURNACE : target == REEDS ? REEDS_ITEM : isDoubleSlab(target) ? singleSlabOf(target)
                          : target == WOOD_DOOR ? WOOD_DOOR_ITEM : target == IRON_DOOR ? IRON_DOOR_ITEM : target == CAKE ? CAKE_ITEM
                          : target == COCOA ? DYE : target == REDSTONE_TORCH_OFF ? REDSTONE_TORCH_ON : target;
            // Блоки, которые ставятся предметом (провод, повторитель, горшок, морковь, голова…) — ищем этот предмет
            uint8_t placedAs = target == REPEATER_ON ? REPEATER_OFF : target;
            if (want == target && isItemOnlyBlock(placedAs) && placedAs != REDSTONE_TORCH_OFF)
                for (uint16_t id = BLOCK_COUNT; id < ITEM_ID_LIMIT; ++id)
                    if (isValidItem(id) && placedBlock(id) == placedAs) { want = id; break; }
            uint8_t tm = world->getMeta(hit.x, hit.y, hit.z);
            uint16_t variant = target == COCOA ? DYE_COCOA : target == SKULL_BLOCK ? (uint16_t)(tm & 7) : !blockHasVariants(target) ? 0
                             : (target == WOOL || target == MONSTER_EGG) ? (uint16_t)(tm & 15)
                             : (target == SLAB || target == DOUBLE_SLAB) ? (uint16_t)(tm & 7) : (uint16_t)(tm & 3);
            int found = -1;
            for (int i = 0; i < 9; ++i) if (inv.slots[i].id == want && inv.slots[i].damage == variant) found = i;
            if (found >= 0) g_in.selected = found;
            else if (player.creative() && want != 0 && want != WHEAT && want != FARMLAND && want != MOB_SPAWNER && isValidItem(want) &&
                     !(want < BLOCK_COUNT && isItemOnlyBlock((uint8_t)want)))
                inv.slots[g_in.selected] = makeStack(want, 64, variant);
        }
        g_in.pickClick = false;

        // Q — выбросить предмет (Ctrl+Q — всю стопку)
        if (g_in.dropKey) {
            if (!held.empty()) {
                ItemStack s = held;
                if (!g_in.dropStack) s.count = 1;
                held.count = (uint8_t)(held.count - s.count);
                if (held.count == 0) held.clear();
                throwItem(s);
                startSwing();
            }
            g_in.dropKey = false;
        }

        // Сорванные без опоры блоки выпадают предметами
        for (auto& pp : world->popped) {
            audio.playDig(pp.block, glm::vec3(pp.pos) + 0.5f);
            spawnBreakParticles(particles, pp.pos, pp.block, pp.meta);
            if (!player.creative() && !mp)
                for (const ItemStack& d : blockDrops(pp.block, pp.meta, gameRng)) dropFromBlock(items, pp.pos, d, gameRng);
        }
        world->popped.clear();

        // ---- Выпавшие предметы: физика и подбор
        if (!mp && tickItems(items, *world) > 0) audio.play("random/fizz", 0.4f, 2.f + rnd() * 0.4f);
        for (auto& f : pickupFx) ++f.age;
        pickupFx.erase(std::remove_if(pickupFx.begin(), pickupFx.end(), [](const ItemEntity& f) { return f.age > 3; }), pickupFx.end());
        if (!player.dead && !mp) {
            bool picked = false;
            for (auto& e : items) {
                if (e.pickupDelay > 0 || e.dead) continue;
                glm::vec3 d = e.pos - player.pos;
                if (std::abs(d.x) < 1.3f && std::abs(d.z) < 1.3f && d.y > -0.6f && d.y < 2.4f) {
                    int before = e.stack.count;
                    ItemEntity fly = e; // копия для анимации подлёта (EntityPickupFX 1.0)
                    inv.add(e.stack);
                    if (e.stack.count != before) {
                        fly.stack.count = (uint8_t)(before - e.stack.count);
                        fly.age = 0;
                        pickupFx.push_back(fly);
                    }
                    if (e.stack.empty()) e.dead = true;
                    if (e.stack.count != before || e.dead) picked = true;
                }
            }
            if (picked) audio.play("random/pop", 0.2f, ((rnd() - rnd()) * 0.7f + 1.f) * 2.f);
        }

        // ---- Печи работают, даже если окно закрыто (в сетевой игре — на сервере)
        if (!mp) tickTileEntities(*world);

        // ---- Рост растений, трава, грядки
        world->randomTick(player.pos, gameRng, rainStrength > 0.3f);

        // ---- Случайные эффекты блоков рядом (randomDisplayTick): дым факелов и печей, лава
        glm::ivec3 pc((int)std::floor(player.pos.x), (int)std::floor(player.pos.y), (int)std::floor(player.pos.z));
        for (int i = 0; i < 1000; ++i) {
            glm::ivec3 q = pc + glm::ivec3((int)(rnd() * 32) - 16, (int)(rnd() * 32) - 16, (int)(rnd() * 32) - 16);
            uint8_t b = world->getBlock(q.x, q.y, q.z);
            if ((b == TORCH || b == FURNACE_LIT) && (opt.particles == 2 || (opt.particles == 1 && rnd() < 0.5f))) continue;
            if (b == TORCH) {
                glm::vec3 fp = glm::vec3(q) + glm::vec3(0.5f, 0.7f, 0.5f);
                switch (world->getMeta(q.x, q.y, q.z)) {
                case TORCH_WEST_WALL: fp += glm::vec3(-0.27f, 0.22f, 0); break;
                case TORCH_EAST_WALL: fp += glm::vec3(0.27f, 0.22f, 0); break;
                case TORCH_NORTH_WALL: fp += glm::vec3(0, 0.22f, -0.27f); break;
                case TORCH_SOUTH_WALL: fp += glm::vec3(0, 0.22f, 0.27f); break;
                default: break;
                }
                spawnSmoke(particles, fp, false);
                spawnSmoke(particles, fp, true);
            } else if (b == FURNACE_LIT) {
                // Огонь и дым перед дверцей горящей печи
                glm::vec3 f(0.f);
                switch (world->getMeta(q.x, q.y, q.z)) {
                case 2: f = {0, 0, -0.52f}; break;
                case 3: f = {0, 0, 0.52f}; break;
                case 4: f = {-0.52f, 0, 0}; break;
                default: f = {0.52f, 0, 0}; break;
                }
                glm::vec3 side = glm::vec3(std::abs(f.z), 0, std::abs(f.x)) * ((rnd() - 0.5f) * 1.2f);
                glm::vec3 fp = glm::vec3(q) + glm::vec3(0.5f, rnd() * 6.f / 16.f, 0.5f) + f + side;
                spawnSmoke(particles, fp, false);
                spawnSmoke(particles, fp, true);
            } else if (b == FIRE) {
                if (rnd() < 1.f / 24.f) {
                    glm::vec3 fp = glm::vec3(q) + 0.5f;
                    audio.play("fire/fire", 1.f + rnd(), rnd() * 0.7f + 0.3f, &fp);
                }
                for (int s = 0; s < 3; ++s) {
                    spawnSmoke(particles, glm::vec3(q) + glm::vec3(rnd(), rnd() * 0.5f + 0.5f, rnd()), false);
                    particles.back().scale *= 2.5f; // «largesmoke»
                }
            } else if (b == PORTAL) {
                // BlockPortal.randomDisplayTick: гул портала и фиолетовые частицы, летящие из пластины
                if (rnd() < 0.01f) {
                    glm::vec3 sp = glm::vec3(q) + 0.5f;
                    audio.play("portal/portal", 1.f, rnd() * 0.4f + 0.8f, &sp);
                }
                bool alongX = world->getBlock(q.x - 1, q.y, q.z) == PORTAL || world->getBlock(q.x + 1, q.y, q.z) == PORTAL;
                for (int l = 0; l < 4; ++l) {
                    glm::vec3 pp = glm::vec3(q) + glm::vec3(rnd(), rnd(), rnd());
                    glm::vec3 mv((rnd() - 0.5f) * 0.5f, (rnd() - 0.5f) * 0.5f, (rnd() - 0.5f) * 0.5f);
                    float side = rnd() < 0.5f ? -1.f : 1.f;
                    if (alongX) { pp.z = q.z + 0.5f + 0.25f * side; mv.z = rnd() * 2.f * side; }
                    else { pp.x = q.x + 0.5f + 0.25f * side; mv.x = rnd() * 2.f * side; }
                    spawnPortalParticle(particles, pp, mv);
                }
            } else if (b == LAVA && world->getBlock(q.x, q.y + 1, q.z) == AIR && rnd() < 0.01f) {
                glm::vec3 lp = glm::vec3(q) + glm::vec3(rnd(), 1.f, rnd());
                audio.play("liquid/lavapop", 0.2f + rnd() * 0.2f, 0.9f + rnd() * 0.15f, &lp);
                spawnLava(particles, lp);
            }
        }
        for (const Mob& dm : mobMgr.mobs)
            if (dm.type == MobType::EnderDragon && dm.deathTime >= 180 && dm.deathTime <= 200) {
                // EntityDragon: hugeexplosion каждый тик — 6 больших клубов вокруг
                glm::vec3 c = dm.pos + glm::vec3((rnd() - 0.5f) * 8.f, 2.f + (rnd() - 0.5f) * 4.f, (rnd() - 0.5f) * 8.f);
                for (int i = 0; i < 6; ++i) spawnLargeExplode(particles, c + glm::vec3(rnd() - rnd(), rnd() - rnd(), rnd() - rnd()) * 4.f, (float)i / 6.f);
            }
        tickParticles(particles, *world);

        // Покачивание камеры
        prevBobAmp = bobAmp;
        float speed = glm::length(glm::vec2(player.pos.x - player.prevPos.x, player.pos.z - player.prevPos.z));
        float targetBob = (player.onGround && !player.fly) ? std::min(0.1f, speed) : 0.f;
        bobAmp += (targetBob - bobAmp) * 0.4f;

        // Автосохранение раз в 5 минут
        if (worldTime % 6000 == 0 && !mp) { world->save(savePath, currentSave()); mobMgr.save(entitiesPath, player, &items); saveMaps(); saves.writeInfo(currentWorld); }
    };

    // Применение настроек к миру, звуку и мобам
    auto applyOptions = [&]() {
        audio.sfxVolume = opt.sound;
        audio.setMusicVolume(opt.music);
        gamma = opt.gamma;
        for (int i = 0; i < KB_COUNT; ++i) g_keys[i] = opt.keys[i];
        glfwSwapInterval(opt.performance == 0 ? 0 : 1); // Max FPS — без вертикальной синхронизации
        mobMgr.difficulty = currentWorld.hardcore ? 3 : opt.difficulty;
        player.starveFloor = mobMgr.difficulty == 1 ? 10 : mobMgr.difficulty == 2 ? 1 : 0;
        if (world) {
            bool remesh = world->smoothLighting != opt.smoothLighting || world->fancyGraphics != opt.fancy;
            world->smoothLighting = opt.smoothLighting;
            world->fancyGraphics = opt.fancy;
            world->renderDistance = opt.chunkDistance();
            if (remesh) for (auto& [key, ch] : world->chunks) ch->dirty = true;
        }
        opt.save(optionsPath);
    };

    auto startWorld = [&](const std::string& folder, const NewWorldRequest* nw) {
        WorldInfo info;
        bool haveInfo = saves.readInfo(folder, info);
        dimension = (!nw && haveInfo) ? info.dimension : 0;
        worldFolder = folder;
        auto dimFile = [&](const char* base) {
            std::string pre = dimension == -1 ? "nether_" : dimension == 1 ? "end_" : "";
            return saves.path(folder) + pre + base;
        };
        savePath = dimFile("world.sav");
        entitiesPath = dimFile("entities.sav");
        portalTimer = portalCooldown = 0;
        pendingPortal = pendingSpawnCheck = false;
        uint32_t randomSeed = (uint32_t)std::chrono::high_resolution_clock::now().time_since_epoch().count();
        if (nw) {
            info = WorldInfo{};
            info.folder = folder;
            info.name = nw->name;
            info.gameMode = nw->mode == 2 ? 1 : 0;
            info.hardcore = nw->mode == 1;
            info.seed = seedFromText(nw->seedText, randomSeed);
            info.generator = 3; // новые миры — генератор 1.0 (+ родники и большие дубы)
            info.cheats = nw->cheats && !info.hardcore;
        } else if (!haveInfo) {
            info.folder = info.name = folder;
            info.seed = randomSeed;
            info.generator = 3;
        }
        seed = info.seed;
        player = Player{};
        inv = Inventory{};
        st = World::SaveState{};
        mobMgr = MobManager{};
        items.clear();
        particles.clear();
        world = std::make_unique<World>(seed, dimension);
        world->setGenVersion(info.generator);
        if (!nw && world->load(savePath, st)) {
            player.pos = player.prevPos = st.pos;
            player.yaw = st.yaw;
            player.pitch = st.pitch;
            player.health = st.health;
            player.food = st.food;
            player.air = st.air;
            player.saturation = st.saturation;
            player.mode = (GameMode)st.gameMode;
            if (player.health <= 0) { player.health = 0; player.dead = true; player.deathTicks = 20; }
            inv = st.inventory;
            mobMgr.load(entitiesPath, player, &items);
            seed = info.seed = world->seed();
        } else if (!nw && (fileExistsUtf8(savePath) || fileExistsUtf8(savePath + ".bak"))) {
            // Файл мира есть, но не читается — не начинаем мир заново (автосохранение затёрло бы его)
            world.reset();
            menu.disconnectTitle = "Could not load the world '" + info.name + "'";
            menu.disconnectReason = "world.sav is damaged. The file was left untouched.";
            menu.page = MenuSystem::Page::Disconnected;
            openScreen(win, Screen::Menu);
            return;
        } else {
            // Ищем сушу для спавна
            int sx = 0, sz = 0;
            for (int i = 0; i < 400; ++i) {
                int x = (int)(hash32(seed + i * 2) % 2000) - 1000, z = (int)(hash32(seed + i * 2 + 1) % 2000) - 1000;
                int h = world->terrainHeight(x, z);
                if (h > SEA + 2 && h < SEA + 20) { sx = x; sz = z; break; }
            }
            st.spawn = glm::vec3(sx + 0.5f, world->terrainHeight(sx, sz) + 1.0f, sz + 0.5f);
            st.worldTime = 0;
            pendingSpawnCheck = true; // рельеф выше — без пещер и озёр; настоящую сушу найдём после загрузки
            player.pos = player.prevPos = st.spawn;
            player.mode = info.gameMode == 1 ? GameMode::Creative : GameMode::Survival;
            glm::ivec3 vil;
            if (std::getenv("MC_SHOW_VILLAGE") && world->findVillage((int)st.spawn.x, (int)st.spawn.z, vil)) {
                player.pos = player.prevPos = st.spawn = glm::vec3(vil.x + 6.5f, (float)vil.y + 12.f, vil.z + 6.5f);
                pendingSpawnCheck = false;
            }
        }
        worldTime = st.worldTime;
        raining = st.raining != 0;
        thundering = st.thundering != 0;
        rainTime = st.rainTime;
        thunderTime = st.thunderTime;
        rainStrength = prevRain = raining ? 1.f : 0.f;
        thunderStrength = prevThunder = raining && thundering ? 1.f : 0.f;
        lightningTicks = 0;
        spawnPoint = st.spawn;
        gameRng = seed | 1u;
        mining = Mining{};
        breakDelay = placeDelay = useTicks = bowCharge = 0;
        swing = prevSwing = 1.f;
        tickAcc = 0.0;
        info.lastPlayed = nowSeconds();
        info.dimension = dimension;
        saves.writeInfo(info);
        currentWorld = info;
        applyOptions();
        inGame = true;
        loading = true;
        loadingStart = glfwGetTime();
        menu.page = MenuSystem::Page::Loading;
        menu.loadingTitle = nw ? "Generating level" : "Loading level";
        menu.loadingStage = "Building terrain";
        openScreen(win, Screen::Menu);
    };

    // Переход через портал: сохранить текущее измерение, загрузить или создать другое, координаты x8 / :8
    auto switchDimension = [&](int target) {
        if (!mp) {
            world->save(savePath, currentSave());
            mobMgr.save(entitiesPath, player, &items); saveMaps();
        }
        int from = dimension;
        dimension = target;
        std::string pre = target == -1 ? "nether_" : target == 1 ? "end_" : "";
        savePath = saves.path(worldFolder) + pre + "world.sav";
        entitiesPath = saves.path(worldFolder) + pre + "entities.sav";
        glm::vec3 p = player.pos;
        float k = (from != -1 && target == -1) ? 1.f / 8.f : (from == -1 && target == 0) ? 8.f : 1.f;
        pendingEnd = target == 1;
        pendingRespawn = from == 1 && target == 0; // вышли из Края через портал выхода
        mpPendingChunks.clear();
        world = std::make_unique<World>(seed, dimension);
        world->setGenVersion(currentWorld.generator);
        World::SaveState tmp;
        if (mp) {
            // Сетевая игра: мир измерения ведёт сервер
            world->remote = true;
            world->sendEdit = [&](int x, int y, int z, uint8_t b, uint8_t m, bool onlyMeta) {
                net::Writer e;
                e.i32(x); e.u8((uint8_t)y); e.i32(z); e.u8(b); e.u8(m); e.u8(onlyMeta ? 1 : 0);
                netConn.send(C_SET_BLOCK, e);
            };
            net::Writer dw; dw.u8((uint8_t)(int8_t)target);
            netConn.send(C_DIM, dw);
        } else {
            world->load(savePath, tmp); // правки блоков измерения; состояние игрока берём текущее
        }
        // Точка появления мира хранится в файле обычного мира (игру могли загрузить сразу в Незере)
        if (target == 0 && glm::length(tmp.spawn) > 0.f) spawnPoint = tmp.spawn;
        int diff = mobMgr.difficulty;
        mobMgr = MobManager{};
        mobMgr.difficulty = diff;
        Player dummy;
        dummy.pos = target == 1 ? glm::vec3(100.5f, 49.f, 0.5f) : glm::vec3(p.x * k, p.y, p.z * k); // где окажемся (для прореживания монстров)
        items.clear();
        if (!mp) mobMgr.load(entitiesPath, dummy, &items);
        particles.clear();
        mining = Mining{};
        plateTimers.clear();
        player.pos = player.prevPos = glm::vec3(std::floor(p.x * k) + 0.5f, target == -1 ? 70.f : p.y, std::floor(p.z * k) + 0.5f);
        player.motion = glm::vec3(0.f);
        player.fallDistance = 0.f;
        pendingPortal = target == -1 || (target == 0 && from == -1);
        if (target == 1) player.pos = player.prevPos = glm::vec3(100.5f, 49.f, 0.5f);
        portalTimer = 0;
        portalCooldown = 100;
        applyOptions();
        currentWorld.dimension = dimension;
        saves.writeInfo(currentWorld);
        loading = true;
        loadingStart = glfwGetTime();
        menu.page = MenuSystem::Page::Loading;
        menu.loadingTitle = target == -1 ? "Entering the Nether" : target == 1 ? "Entering the End" : from == 1 ? "Leaving the End" : "Leaving the Nether";
        menu.loadingStage = "Building terrain";
        openScreen(win, Screen::Menu);
    };

    switchDimensionFn = switchDimension;

    // Найти портал рядом (радиус 64) или построить новый с обсидиановой площадкой
    auto findOrBuildPortal = [&]() {
        glm::ivec3 c((int)std::floor(player.pos.x), (int)std::floor(player.pos.y), (int)std::floor(player.pos.z));
        glm::ivec3 best(0);
        float bestD = 1e9f;
        for (int dz = -64; dz <= 64; ++dz)
            for (int dx = -64; dx <= 64; ++dx) {
                int x = c.x + dx, z = c.z + dz;
                if (!world->isChunkLoaded(floorDiv(x, CW), floorDiv(z, CW))) continue;
                for (int y = 1; y < CH - 1; ++y)
                    if (world->getBlock(x, y, z) == PORTAL && world->getBlock(x, y - 1, z) != PORTAL) {
                        float d = (float)(dx * dx + dz * dz + (y - c.y) * (y - c.y));
                        if (d < bestD) { bestD = d; best = {x, y, z}; }
                    }
            }
        if (bestD < 1e8f) {
            bool alongX = world->getMeta(best.x, best.y, best.z) == 0;
            player.pos = player.prevPos = glm::vec3(best) + glm::vec3(0.5f, 0.f, 0.5f) + (alongX ? glm::vec3(0, 0, 1.2f) : glm::vec3(1.2f, 0, 0));
            if (isSolid(world->getBlock((int)std::floor(player.pos.x), best.y, (int)std::floor(player.pos.z))))
                player.pos = player.prevPos = glm::vec3(best) + glm::vec3(0.5f, 0.f, 0.5f);
            return;
        }
        // Место: сверху вниз ищем воздух над твёрдым, начиная с высоты игрока
        int y0 = -1;
        for (int y = std::min(CH - 6, c.y + 20); y > 8 && y0 < 0; --y)
            if (world->getBlock(c.x, y, c.z) == AIR && world->getBlock(c.x, y + 1, c.z) == AIR && isSolid(world->getBlock(c.x, y - 1, c.z)) &&
                !isLiquid(world->getBlock(c.x, y - 1, c.z)))
                y0 = y;
        if (y0 < 0) y0 = std::clamp(c.y, 40, 100);
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 2; ++dx) {
                world->setBlock(c.x + dx, y0 - 1, c.z + dz, OBSIDIAN);
                for (int dy = 0; dy <= 3; ++dy) world->setBlock(c.x + dx, y0 + dy, c.z + dz, AIR);
            }
        for (int dx = -1; dx <= 2; ++dx) { world->setBlock(c.x + dx, y0 - 1, c.z, OBSIDIAN); world->setBlock(c.x + dx, y0 + 3, c.z, OBSIDIAN); }
        for (int dy = 0; dy < 3; ++dy) { world->setBlock(c.x - 1, y0 + dy, c.z, OBSIDIAN); world->setBlock(c.x + 2, y0 + dy, c.z, OBSIDIAN); }
        world->tryCreatePortal(c.x, y0, c.z);
        player.pos = player.prevPos = glm::vec3(c.x + 0.5f, (float)y0, c.z + 1.5f);
    };

    auto leaveWorld = [&](bool save) {
        if (!world) return;
        if (mp) { // сетевая игра: мир хранит сервер, игрока — тоже он
            save = false;
            if (savePlayerFn) savePlayerFn();
            netConn.flush();
            netConn.close();
            mp = false;
            others.clear();
        }
        if (g_in.screen == Screen::Container) { GuiContext ctx = makeCtx(0, 0); gui.close(ctx); }
        if (save) {
            world->save(savePath, currentSave());
            mobMgr.save(entitiesPath, player, &items); saveMaps();
            currentWorld.lastPlayed = nowSeconds();
            saves.writeInfo(currentWorld);
        }
        world.reset();
        items.clear();
        particles.clear();
        inGame = false;
        loading = false;
        menu.open(MenuSystem::Page::Title, saves);
        openScreen(win, Screen::Menu);
    };

    // ---- Сетевая игра: подключение, вход и отключение
    auto disconnectToMenu = [&](const std::string& title, const std::string& reason) {
        if (world) leaveWorld(false);
        netConn.close();
        mp = false;
        others.clear();
        menu.disconnectTitle = title;
        menu.disconnectReason = reason;
        menu.page = MenuSystem::Page::Disconnected;
        openScreen(win, Screen::Menu);
    };
    auto startMultiplayer = [&](const std::string& addr, const std::string& name) {
        std::string host = addr;
        int port = DEFAULT_PORT;
        size_t colon = addr.rfind(':');
        if (colon != std::string::npos) { host = addr.substr(0, colon); port = std::atoi(addr.c_str() + colon + 1); }
        std::string err;
        if (!netConn.connect(host, port, 4000, err)) {
            disconnectToMenu("Failed to connect to the server", err + " - check the IP and the host's Windows Firewall");
            return;
        }
        net::Writer lw;
        lw.u16(PROTOCOL_VERSION);
        lw.str(name);
        netConn.send(C_LOGIN, lw);
        netConn.flush();
        // Ждём ответа сервера (до 6 секунд)
        std::vector<uint8_t> data;
        uint16_t type = 0;
        bool ok = false;
        double t0 = glfwGetTime();
        while (glfwGetTime() - t0 < 6.0 && netConn.alive) {
            netConn.poll();
            if (netConn.next(type, data)) {
                if (type == S_LOGIN_OK) { ok = true; break; }
                if (type == S_KICK) { net::Reader r(data); disconnectToMenu("Disconnected by server", r.str()); return; }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (!ok) { disconnectToMenu("Failed to connect to the server", netConn.alive ? "The server did not answer" : "Connection closed"); return; }
        net::Reader r(data);
        myId = r.u32();
        uint32_t sseed = r.u32();
        int gen = r.u8();
        int64_t wt = r.i64();
        bool rn = r.u8() != 0, th = r.u8() != 0;
        glm::vec3 sp = readVec3(r);
        int gm = r.u8(), diff = r.u8();
        std::string motd = r.str();
        mpPvp = r.u8() != 0;
        mpOp = r.more() && r.u8() != 0;
        bool mpHardcore = r.more() && r.u8() != 0;
        playerList.clear();
        mp = true;
        others.clear();
        mpPendingChunks.clear();
        dimension = 0;
        worldFolder.clear();
        savePath.clear();
        entitiesPath.clear();
        portalTimer = portalCooldown = 0;
        pendingPortal = false;
        seed = sseed;
        player = Player{};
        inv = Inventory{};
        mobMgr = MobManager{};
        items.clear();
        particles.clear();
        world = std::make_unique<World>(seed, 0);
        world->setGenVersion(gen);
        world->remote = true;
        world->sendEdit = [&](int x, int y, int z, uint8_t b, uint8_t m, bool onlyMeta) {
            net::Writer e;
            e.i32(x); e.u8((uint8_t)y); e.i32(z); e.u8(b); e.u8(m); e.u8(onlyMeta ? 1 : 0);
            netConn.send(C_SET_BLOCK, e);
        };
        player.pos = player.prevPos = sp;
        player.mode = gm == 1 ? GameMode::Creative : GameMode::Survival;
        worldTime = wt;
        raining = rn;
        thundering = th;
        rainStrength = prevRain = raining ? 1.f : 0.f;
        thunderStrength = prevThunder = raining && thundering ? 1.f : 0.f;
        lightningTicks = 0;
        spawnPoint = sp;
        gameRng = seed | 1u;
        mining = Mining{};
        breakDelay = placeDelay = useTicks = bowCharge = 0;
        swing = prevSwing = 1.f;
        tickAcc = 0.0;
        currentWorld = WorldInfo{};
        currentWorld.name = "Multiplayer";
        currentWorld.seed = sseed;
        currentWorld.generator = gen;
        currentWorld.gameMode = gm;
        currentWorld.hardcore = mpHardcore;
        applyOptions();
        mobMgr.difficulty = diff;
        chatLog.push_back({motd, glfwGetTime()});
        inGame = true;
        loading = true;
        loadingStart = glfwGetTime();
        menu.page = MenuSystem::Page::Loading;
        menu.loadingTitle = "Logging in";
        menu.loadingStage = "Downloading terrain";
        openScreen(win, Screen::Menu);
    };
    // Снимок сущностей от сервера: мобы, предметы, стрелы, шары, броски, динамит, транспорт.
    // Прежние копии сохраняют «предыдущую» позицию для плавности и анимацию шагов
    auto applyEntities = [&](net::Reader& r) {
        std::unordered_map<uint32_t, Mob> oldMobs;
        for (auto& m : mobMgr.mobs) oldMobs[m.id] = m;
        std::vector<Mob> ms;
        int n = r.u16();
        ms.reserve(n);
        for (int i = 0; i < n && r.ok; ++i) {
            uint32_t id = r.u32();
            MobType type = (MobType)r.u8();
            glm::vec3 pos = readVec3(r);
            float yaw = r.f32();
            uint16_t f = r.u16();
            int hurt = r.u8(), death = r.u8(), health = r.u16();
            int color = r.u8(), size = r.u8(), hb = r.u8(), hm = r.u8(), fuse = r.u8(), sw = r.u8(), cd = r.u8();
            int counter = (int8_t)r.u8();
            float wing = r.f32(), squish = r.f32(), tent = r.f32(), sqp = r.f32(), sqr = r.f32();
            uint32_t target = r.u32();
            Mob m;
            auto it = oldMobs.find(id);
            bool had = it != oldMobs.end() && it->second.type == type;
            if (had) m = it->second;
            else { m.id = id; m.type = type; m.pos = pos; m.yaw = yaw; m.wingFlap = wing; m.squish = squish; m.tentacle = tent; m.squidPitch = sqp; m.squidRoll = sqr; }
            m.prev = m.pos; m.prevYaw = m.yaw;
            m.prevFuse = m.fuse; m.prevWingFlap = m.wingFlap; m.prevSquish = m.squish; m.prevTentacle = m.tentacle;
            m.prevSquidPitch = m.squidPitch; m.prevSquidRoll = m.squidRoll; m.prevSwing = m.swing;
            m.pos = pos; m.yaw = yaw;
            m.deathTime = (f & 1) ? death : -1;
            m.angry = f & 2; m.sitting = f & 4; m.saddled = f & 8; m.sheared = f & 16; m.tamed = f & 32; m.aiming = f & 64;
            m.fireTicks = (f & 128) ? 20 : 0; m.ridden = f & 256; m.charged = f & 1024;
            m.growingAge = (f & 512) ? -1 : 0;
            m.hurtTime = hurt; m.health = health; m.color = color; m.size = std::max(1, size);
            m.scale = (type == MobType::Slime || type == MobType::MagmaCube) ? (float)m.size : ((f & 512) ? 0.5f : 1.f);
            m.heldBlock = (uint8_t)hb; m.heldMeta = (uint8_t)hm; m.fuse = fuse;
            m.swingTicks = sw - 1; m.swing = m.swingTicks >= 0 ? m.swingTicks / 8.f : 0.f;
            m.attackCooldown = cd; m.attackCounter = counter;
            m.wingFlap = wing; m.squish = squish; m.tentacle = tent; m.squidPitch = sqp; m.squidRoll = sqr;
            m.targetId = target;
            // Шаги: размах ног по пройденному пути
            float dist = had ? glm::length(glm::vec2(m.pos.x - m.prev.x, m.pos.z - m.prev.z)) : 0.f;
            m.prevLimbAmount = m.limbAmount;
            m.limbAmount += (std::min(dist * 4.f, 1.f) - m.limbAmount) * 0.4f;
            m.limbSwing += m.limbAmount;
            ++m.age;
            ms.push_back(m);
            if (had) oldMobs.erase(it);
        }
        // Пропавшие умиравшие мобы — облачко дыма
        for (auto& [id, m] : oldMobs)
            if (m.dying()) spawnPoof(particles, m.pos + glm::vec3(0, mobHeight(m) * 0.5f, 0), 20, mobHalfW(m) * 2.f + 0.5f);
        mobMgr.mobs.swap(ms);
        // Предметы
        std::unordered_map<uint32_t, glm::vec3> oldPos;
        for (auto& e : items) oldPos[e.id] = e.pos;
        items.clear();
        n = r.u16();
        for (int i = 0; i < n && r.ok; ++i) {
            ItemEntity e;
            e.id = r.u32();
            e.pos = readVec3(r);
            e.age = r.u16();
            e.bobOffset = r.f32();
            e.stack = readItem(r);
            auto it = oldPos.find(e.id);
            e.prev = it != oldPos.end() ? it->second : e.pos;
            e.motion = glm::vec3(0.f);
            items.push_back(e);
        }
        // Стрелы
        std::unordered_map<uint32_t, glm::vec3> old2;
        for (auto& a : mobMgr.arrows) old2[a.id] = a.pos;
        mobMgr.arrows.clear();
        n = r.u16();
        for (int i = 0; i < n && r.ok; ++i) {
            Arrow a;
            a.id = r.u32();
            a.pos = readVec3(r);
            a.dir = readVec3(r);
            auto it = old2.find(a.id);
            a.prev = it != old2.end() ? it->second : a.pos;
            mobMgr.arrows.push_back(a);
        }
        // Огненные шары
        old2.clear();
        for (auto& fb : mobMgr.fireballs) old2[fb.id] = fb.pos;
        mobMgr.fireballs.clear();
        n = r.u16();
        for (int i = 0; i < n && r.ok; ++i) {
            Fireball fb;
            fb.id = r.u32();
            fb.pos = readVec3(r);
            fb.small = r.u8() != 0;
            auto it = old2.find(fb.id);
            fb.prev = it != old2.end() ? it->second : fb.pos;
            // Дымный след, как на сервере
            spawnSmoke(particles, fb.pos + glm::vec3(0, fb.small ? 0.1f : 0.3f, 0), false);
            mobMgr.fireballs.push_back(fb);
        }
        // Брошенные предметы
        old2.clear();
        for (auto& t : mobMgr.throwables) old2[t.id] = t.pos;
        mobMgr.throwables.clear();
        n = r.u16();
        for (int i = 0; i < n && r.ok; ++i) {
            Throwable t;
            t.id = r.u32();
            t.pos = readVec3(r);
            t.item = r.u16();
            t.damage = r.u16();
            auto it = old2.find(t.id);
            t.prev = it != old2.end() ? it->second : t.pos;
            mobMgr.throwables.push_back(t);
        }
        // Динамит
        old2.clear();
        for (auto& t : mobMgr.tnts) old2[t.id] = t.pos;
        mobMgr.tnts.clear();
        n = r.u16();
        for (int i = 0; i < n && r.ok; ++i) {
            PrimedTnt t;
            t.id = r.u32();
            t.pos = readVec3(r);
            t.fuse = r.u16();
            auto it = old2.find(t.id);
            t.prev = it != old2.end() ? it->second : t.pos;
            mobMgr.tnts.push_back(t);
        }
        // Транспорт
        std::unordered_map<uint32_t, Vehicle> oldV;
        for (auto& v : mobMgr.vehicles) oldV[v.id] = v;
        mobMgr.vehicles.clear();
        n = r.u16();
        for (int i = 0; i < n && r.ok; ++i) {
            Vehicle v;
            v.id = r.u32();
            v.kind = (VehicleKind)r.u8();
            v.pos = readVec3(r);
            v.yaw = r.f32();
            v.hurtTime = r.u8();
            v.fuel = r.u8() ? 1 : 0;
            auto it = oldV.find(v.id);
            v.prev = it != oldV.end() ? it->second.pos : v.pos;
            v.prevYaw = it != oldV.end() ? it->second.yaw : v.yaw;
            mobMgr.vehicles.push_back(v);
        }
        // Картины
        mobMgr.paintings.clear();
        n = r.u16();
        for (int i = 0; i < n && r.ok; ++i) {
            Painting pt;
            pt.id = r.u32();
            pt.wall.x = r.i32(); pt.wall.y = r.u8(); pt.wall.z = r.i32();
            pt.dir = r.u8() & 3;
            pt.art = std::min<int>(r.u8(), PAINTING_ART_COUNT - 1);
            pt.frame = r.u8() != 0;
            if (pt.frame) { pt.rotation = r.u8() & 3; pt.item = readItem(r); }
            mobMgr.paintings.push_back(pt);
        }
        // Падающий песок и гравий
        std::unordered_map<uint32_t, glm::vec3> oldF;
        for (auto& f : mobMgr.falling) oldF[f.id] = f.pos;
        mobMgr.falling.clear();
        n = r.u16();
        for (int i = 0; i < n && r.ok; ++i) {
            FallingBlock f;
            f.id = r.u32();
            f.pos = readVec3(r);
            f.block = r.u8();
            auto it = oldF.find(f.id);
            f.prev = it != oldF.end() ? it->second : f.pos;
            mobMgr.falling.push_back(f);
        }
        // Шары опыта
        std::unordered_map<uint32_t, XpOrb> oldO;
        for (auto& o : mobMgr.orbs) oldO[o.id] = o;
        mobMgr.orbs.clear();
        n = r.u16();
        for (int i = 0; i < n && r.ok; ++i) {
            XpOrb o;
            o.id = r.u32();
            o.pos = readVec3(r);
            o.value = r.u16();
            auto it = oldO.find(o.id);
            o.prev = it != oldO.end() ? it->second.pos : o.pos;
            o.color = it != oldO.end() ? it->second.color + 1 : (int)(o.id * 7 % 60);
            mobMgr.orbs.push_back(o);
        }
    };

    // Сохранение игрока на сервере: место, здоровье, голод, опыт, режим, измерение, кровать, инвентарь и броня
    auto savePlayerBlob = [&]() {
        if (!mp) return;
        net::Writer w;
        w.u8(1);
        w.f32(player.pos.x); w.f32(player.pos.y); w.f32(player.pos.z); w.f32(player.yaw); w.f32(player.pitch);
        w.u16((uint16_t)std::max(0, player.health)); w.u16((uint16_t)player.food); w.f32(player.saturation); w.u16((uint16_t)player.air);
        w.i32(player.xpLevel); w.f32(player.xpProgress); w.i32(player.xpTotal);
        w.u8(player.creative() ? 1 : 0);
        w.u8((uint8_t)(int8_t)dimension);
        w.u8(world && world->hasBedSpawn ? 1 : 0);
        glm::ivec3 bed = world ? world->bedSpawn : glm::ivec3(0);
        w.i32(bed.x); w.i32(bed.y); w.i32(bed.z);
        for (auto& s : inv.slots) writeItem(w, s);
        for (auto& s : inv.armor) writeItem(w, s);
        for (auto& s : player.enderChest.items) writeItem(w, s); // эндер-сундук (необязательный хвост)
        netConn.send(C_SAVE, w);
    };
    savePlayerFn = savePlayerBlob;
    auto loadPlayerBlob = [&](net::Reader& r) {
        if (r.u8() != 1) return;
        glm::vec3 pos = readVec3(r);
        float yaw = r.f32(), pitch = r.f32();
        int health = r.u16(), food = r.u16();
        float sat = r.f32();
        int air = r.u16();
        int xpl = r.i32();
        float xpp = r.f32();
        int xpt = r.i32();
        bool creative = r.u8() != 0;
        int dim = (int8_t)r.u8();
        bool hasBed = r.u8() != 0;
        glm::ivec3 bed = readIVec3(r);
        Inventory ni;
        for (auto& s : ni.slots) s = readItem(r);
        for (auto& s : ni.armor) s = readItem(r);
        if (!r.ok) return;
        TileEntity ender;
        if (r.more()) {
            for (auto& s : ender.items) s = readItem(r);
            if (!r.ok) ender = TileEntity{};
        }
        if (dim != 0 && dim >= -1 && dim <= 1) { switchDimension(dim); pendingPortal = pendingEnd = pendingRespawn = false; }
        player.pos = player.prevPos = pos;
        player.yaw = yaw; player.pitch = pitch;
        player.health = health > 0 ? health : 20;
        player.food = food; player.saturation = sat; player.air = air;
        player.xpLevel = xpl; player.xpProgress = xpp; player.xpTotal = xpt;
        player.mode = creative ? GameMode::Creative : GameMode::Survival;
        inv = ni;
        player.enderChest = ender;
        if (world) { world->hasBedSpawn = hasBed; world->bedSpawn = bed; }
        loading = true;
        loadingStart = glfwGetTime();
    };

    // Пакеты сервера (каждый кадр)
    auto pollNetwork = [&]() {
        if (!mp) return;
        netConn.poll();
        uint16_t type;
        std::vector<uint8_t> data;
        while (mp && netConn.next(type, data)) {
            net::Reader r(data);
            switch (type) {
            case S_CHUNK: {
                int cx = r.i32(), cz = r.i32();
                mpPendingChunks.erase(chunkKey(cx, cz));
                uint32_t n = r.u32();
                std::vector<std::pair<uint16_t, uint16_t>> e;
                e.reserve(n);
                for (uint32_t i = 0; i < n && r.ok; ++i) { uint16_t idx = r.u16(); uint16_t v = r.u16(); e.push_back({idx, v}); }
                if (world) world->applyChunkEdits(cx, cz, e);
                uint16_t ns = r.u16();
                for (uint16_t i = 0; i < ns && r.ok; ++i) {
                    int x = r.i32(), y = r.u8(), z = r.i32();
                    std::array<std::string, 4> lines;
                    for (auto& l : lines) l = r.str();
                    if (world) world->signs[posKey(x, y, z)] = lines;
                }
                break;
            }
            case S_BLOCKS: {
                uint32_t n = r.u32();
                for (uint32_t i = 0; i < n && r.ok; ++i) {
                    int x = r.i32(), y = r.u8(), z = r.i32();
                    uint8_t b = r.u8(), m = r.u8();
                    if (world) world->applyRemote(x, y, z, b, m);
                }
                break;
            }
            case S_PLAYER_ADD: { uint32_t id = r.u32(); others[id].name = r.str(); break; }
            case S_PLAYER_DEL: others.erase(r.u32()); break;
            case S_PLAYER: {
                uint32_t id = r.u32();
                if (id == myId) break;
                RemotePlayer& rp = others[id];
                PlayerNet s = readPlayerNet(r);
                rp.net = s;
                if (s.flags & 4) rp.swing = 0.f;
                if (!rp.has) {
                    rp.has = true;
                    rp.pos = rp.prevPos = glm::vec3(s.x, s.y, s.z);
                    rp.yaw = rp.bodyYaw = rp.prevBodyYaw = s.yaw;
                    rp.pitch = s.pitch;
                }
                break;
            }
            case S_CHAT: chatLog.push_back({r.str(), glfwGetTime()}); break;
            case S_TIME: {
                int64_t t = r.i64();
                if (std::abs(t - worldTime) > 10) worldTime = t;
                raining = r.u8() != 0;
                thundering = r.u8() != 0;
                break;
            }
            case S_KICK: { std::string why = r.str(); disconnectToMenu("Disconnected by server", why); return; }
            case S_ENTITIES: applyEntities(r); break;
            case S_DAMAGE: {
                int dmg = r.u16();
                int src = (int8_t)r.u8();
                glm::vec3 push = readVec3(r);
                int fire = r.u16(), poison = r.u16();
                bool through = r.u8() != 0;
                if (player.dead) break;
                if (dmg > 0) {
                    player.damageSource = src;
                    TickEvents dev;
                    player.invulnerable = 0;
                    hurtPlayer(player, dmg, dev, through);
                }
                player.motion += push;
                if (fire > 0) player.fireTicks = std::max(player.fireTicks, fire);
                if (poison > 0) player.poisonTicks = std::max(player.poisonTicks, poison);
                break;
            }
            case S_SOUND: {
                std::string n = r.str();
                glm::vec3 sp = readVec3(r);
                float v = r.f32(), pt = r.f32();
                if (std::isnan(sp.x)) audio.play(n, v, pt);
                else audio.play(n, v, pt, &sp);
                break;
            }
            case S_NOTE: { int x = r.i32(), y = r.u8(), z = r.i32(); if (world) playNote({x, y, z}); break; }
            case S_DIGFX: {
                int x = r.i32(), y = r.u8(), z = r.i32();
                uint8_t b = r.u8(), m = r.u8();
                audio.playDig(b, glm::vec3(x, y, z) + 0.5f);
                spawnBreakParticles(particles, {x, y, z}, b, m);
                break;
            }
            case S_EXPLODE: {
                glm::vec3 c = readVec3(r);
                float power = r.f32();
                spawnPoof(particles, c, 40, power * 1.2f, 0.25f);
                break;
            }
            case S_GIVE: {
                ItemStack s = readItem(r);
                if (!inv.add(s) && !s.empty()) throwItem(s); // не влезло — выбрасываем обратно
                audio.play("random/pop", 0.2f, ((rnd() - rnd()) * 0.7f + 1.f) * 2.f);
                break;
            }
            case S_XP: addXp(r.u16()); break;
            case S_KILL: { MobType t = (MobType)r.u8(); if (mobHooks.onKill) mobHooks.onKill(t); break; }
            case S_ACHIEVE: unlockAch(r.u8()); break;
            case S_TELEPORT: {
                glm::vec3 to = readVec3(r);
                player.pos = player.prevPos = to;
                player.motion = glm::vec3(0.f);
                player.fallDistance = 0.f;
                player.invulnerable = 0;
                TickEvents tev;
                hurtPlayer(player, 5, tev, true);
                break;
            }
            case S_POTION: {
                int e = r.u8(), amp = r.u8(), dur = r.u16();
                if (e == EFF_HEAL) player.health = std::min(20, player.health + dur);
                else if (dur > 20) addEffect(player, e, amp, dur);
                break;
            }
            case S_USE_RESULT: {
                int consume = r.u8();
                ItemStack rep = readItem(r);
                int ride = r.u8();
                uint32_t rid = r.u32();
                ItemStack& h = inv.slots[g_in.selected];
                for (int i = 0; i < consume; ++i) consumeHeld();
                if (!rep.empty()) {
                    if (h.empty()) h = rep;
                    else if (!inv.add(rep)) throwItem(rep);
                }
                if (ride == 1 && !ridingId) ridingId = rid;       // транспорт
                if (ride == 2 && !ridingPigId) ridingPigId = rid; // свинья
                break;
            }
            case S_CONTAINER: {
                int kind = r.u8();
                glm::ivec3 pos(0);
                uint32_t vid = 0;
                if (kind == 0) { pos.x = r.i32(); pos.y = r.u8(); pos.z = r.i32(); }
                else vid = r.u32();
                TileEntity::Type tt = (TileEntity::Type)r.u8();
                int n = r.u8();
                std::vector<ItemStack> its(n);
                for (auto& s : its) s = readItem(r);
                int burn = r.u16(), burnMax = r.u16(), cook = r.u16();
                if (!world) break;
                TileEntity* te = nullptr;
                TileEntity* te2 = nullptr;
                glm::ivec3 ca, cb;
                if (kind == 0 && tt == TileEntity::Chest && n == 54 && world->chestPair(pos.x, pos.y, pos.z, ca, cb)) {
                    // Двойной сундук: 27 + 27 слотов
                    te = world->tileAt(ca.x, ca.y, ca.z);
                    if (!te) te = &world->createTile(ca.x, ca.y, ca.z, TileEntity::Chest);
                    te2 = world->tileAt(cb.x, cb.y, cb.z);
                    if (!te2) te2 = &world->createTile(cb.x, cb.y, cb.z, TileEntity::Chest);
                    for (int i = 0; i < 27; ++i) te2->items[i] = its[27 + i];
                } else if (kind == 0) {
                    te = world->tileAt(pos.x, pos.y, pos.z);
                    if (!te) te = &world->createTile(pos.x, pos.y, pos.z, tt);
                } else {
                    te = &mpCartChests[vid];
                    te->type = TileEntity::Chest;
                }
                for (int i = 0; i < std::min(n, te->size()); ++i) te->items[i] = its[i];
                te->burnTime = burn; te->burnMax = burnMax; te->cookTime = cook;
                bool mine = mpOpenKind == kind && (kind == 0 ? mpOpenPos == pos : mpOpenVehicle == vid);
                if (mine) {
                    mpOpenSent.assign(te->items, te->items + te->size());
                    if (te2) mpOpenSent.insert(mpOpenSent.end(), te2->items, te2->items + 27);
                    if (g_in.screen != Screen::Container) {
                        GuiKind gk = tt == TileEntity::Chest ? GuiKind::Chest : tt == TileEntity::Dispenser ? GuiKind::Dispenser
                                   : tt == TileEntity::Brewing ? GuiKind::Brewing : GuiKind::Furnace;
                        openGui(gk, te, te2);
                    }
                }
                break;
            }
            case S_SIGN: {
                int x = r.i32(), y = r.u8(), z = r.i32();
                std::array<std::string, 4> lines;
                for (auto& l : lines) l = r.str();
                if (world) world->signs[posKey(x, y, z)] = lines;
                break;
            }
            case S_PLAYERDATA: loadPlayerBlob(r); break;
            case S_KEEPALIVE: { net::Writer w; w.u32(r.u32()); netConn.send(C_KEEPALIVE, w); break; }
            case S_LIGHTNING: showLightning(readVec3(r)); break;
            case S_PLAYER_LIST: {
                std::vector<ListEntry> pl;
                int n = r.u16();
                for (int i = 0; i < n && r.ok; ++i) {
                    ListEntry e;
                    e.id = r.u32();
                    e.name = r.str();
                    e.ping = r.u16();
                    e.dim = (int8_t)r.u8();
                    pl.push_back(e);
                }
                if (r.ok) playerList.swap(pl);
                break;
            }
            default: break;
            }
        }
        if (mp && !netConn.alive) { disconnectToMenu("Connection lost", "The server closed the connection"); return; }
        netConn.flush();
    };

    // Размытая вращающаяся панорама титульного экрана (drawPanorama + rotateAndBlurSkybox)
    auto drawPanorama = [&](float t, int fbw, int fbh) {
        glBindFramebuffer(GL_FRAMEBUFFER, panoFbo);
        glViewport(0, 0, 256, 256);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(spriteProg);
        glUniform1i(glGetUniformLocation(spriteProg, "uTex"), 0);
        glActiveTexture(GL_TEXTURE0);
        glm::mat4 proj = glm::perspective(glm::radians(120.f), 1.f, 0.05f, 10.f);
        for (int pass = 0; pass < 16; ++pass) {
            float jx = ((pass % 4) / 4.f - 0.5f) / 64.f, jy = ((pass / 4) / 4.f - 0.5f) / 64.f;
            glm::mat4 v = glm::translate(glm::mat4(1.f), glm::vec3(jx, jy, 0.f));
            v = glm::rotate(v, glm::radians(180.f), glm::vec3(1, 0, 0));
            v = glm::rotate(v, glm::radians(std::sin(t / 400.f) * 25.f + 20.f), glm::vec3(1, 0, 0));
            v = glm::rotate(v, glm::radians(-t * 0.1f), glm::vec3(0, 1, 0));
            glUniform4f(glGetUniformLocation(spriteProg, "uColor"), 1, 1, 1, 1.f / (pass + 1));
            for (int k = 0; k < 6; ++k) {
                glm::mat4 m = v;
                if (k == 1) m = glm::rotate(m, glm::radians(90.f), glm::vec3(0, 1, 0));
                if (k == 2) m = glm::rotate(m, glm::radians(180.f), glm::vec3(0, 1, 0));
                if (k == 3) m = glm::rotate(m, glm::radians(-90.f), glm::vec3(0, 1, 0));
                if (k == 4) m = glm::rotate(m, glm::radians(90.f), glm::vec3(1, 0, 0));
                if (k == 5) m = glm::rotate(m, glm::radians(-90.f), glm::vec3(1, 0, 0));
                glm::mat4 mvp = proj * m;
                glUniformMatrix4fv(glGetUniformLocation(spriteProg, "uVP"), 1, GL_FALSE, glm::value_ptr(mvp));
                std::vector<float> q = {-1, -1, 1, 0, 0, 1, -1, 1, 1, 0, 1, 1, 1, 1, 1,
                                        -1, -1, 1, 0, 0, 1, 1, 1, 1, 1, -1, 1, 1, 0, 1};
                glBindTexture(GL_TEXTURE_2D, panoTex[k]);
                drawSprite(q);
            }
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, fbw, fbh);
    };

    menu.open(MenuSystem::Page::Title, saves);
    openScreen(win, Screen::Menu);
    applyOptions();
    if (!autoWorld.empty()) {
        WorldInfo wi;
        if (saves.readInfo(autoWorld, wi)) startWorld(autoWorld, nullptr);
        else {
            NewWorldRequest nw;
            nw.name = nw.folder = autoWorld;
            nw.seedText = "showcase";
            nw.mode = 2;
            nw.cheats = true;
            startWorld(autoWorld, &nw);
        }
    }

    // Витрина новых блоков перед игроком (смотрит на +X)
    auto buildShowcase = [&]() {
        glm::ivec3 o((int)std::floor(player.pos.x) + 2, (int)std::floor(player.pos.y), (int)std::floor(player.pos.z) - 8);
        for (int x = -2; x < 16; ++x)
            for (int z = 0; z < 34; ++z) {
                world->setBlock(o.x + x, o.y - 1, o.z + z, STONE);
                for (int y = 0; y < 5; ++y) world->setBlock(o.x + x, o.y + y, o.z + z, AIR);
            }
        auto S = [&](int x, int y, int z, uint8_t b, uint8_t m = 0) { world->setBlock(o.x + x, o.y + y, o.z + z, b, m); };
        for (int i = 0; i < 6; ++i) S(2, 0, 1 + i * 2, SLAB, (uint8_t)i);
        S(2, 0, 13, DOUBLE_SLAB, 1);
        S(4, 0, 1, WOOD_STAIRS, 1); S(4, 0, 3, COBBLE_STAIRS, 2); S(4, 0, 5, BRICK_STAIRS, 3); S(4, 0, 7, STONEBRICK_STAIRS, 0);
        S(6, 0, 1, WOOD_DOOR, 2); S(6, 1, 1, WOOD_DOOR, 2 | 8);
        S(6, 0, 3, WOOD_DOOR, 2 | 4); S(6, 1, 3, WOOD_DOOR, 2 | 4 | 8);
        S(6, 0, 5, IRON_DOOR, 2); S(6, 1, 5, IRON_DOOR, 2 | 8);
        S(6, 0, 7, TRAPDOOR, 0); S(6, 0, 9, TRAPDOOR, 2 | 4); S(7, 0, 9, PLANKS);
        S(6, 0, 11, FENCE_GATE, 0); S(6, 0, 13, FENCE_GATE, 0 | 4);
        for (int z = 1; z < 6; ++z) S(8, 0, z, GLASS_PANE);
        for (int z = 7; z < 12; ++z) S(8, 0, z, IRON_BARS);
        S(8, 0, 13, LAPIS_ORE); S(8, 0, 14, LAPIS_BLOCK); S(8, 0, 15, REDSTONE_ORE);
        S(10, 0, 1, CAKE, 0); S(10, 0, 3, CAKE, 3);
        S(11, 0, 5, COBBLE); S(11, 1, 5, COBBLE); S(10, 0, 5, LADDER, 0); S(10, 1, 5, LADDER, 0);
        for (int i = 0; i < 16; ++i) S(13, 0, i, WOOL, (uint8_t)i);
        S(10, 0, 9, PLANKS); S(10, 1, 9, FIRE); S(10, 0, 11, TNT);
        // Редстоун: включённый рычаг -> пыль -> железная дверь; выключенный рычаг -> пыль; факел -> пыль
        S(0, 0, 19, LEVER, 5 | 8);
        for (int x = 1; x <= 4; ++x) S(x, 0, 19, REDSTONE_WIRE);
        S(5, 0, 19, IRON_DOOR, 0); S(5, 1, 19, IRON_DOOR, 8);
        S(0, 0, 21, LEVER, 5);
        for (int x = 1; x <= 4; ++x) S(x, 0, 21, REDSTONE_WIRE);
        S(14, 0, 21, REDSTONE_TORCH_ON, TORCH_FLOOR);
        for (int x = 11; x <= 13; ++x) S(x, 0, 21, REDSTONE_WIRE);
        S(7, 0, 21, REPEATER_OFF, 2 | 4); S(8, 0, 21, REDSTONE_WIRE);
        S(7, 0, 19, NOTE_BLOCK, 12);
        S(9, 0, 19, BED, 0); S(10, 0, 19, BED, 8);
        S(12, 0, 19, SIGN_POST, 12);
        // Поршни: запитанный толкает булыжник, липкий смотрит вверх; раздатчик; проигрыватель
        S(2, 0, 24, COBBLE); S(1, 0, 24, PISTON, 0); S(0, 0, 24, LEVER, 5 | 8);
        S(5, 0, 24, STICKY_PISTON, 2); S(7, 0, 24, PISTON, 4);
        S(9, 0, 24, DISPENSER, 5); S(11, 0, 24, JUKEBOX);
        // Мобы 1.0 рядком (для проверки моделей)
        {
            const MobType row[] = {MobType::Wolf, MobType::Wolf, MobType::Squid, MobType::Slime, MobType::Enderman, MobType::Silverfish,
                                   MobType::CaveSpider, MobType::Mooshroom, MobType::SnowGolem, MobType::Villager, MobType::PigZombie,
                                   MobType::Blaze, MobType::MagmaCube, MobType::Sheep, MobType::Sheep, MobType::Pig};
            for (int i = 0; i < 16; ++i) {
                Mob& mb = mobMgr.spawn(row[i], glm::vec3(o.x + i * 1.2f - 2.f, (float)o.y, o.z + 30.5f), 270.f);
                if (i == 1) { mb.tamed = true; mb.sitting = true; mb.health = 20; }
                if (i == 13) mb.color = 14;
                if (i == 14) mb.color = 11;
                if (i == 15) { mb.growingAge = -24000; mb.scale = 0.5f; }
            }
            mobMgr.spawn(MobType::Ghast, glm::vec3(o.x + 8.f, (float)o.y + 12.f, o.z + 40.f), 270.f);
        }
        world->signs[posKey(o.x + 12, o.y, o.z + 19)] = {"MiniCraft", "Minecraft 1.0", "port", ":)"};
        if (std::getenv("MC_SHOW_142")) {
            // Витрина 1.4.2 перед игроком: блоки и мобы с новыми текстурами (проверка перехода на ассеты 1.4.2)
            for (int x = 0; x < 16; ++x)
                for (int z = 0; z < 17; ++z)
                    for (int y = 0; y < 4; ++y) S(x, y, z, AIR);
            S(3, 0, 2, ANVIL, 0); S(3, 0, 4, ANVIL, 1 | 4); S(3, 0, 6, BEACON); S(3, 0, 8, FLOWER_POT);
            S(3, 0, 10, ENDER_CHEST, 5); S(3, 0, 12, CHEST, 5); S(3, 0, 14, EMERALD_ORE);
            for (int k = 0; k < 5; ++k) S(5, 0, 2 + k * 2, SKULL_BLOCK, (uint8_t)(k | (12 << 4)));
            for (int d = 0; d < 4; ++d) { S(7, 0, 2 + d * 2, PLANKS, (uint8_t)d); S(7, 1, 2 + d * 2, LOG, (uint8_t)d); S(7, 2, 2 + d * 2, LEAVES, (uint8_t)(d | LEAVES_PLAYER)); }
            for (int d = 0; d < 3; ++d) S(7, 0, 10 + d * 2, SANDSTONE, (uint8_t)d);
            S(9, 0, 2, STONE_BRICK, 3); S(9, 0, 4, COBBLE_WALL, 1); S(9, 0, 6, FARMLAND, 7); S(9, 1, 6, CARROTS, 7);
            S(9, 0, 8, FARMLAND, 7); S(9, 1, 8, POTATOES, 3); S(9, 0, 10, COMMAND_BLOCK); S(9, 0, 12, EMERALD_BLOCK);
            S(6, 0, 14, COBBLE); S(6, 1, 14, COBBLE);
            // Ствол джунглей с какао трёх возрастов
            S(9, 0, 14, LOG, 3); S(9, 1, 14, LOG, 3); S(9, 2, 14, LOG, 3);
            S(8, 1, 14, COCOA, 3 | (2 << 2)); S(9, 1, 13, COCOA, 0 | (1 << 2)); S(9, 2, 15, COCOA, 2); S(10, 1, 14, COCOA, 1 | (2 << 2));
            for (int i = 0; i < 4; ++i) S(3, 1, 1 + i * 2, WOOD_SLAB, (uint8_t)(i | (i & 1 ? 8 : 0))); // деревянные плиты
            S(2, 1, 13, SLAB, 1 | 8); S(2, 1, 14, COBBLE_STAIRS, 0 | 4); S(2, 1, 15, SLAB, 4 | 8); S(2, 0, 15, SLAB, 4); // верх/низ
            if (placeItemFrame(mobMgr.paintings, *world, o + glm::ivec3(6, 0, 14), glm::ivec3(-1, 0, 0)))
                mobMgr.paintings.back().item = makeStack(DIAMOND_SWORD);
            if (placeItemFrame(mobMgr.paintings, *world, o + glm::ivec3(6, 0, 14), glm::ivec3(0, 0, -1))) {
                mobMgr.paintings.back().item = makeStack(GLOWSTONE);
                mobMgr.paintings.back().rotation = 1;
            } S(5, 1, 14, SKULL_BLOCK, 4 | 8 | (2 << 4)); S(6, 1, 13, SKULL_BLOCK, 2 | 8 | (3 << 4));
            S(1, 0, 3, FLOWER_POT, 1); S(1, 0, 4, FLOWER_POT, 3); S(1, 0, 5, FLOWER_POT, 9); S(1, 0, 6, FLOWER_POT, 11); S(1, 0, 7, FLOWER_POT, 7);
            const MobType row142[] = {MobType::Zombie, MobType::PigZombie, MobType::ZombieVillager, MobType::WitherSkeleton, MobType::Witch,
                                      MobType::Skeleton};
            // Загон под навесом (иначе нежить горит на солнце и за огнём текстуры не видно)
            for (int z = 1; z <= 15; ++z) {
                S(10, 0, z, FENCE); S(13, 0, z, FENCE);
                for (int x = 10; x <= 13; ++x) S(x, 3, z, STONE);
            }
            for (int x = 10; x <= 13; ++x) { S(x, 0, 1, FENCE); S(x, 0, 15, FENCE); }
            for (int i = 0; i < 6; ++i) mobMgr.spawn(row142[i], glm::vec3(o.x + 11.5f, (float)o.y, o.z + 3.5f + i * 2.f), 180.f);
        }
        for (int y = 0; y < 3; ++y) S(-2, y, 8, STONE);
        S(0, 1, 8, OBSIDIAN); S(0, 2, 8, ENCHANT_TABLE); // стол с книгой рядом с игроком
        if (std::getenv("MC_SHOW_CART")) {
            // Проверка вагонетки: прямой путь на 30 блоков по +X, игрок сразу сидит в вагонетке
            for (int i = 0; i < 30; ++i) { S(-6 + i, -1, 34, STONE); S(-6 + i, 0, 34, AIR); S(-6 + i, 1, 34, AIR); S(-6 + i, 0, 34, RAIL); }
            for (int i = 0; i < 30; ++i) world->updateRailShape(o.x - 6 + i, o.y, o.z + 34, false);
            Vehicle v;
            v.kind = VehicleKind::Minecart;
            v.pos = v.prev = glm::vec3(o.x - 5 + 0.5f, (float)o.y, o.z + 34.5f);
            v.id = mobMgr.nextId++;
            mobMgr.vehicles.push_back(v);
            ridingId = v.id;
        }
        player.pos = player.prevPos = glm::vec3(o.x - 1.5f, (float)o.y + 3.f, o.z + 8.5f);
        player.motion = glm::vec3(0.f);
        const char* sy = std::getenv("MC_SHOW_YAW");
        player.yaw = sy ? (float)std::atof(sy) : 0.f;
        { const char* sp = std::getenv("MC_SHOW_PITCH"); player.pitch = sp ? (float)std::atof(sp) : -35.f; }
        showcaseHold = 60;
        if (std::getenv("MC_SHOW_END")) {
            // Портал Края на площадке и игрок в нём — проверка перехода в Край
            for (int dx = -1; dx <= 1; ++dx)
                for (int dz = -1; dz <= 1; ++dz) S(-4 + dx, 0, 8 + dz, END_PORTAL);
            player.pos = player.prevPos = glm::vec3(o.x - 4 + 0.5f, (float)o.y, o.z + 8.5f);
            showcaseHold = 0;
        }
        if (std::getenv("MC_SHOW_PORTAL")) {
            // Портал рядом и игрок внутри — проверка перехода в Незер
            glm::ivec3 q = o + glm::ivec3(-6, 0, 8);
            for (int dx = -3; dx <= 4; ++dx)
                for (int dz = -2; dz <= 2; ++dz)
                    for (int dy = -1; dy <= 5; ++dy) S(-6 + dx, dy, 8 + dz, dy < 0 ? STONE : AIR);
            for (int dx = -1; dx <= 2; ++dx) { S(-6 + dx, -1, 8, OBSIDIAN); S(-6 + dx, 3, 8, OBSIDIAN); }
            for (int dy = 0; dy < 3; ++dy) { S(-7, dy, 8, OBSIDIAN); S(-4, dy, 8, OBSIDIAN); }
            world->tryCreatePortal(q.x, q.y, q.z);
            player.pos = player.prevPos = glm::vec3(q) + glm::vec3(0.5f, 0.f, 0.5f);
            showcaseHold = 0;
        }
        const ItemStack bar[9] = {makeStack(FLINT_AND_STEEL), makeStack(SLAB, 64, 1), makeStack(WOOD_STAIRS, 64), makeStack(TRAPDOOR, 64),
                                  makeStack(FENCE_GATE, 64), makeStack(FENCE, 64), makeStack(GLASS_PANE, 64), makeStack(TNT, 64),
                                  makeStack(WOOD_DOOR_ITEM)};
        for (int i = 0; i < 9; ++i) inv.slots[i] = bar[i];
        if (std::getenv("MC_SHOW_142")) {
            const ItemStack bar142[9] = {makeStack(CHEST), makeStack(ENDER_CHEST), makeStack(ANVIL), makeStack(BEACON),
                                         makeStack(SKULL_ITEM, 1, 2), makeStack(CHAIN_HELMET), makeStack(FLOWER_POT_ITEM),
                                         makeStack(ITEM_FRAME_ITEM), makeStack(PLANKS, 64, 1)};
            for (int i = 0; i < 9; ++i) inv.slots[i] = bar142[i];
            world->setBlock(o.x, o.y + 2, o.z + 9, ENDER_CHEST, 5); // эндер-сундук у игрока (ПКМ — личный инвентарь)
            world->setBlock(o.x, o.y + 2, o.z + 10, ANVIL, 1);      // наковальня рядом (ремонт)
            inv.slots[9] = makeStack(DIAMOND_PICKAXE);
            inv.slots[9].damage = 1200;
            inv.slots[10] = makeStack(DIAMOND, 3);
            player.xpLevel = 30;
            player.enderChest.items[0] = makeStack(DIAMOND, 5);
        }
        g_in.selected = 2;
        // Сундуки: одиночный и двойной (крышка двойного открыта при MC_SHOW_CHEST)
        S(10, 0, 7, CHEST, 4); S(10, 0, 15, CHEST, 2); S(11, 0, 15, CHEST, 2); S(11, 0, 12, CHEST, 5); S(11, 0, 13, CHEST, 5);
        if (std::getenv("MC_SHOW_PAINT")) {
            for (int z = 0; z < 16; ++z)
                for (int y = 0; y < 5; ++y) S(15, y, z, STONE);
            for (int z = 1; z < 15; z += 2)
                for (int y = 0; y < 5; y += 2) placePainting(mobMgr.paintings, *world, o + glm::ivec3(15, y, z), glm::ivec3(-1, 0, 0), gameRng);
        }
        if (std::getenv("MC_SHOW_XP")) spawnXpOrbs(mobMgr.orbs, glm::vec3(o) + glm::vec3(3.5f, 0.5f, 16.5f), 6000, gameRng);
        if (std::getenv("MC_SHOW_LIQUID")) { S(15, 0, 3, STONE); S(15, 1, 3, WATER, 0); S(15, 0, 11, STONE); S(15, 1, 11, LAVA, 0); }
        if (std::getenv("MC_SHOW_CHEST")) showcaseLidKey = posKey(o.x + 10, o.y, o.z + 15);
        if (std::getenv("MC_SHOW_WIRE")) {
            // Пыль: одиночная, линия вдоль Z, линия вдоль X, уголок
            for (int x = 0; x < 6; ++x)
                for (int z = 0; z < 16; ++z) S(x, 0, z, AIR);
            S(1, 0, 2, REDSTONE_WIRE);
            for (int z = 5; z < 9; ++z) S(1, 0, z, REDSTONE_WIRE);
            for (int x = 2; x < 6; ++x) S(x, 0, 11, REDSTONE_WIRE);
            S(3, 0, 2, REDSTONE_WIRE); S(4, 0, 2, REDSTONE_WIRE); S(3, 0, 3, REDSTONE_WIRE);
            S(3, 0, 6, REPEATER_OFF, 0); S(4, 0, 6, REDSTONE_WIRE); S(3, 0, 8, REPEATER_ON, 1 | 4); S(3, 0, 9, REDSTONE_WIRE);
        }
        if (std::getenv("MC_SHOW_MAP")) inv.slots[2] = makeStack(MAP);
        if (std::getenv("MC_SHOW_PUMPKIN")) { inv.armor[0] = makeStack(PUMPKIN); camMode = 2; } // проверка тыквы на голове (вид спереди)
    };

    // ================================================================ Главный цикл
    // Отладка сетевой игры: MC_CONNECT=адрес (и MC_NAME=имя) — сразу подключиться к серверу
    if (const char* ca = std::getenv("MC_CONNECT")) {
        const char* cn = std::getenv("MC_NAME");
        startMultiplayer(ca, cn ? cn : opt.playerName);
    }
    while (!glfwWindowShouldClose(win)) {
        double now = glfwGetTime();
        float frameDt = (float)(now - lastTime);
        lastTime = now;

        glfwPollEvents();
        pollNetwork();

        int fbw, fbh;
        glfwGetFramebufferSize(win, &fbw, &fbh);
        if (fbw > 0 && fbh > 0) {
            int maxScale = std::clamp(std::min(fbw / 320, fbh / 240), 1, 4);
            lastSc = (float)(opt.guiScale == 0 ? maxScale : std::min(opt.guiScale, maxScale));
            lastW = fbw / lastSc;
            lastH = fbh / lastSc;
        }

        // ---- Меню вне мира и экран загрузки
        if (!inGame || loading) {
            if (fbw == 0 || fbh == 0) { glfwSwapBuffers(win); continue; }
            double cmx, cmy;
            glfwGetCursorPos(win, &cmx, &cmy);
            MenuInput min;
            min.mx = (float)cmx / lastSc;
            min.my = (float)cmy / lastSc;
            min.click = g_in.menuClick;
            min.doubleClick = g_in.doubleClick;
            min.mouseDown = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            min.scroll = g_in.scrollDelta;
            min.typed = g_in.typed;
            min.backspace = g_in.keyBackspace;
            min.enter = g_in.keyEnter;
            min.escape = g_in.keyEscape;
            min.tab = g_in.keyTab;
            min.keyPressed = g_in.lastKey;
            g_in.lastKey = -1;
            g_in.menuClick = g_in.doubleClick = false;
            g_in.scrollDelta = 0.f;
            g_in.typed.clear();
            g_in.keyBackspace = g_in.keyEnter = g_in.keyEscape = g_in.keyTab = false;
            g_in.clickButton = -1;

            if (loading) {
                // Строим мир вокруг точки появления, пока не будут готовы ближние чанки
                world->update(player.pos, 24, 24);
                int pcx = floorDiv((int)std::floor(player.pos.x), CW), pcz = floorDiv((int)std::floor(player.pos.z), CW);
                if (mp) {
                    for (auto [gx, gz] : world->generated) {
                        net::Writer w;
                        w.i32(gx); w.i32(gz);
                        netConn.send(C_CHUNK_REQ, w);
                        mpPendingChunks.insert(chunkKey(gx, gz));
                    }
                    world->generated.clear();
                    for (auto& [sp, sm] : world->newSpawners) mobMgr.addSpawner(sp, sm);
                    world->newSpawners.clear();
                    world->villagerSpawns.clear();
                }
                int ready = 0, total = 0;
                for (int dz = -2; dz <= 2; ++dz)
                    for (int dx = -2; dx <= 2; ++dx) {
                        ++total;
                        Chunk* c = world->chunkAt(pcx + dx, pcz + dz);
                        if (c && c->meshed && !mpPendingChunks.count(chunkKey(pcx + dx, pcz + dz))) ++ready;
                    }
                menu.loadingProgress = (float)ready / total;
                if (ready == total || glfwGetTime() - loadingStart > 20.0) {
                    if (pendingPortal) { findOrBuildPortal(); pendingPortal = false; }
                    if (pendingRespawn) { player.pos = player.prevPos = respawnPos(); pendingRespawn = false; }
                    if (pendingSpawnCheck) {
                        pendingSpawnCheck = false;
                        st.spawn = spawnPoint = world->safeSpawnNear(st.spawn, 60, false);
                        player.pos = player.prevPos = spawnPoint;
                    }
                    if (pendingEnd) {
                        // Обсидиановая площадка 5x5 в точке (100, 48, 0), как в 1.0
                        for (int dx = -2; dx <= 2; ++dx)
                            for (int dz = -2; dz <= 2; ++dz) {
                                world->setBlock(100 + dx, 48, dz, OBSIDIAN);
                                for (int dy = 1; dy <= 3; ++dy) world->setBlock(100 + dx, 48 + dy, dz, AIR);
                            }
                        player.pos = player.prevPos = glm::vec3(100.5f, 49.f, 0.5f);
                        bool haveDragon = false, haveCrystals = false;
                        for (auto& mb : mobMgr.mobs) {
                            haveDragon |= mb.type == MobType::EnderDragon;
                            haveCrystals |= mb.type == MobType::EnderCrystal;
                        }
                        if (!world->dragonDefeated && !mp) {
                            if (!haveDragon) mobMgr.spawn(MobType::EnderDragon, glm::vec3(0.f, 110.f, 0.f), 0.f);
                            if (!haveCrystals)
                                for (const glm::ivec3& t : world->endPillarTops())
                                    mobMgr.spawn(MobType::EnderCrystal, glm::vec3(t) + glm::vec3(0.5f, 1.f, 0.5f), 0.f);
                        }
                        pendingEnd = false;
                    }
                    if (showcase) { buildShowcase(); showcase = false; }
                    loading = false;
                    menu.page = MenuSystem::Page::None;
                    if (pendingCredits) {
                        pendingCredits = false;
                        creditsTime = 0;
                        openScreen(win, Screen::Credits);
                    } else {
                        openScreen(win, player.dead ? Screen::Dead : Screen::Playing);
                    }
                }
            }

            glViewport(0, 0, fbw, fbh);
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            float t = (float)glfwGetTime() * 20.f;
            bool title = menu.page == MenuSystem::Page::Title;
            if (title) drawPanorama(t, fbw, fbh);
            ui.begin(fbw, fbh);
            if (title) {
                glm::vec2 pp[4] = {{0, 0}, {(float)fbw, 0}, {(float)fbw, (float)fbh}, {0, (float)fbh}};
                glm::vec2 puv[4] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
                ui.quad(panoFboTex, pp, puv, glm::vec4(1.f));
                ui.gradient(0, 0, (float)fbw, (float)fbh, {1, 1, 1, 0.5f}, {1, 1, 1, 0.f});
                ui.gradient(0, 0, (float)fbw, (float)fbh, {0, 0, 0, 0.f}, {0, 0, 0, 0.5f});
            }
            MenuAction act = menu.frame(ui, menuTex, lastSc, lastW, lastH, min, opt, saves, (float)glfwGetTime());
            ui.flush();
            glfwSwapBuffers(win);
            audio.update(frameDt, false);
            if (act == MenuAction::Quit) glfwSetWindowShouldClose(win, GLFW_TRUE);
            if (act == MenuAction::OptionsChanged || act == MenuAction::CloseOptions) applyOptions();
            if (act == MenuAction::PlayWorld) startWorld(menu.selectedFolder, nullptr);
            if (act == MenuAction::CreateWorld) startWorld(menu.newWorld.folder, &menu.newWorld);
            if (act == MenuAction::JoinServer) { opt.save(optionsPath); startMultiplayer(opt.lastServer, opt.playerName); }
            continue;
        }

        // Обзор мышью (каждый кадр, без привязки к тикам)
        double mx, my;
        glfwGetCursorPos(win, &mx, &my);
        if (g_in.screen == Screen::Playing) {
            if (g_in.firstMouse) { g_in.lastX = mx; g_in.lastY = my; g_in.firstMouse = false; }
            const float sens = opt.mouseDegreesPerPixel();
            player.yaw += (float)(mx - g_in.lastX) * sens;
            player.pitch -= (float)(my - g_in.lastY) * sens * (opt.invertMouse ? -1.f : 1.f);
            player.pitch = std::clamp(player.pitch, -90.f, 90.f);
        }
        g_in.lastX = mx; g_in.lastY = my;
        const float gmx = (float)mx / lastSc, gmy = (float)my / lastSc;

        // Окна с предметами
        if (g_in.openInventory) {
            g_in.openInventory = false;
            unlockAch(0);
            openGui(player.creative() ? GuiKind::Creative : GuiKind::Inventory, nullptr);
        }
        if (g_in.screen == Screen::Container) {
            GuiContext ctx = makeCtx(gmx, gmy);
            if (g_in.clickButton >= 0) gui.mouseDown(ctx, g_in.clickButton, g_in.clickShift);
            if (g_in.mouseReleased) gui.mouseUp();
            if (g_in.scrollDelta != 0.f) gui.scroll(g_in.scrollDelta);
            if (g_in.invNumber >= 0) gui.hotkey(ctx, g_in.invNumber);
            if (g_in.closeGui) closeGui();
        }
        g_in.clickButton = -1;
        g_in.mouseReleased = false;
        g_in.scrollDelta = 0.f;
        g_in.invNumber = -1;
        g_in.closeGui = false;

        if (g_in.timeSkip) {
            if (!mp && cheatsAllowed()) worldTime += 6000;
            else showInfo(mp ? "Time is controlled by the server" : "Cheats are not enabled on this world");
            g_in.timeSkip = false;
        }
        if (g_in.toggleSmooth) {
            g_in.toggleSmooth = false;
            opt.smoothLighting = !opt.smoothLighting;
            applyOptions();
            showInfo(opt.smoothLighting ? "Smooth lighting: ON" : "Smooth lighting: OFF");
        }
        if (g_in.renderDelta) {
            world->renderDistance = std::clamp(world->renderDistance + g_in.renderDelta, 3, 16);
            g_in.renderDelta = 0;
            showInfo("Render distance: " + std::to_string(world->renderDistance) + " chunks");
        }

        // Стриминг чанков (больше за кадр, пока вокруг игрока пусто)
        int pcx = floorDiv((int)std::floor(player.pos.x), CW), pcz = floorDiv((int)std::floor(player.pos.z), CW);
        Chunk* hereChunk = world->chunkAt(pcx, pcz);
        bool here = hereChunk && hereChunk->meshed;
        world->update(player.pos, here ? 3 : 12, here ? 3 : 12);

        // Фиксированные тики; пауза останавливает мир, как в одиночной игре 1.0
        if (mp || (g_in.screen != Screen::Paused && g_in.screen != Screen::Menu)) {
            tickAcc += std::min(frameDt, 0.25f);
            while (tickAcc >= 1.0 / TICKS_PER_SECOND) {
                tick();
                tickAcc -= 1.0 / TICKS_PER_SECOND;
            }
        }
        const float partial = (float)(tickAcc * TICKS_PER_SECOND);
        const bool playing = g_in.screen == Screen::Playing;
        const ItemStack held = inv.slots[g_in.selected];

        if (g_in.selected != lastSelected) { nameTimer = 2.f; lastSelected = g_in.selected; }
        nameTimer -= frameDt;
        infoTimer -= frameDt;

        // ------------------------------------------------ Камера
        glm::vec3 eye = sleeping ? player.pos + glm::vec3(0, 0.3f, 0) : player.eye(partial);
        glm::vec3 look = player.look();
        // Вправо — по повороту головы, а не через взгляд: при взгляде строго вверх/вниз cross(look, up) = 0 и выходит NaN
        glm::vec3 right(-std::sin(glm::radians(player.yaw)), 0.f, std::cos(glm::radians(player.yaw)));
        float walk = glm::mix(player.prevWalkDist, player.walkDist, partial);
        float bob = opt.viewBobbing ? glm::mix(prevBobAmp, bobAmp, partial) : 0.f;
        glm::vec3 camPos = eye + right * (std::sin(walk * glm::pi<float>()) * bob * 0.5f) +
                           glm::vec3(0, -std::abs(std::cos(walk * glm::pi<float>()) * bob), 0);

        audio.setListener(eye, look);
        int surface = world->terrainHeight((int)std::floor(eye.x), (int)std::floor(eye.z));
        audio.update(frameDt, eye.y < surface - 8 && world->getSkyLight((int)std::floor(eye.x), (int)std::floor(eye.y), (int)std::floor(eye.z)) < 4);

        glm::ivec3 hit, prev;
        bool hasHit = playing && raycast(*world, eye, look, player.creative() ? 5.f : 4.5f, hit, prev);
        uint8_t target = hasHit ? world->getBlock(hit.x, hit.y, hit.z) : AIR;

        // ------------------------------------------------ Рендер мира
        if (fbw == 0 || fbh == 0) { glfwSwapBuffers(win); continue; }
        glViewport(0, 0, fbw, fbh);

        bool eyeInWater = blockAt(eye) == WATER;
        bool eyeInLava = blockAt(eye) == LAVA;
        float rainNow = glm::mix(prevRain, rainStrength, partial), thunderNow = glm::mix(prevThunder, thunderStrength, partial);
        float flashNow = lightningTicks > 0 && (lightningTicks % 3) != 0 ? 1.f : 0.f;
        Sky sky = computeSky(worldTime, partial, look, rainNow, thunderNow, flashNow);
        glm::vec3 fogColor = sky.fog;
        float farDist = (float)(world->renderDistance * CW);
        float fogStart = farDist * 0.25f, fogEnd = farDist;
        if (dimension == -1) {
            // Незер: тёмно-красный густой туман, света неба нет
            sky.fog = fogColor = glm::vec3(0.2f, 0.03f, 0.03f);
            sky.skyFactor = 0.f;
            sky.starAlpha = 0.f;
            fogStart = farDist * 0.05f;
            fogEnd = farDist * 0.55f;
        } else if (dimension == 1) {
            sky.fog = fogColor = glm::vec3(0.04f, 0.03f, 0.06f);
            sky.skyFactor = 0.f;
            sky.starAlpha = 0.f;
        }
        if (eyeInWater) { fogColor = glm::vec3(0.02f, 0.02f, 0.2f) * sky.skyFactor + 0.02f; fogStart = 0.f; fogEnd = 16.f; }
        if (eyeInLava) { fogColor = glm::vec3(0.6f, 0.1f, 0.f); fogStart = 0.f; fogEnd = 2.f; }

        glClearColor(fogColor.r, fogColor.g, fogColor.b, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float targetFov = opt.fovDegrees() * (player.sprinting ? 1.15f : 1.f);
        if (player.fly) targetFov += 5.f;
        if (bowCharge > 0) {
            float f = std::min(1.f, bowCharge / 20.f);
            targetFov *= 1.f - f * f * 0.15f;
        }
        fov += (targetFov - fov) * std::min(1.f, 10.f * frameDt);
        glm::mat4 proj = glm::perspective(glm::radians(fov), (float)fbw / fbh, 0.05f, farDist * 2.5f);
        // «Верх» камеры всегда перпендикулярен взгляду (иначе при pitch = ±90 матрица вида вырождается и мир пропадает)
        glm::vec3 viewDir = look;
        if (camMode != 0 && !player.dead) {
            // Камера на 4 блока сзади (или спереди, глядя на игрока), не заходит в стены
            glm::vec3 back = camMode == 1 ? -look : look;
            float d = 4.f;
            for (float t = 0.1f; t <= 4.f; t += 0.1f) {
                glm::vec3 q = eye + back * t;
                bool hitWall = false;
                for (int k = 0; k < 8 && !hitWall; ++k) {
                    glm::vec3 o(((k & 1) * 2 - 1) * 0.1f, (((k >> 1) & 1) * 2 - 1) * 0.1f, (((k >> 2) & 1) * 2 - 1) * 0.1f);
                    hitWall = pointCollides(*world, q + o);
                }
                if (hitWall) { d = std::max(0.f, t - 0.1f); break; }
            }
            camPos = eye + back * d;
            if (camMode == 2) viewDir = -look;
        }
        glm::mat4 view = glm::lookAt(camPos, camPos + viewDir, glm::cross(right, look));
        // Тряска камеры при уроне (hurtCameraEffect)
        if (player.hurtTime > 0 || player.dead) {
            float f = player.dead ? 1.f : (player.hurtTime - partial) / 10.f;
            f = std::sin(f * f * f * f * glm::pi<float>());
            float roll = player.dead ? 40.f : -f * 14.f;
            view = glm::rotate(glm::mat4(1.f), glm::radians(roll), glm::vec3(0, 0, 1)) * view;
        }
        glm::mat4 vp = proj * view;
        lastVP = vp;
        Frustum frustum(vp);
        glm::vec3 camRight(view[0][0], view[1][0], view[2][0]), camUp(view[0][1], view[1][1], view[2][1]);

        // ---- Звёзды, солнце и луна (относительно камеры, без записи глубины)
        if (!eyeInWater && !eyeInLava && dimension == 0) {
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glDisable(GL_CULL_FACE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            // Небесная сфера вращается вокруг оси Z: солнце встаёт на востоке (+X) и садится на западе (-X)
            glm::mat4 skyRot = glm::rotate(glm::mat4(1.f), sky.celestial * glm::two_pi<float>(), glm::vec3(0, 0, 1));
            glm::mat4 skyVP = proj * glm::mat4(glm::mat3(view)) * skyRot;
            glUseProgram(spriteProg);
            glUniformMatrix4fv(glGetUniformLocation(spriteProg, "uVP"), 1, GL_FALSE, glm::value_ptr(skyVP));
            glUniform1i(glGetUniformLocation(spriteProg, "uTex"), 0);
            glActiveTexture(GL_TEXTURE0);
            if (sky.starAlpha > 0.f) {
                glBindTexture(GL_TEXTURE_2D, ui.whiteTex);
                glUniform4f(glGetUniformLocation(spriteProg, "uColor"), 1, 1, 1, sky.starAlpha);
                glBindVertexArray(starVao);
                glDrawArrays(GL_TRIANGLES, 0, starCount);
            }
            auto body = [&](float y, float size, GLuint tex) {
                // Квадрат над головой (+Y) в системе небесной сферы
                std::vector<float> v = {-size, y, -size, 0, 0, size, y, -size, 1, 0, size, y, size, 1, 1,
                                        -size, y, -size, 0, 0, size, y, size, 1, 1, -size, y, size, 0, 1};
                glBindTexture(GL_TEXTURE_2D, tex);
                drawSprite(v);
            };
            glUniform4f(glGetUniformLocation(spriteProg, "uColor"), 1, 1, 1, 1.f - rainNow);
            body(100.f, 30.f, sunTex);
            if (moonPhases) {
                // Фаза Луны меняется каждые сутки (World.getMoonPhase 1.4.2): клетка phase%4, phase/4 в сетке 4x2.
                // Углы и UV — как в RenderGlobal 1.4.2; наша сфера повёрнута на -90° вокруг Y (x = -z_mc, z = x_mc)
                int phase = (int)((worldTime / 24000) % 8);
                float u0 = (phase % 4) / 4.f, v0 = (phase / 4) / 2.f, u1 = u0 + 0.25f, v1 = v0 + 0.5f;
                const float s = 20.f, y = -100.f;
                std::vector<float> v = {-s, y, -s, u1, v1, s, y, -s, u1, v0, s, y, s, u0, v0,
                                        -s, y, -s, u1, v1, s, y, s, u0, v0, -s, y, s, u0, v1};
                glBindTexture(GL_TEXTURE_2D, moonTex);
                drawSprite(v);
            } else {
                body(-100.f, 20.f, moonTex);
            }
            glDepthMask(GL_TRUE);
        }

        glEnable(GL_DEPTH_TEST);
        // LEQUAL: наложения в той же плоскости (чёлка травы, глаза паука) проходят тест глубины
        glDepthFunc(GL_LEQUAL);
        glEnable(GL_CULL_FACE);
        glDisable(GL_BLEND);

        // Общие параметры для обоих вариантов шейдера мира
        auto setWorldUniforms = [&](GLuint prog, const glm::mat4& m, const glm::vec3& cam) {
            glUseProgram(prog);
            glUniformMatrix4fv(glGetUniformLocation(prog, "uVP"), 1, GL_FALSE, glm::value_ptr(m));
            glUniform3fv(glGetUniformLocation(prog, "uCam"), 1, glm::value_ptr(cam));
            glUniform3fv(glGetUniformLocation(prog, "uFogColor"), 1, glm::value_ptr(fogColor));
            glUniform1f(glGetUniformLocation(prog, "uFogStart"), fogStart);
            glUniform1f(glGetUniformLocation(prog, "uFogEnd"), fogEnd);
            glUniform3f(glGetUniformLocation(prog, "uTint"), 1, 1, 1);
            glUniform1f(glGetUniformLocation(prog, "uAlpha"), 1.f);
            glUniform1f(glGetUniformLocation(prog, "uSkyFactor"), sky.skyFactor);
            glUniform1f(glGetUniformLocation(prog, "uFlicker"), flicker);
            glUniform1f(glGetUniformLocation(prog, "uGamma"), gamma);
            glUniform1f(glGetUniformLocation(prog, "uAmbient"), dimension == 0 ? 0.f : dimension == -1 ? 0.1f : 0.4f);
            glUniform1i(glGetUniformLocation(prog, "uTex"), 0);
            glUniform3f(glGetUniformLocation(prog, "uOffset"), 0, 0, 0);
        };
        setWorldUniforms(solidProg, vp, camPos);
        setWorldUniforms(chunkProg, vp, camPos);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, terrainTex);

        std::vector<Chunk*> visible;
        for (auto& [key, c] : world->chunks) {
            if (!c->meshed) continue;
            glm::vec3 mn(c->cx * CW, 0, c->cz * CW);
            if (!frustum.boxVisible(mn, mn + glm::vec3(CW, CH, CW))) continue;
            visible.push_back(c.get());
        }
        // Ближние первыми — меньше перерисовки пикселей
        std::sort(visible.begin(), visible.end(), [&](Chunk* a, Chunk* b) {
            auto dist = [&](Chunk* c) {
                float dx = c->cx * CW + CW / 2.f - eye.x, dz = c->cz * CW + CW / 2.f - eye.z;
                return dx * dx + dz * dz;
            };
            return dist(a) < dist(b);
        });

        auto drawChunks = [&](GLuint prog, int mesh) {
            GLint loc = glGetUniformLocation(prog, "uOffset");
            for (Chunk* c : visible) {
                if (!c->count[mesh]) continue;
                glUniform3f(loc, (float)(c->cx * CW), 0.f, (float)(c->cz * CW));
                glBindVertexArray(c->vao[mesh]);
                glDrawArrays(GL_TRIANGLES, 0, c->count[mesh]);
            }
        };

        // Сплошные блоки, затем «дырявые» (листва, стекло, растения, факелы)
        glUseProgram(solidProg);
        drawChunks(solidProg, MESH_SOLID);
        glUseProgram(chunkProg);
        drawChunks(chunkProg, MESH_CUTOUT);
        GLint offLoc = glGetUniformLocation(chunkProg, "uOffset");
        GLint tintLoc = glGetUniformLocation(chunkProg, "uTint");
        glUniform3f(offLoc, 0, 0, 0);
        glDisable(GL_CULL_FACE);

        // Модель игрока (RenderPlayer 1.0): тело из char.png, броня поверх (раздутые коробки), предмет в правой руке.
        // root — точка ног с поворотом тела, в блоках (зеркало модели добавляется здесь)
        auto drawPlayerModel = [&](const glm::mat4& root, const PlayerPose& pp, float sky, float bl, const ItemStack& heldStack,
                                   const uint16_t* armorIds = nullptr) {
            glm::mat4 em = glm::scale(root, glm::vec3(-1.f / 16.f, -1.f / 16.f, 1.f / 16.f));
            em = glm::translate(em, glm::vec3(0.f, -24.f, 0.f));
            auto model = buildPlayerModel(pp, 0.f);
            std::vector<Vertex> mv;
            emitModel(mv, model, em, sky, bl);
            glBindTexture(GL_TEXTURE_2D, charTex);
            drawDynamic(mv);
            // Броня: шлем — голова и шляпа, нагрудник — тело и руки, поножи (слой 2) — тело и ноги, ботинки — ноги
            static const std::vector<int> PARTS[4] = {{0, 1}, {2, 3, 4}, {2, 5, 6}, {5, 6}};
            for (int s = 0; s < 4; ++s) {
                uint16_t aid = armorIds ? armorIds[s] : (inv.armor[s].empty() ? 0 : inv.armor[s].id);
                int mat = aid == 0 ? -1 : armorMaterial(aid);
                if (mat < 0 || !armorTex[mat][s == 2]) continue;
                auto am = buildPlayerModel(pp, s == 2 ? 0.5f : 1.f);
                std::vector<ModelPart> sub;
                for (int i : PARTS[s]) sub.push_back(am[i]);
                std::vector<Vertex> av;
                emitModel(av, sub, em, sky, bl);
                glBindTexture(GL_TEXTURE_2D, armorTex[mat][s == 2]);
                drawDynamic(av);
            }
            uint16_t helmet = armorIds ? armorIds[0] : (inv.armor[0].empty() ? 0 : inv.armor[0].id);
            if (helmet == PUMPKIN) {
                const ModelPart& head = model[0];
                glm::mat4 hm = glm::translate(em, head.pivot);
                hm = glm::rotate(hm, head.rot.z, glm::vec3(0, 0, 1));
                hm = glm::rotate(hm, head.rot.y, glm::vec3(0, 1, 0));
                hm = glm::rotate(hm, head.rot.x, glm::vec3(1, 0, 0));
                hm = glm::translate(hm, glm::vec3(0.f, -4.f, 0.f));
                hm = glm::scale(hm, glm::vec3(10.f, -10.f, 10.f));
                hm = glm::translate(hm, glm::vec3(-0.5f));
                std::vector<Vertex> bm, pv;
                appendBlockModel(bm, PUMPKIN, 1.f, 0.f, 2);
                appendTransformed(pv, bm, hm, sky, bl);
                glBindTexture(GL_TEXTURE_2D, terrainUiTex);
                drawDynamic(pv);
            }
            if (heldStack.empty()) return;
            // Предмет в руке: от точки правой руки к кисти, затем поза по виду предмета
            const ModelPart& arm = model[3];
            glm::mat4 im = glm::translate(em, arm.pivot);
            im = glm::rotate(im, arm.rot.z, glm::vec3(0, 0, 1));
            im = glm::rotate(im, arm.rot.y, glm::vec3(0, 1, 0));
            im = glm::rotate(im, arm.rot.x, glm::vec3(1, 0, 0));
            im = glm::scale(im, glm::vec3(16.f));
            im = glm::translate(im, glm::vec3(-0.0625f, 0.4375f, 0.0625f));
            std::vector<Vertex> hv;
            GLuint htex = terrainUiTex;
            if (itemIsCube(heldStack)) {
                im = glm::translate(im, glm::vec3(0.f, 0.1875f, -0.3125f));
                im = glm::rotate(im, glm::radians(20.f), glm::vec3(1, 0, 0));
                im = glm::rotate(im, glm::radians(45.f), glm::vec3(0, 1, 0));
                im = glm::scale(im, glm::vec3(0.375f, -0.375f, 0.375f));
                im = glm::translate(im, glm::vec3(-0.5f));
                std::vector<Vertex> bm;
                appendBlockModel(bm, (uint8_t)heldStack.id, 1.f, 0.f, blockHasVariants((uint8_t)heldStack.id) ? (uint8_t)heldStack.damage : (uint8_t)3);
                appendTransformed(hv, bm, im, sky, bl);
            } else {
                uint16_t id = heldStack.id;
                ToolInfo ti = toolInfo(id);
                bool full3D = (ti.type != Tool::None) || id == STICK || id == BONE || id == FISHING_ROD || id == BLAZE_ROD;
                if (id == BOW) {
                    im = glm::translate(im, glm::vec3(0.f, 0.125f, 0.3125f));
                    im = glm::rotate(im, glm::radians(-20.f), glm::vec3(0, 1, 0));
                    im = glm::scale(im, glm::vec3(0.625f, -0.625f, 0.625f));
                    im = glm::rotate(im, glm::radians(-100.f), glm::vec3(1, 0, 0));
                    im = glm::rotate(im, glm::radians(45.f), glm::vec3(0, 1, 0));
                } else if (full3D) {
                    if (id == FISHING_ROD) {
                        im = glm::rotate(im, glm::radians(180.f), glm::vec3(0, 0, 1));
                        im = glm::translate(im, glm::vec3(0.f, -0.125f, 0.f));
                    }
                    im = glm::translate(im, glm::vec3(0.f, 0.1875f, 0.f));
                    im = glm::scale(im, glm::vec3(0.625f, -0.625f, 0.625f));
                    im = glm::rotate(im, glm::radians(-100.f), glm::vec3(1, 0, 0));
                    im = glm::rotate(im, glm::radians(45.f), glm::vec3(0, 1, 0));
                } else {
                    im = glm::translate(im, glm::vec3(0.25f, 0.1875f, -0.1875f));
                    im = glm::scale(im, glm::vec3(0.375f));
                    im = glm::rotate(im, glm::radians(60.f), glm::vec3(0, 0, 1));
                    im = glm::rotate(im, glm::radians(-90.f), glm::vec3(1, 0, 0));
                    im = glm::rotate(im, glm::radians(20.f), glm::vec3(0, 0, 1));
                }
                im = glm::translate(im, glm::vec3(0.f, -0.3f, 0.f));
                im = glm::scale(im, glm::vec3(1.5f));
                im = glm::rotate(im, glm::radians(50.f), glm::vec3(0, 1, 0));
                im = glm::rotate(im, glm::radians(335.f), glm::vec3(0, 0, 1));
                im = glm::translate(im, glm::vec3(-0.9375f, -0.0625f, 0.f));
                im = glm::translate(im, glm::vec3(1.f, 0.f, 0.f));
                im = glm::scale(im, glm::vec3(-1.f, 1.f, 1.f));
                bool fromItems = false;
                const std::vector<Vertex>* mdl = &itemModels.extruded(heldStack, fromItems);
                if (id == FISHING_ROD && bobber) { mdl = &itemModels.extrudedTile(5, 5, true); fromItems = true; }
                if (id == BOW && bowCharge > 0) {
                    int row = bowCharge >= 18 ? 8 : bowCharge > 13 ? 7 : 6;
                    mdl = &itemModels.extrudedTile(5, row, true);
                    fromItems = true;
                }
                if (fromItems) htex = itemsTex;
                appendTransformed(hv, *mdl, im, sky, bl);
            }
            glBindTexture(GL_TEXTURE_2D, htex);
            drawDynamic(hv);
        };
        auto currentPlayerPose = [&]() {
            PlayerPose pp;
            pp.limbAmount = glm::mix(plPrevLimbAmount, plLimbAmount, partial);
            pp.limbSwing = plLimbSwing - plLimbAmount * (1.f - partial);
            pp.age = (float)g_in.tick + partial;
            pp.sneak = player.sneaking;
            pp.holding = !inv.slots[g_in.selected].empty();
            float sw = glm::mix(prevSwing, swing, partial);
            pp.swing = sw < 1.f ? sw : 0.f;
            pp.riding = ridingId != 0 || ridingPigId != 0;
            return pp;
        };

        // ---- Выпавшие предметы: кубики вращаются, плоские всегда смотрят на камеру
        if (!items.empty() || !pickupFx.empty()) {
            std::vector<Vertex> terrainBatch, itemBatch;
            std::vector<ItemEntity> drawList;
            drawList.reserve(items.size() + pickupFx.size());
            for (const ItemEntity& e : items)
                if (!e.dead && !e.stack.empty()) drawList.push_back(e);
            for (ItemEntity f : pickupFx) {
                // Подлёт к игроку: от места подбора к поясу игрока, квадратично (как EntityPickupFX)
                float k = std::min(1.f, (f.age + partial) / 3.f);
                k *= k;
                glm::vec3 target = glm::mix(player.prevPos, player.pos, partial) + glm::vec3(0, 0.6f, 0);
                f.pos = f.prev = f.pos + (target - f.pos) * k;
                drawList.push_back(f);
            }
            for (const ItemEntity& e : drawList) {
                glm::vec3 pos = glm::mix(e.prev, e.pos, partial);
                if (glm::length(pos - eye) > farDist) continue;
                float t = e.age + partial;
                float hover = std::sin(t / 10.f + e.bobOffset) * 0.1f + 0.1f;
                glm::ivec3 cell((int)std::floor(pos.x), (int)std::floor(pos.y + 0.2f), (int)std::floor(pos.z));
                float sky = world->getSkyLight(cell.x, cell.y, cell.z) / 15.f, bl = world->getBlockLight(cell.x, cell.y, cell.z) / 15.f;
                int copies = e.stack.count > 20 ? 4 : e.stack.count > 5 ? 3 : e.stack.count > 1 ? 2 : 1;
                uint32_t jr = (uint32_t)(e.bobOffset * 1000.f) | 1u;
                if (itemIsCube(e.stack)) {
                    std::vector<Vertex> model;
                    appendBlockModel(model, (uint8_t)e.stack.id, 1.f, 0.f,
                                     blockHasVariants((uint8_t)e.stack.id) ? (uint8_t)e.stack.damage : (uint8_t)3);
                    for (int k = 0; k < copies; ++k) {
                        glm::vec3 off(0.f);
                        if (k > 0) {
                            jr ^= jr << 13; jr ^= jr >> 17; jr ^= jr << 5;
                            off = glm::vec3((jr & 255) / 255.f - 0.5f, ((jr >> 8) & 255) / 255.f - 0.5f, ((jr >> 16) & 255) / 255.f - 0.5f) * 0.2f;
                        }
                        glm::mat4 m = glm::translate(glm::mat4(1.f), pos + glm::vec3(0, hover, 0) + off);
                        m = glm::rotate(m, t / 20.f + e.bobOffset, glm::vec3(0, 1, 0));
                        m = glm::scale(m, glm::vec3(0.25f));
                        m = glm::translate(m, glm::vec3(-0.5f, 0.f, -0.5f));
                        appendTransformed(terrainBatch, model, m, sky, bl);
                    }
                } else {
                    int col, row;
                    bool fromItems;
                    itemTile(e.stack, col, row, fromItems);
                    float u0 = col / 16.f, v0 = row / 16.f, du = 1.f / 16.f;
                    for (int k = 0; k < copies; ++k) {
                        glm::vec3 c = pos + glm::vec3(0, hover + 0.25f, 0);
                        if (k > 0) {
                            jr ^= jr << 13; jr ^= jr >> 17; jr ^= jr << 5;
                            c += (camRight * (((jr & 255) / 255.f) - 0.5f) + camUp * ((((jr >> 8) & 255) / 255.f) - 0.5f)) * 0.3f;
                        }
                        glm::vec3 r = camRight * 0.25f, u = camUp * 0.25f;
                        glm::vec3 a = c - r - u, b = c + r - u, cc = c + r + u, d = c - r + u;
                        Vertex va{a.x, a.y, a.z, u0, v0 + du, 1, sky, bl}, vb{b.x, b.y, b.z, u0 + du, v0 + du, 1, sky, bl};
                        Vertex vc{cc.x, cc.y, cc.z, u0 + du, v0, 1, sky, bl}, vd{d.x, d.y, d.z, u0, v0, 1, sky, bl};
                        auto& batch = fromItems ? itemBatch : terrainBatch;
                        batch.insert(batch.end(), {va, vb, vc, va, vc, vd});
                    }
                }
            }
            glBindTexture(GL_TEXTURE_2D, terrainUiTex);
            drawDynamic(terrainBatch);
            glBindTexture(GL_TEXTURE_2D, itemsTex);
            drawDynamic(itemBatch);
            glBindTexture(GL_TEXTURE_2D, terrainTex);
        }

        // ---- Мобы
        std::vector<Vertex> fireBatch; // пламя на горящих мобах (renderEntityOnFire), рисуется после всех мобов
        bossFrac = -1.f;
        bossName = "Boss health";
        for (const Mob& m : mobMgr.mobs) {
            if (m.type == MobType::EnderDragon && (m.health > 0 || m.deathTime < 200)) {
                float dist = glm::length(m.pos - eye);
                // В Краю полоска босса видна по всему острову (~384 блока), а не только в 128 блоках
                if (world->dimension() == 1 || dist <= 384.f) {
                    bossFrac = std::clamp(m.health / (float)mobDef(m.type).maxHealth, 0.f, 1.f);
                    bossName = "Boss health";
                    break;
                }
            } else if (m.type == MobType::Wither && (m.health > 0 || m.deathTime < 200)) {
                float dist = glm::length(m.pos - eye);
                if (dist <= 128.f) {
                    bossFrac = std::clamp(m.health / (float)mobDef(m.type).maxHealth, 0.f, 1.f);
                    bossName = "Wither";
                    break;
                }
            }
        }
        for (const Mob& m : mobMgr.mobs) {
            glm::vec3 pos = glm::mix(m.prev, m.pos, partial);
            if (glm::length(pos - eye) > farDist) continue;
            const MobDef& d = mobDef(m.type);
            glm::vec3 ext(d.halfWidth + 0.5f, d.height + 0.5f, d.halfWidth + 0.5f);
            if (!frustum.boxVisible(pos - glm::vec3(ext.x, 0.5f, ext.z), pos + ext)) continue;
            float dyaw = m.yaw - m.prevYaw;
            while (dyaw > 180.f) dyaw -= 360.f;
            while (dyaw < -180.f) dyaw += 360.f;
            float yaw = m.prevYaw + dyaw * partial;

            ModelPose pose;
            pose.limbAmount = glm::mix(m.prevLimbAmount, m.limbAmount, partial);
            pose.limbSwing = m.limbSwing - m.limbAmount * (1.f - partial);
            pose.age = m.age + partial;
            pose.sheared = m.sheared;
            float flap = glm::mix(m.prevWingFlap, m.wingFlap, partial);
            pose.wingFlap = (std::sin(flap) + 1.f) * m.wingSpeed;

            pose.sitting = m.sitting;
            pose.angry = m.angry;
            pose.carrying = m.heldBlock != 0;
            pose.aimBow = m.type == MobType::Skeleton && m.aiming && !m.dying();
            pose.swing = m.swingTicks >= 0 ? glm::mix(m.prevSwing, m.swing, partial) : 0.f;
            pose.squish = glm::mix(m.prevSquish, m.squish, partial);
            pose.tentacle = glm::mix(m.prevTentacle, m.tentacle, partial);
            pose.tail = m.tamed ? (0.55f - (20 - m.health) * 0.02f) * 3.1415927f : m.angry ? 1.5393804f : 0.62831855f;
            float deathAngle = m.dying() ? std::min(1.f, std::sqrt(std::max(0.f, (m.deathTime + partial - 1.f) / 20.f * 1.6f))) * 90.f : 0.f;
            glm::vec3 scale(m.scale), tint(1.f);
            if (m.type == MobType::Ghast) scale = glm::vec3(4.5f);
            if (m.type == MobType::CaveSpider) scale = glm::vec3(0.7f);
            if (m.type == MobType::Slime || m.type == MobType::MagmaCube) {
                float sq = pose.squish / (m.size * 0.5f + 1.f);
                scale = glm::vec3(m.size * (1.f + sq * 0.5f) * 1.f, m.size / (1.f + sq * 0.5f), m.size * (1.f + sq * 0.5f));
                pose.squish = 0.f;
                if (m.type == MobType::MagmaCube) pose.squish = glm::mix(m.prevSquish, m.squish, partial) < 0.f ? 0.f : glm::mix(m.prevSquish, m.squish, partial);
            }
            if (m.type == MobType::Creeper && (m.fuse > 0 || m.prevFuse > 0)) {
                // Крипер раздувается и мигает белым перед взрывом
                float f = glm::mix((float)m.prevFuse, (float)m.fuse, partial) / 28.f;
                float wob = 1.f + std::sin(f * 100.f) * f * 0.01f;
                f = std::clamp(f, 0.f, 1.f);
                f = f * f * f * f;
                scale = glm::vec3((1.f + f * 0.4f) * wob, (1.f + f * 0.1f) / wob, (1.f + f * 0.4f) * wob);
                if ((int)(glm::mix((float)m.prevFuse, (float)m.fuse, partial) / 28.f * 10.f) % 2 == 1) tint = glm::vec3(2.2f);
            }
            if (m.hurtTime > 0 || m.dying()) tint = glm::vec3(1.f, 0.55f, 0.55f);
            if (m.fireTicks > 0) tint *= glm::vec3(1.f, 0.8f, 0.6f);

            glm::vec3 c = pos + glm::vec3(0, d.height * 0.5f, 0);
            glm::ivec3 cell((int)std::floor(c.x), (int)std::floor(c.y), (int)std::floor(c.z));
            float sky = world->getSkyLight(cell.x, cell.y, cell.z) / 15.f, bl = world->getBlockLight(cell.x, cell.y, cell.z) / 15.f;
            glm::vec3 feet = pos;
            if (m.type == MobType::Ghast) feet.y -= 2.5f;
            glm::mat4 em = entityMatrix(feet, yaw, deathAngle, scale);
            if (m.type == MobType::Squid) {
                // RenderSquid.rotateCorpse: подъём на 0.5, поворот, наклон тела (pitch) и вращение вокруг оси (roll), опускание на 1.2
                float sp = glm::mix(m.prevSquidPitch, m.squidPitch, partial), sr = glm::mix(m.prevSquidRoll, m.squidRoll, partial);
                em = glm::translate(glm::mat4(1.f), pos + glm::vec3(0, 0.5f, 0));
                em = glm::rotate(em, glm::radians(-(yaw + 90.f)), glm::vec3(0, 1, 0));
                em = glm::rotate(em, glm::radians(sp), glm::vec3(1, 0, 0));
                em = glm::rotate(em, glm::radians(sr), glm::vec3(0, 1, 0));
                em = glm::translate(em, glm::vec3(0, -1.2f, 0));
                em = glm::scale(em, scale * glm::vec3(-1.f / 16.f, -1.f / 16.f, 1.f / 16.f));
                em = glm::translate(em, glm::vec3(0.f, -24.f, 0.f));
            }
            glm::vec2 ts = mobTexSize(m.type);
            ts.y = ts.x * mobTexAspect[(int)m.type];
            if (m.fireTicks > 0 && !isFireImmune(m.type)) {
                // Слои пламени, повёрнутые к камере: каждый выше на 0.45, уже на 10% и чуть ближе к зрителю
                float sz = (m.type == MobType::Slime || m.type == MobType::MagmaCube) ? (float)m.size : m.scale;
                float f = d.halfWidth * 2.f * sz * 1.4f;
                glm::vec3 toCam = camPos - pos;
                toCam.y = 0.f;
                toCam = glm::length(toCam) > 1e-3f ? glm::normalize(toCam) : glm::vec3(0, 0, 1);
                glm::vec3 side(toCam.z, 0.f, -toCam.x);
                float hw = 0.5f, left = d.height * sz / f, y = 0.f, depth = 0.3f;
                for (int i = 0; left > 0.f && i < 12; ++i) {
                    float u0 = 15.f / 16.f, u1 = 1.f, v0 = (i % 2 ? 2.f : 1.f) / 16.f, v1 = v0 + 1.f / 16.f;
                    if ((i / 2) % 2) std::swap(u0, u1);
                    glm::vec3 base = pos + toCam * (depth * f) + glm::vec3(0, y * f, 0);
                    glm::vec3 a = base - side * (hw * f), b = base + side * (hw * f);
                    glm::vec3 up(0, 1.4f * f, 0);
                    Vertex va{a.x, a.y, a.z, u0, v1, 1, 1, 1}, vb{b.x, b.y, b.z, u1, v1, 1, 1, 1};
                    Vertex vc{b.x + up.x, b.y + up.y, b.z + up.z, u1, v0, 1, 1, 1}, vd{a.x + up.x, a.y + up.y, a.z + up.z, u0, v0, 1, 1, 1};
                    fireBatch.insert(fireBatch.end(), {va, vb, vc, va, vc, vd});
                    left -= 0.45f; y += 0.45f; hw *= 0.9f; depth += 0.03f;
                }
            }

            std::vector<Vertex> mv;
            float flapNow = glm::mix(m.prevWingFlap, m.wingFlap, partial) / 6.2831853f;
            auto model = m.type == MobType::EnderDragon ? buildDragonModel(flapNow, pose.age, m.attackCounter == 1 ? 0.4f : 0.1f)
                       : m.type == MobType::EnderCrystal ? buildCrystalModel(pose.age)
                       : buildMobModel(m.type, pose, 0);
            if (m.type == MobType::EnderDragon || m.type == MobType::EnderCrystal) { sky = 1.f; bl = 1.f; }
            if (m.type == MobType::EnderCrystal) em = glm::translate(glm::mat4(1.f), pos); // своя модель, без зеркала
            emitModel(mv, model, em, sky, bl, ts.x, ts.y);
            glUniform3fv(tintLoc, 1, glm::value_ptr(tint));
            GLuint mt = mobTex[(int)m.type];
            if (m.type == MobType::Cat) {
                mt = (m.color == 1) ? catRedTex : (m.color == 2) ? catSiameseTex : mobTex[(int)MobType::Cat];
            }
            if (m.type == MobType::Wolf) mt = m.tamed ? wolfTameTex : m.angry ? wolfAngryTex : mt;
            if (m.type == MobType::Ghast && m.attackCounter > 10) mt = ghastFireTex;
            if (m.type == MobType::Villager) mt = villagerTex[m.color % 5]; // профессия
            if (m.type == MobType::Wither && m.invulnerable > 0 && witherInvulTex && (m.invulnerable > 80 || (m.invulnerable / 5) % 2 != 1))
                mt = witherInvulTex;
            if (isFireImmune(m.type) && m.type != MobType::PigZombie) { sky = 1.f; bl = 1.f; }
            if (m.type == MobType::EnderDragon && m.dying() && shuffleTex) {
                // RenderDragon 1.0: сначала глубина только там, где альфа shuffle.png больше доли смерти,
                // затем обычная текстура строго по этой глубине — дракон рассыпается на пиксели
                float f5 = std::min(1.f, (m.deathTime + partial) / 200.f);
                GLint alphaRefLoc = glGetUniformLocation(chunkProg, "uAlphaRef");
                GLint oldDepth;
                glGetIntegerv(GL_DEPTH_FUNC, &oldDepth);
                glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                glUniform1f(alphaRefLoc, f5);
                glBindTexture(GL_TEXTURE_2D, shuffleTex);
                drawDynamic(mv);
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                glUniform1f(alphaRefLoc, 0.f);
                glDepthFunc(GL_EQUAL);
                glBindTexture(GL_TEXTURE_2D, mt);
                drawDynamic(mv);
                glDepthFunc(oldDepth);
            } else {
                glBindTexture(GL_TEXTURE_2D, mt);
                drawDynamic(mv);
            }
            if (m.type == MobType::EnderDragon && m.dying()) {
                // Лучи света из центра (RenderDragon.renderEquippedItems): их число растёт, в конце гаснут
                float f1 = std::min(1.f, (m.deathTime + partial) / 200.f), f2 = f1 > 0.8f ? (f1 - 0.8f) / 0.2f : 0.f;
                uint32_t rr = 432u;
                auto rf = [&]() { rr ^= rr << 13; rr ^= rr >> 17; rr ^= rr << 5; return (rr & 0xFFFFFF) / float(0x1000000); };
                glm::vec3 c0 = pos + glm::vec3(0.f, 2.f, 0.f);
                std::vector<Vertex> rays;
                int n = (int)((f1 + f1 * f1) / 2.f * 60.f);
                glm::mat4 R(1.f);
                for (int i = 0; i < n; ++i) {
                    R = glm::rotate(R, glm::radians(rf() * 360.f), glm::vec3(1, 0, 0));
                    R = glm::rotate(R, glm::radians(rf() * 360.f), glm::vec3(0, 1, 0));
                    R = glm::rotate(R, glm::radians(rf() * 360.f), glm::vec3(0, 0, 1));
                    R = glm::rotate(R, glm::radians(rf() * 360.f), glm::vec3(1, 0, 0));
                    R = glm::rotate(R, glm::radians(rf() * 360.f), glm::vec3(0, 1, 0));
                    R = glm::rotate(R, glm::radians(rf() * 360.f + f1 * 90.f), glm::vec3(0, 0, 1));
                    float len = rf() * 20.f + 5.f + f2 * 10.f, w = rf() * 2.f + 1.f + f2 * 2.f;
                    auto P = [&](float x, float y, float z) { return c0 + glm::vec3(R * glm::vec4(x, y, z, 0.f)); };
                    glm::vec3 p1 = P(-0.866f * w, len, -0.5f * w), p2 = P(0.866f * w, len, -0.5f * w), p3 = P(0.f, len, w);
                    // Сложение цветов: середина белая (гаснет к концу), края чёрные = прозрачные
                    float k = 1.f - f2;
                    Vertex vc{c0.x, c0.y, c0.z, 0.5f, 0.5f, 1, 1, 1, k, k, k};
                    auto E = [&](glm::vec3 q) { return Vertex{q.x, q.y, q.z, 0.5f, 0.5f, 1, 1, 1, 0.f, 0.f, 0.f}; };
                    rays.insert(rays.end(), {vc, E(p1), E(p2), vc, E(p2), E(p3), vc, E(p3), E(p1)});
                }
                if (!rays.empty()) {
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_ONE, GL_ONE);
                    glDepthMask(GL_FALSE);
                    glDisable(GL_CULL_FACE);
                    glUniform3f(tintLoc, 1, 1, 1);
                    glBindTexture(GL_TEXTURE_2D, ui.whiteTex);
                    drawDynamic(rays);
                    glDepthMask(GL_TRUE);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                    glDisable(GL_BLEND);
                }
            }
            if ((m.type == MobType::Skeleton || m.type == MobType::PigZombie || m.type == MobType::WitherSkeleton) && model.size() > 2) {
                // Предмет в правой руке (RenderBiped): лук у скелета, золотой меч у свинозомби, каменный меч у визер-скелета.
                // От точки руки — сдвиг к кисти, затем своя поза для лука и для «объёмного» предмета (меча)
                bool bow = m.type == MobType::Skeleton;
                const ModelPart& arm = model[2];
                glm::mat4 im = glm::translate(em, arm.pivot);
                im = glm::rotate(im, arm.rot.z, glm::vec3(0, 0, 1));
                im = glm::rotate(im, arm.rot.y, glm::vec3(0, 1, 0));
                im = glm::rotate(im, arm.rot.x, glm::vec3(1, 0, 0));
                im = glm::scale(im, glm::vec3(16.f)); // дальше — в блоках, как в оригинале
                im = glm::translate(im, glm::vec3(-0.0625f, 0.4375f, 0.0625f));
                if (bow) {
                    im = glm::translate(im, glm::vec3(0.f, 0.125f, 0.3125f));
                    im = glm::rotate(im, glm::radians(-20.f), glm::vec3(0, 1, 0));
                } else {
                    im = glm::translate(im, glm::vec3(0.f, 0.1875f, 0.f));
                }
                im = glm::scale(im, glm::vec3(0.625f, -0.625f, 0.625f));
                im = glm::rotate(im, glm::radians(-100.f), glm::vec3(1, 0, 0));
                im = glm::rotate(im, glm::radians(45.f), glm::vec3(0, 1, 0));
                im = glm::translate(im, glm::vec3(0.f, -0.3f, 0.f));
                im = glm::scale(im, glm::vec3(1.5f));
                im = glm::rotate(im, glm::radians(50.f), glm::vec3(0, 1, 0));
                im = glm::rotate(im, glm::radians(335.f), glm::vec3(0, 0, 1));
                im = glm::translate(im, glm::vec3(-0.9375f, -0.0625f, 0.f));
                im = glm::translate(im, glm::vec3(1.f, 0.f, 0.f));
                im = glm::scale(im, glm::vec3(-1.f, 1.f, 1.f));
                // Натяжение: последняя секунда перед выстрелом (иконки лука 5,6..5,8 как у игрока)
                int col = 5, row = 1;
                if (m.type == MobType::WitherSkeleton) {
                    itemIcon(STONE_SWORD, col, row);
                } else if (!bow) {
                    itemIcon(GOLD_SWORD, col, row);
                } else if (m.aiming && !m.dying() && m.attackCooldown <= 20) {
                    float pull = (20 - m.attackCooldown) / 20.f;
                    row = pull >= 0.9f ? 8 : pull > 0.65f ? 7 : 6;
                }
                std::vector<Vertex> bv;
                appendTransformed(bv, itemModels.extrudedTile(col, row, true), im, sky, bl);
                glBindTexture(GL_TEXTURE_2D, itemsTex);
                drawDynamic(bv);
            }
            if (m.type == MobType::Sheep && !m.sheared) {
                // Шерсть красится цветом овцы (EntitySheep.fleeceColorTable)
                static const float FLEECE[16][3] = {{1, 1, 1}, {0.95f, 0.7f, 0.2f}, {0.9f, 0.5f, 0.85f}, {0.6f, 0.7f, 0.95f},
                                                   {0.9f, 0.9f, 0.2f}, {0.5f, 0.8f, 0.1f}, {0.95f, 0.7f, 0.8f}, {0.3f, 0.3f, 0.3f},
                                                   {0.6f, 0.6f, 0.6f}, {0.3f, 0.6f, 0.7f}, {0.7f, 0.4f, 0.9f}, {0.2f, 0.4f, 0.8f},
                                                   {0.5f, 0.4f, 0.3f}, {0.4f, 0.5f, 0.2f}, {0.8f, 0.3f, 0.3f}, {0.1f, 0.1f, 0.1f}};
                const float* fc = FLEECE[m.color & 15];
                glm::vec3 ft = tint * glm::vec3(fc[0], fc[1], fc[2]);
                glUniform3fv(tintLoc, 1, glm::value_ptr(ft));
                std::vector<Vertex> fur;
                emitModel(fur, buildMobModel(m.type, pose, 1), em, sky, bl);
                glBindTexture(GL_TEXTURE_2D, furTex);
                drawDynamic(fur);
                glUniform3fv(tintLoc, 1, glm::value_ptr(tint));
            }
            if (m.type == MobType::Wolf && m.tamed && wolfCollarTex) {
                static const float FLEECE[16][3] = {{1, 1, 1}, {0.95f, 0.7f, 0.2f}, {0.9f, 0.5f, 0.85f}, {0.6f, 0.7f, 0.95f},
                                                   {0.9f, 0.9f, 0.2f}, {0.5f, 0.8f, 0.1f}, {0.95f, 0.7f, 0.8f}, {0.3f, 0.3f, 0.3f},
                                                   {0.6f, 0.6f, 0.6f}, {0.3f, 0.6f, 0.7f}, {0.7f, 0.4f, 0.9f}, {0.2f, 0.4f, 0.8f},
                                                   {0.5f, 0.4f, 0.3f}, {0.4f, 0.5f, 0.2f}, {0.8f, 0.3f, 0.3f}, {0.1f, 0.1f, 0.1f}};
                const float* fc = FLEECE[m.color & 15];
                glm::vec3 ct = tint * glm::vec3(fc[0], fc[1], fc[2]);
                glUniform3fv(tintLoc, 1, glm::value_ptr(ct));
                std::vector<Vertex> cv;
                emitModel(cv, buildMobModel(m.type, pose, 0), em, sky, bl);
                glBindTexture(GL_TEXTURE_2D, wolfCollarTex);
                drawDynamic(cv);
                glUniform3fv(tintLoc, 1, glm::value_ptr(tint));
            }
            if (m.type == MobType::Wither && m.invulnerable <= 0 && m.health <= 150 && witherArmorTex) {
                std::vector<Vertex> pv;
                auto pm = buildMobModel(m.type, pose, 0);
                for (auto& pp : pm)
                    for (auto& bx : pp.boxes) bx.inflate += 2.f;
                emitModel(pv, pm, em, 1.f, 1.f, 64.f, 64.f);
                float sh = (float)(g_in.tick + partial) * 0.01f;
                for (auto& v : pv) { v.u += sh; v.v += sh; }
                glEnable(GL_BLEND);
                glBlendFunc(GL_ONE, GL_ONE);
                glUniform3f(tintLoc, 0.5f, 0.5f, 0.5f);
                glBindTexture(GL_TEXTURE_2D, witherArmorTex);
                drawDynamic(pv);
                glDisable(GL_BLEND);
                glUniform3fv(tintLoc, 1, glm::value_ptr(tint));
            }
            if (m.type == MobType::Creeper && m.charged && powerTex) {
                std::vector<Vertex> pv;
                auto pm = buildMobModel(m.type, pose, 0);
                for (auto& pp : pm)
                    for (auto& bx : pp.boxes) bx.inflate += 2.f;
                emitModel(pv, pm, em, 1.f, 1.f);
                float sh = (float)(g_in.tick + partial) * 0.01f;
                for (auto& v : pv) { v.u += sh; v.v += sh; }
                glEnable(GL_BLEND);
                glBlendFunc(GL_ONE, GL_ONE);
                glUniform3f(tintLoc, 0.5f, 0.5f, 0.5f);
                glBindTexture(GL_TEXTURE_2D, powerTex);
                drawDynamic(pv);
                glDisable(GL_BLEND);
                glUniform3fv(tintLoc, 1, glm::value_ptr(tint));
            }
            if (m.type == MobType::Pig && m.saddled) {
                std::vector<Vertex> sv;
                auto sm = buildMobModel(m.type, pose, 0);
                for (auto& pp : sm)
                    for (auto& bx : pp.boxes) bx.inflate += 0.5f;
                emitModel(sv, sm, em, sky, bl);
                glBindTexture(GL_TEXTURE_2D, saddleTex);
                drawDynamic(sv);
            }
            if (m.type == MobType::Slime) {
                // Полупрозрачная оболочка поверх ядра
                std::vector<Vertex> shell;
                emitModel(shell, buildMobModel(m.type, pose, 1), em, sky, bl);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDepthMask(GL_FALSE);
                glBindTexture(GL_TEXTURE_2D, mt);
                drawDynamic(shell);
                glDepthMask(GL_TRUE);
                glDisable(GL_BLEND);
            }
            if (m.type == MobType::Mooshroom && m.growingAge >= 0) {
                // Красные грибы на спине
                std::vector<Vertex> mush;
                const int t = T(12, 1);
                float u0 = (t % 16) / 16.f, v0 = (t / 16) / 16.f, du = 1.f / 16.f;
                float yr = glm::radians(-(yaw + 90.f));
                glm::vec3 fwd(-std::sin(yr) * -1.f, 0, std::cos(yr) * -1.f);
                (void)fwd;
                glm::mat4 bodyM = entityMatrix(pos, yaw, deathAngle, scale);
                const glm::vec3 spots[3] = {{-2.f, 5.f, 2.f}, {3.f, 5.f, -4.f}, {0.f, -2.f, -6.f}};
                for (int si = 0; si < 3; ++si) {
                    glm::vec3 base = glm::vec3(bodyM * glm::vec4(spots[si].x, 24.f - 12.f - 5.f + (si == 2 ? -8.f : 0.f), spots[si].z, 1.f));
                    for (int q = 0; q < 2; ++q) {
                        float a = glm::radians(yaw + 45.f + q * 90.f);
                        glm::vec3 dx(std::cos(a) * 0.35f, 0, std::sin(a) * 0.35f);
                        Vertex va{base.x - dx.x, base.y, base.z - dx.z, u0, v0 + du, 1, sky, bl}, vb{base.x + dx.x, base.y, base.z + dx.z, u0 + du, v0 + du, 1, sky, bl};
                        Vertex vc{base.x + dx.x, base.y + 0.7f, base.z + dx.z, u0 + du, v0, 1, sky, bl}, vd{base.x - dx.x, base.y + 0.7f, base.z - dx.z, u0, v0, 1, sky, bl};
                        mush.insert(mush.end(), {va, vb, vc, va, vc, vd, va, vc, vb, va, vd, vc});
                    }
                }
                glBindTexture(GL_TEXTURE_2D, terrainUiTex);
                drawDynamic(mush);
            }
            if (m.type == MobType::SnowGolem || (m.type == MobType::Enderman && m.heldBlock != 0)) {
                // Тыква на голове снеговика / блок в руках эндермена
                std::vector<Vertex> bm;
                uint8_t blk = m.type == MobType::SnowGolem ? PUMPKIN : m.heldBlock;
                appendBlockModel(bm, blk, sky, bl, m.type == MobType::SnowGolem ? 2 : m.heldMeta);
                glm::mat4 bmat = entityMatrix(pos, yaw, deathAngle, glm::vec3(m.scale));
                if (m.type == MobType::SnowGolem) {
                    bmat = glm::translate(bmat, glm::vec3(0.f, 1.25f, 0.f));
                    bmat = glm::rotate(bmat, glm::radians(-pose.headYaw * 57.29578f), glm::vec3(0, 1, 0));
                    bmat = glm::scale(bmat, glm::vec3(0.625f));
                } else {
                    bmat = glm::translate(bmat, glm::vec3(0.f, 1.7f, 0.45f));
                    bmat = glm::scale(bmat, glm::vec3(0.5f));
                }
                bmat = glm::translate(bmat, glm::vec3(-0.5f, 0.f, -0.5f));
                for (auto& v : bm) {
                    glm::vec3 wv = glm::vec3(bmat * glm::vec4(v.x, v.y, v.z, 1.f));
                    v.x = wv.x; v.y = wv.y; v.z = wv.z;
                }
                glBindTexture(GL_TEXTURE_2D, terrainUiTex);
                drawDynamic(bm);
            }
            if (m.type == MobType::EnderDragon && !m.dying()) {
                std::vector<Vertex> eyes;
                emitModel(eyes, model, em, 0.f, 1.f, 256.f, 256.f);
                glEnable(GL_BLEND);
                glBlendFunc(GL_ONE, GL_ONE);
                glUniform3f(tintLoc, 1, 1, 1);
                glBindTexture(GL_TEXTURE_2D, dragonEyesTex);
                drawDynamic(eyes);
                glDisable(GL_BLEND);
                // Луч лечения от кристалла
                for (const Mob& o : mobMgr.mobs)
                    if (o.id == m.targetId && m.targetId) {
                        // RenderDragon 1.0: 8-гранный конус от дракона (узкий, тёмный) к подпрыгивающему кристаллу
                        // (широкий, светлый), текстура beam.png ползёт по длине
                        float ct = o.age + partial;
                        float bob = std::sin(ct * 0.2f) / 2.f + 0.5f;
                        bob = (bob * bob + bob) * 0.2f;
                        glm::vec3 a = pos + glm::vec3(0, 2.f, 0), b2 = glm::mix(o.prev, o.pos, partial) + glm::vec3(0, bob + 1.f, 0);
                        glm::vec3 axis = b2 - a;
                        float len = glm::length(axis);
                        if (len > 1e-3f) {
                            glm::vec3 ax = axis / len;
                            glm::vec3 u = glm::normalize(glm::cross(ax, std::abs(ax.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
                            glm::vec3 v = glm::cross(ax, u);
                            float tk = (float)m.age + partial;
                            float vFar = -tk * 0.01f, vNear = len / 32.f - tk * 0.01f;
                            std::vector<Vertex> beam;
                            auto ring = [&](int j, float r, glm::vec3 c, float col, float vv) {
                                float an = (j % 8) * 6.2831853f / 8.f;
                                glm::vec3 q = c + (u * std::sin(an) + v * std::cos(an)) * r;
                                return Vertex{q.x, q.y, q.z, j / 8.f, vv, 1, 1, 1, col, col, col};
                            };
                            for (int j = 0; j < 8; ++j) {
                                Vertex n0 = ring(j, 0.15f, a, 0.f, vNear), n1 = ring(j + 1, 0.15f, a, 0.f, vNear);
                                Vertex f0 = ring(j, 0.75f, b2, 1.f, vFar), f1 = ring(j + 1, 0.75f, b2, 1.f, vFar);
                                n1.u = f1.u = (j + 1) / 8.f;
                                beam.insert(beam.end(), {n0, f0, f1, n0, f1, n1});
                            }
                            glDisable(GL_CULL_FACE);
                            glBindTexture(GL_TEXTURE_2D, beamTex);
                            drawDynamic(beam);
                        }
                    }
            }
            if (m.type == MobType::Enderman && !m.dying()) {
                std::vector<Vertex> eyes;
                emitModel(eyes, model, em, 0.f, 1.f);
                glEnable(GL_BLEND);
                glBlendFunc(GL_ONE, GL_ONE);
                glUniform3f(tintLoc, 1, 1, 1);
                glBindTexture(GL_TEXTURE_2D, enderEyesTex);
                drawDynamic(eyes);
                glDisable(GL_BLEND);
            }
            if (isSpiderLike(m.type) && !m.dying()) {
                // Светящиеся глаза: поверх, аддитивно, полная яркость
                std::vector<Vertex> eyes;
                emitModel(eyes, model, em, 0.f, 1.f);
                glEnable(GL_BLEND);
                glBlendFunc(GL_ONE, GL_ONE);
                glUniform3f(tintLoc, 1, 1, 1);
                glBindTexture(GL_TEXTURE_2D, eyesTex);
                drawDynamic(eyes);
                glDisable(GL_BLEND);
            }
        }
        glUniform3f(tintLoc, 1, 1, 1);
        // Сам игрок — виден только с камеры от третьего лица
        if (camMode != 0 && !player.dead) {
            glm::vec3 feet = glm::mix(player.prevPos, player.pos, partial);
            float by = plPrevBodyYaw + [&] { float d = plBodyYaw - plPrevBodyYaw; while (d > 180.f) d -= 360.f; while (d < -180.f) d += 360.f; return d; }() * partial;
            PlayerPose pp = currentPlayerPose();
            pp.headYaw = glm::radians(player.yaw - by);
            pp.headPitch = glm::radians(-player.pitch);
            glm::mat4 root = glm::translate(glm::mat4(1.f), feet - glm::vec3(0, player.sneaking ? 0.125f : 0.f, 0));
            root = glm::rotate(root, glm::radians(-(by + 90.f)), glm::vec3(0, 1, 0));
            glm::ivec3 c((int)std::floor(feet.x), (int)std::floor(feet.y + 1.f), (int)std::floor(feet.z));
            if (player.hurtTime > 0) glUniform3f(tintLoc, 1.f, 0.55f, 0.55f);
            drawPlayerModel(root, pp, world->getSkyLight(c.x, c.y, c.z) / 15.f, world->getBlockLight(c.x, c.y, c.z) / 15.f,
                            inv.slots[g_in.selected]);
            glUniform3f(tintLoc, 1, 1, 1);
        }
        // Другие игроки (сетевая игра)
        for (auto& [oid, rp] : others) {
            if (!rp.has || (rp.net.flags & 8) || rp.net.dim != dimension) continue;
            glm::vec3 feet = glm::mix(rp.prevPos, rp.pos, partial);
            if (glm::length(feet - eye) > farDist) continue;
            float dby = rp.bodyYaw - rp.prevBodyYaw;
            while (dby > 180.f) dby -= 360.f;
            while (dby < -180.f) dby += 360.f;
            float by = rp.prevBodyYaw + dby * partial;
            PlayerPose pp;
            pp.limbAmount = glm::mix(rp.prevLimbAmount, rp.limbAmount, partial);
            pp.limbSwing = rp.limbSwing - rp.limbAmount * (1.f - partial);
            pp.age = (float)g_in.tick + partial + oid * 37.f;
            pp.sneak = (rp.net.flags & 2) != 0;
            pp.holding = rp.net.held != 0;
            float sw = glm::mix(rp.prevSwing, rp.swing, partial);
            pp.swing = sw < 1.f ? sw : 0.f;
            pp.headYaw = glm::radians(rp.yaw - by);
            pp.headPitch = glm::radians(-rp.pitch);
            glm::mat4 root = glm::translate(glm::mat4(1.f), feet - glm::vec3(0, pp.sneak ? 0.125f : 0.f, 0));
            root = glm::rotate(root, glm::radians(-(by + 90.f)), glm::vec3(0, 1, 0));
            glm::ivec3 c((int)std::floor(feet.x), (int)std::floor(feet.y + 1.f), (int)std::floor(feet.z));
            ItemStack h = rp.net.held ? makeStack(rp.net.held, 1, rp.net.heldDamage) : ItemStack{};
            glUniform3f(tintLoc, 1.f, (rp.net.flags & 32) ? 0.55f : 1.f, (rp.net.flags & 32) ? 0.55f : 1.f);
            drawPlayerModel(root, pp, world->getSkyLight(c.x, c.y, c.z) / 15.f, world->getBlockLight(c.x, c.y, c.z) / 15.f, h, rp.net.armor);
            glUniform3f(tintLoc, 1, 1, 1);
        }
        if (!fireBatch.empty()) {
            glBindTexture(GL_TEXTURE_2D, terrainTex);
            drawDynamic(fireBatch);
        }
        for (auto& [key, sp] : mobMgr.spawners) {
            glm::vec3 c(sp.x + 0.5f, sp.y + 0.0f, sp.z + 0.5f);
            if (glm::length(c - eye) > 24.f) continue;
            float spin = glm::mix(sp.prevSpin, sp.spin, partial);
            ModelPose pose;
            pose.age = (float)worldTime + partial;
            float scale = sp.type == MobType::Spider ? 0.3f : 0.4375f;
            glm::mat4 em = entityMatrix(c + glm::vec3(0, 0.4f - scale * 0.5f, 0), spin, 0.f, glm::vec3(scale));
            std::vector<Vertex> mv;
            glm::ivec3 cell(sp.x, sp.y, sp.z);
            glm::vec2 ts = mobTexSize(sp.type);
            ts.y = ts.x * mobTexAspect[(int)sp.type];
            emitModel(mv, buildMobModel(sp.type, pose, 0), em, world->getSkyLight(cell.x, cell.y + 1, cell.z) / 15.f,
                      world->getBlockLight(cell.x, cell.y + 1, cell.z) / 15.f, ts.x, ts.y);
            glBindTexture(GL_TEXTURE_2D, mobTex[(int)sp.type]);
            drawDynamic(mv);
        }
        glBindTexture(GL_TEXTURE_2D, terrainTex);

        // ---- Таблички (TileEntitySignRenderer): доска из досок, столбик, 4 строки чёрным текстом
        if (!world->signs.empty()) {
            std::vector<Vertex> sv, tv;
            for (auto& [key, lines] : world->signs) {
                glm::ivec3 sp = posFromKey(key);
                uint8_t sb = world->getBlock(sp.x, sp.y, sp.z);
                if (sb != SIGN_POST && sb != WALL_SIGN) continue;
                glm::vec3 centerW = glm::vec3(sp) + 0.5f;
                if (glm::length(centerW - camPos) > 64.f) continue;
                float sky = world->getSkyLight(sp.x, sp.y, sp.z) / 15.f, bl = world->getBlockLight(sp.x, sp.y, sp.z) / 15.f;
                uint8_t sm = world->getMeta(sp.x, sp.y, sp.z);
                // Локальная система: f — нормаль лицевой стороны, r — вправо по доске
                glm::vec3 f, origin;
                if (sb == SIGN_POST) {
                    float a = sm / 16.f * 6.2831853f;
                    f = glm::vec3(std::cos(a), 0, std::sin(a));
                    origin = glm::vec3(sp) + glm::vec3(0.5f, 0.75f, 0.5f);
                } else {
                    static const float WD[4][2] = {{-1, 0}, {0, -1}, {1, 0}, {0, 1}}; // от стены наружу
                    f = glm::vec3(WD[sm & 3][0], 0, WD[sm & 3][1]);
                    origin = glm::vec3(sp) + glm::vec3(0.5f, 0.53f, 0.5f) - f * (0.5f - 1.f / 24.f);
                }
                glm::vec3 r = glm::normalize(glm::cross(glm::vec3(0, 1, 0), f)), up(0, 1, 0);
                auto box = [&](glm::vec3 c, float hw, float hh, float hd, int tile) {
                    // Коробка вокруг c: полуразмеры по r, up, f
                    glm::vec3 ax[3] = {r * hw, up * hh, f * hd};
                    for (int d = 0; d < 6; ++d) {
                        int axis = d / 2;
                        float sgn = (d % 2) ? -1.f : 1.f;
                        glm::vec3 n = ax[axis] * sgn, a = ax[(axis + 1) % 3], b = ax[(axis + 2) % 3];
                        glm::vec3 q[4] = {c + n - a - b, c + n + a - b, c + n + a + b, c + n - a + b};
                        float u0 = (tile % 16) / 16.f, v0 = (tile / 16) / 16.f, du = 1.f / 16.f;
                        glm::vec2 uv[4] = {{u0, v0 + du}, {u0 + du, v0 + du}, {u0 + du, v0}, {u0, v0}};
                        float shade = axis == 1 ? (sgn > 0 ? 1.f : 0.5f) : 0.8f;
                        Vertex v4[4];
                        for (int i = 0; i < 4; ++i) v4[i] = Vertex{q[i].x, q[i].y, q[i].z, uv[i].x, uv[i].y, shade, sky, bl};
                        sv.insert(sv.end(), {v4[0], v4[1], v4[2], v4[0], v4[2], v4[3], v4[0], v4[2], v4[1], v4[0], v4[3], v4[2]});
                    }
                };
                box(origin, 0.5f, 0.25f, 1.f / 24.f, T(4, 0));
                if (sb == SIGN_POST) box(origin - glm::vec3(0, 0.5f, 0), 1.f / 24.f, 0.25f + 1.f / 48.f, 1.f / 24.f, T(4, 1));
                // Текст на лицевой стороне: 1 пиксель шрифта = 1/90 блока, строки по 10 пикселей
                const float px = 1.f / 90.f;
                glm::vec3 face = origin + f * (1.f / 24.f + 0.005f);
                for (int li = 0; li < 4; ++li) {
                    const std::string& line = lines[li];
                    float w = 0.f;
                    for (unsigned char ch : line) w += ui.glyphWidth(ch) + 1;
                    float x0 = -w / 2.f, y0 = 20.f - li * 10.f - 1.f; // верх строки относительно центра
                    for (unsigned char ch : line) {
                        if (ch != ' ') {
                            float gu = (ch % 16) * 8.f / 128.f, gv = (ch / 16) * 8.f / 128.f, gs = 8.f / 128.f;
                            glm::vec3 p0 = face + r * (x0 * px) + up * (y0 * px), p1 = p0 + r * (8 * px), p2 = p1 - up * (8 * px), p3 = p0 - up * (8 * px);
                            Vertex a{p0.x, p0.y, p0.z, gu, gv, 1, sky, bl}, b{p1.x, p1.y, p1.z, gu + gs, gv, 1, sky, bl};
                            Vertex c{p2.x, p2.y, p2.z, gu + gs, gv + gs, 1, sky, bl}, d{p3.x, p3.y, p3.z, gu, gv + gs, 1, sky, bl};
                            for (Vertex* vv : {&a, &b, &c, &d}) { vv->r = vv->g = vv->b = 0.f; }
                            tv.insert(tv.end(), {a, b, c, a, c, d, a, c, b, a, d, c});
                        }
                        x0 += ui.glyphWidth(ch) + 1;
                    }
                }
            }
            glBindTexture(GL_TEXTURE_2D, terrainUiTex);
            drawDynamic(sv);
            glBindTexture(GL_TEXTURE_2D, fontTex);
            drawDynamic(tv);
            glBindTexture(GL_TEXTURE_2D, terrainTex);
        }

        // ---- Поплавок и леска
        if (bobber) {
            std::vector<Vertex> bv;
            glm::vec3 bp = glm::mix(bobPrev, bobPos, partial);
            glm::ivec3 cell((int)std::floor(bp.x), (int)std::floor(bp.y), (int)std::floor(bp.z));
            float sky = world->getSkyLight(cell.x, cell.y, cell.z) / 15.f, bl = world->getBlockLight(cell.x, cell.y, cell.z) / 15.f;
            // Поплавок (RenderFish): спрайт (1,2) из particles.png лицом к камере, размер 0.5
            std::vector<Vertex> fv;
            {
                const float u0 = 8.f / 128.f, v0 = 16.f / 128.f, du = 8.f / 128.f;
                glm::vec3 r = camRight * 0.25f, u = camUp * 0.25f;
                glm::vec3 a = bp - r - u, b = bp + r - u, c = bp + r + u, d = bp - r + u;
                Vertex va{a.x, a.y, a.z, u0, v0 + du, 1, sky, bl}, vb{b.x, b.y, b.z, u0 + du, v0 + du, 1, sky, bl};
                Vertex vc{c.x, c.y, c.z, u0 + du, v0, 1, sky, bl}, vd{d.x, d.y, d.z, u0, v0, 1, sky, bl};
                fv.insert(fv.end(), {va, vb, vc, va, vc, vd});
            }
            glBindTexture(GL_TEXTURE_2D, particlesTex);
            drawDynamic(fv);
            glm::vec3 hand = camPos + camRight * 0.35f - camUp * 0.25f + look * 0.6f;
            glm::vec3 axis = bp - hand, side = glm::normalize(glm::cross(axis, camPos - hand)) * 0.012f;
            const int t = 64 + 15 * 16 + 1; // тайл чёрной шерсти (1,7)
            (void)t;
            float u0 = 1.f / 16.f + 0.01f, v0 = 7.f / 16.f + 0.01f;
            for (int i = 0; i < 12; ++i) {
                float a0 = i / 12.f, a1 = (i + 1) / 12.f;
                glm::vec3 p0 = hand + axis * a0 - glm::vec3(0, std::sin(a0 * 3.1416f) * 0.3f, 0);
                glm::vec3 p1 = hand + axis * a1 - glm::vec3(0, std::sin(a1 * 3.1416f) * 0.3f, 0);
                Vertex q0{p0.x - side.x, p0.y - side.y, p0.z - side.z, u0, v0, 1, sky, bl}, q1{p0.x + side.x, p0.y + side.y, p0.z + side.z, u0, v0, 1, sky, bl};
                Vertex q2{p1.x + side.x, p1.y + side.y, p1.z + side.z, u0, v0, 1, sky, bl}, q3{p1.x - side.x, p1.y - side.y, p1.z - side.z, u0, v0, 1, sky, bl};
                bv.insert(bv.end(), {q0, q1, q2, q0, q2, q3, q0, q2, q1, q0, q3, q2});
            }
            glBindTexture(GL_TEXTURE_2D, terrainUiTex);
            drawDynamic(bv);
            glBindTexture(GL_TEXTURE_2D, terrainTex);
        }

        // ---- Вагонетки и лодки: коробки из текстур блоков (железо / доски), с сундуком или печью внутри
        if (!mobMgr.vehicles.empty()) {
            std::vector<Vertex> vv;
            auto boxW = [&](const glm::mat4& mm, glm::vec3 mn, glm::vec3 mx, int tile, float sky, float bl) {
                std::vector<Vertex> tmp;
                int tex[6] = {tile, tile, tile, tile, tile, tile};
                (void)tex;
                appendBlockModel(tmp, IRON_BLOCK, sky, bl); // единичный куб; тайл подменим ниже
                float tu = (tile % 16) / 16.f, tv = (tile / 16) / 16.f, ib = (T(6, 1) % 16) / 16.f, jb = (T(6, 1) / 16) / 16.f;
                for (Vertex q : tmp) {
                    glm::vec3 lp = mn + glm::vec3(q.x, q.y, q.z) * (mx - mn);
                    glm::vec3 wp = glm::vec3(mm * glm::vec4(lp, 1.f));
                    q.x = wp.x; q.y = wp.y; q.z = wp.z;
                    q.u = q.u - ib + tu;
                    q.v = q.v - jb + tv;
                    vv.push_back(q);
                }
            };
            for (const Vehicle& v : mobMgr.vehicles) {
                glm::vec3 pos = glm::mix(v.prev, v.pos, partial);
                if (glm::length(pos - eye) > farDist) continue;
                glm::ivec3 cell((int)std::floor(pos.x), (int)std::floor(pos.y + 0.5f), (int)std::floor(pos.z));
                float sky = world->getSkyLight(cell.x, cell.y, cell.z) / 15.f, bl = world->getBlockLight(cell.x, cell.y, cell.z) / 15.f;
                float dyaw = v.yaw - v.prevYaw;
                while (dyaw > 180.f) dyaw -= 360.f;
                while (dyaw < -180.f) dyaw += 360.f;
                glm::mat4 mm = glm::translate(glm::mat4(1.f), pos);
                mm = glm::rotate(mm, glm::radians(-(v.prevYaw + dyaw * partial)), glm::vec3(0, 1, 0));
                if (v.hurtTime > 0) mm = glm::rotate(mm, std::sin((v.hurtTime - partial) * 1.2f) * 0.2f, glm::vec3(1, 0, 0));
                const float k = 1.f / 16.f;
                if ((v.kind == VehicleKind::Boat && haveBoat) || (v.kind != VehicleKind::Boat && haveCart)) {
                    // Модель 1.0: центр модели выше низа (у вагонетки на 6/16, у лодки на 7/16), зеркало X/Y как в RenderLiving
                    bool boat = v.kind == VehicleKind::Boat;
                    glm::mat4 em = glm::translate(glm::mat4(1.f), glm::vec3(0.f, boat ? 7 * k : 6 * k, 0.f));
                    em = glm::scale(mm * em, glm::vec3(-k, -k, k));
                    std::vector<Vertex> mv;
                    emitModel(mv, boat ? buildBoatModel() : buildMinecartModel(), em, sky, bl, 64.f, 32.f);
                    glBindTexture(GL_TEXTURE_2D, boat ? boatTex : cartTex);
                    glDisable(GL_CULL_FACE);
                    drawDynamic(mv);
                    if (v.kind != VehicleKind::Minecart && !boat) {
                        std::vector<Vertex> blk;
                        appendBlockModel(blk, v.kind == VehicleKind::ChestCart ? CHEST : (v.fuel > 0 ? FURNACE_LIT : FURNACE), sky, bl, 5);
                        for (Vertex q : blk) {
                            glm::vec3 lp = glm::vec3(-0.375f, 3 * k, -0.375f) + glm::vec3(q.x, q.y, q.z) * 0.75f;
                            glm::vec3 wp = glm::vec3(mm * glm::vec4(lp, 1.f));
                            q.x = wp.x; q.y = wp.y; q.z = wp.z;
                            vv.push_back(q);
                        }
                    }
                } else if (v.kind == VehicleKind::Boat) {
                    int t = T(4, 0);
                    boxW(mm, {-0.75f, 0.0f, -0.5f}, {0.75f, 3 * k, 0.5f}, t, sky, bl);
                    boxW(mm, {-0.75f, 3 * k, -0.5f}, {0.75f, 9 * k, -0.5f + 2 * k}, t, sky, bl);
                    boxW(mm, {-0.75f, 3 * k, 0.5f - 2 * k}, {0.75f, 9 * k, 0.5f}, t, sky, bl);
                    boxW(mm, {-0.75f, 3 * k, -0.5f}, {-0.75f + 2 * k, 9 * k, 0.5f}, t, sky, bl);
                    boxW(mm, {0.75f - 2 * k, 3 * k, -0.5f}, {0.75f, 9 * k, 0.5f}, t, sky, bl);
                } else {
                    int t = T(6, 1);
                    boxW(mm, {-0.625f, 1 * k, -0.4375f}, {0.625f, 3 * k, 0.4375f}, t, sky, bl);
                    boxW(mm, {-0.625f, 3 * k, -0.4375f}, {0.625f, 11 * k, -0.4375f + 2 * k}, t, sky, bl);
                    boxW(mm, {-0.625f, 3 * k, 0.4375f - 2 * k}, {0.625f, 11 * k, 0.4375f}, t, sky, bl);
                    boxW(mm, {-0.625f, 3 * k, -0.4375f}, {-0.625f + 2 * k, 11 * k, 0.4375f}, t, sky, bl);
                    boxW(mm, {0.625f - 2 * k, 3 * k, -0.4375f}, {0.625f, 11 * k, 0.4375f}, t, sky, bl);
                    if (v.kind != VehicleKind::Minecart) {
                        std::vector<Vertex> blk;
                        appendBlockModel(blk, v.kind == VehicleKind::ChestCart ? CHEST : (v.fuel > 0 ? FURNACE_LIT : FURNACE), sky, bl, 5);
                        for (Vertex q : blk) {
                            glm::vec3 lp = glm::vec3(-0.375f, 3 * k, -0.375f) + glm::vec3(q.x, q.y, q.z) * 0.75f;
                            glm::vec3 wp = glm::vec3(mm * glm::vec4(lp, 1.f));
                            q.x = wp.x; q.y = wp.y; q.z = wp.z;
                            vv.push_back(q);
                        }
                    }
                }
            }
            glBindTexture(GL_TEXTURE_2D, terrainUiTex);
            glDisable(GL_CULL_FACE);
            drawDynamic(vv);
            glBindTexture(GL_TEXTURE_2D, terrainTex);
        }

        // ---- Падающий песок и гравий (RenderFallingSand): обычный куб блока
        if (!mobMgr.falling.empty()) {
            std::vector<Vertex> fv, model;
            for (const FallingBlock& f : mobMgr.falling) {
                glm::vec3 pos = glm::mix(f.prev, f.pos, partial);
                glm::ivec3 cell((int)std::floor(pos.x), (int)std::floor(pos.y + 0.5f), (int)std::floor(pos.z));
                float sky = world->getSkyLight(cell.x, cell.y, cell.z) / 15.f, bl = world->getBlockLight(cell.x, cell.y, cell.z) / 15.f;
                model.clear();
                appendBlockModel(model, f.block, sky, bl);
                for (Vertex v : model) {
                    v.x += pos.x - 0.5f; v.y += pos.y; v.z += pos.z - 0.5f;
                    fv.push_back(v);
                }
            }
            glBindTexture(GL_TEXTURE_2D, terrainUiTex);
            drawDynamic(fv);
            glBindTexture(GL_TEXTURE_2D, terrainTex);
        }

        // ---- Зажжённый динамит: куб TNT, раздувается и мигает белым перед взрывом (RenderTNTPrimed)
        if (!mobMgr.tnts.empty()) {
            std::vector<Vertex> tv, model;
            for (const PrimedTnt& t : mobMgr.tnts) {
                glm::vec3 pos = glm::mix(t.prev, t.pos, partial);
                glm::ivec3 cell((int)std::floor(pos.x), (int)std::floor(pos.y + 0.5f), (int)std::floor(pos.z));
                float sky = world->getSkyLight(cell.x, cell.y, cell.z) / 15.f, bl = world->getBlockLight(cell.x, cell.y, cell.z) / 15.f;
                float left = t.fuse - partial + 1.f, sc = 1.f;
                if (left < 10.f) {
                    float f = std::clamp(1.f - left / 10.f, 0.f, 1.f);
                    f *= f; f *= f;
                    sc = 1.f + f * 0.3f;
                }
                float flash = (t.fuse / 5) % 2 == 0 ? (1.f - left / 100.f) * 0.8f : 0.f;
                model.clear();
                appendBlockModel(model, TNT, sky, bl);
                for (Vertex v : model) {
                    v.x = pos.x + (v.x - 0.5f) * sc;
                    v.y = pos.y + v.y * sc - (sc - 1.f) * 0.5f;
                    v.z = pos.z + (v.z - 0.5f) * sc;
                    float k = 1.f + flash * 3.f;
                    v.r *= k; v.g *= k; v.b *= k;
                    tv.push_back(v);
                }
            }
            glBindTexture(GL_TEXTURE_2D, terrainUiTex);
            drawDynamic(tv);
            glBindTexture(GL_TEXTURE_2D, terrainTex);
        }

        // ---- Стрелы (RenderArrow): две скрещённые плоскости 16x4 пикселя и торец 4x4 с оперением,
        // масштаб 0.05625 блока на пиксель, наконечник впереди точки попадания на 4 пикселя
        if (!mobMgr.arrows.empty()) {
            std::vector<Vertex> av;
            const float S = 0.05625f;
            for (const Arrow& a : mobMgr.arrows) {
                glm::vec3 pos = glm::mix(a.prev, a.pos, partial);
                glm::vec3 d = glm::length(a.dir) > 1e-4f ? glm::normalize(a.dir) : glm::vec3(0, 0, 1);
                glm::vec3 s1 = glm::normalize(glm::cross(d, std::abs(d.y) < 0.95f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
                glm::vec3 s2 = glm::cross(d, s1);
                glm::ivec3 cell((int)std::floor(pos.x), (int)std::floor(pos.y), (int)std::floor(pos.z));
                float sky = world->getSkyLight(cell.x, cell.y, cell.z) / 15.f, bl = world->getBlockLight(cell.x, cell.y, cell.z) / 15.f;
                glm::vec3 c = pos - d * (4.f * S);
                glm::vec3 tail = c - d * (8.f * S), head = c + d * (8.f * S);
                for (glm::vec3 s : {s1, s2}) {
                    glm::vec3 h = s * (2.f * S);
                    Vertex a0{tail.x - h.x, tail.y - h.y, tail.z - h.z, 0.f, 5.f / 32, 1, sky, bl};
                    Vertex a1{head.x - h.x, head.y - h.y, head.z - h.z, 16.f / 32, 5.f / 32, 1, sky, bl};
                    Vertex a2{head.x + h.x, head.y + h.y, head.z + h.z, 16.f / 32, 0.f, 1, sky, bl};
                    Vertex a3{tail.x + h.x, tail.y + h.y, tail.z + h.z, 0.f, 0.f, 1, sky, bl};
                    av.insert(av.end(), {a0, a1, a2, a0, a2, a3});
                }
                // Торец с оперением
                glm::vec3 bk = c - d * (7.f * S), p1 = s1 * (2.f * S), p2 = s2 * (2.f * S);
                glm::vec3 b0 = bk - p1 - p2, b1 = bk + p1 - p2, b2 = bk + p1 + p2, b3 = bk - p1 + p2;
                Vertex e0{b0.x, b0.y, b0.z, 0.f, 10.f / 32, 0.8f, sky, bl}, e1{b1.x, b1.y, b1.z, 5.f / 32, 10.f / 32, 0.8f, sky, bl};
                Vertex e2{b2.x, b2.y, b2.z, 5.f / 32, 5.f / 32, 0.8f, sky, bl}, e3{b3.x, b3.y, b3.z, 0.f, 5.f / 32, 0.8f, sky, bl};
                av.insert(av.end(), {e0, e1, e2, e0, e2, e3});
            }
            glBindTexture(GL_TEXTURE_2D, arrowTex);
            drawDynamic(av);
            glBindTexture(GL_TEXTURE_2D, terrainTex);
        }

        // Книги над столами зачарования (RenderEnchantmentTable)
        for (int64_t key : world->enchantTables) {
            glm::ivec3 tp = posFromKey(key);
            if (world->getBlock(tp.x, tp.y, tp.z) != ENCHANT_TABLE || glm::length(glm::vec3(tp) - eye) > 48.f) continue;
            auto bit = books.find(key);
            if (bit == books.end()) continue;
            const BookState& bs = bit->second;
            float t = bs.ticks + partial;
            glm::mat4 bm = glm::translate(glm::mat4(1.f), glm::vec3(tp) + glm::vec3(0.5f, 0.75f + 0.1f + std::sin(t * 0.1f) * 0.01f, 0.5f));
            float rot = bs.prevRot + (bs.rot - bs.prevRot) * partial;
            bm = glm::rotate(bm, -rot, glm::vec3(0, 1, 0));
            bm = glm::rotate(bm, glm::radians(80.f), glm::vec3(0, 0, 1));
            bm = glm::scale(bm, glm::vec3(1.f / 16.f));
            float fl = bs.prevFlip + (bs.flip - bs.prevFlip) * partial;
            float fa = fl + 0.25f, fb = fl + 0.75f;
            fa = std::clamp((fa - std::floor(fa)) * 1.6f - 0.3f, 0.f, 1.f);
            fb = std::clamp((fb - std::floor(fb)) * 1.6f - 0.3f, 0.f, 1.f);
            float spread = bs.prevSpread + (bs.spread - bs.prevSpread) * partial;
            float sky = world->getSkyLight(tp.x, tp.y + 1, tp.z) / 15.f, bl = world->getBlockLight(tp.x, tp.y + 1, tp.z) / 15.f;
            std::vector<Vertex> bv;
            emitModel(bv, buildBookModel(t, fa, fb, spread), bm, sky, bl, 64.f, 32.f);
            glBindTexture(GL_TEXTURE_2D, bookTex);
            // У обложек и страниц нулевая толщина: без отсечения задних граней две грани в одной плоскости мерцают
            glEnable(GL_CULL_FACE);
            drawDynamic(bv);
            glDisable(GL_CULL_FACE);
            glBindTexture(GL_TEXTURE_2D, terrainTex);
        }

        // Сундуки (TileEntityChestRenderer 1.0): у двойного рисует половина с меньшей координатой.
        // Там же эндер-сундуки (item/enderchest.png) и головы мобов (TileEntitySkullRenderer 1.4.2)
        if (chestTex && largeChestTex) {
            std::vector<Vertex> sv, lv, ev;
            std::vector<Vertex> skullV[5];
            for (auto& [ck, ch] : world->chunks) {
                if (ch->chests.empty()) continue;
                float cdx = ch->cx * CW + 8.f - eye.x, cdz = ch->cz * CW + 8.f - eye.z;
                if (cdx * cdx + cdz * cdz > 80.f * 80.f) continue;
                for (const glm::ivec3& p : ch->chests) {
                    uint8_t cb = world->getBlock(p.x, p.y, p.z);
                    if (cb == SKULL_BLOCK) {
                        // Голова 8x8x8 с развёрткой (0,0) текстуры моба; биты 4-7 меты — поворот к игроку (по 22.5°)
                        uint8_t sm = world->getMeta(p.x, p.y, p.z);
                        int kind = std::min(4, sm & 7);
                        float yawDeg = ((sm >> 4) & 15) * 22.5f;
                        glm::vec3 feet(p.x + 0.5f, p.y - 1.5f, p.z + 0.5f);
                        if (sm & 8) {
                            // На стене: голова прижата к стене, низ на 0.25, смотрит от стены
                            static const float SX[4] = {1, 0, -1, 0}, SZ[4] = {0, 1, 0, -1};
                            int side = (sm >> 4) & 3;
                            yawDeg = side * 90.f;
                            feet += glm::vec3(-SX[side] * 0.25f, 0.25f, -SZ[side] * 0.25f);
                        }
                        ModelPart head;
                        head.boxes.push_back({-4.f, -8.f, -4.f, 8, 8, 8, 0, 0});
                        glm::mat4 hm = entityMatrix(feet, yawDeg);
                        static const MobType SKULL_MOB[5] = {MobType::Skeleton, MobType::WitherSkeleton, MobType::Zombie, MobType::Zombie,
                                                             MobType::Creeper};
                        float texH = kind == 3 ? 32.f : 64.f * mobTexAspect[(int)SKULL_MOB[kind]];
                        emitModel(skullV[kind], {head}, hm, world->getSkyLight(p.x, p.y, p.z) / 15.f, world->getBlockLight(p.x, p.y, p.z) / 15.f,
                                  64.f, texH);
                        continue;
                    }
                    bool ender = cb == ENDER_CHEST;
                    if (cb != CHEST && !ender) continue;
                    if (!ender && (world->getBlock(p.x - 1, p.y, p.z) == CHEST || world->getBlock(p.x, p.y, p.z - 1) == CHEST)) continue;
                    bool xp = !ender && world->getBlock(p.x + 1, p.y, p.z) == CHEST, zp = !ender && world->getBlock(p.x, p.y, p.z + 1) == CHEST;
                    int meta = world->getMeta(p.x, p.y, p.z);
                    float ang = meta == 2 ? 180.f : meta == 4 ? 90.f : meta == 5 ? -90.f : 0.f;
                    glm::mat4 m = glm::translate(glm::mat4(1.f), glm::vec3(p) + glm::vec3(0, 1, 1));
                    m = glm::scale(m, glm::vec3(1, -1, -1));
                    m = glm::translate(m, glm::vec3(0.5f));
                    if (meta == 2 && xp) m = glm::translate(m, glm::vec3(1, 0, 0));
                    if (meta == 5 && zp) m = glm::translate(m, glm::vec3(0, 0, -1));
                    m = glm::rotate(m, glm::radians(ang), glm::vec3(0, 1, 0));
                    m = glm::translate(m, glm::vec3(-0.5f));
                    m = glm::scale(m, glm::vec3(1.f / 16.f));
                    float lid = 0.f;
                    auto li = chestLids.find(posKey(p.x, p.y, p.z));
                    if (li != chestLids.end()) lid = li->second.first + (li->second.second - li->second.first) * partial;
                    lid = 1.f - lid;
                    lid = 1.f - lid * lid * lid;
                    float sky = world->getSkyLight(p.x, p.y, p.z) / 15.f, bl = world->getBlockLight(p.x, p.y, p.z) / 15.f;
                    bool large = xp || zp;
                    emitModel(ender ? ev : large ? lv : sv, buildChestModel(large, lid), m, sky, bl, large ? 128.f : 64.f, 64.f);
                }
            }
            if (!sv.empty()) { glBindTexture(GL_TEXTURE_2D, chestTex); drawDynamic(sv); }
            if (!lv.empty()) { glBindTexture(GL_TEXTURE_2D, largeChestTex); drawDynamic(lv); }
            if (!ev.empty() && enderChestTex) { glBindTexture(GL_TEXTURE_2D, enderChestTex); drawDynamic(ev); }
            const GLuint skullTex[5] = {mobTex[(int)MobType::Skeleton], mobTex[(int)MobType::WitherSkeleton], mobTex[(int)MobType::Zombie],
                                        charTex, mobTex[(int)MobType::Creeper]};
            for (int k = 0; k < 5; ++k)
                if (!skullV[k].empty()) { glBindTexture(GL_TEXTURE_2D, skullTex[k]); drawDynamic(skullV[k]); }
            glBindTexture(GL_TEXTURE_2D, terrainTex);
        }

        // Картины (RenderPainting 1.0): лицевая сторона — сюжет, изнанка и торцы — дерево; свет по клеткам 16x16
        if (!mobMgr.paintings.empty() && paintingTex) {
            std::vector<Vertex> pv;
            const float TX = 1.f / 256.f;
            for (const Painting& pt : mobMgr.paintings) {
                if (pt.frame) continue;
                const PaintingArt& a = PAINTING_ARTS[pt.art];
                glm::vec3 n = pt.normal(), r = pt.right(), up(0, 1, 0), c = pt.center();
                if (glm::length(c - eye) > 64.f) continue;
                int cw = a.w / 16, ch = a.h / 16;
                glm::vec3 corner = c - r * (cw * 0.5f) - up * (ch * 0.5f);
                for (int i = 0; i < cw; ++i)
                    for (int j = 0; j < ch; ++j) {
                        glm::vec3 p0 = corner + r * (float)i + up * (float)j;
                        glm::vec3 q = p0 + r * 0.5f + up * 0.5f + n * 0.5f;
                        glm::ivec3 cell((int)std::floor(q.x), (int)std::floor(q.y), (int)std::floor(q.z));
                        float sky = world->getSkyLight(cell.x, cell.y, cell.z) / 15.f, bl = world->getBlockLight(cell.x, cell.y, cell.z) / 15.f;
                        auto quad = [&](glm::vec3 o0, glm::vec3 du, glm::vec3 dv, float u0, float v0, float u1, float v1, float shade) {
                            // o0 — нижний левый угол, du — вправо, dv — вверх
                            glm::vec3 A = o0, B = o0 + du, C = o0 + du + dv, D = o0 + dv;
                            Vertex va{A.x, A.y, A.z, u0, v1, shade, sky, bl}, vb{B.x, B.y, B.z, u1, v1, shade, sky, bl};
                            Vertex vc{C.x, C.y, C.z, u1, v0, shade, sky, bl}, vd{D.x, D.y, D.z, u0, v0, shade, sky, bl};
                            pv.insert(pv.end(), {va, vb, vc, va, vc, vd});
                        };
                        float fu = (a.u + i * 16) * TX, fv = (a.v + (ch - 1 - j) * 16) * TX;
                        const float h = 1.f / 32.f;
                        quad(p0 + n * h, r, up, fu, fv, fu + 16 * TX, fv + 16 * TX, 1.f);                         // лицо
                        quad(p0 + r - n * h, -r, up, 192 * TX, 0, 208 * TX, 16 * TX, 0.8f);                          // изнанка
                        if (j == ch - 1) quad(p0 + up - n * h, r, n * (2 * h), 192 * TX, 0, 208 * TX, 1 * TX, 1.f);  // верх
                        if (j == 0) quad(p0 - n * h, r, n * (2 * h), 192 * TX, 0, 208 * TX, 1 * TX, 0.5f);           // низ
                        if (i == 0) quad(p0 - n * h, n * (2 * h), up, 192 * TX, 0, 193 * TX, 16 * TX, 0.6f);         // левый торец
                        if (i == cw - 1) quad(p0 + r - n * h, n * (2 * h), up, 192 * TX, 0, 193 * TX, 16 * TX, 0.6f); // правый торец
                    }
            }
            glBindTexture(GL_TEXTURE_2D, paintingTex);
            drawDynamic(pv);
            glBindTexture(GL_TEXTURE_2D, terrainTex);
        }

        // Рамки для предметов (RenderItemFrame 1.4.2): задник — тайл рамки (9,11), бортик из досок, предмет — половинного
        // размера, блоки маленьким кубиком, остальное плоской иконкой; поворот по 90°
        {
            std::vector<Vertex> tv, iv;
            const float TS16 = 1.f / 16.f, px = 1.f / 16.f;
            for (const Painting& fr : mobMgr.paintings) {
                if (!fr.frame || fr.dead) continue;
                glm::vec3 n = fr.normal(), r = fr.right(), up(0, 1, 0);
                glm::vec3 wf = glm::vec3(fr.wall) + 0.5f + n * 0.5f; // центр грани стены
                if (glm::length(wf - eye) > 64.f) continue;
                glm::ivec3 cell = fr.wall + glm::ivec3(glm::round(n));
                float sky = world->getSkyLight(cell.x, cell.y, cell.z) / 15.f, bl = world->getBlockLight(cell.x, cell.y, cell.z) / 15.f;
                auto quad = [&](std::vector<Vertex>& out, glm::vec3 o0, glm::vec3 du, glm::vec3 dv, float u0, float v0, float u1, float v1, float shade) {
                    glm::vec3 A = o0, B = o0 + du, C = o0 + du + dv, D = o0 + dv;
                    Vertex va{A.x, A.y, A.z, u0, v1, shade, sky, bl}, vb{B.x, B.y, B.z, u1, v1, shade, sky, bl};
                    Vertex vc{C.x, C.y, C.z, u1, v0, shade, sky, bl}, vd{D.x, D.y, D.z, u0, v0, shade, sky, bl};
                    out.insert(out.end(), {va, vb, vc, va, vc, vd});
                };
                // Коробка в осях рамки (r, up, n) из пикселей: грани с тайлом t
                auto box = [&](float x0, float y0, float z0, float x1, float y1, float z1, int t) {
                    float tu = (t % 16) * TS16, tv0 = (t / 16) * TS16;
                    auto P = [&](float x, float y, float z) { return wf + r * (x * px) + up * (y * px) + n * (z * px); };
                    auto U = [&](float a) { return tu + (a + 8.f) / 16.f * TS16; };
                    auto V = [&](float b) { return tv0 + (8.f - b) / 16.f * TS16; };
                    quad(tv, P(x0, y0, z1), r * ((x1 - x0) * px), up * ((y1 - y0) * px), U(x0), V(y1), U(x1), V(y0), 1.f);      // лицо
                    quad(tv, P(x1, y0, z0), -r * ((x1 - x0) * px), up * ((y1 - y0) * px), U(x0), V(y1), U(x1), V(y0), 0.8f);   // изнанка
                    quad(tv, P(x0, y1, z1), r * ((x1 - x0) * px), -n * ((z1 - z0) * px), U(x0), V(y1), U(x1), V(y1 - 1), 1.f);  // верх
                    quad(tv, P(x0, y0, z0), r * ((x1 - x0) * px), n * ((z1 - z0) * px), U(x0), V(y0 + 1), U(x1), V(y0), 0.5f);  // низ
                    quad(tv, P(x0, y0, z0), n * ((z1 - z0) * px), up * ((y1 - y0) * px), U(x0), V(y1), U(x0 + 1), V(y0), 0.6f); // левый
                    quad(tv, P(x1, y0, z1), -n * ((z1 - z0) * px), up * ((y1 - y0) * px), U(x1 - 1), V(y1), U(x1), V(y0), 0.6f);
                };
                box(-5, -5, 0, 5, 5, 0.5f, T(9, 11));  // задник
                box(-6, -6, 0, 6, -5, 1, T(4, 0));     // бортик из досок
                box(-6, 5, 0, 6, 6, 1, T(4, 0));
                box(-6, -5, 0, -5, 5, 1, T(4, 0));
                box(5, -5, 0, 6, 5, 1, T(4, 0));
                if (fr.item.empty()) continue;
                // Поворот предмета вокруг нормали
                float ang = fr.rotation * glm::half_pi<float>();
                glm::vec3 ru = r * std::cos(ang) + up * std::sin(ang), uu = up * std::cos(ang) - r * std::sin(ang);
                if (itemIsCube(fr.item)) {
                    std::vector<Vertex> model;
                    appendBlockModel(model, (uint8_t)fr.item.id, 1.f, 0.f,
                                     blockHasVariants((uint8_t)fr.item.id) ? (uint8_t)fr.item.damage : (uint8_t)3);
                    glm::mat4 m(1.f);
                    m[0] = glm::vec4(ru, 0.f); m[1] = glm::vec4(uu, 0.f); m[2] = glm::vec4(n, 0.f);
                    m[3] = glm::vec4(wf + n * (1.f / 16.f), 1.f);
                    m = glm::scale(m, glm::vec3(0.375f));
                    m = glm::translate(m, glm::vec3(-0.5f, -0.5f, 0.f));
                    appendTransformed(tv, model, m, sky, bl);
                } else {
                    int col, row;
                    bool fromItems;
                    itemTile(fr.item, col, row, fromItems);
                    float u0 = col / 16.f, v0 = row / 16.f, du = 1.f / 16.f;
                    glm::vec3 o0 = wf + n * (1.f / 16.f + 0.004f) - ru * 0.25f - uu * 0.25f;
                    quad(fromItems ? iv : tv, o0, ru * 0.5f, uu * 0.5f, u0, v0, u0 + du, v0 + du, 1.f);
                }
            }
            if (!tv.empty()) { glBindTexture(GL_TEXTURE_2D, terrainUiTex); drawDynamic(tv); }
            if (!iv.empty()) { glBindTexture(GL_TEXTURE_2D, itemsTex); drawDynamic(iv); }
            glBindTexture(GL_TEXTURE_2D, terrainTex);
        }

        // Портал Края: пластина на высоте 3/4, 16 слоёв (0 — туннель с альфой, 1..15 — поля частиц, сложение цветов)
        if (tunnelTex && fieldTex) {
            std::vector<float> ev;
            for (auto& [ck, ch] : world->chunks) {
                if (ch->endPortals.empty()) continue;
                for (const glm::ivec3& p : ch->endPortals) {
                    if (world->getBlock(p.x, p.y, p.z) != END_PORTAL || glm::length(glm::vec3(p) - eye) > 64.f) continue;
                    float y = p.y + 0.75f, x0 = (float)p.x, z0 = (float)p.z;
                    float q[6][3] = {{x0, y, z0}, {x0, y, z0 + 1}, {x0 + 1, y, z0 + 1}, {x0, y, z0}, {x0 + 1, y, z0 + 1}, {x0 + 1, y, z0}};
                    for (auto& v : q) ev.insert(ev.end(), {v[0], v[1], v[2], 0.f, 0.f});
                }
            }
            if (!ev.empty()) {
                glUseProgram(endPortalProg);
                glUniformMatrix4fv(glGetUniformLocation(endPortalProg, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
                glUniform3f(glGetUniformLocation(endPortalProg, "uCam"), eye.x, eye.y, eye.z);
                GLboolean wasCull = glIsEnabled(GL_CULL_FACE), wasBlend = glIsEnabled(GL_BLEND);
                glDisable(GL_CULL_FACE);
                glEnable(GL_BLEND);
                glDepthMask(GL_FALSE);
                float scroll = (float)(std::fmod(glfwGetTime() * 1000.0, 700000.0) / 700000.0);
                uint32_t pr = 31100u;
                auto prf = [&]() { pr ^= pr << 13; pr ^= pr >> 17; pr ^= pr << 5; return (pr & 0xFFFFFF) / float(0x1000000); };
                for (int i = 0; i < 16; ++i) {
                    float depth = (float)(16 - i), scale = 0.0625f, bright = 1.f / (depth + 1.f);
                    glm::vec3 col(prf() * 0.5f + 0.1f, prf() * 0.5f + 0.4f, prf() * 0.5f + 0.5f);
                    if (i == 0) {
                        glBindTexture(GL_TEXTURE_2D, tunnelTex);
                        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                        depth = 65.f; scale = 0.125f; bright = 0.1f; col = glm::vec3(1.f);
                    } else {
                        glBindTexture(GL_TEXTURE_2D, fieldTex);
                        glBlendFunc(GL_ONE, GL_ONE);
                        if (i == 1) scale = 0.5f;
                    }
                    glUniform1f(glGetUniformLocation(endPortalProg, "uDepth"), depth * 0.0625f);
                    glUniform1f(glGetUniformLocation(endPortalProg, "uScale"), scale);
                    glUniform1f(glGetUniformLocation(endPortalProg, "uAngle"), glm::radians((float)((i * i * 4321 + i * 9) * 2)));
                    glUniform1f(glGetUniformLocation(endPortalProg, "uScroll"), scroll);
                    glUniform4f(glGetUniformLocation(endPortalProg, "uColor"), col.r * bright, col.g * bright, col.b * bright, 1.f);
                    drawSprite(ev);
                }
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDepthMask(GL_TRUE);
                if (wasCull) glEnable(GL_CULL_FACE);
                if (!wasBlend) glDisable(GL_BLEND);
                glUseProgram(chunkProg);
                glBindTexture(GL_TEXTURE_2D, terrainTex);
            }
        }

        // Шары опыта (RenderXPOrb 1.0): значок по величине, лицом к камере, цвет переливается от зелёного к жёлтому
        if (!mobMgr.orbs.empty() && xpOrbTex) {
            std::vector<Vertex> ov;
            for (const XpOrb& o : mobMgr.orbs) {
                glm::vec3 pos = glm::mix(o.prev, o.pos, partial) + glm::vec3(0, 0.1f, 0);
                int ic = xpOrbIcon(o.value);
                float u0 = (ic % 4) * 0.25f, v0 = (ic / 4) * 0.25f, du = 0.25f;
                float t = (o.color + partial) / 2.f;
                float cr = (std::sin(t) + 1.f) * 0.5f, cb = (std::sin(t + 4.1887903f) + 1.f) * 0.1f;
                glm::ivec3 cell((int)std::floor(pos.x), (int)std::floor(pos.y), (int)std::floor(pos.z));
                float sky = world->getSkyLight(cell.x, cell.y, cell.z) / 15.f;
                float bl = std::min(1.f, world->getBlockLight(cell.x, cell.y, cell.z) / 15.f + 0.5f); // шар светится сам
                const float s = 0.3f;
                glm::vec3 rr = camRight * (0.5f * s), dn = camUp * (-0.25f * s), up = camUp * (0.75f * s);
                glm::vec3 a = pos - rr + dn, b = pos + rr + dn, c = pos + rr + up, d = pos - rr + up;
                Vertex va{a.x, a.y, a.z, u0, v0 + du, 1, sky, bl, cr, 1.f, cb}, vb{b.x, b.y, b.z, u0 + du, v0 + du, 1, sky, bl, cr, 1.f, cb};
                Vertex vc{c.x, c.y, c.z, u0 + du, v0, 1, sky, bl, cr, 1.f, cb}, vd{d.x, d.y, d.z, u0, v0, 1, sky, bl, cr, 1.f, cb};
                ov.insert(ov.end(), {va, vb, vc, va, vc, vd});
            }
            glBindTexture(GL_TEXTURE_2D, xpOrbTex);
            drawDynamic(ov);
            glBindTexture(GL_TEXTURE_2D, terrainTex);
        }

        // Огненные шары (RenderFireball): иконка шара из items.png (слот 46) лицом к камере, всегда яркая;
        // у гаста размер 2, у ифрита 0.5
        if (!mobMgr.fireballs.empty()) {
            std::vector<Vertex> fv;
            for (const Fireball& f : mobMgr.fireballs) {
                glm::vec3 pos = glm::mix(f.prev, f.pos, partial);
                float s = f.wither ? 0.7f : f.small ? 0.5f : 2.f;
                float u0 = (f.wither ? (f.blue ? 2.f : 1.f) : 14.f) / 16.f;
                float v0 = (f.wither ? 9.f : 2.f) / 16.f, du = 1.f / 16.f;
                glm::vec3 r = camRight * (0.5f * s), dn = camUp * (-0.25f * s), up = camUp * (0.75f * s);
                glm::vec3 a = pos - r + dn, b = pos + r + dn, c = pos + r + up, d = pos - r + up;
                float cr = 1.f, cg = 1.f, cb = 1.f;
                if (f.wither && f.blue) { cr = 0.4f; cg = 0.7f; cb = 1.f; }
                Vertex va{a.x, a.y, a.z, u0, v0 + du, 1, 1, 1, cr, cg, cb}, vb{b.x, b.y, b.z, u0 + du, v0 + du, 1, 1, 1, cr, cg, cb};
                Vertex vc{c.x, c.y, c.z, u0 + du, v0, 1, 1, 1, cr, cg, cb}, vd{d.x, d.y, d.z, u0, v0, 1, 1, 1, cr, cg, cb};
                fv.insert(fv.end(), {va, vb, vc, va, vc, vd});
            }
            glBindTexture(GL_TEXTURE_2D, itemsTex);
            drawDynamic(fv);
            glBindTexture(GL_TEXTURE_2D, terrainTex);
        }

        // Снежки и яйца — иконка предмета, повёрнутая к камере (RenderSnowball)
        if (!mobMgr.throwables.empty()) {
            std::vector<Vertex> tv;
            for (const Throwable& t : mobMgr.throwables) {
                int col = 0, row = 0;
                if (!itemIcon(t.item, col, row)) continue;
                glm::vec3 pos = glm::mix(t.prev, t.pos, partial);
                glm::ivec3 cell((int)std::floor(pos.x), (int)std::floor(pos.y), (int)std::floor(pos.z));
                float sky = world->getSkyLight(cell.x, cell.y, cell.z) / 15.f, bl = world->getBlockLight(cell.x, cell.y, cell.z) / 15.f;
                float u0 = col / 16.f, v0 = row / 16.f, du = 1.f / 16.f, hs = 0.25f;
                glm::vec3 a = pos - camRight * hs - camUp * hs, b = pos + camRight * hs - camUp * hs;
                glm::vec3 c = pos + camRight * hs + camUp * hs, d = pos - camRight * hs + camUp * hs;
                Vertex va{a.x, a.y, a.z, u0, v0 + du, 1, sky, bl}, vb{b.x, b.y, b.z, u0 + du, v0 + du, 1, sky, bl};
                Vertex vc{c.x, c.y, c.z, u0 + du, v0, 1, sky, bl}, vd{d.x, d.y, d.z, u0, v0, 1, sky, bl};
                tv.insert(tv.end(), {va, vb, vc, va, vc, vd});
            }
            glBindTexture(GL_TEXTURE_2D, itemsTex);
            drawDynamic(tv);
            glBindTexture(GL_TEXTURE_2D, terrainTex);
        }

        // Частицы — плоские квадраты, повёрнутые к камере (цвет и яркость как у EntityFX)
        if (!particles.empty()) {
            std::vector<Vertex> dig, sprites, crack, huge;
            for (auto& p : particles) {
                glm::vec3 pos = glm::mix(p.prev, p.pos, partial);
                glm::ivec3 cell((int)std::floor(pos.x), (int)std::floor(pos.y), (int)std::floor(pos.z));
                float sky = world->getSkyLight(cell.x, cell.y, cell.z) / 15.f;
                float bl = world->getBlockLight(cell.x, cell.y, cell.z) / 15.f;
                if (p.type == PType::Flame || p.type == PType::Lava || p.type == PType::Crit || p.type == PType::LargeExplode || p.type == PType::Portal || p.type == PType::Spell) { sky = 1.f; bl = 1.f; }
                int tex;
                float size;
                particleSprite(p, partial, tex, size);
                float u0, v0, du;
                if (p.type == PType::Digging || p.type == PType::ItemCrack) { u0 = p.u; v0 = p.v; du = 4.f / 256.f; }
                else if (p.type == PType::LargeExplode) { u0 = (tex % 4) * 0.25f; v0 = (tex / 4) * 0.25f; du = 0.25f; }
                else { u0 = (float)(tex % 16) / 16.f; v0 = (float)(tex / 16) / 16.f; du = 1.f / 16.f; }
                glm::vec3 a = pos - camRight * size - camUp * size, b = pos + camRight * size - camUp * size;
                glm::vec3 c = pos + camRight * size + camUp * size, d = pos - camRight * size + camUp * size;
                auto V = [&](glm::vec3 q, float u, float v) {
                    Vertex r{q.x, q.y, q.z, u, v, 1.f, sky, bl};
                    r.r = p.color.r; r.g = p.color.g; r.b = p.color.b;
                    return r;
                };
                Vertex va = V(a, u0, v0 + du), vb = V(b, u0 + du, v0 + du), vc = V(c, u0 + du, v0), vd = V(d, u0, v0);
                auto& out = p.type == PType::Digging ? dig : p.type == PType::ItemCrack ? crack : p.type == PType::LargeExplode ? huge : sprites;
                out.insert(out.end(), {va, vb, vc, va, vc, vd});
            }
            glBindTexture(GL_TEXTURE_2D, terrainUiTex);
            drawDynamic(dig);
            glBindTexture(GL_TEXTURE_2D, itemsTex);
            drawDynamic(crack);
            glBindTexture(GL_TEXTURE_2D, particlesTex);
            drawDynamic(sprites);
            if (!huge.empty()) {
                glBindTexture(GL_TEXTURE_2D, explosionTex);
                drawDynamic(huge);
            }
            glBindTexture(GL_TEXTURE_2D, terrainTex);
        }
        glEnable(GL_CULL_FACE);

        // Рамка выделенного блока
        if (hasHit) {
            glUseProgram(lineProg);
            glUniformMatrix4fv(glGetUniformLocation(lineProg, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
            glUniform3f(glGetUniformLocation(lineProg, "uOffset"), (float)hit.x, (float)hit.y, (float)hit.z);
            // Рамка по форме блока: у ступенек, панелей, калиток — несколько коробок
            std::vector<AABB> sel;
            selectionBoxes(*world, hit.x, hit.y, hit.z, sel);
            glUniform4f(glGetUniformLocation(lineProg, "uColor"), 0, 0, 0, 0.4f);
            glLineWidth(2.f);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glBindVertexArray(lineVao);
            for (const AABB& bx : sel) {
                glm::vec3 bmn = bx.mn - 0.002f, bmx = bx.mx + 0.002f;
                glUniform3fv(glGetUniformLocation(lineProg, "uBoxMin"), 1, glm::value_ptr(bmn));
                glm::vec3 bsz = bmx - bmn;
                glUniform3fv(glGetUniformLocation(lineProg, "uBoxSize"), 1, glm::value_ptr(bsz));
                glDrawArrays(GL_LINES, 0, 24);
            }
        }

        // Трещины на ломаемом блоке (тайлы 0..9 последней строки terrain.png)
        if (mining.active && mining.progress > 0.f && hasHit) {
            int stage = std::clamp((int)(mining.progress * 10.f), 0, 9);
            float u0 = stage / 16.f, u1 = (stage + 1) / 16.f, v0 = 15.f / 16.f, v1 = 1.f;
            static const int F[6][4][3] = {
                {{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}}, {{0, 0, 1}, {0, 1, 1}, {0, 1, 0}, {0, 0, 0}},
                {{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}}, {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}},
                {{1, 0, 1}, {1, 1, 1}, {0, 1, 1}, {0, 0, 1}}, {{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}}};
            const float uvs[4][2] = {{u0, v1}, {u0, v0}, {u1, v0}, {u1, v1}};
            std::vector<float> v;
            glm::vec3 cmn, cmx;
            blockBounds(world->getBlock(mining.pos.x, mining.pos.y, mining.pos.z), world->getMeta(mining.pos.x, mining.pos.y, mining.pos.z),
                        mining.pos.x, mining.pos.z, cmn, cmx);
            glm::vec3 base = glm::vec3(mining.pos) + cmn - 0.002f, size = cmx - cmn + 0.004f;
            for (auto& f : F)
                for (int i : {0, 1, 2, 0, 2, 3}) {
                    glm::vec3 p = base + glm::vec3(f[i][0], f[i][1], f[i][2]) * size;
                    v.insert(v.end(), {p.x, p.y, p.z, uvs[i][0], uvs[i][1]});
                }
            glUseProgram(spriteProg);
            glUniformMatrix4fv(glGetUniformLocation(spriteProg, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
            glUniform4f(glGetUniformLocation(spriteProg, "uColor"), 1, 1, 1, 1);
            glBindTexture(GL_TEXTURE_2D, terrainTex);
            glEnable(GL_BLEND);
            glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
            glDepthMask(GL_FALSE);
            drawSprite(v);
            glDepthMask(GL_TRUE);
        }

        // Облака: плоский слой на высоте 108 (как в 1.0), clouds.png — 1 пиксель = 12 блоков
        glUseProgram(chunkProg);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        if (opt.clouds && dimension == 0) {
            const float cy = 108.33f, R = farDist * 2.f, scale = 1.f / (12.f * 256.f);
            float drift = (float)((worldTime + partial) * 0.03);
            float x0 = camPos.x - R, x1 = camPos.x + R, z0 = camPos.z - R, z1 = camPos.z + R;
            auto V = [&](float x, float z) { return Vertex{x, cy, z, (x + drift) * scale, z * scale, 1.f, 1.f, 0.f}; };
            std::vector<Vertex> cv = {V(x0, z0), V(x1, z0), V(x1, z1), V(x0, z0), V(x1, z1), V(x0, z1)};
            glUniform1f(glGetUniformLocation(chunkProg, "uAlpha"), 0.8f);
            glUniform3f(tintLoc, 1.f - rainNow * 0.6f, 1.f - rainNow * 0.6f, 1.f - rainNow * 0.55f);
            glUniform1f(glGetUniformLocation(chunkProg, "uFogStart"), eyeInWater ? 0.f : farDist);
            glUniform1f(glGetUniformLocation(chunkProg, "uFogEnd"), eyeInWater ? 16.f : R);
            glBindTexture(GL_TEXTURE_2D, cloudsTex);
            drawDynamic(cv);
            glUniform1f(glGetUniformLocation(chunkProg, "uFogStart"), fogStart);
            glUniform1f(glGetUniformLocation(chunkProg, "uFogEnd"), fogEnd);
            glUniform1f(glGetUniformLocation(chunkProg, "uAlpha"), 1.f);
            glUniform3f(tintLoc, 1, 1, 1);
            glBindTexture(GL_TEXTURE_2D, terrainTex);
        }

        // Вода и лёд: прозрачные, дальние чанки первыми
        std::reverse(visible.begin(), visible.end());
        glDepthMask(GL_TRUE);
        drawChunks(chunkProg, MESH_TRANSLUCENT);
        glDepthMask(GL_FALSE);

        // ---- Дождь и снег: вертикальные полосы вокруг игрока (EntityRenderer.renderRainSnow)
        if (rainNow > 0.f) {
            glUniform3f(offLoc, 0, 0, 0); // после воды в шейдере осталось смещение последнего чанка
            const int R = opt.fancy ? 10 : 5;
            std::vector<Vertex> rainV, snowV;
            int ex = (int)std::floor(eye.x), ez = (int)std::floor(eye.z);
            float t = (float)((worldTime % 100000) + partial);
            for (int dz = -R; dz <= R; ++dz)
                for (int dx = -R; dx <= R; ++dx) {
                    if (dx * dx + dz * dz > R * R) continue;
                    int x = ex + dx, z = ez + dz;
                    const BiomeInfo& rb = biomeInfo(world->loadedBiome(x, z));
                    if (rb.dry) continue;
                    float top = (float)world->topBlockY(x, z);
                    float y0 = std::max(top, eye.y - R), y1 = std::max(top, eye.y + R);
                    if (y1 - y0 < 0.01f) continue;
                    uint32_t hsh = hash32((uint32_t)(x * 3121 + x * x * 45238971) ^ (uint32_t)(z * z * 418711 + z * 13761));
                    glm::vec2 d(x + 0.5f - eye.x, z + 0.5f - eye.z);
                    if (glm::length(d) < 0.01f) d = glm::vec2(1, 0);
                    d = glm::normalize(d);
                    glm::vec2 perp(-d.y * 0.5f, d.x * 0.5f);
                    int ly = (int)std::floor(std::max(eye.y, top));
                    float sky = world->getSkyLight(x, ly, z) / 15.f, bl = world->getBlockLight(x, ly, z) / 15.f;
                    // Как renderRainSnow в 1.0: у каждой колонки своя фаза и скорость (3..4)/32 за тик
                    float speed = rb.snowy ? 1.f / 512.f : (3.f + (hsh % 100) / 100.f) / 32.f;
                    float off = ((hsh >> 8) % 32) / 32.f;
                    float vOff = -(t * speed + off); // сдвиг вниз по экрану — капли и снежинки падают
                    float uOff = rb.snowy ? std::sin(t * 0.01f + (hsh % 7)) * 0.2f : 0.f;
                    // Текстура привязана к мировой высоте, а не к камере: v зависит только от y
                    float v1 = -y0 * 0.25f + vOff, v0 = -y1 * 0.25f + vOff;
                    float cx = x + 0.5f, cz = z + 0.5f;
                    Vertex a{cx - perp.x, y0, cz - perp.y, 0 + uOff, v1, 1, sky, bl}, b{cx + perp.x, y0, cz + perp.y, 1 + uOff, v1, 1, sky, bl};
                    Vertex c{cx + perp.x, y1, cz + perp.y, 1 + uOff, v0, 1, sky, bl}, e{cx - perp.x, y1, cz - perp.y, 0 + uOff, v0, 1, sky, bl};
                    auto& out = rb.snowy ? snowV : rainV;
                    out.insert(out.end(), {a, b, c, a, c, e});
                }
            glUniform1f(glGetUniformLocation(chunkProg, "uAlpha"), rainNow);
            glBindTexture(GL_TEXTURE_2D, rainTex);
            drawDynamic(rainV);
            glBindTexture(GL_TEXTURE_2D, snowTex);
            drawDynamic(snowV);
            glUniform1f(glGetUniformLocation(chunkProg, "uAlpha"), 1.f);
            glBindTexture(GL_TEXTURE_2D, terrainTex);
        }

        // ---- Молния: ломаная из светящихся лент от облаков к земле
        if (lightningTicks > 0) {
            std::vector<glm::vec3> pts;
            uint32_t ls = lightningSeed;
            glm::vec3 cur(lightningPos.x, 128.f, lightningPos.z);
            auto lr = [&]() { ls ^= ls << 13; ls ^= ls >> 17; ls ^= ls << 5; return (ls & 0xFFFF) / 65535.f - 0.5f; };
            glm::vec3 off(0.f);
            for (float y = 128.f; y > lightningPos.y; y -= 8.f) {
                pts.push_back(glm::vec3(lightningPos.x, y, lightningPos.z) + off);
                off += glm::vec3(lr() * 6.f, 0, lr() * 6.f);
                off *= 0.8f;
            }
            pts.push_back(lightningPos);
            std::vector<float> bv;
            for (size_t i = 0; i + 1 < pts.size(); ++i) {
                glm::vec3 a = pts[i], b = pts[i + 1];
                glm::vec3 side = glm::normalize(glm::cross(b - a, eye - a)) * 0.35f;
                glm::vec3 q[4] = {a - side, a + side, b + side, b - side};
                for (int k : {0, 1, 2, 0, 2, 3}) bv.insert(bv.end(), {q[k].x, q[k].y, q[k].z, 0.5f, 0.5f});
            }
            glUseProgram(spriteProg);
            glUniformMatrix4fv(glGetUniformLocation(spriteProg, "uVP"), 1, GL_FALSE, glm::value_ptr(vp));
            glUniform4f(glGetUniformLocation(spriteProg, "uColor"), 0.45f, 0.45f, 0.5f, 1.f);
            glBindTexture(GL_TEXTURE_2D, ui.whiteTex);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            drawSprite(bv);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glBindTexture(GL_TEXTURE_2D, terrainTex);
            glUseProgram(chunkProg);
        }
        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE);
        glDisable(GL_BLEND);

        // ---- Пустая рука: правая рука игрока из char.png
        if (camMode == 0 && held.empty() && !player.dead && g_in.screen != Screen::Dead) {
            glClear(GL_DEPTH_BUFFER_BIT);
            const float PI = glm::pi<float>();
            float sw = glm::mix(prevSwing, swing, partial);
            float s19 = std::sin(sw * sw * PI), s20 = std::sin(std::sqrt(sw) * PI);
            float hb = std::sin(walk * PI) * bob;
            glm::mat4 m(1.f);
            m = glm::translate(m, glm::vec3(hb * 0.5f, -std::abs(std::cos(walk * PI) * bob), 0.f));
            m = glm::translate(m, glm::vec3(-s20 * 0.3f, std::sin(std::sqrt(sw) * PI * 2.f) * 0.4f, -s19 * 0.4f));
            m = glm::translate(m, glm::vec3(0.64f, -0.6f, -0.72f));
            m = glm::rotate(m, glm::radians(45.f), glm::vec3(0, 1, 0));
            m = glm::rotate(m, glm::radians(s20 * 70.f), glm::vec3(0, 1, 0));
            m = glm::rotate(m, glm::radians(-s19 * 20.f), glm::vec3(0, 0, 1));
            m = glm::translate(m, glm::vec3(-1.f, 3.6f, 3.5f));
            m = glm::rotate(m, glm::radians(120.f), glm::vec3(0, 0, 1));
            m = glm::rotate(m, glm::radians(200.f), glm::vec3(1, 0, 0));
            m = glm::rotate(m, glm::radians(-135.f), glm::vec3(0, 1, 0));
            m = glm::translate(m, glm::vec3(5.6f, 0.f, 0.f));
            m = glm::scale(m, glm::vec3(1.f / 16.f));
            glm::ivec3 ec((int)std::floor(eye.x), (int)std::floor(eye.y), (int)std::floor(eye.z));
            std::vector<Vertex> arm;
            emitModel(arm, buildPlayerArm(), m, world->getSkyLight(ec.x, ec.y, ec.z) / 15.f, world->getBlockLight(ec.x, ec.y, ec.z) / 15.f);
            glm::mat4 handProj = glm::perspective(glm::radians(70.f), (float)fbw / fbh, 0.01f, 10.f);
            setWorldUniforms(chunkProg, handProj, glm::vec3(0));
            glUniform1f(glGetUniformLocation(chunkProg, "uFogStart"), 100.f);
            glUniform1f(glGetUniformLocation(chunkProg, "uFogEnd"), 200.f);
            glDisable(GL_CULL_FACE);
            glBindTexture(GL_TEXTURE_2D, charTex);
            drawDynamic(arm);
            glBindTexture(GL_TEXTURE_2D, terrainTex);
        }

        // ---- Предмет в руке (в пространстве камеры, поверх мира)
        if (camMode == 0 && !held.empty() && held.id != MAP && !player.dead) {
            glClear(GL_DEPTH_BUFFER_BIT);
            const float PI = glm::pi<float>();
            float sw = glm::mix(prevSwing, swing, partial);
            float s19 = std::sin(sw * sw * PI), s21 = std::sin(std::sqrt(sw) * PI);
            float hb = std::sin(walk * PI) * bob;
            glm::mat4 m(1.f);
            m = glm::translate(m, glm::vec3(hb * 0.5f, -std::abs(std::cos(walk * PI) * bob), 0.f));
            // Еда: предмет подносится ко рту и подрагивает (как в 1.0)
            if (useTicks > 0) {
                float f = useTicks + partial;
                float left = 1.f - f / 32.f;
                float k = 1.f - std::pow(std::max(left, 0.f), 27.f);
                if (left > 0.2f) m = glm::translate(m, glm::vec3(0.f, std::abs(std::cos(f / 4.f * PI) * 0.1f), 0.f));
                m = glm::translate(m, glm::vec3(k * 0.6f, -k * 0.5f, 0.f));
                m = glm::rotate(m, glm::radians(k * 90.f), glm::vec3(0, 1, 0));
                m = glm::rotate(m, glm::radians(k * 10.f), glm::vec3(1, 0, 0));
                m = glm::rotate(m, glm::radians(k * 30.f), glm::vec3(0, 0, 1));
            }
            m = glm::translate(m, glm::vec3(-s21 * 0.4f, std::sin(std::sqrt(sw) * PI * 2.f) * 0.2f, -s19 * 0.2f));
            m = glm::translate(m, glm::vec3(0.56f, -0.52f, -0.72f));
            m = glm::rotate(m, glm::radians(45.f), glm::vec3(0, 1, 0));
            m = glm::rotate(m, glm::radians(-s19 * 20.f), glm::vec3(0, 1, 0));
            m = glm::rotate(m, glm::radians(-s21 * 20.f), glm::vec3(0, 0, 1));
            m = glm::rotate(m, glm::radians(-s21 * 80.f), glm::vec3(1, 0, 0));
            m = glm::scale(m, glm::vec3(0.4f));
            glm::ivec3 ec((int)std::floor(eye.x), (int)std::floor(eye.y), (int)std::floor(eye.z));
            float hs = world->getSkyLight(ec.x, ec.y, ec.z) / 15.f, hbl = world->getBlockLight(ec.x, ec.y, ec.z) / 15.f;
            std::vector<Vertex> hv;
            GLuint handTex = terrainUiTex;
            if (itemIsCube(held)) {
                m = glm::translate(m, glm::vec3(-0.5f));
                std::vector<Vertex> model;
                appendBlockModel(model, (uint8_t)held.id, 1.f, 0.f,
                                 blockHasVariants((uint8_t)held.id) ? (uint8_t)held.damage : (uint8_t)3);
                appendTransformed(hv, model, m, hs, hbl);
            } else {
                // Плоский предмет «выдавлен» из иконки и держится под углом, как в 1.0
                bool fromItems = false;
                const std::vector<Vertex>* modelP = &itemModels.extruded(held, fromItems);
                if (held.id == FISHING_ROD && bobber) {
                    modelP = &itemModels.extrudedTile(5, 5, true); // удочка без лески: снасть заброшена
                    fromItems = true;
                }
                if (held.id == BOW && bowCharge > 0) {
                    // Натяжение лука (EnumAction.bow): лук уходит к центру экрана, приближается и
                    // вытягивается по мере натяжения, при полном натяжении мелко дрожит; иконка — стадия натяжения
                    float t = bowCharge + partial;
                    float f = t / 20.f;
                    f = std::min(1.f, (f * f + f * 2.f) / 3.f);
                    m = glm::rotate(m, glm::radians(-18.f), glm::vec3(0, 0, 1));
                    m = glm::rotate(m, glm::radians(-12.f), glm::vec3(0, 1, 0));
                    m = glm::rotate(m, glm::radians(-8.f), glm::vec3(1, 0, 0));
                    m = glm::translate(m, glm::vec3(-0.9f, 0.2f, 0.f));
                    if (f > 0.1f) m = glm::translate(m, glm::vec3(0.f, std::sin((t - 0.1f) * 1.3f) * 0.01f * (f - 0.1f), 0.f));
                    m = glm::translate(m, glm::vec3(0.f, 0.f, f * 0.1f));
                    m = glm::rotate(m, glm::radians(-335.f), glm::vec3(0, 0, 1));
                    m = glm::rotate(m, glm::radians(-50.f), glm::vec3(0, 1, 0));
                    m = glm::translate(m, glm::vec3(0.f, 0.5f, 0.f));
                    m = glm::scale(m, glm::vec3(1.f, 1.f, 1.f + f * 0.2f));
                    m = glm::translate(m, glm::vec3(0.f, -0.5f, 0.f));
                    m = glm::rotate(m, glm::radians(50.f), glm::vec3(0, 1, 0));
                    m = glm::rotate(m, glm::radians(335.f), glm::vec3(0, 0, 1));
                    int row = bowCharge >= 18 ? 8 : bowCharge > 13 ? 7 : 6;
                    modelP = &itemModels.extrudedTile(5, row, true);
                    fromItems = true;
                }
                const std::vector<Vertex>& model = *modelP;
                if (fromItems) handTex = itemsTex;
                m = glm::translate(m, glm::vec3(0.f, -0.3f, 0.f));
                m = glm::scale(m, glm::vec3(1.5f));
                m = glm::rotate(m, glm::radians(50.f), glm::vec3(0, 1, 0));
                m = glm::rotate(m, glm::radians(335.f), glm::vec3(0, 0, 1));
                m = glm::translate(m, glm::vec3(-0.9375f, -0.0625f, 0.f));
                // В 1.0 иконка в руке отзеркалена по u относительно нашей модели
                m = glm::translate(m, glm::vec3(1.f, 0.f, 0.f));
                m = glm::scale(m, glm::vec3(-1.f, 1.f, 1.f));
                appendTransformed(hv, model, m, hs, hbl);
            }
            glm::mat4 handProj = glm::perspective(glm::radians(70.f), (float)fbw / fbh, 0.01f, 10.f);
            setWorldUniforms(chunkProg, handProj, glm::vec3(0));
            glUniform1f(glGetUniformLocation(chunkProg, "uFogStart"), 100.f);
            glUniform1f(glGetUniformLocation(chunkProg, "uFogEnd"), 200.f);
            glDisable(GL_CULL_FACE);
            glBindTexture(GL_TEXTURE_2D, handTex);
            drawDynamic(hv);
            if (held.enchanted() && !hv.empty()) {
                float gp = 0.5f + 0.5f * std::sin((float)glfwGetTime() * 3.f);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE);
                glUniform3f(glGetUniformLocation(chunkProg, "uTint"), 0.5f * gp, 0.25f * gp, 0.8f * gp);
                drawDynamic(hv);
                glUniform3f(glGetUniformLocation(chunkProg, "uTint"), 1, 1, 1);
                glDisable(GL_BLEND);
            }
            glBindTexture(GL_TEXTURE_2D, terrainTex);
        }

        // ------------------------------------------------ Интерфейс
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        const float sc = lastSc;
        const float W = lastW, H = lastH; // размеры экрана в единицах GUI
        const float cx = fbw / 2.f, cyScreen = fbh / 2.f;
        ui.begin(fbw, fbh);

        // Картинка из текстуры в единицах GUI
        auto gimg = [&](GLuint tex, float x, float y, float u, float v, float w, float h, glm::vec4 col = glm::vec4(1)) {
            ui.image(tex, x * sc, y * sc, w * sc, h * sc, u, v, w, h, 256, 256, col);
        };
        auto gtextCenter = [&](const std::string& s, float x, float y, glm::vec4 col = glm::vec4(1), float k = 1.f) {
            ui.text(s, x * sc - ui.textWidth(s, sc * k) / 2, y * sc, sc * k, col);
        };
        // Кнопка из gui.png (200x20): выключенная v=46, обычная v=66, под курсором v=86
        auto button = [&](float x, float y, float w, const std::string& label, bool enabled) {
            bool hover = enabled && gmx >= x && gmx < x + w && gmy >= y && gmy < y + 20;
            float v = !enabled ? 46.f : hover ? 86.f : 66.f;
            gimg(guiTex, x, y, 0, v, w / 2, 20);
            gimg(guiTex, x + w / 2, y, 200 - w / 2, v, w / 2, 20);
            glm::vec4 col = !enabled ? glm::vec4(0.63f, 0.63f, 0.63f, 1) : hover ? glm::vec4(1, 1, 0.63f, 1) : glm::vec4(0.88f, 0.88f, 0.88f, 1);
            gtextCenter(label, x + w / 2, y + 6, col);
            return hover && g_in.menuClick;
        };

        if (eyeInWater) ui.rect(0, 0, (float)fbw, (float)fbh, {0.05f, 0.15f, 0.5f, 0.25f});
        if (camMode == 0 && inv.armor[0].id == PUMPKIN && pumpkinBlurTex && !player.dead) {
            glm::vec2 pp[4] = {{0, 0}, {(float)fbw, 0}, {(float)fbw, (float)fbh}, {0, (float)fbh}};
            glm::vec2 puv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
            ui.quad(pumpkinBlurTex, pp, puv, glm::vec4(1.f));
        }
        if (camMode == 0 && held.id == MAP && !player.dead && g_in.screen != Screen::Dead) {
            // Карта в руках (ItemRenderer.renderItemInFirstPerson 1.0): бумажная подложка, карта, стрелка игрока
            MapData* md = held.damage ? mapFor(held.damage) : nullptr;
            float S = fbh * 0.62f, x0 = (fbw - S) * 0.5f, y0 = fbh * 0.40f;
            float sw = glm::mix(prevSwing, swing, partial);
            y0 += std::sin(sw * glm::pi<float>()) * S * 0.08f; // взмах чуть опускает карту
            glm::vec2 uv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
            if (mapBgTex) {
                glm::vec2 p[4] = {{x0, y0}, {x0 + S, y0}, {x0 + S, y0 + S}, {x0, y0 + S}};
                ui.quad(mapBgTex, p, uv, glm::vec4(1.f));
            }
            float m0 = S * 7.f / 142.f, ms = S * 128.f / 142.f, mx = x0 + m0, my = y0 + m0;
            if (md) {
                if (mapTexId != held.damage || md->texDirty) {
                    std::vector<uint32_t> px(128 * 128);
                    for (int i = 0; i < 128 * 128; ++i) px[i] = mapPixelRGBA(md->colors[i]);
                    glBindTexture(GL_TEXTURE_2D, mapTex);
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 128, 128, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
                    md->texDirty = false;
                    mapTexId = held.damage;
                }
                glm::vec2 p[4] = {{mx, my}, {mx + ms, my}, {mx + ms, my + ms}, {mx, my + ms}};
                ui.quad(mapTex, p, uv, glm::vec4(1.f));
                // Стрелка игрока (значок 0), повёрнутая по взгляду; за краем карты не видна
                int s = 1 << md->scale;
                float fx = (player.pos.x - md->xCenter) / s, fz = (player.pos.z - md->zCenter) / s;
                if (md->dim == dimension && fx >= -63.f && fx <= 63.f && fz >= -63.f && fz <= 63.f && mapIconsTex) {
                    glm::vec2 c(mx + (fx + 64.f) / 128.f * ms, my + (fz + 64.f) / 128.f * ms);
                    glm::vec3 lk = player.look();
                    glm::vec2 fwd = glm::length(glm::vec2(lk.x, lk.z)) > 1e-4f ? glm::normalize(glm::vec2(lk.x, lk.z)) : glm::vec2(0, -1);
                    glm::vec2 rgt(-fwd.y, fwd.x);
                    float h = ms * 4.f / 128.f;
                    glm::vec2 ip[4] = {c + (-rgt + fwd) * h, c + (rgt + fwd) * h, c + (rgt - fwd) * h, c + (-rgt - fwd) * h};
                    glm::vec2 iuv[4] = {{0, 0}, {0.25f, 0}, {0.25f, 0.25f}, {0, 0.25f}};
                    ui.quad(mapIconsTex, ip, iuv, glm::vec4(1.f));
                }
            }
        }
        if (player.fireTicks > 0 && !player.inWater && !player.creative()) {
            // renderFireInFirstPerson: два языка пламени по нижним углам экрана, правый отражён
            float w = fbw * 0.42f, h = fbh * 0.55f;
            for (int k = 0; k < 2; ++k) {
                float x0 = k == 0 ? -fbw * 0.02f : fbw - w + fbw * 0.02f, y0 = fbh - h + fbh * 0.1f;
                float sk = (k == 0 ? 1.f : -1.f) * w * 0.12f; // лёгкий наклон к центру
                glm::vec2 p[4] = {{x0 + sk, y0}, {x0 + w + sk, y0}, {x0 + w, y0 + h}, {x0, y0 + h}};
                float u0 = 15.f / 16.f, u1 = 1.f, v0 = 1.f / 16.f, v1 = 2.f / 16.f;
                if (k == 1) std::swap(u0, u1);
                glm::vec2 uv[4] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
                ui.quad(terrainTex, p, uv, {1, 1, 1, 0.9f});
            }
        }
        if (portalTimer > 0) {
            // Портал «затягивает»: фиолетовая завеса усиливается
            // renderPortalOverlay 1.0: в начале завеса сразу 0.2, дальше растёт как t^4
            float a = std::min(1.f, portalTimer / 80.f);
            if (a < 1.f) a = a * a * a * a * 0.8f + 0.2f;
            ui.image(terrainTex, 0, 0, (float)fbw, (float)fbh, 14 * 16.f, 0, 16, 16, 256, 256, {1, 1, 1, a});
        }

        // Прицел из icons.png, инвертирует цвет под собой
        if (playing)
            ui.image(iconsTex, cx - 8 * sc, cyScreen - 8 * sc, 16 * sc, 16 * sc, 0, 0, 16, 16, 256, 256,
                     glm::vec4(1), UI::Blend::Invert);

        // Полоска здоровья дракона (GuiIngame.renderBossHealth 1.0): 182x5 из icons.png, надпись розовым
        if (bossFrac >= 0.f && g_in.screen == Screen::Playing) {
            float bx = W / 2 - 91, by = 12;
            gimg(iconsTex, bx, by, 0, 74, 182, 5);
            int bw = (int)(bossFrac * 183.f);
            if (bw > 0) gimg(iconsTex, bx, by, 0, 79, (float)std::min(bw, 182), 5);
            gtextCenter(bossName, W / 2 + 1.f / sc, by - 10 + 1.f / sc, {0.25f, 0.f, 0.25f, 1.f}); // тень
            gtextCenter(bossName, W / 2, by - 10, {1.f, 0.f, 1.f, 1.f});
        }

        // Хотбар из gui.png с предметами
        float hx = W / 2 - 91, hy = H - 22;
        gimg(guiTex, hx, hy, 0, 0, 182, 22);
        gimg(guiTex, hx - 1 + g_in.selected * 20, hy - 1, 0, 22, 24, 24);
        for (int i = 0; i < 9; ++i) drawItemStack(ui, guiTex2, inv.slots[i], hx + 3 + i * 20, hy + 3, sc);

        // Здоровье, броня, голод, воздух, опыт (только в выживании) — раскладка GuiIngame 1.0
        if (!player.creative()) {
            std::mt19937 hudRng((uint32_t)(g_in.tick * 312871));
            auto jitter = [&](int n) { return (int)(hudRng() % n); };
            gimg(iconsTex, hx, H - 29, 0, 64, 182, 5);
            int xpW = (int)(player.xpProgress * 183.f);
            if (xpW > 0) gimg(iconsTex, hx, H - 29, 0, 69, (float)xpW, 5);
            if (player.xpLevel > 0) {
                std::string lv = std::to_string(player.xpLevel);
                float lx = W / 2 - ui.textWidth(lv, sc) / sc / 2, ly = H - 35;
                for (auto [ox, oy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}})
                    ui.text(lv, (lx + ox) * sc, (ly + oy) * sc, sc, {0, 0, 0, 1}, false);
                ui.text(lv, lx * sc, ly * sc, sc, {0.5f, 1.f, 0.125f, 1}, false);
            }
            int armor = inv.armorValue();
            if (armor > 0)
                for (int i = 0; i < 10; ++i) {
                    float x = hx + i * 8, y = H - 49;
                    float u = i * 2 + 1 < armor ? 34.f : i * 2 + 1 == armor ? 25.f : 16.f;
                    gimg(iconsTex, x, y, u, 9, 9, 9);
                }
            bool flash = player.invulnerable > 0 && (player.invulnerable / 3) % 2 == 1;
            for (int i = 0; i < 10; ++i) {
                float x = hx + i * 8, y = H - 39;
                if (player.health <= 4) y += jitter(2);
                float hv = currentWorld.hardcore ? 45.f : 0.f; // в хардкоре сердца другие
                gimg(iconsTex, x, y, flash ? 25.f : 16.f, hv, 9, 9);
                float pu = player.hasEffect(EFF_WITHER) ? 72.f : (player.poisonTicks > 0 || player.hasEffect(EFF_POISON)) ? 36.f : 0.f; // отравленные или иссушённые (черные) сердца
                if (i * 2 + 1 < player.health) gimg(iconsTex, x, y, 52 + pu, hv, 9, 9);
                else if (i * 2 + 1 == player.health) gimg(iconsTex, x, y, 61 + pu, hv, 9, 9);
            }
            for (int i = 0; i < 10; ++i) {
                float x = W / 2 + 91 - i * 8 - 9, y = H - 39;
                if (player.saturation <= 0.f && g_in.tick % (player.food * 3 + 1) == 0) y += jitter(3) - 1;
                bool hungry = player.hasEffect(EFF_HUNGER); // «голодная» еда зелёная
                gimg(iconsTex, x, y, hungry ? 133.f : 16.f, 27, 9, 9);
                if (i * 2 + 1 < player.food) gimg(iconsTex, x, y, hungry ? 88.f : 52.f, 27, 9, 9);
                else if (i * 2 + 1 == player.food) gimg(iconsTex, x, y, hungry ? 97.f : 61.f, 27, 9, 9);
            }
            if (player.eyeInWater) {
                int full = (int)std::ceil((player.air - 2) * 10.0 / 300.0);
                int popping = (int)std::ceil(player.air * 10.0 / 300.0) - full;
                for (int i = 0; i < full + popping; ++i)
                    gimg(iconsTex, W / 2 + 91 - i * 8 - 9, H - 49, i < full ? 16.f : 25.f, 18, 9, 9);
            }
        }

        // Название выбранного предмета и сообщения
        if (nameTimer > 0.f && playing && !held.empty()) {
            float a = std::min(1.f, nameTimer);
            gtextCenter(itemDisplayName(held), W / 2, H - (player.creative() ? 36.f : 59.f), {1, 1, 1, a});
        }
        if (infoTimer > 0.f && !infoText.empty())
            gtextCenter(infoText, W / 2, H - 72, {1, 1, 0.6f, std::min(1.f, infoTimer)});

        // Список игроков: удерживать TAB
        if (playing && glfwGetKey(win, GLFW_KEY_TAB) == GLFW_PRESS) {
            std::vector<ListEntry> list = playerList;
            if (list.empty()) list.push_back({myId, opt.playerName, 0, dimension}); // одиночная игра — только ты
            int n = (int)list.size(), cols = 1, rows = n;
            while (rows > 20) { ++cols; rows = (n + cols - 1) / cols; }
            float colW = std::min(150.f, 300.f / cols);
            float x0 = std::floor((W - cols * colW) / 2.f), y0 = 10.f;
            ui.rect((x0 - 1) * sc, (y0 - 1) * sc, (x0 + colW * cols) * sc, (y0 + 9 * rows) * sc, {0, 0, 0, 0.5f});
            for (int i = 0; i < n; ++i) {
                float x = x0 + (i % cols) * colW, y = y0 + (i / cols) * 9.f;
                ui.rect(x * sc, y * sc, (x + colW - 1) * sc, (y + 8) * sc, {1, 1, 1, 0.125f});
                const ListEntry& e = list[i];
                std::string label = e.name + (e.dim == -1 ? " (Nether)" : e.dim == 1 ? " (End)" : "");
                glm::vec4 col = (mp && e.id == myId) ? glm::vec4(1, 1, 0.6f, 1) : glm::vec4(1);
                ui.text(label, x * sc, y * sc, sc, col);
                int bars = e.ping < 150 ? 0 : e.ping < 300 ? 1 : e.ping < 600 ? 2 : e.ping < 1000 ? 3 : 4;
                gimg(iconsTex, x + colW - 12, y, 0, 176.f + bars * 8.f, 10, 8);
            }
        }

        // Отладочный экран F3
        if (g_in.debug) {
            char buf[200];
            std::vector<std::string> lines;
            lines.push_back("MiniCraft 1.0 port (" + std::to_string((int)fps) + " fps)");
            std::snprintf(buf, sizeof(buf), "XYZ: %.3f / %.3f / %.3f", player.pos.x, player.pos.y, player.pos.z);
            lines.push_back(buf);
            std::snprintf(buf, sizeof(buf), "Chunk: %d %d  (loaded %zu, visible %zu)  E: %zu", pcx, pcz, world->chunks.size(),
                          visible.size(), items.size() + mobMgr.mobs.size());
            lines.push_back(buf);
            const char* facing[4] = {"east (+X)", "south (+Z)", "west (-X)", "north (-Z)"};
            int fi = ((int)std::floor(std::fmod(player.yaw + 45.f + 3600.f, 360.f) / 90.f)) % 4;
            std::snprintf(buf, sizeof(buf), "Facing: %s  yaw %.1f pitch %.1f", facing[fi], player.yaw, player.pitch);
            lines.push_back(buf);
            glm::ivec3 fc((int)std::floor(player.pos.x), (int)std::floor(player.pos.y), (int)std::floor(player.pos.z));
            std::snprintf(buf, sizeof(buf), "Light: sky %d, block %d", world->getSkyLight(fc.x, fc.y, fc.z), world->getBlockLight(fc.x, fc.y, fc.z));
            lines.push_back(buf);
            int64_t dayTime = worldTime % 24000;
            int hours = (int)((dayTime / 1000 + 6) % 24), minutes = (int)(dayTime % 1000 * 60 / 1000);
            std::snprintf(buf, sizeof(buf), "Day %lld, %02d:%02d  Seed: %u", (long long)(worldTime / 24000), hours, minutes, world->seed());
            lines.push_back(buf);
            std::snprintf(buf, sizeof(buf), "Health %d  Food %d  Sat %.1f  Exh %.2f  Air %d  Armor %d", player.health, player.food,
                          player.saturation, player.exhaustion, player.air, inv.armorValue());
            lines.push_back(buf);
            if (hasHit) {
                std::snprintf(buf, sizeof(buf), "Looking at: %s (%d, %d, %d) meta %d", blockName(target), hit.x, hit.y, hit.z,
                              world->getMeta(hit.x, hit.y, hit.z));
                lines.push_back(buf);
            }
            lines.push_back(std::string("Biome: ") + biomeInfo(world->loadedBiome(fc.x, fc.z)).name + "  Weather: " +
                            (thundering && raining ? "thunder" : raining ? "rain" : "clear") + "  Spawners: " +
                            std::to_string(mobMgr.spawners.size()));
            lines.push_back(std::string("Mode: ") + (player.creative() ? "creative" : "survival") + (player.fly ? ", flying" : "") +
                            (player.sprinting ? ", sprinting" : "") + (camMode ? ", camera " + std::to_string(camMode) : std::string()));
            float y = 2;
            for (auto& l : lines) {
                ui.rect(sc, (y - 1) * sc, 3 * sc + ui.textWidth(l, sc), (y + 9) * sc, {0.3f, 0.3f, 0.3f, 0.5f});
                ui.text(l, 2 * sc, y * sc, sc, {0.88f, 0.88f, 0.88f, 1}, false);
                y += 10;
            }
        }

        // ---- Окно с предметами
        if (g_in.screen == Screen::Container) {
            GuiContext ctx = makeCtx(gmx, gmy);
            // Отрисовка игрока в окошке GUI: fx, fy — точка между ступнями, scale — масштаб модели
            ctx.drawPlayer = [&](float fx, float fy, float scale) {
                ui.flushNow();
                float dx = fx - gmx, dy = (fy - scale * 1.65f) - gmy;
                float bodyTurn = std::atan(dx / 40.f) * 20.f, headTurn = std::atan(dx / 40.f) * 40.f, pitch = -std::atan(dy / 40.f) * 20.f;
                glm::mat4 proj = glm::ortho(0.f, (float)fbw, (float)fbh, 0.f, -1000.f, 1000.f);
                glm::mat4 root = glm::translate(glm::mat4(1.f), glm::vec3(fx * sc, fy * sc, 0.f));
                root = glm::scale(root, glm::vec3(scale * sc, -scale * sc, scale * sc));
                float yaw = 90.f + bodyTurn;
                root = glm::rotate(root, glm::radians(-(yaw + 90.f)), glm::vec3(0, 1, 0));
                PlayerPose pp = currentPlayerPose();
                pp.limbAmount = 0.f;
                pp.swing = 0.f;
                pp.riding = false;
                pp.headYaw = glm::radians(headTurn - bodyTurn);
                pp.headPitch = glm::radians(-pitch);
                glClear(GL_DEPTH_BUFFER_BIT);
                glEnable(GL_DEPTH_TEST);
                setWorldUniforms(chunkProg, proj, glm::vec3(0.f, 0.f, 1e6f));
                glUniform1f(glGetUniformLocation(chunkProg, "uFogStart"), 1e7f);
                glUniform1f(glGetUniformLocation(chunkProg, "uFogEnd"), 2e7f);
                glUniform1f(glGetUniformLocation(chunkProg, "uSkyFactor"), 1.f);
                glUniform1f(glGetUniformLocation(chunkProg, "uAmbient"), 0.f);
                drawPlayerModel(root, pp, 1.f, 0.f, inv.slots[g_in.selected]);
                glDisable(GL_DEPTH_TEST);
            };
            gui.draw(ctx);
        }

        // ---- Чат и команды (читы)
        auto chatMsg = [&](const std::string& t) {
            chatLog.push_back({t, glfwGetTime()});
            if (chatLog.size() > 100) chatLog.erase(chatLog.begin());
        };
        auto runCommand = [&](const std::string& line) {
            std::vector<std::string> a;
            {
                std::string cur;
                for (char c : line.substr(1)) {
                    if (c == ' ') { if (!cur.empty()) a.push_back(cur); cur.clear(); }
                    else cur += c;
                }
                if (!cur.empty()) a.push_back(cur);
            }
            if (a.empty()) return;
            auto lower = [](std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; };
            auto num = [](const std::string& s, bool& ok) { char* e = nullptr; long v = std::strtol(s.c_str(), &e, 10); ok = e && *e == 0 && !s.empty(); return (int)v; };
            std::string cmd = lower(a[0]);
            bool ok = true;
            if (!cheatsAllowed()) {
                chatMsg(mp ? "You are not allowed to use commands on this server" : "Cheats are not enabled on this world");
                return;
            }
            // Предмет по номеру или имени (пробелы — «_»): «/give diamond_sword», «/give 276 1»
            auto findItem = [&](const std::string& s, uint16_t& id) {
                bool isNum = false;
                int n = num(s, isNum);
                if (isNum) { if (n > 0 && isValidItem((uint16_t)n)) { id = (uint16_t)n; return true; } return false; }
                std::string want = lower(s);
                for (auto& c : want) if (c == '_') c = ' ';
                for (int i = 1; i < 2300; ++i) {
                    if (!isValidItem((uint16_t)i)) continue;
                    if (lower(itemName(makeStack((uint16_t)i))) == want) { id = (uint16_t)i; return true; }
                }
                return false;
            };
            if (cmd == "help" || cmd == "?") {
                chatMsg("Commands: /give <item> [count] [data], /time set|add <value|day|night>,");
                chatMsg("/gamemode <0|1>, /tp <x> <y> <z>, /weather <clear|rain|thunder>, /kill,");
                chatMsg("/xp <amount>, /seed, /summon <mob>, /difficulty <0-3>, /heal, /spawnpoint");
            } else if (cmd == "give" && a.size() >= 2) {
                uint16_t id = 0;
                if (!findItem(a[1], id)) { chatMsg("Unknown item: " + a[1]); return; }
                int count = 1, data = 0;
                if (a.size() >= 3) count = std::clamp(num(a[2], ok), 1, 64 * 36);
                if (a.size() >= 4) data = std::max(0, num(a[3], ok));
                int left = count;
                while (left > 0) {
                    ItemStack st = makeStack(id, std::min(left, maxStackSize(id)), (uint16_t)data);
                    left -= st.count;
                    if (!inv.add(st)) throwItem(st);
                }
                chatMsg("Given " + std::to_string(count) + " * " + itemName(makeStack(id, 1, (uint16_t)data)));
            } else if (cmd == "time" && a.size() >= 3) {
                std::string v = lower(a[2]);
                int t = v == "day" ? 1000 : v == "night" ? 13000 : num(a[2], ok);
                if (!ok) { chatMsg("Bad time value"); return; }
                if (lower(a[1]) == "set") worldTime = worldTime - (worldTime % 24000) + t;
                else worldTime += t;
                chatMsg("Time " + std::string(lower(a[1]) == "set" ? "set to " : "added: ") + std::to_string(t));
            } else if (cmd == "gamemode" && a.size() >= 2) {
                std::string v = lower(a[1]);
                bool creative = v == "1" || v == "creative" || v == "c";
                player.mode = creative ? GameMode::Creative : GameMode::Survival;
                if (!creative) player.fly = false;
                chatMsg(creative ? "Game mode: Creative" : "Game mode: Survival");
            } else if (cmd == "locate" && a.size() >= 2) {
                // Не из 1.0 — добавлено по просьбе: координаты ближайшего строения
                std::string kind = lower(a[1]);
                glm::ivec3 at;
                if (kind != "village" && kind != "stronghold" && kind != "mineshaft" && kind != "fortress" && kind != "dungeon")
                    chatMsg("Unknown structure. Try: village, stronghold, mineshaft, fortress, dungeon");
                else if (world->locateStructure(kind, player.pos, at)) {
                    int dist = (int)std::round(glm::length(glm::vec2(at.x - player.pos.x, at.z - player.pos.z)));
                    chatMsg("The nearest " + kind + " is at " + std::to_string(at.x) + " " + std::to_string(at.y) + " " +
                            std::to_string(at.z) + " (" + std::to_string(dist) + " blocks away)");
                } else if ((kind == "fortress") != (dimension == -1) && kind != "dungeon") {
                    chatMsg(kind == "fortress" ? "Fortresses are only in the Nether" : "There are no " + kind + "s in this dimension");
                } else {
                    chatMsg(kind == "dungeon" ? "No dungeon in the loaded area" : "Could not find a " + kind + " nearby");
                }
            } else if ((cmd == "tp" || cmd == "teleport") && a.size() >= 4) {
                auto coord = [&](const std::string& s, float base) {
                    if (!s.empty() && s[0] == '~') return base + (s.size() > 1 ? (float)std::atof(s.c_str() + 1) : 0.f);
                    return (float)std::atof(s.c_str());
                };
                glm::vec3 t(coord(a[1], player.pos.x), coord(a[2], player.pos.y), coord(a[3], player.pos.z));
                player.pos = player.prevPos = t;
                player.motion = glm::vec3(0.f);
                player.fallDistance = 0.f;
                chatMsg("Teleported to " + std::to_string((int)t.x) + ", " + std::to_string((int)t.y) + ", " + std::to_string((int)t.z));
            } else if (cmd == "weather" && a.size() >= 2) {
                std::string v = lower(a[1]);
                raining = v != "clear";
                thundering = v == "thunder";
                rainTime = 12000;
                thunderTime = 12000;
                chatMsg("Weather: " + v);
            } else if (cmd == "toggledownfall") {
                g_in.toggleWeather = true;
            } else if (cmd == "kill") {
                player.damageSource = 0;
                TickEvents ev0;
                hurtPlayer(player, 1000, ev0, true);
                chatMsg("Ouch. That looks like it hurt.");
            } else if (cmd == "xp" && a.size() >= 2) {
                int n = num(a[1], ok);
                if (ok && n > 0) { addXp(n); chatMsg("Given " + std::to_string(n) + " experience"); }
            } else if (cmd == "seed") {
                chatMsg("Seed: " + std::to_string((int32_t)seed));
            } else if (cmd == "summon" && a.size() >= 2) {
                std::string want = lower(a[1]);
                for (auto& c : want) if (c == '_') c = ' ';
                int found = -1;
                for (int i = 0; i < (int)MobType::COUNT; ++i)
                    if (lower(mobDef((MobType)i).name) == want) found = i;
                if (found < 0) { chatMsg("Unknown mob: " + a[1]); return; }
                glm::vec3 f = player.look();
                f.y = 0;
                glm::vec3 at = player.pos + glm::normalize(f + glm::vec3(1e-4f)) * 3.f;
                mobMgr.spawn((MobType)found, at, player.yaw + 180.f);
                chatMsg(std::string("Summoned ") + mobDef((MobType)found).name);
            } else if (cmd == "difficulty" && a.size() >= 2) {
                int dv = std::clamp(num(a[1], ok), 0, 3);
                opt.difficulty = dv;
                applyOptions();
                static const char* DN[4] = {"Peaceful", "Easy", "Normal", "Hard"};
                chatMsg(std::string("Difficulty: ") + DN[dv]);
            } else if (cmd == "heal") {
                player.health = 20;
                player.food = 20;
                player.saturation = 5.f;
                chatMsg("Healed");
            } else if (cmd == "spawnpoint") {
                spawnPoint = player.pos;
                world->hasBedSpawn = false;
                chatMsg("Spawn point set");
            } else {
                chatMsg("Unknown command. Type /help for help.");
            }
        };
        // Команды для подсказок: где выполняется (0 — только одиночная игра, 1 — у игрока, 2 — у сервера в сетевой, 3 — только сервер)
        struct CmdHelp { const char* name; const char* args; int scope; };
        static const CmdHelp CMDS[] = {
            {"give", "<item> [count] [data]", 1}, {"tp", "<x> <y> <z>", 1}, {"gamemode", "<survival|creative>", 1},
            {"kill", "", 1}, {"xp", "<amount>", 1}, {"locate", "<structure>", 1}, {"heal", "", 1}, {"spawnpoint", "", 1}, {"seed", "", 1},
            {"summon", "<mob>", 0}, {"time", "<set|add> <value|day|night>", 2}, {"weather", "<clear|rain|thunder>", 2},
            {"toggledownfall", "", 2}, {"difficulty", "<0-3>", 2}, {"help", "", 2}, {"say", "<message>", 3},
            {"list", "", 3}, {"kick", "<player>", 3}, {"save", "", 3}, {"stop", "", 3},
        };
        auto cmdVisible = [&](const CmdHelp& c) { return mp ? c.scope != 0 : c.scope != 3; };
        auto lowerS = [](std::string v) { for (auto& ch : v) ch = (char)std::tolower((unsigned char)ch); return v; };
        // Варианты для аргумента номер argi команды cmd
        auto argOptions = [&](const std::string& cmd, int argi) {
            std::vector<std::string> o;
            if (cmd == "give" && argi == 1) {
                static std::vector<std::string> items;
                if (items.empty()) {
                    for (int i = 1; i < 2300; ++i) {
                        if (!isValidItem((uint16_t)i)) continue;
                        std::string nm = lowerS(itemName(makeStack((uint16_t)i)));
                        for (auto& ch : nm) if (ch == ' ') ch = '_';
                        if (std::find(items.begin(), items.end(), nm) == items.end()) items.push_back(nm);
                    }
                    std::sort(items.begin(), items.end());
                }
                o = items;
            } else if (cmd == "summon" && argi == 1) {
                for (int i = 0; i < (int)MobType::COUNT; ++i) {
                    std::string nm = lowerS(mobDef((MobType)i).name);
                    for (auto& ch : nm) if (ch == ' ') ch = '_';
                    o.push_back(nm);
                }
            } else if (cmd == "gamemode" && argi == 1) o = {"survival", "creative"};
            else if (cmd == "locate" && argi == 1) o = {"village", "stronghold", "mineshaft", "fortress", "dungeon"};
            else if (cmd == "time" && argi == 1) o = {"set", "add"};
            else if (cmd == "time" && argi == 2) o = {"day", "night"};
            else if (cmd == "weather" && argi == 1) o = {"clear", "rain", "thunder"};
            else if (cmd == "difficulty" && argi == 1) o = {"0", "1", "2", "3"};
            else if (cmd == "kick" && argi == 1) for (auto& e : playerList) o.push_back(e.name);
            else if (cmd == "tp" && argi >= 1 && argi <= 3) o = {"~"};
            return o;
        };
        // Разбор строки чата: номер текущего слова, начало слова, само слово и команда
        struct ChatParse { int argi = 0; size_t tokStart = 1; std::string token, cmd; };
        auto parseChat = [&](const std::string& line) {
            ChatParse cp;
            size_t sp = line.rfind(' ');
            cp.tokStart = sp == std::string::npos ? 1 : sp + 1;
            cp.token = lowerS(line.substr(std::min(cp.tokStart, line.size())));
            size_t first = line.find(' ');
            cp.cmd = lowerS(line.substr(1, first == std::string::npos ? std::string::npos : first - 1));
            for (size_t i = 1; i < cp.tokStart && i < line.size(); ++i)
                if (line[i] == ' ' && (i + 1 < line.size() && line[i + 1] != ' ')) ++cp.argi;
            if (cp.tokStart > 1 && cp.argi == 0) cp.argi = 1;
            return cp;
        };
        auto matches = [&](const ChatParse& cp) {
            std::vector<std::string> m;
            if (cp.argi == 0) {
                for (auto& c : CMDS)
                    if (cmdVisible(c) && std::string(c.name).rfind(cp.token, 0) == 0) m.push_back(c.name);
            } else {
                for (auto& o : argOptions(cp.cmd, cp.argi))
                    if (lowerS(o).rfind(cp.token, 0) == 0) m.push_back(o);
            }
            return m;
        };
        if (g_in.openChat && g_in.screen == Screen::Playing) {
            tabCands.clear();
            tabLast.clear();
            chatLine = g_in.openChat == 2 ? "/" : "";
            chatHistPos = -1;
            g_in.typed.clear();
            g_in.keyEnter = g_in.keyEscape = g_in.keyBackspace = g_in.keyUp = g_in.keyDown = false;
            openScreen(win, Screen::Chat);
        }
        g_in.openChat = 0;
        if (g_in.screen == Screen::Chat) {
            for (char ch : g_in.typed)
                if (chatLine.size() < 100) chatLine += ch;
            g_in.typed.clear();
            if (g_in.keyBackspace && !chatLine.empty()) chatLine.pop_back();
            if (g_in.keyTab && !chatLine.empty() && chatLine[0] == '/' && cheatsAllowed()) {
                // TAB: подставить вариант; повторный TAB — следующий
                if (tabCands.empty() || chatLine != tabLast) {
                    ChatParse cp = parseChat(chatLine);
                    tabBase = chatLine.substr(0, std::min(cp.tokStart, chatLine.size()));
                    tabCands = matches(cp);
                    tabIdx = 0;
                } else {
                    tabIdx = (tabIdx + 1) % tabCands.size();
                }
                if (!tabCands.empty()) {
                    chatLine = tabBase + tabCands[tabIdx] + (tabCands.size() == 1 ? " " : "");
                    tabLast = chatLine;
                    if (tabCands.size() == 1) tabCands.clear();
                }
            }
            if ((g_in.keyUp || g_in.keyDown) && !chatHistory.empty()) {
                if (chatHistPos < 0) chatHistPos = (int)chatHistory.size();
                chatHistPos = std::clamp(chatHistPos + (g_in.keyUp ? -1 : 1), 0, (int)chatHistory.size() - 1);
                chatLine = chatHistory[chatHistPos];
            }
            if (g_in.keyEnter) {
                std::string line = chatLine;
                while (!line.empty() && line.back() == ' ') line.pop_back();
                if (!line.empty()) {
                    chatHistory.push_back(line);
                    if (mp) {
                        // Сетевая игра: читы про самого игрока — здесь, про мир (время, погода) и сообщения — серверу
                        std::string w0 = line.substr(1, line.find(' ') == std::string::npos ? std::string::npos : line.find(' ') - 1);
                        static const char* LOCAL[] = {"give", "tp", "teleport", "gamemode", "kill", "xp", "heal", "spawnpoint", "seed"};
                        bool local = false;
                        if (line[0] == '/') for (auto* l : LOCAL) local |= w0 == l;
                        if (local) runCommand(line);
                        else { net::Writer w; w.str(line); netConn.send(C_CHAT, w); }
                    } else if (line[0] == '/') runCommand(line);
                    else chatMsg("<" + opt.playerName + "> " + line);
                }
                openScreen(win, Screen::Playing);
            } else if (g_in.keyEscape) {
                openScreen(win, Screen::Playing);
            }
            g_in.keyEnter = g_in.keyEscape = g_in.keyBackspace = g_in.keyUp = g_in.keyDown = g_in.keyTab = false;
        }
        // Имена других игроков над головой
        for (auto& [oid, rp] : others) {
            if (!rp.has || rp.net.dim != dimension || (rp.net.flags & 8)) continue;
            glm::vec3 head = glm::mix(rp.prevPos, rp.pos, partial) + glm::vec3(0, 2.1f, 0);
            glm::vec4 cp = lastVP * glm::vec4(head, 1.f);
            if (cp.w <= 0.1f || glm::length(head - player.eye()) > 64.f) continue;
            float sx = (cp.x / cp.w * 0.5f + 0.5f) * W, sy = (1.f - (cp.y / cp.w * 0.5f + 0.5f)) * H;
            if (sx < -50 || sx > W + 50 || sy < -20 || sy > H + 20) continue;
            float tw = ui.textWidth(rp.name, sc) / sc;
            ui.rect((sx - tw / 2 - 1) * sc, (sy - 1) * sc, (sx + tw / 2 + 1) * sc, (sy + 8) * sc, {0, 0, 0, 0.25f});
            ui.text(rp.name, (sx - tw / 2) * sc, sy * sc, sc, (rp.net.flags & 2) ? glm::vec4(1, 1, 1, 0.3f) : glm::vec4(1, 1, 1, 1), false);
        }
        // Сообщения над хотбаром: видны 10 секунд и гаснут; с открытым чатом — последние 20
        {
            bool open = g_in.screen == Screen::Chat;
            double now = glfwGetTime();
            float y = H - 48;
            int shown = 0;
            for (int i = (int)chatLog.size() - 1; i >= 0 && shown < (open ? 20 : 10); --i) {
                double age = now - chatLog[i].second;
                if (!open && age > 10.0) break;
                float alpha = open ? 1.f : (float)std::clamp((10.0 - age) / 1.0, 0.0, 1.0);
                ui.rect(2 * sc, (y - 1) * sc, 322 * sc, (y + 8) * sc, {0, 0, 0, 0.5f * alpha});
                ui.text(chatLog[i].first, 2 * sc, y * sc, sc, {1, 1, 1, alpha});
                y -= 9;
                ++shown;
            }
            if (open) {
                ui.rect(2 * sc, (H - 14) * sc, (W - 2) * sc, (H - 2) * sc, {0, 0, 0, 0.5f});
                bool blink = ((int)(now * 3.0)) % 2 == 0;
                ui.text("> " + chatLine + (blink ? "_" : ""), 4 * sc, (H - 12) * sc, sc, {0.88f, 0.88f, 0.88f, 1});
                // Подсказки команд (только если разрешены читы): список команд или варианты текущего аргумента
                if (!chatLine.empty() && chatLine[0] == '/' && cheatsAllowed()) {
                    ChatParse cp = parseChat(chatLine);
                    std::vector<std::pair<std::string, glm::vec4>> rows;
                    const glm::vec4 GRAYC(0.67f, 0.67f, 0.67f, 1), YEL(1, 1, 0.33f, 1), WHITEC(1, 1, 1, 1);
                    std::string cur = tabLast == chatLine && !tabCands.empty() ? tabCands[tabIdx] : "";
                    if (cp.argi == 0) {
                        for (auto& c : CMDS)
                            if (cmdVisible(c) && std::string(c.name).rfind(cp.token, 0) == 0)
                                rows.push_back({"/" + std::string(c.name) + (c.args[0] ? " " : "") + c.args, c.name == cur ? YEL : WHITEC});
                        if (rows.empty()) rows.push_back({"Unknown command", glm::vec4(1, 0.4f, 0.4f, 1)});
                    } else {
                        for (auto& c : CMDS)
                            if (cmdVisible(c) && cp.cmd == c.name) rows.push_back({"/" + std::string(c.name) + (c.args[0] ? " " : "") + c.args, GRAYC});
                        std::vector<std::string> m = matches(cp);
                        for (auto& o : m) rows.push_back({o, o == cur ? YEL : WHITEC});
                    }
                    const int MAXR = 10;
                    int extra = (int)rows.size() > MAXR ? (int)rows.size() - (MAXR - 1) : 0;
                    if (extra) { rows.resize(MAXR - 1); rows.push_back({"... and " + std::to_string(extra) + " more (TAB)", GRAYC}); }
                    float bw = 0.f;
                    for (auto& rw : rows) bw = std::max(bw, ui.textWidth(rw.first, sc) / sc);
                    float by = H - 16 - rows.size() * 10.f;
                    ui.rect(2 * sc, (by - 2) * sc, (8 + bw) * sc, (H - 15) * sc, {0, 0, 0, 0.8f});
                    for (size_t i = 0; i < rows.size(); ++i) ui.text(rows[i].first, 4 * sc, (by + i * 10.f) * sc, sc, rows[i].second);
                }
            }
        }

        // ---- Редактор таблички (GuiEditSign)
        if (g_in.screen == Screen::Sign) {
            auto it = world->signs.find(posKey(signEdit.x, signEdit.y, signEdit.z));
            bool close = it == world->signs.end() || g_in.keyEscape;
            if (!close) {
                auto& lines = it->second;
                std::string& cur = lines[signLine];
                for (char ch : g_in.typed) {
                    float w = 0.f;
                    for (unsigned char c2 : cur) w += ui.glyphWidth(c2) + 1;
                    if (cur.size() < 15 && w + ui.glyphWidth((unsigned char)ch) + 1 <= 90.f) cur += ch;
                }
                if (g_in.keyBackspace && !cur.empty()) cur.pop_back();
                if (g_in.keyDown) signLine = (signLine + 1) % 4;
                if (g_in.keyUp) signLine = (signLine + 3) % 4;
                ui.rect(0, 0, (float)fbw, (float)fbh, {0.06f, 0.06f, 0.06f, 0.5f});
                gtextCenter("Edit sign message:", W / 2, 40);
                float bx = W / 2 - 60, by = 60;
                for (int i = 0; i < 8; ++i)
                    for (int j = 0; j < 4; ++j)
                        ui.image(terrainUiTex, (bx + i * 15) * sc, (by + j * 15) * sc, 15 * sc, 15 * sc, 64, 0, 16, 16, 256, 256);
                bool blink = ((int)(glfwGetTime() * 3.f)) % 2 == 0;
                for (int i = 0; i < 4; ++i) {
                    std::string l = lines[i];
                    if (i == signLine && blink) l = "> " + l + " <";
                    float tw = ui.textWidth(l, sc);
                    ui.text(l, W / 2 * sc - tw / 2, (by + 10 + i * 10) * sc, sc, {0, 0, 0, 1}, false);
                }
                if (button(W / 2 - 100, H / 4 + 120, 200, "Done", true)) close = true;
            }
            g_in.typed.clear();
            g_in.keyBackspace = g_in.keyUp = g_in.keyDown = g_in.keyEscape = false;
            if (close) {
                if (mp && it != world->signs.end()) {
                    net::Writer w;
                    w.i32(signEdit.x); w.u8((uint8_t)signEdit.y); w.i32(signEdit.z);
                    for (auto& l : it->second) w.str(l);
                    netConn.send(C_SIGN, w);
                }
                openScreen(win, Screen::Playing);
            }
        }

        // ---- Сон: экран темнеет, кнопка «Leave Bed»
        if (g_in.screen == Screen::Sleep) {
            float a = std::min(1.f, (sleepTimer + partial) / 100.f);
            ui.rect(0, 0, (float)fbw, (float)fbh, {0.063f, 0.063f, 0.125f, a * 0.8f});
            if (button(W / 2 - 100, H - 40, 200, "Leave Bed", true) || g_in.closeGui) {
                g_in.closeGui = false;
                wakeUp();
            }
        }

        // ---- Титры после победы над драконом (свой текст, без поэмы из оригинала)
        if (g_in.screen == Screen::Credits && creditsTime >= 0) {
            ++creditsTime;
            ui.rect(0, 0, (float)fbw, (float)fbh, {0.f, 0.f, 0.f, 1.f});
            static const char* LINES[] = {"THE END", "", "", "You defeated the Ender Dragon.", "The way home is open.", "", "", "",
                                          "MiniCraft", "a clean-room port of Minecraft 1.0", "", "Code", "Claude and Kirill", "",
                                          "Original game", "Mojang (2009-2011)", "", "Music", "C418", "", "", "",
                                          "Thank you for playing!"};
            const int n = sizeof(LINES) / sizeof(LINES[0]);
            float scroll = creditsTime * 0.5f;
            for (int i = 0; i < n; ++i) {
                float ly = H + 20.f + i * 14.f - scroll;
                if (ly < -20.f || ly > H + 20.f) continue;
                gtextCenter(LINES[i], W / 2, ly, i == 0 || i == 8 ? glm::vec4(1, 1, 0.4f, 1) : glm::vec4(1), i == 0 ? 2.f : 1.f);
            }
            if (H + 20.f + n * 14.f - scroll < -20.f || g_in.closeGui) {
                g_in.closeGui = false;
                creditsTime = -1;
                openScreen(win, Screen::Playing);
            }
        }

        // ---- Список достижений
        if (g_in.screen == Screen::Achievements) {
            // GuiAchievements 1.0: окно 256x202, внутри карта 224x155 с фоном из блоков; достижения — рамки 26x26,
            // от каждого линия к родителю; карту тянут мышью, подсказка при наведении
            ui.rect(0, 0, (float)fbw, (float)fbh, {0.06f, 0.06f, 0.06f, 0.75f});
            const float WW = 256, WH = 202, PW = 224, PH = 155;
            float wx = std::floor(W / 2 - WW / 2), wy = std::floor(H / 2 - WH / 2);
            float px0 = wx + 16, py0 = wy + 17;
            bool down = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            bool inPane = gmx >= px0 && gmx < px0 + PW && gmy >= py0 && gmy < py0 + PH;
            static bool achPrevDown = true; // клик, открывший экран, не должен тянуть карту
            bool pressStart = down && !achPrevDown;
            achPrevDown = down;
            if (down && (achDragging || (inPane && pressStart))) {
                if (achDragging) { achMapX -= gmx - achDragX; achMapY -= gmy - achDragY; }
                achDragging = true;
                achDragX = gmx; achDragY = gmy;
            } else {
                achDragging = false;
            }
            // Границы карты: от -5..10 по столбцам и -5..13 по строкам (как в 1.0), в пикселях по 24
            achMapX = std::clamp(achMapX, -5 * 24.f - 12.f, 10 * 24.f - PW + 36.f);
            achMapY = std::clamp(achMapY, -5 * 24.f - 12.f, 13 * 24.f - PH + 36.f);
            // Всё внутри карты рисуем с обрезкой по окну
            ui.flushNow();
            glEnable(GL_SCISSOR_TEST);
            glScissor((int)(px0 * sc), (int)(fbh - (py0 + PH) * sc), (int)(PW * sc), (int)(PH * sc));
            // Фон: плитки 16x16, глубже — камень, руды и бедрок
            int tx0 = (int)std::floor((achMapX + 288.f) / 16.f), ty0 = (int)std::floor((achMapY + 288.f) / 16.f);
            float ox = std::fmod(achMapX + 288.f, 16.f), oy = std::fmod(achMapY + 288.f, 16.f);
            for (int ty = 0; ty <= (int)(PH / 16) + 1; ++ty)
                for (int tx = 0; tx <= (int)(PW / 16) + 1; ++tx) {
                    int depth = ty0 + ty;
                    uint32_t h = hash32((uint32_t)(1234 + (tx0 + tx) * 7919 + depth * 104729));
                    int r = (int)(h % (uint32_t)(1 + depth)) + depth / 2;
                    int tile = T(2, 1); // песок
                    if (r > 37 || depth == 35) tile = T(1, 1);                  // бедрок
                    else if (r == 22) tile = (h >> 8) % 2 ? T(2, 3) : T(3, 3);   // алмазная / красная руда
                    else if (r == 10) tile = T(1, 2);                           // железная
                    else if (r == 8) tile = T(2, 2);                            // угольная
                    else if (r > 4) tile = T(1, 0);                             // камень
                    else if (r > 0) tile = T(2, 0);                             // земля
                    float bright = 0.6f - std::min(0.3f, depth * 0.01f);
                    ui.image(terrainUiTex, (px0 + tx * 16 - ox) * sc, (py0 + ty * 16 - oy) * sc, 16 * sc, 16 * sc, (tile % 16) * 16.f + 0.01f, (tile / 16) * 16.f + 0.01f, 15.98f, 15.98f, 256, 256, {bright, bright, bright, 1});
                }
            auto cellX = [&](int i) { return px0 + ACH[i].col * 24 - achMapX + 3; };
            auto cellY = [&](int i) { return py0 + ACH[i].row * 24 - achMapY + 3; };
            // Линии к родителям: получено — светлая, доступно — мигающая зелёная, закрыто — чёрная
            float pulse = 0.5f + 0.5f * std::sin((float)glfwGetTime() * 6.f);
            for (int i = 0; i < ACH_N; ++i) {
                int pi = achIndex(ACH[i].parent);
                if (pi < 0) continue;
                bool has = achHas(ACH[i].bit), avail = achHas(ACH[i].parent);
                glm::vec4 col = has ? glm::vec4(0.63f, 0.63f, 0.63f, 1) : avail ? glm::vec4(0.f, 0.5f + 0.5f * pulse, 0.f, 1) : glm::vec4(0, 0, 0, 1);
                float x1 = cellX(i) + 11, y1 = cellY(i) + 11, x2 = cellX(pi) + 11, y2 = cellY(pi) + 11;
                ui.rect(std::min(x1, x2) * sc, y1 * sc, (std::max(x1, x2) + 1) * sc, (y1 + 1) * sc, col);
                ui.rect(x2 * sc, std::min(y1, y2) * sc, (x2 + 1) * sc, (std::max(y1, y2) + 1) * sc, col);
            }
            int hoverI = -1;
            for (int i = 0; i < ACH_N; ++i) {
                float x = cellX(i), y = cellY(i);
                bool has = achHas(ACH[i].bit), avail = achHas(ACH[i].parent);
                float k = has ? 1.f : avail ? 0.6f : 0.3f;
                if (haveAchBg) ui.image(achBgTex, (x - 2) * sc, (y - 2) * sc, 26 * sc, 26 * sc, ACH[i].special ? 26.f : 0.f, 202, 26, 26, 256, 256, {k, k, k, 1});
                ItemStack icon = makeStack(ACH[i].icon, 1, ACH[i].icon == POTION ? 8193 : 0);
                drawItemStack(ui, guiTex2, icon, x + 3, y + 3, sc);
                if (!has) ui.rect((x + 3) * sc, (y + 3) * sc, (x + 19) * sc, (y + 19) * sc, {0, 0, 0, 1.f - k}); // затемнение значка
                if (inPane && gmx >= x - 2 && gmx < x + 24 && gmy >= y - 2 && gmy < y + 24) hoverI = i;
            }
            ui.flushNow();
            glDisable(GL_SCISSOR_TEST);
            // Рамка окна и заголовок
            if (haveAchBg) ui.image(achBgTex, wx * sc, wy * sc, WW * sc, WH * sc, 0, 0, WW, WH, 256, 256);
            int got = 0;
            for (int i = 0; i < ACH_N; ++i) got += achHas(ACH[i].bit);
            ui.text("Achievements", (wx + 15) * sc, (wy + 5) * sc, sc, {0.25f, 0.25f, 0.25f, 1}, false);
            // Подсказка: название (жёлтое — получено, серое — нет) и описание / «Requires ...»
            if (hoverI >= 0) {
                const AchDef& a = ACH[hoverI];
                bool has = achHas(a.bit), avail = achHas(a.parent);
                std::string title = a.name;
                std::string desc = avail ? a.desc : std::string("Requires '") + ACH[achIndex(a.parent)].name + "'";
                float tw = std::max(120.f, std::max(ui.textWidth(title, sc), ui.textWidth(desc, sc)) / sc + 6);
                float bx = gmx + 12, by = gmy - 4;
                ui.rect((bx - 3) * sc, (by - 3) * sc, (bx + tw + 3) * sc, (by + 24) * sc, {0.06f, 0.f, 0.06f, 0.94f});
                ui.text(title, bx * sc, by * sc, sc, has ? glm::vec4(1, 1, 0.33f, 1) : avail ? glm::vec4(1, 1, 1, 1) : glm::vec4(0.5f, 0.5f, 0.5f, 1));
                ui.text(desc, bx * sc, (by + 11) * sc, sc, avail ? glm::vec4(0.66f, 0.66f, 0.66f, 1) : glm::vec4(0.55f, 0.35f, 0.35f, 1));
                if (has) ui.text("Taken!", bx * sc, (by + 22) * sc, sc, {0.58f, 0.58f, 1.f, 1});
            }
            if (button(wx + WW / 2 - 40, wy + WH + 4, 80, "Done", true)) openScreen(win, Screen::Paused);
            (void)got;
        }
        // ---- Уведомление «Achievement get!» справа сверху
        if (!achToasts.empty()) {
            double t = glfwGetTime() - achToasts.front().second;
            if (t > 4.0) achToasts.erase(achToasts.begin());
            else {
                float slide = (float)std::min({t / 0.5, 1.0, (4.0 - t) / 0.5});
                float bx = W - 160, by = -32.f * (1.f - slide); // выезжает сверху, как GuiAchievement 1.0
                if (haveAchBg) ui.image(achBgTex, bx * sc, by * sc, 160 * sc, 32 * sc, 96, 202, 160, 32, 256, 256);
                else ui.rect(bx * sc, by * sc, (bx + 160) * sc, (by + 32) * sc, {0.12f, 0.12f, 0.12f, 0.95f});
                const AchDef& a = ACH[achToasts.front().first];
                drawItemStack(ui, guiTex2, makeStack(a.icon, 1, a.icon == POTION ? 8193 : 0), bx + 8, by + 8, sc);
                ui.text("Achievement get!", (bx + 30) * sc, (by + 7) * sc, sc, {1, 1, 0.33f, 1});
                ui.text(a.name, (bx + 30) * sc, (by + 18) * sc, sc);
            }
        }

        // ---- Меню игры (пауза), как GuiIngameMenu 1.0
        if (g_in.screen == Screen::Paused) {
            ui.rect(0, 0, (float)fbw, (float)fbh, {0.06f, 0.06f, 0.06f, 0.6f});
            gtextCenter("Game menu", W / 2, 40);
            float bx = W / 2 - 100, by = H / 4 + 8;
            if (button(bx, by, 200, "Back to Game", true)) openScreen(win, Screen::Playing);
            if (button(bx, by + 24, 98, "Achievements", true)) openScreen(win, Screen::Achievements);
            button(bx + 102, by + 24, 98, "Statistics", false);
            if (button(bx, by + 72, 200, "Options...", true)) {
                menu.fromGame = true;
                menu.open(MenuSystem::Page::Options, saves);
                openScreen(win, Screen::Menu);
            }
            if (button(bx, by + 96, 200, mp ? "Disconnect" : "Save and Quit to Title", true)) { g_in.menuClick = false; leaveWorld(true); }
        }

        // ---- Настройки поверх игры (из меню паузы)
        if (g_in.screen == Screen::Menu && inGame) {
            MenuInput min;
            min.mx = gmx;
            min.my = gmy;
            min.click = g_in.menuClick;
            min.mouseDown = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            min.escape = g_in.keyEscape;
            min.keyPressed = g_in.lastKey;
            g_in.lastKey = -1;
            g_in.keyEscape = false;
            MenuAction act = menu.frame(ui, menuTex, lastSc, lastW, lastH, min, opt, saves, (float)glfwGetTime());
            if (act == MenuAction::OptionsChanged) applyOptions();
            if (act == MenuAction::CloseOptions) { applyOptions(); openScreen(win, Screen::Paused); }
        }

        // ---- Экран смерти
        if (g_in.screen == Screen::Dead) {
            ui.rect(0, 0, (float)fbw, (float)fbh, {0.5f, 0.05f, 0.05f, 0.45f});
            gtextCenter("Game over!", W / 2, 30, glm::vec4(1), 2.f);
            if (currentWorld.hardcore) gtextCenter("You cannot respawn in hardcore mode!", W / 2, 144);
            gtextCenter("Score: " + std::to_string(player.xpTotal), W / 2, 100, {1, 1, 0.33f, 1});
            if (player.deathTicks >= 20 && currentWorld.hardcore) {
                if (mp) {
                    // Сетевой хардкор: нельзя возродиться — только выйти с сервера
                    if (button(W / 2 - 100, H / 4 + 96, 200, "Disconnect", true)) {
                        g_in.menuClick = false;
                        leaveWorld(true);
                    }
                } else {
                    // Одиночный хардкор: мир удаляется, как в 1.0
                    if (button(W / 2 - 100, H / 4 + 96, 200, "Delete world", true)) {
                        std::string folder = currentWorld.folder;
                        g_in.menuClick = false;
                        leaveWorld(false);
                        saves.remove(folder);
                    }
                }
            } else if (player.deathTicks >= 20) {
                // Возрождение всегда в обычном мире (как в 1.0): из Незера и Края сначала возвращаемся домой
                auto returnHome = [&]() {
                    if (dimension == 0) return false;
                    switchDimension(0);
                    pendingPortal = false;
                    pendingRespawn = true; // к кровати или точке появления, когда мир загрузится
                    player.pos = player.prevPos = spawnPoint + glm::vec3(0, 1, 0);
                    return true;
                };
                if (button(W / 2 - 100, H / 4 + 72, 200, "Respawn", true)) {
                    player.resetStats();
                    player.xpLevel = player.xpTotal = 0;
                    player.xpProgress = 0.f;
                    player.fly = false;
                    if (!returnHome()) {
                        player.pos = player.prevPos = respawnPos();
                        openScreen(win, Screen::Playing);
                    }
                }
                if (button(W / 2 - 100, H / 4 + 96, 200, "Title menu", true)) {
                    player.resetStats();
                    if (!returnHome()) player.pos = player.prevPos = respawnPos();
                    else { loading = false; pendingRespawn = false; }
                    g_in.menuClick = false;
                    leaveWorld(true);
                }
            }
        }
        g_in.menuClick = false;

        ui.flush();
        glfwSwapBuffers(win);
        // «Power saver»: не больше 40 кадров в секунду
        if (opt.performance == 2) {
            static double lastFrameEnd = 0.0;
            double now = glfwGetTime(), wait = 1.0 / 40.0 - (now - lastFrameEnd);
            if (wait > 0.0) std::this_thread::sleep_for(std::chrono::microseconds((int64_t)(wait * 1e6)));
            lastFrameEnd = glfwGetTime();
        }
        if (!inGame) continue;

        // Заголовок окна
        frames++;
        titleTimer += frameDt;
        if (titleTimer >= 0.5) {
            fps = frames / (float)titleTimer;
            frames = 0;
            titleTimer = 0;
            char title[128];
            std::snprintf(title, sizeof(title), "MiniCraft | %.0f FPS", fps);
            glfwSetWindowTitle(win, title);
        }
    }

    if (inGame && !loading) leaveWorld(true);
    opt.save(optionsPath);

    world.reset(); // GL-ресурсы чанков удаляются, пока контекст жив
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
