#include "Model.h"
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace {

const float PI = 3.14159265f;

ModelPart part(glm::vec3 pivot, std::vector<ModelBox> boxes, glm::vec3 rot = glm::vec3(0.f)) {
    ModelPart p;
    p.pivot = pivot;
    p.rot = rot;
    p.boxes = std::move(boxes);
    return p;
}

// Четвероногое (ModelQuadruped): голова, тело, 4 ноги высотой legH
std::vector<ModelPart> quadruped(int legH, const ModelPose& ps) {
    float ls = ps.limbSwing * 0.6662f, la = ps.limbAmount;
    std::vector<ModelPart> m;
    m.push_back(part({0, 18.f - legH, -6}, {{-4, -4, -8, 8, 8, 8, 0, 0}}, {ps.headPitch, ps.headYaw, 0}));
    m.push_back(part({0, 17.f - legH, 2}, {{-5, -10, -7, 10, 16, 8, 28, 8}}, {PI / 2, 0, 0}));
    float y = 24.f - legH;
    ModelBox leg{-2, 0, -2, 4, legH, 4, 0, 16};
    m.push_back(part({-3, y, 7}, {leg}, {std::cos(ls) * 1.4f * la, 0, 0}));
    m.push_back(part({3, y, 7}, {leg}, {std::cos(ls + PI) * 1.4f * la, 0, 0}));
    m.push_back(part({-3, y, -5}, {leg}, {std::cos(ls + PI) * 1.4f * la, 0, 0}));
    m.push_back(part({3, y, -5}, {leg}, {std::cos(ls) * 1.4f * la, 0, 0}));
    return m;
}

// Двуногое (ModelBiped), thin — тонкие руки и ноги скелета
std::vector<ModelPart> biped(const ModelPose& ps, bool thin, bool armsForward) {
    float ls = ps.limbSwing * 0.6662f, la = ps.limbAmount;
    std::vector<ModelPart> m;
    m.push_back(part({0, 0, 0}, {{-4, -8, -4, 8, 8, 8, 0, 0}}, {ps.headPitch, ps.headYaw, 0}));
    m.push_back(part({0, 0, 0}, {{-4, 0, -2, 8, 12, 4, 16, 16}}));
    ModelBox rArm = thin ? ModelBox{-1, -2, -1, 2, 12, 2, 40, 16} : ModelBox{-3, -2, -2, 4, 12, 4, 40, 16};
    ModelBox lArm = thin ? ModelBox{-1, -2, -1, 2, 12, 2, 40, 16} : ModelBox{-1, -2, -2, 4, 12, 4, 40, 16};
    lArm.mirror = true;
    ModelBox rLeg = thin ? ModelBox{-1, 0, -1, 2, 12, 2, 0, 16} : ModelBox{-2, 0, -2, 4, 12, 4, 0, 16};
    ModelBox lLeg = rLeg;
    lLeg.mirror = true;

    float armSwingR = std::cos(ls + PI) * 2.f * la * 0.5f, armSwingL = std::cos(ls) * 2.f * la * 0.5f;
    // Лёгкое «дыхание» рук
    float breatheZ = std::cos(ps.age * 0.09f) * 0.05f + 0.05f, breatheX = std::sin(ps.age * 0.067f) * 0.05f;
    glm::vec3 rRot, lRot;
    if (ps.aimBow) {
        // Прицеливание из лука: обе руки смотрят туда же, куда голова, левая сведена к тетиве
        rRot = {-PI / 2 + ps.headPitch + breatheX, -0.1f + ps.headYaw, breatheZ};
        lRot = {-PI / 2 + ps.headPitch - breatheX, 0.1f + ps.headYaw + 0.4f, -breatheZ};
    } else if (armsForward) {
        // Зомби и скелет держат руки перед собой (ModelZombie); при ударе руки рывком опускаются и возвращаются
        float s1 = std::sin(ps.swing * PI), s2 = std::sin((1.f - (1.f - ps.swing) * (1.f - ps.swing)) * PI);
        float down = s1 * 1.2f - s2 * 0.4f;
        rRot = {-PI / 2 - down + breatheX, -(0.1f - s1 * 0.6f), breatheZ};
        lRot = {-PI / 2 - down - breatheX, 0.1f - s1 * 0.6f, -breatheZ};
    } else {
        rRot = {armSwingR + breatheX, 0.f, breatheZ};
        lRot = {armSwingL - breatheX, 0.f, -breatheZ};
    }
    m.push_back(part({-5, 2, 0}, {rArm}, rRot));
    m.push_back(part({5, 2, 0}, {lArm}, lRot));
    m.push_back(part({-2, 12, 0}, {rLeg}, {std::cos(ls) * 1.4f * la, 0, 0}));
    m.push_back(part({2, 12, 0}, {lLeg}, {std::cos(ls + PI) * 1.4f * la, 0, 0}));
    return m;
}

} // namespace

