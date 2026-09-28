#pragma once
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "Blocks.h"

// Звук на miniaudio + stb_vorbis.
// Файлы группируются по имени без цифр: sounds/step/grass1..6.ogg -> группа "step/grass".
class Audio {
public:
    Audio();
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    bool init(const std::string& soundsDir);
    bool ok() const { return impl_ != nullptr; }

    // Случайный звук из группы. pos == nullptr — без позиционирования (звук «у игрока»)
    void play(const std::string& group, float volume = 1.f, float pitch = 1.f, const glm::vec3* pos = nullptr);

    void playStep(uint8_t block);                      // шаг по блоку
    void playDig(uint8_t block, const glm::vec3& pos); // блок сломан / поставлен
    void playHit(uint8_t block, const glm::vec3& pos); // удар по блоку во время копания

    void setListener(const glm::vec3& pos, const glm::vec3& forward);
    // Пластинка в проигрывателе (одна одновременно), слышна в радиусе ~64 блоков
    void playRecord(const std::string& name, const glm::vec3& pos);
    void stopRecord();
    void setMusicVolume(float v); // меняет и текущий трек
    // Фоновая музыка и эмбиент; underground — игрок в пещере
    void update(float dt, bool underground);

    float musicVolume = 0.35f;
    float sfxVolume = 1.0f;

private:
    struct Impl;
    Impl* impl_ = nullptr;
    std::string root_;
    std::unordered_map<std::string, std::vector<std::string>> groups_;
    float musicTimer_ = 0.f;
    float caveTimer_ = 0.f;

    const std::vector<std::string>& group(const std::string& name);
    void cleanup();
};
