#include "Menu.h"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <glm/gtc/matrix_transform.hpp>

void drawDirtBackground(UI& ui, GLuint tex, float sc, float W, float H, float scrollV) {
    // Плитка 16x16, растянутая до 32 единиц GUI, затемнённая до 0x404040
    glm::vec2 p[4] = {{0, 0}, {W * sc, 0}, {W * sc, H * sc}, {0, H * sc}};
    float u = W / 32.f, v = H / 32.f;
    glm::vec2 uv[4] = {{0, scrollV}, {u, scrollV}, {u, v + scrollV}, {0, v + scrollV}};
    ui.quad(tex, p, uv, glm::vec4(0.25f, 0.25f, 0.25f, 1.f));
}

void MenuSystem::open(Page p, SaveManager& saves) {
    page = p;
    dragSlider_ = -1;
    if (p == Page::SelectWorld) {
        worlds_ = saves.list();
        selected_ = -1;
        listScroll_ = 0.f;
    }
    if (p == Page::CreateWorld) {
        nameField_ = "New World";
        seedField_.clear();
        mode_ = 0;
        cheats_ = cheatsTouched_ = false;
        focus_ = 0;
        moreOptions_ = false;
    }
}

MenuAction MenuSystem::frame(UI& ui, const MenuTextures& tex, float sc, float W, float H, const MenuInput& in, Options& opt,
                             SaveManager& saves, float time) {
    const glm::vec4 WHITE(1.f), GRAY(0.63f, 0.63f, 0.63f, 1.f), YELLOW(1.f, 1.f, 0.f, 1.f);
    MenuAction action = MenuAction::None;
    auto text = [&](const std::string& s, float x, float y, glm::vec4 c = glm::vec4(1.f)) { ui.text(s, x * sc, y * sc, sc, c); };
    auto centered = [&](const std::string& s, float x, float y, glm::vec4 c = glm::vec4(1.f)) {
        ui.text(s, x * sc - ui.textWidth(s, sc) / 2, y * sc, sc, c);
    };
    auto img = [&](GLuint t, float x, float y, float u, float v, float w, float h, float tw = 256, float th = 256) {
        ui.image(t, x * sc, y * sc, w * sc, h * sc, u, v, w, h, tw, th);
    };
    auto hover = [&](float x, float y, float w, float h) { return in.mx >= x && in.mx < x + w && in.my >= y && in.my < y + h; };
    // Кнопка из gui.png (200x20): выключенная v=46, обычная v=66, под курсором v=86
    auto button = [&](float x, float y, float w, const std::string& label, bool enabled = true) {
        bool hv = enabled && hover(x, y, w, 20);
        float v = !enabled ? 46.f : hv ? 86.f : 66.f;
        img(tex.gui, x, y, 0, v, w / 2, 20);
        img(tex.gui, x + w / 2, y, 200 - w / 2, v, w / 2, 20);
        centered(label, x + w / 2, y + 6, !enabled ? GRAY : hv ? glm::vec4(1, 1, 0.63f, 1) : glm::vec4(0.88f, 0.88f, 0.88f, 1));
        bool pressed = hv && in.click;
        if (pressed && clickSound) clickSound();
        return pressed;
    };
    // Ползунок (GuiSlider): фон — выключенная кнопка, бегунок 8 пикселей
    auto slider = [&](int id, float x, float y, float w, float& value, const std::string& label) {
        img(tex.gui, x, y, 0, 46, w / 2, 20);
        img(tex.gui, x + w / 2, y, 200 - w / 2, 46, w / 2, 20);
        if (in.click && hover(x, y, w, 20)) { dragSlider_ = id; if (clickSound) clickSound(); }
        if (dragSlider_ == id) {
            if (in.mouseDown) value = std::clamp((in.mx - (x + 4)) / (w - 8), 0.f, 1.f);
            else dragSlider_ = -1;
        }
        float kx = x + value * (w - 8);
        img(tex.gui, kx, y, 0, 66, 4, 20);
        img(tex.gui, kx + 4, y, 196, 66, 4, 20);
        centered(label, x + w / 2, y + 6, hover(x, y, w, 20) ? glm::vec4(1, 1, 0.63f, 1) : glm::vec4(0.88f, 0.88f, 0.88f, 1));
        return dragSlider_ == id;
    };
    // Текстовое поле (GuiTextField)
    auto textField = [&](int id, float x, float y, float w, std::string& value) {
        ui.rect((x - 1) * sc, (y - 1) * sc, (x + w + 1) * sc, (y + 21) * sc, {0.63f, 0.63f, 0.63f, 1});
        ui.rect(x * sc, y * sc, (x + w) * sc, (y + 20) * sc, {0, 0, 0, 1});
        if (in.click && hover(x, y, w, 20)) focus_ = id;
        bool focused = focus_ == id;
        if (focused) {
            for (char c : in.typed)
                if ((unsigned char)c >= 32 && value.size() < 32) value += c;
            if (in.backspace && !value.empty()) {
                // Удаляем целиком последний символ UTF-8
                size_t i = value.size() - 1;
                while (i > 0 && ((unsigned char)value[i] & 0xC0) == 0x80) --i;
                value.erase(i);
            }
        }
        std::string shown = value + (focused && ((int)(time * 20) / 6) % 2 == 0 ? "_" : "");
        text(shown, x + 4, y + 6, {0.88f, 0.88f, 0.88f, 1});
    };
    auto pct = [](float v) { return std::to_string((int)(v * 100.f + 0.5f)) + "%"; };

    switch (page) {
    // ------------------------------------------------ Титульный экран (GuiMainMenu)
    case Page::Title: {
        img(tex.logo, W / 2 - tex.logoW / 2, 30, 0, 0, tex.logoW, tex.logoH, tex.logoW, tex.logoH);
        // Жёлтая фраза под наклоном, пульсирует
        float f = 1.8f - std::abs(std::sin(std::fmod(time, 1.f) * glm::two_pi<float>()) * 0.1f);
        f = f * 100.f / (ui.textWidth(splash, 1.f) + 32.f);
        glm::mat3 m(1.f);
        float ang = glm::radians(-20.f), cs = std::cos(ang), sn = std::sin(ang);
        glm::mat3 T(1.f), R(1.f), S(1.f);
        T[2] = glm::vec3((W / 2 + 90) * sc, 70 * sc, 1.f);
        R[0] = glm::vec3(cs, sn, 0);
        R[1] = glm::vec3(-sn, cs, 0);
        S[0][0] = f; S[1][1] = f;
        ui.xform = T * R * S;
        ui.text(splash, -ui.textWidth(splash, sc) / 2, -8 * sc, sc, YELLOW);
        ui.xform = glm::mat3(1.f);

        float y = H / 4 + 48;
        if (button(W / 2 - 100, y, 200, "Singleplayer")) open(Page::SelectWorld, saves);
        if (button(W / 2 - 100, y + 24, 200, "Multiplayer")) {
            serverField = opt.lastServer;
            nameFieldMp = opt.playerName;
            focus_ = 0;
            page = Page::Multiplayer;
        }
        button(W / 2 - 100, y + 48, 200, "Texture Packs", false);
        if (button(W / 2 - 100, y + 84, 98, "Options...")) { fromGame = false; open(Page::Options, saves); }
        if (button(W / 2 + 2, y + 84, 98, "Quit Game")) action = MenuAction::Quit;
        text("MiniCraft (Minecraft 1.0 port)", 2, H - 10, {0.5f, 0.5f, 0.5f, 1});
        std::string r = "Fan remake, not by Mojang";
        text(r, W - ui.textWidth(r, sc) / sc - 2, H - 10, {0.5f, 0.5f, 0.5f, 1});
        break;
    }

    // ------------------------------------------------ Выбор мира (GuiSelectWorld)
    case Page::SelectWorld: {
        drawDirtBackground(ui, tex.background, sc, W, H);
        centered("Select World", W / 2, 16);
        float top = 32, bottom = H - 64, rowH = 36;
        ui.rect(0, top * sc, W * sc, bottom * sc, {0, 0, 0, 0.35f});
        float maxScroll = std::max(0.f, worlds_.size() * rowH - (bottom - top - 4));
        listScroll_ = std::clamp(listScroll_ - in.scroll * rowH, 0.f, maxScroll);
        for (int i = 0; i < (int)worlds_.size(); ++i) {
            float y = top + 4 + i * rowH - listScroll_;
            if (y + rowH < top || y > bottom - 4) continue;
            float x = W / 2 - 110;
            const WorldInfo& w = worlds_[i];
            bool hv = hover(x - 2, y - 2, 224, rowH - 2) && in.my > top && in.my < bottom;
            if (hv && in.click) {
                if (selected_ == i && in.doubleClick) { selectedFolder = w.folder; action = MenuAction::PlayWorld; }
                selected_ = i;
            }
            if (selected_ == i) {
                ui.rect((x - 2) * sc, (y - 2) * sc, (x + 222) * sc, (y + rowH - 4) * sc, {0.5f, 0.5f, 0.5f, 1});
                ui.rect((x - 1) * sc, (y - 1) * sc, (x + 221) * sc, (y + rowH - 5) * sc, {0, 0, 0, 1});
            }
            char date[64] = "";
            std::time_t t = (std::time_t)w.lastPlayed;
            if (std::tm* tm = std::localtime(&t)) std::strftime(date, sizeof(date), "%d.%m.%y %H:%M", tm);
            text(w.name, x + 2, y + 1, WHITE);
            text(w.folder + " (" + date + ")", x + 2, y + 12, {0.5f, 0.5f, 0.5f, 1});
            std::string mode = w.hardcore ? "Hardcore Mode!" : w.gameMode == 1 ? "Creative Mode" : "Survival Mode";
            if (w.cheats && !w.hardcore) mode += ", Cheats";
            text(mode, x + 2, y + 22, w.hardcore ? glm::vec4(1, 0.33f, 0.33f, 1) : glm::vec4(0.5f, 0.5f, 0.5f, 1));
        }
        bool has = selected_ >= 0 && selected_ < (int)worlds_.size();
        if (button(W / 2 - 154, H - 52, 150, "Play Selected World", has)) { selectedFolder = worlds_[selected_].folder; action = MenuAction::PlayWorld; }
        if (button(W / 2 + 4, H - 52, 150, "Create New World")) open(Page::CreateWorld, saves);
        button(W / 2 - 154, H - 28, 72, "Rename", false);
        if (button(W / 2 - 76, H - 28, 72, "Delete", has)) { selectedFolder = worlds_[selected_].folder; page = Page::ConfirmDelete; }
        if (button(W / 2 + 4, H - 28, 150, "Cancel") || in.escape) open(Page::Title, saves);
        if (has && in.enter) { selectedFolder = worlds_[selected_].folder; action = MenuAction::PlayWorld; }
        break;
    }

    // ------------------------------------------------ Удаление мира
    case Page::ConfirmDelete: {
        drawDirtBackground(ui, tex.background, sc, W, H);
        std::string name = selectedFolder;
        for (auto& w : worlds_) if (w.folder == selectedFolder) name = w.name;
        centered("Are you sure you want to delete this world?", W / 2, 70);
        centered("'" + name + "' will be lost forever! (A long time!)", W / 2, 90, GRAY);
        if (button(W / 2 - 155, H / 6 + 96, 150, "Delete")) {
            saves.remove(selectedFolder);
            open(Page::SelectWorld, saves);
        }
        if (button(W / 2 + 5, H / 6 + 96, 150, "Cancel") || in.escape) page = Page::SelectWorld;
        break;
    }

    // ------------------------------------------------ Создание мира (GuiCreateWorld)
    case Page::CreateWorld: {
        drawDirtBackground(ui, tex.background, sc, W, H);
        centered("Create New World", W / 2, 20);
        if (in.tab) focus_ = moreOptions_ ? 1 : 0;
        if (!moreOptions_) {
            text("World Name", W / 2 - 100, 47, GRAY);
            textField(0, W / 2 - 100, 60, 200, nameField_);
            std::string folder = saves.uniqueFolder(nameField_);
            text("Will be saved in: " + folder, W / 2 - 100, 85, GRAY);
            static const char* modes[3] = {"Survival", "Hardcore", "Creative"};
            static const char* desc1[3] = {"Search for resources, crafting, gain", "Same as survival mode, locked at hardest",
                                           "Unlimited resources, free flying and"};
            static const char* desc2[3] = {"levels, health and hunger", "difficulty, and one life only", "destroy blocks instantly"};
            if (button(W / 2 - 75, 100, 150, std::string("Game Mode: ") + modes[mode_])) {
                mode_ = (mode_ + 1) % 3;
                if (!cheatsTouched_) cheats_ = mode_ == 2;
            }
            centered(desc1[mode_], W / 2, 122, GRAY);
            centered(desc2[mode_], W / 2, 134, GRAY);
            if (button(W / 2 - 75, 172, 150, "More World Options...")) { moreOptions_ = true; focus_ = 1; }
        } else {
            text("Seed for the World Generator", W / 2 - 100, 47, GRAY);
            textField(1, W / 2 - 100, 60, 200, seedField_);
            text("Leave blank for a random seed", W / 2 - 100, 85, GRAY);
            bool hc = mode_ == 1;
            if (button(W / 2 - 75, 110, 150, std::string("Allow Cheats: ") + (cheats_ && !hc ? "ON" : "OFF"), !hc)) {
                cheats_ = !cheats_;
                cheatsTouched_ = true;
            }
            centered("Commands like /give, /time, /gamemode", W / 2, 134, GRAY);
            if (button(W / 2 - 75, 172, 150, "Done")) { moreOptions_ = false; focus_ = 0; }
        }
        bool nameOk = !nameField_.empty();
        if (button(W / 2 - 155, H - 28, 150, "Create New World", nameOk) || (in.enter && nameOk)) {
            newWorld.name = nameField_;
            newWorld.folder = saves.uniqueFolder(nameField_);
            newWorld.seedText = seedField_;
            newWorld.mode = mode_;
            newWorld.cheats = cheats_ && mode_ != 1;
            action = MenuAction::CreateWorld;
        }
        if (button(W / 2 + 5, H - 28, 150, "Cancel") || in.escape) open(Page::SelectWorld, saves);
        break;
    }

    // ------------------------------------------------ Настройки (GuiOptions)
    case Page::Options: {
        if (fromGame) ui.gradient(0, 0, W * sc, H * sc, {0.06f, 0.06f, 0.06f, 0.75f}, {0.06f, 0.06f, 0.06f, 0.8f});
        else drawDirtBackground(ui, tex.background, sc, W, H);
        centered("Options", W / 2, 20);
        auto pos = [&](int i) { return glm::vec2(W / 2 - 155 + (i % 2) * 160, H / 6 + 24 * (i / 2)); };
        bool changed = false;
        glm::vec2 p = pos(0);
        changed |= slider(0, p.x, p.y, 150, opt.music, opt.music <= 0 ? "Music: OFF" : "Music: " + pct(opt.music));
        p = pos(1);
        changed |= slider(1, p.x, p.y, 150, opt.sound, opt.sound <= 0 ? "Sound: OFF" : "Sound: " + pct(opt.sound));
        p = pos(2);
        if (button(p.x, p.y, 150, std::string("Invert Mouse: ") + (opt.invertMouse ? "ON" : "OFF"))) { opt.invertMouse = !opt.invertMouse; changed = true; }
        p = pos(3);
        std::string sens = opt.sensitivity <= 0 ? "Sensitivity: *yawn*" : opt.sensitivity >= 1 ? "Sensitivity: HYPERSPEED!!!" : "Sensitivity: " + pct(opt.sensitivity * 2.f);
        changed |= slider(3, p.x, p.y, 150, opt.sensitivity, sens);
        p = pos(4);
        std::string fovS = opt.fov <= 0 ? "FOV: Normal" : opt.fov >= 1 ? "FOV: Quake pro" : "FOV: " + std::to_string((int)opt.fovDegrees());
        changed |= slider(4, p.x, p.y, 150, opt.fov, fovS);
        p = pos(5);
        static const char* diff[4] = {"Peaceful", "Easy", "Normal", "Hard"};
        if (button(p.x, p.y, 150, std::string("Difficulty: ") + diff[opt.difficulty])) { opt.difficulty = (opt.difficulty + 1) % 4; changed = true; }

        if (button(W / 2 - 100, H / 6 + 96 + 12, 200, "Video Settings...")) page = Page::Video;
        if (button(W / 2 - 100, H / 6 + 120 + 12, 200, "Controls...")) { page = Page::Controls; waitingKey = -1; }
        if (button(W / 2 - 100, H / 6 + 168, 200, "Done") || in.escape) {
            action = MenuAction::CloseOptions;
            if (!fromGame) open(Page::Title, saves);
        }
        if (changed && action == MenuAction::None) action = MenuAction::OptionsChanged;
        break;
    }

    // ------------------------------------------------ Настройки графики (GuiVideoSettings)
    case Page::Video: {
        if (fromGame) ui.gradient(0, 0, W * sc, H * sc, {0.06f, 0.06f, 0.06f, 0.75f}, {0.06f, 0.06f, 0.06f, 0.8f});
        else drawDirtBackground(ui, tex.background, sc, W, H);
        centered("Video Settings", W / 2, 20);
        auto pos = [&](int i) { return glm::vec2(W / 2 - 155 + (i % 2) * 160, H / 6 + 24 * (i / 2)); };
        bool changed = false;
        static const char* dist[4] = {"Far", "Normal", "Short", "Tiny"};
        static const char* gui[4] = {"Auto", "Small", "Normal", "Large"};
        static const char* parts[3] = {"All", "Decreased", "Minimal"};
        glm::vec2 p = pos(0);
        if (button(p.x, p.y, 150, std::string("Graphics: ") + (opt.fancy ? "Fancy" : "Fast"))) { opt.fancy = !opt.fancy; changed = true; }
        p = pos(1);
        if (button(p.x, p.y, 150, std::string("Render Distance: ") + dist[opt.renderDistance])) { opt.renderDistance = (opt.renderDistance + 1) % 4; changed = true; }
        p = pos(2);
        if (button(p.x, p.y, 150, std::string("Smooth Lighting: ") + (opt.smoothLighting ? "ON" : "OFF"))) { opt.smoothLighting = !opt.smoothLighting; changed = true; }
        p = pos(3);
        std::string br = opt.gamma <= 0 ? "Brightness: Moody" : opt.gamma >= 1 ? "Brightness: Bright" : "Brightness: +" + pct(opt.gamma);
        changed |= slider(13, p.x, p.y, 150, opt.gamma, br);
        p = pos(4);
        if (button(p.x, p.y, 150, std::string("View Bobbing: ") + (opt.viewBobbing ? "ON" : "OFF"))) { opt.viewBobbing = !opt.viewBobbing; changed = true; }
        p = pos(5);
        if (button(p.x, p.y, 150, std::string("GUI Scale: ") + gui[opt.guiScale])) { opt.guiScale = (opt.guiScale + 1) % 4; changed = true; }
        p = pos(6);
        if (button(p.x, p.y, 150, std::string("Clouds: ") + (opt.clouds ? "ON" : "OFF"))) { opt.clouds = !opt.clouds; changed = true; }
        p = pos(7);
        if (button(p.x, p.y, 150, std::string("Particles: ") + parts[opt.particles])) { opt.particles = (opt.particles + 1) % 3; changed = true; }
        p = pos(8);
        static const char* perf[3] = {"Max FPS", "Balanced", "Power saver"};
        if (button(p.x, p.y, 150, std::string("Performance: ") + perf[opt.performance])) { opt.performance = (opt.performance + 1) % 3; changed = true; }
        if (button(W / 2 - 100, H / 6 + 168, 200, "Done") || in.escape) page = Page::Options;
        if (changed) action = MenuAction::OptionsChanged;
        break;
    }

    // ------------------------------------------------ Управление (GuiControls): клик по кнопке, затем новая клавиша
    case Page::Controls: {
        if (fromGame) ui.gradient(0, 0, W * sc, H * sc, {0.06f, 0.06f, 0.06f, 0.75f}, {0.06f, 0.06f, 0.06f, 0.8f});
        else drawDirtBackground(ui, tex.background, sc, W, H);
        centered("Controls", W / 2, 20);
        bool changed = false;
        if (waitingKey >= 0 && in.keyPressed >= 0) {
            if (in.keyPressed != 256) { opt.keys[waitingKey] = in.keyPressed; changed = true; } // Esc — отмена
            waitingKey = -1;
        }
        for (int i = 0; i < KB_COUNT; ++i) {
            float x = W / 2 - 155 + (i % 2) * 160, y = H / 6 + 24 * (i / 2);
            std::string label = waitingKey == i ? "> ??? <" : keyDisplayName(opt.keys[i]);
            ui.text(keyBindName(i), (x + 75) * sc, (y + 6) * sc, sc);
            if (button(x, y, 70, label)) waitingKey = i;
        }
        if (button(W / 2 - 100, H / 6 + 168, 200, "Done") || (in.escape && waitingKey < 0)) page = Page::Options;
        if (changed) action = MenuAction::OptionsChanged;
        break;
    }

    // ------------------------------------------------ Сетевая игра (GuiMultiplayer 1.0): адрес сервера и имя
    case Page::Multiplayer: {
        drawDirtBackground(ui, tex.background, sc, W, H);
        centered("Play Multiplayer", W / 2, H / 4 - 60 + 20);
        text("Enter the IP of a server to connect to:", W / 2 - 140, H / 4 - 60 + 60, GRAY);
        text("(same Wi-Fi: the address shown in the server window)", W / 2 - 140, H / 4 - 60 + 71, GRAY);
        if (in.tab) focus_ = focus_ == 0 ? 1 : 0;
        textField(0, W / 2 - 100, H / 4 - 10 + 50 + 18, 200, serverField);
        text("Your name:", W / 2 - 100, H / 4 + 96 - 14, GRAY);
        textField(1, W / 2 - 100, H / 4 + 96, 200, nameFieldMp);
        if (nameFieldMp.size() > 16) nameFieldMp.resize(16);
        bool ok = !serverField.empty() && !nameFieldMp.empty();
        if (button(W / 2 - 100, H / 4 + 96 + 36, 200, "Connect", ok) || (in.enter && ok)) {
            opt.lastServer = serverField;
            opt.playerName = nameFieldMp;
            action = MenuAction::JoinServer;
        }
        if (button(W / 2 - 100, H / 4 + 120 + 36, 200, "Cancel") || in.escape) open(Page::Title, saves);
        break;
    }

    // ------------------------------------------------ Не удалось подключиться / отключены от сервера
    case Page::Disconnected: {
        drawDirtBackground(ui, tex.background, sc, W, H);
        centered(disconnectTitle, W / 2, H / 2 - 50);
        centered(disconnectReason, W / 2, H / 2 - 10, GRAY);
        if (button(W / 2 - 100, H / 4 + 120 + 12, 200, "Back to title screen") || in.escape) open(Page::Title, saves);
        break;
    }

    // ------------------------------------------------ Загрузка мира
    case Page::Loading: {
        drawDirtBackground(ui, tex.background, sc, W, H);
        centered(loadingTitle, W / 2, H / 2 - 4 - 16);
        centered(loadingStage, W / 2, H / 2 - 4 + 8);
        float bx = W / 2 - 50, by = H / 2 + 16;
        ui.rect(bx * sc, by * sc, (bx + 100) * sc, (by + 2) * sc, {0.5f, 0.5f, 0.5f, 1});
        ui.rect(bx * sc, by * sc, (bx + 100 * std::clamp(loadingProgress, 0.f, 1.f)) * sc, (by + 2) * sc, {0.5f, 1.f, 0.5f, 1});
        break;
    }
    default: break;
    }
    return action;
}