std::vector<ModelPart> buildMobModel(MobType type, const ModelPose& ps, int layer) {
    float ls = ps.limbSwing * 0.6662f, la = ps.limbAmount;
    switch (type) {
    case MobType::Pig: {
        auto m = quadruped(6, ps);
        m[0].boxes.push_back({-2, 0, -9, 4, 3, 1, 16, 16}); // пятачок
        return m;
    }
    case MobType::Cow: {
        auto m = quadruped(12, ps);
        m[0] = part({0, 4, -8}, {{-4, -4, -6, 8, 8, 6, 0, 0}, {-5, -5, -4, 1, 3, 1, 22, 0}, {4, -5, -4, 1, 3, 1, 22, 0}},
                    {ps.headPitch, ps.headYaw, 0});
        m[1] = part({0, 5, 2}, {{-6, -10, -7, 12, 18, 10, 18, 4}, {-2, 2, -8, 4, 6, 1, 52, 0}}, {PI / 2, 0, 0});
        m[2].pivot = {-4, 12, 7};
        m[3].pivot = {4, 12, 7};
        m[4].pivot = {-4, 12, -6};
        m[5].pivot = {4, 12, -6};
        return m;
    }
    case MobType::Sheep: {
        auto m = quadruped(12, ps);
        if (layer == 0) {
            m[0] = part({0, 6, -8}, {{-3, -4, -6, 6, 6, 8, 0, 0}}, {ps.headPitch, ps.headYaw, 0});
            m[1] = part({0, 5, 2}, {{-4, -10, -7, 8, 16, 6, 28, 8}}, {PI / 2, 0, 0});
        } else {
            // Шерсть (sheep_fur.png): раздутые коробки поверх тела
            m[0] = part({0, 6, -8}, {{-3, -4, -4, 6, 6, 6, 0, 0, 0.6f}}, {ps.headPitch, ps.headYaw, 0});
            m[1] = part({0, 5, 2}, {{-4, -10, -7, 8, 16, 6, 28, 8, 1.75f}}, {PI / 2, 0, 0});
            for (int i = 2; i < 6; ++i) m[i].boxes = {{-2, 0, -2, 4, 6, 4, 0, 16, 0.5f}};
        }
        return m;
    }
    case MobType::Chicken: {
        std::vector<ModelPart> m;
        glm::vec3 headRot{ps.headPitch, ps.headYaw, 0};
        m.push_back(part({0, 15, -4}, {{-2, -6, -2, 4, 6, 3, 0, 0}, {-2, -4, -4, 4, 2, 2, 14, 0}, {-1, -2, -3, 2, 2, 2, 14, 4}}, headRot));
        m.push_back(part({0, 16, 0}, {{-3, -4, -3, 6, 8, 6, 0, 9}}, {PI / 2, 0, 0}));
        m.push_back(part({-2, 19, 1}, {{-1, 0, -3, 3, 5, 3, 26, 0}}, {std::cos(ls) * 1.4f * la, 0, 0}));
        m.push_back(part({1, 19, 1}, {{-1, 0, -3, 3, 5, 3, 26, 0}}, {std::cos(ls + PI) * 1.4f * la, 0, 0}));
        m.push_back(part({-4, 13, 0}, {{0, 0, -3, 1, 4, 6, 24, 13}}, {0, 0, ps.wingFlap}));
        m.push_back(part({4, 13, 0}, {{-1, 0, -3, 1, 4, 6, 24, 13}}, {0, 0, -ps.wingFlap}));
        return m;
    }
    case MobType::Zombie: return biped(ps, false, true);
    case MobType::Skeleton: return biped(ps, true, true);
    case MobType::Spider: {
        std::vector<ModelPart> m;
        m.push_back(part({0, 15, -3}, {{-4, -4, -8, 8, 8, 8, 32, 4}}, {ps.headPitch, ps.headYaw, 0}));
        m.push_back(part({0, 15, 0}, {{-3, -3, -3, 6, 6, 6, 0, 0}}));
        m.push_back(part({0, 15, 9}, {{-5, -4, -6, 10, 8, 12, 0, 12}}));
        // 8 ног: нечётные справа, чётные слева; базовые углы и походка как в ModelSpider
        const float q = PI / 4, e = PI / 8;
        const float baseZ[8] = {-q, q, -q * 0.74f, q * 0.74f, -q * 0.74f, q * 0.74f, -q, q};
        const float baseY[8] = {q, -q, e, -e, -e, e, -q, q};
        const float legZ[8] = {2, 2, 1, 1, 0, 0, -1, -1};
        float l2 = ls * 2.f;
        float fy[4] = {-(std::cos(l2) * 0.4f) * la, -(std::cos(l2 + PI) * 0.4f) * la,
                       -(std::cos(l2 + PI / 2) * 0.4f) * la, -(std::cos(l2 + PI * 1.5f) * 0.4f) * la};
        float fz[4] = {std::abs(std::sin(ls) * 0.4f) * la, std::abs(std::sin(ls + PI) * 0.4f) * la,
                       std::abs(std::sin(ls + PI / 2) * 0.4f) * la, std::abs(std::sin(ls + PI * 1.5f) * 0.4f) * la};
        for (int i = 0; i < 8; ++i) {
            bool right = i % 2 == 0;
            int pair = i / 2;
            float sgn = right ? 1.f : -1.f;
            ModelBox b = right ? ModelBox{-15, -1, -1, 16, 2, 2, 18, 0} : ModelBox{-1, -1, -1, 16, 2, 2, 18, 0};
            m.push_back(part({right ? -4.f : 4.f, 15, legZ[i]}, {b},
                             {0, baseY[i] + sgn * fy[pair], baseZ[i] + sgn * fz[pair]}));
        }
        return m;
    }
    case MobType::Creeper: {
        std::vector<ModelPart> m;
        m.push_back(part({0, 4, 0}, {{-4, -8, -4, 8, 8, 8, 0, 0}}, {ps.headPitch, ps.headYaw, 0}));
        m.push_back(part({0, 4, 0}, {{-4, 0, -2, 8, 12, 4, 16, 16}}));
        ModelBox leg{-2, 0, -2, 4, 6, 4, 0, 16};
        m.push_back(part({-2, 16, 4}, {leg}, {std::cos(ls) * 1.4f * la, 0, 0}));
        m.push_back(part({2, 16, 4}, {leg}, {std::cos(ls + PI) * 1.4f * la, 0, 0}));
        m.push_back(part({-2, 16, -4}, {leg}, {std::cos(ls + PI) * 1.4f * la, 0, 0}));
        m.push_back(part({2, 16, -4}, {leg}, {std::cos(ls) * 1.4f * la, 0, 0}));
        return m;
    }
    case MobType::Wolf: {
        // ModelWolf: голова с ушами и мордой, тело, «грива», 4 лапы, хвост
        std::vector<ModelPart> m;
        glm::vec3 headRot{ps.headPitch, ps.headYaw, 0};
        m.push_back(part({-1, 13.5f, -7}, {{-3, -3, -2, 6, 6, 4, 0, 0}, {-3, -5, 0, 2, 2, 1, 16, 14}, {1, -5, 0, 2, 2, 1, 16, 14},
                                           {-1.5f, 0, -5, 3, 3, 4, 0, 10}}, headRot));
        if (ps.sitting) {
            m.push_back(part({0, 18, 0}, {{-4, -2, -3, 6, 9, 6, 18, 14}}, {PI / 4, 0, 0}));
            m.push_back(part({-1, 16, -3}, {{-4, -3, -3, 8, 6, 7, 21, 0}}, {2 * PI / 5, 0, 0}));
            ModelBox leg{-1, 0, -1, 2, 8, 2, 0, 18};
            m.push_back(part({-2.5f, 22, 2}, {leg}, {3 * PI / 2, 0, 0}));
            m.push_back(part({0.5f, 22, 2}, {leg}, {3 * PI / 2, 0, 0}));
            m.push_back(part({-2.49f, 17, -4}, {leg}, {5.811947f, 0, 0}));
            m.push_back(part({0.51f, 17, -4}, {leg}, {5.811947f, 0, 0}));
            m.push_back(part({-1, 21, 6}, {{-1, 0, -1, 2, 8, 2, 9, 18}}, {ps.tail, 0, 0}));
        } else {
            m.push_back(part({0, 14, 2}, {{-4, -2, -3, 6, 9, 6, 18, 14}}, {PI / 2, 0, 0}));
            m.push_back(part({-1, 14, -3}, {{-4, -3, -3, 8, 6, 7, 21, 0}}, {PI / 2, 0, 0}));
            ModelBox leg{-1, 0, -1, 2, 8, 2, 0, 18};
            m.push_back(part({-2.5f, 16, 7}, {leg}, {std::cos(ls) * 1.4f * la, 0, 0}));
            m.push_back(part({0.5f, 16, 7}, {leg}, {std::cos(ls + PI) * 1.4f * la, 0, 0}));
            m.push_back(part({-2.5f, 16, -4}, {leg}, {std::cos(ls + PI) * 1.4f * la, 0, 0}));
            m.push_back(part({0.5f, 16, -4}, {leg}, {std::cos(ls) * 1.4f * la, 0, 0}));
            m.push_back(part({-1, 12, 8}, {{-1, 0, -1, 2, 8, 2, 9, 18}}, {ps.tail, std::cos(ls) * 0.5f * la, 0}));
        }
        return m;
    }
    case MobType::Squid: {
        // ModelSquid: тело 12x16x12 и 8 щупалец по кругу
        std::vector<ModelPart> m;
        m.push_back(part({0, 8, 0}, {{-6, -8, -6, 12, 16, 12, 0, 0}}));
        for (int i = 0; i < 8; ++i) {
            float a = i * PI * 2.f / 8.f + PI / 2;
            glm::vec3 piv(std::cos(a) * 5.f, 15.f, std::sin(a) * 5.f);
            float rotY = i * PI * -2.f / 8.f + PI / 2;
            m.push_back(part(piv, {{-1, 0, -1, 2, 18, 2, 48, 0}}, {ps.tentacle, rotY, 0}));
        }
        return m;
    }
    case MobType::Slime: case MobType::MagmaCube: {
        // Слизень: внутреннее ядро с глазами (слой 0) и прозрачная оболочка (слой 1); лавовый куб — сегменты
        std::vector<ModelPart> m;
        if (type == MobType::MagmaCube) {
            // ModelMagmaCube: 8 пластин 8x1x8 (при прыжке расходятся) и ядро
            for (int i = 0; i < 8; ++i) {
                int u = 0, v = i;
                if (i == 2) { u = 24; v = 10; }
                else if (i == 3) { u = 24; v = 19; }
                m.push_back(part({0, -(4 - i) * ps.squish * 1.7f, 0}, {{-4, (float)(16 + i), -4, 8, 1, 8, u, v}}));
            }
            m.push_back(part({0, 0, 0}, {{-2, 18, -2, 4, 4, 4, 0, 16}}));
            return m;
        }
        if (layer == 0) {
            m.push_back(part({0, 0, 0}, {{-3, 17, -3, 6, 6, 6, 0, 16}, {-3.25f, 18, -3.5f, 2, 2, 2, 32, 0}, {1.25f, 18, -3.5f, 2, 2, 2, 32, 4},
                                        {0, 21, -3.5f, 1, 1, 1, 32, 8}}));
        } else {
            m.push_back(part({0, 0, 0}, {{-4, 16, -4, 8, 8, 8, 0, 0}}));
        }
        return m;
    }
    case MobType::Enderman: {
        // ModelEnderman: вытянутый двуногий, длинные тонкие руки и ноги, голова с «челюстью»
        std::vector<ModelPart> m;
        glm::vec3 headRot{ps.headPitch, ps.headYaw, 0};
        m.push_back(part({0, -13.f - (ps.angry ? 5.f : 0.f), 0}, {{-4, -8, -4, 8, 8, 8, 0, 0}}, headRot));
        m.push_back(part({0, -13, 0}, {{-4, -8, -4, 8, 8, 8, 0, 16, -0.5f}}, headRot)); // «челюсть»
        m.push_back(part({0, -14, 0}, {{-4, 0, -2, 8, 12, 4, 32, 16}}));
        float armSwing = std::cos(ls) * 2.f * la * 0.5f * 0.4f;
        glm::vec3 rArm{ps.carrying ? -0.5f : armSwing, 0, 0.05f}, lArm{ps.carrying ? -0.5f : -armSwing, 0, -0.05f};
        m.push_back(part({-3, -12, 0}, {{-1, -2, -1, 2, 30, 2, 56, 0}}, rArm));
        m.push_back(part({5, -12, 0}, {{-1, -2, -1, 2, 30, 2, 56, 0, 0.f, true}}, lArm));
        float legSwing = std::cos(ls) * 1.4f * la * 0.5f;
        m.push_back(part({-2, -5, 0}, {{-1, 0, -1, 2, 30, 2, 56, 0}}, {legSwing, 0, 0}));
        m.push_back(part({2, -5, 0}, {{-1, 0, -1, 2, 30, 2, 56, 0, 0.f, true}}, {-legSwing, 0, 0}));
        return m;
    }
    case MobType::Silverfish: {
        // ModelSilverfish: 7 сегментов тела, покачивающихся змейкой
        static const int SZ[7][3] = {{3, 2, 2}, {4, 3, 2}, {6, 4, 3}, {3, 3, 3}, {2, 2, 3}, {2, 1, 2}, {1, 1, 2}};
        static const int UV[7][2] = {{0, 0}, {0, 4}, {0, 9}, {0, 16}, {0, 22}, {11, 0}, {13, 4}};
        std::vector<ModelPart> m;
        float z = -3.5f;
        for (int i = 0; i < 7; ++i) {
            float wob = std::cos(ps.age * 0.9f + i * 0.15f * PI) * PI * 0.05f * (1 + std::abs(i - 2));
            m.push_back(part({std::sin(ps.age * 0.9f + i * 0.15f * PI) * PI * 0.2f * std::abs(i - 2), 24.f - SZ[i][1], z},
                             {{SZ[i][0] * -0.5f, 0, SZ[i][2] * -0.5f, SZ[i][0], SZ[i][1], SZ[i][2], UV[i][0], UV[i][1]}}, {0, wob, 0}));
            if (i < 6) z += (SZ[i][2] + SZ[i + 1][2]) * 0.5f;
        }
        return m;
    }
    case MobType::CaveSpider: return buildMobModel(MobType::Spider, ps, layer);
    case MobType::Mooshroom: return buildMobModel(MobType::Cow, ps, layer);
    case MobType::PigZombie: return biped(ps, false, true);
    case MobType::SnowGolem: {
        // ModelSnowMan: голова, два шара тела, руки-палки
        std::vector<ModelPart> m;
        float sway = std::sin(ps.age * 0.1f) * 0.1f;
        m.push_back(part({0, 4, 0}, {{-4, -8, -4, 8, 8, 8, 0, 0, -0.5f}}, {ps.headPitch, ps.headYaw, 0}));
        m.push_back(part({0, 13, 0}, {{-5, -10, -5, 10, 10, 10, 0, 16, -0.5f}}));
        m.push_back(part({0, 24, 0}, {{-6, -12, -6, 12, 12, 12, 0, 36, -0.5f}}));
        m.push_back(part({-5, 6, 1}, {{-1, 0, -1, 12, 2, 2, 32, 0, -0.5f}}, {0, sway, 1.f}));
        m.push_back(part({5, 6, -1}, {{-1, 0, -1, 12, 2, 2, 32, 0, -0.5f}}, {0, PI + sway, -1.f}));
        return m;
    }
    case MobType::Villager: {
        // ModelVillager: голова с носом, тело в мантии, скрещённые руки, ноги
        std::vector<ModelPart> m;
        m.push_back(part({0, 0, 0}, {{-4, -10, -4, 8, 10, 8, 0, 0}, {-1, -3, -6, 2, 4, 2, 24, 0}}, {ps.headPitch, ps.headYaw, 0}));
        m.push_back(part({0, 0, 0}, {{-4, 0, -3, 8, 12, 6, 16, 20}, {-4, 0, -3, 8, 18, 6, 0, 38, 0.5f}}));
        m.push_back(part({0, 3, -1}, {{-8, -2, -2, 4, 8, 4, 44, 22}, {4, -2, -2, 4, 8, 4, 44, 22}, {-4, 2, -2, 8, 4, 4, 40, 38}},
                         {-0.75f, 0, 0}));
        m.push_back(part({-2, 12, 0}, {{-2, 0, -2, 4, 12, 4, 0, 22}}, {std::cos(ls) * 1.4f * la * 0.5f, 0, 0}));
        m.push_back(part({2, 12, 0}, {{-2, 0, -2, 4, 12, 4, 0, 22, 0.f, true}}, {std::cos(ls + PI) * 1.4f * la * 0.5f, 0, 0}));
        return m;
    }
    case MobType::Ghast: {
        // ModelGhast: куб 16x16x16 и 9 шевелящихся щупалец
        std::vector<ModelPart> m;
        m.push_back(part({0, 8, 0}, {{-8, -8, -8, 16, 16, 16, 0, 0}}));
        uint32_t seed = 1660;
        for (int i = 0; i < 9; ++i) {
            seed = seed * 1103515245u + 12345u;
            int len = (int)((seed >> 16) % 7) + 8;
            float x = ((i % 3) - (i / 3 % 2) * 0.5f + 0.25f) / 2.f * 2.f - 1.f, z = ((float)(i / 3) / 2.f * 2.f - 1.f);
            float wig = 0.2f * std::sin(ps.age * 0.3f + i) + 0.4f;
            m.push_back(part({x * 5.f, 15.f, z * 5.f}, {{-1, 0, -1, 2, (int)len, 2, 0, 0}}, {wig, 0, 0}));
        }
        return m;
    }
    case MobType::Blaze: {
        // ModelBlaze: голова 8x8x8 и 12 стержней, кружащихся в три яруса
        std::vector<ModelPart> m;
        m.push_back(part({0, 0, 0}, {{-4, -4, -4, 8, 8, 8, 0, 0}}, {ps.headPitch, ps.headYaw, 0}));
        float a = ps.age * PI * -0.1f;
        for (int i = 0; i < 4; ++i, a += PI / 2)
            m.push_back(part({std::cos(a) * 9.f, -2.f + std::cos((i * 2 + ps.age) * 0.25f), std::sin(a) * 9.f}, {{0, 0, 0, 2, 8, 2, 0, 16}}));
        a = PI / 4 + ps.age * PI * 0.03f;
        for (int i = 4; i < 8; ++i, a += PI / 2)
            m.push_back(part({std::cos(a) * 7.f, 2.f + std::cos((i * 2 + ps.age) * 0.25f), std::sin(a) * 7.f}, {{0, 0, 0, 2, 8, 2, 0, 16}}));
        a = 0.47123894f + ps.age * PI * -0.05f;
        for (int i = 8; i < 12; ++i, a += PI / 2)
            m.push_back(part({std::cos(a) * 5.f, 11.f + std::cos((i * 1.5f + ps.age) * 0.5f), std::sin(a) * 5.f}, {{0, 0, 0, 2, 8, 2, 0, 16}}));
        return m;
    }
    case MobType::WitherSkeleton: return biped(ps, true, false);
    case MobType::ZombieVillager: {
        auto m = biped(ps, false, true);
        m[0] = part({0, 0, 0}, {{-4, -10, -4, 8, 10, 8, 0, 32}, {-1, -3, -6, 2, 4, 2, 24, 32}}, {ps.headPitch, ps.headYaw, 0});
        return m;
    }
    case MobType::Witch: {
        std::vector<ModelPart> m;
        m.push_back(part({0, 0, 0}, {
            {-4, -10, -4, 8, 10, 8, 0, 0},
            {-1, -3, -6, 2, 4, 2, 24, 0},
            {0, -2, -6.75f, 1, 1, 1, 0, 0, -0.25f},
            {-5, -10.05f, -5, 10, 2, 10, 0, 64},
            {-3.5f, -14.05f, -3.5f, 7, 4, 7, 0, 76},
            {-2, -18.05f, -2, 4, 4, 4, 0, 87},
            {-1, -20.05f, -1, 2, 2, 2, 0, 95}
        }, {ps.headPitch, ps.headYaw, 0}));
        m.push_back(part({0, 0, 0}, {{-4, 0, -3, 8, 12, 6, 16, 20}, {-4, 0, -3, 8, 18, 6, 0, 38, 0.5f}}));
        m.push_back(part({0, 3, -1}, {{-8, -2, -2, 4, 8, 4, 44, 22}, {4, -2, -2, 4, 8, 4, 44, 22}, {-4, 2, -2, 8, 4, 4, 40, 38}},
                         {-0.75f, 0, 0}));
        m.push_back(part({-2, 12, 0}, {{-2, 0, -2, 4, 12, 4, 0, 22}}, {std::cos(ls) * 1.4f * la * 0.5f, 0, 0}));
        m.push_back(part({2, 12, 0}, {{-2, 0, -2, 4, 12, 4, 0, 22, 0.f, true}}, {std::cos(ls + PI) * 1.4f * la * 0.5f, 0, 0}));
        return m;
    }
    case MobType::Bat: {
        std::vector<ModelPart> m;
        m.push_back(part({0, 17, 0}, {{-3, -3, -3, 6, 6, 6, 0, 0}, {-4, -6, -2, 3, 4, 1, 24, 0}, {1, -6, -2, 3, 4, 1, 24, 0}},
                         {ps.headPitch, ps.headYaw, 0}));
        m.push_back(part({0, 17, 0}, {{-5, 0, -3, 10, 16, 6, 0, 34}}));
        float flap = ps.wingFlap;
        m.push_back(part({0, 17, 0}, {{-12, 1, 1.5f, 10, 16, 1, 42, 0}}, {0.f, 0.f, flap}));
        ModelBox lWing{2, 1, 1.5f, 10, 16, 1, 42, 0};
        lWing.mirror = true;
        m.push_back(part({0, 17, 0}, {lWing}, {0.f, 0.f, -flap}));
        m.push_back(part({0, 17, 0}, {{-5, 16, 0, 10, 6, 1, 0, 16}}));
        return m;
    }
    case MobType::IronGolem: {
        std::vector<ModelPart> m;
        m.push_back(part({0, -7, -2}, {{-4, -12, -5.5f, 8, 10, 8, 0, 0}, {-1, -5, -7.5f, 2, 4, 2, 24, 0}}, {ps.headPitch, ps.headYaw, 0}));
        m.push_back(part({0, -7, 0}, {{-9, -2, -6, 18, 12, 11, 0, 40}, {-4.5f, 10, -3, 9, 5, 6, 0, 70, 0.5f}}));
        float rArmSwing = (ps.swing > 0.f) ? (-2.f + 1.5f * std::sin(ps.swing * PI)) : (std::cos(ls + PI) * 0.6f * la);
        float lArmSwing = std::cos(ls) * 0.6f * la;
        m.push_back(part({0, -7, 0}, {{-13, -2.5f, -3, 4, 30, 6, 60, 21}}, {rArmSwing, 0, 0}));
        m.push_back(part({0, -7, 0}, {{9, -2.5f, -3, 4, 30, 6, 60, 58}}, {lArmSwing, 0, 0}));
        m.push_back(part({-4, 11, 0}, {{-3.5f, -3, -3, 6, 16, 5, 37, 0}}, {std::cos(ls) * 1.f * la, 0, 0}));
        m.push_back(part({5, 11, 0}, {{-3.5f, -3, -3, 6, 16, 5, 60, 0}}, {std::cos(ls + PI) * 1.f * la, 0, 0}));
        return m;
    }
    case MobType::Ocelot: case MobType::Cat: {
        std::vector<ModelPart> m;
        m.push_back(part({0, 15, -9}, {
            {-2.5f, -2, -3, 5, 4, 5, 0, 0},
            {-1.5f, 0, -4, 3, 2, 2, 0, 24},
            {-2, -3, 0, 1, 1, 2, 0, 10},
            {1, -3, 0, 1, 1, 2, 0, 10}
        }, {ps.headPitch, ps.headYaw, 0}));
        if (ps.sitting) {
            m.push_back(part({0, 16, -6}, {{-2, 3, -8, 4, 16, 6, 20, 0}}, {PI / 4.f, 0, 0}));
            m.push_back(part({-1.1f, 21, 5}, {{-1, 0, 1, 2, 5, 2, 8, 13}}, {-PI / 2.f, 0, 0}));
            m.push_back(part({1.1f, 21, 5}, {{-1, 0, 1, 2, 5, 2, 8, 13}}, {-PI / 2.f, 0, 0}));
            m.push_back(part({-1.2f, 15.5f, -4}, {{-1, 0, 0, 2, 9, 2, 8, 13}}, {0, 0, 0}));
            m.push_back(part({1.2f, 15.5f, -4}, {{-1, 0, 0, 2, 9, 2, 8, 13}}, {0, 0, 0}));
            m.push_back(part({0, 21, 5}, {{-0.5f, 0, 0, 1, 8, 1, 0, 15}}, {PI / 2.5f, 0, 0}));
            m.push_back(part({0, 22, 10}, {{-0.5f, 0, 0, 1, 8, 1, 4, 15}}, {PI / 3.f, 0, 0}));
        } else {
            m.push_back(part({0, 12, -10}, {{-2, 3, -8, 4, 16, 6, 20, 0}}, {PI / 2.f, 0, 0}));
            m.push_back(part({-1.1f, 18, 5}, {{-1, 0, 1, 2, 10, 2, 8, 13}}, {std::cos(ls) * 1.f * la, 0, 0}));
            m.push_back(part({1.1f, 18, 5}, {{-1, 0, 1, 2, 10, 2, 8, 13}}, {std::cos(ls + PI) * 1.f * la, 0, 0}));
            m.push_back(part({-1.2f, 14.1f, -5}, {{-1, 0, 0, 2, 10, 2, 8, 13}}, {std::cos(ls + PI) * 1.f * la, 0, 0}));
            m.push_back(part({1.2f, 14.1f, -5}, {{-1, 0, 0, 2, 10, 2, 8, 13}}, {std::cos(ls) * 1.f * la, 0, 0}));
            float tailWiggle = std::sin(ps.age * 0.2f) * 0.15f;
            m.push_back(part({0, 15, 8}, {{-0.5f, 0, 0, 1, 8, 1, 0, 15}}, {PI / 2.f + 0.3f, tailWiggle, 0}));
            m.push_back(part({0, 20, 14}, {{-0.5f, 0, 0, 1, 8, 1, 4, 15}}, {PI / 2.f + 0.5f, tailWiggle * 1.5f, 0}));
        }
        return m;
    }
    case MobType::Wither: {
        std::vector<ModelPart> m;
        m.push_back(part({0, 0, 0}, {{-4, -4, -4, 8, 8, 8, 0, 0}}, {ps.headPitch, ps.headYaw, 0}));
        m.push_back(part({-8, 4, 0}, {{-3, -3, -3, 6, 6, 6, 32, 0}}, {ps.headPitch * 0.8f, ps.headYaw * 0.8f, 0}));
        m.push_back(part({8, 4, 0}, {{-3, -3, -3, 6, 6, 6, 32, 0}}, {ps.headPitch * 0.8f, ps.headYaw * 0.8f, 0}));
        m.push_back(part({0, 0, 0}, {{-10, 3.9f, -1.5f, 20, 3, 3, 0, 16}}));
        float sway = std::sin(ps.age * 0.1f) * 0.1f;
        m.push_back(part({0, 6.9f, 0}, {
            {-1.5f, 0, -1.5f, 3, 10, 3, 0, 22},
            {-4, 1.5f, -0.5f, 8, 2, 2, 24, 22},
            {-4, 4.0f, -0.5f, 8, 2, 2, 24, 22},
            {-4, 6.5f, -0.5f, 8, 2, 2, 24, 22}
        }, {sway * 0.5f, 0, 0}));
        m.push_back(part({0, 16.9f, 0}, {{-1.5f, 0, -1.5f, 3, 6, 3, 12, 22}}, {sway, 0, 0}));
        return m;
    }
    default: return {};
    }
}

