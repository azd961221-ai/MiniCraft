#pragma once
// 2D-интерфейс: спрайты из разных текстур, шрифт font.png, иконки блоков.
// Рисование накапливается в сегменты (текстура + режим смешивания) и выводится в flush().
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <string>
#include <vector>
#include "Blocks.h"
#include "Image.h"

class UI {
public:
    enum class Blend { Alpha, Invert, Add };

    GLuint whiteTex = 0;
    GLuint terrainTex = 0;
    float scale = 2.f; // масштаб GUI (как «GUI Scale» в Minecraft)

    void init(GLuint program) {
        prog_ = program;
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(V), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(V), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(V), (void*)(4 * sizeof(float)));

        uint8_t white[4] = {255, 255, 255, 255};
        glGenTextures(1, &whiteTex);
        glBindTexture(GL_TEXTURE_2D, whiteTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }

    // Ширина символов считается по непрозрачным пикселям, как в оригинале
    void setFont(GLuint tex, const Image& img) {
        fontTex_ = tex;
        for (int c = 0; c < 256; ++c) {
            int cx = (c % 16) * 8, cy = (c / 16) * 8, w = 0;
            for (int x = 7; x >= 0 && !w; --x)
                for (int y = 0; y < 8; ++y)
                    if (img.at(cx + x, cy + y)[3] > 0) { w = x + 1; break; }
            glyphW_[c] = c == ' ' ? 4 : (w ? w : 4);
        }
    }

    void begin(int w, int h) { width_ = w; height_ = h; segs_.clear(); }

    // Преобразование координат следующих примитивов (поворот/масштаб надписи)
    glm::mat3 xform{1.f};

    void quad(GLuint tex, const glm::vec2 p[4], const glm::vec2 uv[4], glm::vec4 col, Blend blend = Blend::Alpha) {
        glm::vec4 cols[4] = {col, col, col, col};
        quad4(tex, p, uv, cols, blend);
    }

    // Четырёхугольник с цветом в каждой вершине (TL, TR, BR, BL)
    void quad4(GLuint tex, const glm::vec2 p[4], const glm::vec2 uv[4], const glm::vec4 col[4], Blend blend = Blend::Alpha) {
        auto& v = seg(tex, blend);
        static const int idx[6] = {0, 1, 2, 0, 2, 3};
        for (int i : idx) {
            glm::vec3 q = xform * glm::vec3(p[i], 1.f);
            v.push_back({q.x, q.y, uv[i].x, uv[i].y, col[i].r, col[i].g, col[i].b, col[i].a});
        }
    }

    // Вертикальный градиент (drawGradientRect)
    void gradient(float x0, float y0, float x1, float y1, glm::vec4 top, glm::vec4 bottom) {
        glm::vec2 p[4] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
        glm::vec2 uv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        glm::vec4 c[4] = {top, top, bottom, bottom};
        quad4(whiteTex, p, uv, c);
    }

    // Прямоугольник из текстуры; u/v в пикселях текстуры размером texW x texH
    void image(GLuint tex, float x, float y, float w, float h, float u, float v, float uw, float vh,
               float texW, float texH, glm::vec4 col = glm::vec4(1), Blend blend = Blend::Alpha) {
        glm::vec2 p[4] = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
        float u0 = u / texW, u1 = (u + uw) / texW, v0 = v / texH, v1 = (v + vh) / texH;
        glm::vec2 uv[4] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
        quad(tex, p, uv, col, blend);
    }

    void rect(float x0, float y0, float x1, float y1, glm::vec4 col) {
        image(whiteTex, x0, y0, x1 - x0, y1 - y0, 0, 0, 1, 1, 1, 1, col);
    }

    // Изометрическая иконка блока (растения — плоским спрайтом). cx, cy — центр, s — размер
    void blockIcon(float cx, float cy, float s, uint8_t b, uint8_t meta = 2) {
        if (b == AIR) return;
        if (isPlant(b)) {
            tile(blockTex(b, 4, meta), cx - s / 2, cy - s / 2, s, s, tintFor(blockTex(b, 4, meta)));
            return;
        }
        // Изометрия: видны верх (+Y), грань -Z слева и грань +X справа; коробки — от дальних к ближним
        float r = s * 0.5f, h = r * 0.866f, k = r * 0.5f;
        auto P = [&](float X, float Y, float Z) {
            return glm::vec2(cx + h * (X - 1.f + Z), cy - k * (1.f - X) - k * Z + r * (1.f - Y));
        };
        std::vector<std::pair<glm::vec3, glm::vec3>> boxes;
        itemModelBoxes(b, boxes);
        std::sort(boxes.begin(), boxes.end(), [](const auto& a, const auto& c) {
            auto key = [](const auto& q) { glm::vec3 m = (q.first + q.second) * 0.5f; return m.x - m.z + m.y; };
            return key(a) < key(c);
        });
        for (const auto& [mn, mx] : boxes) {
            // Верх: u = X, v = Z
            glm::vec2 top[4] = {P(mn.x, mx.y, mn.z), P(mn.x, mx.y, mx.z), P(mx.x, mx.y, mx.z), P(mx.x, mx.y, mn.z)};
            glm::vec2 tuv[4] = {{mn.x, mn.z}, {mn.x, mx.z}, {mx.x, mx.z}, {mx.x, mn.z}};
            faceUV(top, tuv, blockTex(b, 2, meta), 1.0f);
            // Грань -Z: u = X, v = 1 - Y
            glm::vec2 lf[4] = {P(mn.x, mx.y, mn.z), P(mx.x, mx.y, mn.z), P(mx.x, mn.y, mn.z), P(mn.x, mn.y, mn.z)};
            glm::vec2 luv[4] = {{mn.x, 1 - mx.y}, {mx.x, 1 - mx.y}, {mx.x, 1 - mn.y}, {mn.x, 1 - mn.y}};
            faceUV(lf, luv, blockTex(b, 5, meta), 0.8f);
            // Грань +X: u = Z, v = 1 - Y
            glm::vec2 rt[4] = {P(mx.x, mx.y, mn.z), P(mx.x, mx.y, mx.z), P(mx.x, mn.y, mx.z), P(mx.x, mn.y, mn.z)};
            glm::vec2 ruv[4] = {{mn.z, 1 - mx.y}, {mx.z, 1 - mx.y}, {mx.z, 1 - mn.y}, {mn.z, 1 - mn.y}};
            faceUV(rt, ruv, blockTex(b, 0, meta), 0.6f);
        }
    }

