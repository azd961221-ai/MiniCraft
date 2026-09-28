#include "Audio.h"

// stb_vorbis подключается дважды: сначала заголовок (для miniaudio), потом реализация
#define STB_VORBIS_HEADER_ONLY
#include "extras/stb_vorbis.c"
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#undef STB_VORBIS_HEADER_ONLY
#include "extras/stb_vorbis.c"

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <list>
#include <random>

namespace fs = std::filesystem;

struct Audio::Impl {
    ma_engine engine;
    std::list<ma_sound> sounds; // одноразовые звуки; удаляются после окончания
    ma_sound music;
    bool musicLoaded = false;
    ma_sound record;
    bool recordLoaded = false;
    std::mt19937 rng{std::random_device{}()};
};

Audio::Audio() = default;

Audio::~Audio() {
    if (!impl_) return;
    for (auto& s : impl_->sounds) ma_sound_uninit(&s);
    if (impl_->musicLoaded) ma_sound_uninit(&impl_->music);
    if (impl_->recordLoaded) ma_sound_uninit(&impl_->record);
    ma_engine_uninit(&impl_->engine);
    delete impl_;
}

bool Audio::init(const std::string& soundsDir) {
    root_ = soundsDir;
    auto* impl = new Impl();
    if (ma_engine_init(nullptr, &impl->engine) != MA_SUCCESS) {
        std::fprintf(stderr, "Audio: failed to init device\n");
        delete impl;
        return false;
    }
    impl_ = impl;
    std::uniform_real_distribution<float> d(20.f, 60.f);
    musicTimer_ = d(impl_->rng);
    caveTimer_ = 30.f;
    return true;
}

const std::vector<std::string>& Audio::group(const std::string& name) {
    auto it = groups_.find(name);
    if (it != groups_.end()) return it->second;

    std::vector<std::string> files;
    fs::path p = fs::u8path(root_) / fs::u8path(name);
    fs::path dir = p.parent_path();
    std::string base = p.filename().u8string();
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file() || e.path().extension() != ".ogg") continue;
        std::string stem = e.path().stem().u8string();
        while (!stem.empty() && std::isdigit((unsigned char)stem.back())) stem.pop_back();
        if (stem == base) files.push_back(e.path().u8string());
    }
    return groups_.emplace(name, std::move(files)).first->second;
}

void Audio::cleanup() {
    for (auto it = impl_->sounds.begin(); it != impl_->sounds.end();) {
        if (ma_sound_at_end(&*it)) {
            ma_sound_uninit(&*it);
            it = impl_->sounds.erase(it);
        } else {
            ++it;
        }
    }
}

void Audio::play(const std::string& name, float volume, float pitch, const glm::vec3* pos) {
    if (!impl_) return;
    const auto& files = group(name);
    if (files.empty()) return;
    if (impl_->sounds.size() > 48) return; // защита от лавины звуков

    std::uniform_int_distribution<size_t> pick(0, files.size() - 1);
    const std::string& file = files[pick(impl_->rng)];

    impl_->sounds.emplace_back();
    ma_sound* s = &impl_->sounds.back();
    // DECODE — декодировать целиком; менеджер ресурсов кэширует повторные загрузки
    if (ma_sound_init_from_file(&impl_->engine, file.c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr, s) != MA_SUCCESS) {
        impl_->sounds.pop_back();
        return;
    }
    ma_sound_set_volume(s, volume * sfxVolume);
    ma_sound_set_pitch(s, pitch);
    if (pos) {
        ma_sound_set_position(s, pos->x, pos->y, pos->z);
        ma_sound_set_min_distance(s, 2.f);
        ma_sound_set_max_distance(s, 24.f);
        ma_sound_set_attenuation_model(s, ma_attenuation_model_linear);
    } else {
        ma_sound_set_spatialization_enabled(s, MA_FALSE);
    }
    ma_sound_start(s);
}

static const char* soundName(Sound s) {
    switch (s) {
    case Sound::Stone: return "stone";
    case Sound::Grass: return "grass";
    case Sound::Gravel: return "gravel";
    case Sound::Sand: return "sand";
    case Sound::Wood: return "wood";
    case Sound::Snow: return "snow";
    case Sound::Cloth: return "cloth";
    case Sound::Glass: return "stone";
    default: return nullptr;
    }
}

void Audio::playStep(uint8_t block) {
    const char* n = soundName(blockInfo(block).sound);
    if (n) play(std::string("step/") + n, 0.25f, 1.0f);
}