namespace {
glm::mat4 partMatrix(const ModelPart& p) {
    glm::mat4 m = glm::translate(p.pre, p.pivot);
    m = glm::rotate(m, p.rot.z, glm::vec3(0, 0, 1));
    m = glm::rotate(m, p.rot.y, glm::vec3(0, 1, 0));
    m = glm::rotate(m, p.rot.x, glm::vec3(1, 0, 0));
    return m;
}
ModelPart child(const ModelPart& parent, glm::vec3 pivot, std::vector<ModelBox> boxes, glm::vec3 rot) {
    ModelPart c = part(pivot, std::move(boxes), rot);
    c.pre = partMatrix(parent);
    return c;
}
ModelBox mirrorX(ModelBox b) {
    b.x = -b.x - b.w;
    b.mirror = !b.mirror;
    return b;
}
} // namespace

std::vector<ModelPart> buildDragonModel(float f, float age, float jaw) {
    std::vector<ModelPart> m;
    const float PI2 = 6.2831853f;
    // Тело
    m.push_back(part({0, 4, 8}, {{-12, 0, -16, 24, 24, 64, 0, 0}, {-1, -6, -10, 2, 6, 12, 220, 53}, {-1, -6, 10, 2, 6, 12, 220, 53},
                                {-1, -6, 30, 2, 6, 12, 220, 53}}));
    // Шея: 5 позвонков вперёд от груди, с волной
    float z = -8.f, y = 14.f;
    for (int i = 0; i < 5; ++i) {
        float wy = std::sin(age * 0.08f + i * 0.6f) * 1.5f;
        m.push_back(part({0, y + wy, z - 5}, {{-5, -5, -5, 10, 10, 10, 192, 104}, {-1, -9, -3, 2, 4, 6, 48, 0}}));
        z -= 10.f;
        y -= 1.f;
    }
    // Голова и челюсть
    ModelPart head = part({0, y - 2, z}, {{-6, -1, -24, 12, 5, 16, 176, 44}, {-8, -8, -10, 16, 16, 16, 112, 30},
                                          {-5, -12, -4, 2, 4, 6, 220, 53}, {3, -12, -4, 2, 4, 6, 220, 53},
                                          {-5, -3, -22, 2, 2, 4, 112, 0}, {3, -3, -22, 2, 2, 4, 112, 0}});
    m.push_back(head);
    m.push_back(child(head, {0, 4, -8}, {{-6, 0, -16, 12, 4, 16, 176, 65}}, {jaw, 0, 0}));
    // Хвост: 12 позвонков назад, изгибается
    z = 64.f;
    y = 10.f;
    for (int i = 0; i < 12; ++i) {
        float wx = std::sin(age * 0.05f + i * 0.45f) * (1.f + i * 0.4f);
        float wy = std::cos(age * 0.07f + i * 0.5f) * 0.8f;
        m.push_back(part({wx, y + wy, z + 5}, {{-5, -5, -5, 10, 10, 10, 192, 104}, {-1, -9, -3, 2, 4, 6, 48, 0}}));
        z += 10.f;
        y += 0.5f;
    }
    // Крылья: кость с перепонкой и концевая часть (ModelDragon: углы от фазы взмаха)
    float fp = f * PI2;
    for (int side = 0; side < 2; ++side) {
        float s = side == 0 ? 1.f : -1.f;
        glm::vec3 rot{0.125f - std::cos(fp) * 0.2f, 0.25f * s, (std::sin(fp) + 0.125f) * 0.8f * s};
        std::vector<ModelBox> wb = {{-56, -4, -4, 56, 8, 8, 112, 88}, {-56, 0, 2, 56, 0, 56, -56, 88}};
        std::vector<ModelBox> tb = {{-56, -2, -2, 56, 4, 4, 112, 136}, {-56, 0, 2, 56, 0, 56, -56, 144}};
        if (side == 1) { for (auto& b : wb) b = mirrorX(b); for (auto& b : tb) b = mirrorX(b); }
        ModelPart wing = part({-12.f * s, 5, 2}, wb, rot);
        m.push_back(wing);
        m.push_back(child(wing, {-56.f * s, 0, 0}, tb, {0, 0, -(std::sin(fp + 2.f) + 0.5f) * 0.75f * s}));
        // Передняя лапа: плечо, предплечье, стопа
        float leg = 1.3f + std::sin(fp) * 0.1f;
        std::vector<ModelBox> fl = {{-4, -4, -4, 8, 24, 8, 112, 104}};
        std::vector<ModelBox> ft = {{-3, -1, -3, 6, 24, 6, 226, 138}};
        std::vector<ModelBox> ff = {{-4, 0, -12, 8, 4, 16, 144, 104}};
        ModelPart front = part({-12.f * s, 20, 2}, fl, {leg, 0, 0});
        ModelPart fTip = child(front, {0, 20, -1}, ft, {-0.5f - std::sin(fp) * 0.1f, 0, 0});
        m.push_back(front);
        m.push_back(fTip);
        m.push_back(child(fTip, {0, 23, 0}, ff, {0.75f + std::sin(fp) * 0.1f, 0, 0}));
        // Задняя лапа
        std::vector<ModelBox> rl = {{-8, -4, -8, 16, 32, 16, 0, 0}};
        std::vector<ModelBox> rt = {{-6, -2, 0, 12, 32, 12, 196, 0}};
        std::vector<ModelBox> rf = {{-9, 0, -20, 18, 6, 24, 112, 0}};
        ModelPart rear = part({-16.f * s, 16, 42}, rl, {1.f + std::sin(fp) * 0.1f, 0, 0});
        ModelPart rTip = child(rear, {0, 32, -4}, rt, {0.5f + std::sin(fp) * 0.1f, 0, 0});
        m.push_back(rear);
        m.push_back(rTip);
        m.push_back(child(rTip, {0, 31, 4}, rf, {0.75f + std::sin(fp) * 0.1f, 0, 0}));
    }
    return m;
}