    int glyphWidth(unsigned char c) const { return glyphW_[c]; }

    float textWidth(const std::string& s, float sc) const {
        float w = 0;
        for (unsigned char c : s) w += (glyphW_[c] + 1) * sc;
        return w;
    }

    // Текст с тенью; sc — размер пикселя шрифта
    float text(const std::string& s, float x, float y, float sc, glm::vec4 col = glm::vec4(1), bool shadow = true) {
        if (shadow) glyphs(s, x + sc, y + sc, sc, glm::vec4(glm::vec3(col) * 0.25f, col.a));
        return glyphs(s, x, y, sc, col);
    }

    void flush() {
        glUseProgram(prog_);
        glm::mat4 proj = glm::ortho(0.f, (float)width_, (float)height_, 0.f);
        glUniformMatrix4fv(glGetUniformLocation(prog_, "uProj"), 1, GL_FALSE, glm::value_ptr(proj));
        glUniform1i(glGetUniformLocation(prog_, "uTex"), 0);
        glActiveTexture(GL_TEXTURE0);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glEnable(GL_BLEND);
        for (auto& s : segs_) {
            if (s.verts.empty()) continue;
            if (s.blend == Blend::Invert) glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA);
            else if (s.blend == Blend::Add) glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            else glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glBindTexture(GL_TEXTURE_2D, s.tex);
            glBufferData(GL_ARRAY_BUFFER, s.verts.size() * sizeof(V), s.verts.data(), GL_STREAM_DRAW);
            glDrawArrays(GL_TRIANGLES, 0, (GLsizei)s.verts.size());
        }
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    // Нарисовать накопленное и очистить очередь (для участков с обрезкой glScissor)
    void flushNow() {
        flush();
        segs_.clear();
    }

private:
    struct V { float x, y, u, v, r, g, b, a; };
    struct Segment { GLuint tex; Blend blend; std::vector<V> verts; };

    GLuint prog_ = 0, vao_ = 0, vbo_ = 0, fontTex_ = 0;
    int width_ = 0, height_ = 0;
    int glyphW_[256] = {};
    std::vector<Segment> segs_;

    std::vector<V>& seg(GLuint tex, Blend blend) {
        if (segs_.empty() || segs_.back().tex != tex || segs_.back().blend != blend) segs_.push_back({tex, blend, {}});
        return segs_.back().verts;
    }

    static glm::vec4 tintFor(int) { return glm::vec4(1); } // подкраска уже «запечена» в атлас

    void tile(int t, float x, float y, float w, float h, glm::vec4 col) {
        image(terrainTex, x, y, w, h, (t % 16) * 16.f + 0.01f, (t / 16) * 16.f + 0.01f, 15.98f, 15.98f, 256, 256, col);
    }

    // Грань иконки: p = TL, TR, BR, BL
    void face(const glm::vec2 p[4], int t, float shade) {
        const float ts = 1.f / 16.f, e = 0.0005f;
        float u0 = (t % 16) * ts + e, u1 = (t % 16 + 1) * ts - e;
        float v0 = (t / 16) * ts + e, v1 = (t / 16 + 1) * ts - e;
        glm::vec2 uv[4] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
        quad(terrainTex, p, uv, glm::vec4(shade, shade, shade, 1));
    }

    // Грань с долями тайла uvf (0..1) в каждой вершине
    void faceUV(const glm::vec2 p[4], const glm::vec2 uvf[4], int t, float shade) {
        const float ts = 1.f / 16.f, e = 0.0005f;
        glm::vec2 uv[4];
        for (int i = 0; i < 4; ++i)
            uv[i] = glm::vec2((t % 16) * ts + e + glm::clamp(uvf[i].x, 0.f, 1.f) * (ts - 2 * e),
                              (t / 16) * ts + e + glm::clamp(uvf[i].y, 0.f, 1.f) * (ts - 2 * e));
        quad(terrainTex, p, uv, glm::vec4(shade, shade, shade, 1));
    }

    float glyphs(const std::string& s, float x, float y, float sc, glm::vec4 col) {
        for (unsigned char c : s) {
            if (c != ' ') image(fontTex_, x, y, 8 * sc, 8 * sc, (c % 16) * 8.f, (c / 16) * 8.f, 8, 8, 128, 128, col);
            x += (glyphW_[c] + 1) * sc;
        }
        return x;
    }
};