void Audio::playDig(uint8_t block, const glm::vec3& pos) {
    Sound s = blockInfo(block).sound;
    if (s == Sound::Glass) { play("random/glass", 0.9f, 1.0f, &pos); return; }
    const char* n = soundName(s);
    if (n) play(std::string("dig/") + n, 0.9f, 0.85f, &pos);
}

void Audio::playHit(uint8_t block, const glm::vec3& pos) {
    const char* n = soundName(blockInfo(block).sound);
    if (n) play(std::string("step/") + n, 0.25f, 0.55f, &pos);
}

void Audio::playRecord(const std::string& name, const glm::vec3& pos) {
    if (!impl_) return;
    stopRecord();
    std::string file = (fs::u8path(root_) / "records" / fs::u8path(name + ".ogg")).u8string();
    if (ma_sound_init_from_file(&impl_->engine, file.c_str(), MA_SOUND_FLAG_STREAM, nullptr, nullptr, &impl_->record) != MA_SUCCESS)
        return;
    impl_->recordLoaded = true;
    ma_sound_set_position(&impl_->record, pos.x, pos.y, pos.z);
    ma_sound_set_min_distance(&impl_->record, 4.f);
    ma_sound_set_max_distance(&impl_->record, 64.f);
    ma_sound_set_attenuation_model(&impl_->record, ma_attenuation_model_linear);
    ma_sound_set_volume(&impl_->record, musicVolume / 0.35f * 0.8f + 0.2f);
    ma_sound_start(&impl_->record);
}

void Audio::stopRecord() {
    if (!impl_ || !impl_->recordLoaded) return;
    ma_sound_uninit(&impl_->record);
    impl_->recordLoaded = false;
}

void Audio::setMusicVolume(float v) {
    musicVolume = v * 0.35f;
    if (impl_ && impl_->musicLoaded) ma_sound_set_volume(&impl_->music, musicVolume);
}

void Audio::setListener(const glm::vec3& pos, const glm::vec3& fwd) {
    if (!impl_) return;
    ma_engine_listener_set_position(&impl_->engine, 0, pos.x, pos.y, pos.z);
    ma_engine_listener_set_direction(&impl_->engine, 0, fwd.x, fwd.y, fwd.z);
}

void Audio::update(float dt, bool underground) {
    if (!impl_) return;
    cleanup();

    // Музыка: трек, затем пауза 1–4 минуты
    if (impl_->musicLoaded && ma_sound_at_end(&impl_->music)) {
        ma_sound_uninit(&impl_->music);
        impl_->musicLoaded = false;
        musicTimer_ = std::uniform_real_distribution<float>(60.f, 240.f)(impl_->rng);
    }
    if (!impl_->musicLoaded) {
        musicTimer_ -= dt;
        if (musicTimer_ <= 0.f) {
            const auto& tracks = [&]() -> const std::vector<std::string>& {
                // Все треки music/game/*.ogg (calm, hal, nuance, piano)
                static std::vector<std::string> all;
                if (all.empty()) {
                    std::error_code ec;
                    for (auto& e : fs::directory_iterator(fs::u8path(root_) / "music" / "game", ec))
                        if (e.is_regular_file() && e.path().extension() == ".ogg") all.push_back(e.path().u8string());
                    if (all.empty()) {
                        for (auto& e : fs::directory_iterator(fs::u8path(root_) / "music", ec))
                            if (e.is_regular_file() && e.path().extension() == ".ogg") all.push_back(e.path().u8string());
                    }
                }
                return all;
            }();
            if (!tracks.empty()) {
                const std::string& t = tracks[std::uniform_int_distribution<size_t>(0, tracks.size() - 1)(impl_->rng)];
                if (ma_sound_init_from_file(&impl_->engine, t.c_str(), MA_SOUND_FLAG_STREAM, nullptr, nullptr,
                                            &impl_->music) == MA_SUCCESS) {
                    impl_->musicLoaded = true;
                    ma_sound_set_spatialization_enabled(&impl_->music, MA_FALSE);
                    ma_sound_set_volume(&impl_->music, musicVolume);
                    ma_sound_start(&impl_->music);
                }
            }
            musicTimer_ = 120.f;
        }
    }

    // Звуки пещер
    if (underground) {
        caveTimer_ -= dt;
        if (caveTimer_ <= 0.f) {
            play("ambient/cave/cave", 0.5f);
            caveTimer_ = std::uniform_real_distribution<float>(40.f, 120.f)(impl_->rng);
        }
    }
}