std::vector<ModelPart> buildCrystalModel(float age) {
    // RenderEnderCrystal: поворот age*3 градуса, подъём ядра f = (s*s + s) * 0.2, где s = sin(age*0.2)/2 + 0.5
    float spin = glm::radians(age * 3.f);
    float s = std::sin(age * 0.2f) / 2.f + 0.5f;
    float bob = (s * s + s) * 0.2f;
    const glm::vec3 diag = glm::normalize(glm::vec3(1.f, 0.f, 1.f));
    const float tilt = glm::radians(60.f), px = 1.f / 16.f;
    std::vector<ModelPart> m;
    glm::mat4 root = glm::scale(glm::mat4(1.f), glm::vec3(2.f));
    root = glm::translate(root, glm::vec3(0.f, -0.5f, 0.f));
    auto add = [&](const glm::mat4& mat, ModelBox box) {
        ModelPart p;
        p.pre = glm::scale(mat, glm::vec3(px));
        p.boxes = {box};
        m.push_back(p);
    };
    add(root, {-6, 0, -6, 12, 4, 12, 0, 16}); // основание
    glm::mat4 g = glm::rotate(root, spin, glm::vec3(0, 1, 0));
    g = glm::translate(g, glm::vec3(0.f, 0.8f + bob, 0.f));
    g = glm::rotate(g, tilt, diag);
    add(g, {-4, -4, -4, 8, 8, 8, 0, 0}); // внешнее стекло
    g = glm::scale(g, glm::vec3(0.875f));
    g = glm::rotate(g, tilt, diag);
    g = glm::rotate(g, spin, glm::vec3(0, 1, 0));
    add(g, {-4, -4, -4, 8, 8, 8, 0, 0}); // внутреннее стекло
    g = glm::scale(g, glm::vec3(0.875f));
    g = glm::rotate(g, tilt, diag);
    g = glm::rotate(g, spin, glm::vec3(0, 1, 0));
    add(g, {-4, -4, -4, 8, 8, 8, 32, 0}); // ядро
    return m;
}

