#pragma once
// Миры в папке saves/: у каждого своя папка с world.sav, entities.sav и level.txt (описание).
#include <cstdint>
#include <string>
#include <vector>

struct WorldInfo {
    std::string folder;     // имя папки
    std::string name;       // имя мира
    uint32_t seed = 0;
    int gameMode = 0;       // 0 выживание, 1 творческий
    bool hardcore = false;
    int64_t lastPlayed = 0; // секунды с 1970
    int dimension = 0;      // где игрок: 0 — обычный мир, -1 — Незер, 1 — Край
    uint32_t achievements = 0; // полученные достижения (биты)
    float cartDistance = 0.f;  // пройдено в вагонетке (для «В путь по рельсам»)
    int generator = 1;         // генератор мира: 1 — прежний (миры без этого поля), 2 — как в 1.0, 3 — + родники, большие дубы,
                               // 4 — + какао и деревья джунглей 1.4.2
    bool cheats = true;        // разрешены команды и отладочные клавиши (старые миры — да)
};

class SaveManager {
public:
    std::string root; // с завершающим '/'

    std::vector<WorldInfo> list() const;         // по дате, новые первыми
    std::string uniqueFolder(const std::string& name) const;
    std::string path(const std::string& folder) const { return root + folder + "/"; }
    bool readInfo(const std::string& folder, WorldInfo& out) const;
    bool writeInfo(const WorldInfo& w) const;
    bool remove(const std::string& folder) const;
};

// Открыть файл по пути в UTF-8 (std::fopen понимает только ANSI — русские имена папок и миров не открывались)
#include <cstdio>
FILE* openFileUtf8(const std::string& path, const char* mode);
// Надёжная запись: пишем в path + ".tmp", затем прежний файл становится path + ".bak", а новый — path.
// Если запись оборвётся, останется либо старый файл, либо его копия .bak
bool commitFile(const std::string& tmpPath, const std::string& path);
bool fileExistsUtf8(const std::string& path);
// Строка без битых последовательностей UTF-8 (fs::u8path в MSVC бросает исключение на неверном UTF-8)
std::string validUtf8(const std::string& s);

// Сид из строки: число как есть, иначе хэш строки (как String.hashCode в Java)
uint32_t seedFromText(const std::string& text, uint32_t fallback);
int64_t nowSeconds();
