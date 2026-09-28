#pragma once
// Меню как в Minecraft 1.0: титульный экран, выбор и создание мира, удаление, настройки, загрузка.
#include <functional>
#include <string>
#include <vector>
#include "Options.h"
#include "Saves.h"
#include "UI.h"

struct MenuInput {
    float mx = 0, my = 0;       // мышь в единицах GUI
    bool click = false;         // левая кнопка нажата в этом кадре
    bool doubleClick = false;
    bool mouseDown = false;     // левая кнопка удерживается
    float scroll = 0.f;
    std::string typed;          // введённые символы (UTF-8)
    bool backspace = false, enter = false, escape = false, tab = false;
    int keyPressed = -1;        // код клавиши, нажатой в этом кадре (для назначения клавиш)
};

struct MenuTextures {
    GLuint gui = 0, background = 0, logo = 0;
    float logoW = 274, logoH = 44;
};

enum class MenuAction { None, PlayWorld, CreateWorld, Quit, CloseOptions, OptionsChanged, JoinServer };

struct NewWorldRequest {
    std::string name, folder, seedText;
    int mode = 0; // 0 выживание, 1 хардкор, 2 творческий
    bool cheats = false;
};

class MenuSystem {
public:
    enum class Page { None, Title, SelectWorld, CreateWorld, ConfirmDelete, Options, Video, Controls, Loading, Multiplayer, Disconnected };
    std::string serverField, nameFieldMp;   // «Multiplayer»: адрес и имя
    std::string disconnectTitle, disconnectReason; // экран ошибки подключения / отключения
    int waitingKey = -1;            // «Controls»: какую клавишу ждём (-1 — никакую)
    Page page = Page::Title;
    bool fromGame = false;          // настройки открыты из меню паузы
    std::string selectedFolder;     // для PlayWorld
    NewWorldRequest newWorld;       // для CreateWorld
    std::string loadingTitle = "Loading level", loadingStage = "Building terrain";
    float loadingProgress = 0.f;
    std::string splash;
    std::function<void()> clickSound;

    void open(Page p, SaveManager& saves);
    // Рисует страницу и обрабатывает ввод. Фон (панорама) рисует игра до вызова.
    MenuAction frame(UI& ui, const MenuTextures& tex, float sc, float W, float H, const MenuInput& in, Options& opt,
                     SaveManager& saves, float time);

private:
    std::vector<WorldInfo> worlds_;
    int selected_ = -1;
    float listScroll_ = 0.f;
    std::string nameField_ = "New World", seedField_;
    int focus_ = 0;        // 0 — имя, 1 — сид
    int mode_ = 0;
    bool cheats_ = false, cheatsTouched_ = false; // «Allow Cheats»: по умолчанию — только в творческом
    bool moreOptions_ = false;
    int dragSlider_ = -1;
};

// Фон «земля» для экранов меню (drawBackground)
void drawDirtBackground(UI& ui, GLuint tex, float sc, float W, float H, float scrollV = 0.f);