std::vector<ModelPart> buildBookModel(float tick, float flipL, float flipR, float spread) {
    float open = (std::sin(tick * 0.02f) * 0.1f + 1.25f) * spread;
    float px = std::sin(open);
    std::vector<ModelPart> m;
    m.push_back(part({0, 0, -1}, {{-6, -5, 0, 6, 10, 0, 0, 0}}, {0, PI + open, 0}));      // обложка справа
    m.push_back(part({0, 0, 1}, {{0, -5, 0, 6, 10, 0, 16, 0}}, {0, -open, 0}));           // обложка слева
    m.push_back(part({px, 0, 0}, {{0, -4, -0.99f, 5, 8, 1, 0, 10}}, {0, open, 0}));      // страницы справа
    m.push_back(part({px, 0, 0}, {{0, -4, -0.01f, 5, 8, 1, 12, 10}}, {0, -open, 0}));    // страницы слева
    m.push_back(part({px, 0, 0}, {{0, -4, 0, 5, 8, 0, 24, 10}}, {0, open - open * 2.f * flipL, 0})); // листаемые
    m.push_back(part({px, 0, 0}, {{0, -4, 0, 5, 8, 0, 24, 10}}, {0, open - open * 2.f * flipR, 0}));
    m.push_back(part({0, 0, 0}, {{-1, -5, 0, 2, 10, 0, 12, 0}}, {0, PI / 2, 0}));          // корешок
    return m;
}

std::vector<ModelPart> buildChestModel(bool large, float lid) {
    int w = large ? 30 : 14;
    glm::vec3 r{-lid * PI / 2, 0, 0};
    std::vector<ModelPart> m;
    m.push_back(part({1, 7, 15}, {{0, -5, -14, w, 5, 14, 0, 0}}, r));                   // крышка
    m.push_back(part({large ? 16.f : 8.f, 7, 15}, {{-1, -2, -15, 2, 4, 1, 0, 0}}, r));  // замок
    m.push_back(part({1, 6, 1}, {{0, 0, 0, w, 10, 14, 0, 19}}));                         // низ
    return m;
}

std::vector<ModelPart> buildMinecartModel() {
    std::vector<ModelPart> m;
    m.push_back(part({0, 4, 0}, {{-10, -8, -1, 20, 16, 2, 0, 10}}, {PI / 2, 0, 0}));   // дно
    ModelBox side{-8, -9, -1, 16, 8, 2, 0, 0};
    m.push_back(part({-9, 4, 0}, {side}, {0, PI * 3 / 2, 0}));
    m.push_back(part({9, 4, 0}, {side}, {0, PI / 2, 0}));
    m.push_back(part({0, 4, -7}, {side}, {0, PI, 0}));
    m.push_back(part({0, 4, 7}, {side}, {0, 0, 0}));
    return m;
}

std::vector<ModelPart> buildBoatModel() {
    std::vector<ModelPart> m;
    m.push_back(part({0, 4, 0}, {{-12, -8, -3, 24, 16, 4, 0, 8}}, {PI / 2, 0, 0}));    // днище
    ModelBox side{-10, -7, -1, 20, 6, 2, 0, 0};
    m.push_back(part({-11, 4, 0}, {side}, {0, PI * 3 / 2, 0}));
    m.push_back(part({11, 4, 0}, {side}, {0, PI / 2, 0}));
    m.push_back(part({0, 4, -9}, {side}, {0, PI, 0}));
    m.push_back(part({0, 4, 9}, {side}, {0, 0, 0}));
    return m;
}

std::vector<ModelPart> buildPlayerModel(const PlayerPose& ps, float inf) {
    float ls = ps.limbSwing * 0.6662f, la = ps.limbAmount;
    glm::vec3 headR{ps.headPitch, ps.headYaw, 0}, bodyR{0, 0, 0};
    glm::vec3 rArm{std::cos(ls + PI) * 2.f * la * 0.5f, 0, 0}, lArm{std::cos(ls) * 2.f * la * 0.5f, 0, 0};
    glm::vec3 rLeg{std::cos(ls) * 1.4f * la, 0, 0}, lLeg{std::cos(ls + PI) * 1.4f * la, 0, 0};
    glm::vec3 rArmP{-5, 2, 0}, lArmP{5, 2, 0}, rLegP{-2, 12, 0}, lLegP{2, 12, 0}, headP{0, 0, 0};
    if (ps.riding) {
        rArm.x += -PI / 5; lArm.x += -PI / 5;
        rLeg = {-PI * 2 / 5, PI / 10, 0}; lLeg = {-PI * 2 / 5, -PI / 10, 0};
    }
    if (ps.holding) rArm.x = rArm.x * 0.5f - PI / 10;
    if (ps.swing > 0.f) {
        // Взмах рукой (onGround): тело слегка поворачивается, правая рука описывает дугу
        float s = ps.swing;
        bodyR.y = std::sin(std::sqrt(s) * PI * 2) * 0.2f;
        rArmP = {-std::cos(bodyR.y) * 5, 2, std::sin(bodyR.y) * 5};
        lArmP = {std::cos(bodyR.y) * 5, 2, -std::sin(bodyR.y) * 5};
        rArm.y += bodyR.y; lArm.y += bodyR.y; lArm.x += bodyR.y;
        float f = 1.f - s; f *= f; f *= f; f = 1.f - f;
        float a = std::sin(f * PI), b = std::sin(s * PI) * -(headR.x - 0.7f) * 0.75f;
        rArm.x -= a * 1.2f + b;
        rArm.y += bodyR.y * 2.f;
        rArm.z = std::sin(s * PI) * -0.4f;
    }
    if (ps.sneak) {
        bodyR.x = 0.5f;
        rArm.x += 0.4f; lArm.x += 0.4f;
        rLegP = {-2, 9, 4}; lLegP = {2, 9, 4};
        headP.y = 1;
    }
    rArm.z += std::cos(ps.age * 0.09f) * 0.05f + 0.05f;
    lArm.z -= std::cos(ps.age * 0.09f) * 0.05f + 0.05f;
    rArm.x += std::sin(ps.age * 0.067f) * 0.05f;
    lArm.x -= std::sin(ps.age * 0.067f) * 0.05f;
    ModelBox lArmB{-1, -2, -2, 4, 12, 4, 40, 16, inf}, lLegB{-2, 0, -2, 4, 12, 4, 0, 16, inf};
    lArmB.mirror = true;
    lLegB.mirror = true;
    std::vector<ModelPart> m;
    m.push_back(part(headP, {{-4, -8, -4, 8, 8, 8, 0, 0, inf}}, headR));
    m.push_back(part(headP, {{-4, -8, -4, 8, 8, 8, 32, 0, inf + 0.5f}}, headR));
    m.push_back(part({0, ps.sneak ? 0.f : 0.f, 0}, {{-4, 0, -2, 8, 12, 4, 16, 16, inf}}, bodyR));
    m.push_back(part(rArmP, {{-3, -2, -2, 4, 12, 4, 40, 16, inf}}, rArm));
    m.push_back(part(lArmP, {lArmB}, lArm));
    m.push_back(part(rLegP, {{-2, 0, -2, 4, 12, 4, 0, 16, inf}}, rLeg));
    m.push_back(part(lLegP, {lLegB}, lLeg));
    return m;
}

std::vector<ModelPart> buildPlayerArm() {
    return {part({-5, 2, 0}, {{-3, -2, -2, 4, 12, 4, 40, 16}})};
}

glm::mat4 entityMatrix(const glm::vec3& feet, float yawDeg, float deathAngleDeg, glm::vec3 scale) {
    // Лицо модели смотрит в -Z; поворачиваем так, чтобы -Z совпал с направлением взгляда (cos yaw, sin yaw)
    glm::mat4 m = glm::translate(glm::mat4(1.f), feet);
    m = glm::rotate(m, glm::radians(-(yawDeg + 90.f)), glm::vec3(0, 1, 0));
    if (deathAngleDeg != 0.f) m = glm::rotate(m, glm::radians(deathAngleDeg), glm::vec3(0, 0, 1));
    m = glm::scale(m, scale);
    // Как в RenderLiving: зеркалим X и Y, модель стоит «на» y = 24 пикселя
    m = glm::scale(m, glm::vec3(-1.f / 16.f, -1.f / 16.f, 1.f / 16.f));
    m = glm::translate(m, glm::vec3(0.f, -24.f, 0.f));
    return m;
}

void emitModel(std::vector<Vertex>& out, const std::vector<ModelPart>& parts, const glm::mat4& root,
               float sky, float block, float texW, float texH) {
    // Два направленных источника сверху (как освещение сущностей в 1.0) + рассеянный свет
    const glm::vec3 L1 = glm::normalize(glm::vec3(0.2f, 1.f, -0.7f)), L2 = glm::normalize(glm::vec3(-0.2f, 1.f, 0.7f));
    for (const ModelPart& p : parts) {
        glm::mat4 m = glm::translate(root * p.pre, p.pivot);
        m = glm::rotate(m, p.rot.z, glm::vec3(0, 0, 1));
        m = glm::rotate(m, p.rot.y, glm::vec3(0, 1, 0));
        m = glm::rotate(m, p.rot.x, glm::vec3(1, 0, 0));
        for (const ModelBox& b : p.boxes) {
            float x0 = b.x - b.inflate, y0 = b.y - b.inflate, z0 = b.z - b.inflate;
            float x1 = b.x + b.w + b.inflate, y1 = b.y + b.h + b.inflate, z1 = b.z + b.d + b.inflate;
            if (b.mirror) std::swap(x0, x1);
            glm::vec3 c[8] = {{x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0},
                              {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}};
            glm::vec3 w[8];
            for (int i = 0; i < 8; ++i) w[i] = glm::vec3(m * glm::vec4(c[i], 1.f));
            glm::vec3 center(0.f);
            for (auto& v : w) center += v * 0.125f;

            // Развёртка ModelBox: {вершины}, (u1, v1, u2, v2)
            const float u = (float)b.u, v = (float)b.v, W = (float)b.w, H = (float)b.h, D = (float)b.d;
            struct Quad { int i[4]; float u1, v1, u2, v2; };
            const Quad quads[6] = {
                {{5, 1, 2, 6}, u + D + W, v + D, u + D + W + D, v + D + H},     // +X
                {{0, 4, 7, 3}, u, v + D, u + D, v + D + H},                     // -X
                {{5, 4, 0, 1}, u + D, v, u + D + W, v + D},                     // верх
                {{2, 3, 7, 6}, u + D + W, v + D, u + D + W + W, v},             // низ
                {{1, 0, 3, 2}, u + D, v + D, u + D + W, v + D + H},             // перед
                {{4, 5, 6, 7}, u + D + W + D, v + D, u + D + W + D + W, v + D + H}, // зад
            };
            for (const Quad& q : quads) {
                glm::vec3 a = w[q.i[0]], bb = w[q.i[1]], cc = w[q.i[2]], d = w[q.i[3]];
                glm::vec3 n = glm::cross(bb - a, cc - a);
                float len = glm::length(n);
                if (len < 1e-8f) continue;
                n /= len;
                if (glm::dot(n, (a + cc) * 0.5f - center) < 0) n = -n; // нормаль наружу
                float shade = 0.4f + 0.6f * std::max({0.f, glm::dot(n, L1), glm::dot(n, L2)});
                // Порядок UV как у TexturedQuad: (u2,v1) (u1,v1) (u1,v2) (u2,v2)
                Vertex va{a.x, a.y, a.z, q.u2 / texW, q.v1 / texH, shade, sky, block};
                Vertex vb{bb.x, bb.y, bb.z, q.u1 / texW, q.v1 / texH, shade, sky, block};
                Vertex vc{cc.x, cc.y, cc.z, q.u1 / texW, q.v2 / texH, shade, sky, block};
                Vertex vd{d.x, d.y, d.z, q.u2 / texW, q.v2 / texH, shade, sky, block};
                out.insert(out.end(), {va, vb, vc, va, vc, vd});
            }
        }
    }
}
