#pragma once
// Экраны с предметами: инвентарь, творческий, верстак, печь, сундук.
#include <functional>
#include <vector>
#include "Inventory.h"
#include "Player.h"
#include "UI.h"

enum class GuiKind { None, Inventory, Creative, Crafting, Furnace, Chest, Dispenser, Enchant, Brewing, Anvil, Beacon };

struct GuiTextures {
    GLuint gui = 0, inventory = 0, crafting = 0, furnace = 0, container = 0, allitems = 0, items = 0, terrain = 0, trap = 0;
    GLuint enchant = 0, alchemy = 0, repair = 0, beacon = 0;
    GLuint creativeList = 0, creativeSearch = 0, creativeSurvival = 0;
};

struct GuiContext {
    UI& ui;
    const GuiTextures& tex;
    float sc;         // размер пикселя GUI
    float W, H;       // экран в единицах GUI
    float mx, my;     // мышь в единицах GUI
    Inventory& inv;
    std::function<void(const ItemStack&)> throwItem; // выбросить предмет в мир
    int* xpLevel = nullptr;                          // уровни опыта (плата за зачарование)
    bool creative = false;
    const std::vector<ActiveEffect>* effects = nullptr; // эффекты зелий (показываются у инвентаря)
    // Стив в окне инвентаря: вызывается после фона (fx, fy — координаты ног, scale — масштаб модели)
    std::function<void(float fx, float fy, float scale)> drawPlayer;
    std::function<void()> onAnvilUse; // взяли результат с наковальни (износ наковальни, звук)
    int beaconLevels = 0;                        // уровни пирамиды под открытым маяком
    std::function<void(uint8_t)> onBeaconConfirm; // маяк: подтвердили выбор (новая мета блока)
};

// Предмет в слоте 16x16 (x, y — левый верхний угол в единицах GUI), с количеством и износом
void drawItemStack(UI& ui, const GuiTextures& tex, const ItemStack& s, float x, float y, float sc);

class ContainerScreen {
public:
    GuiKind kind = GuiKind::None;
    ItemStack cursor;           // предмет «на мышке»
    TileEntity* tile = nullptr; // печь или сундук
    TileEntity* tile2 = nullptr; // вторая половина двойного сундука (нижние 27 слотов)
    bool enderChest = false;     // окно эндер-сундука (заголовок «Ender Chest»)
    int enchantShelves = 0;     // книжные полки вокруг стола зачарования

    void open(GuiKind k, TileEntity* te = nullptr, TileEntity* te2 = nullptr);
    void openBeacon(uint8_t meta); // после open(GuiKind::Beacon): текущий выбор эффектов из меты
    bool wantClose = false;        // окно просит закрыться (маяк: подтверждение или отмена)
    int chestRows() const { return tile2 ? 6 : 3; }
    void close(GuiContext& ctx); // вернуть предметы из сетки крафта и с курсора
    void draw(GuiContext& ctx);
    void mouseDown(GuiContext& ctx, int button, bool shift);
    void mouseUp() { scrolling_ = false; }
    void scroll(float dy);
    // Номер 1..9 над слотом: обмен со слотом хотбара
    void hotkey(GuiContext& ctx, int hotbarSlot);

private:
    enum class Role { Normal, CraftOut, FurnaceOut, Armor, Palette, AnvilOut };
    enum Group { HOTBAR, MAIN, CONTAINER, ARMOR, OUTPUT, PALETTE };
    struct Slot {
        float x, y;
        ItemStack* stack;
        Role role;
        Group group;
        int index;
    };

    ItemStack beaconPay_;
    int beaconPrim_ = 0, beaconSec_ = 0;
    struct BeaconBtn {
        float x, y;      // левый верхний угол (22x22), в единицах GUI от угла окна
        int kind, value; // 0 основной (номер эффекта), 1 вторичный (1 регенерация, 2 основной II), 2 готово, 3 отмена
        int effect;      // значок
        bool enabled, selected;
    };
    std::vector<BeaconBtn> beaconButtons(const GuiContext& ctx) const;
    ItemStack anvilIn_[2], anvilOut_;
    int anvilCost_ = 0, anvilMaterial_ = 0; // цена в уровнях; сколько материала ушло на ремонт
    void updateAnvil();
    ItemStack enchantItem_;
    int enchantLevels_[3] = {0, 0, 0};
    uint32_t enchantRng_ = 12345u, enchantKey_ = 0xFFFFFFFFu;
    std::string enchantWords_[3];
    void refreshEnchant(GuiContext& ctx);
    ItemStack craft_[9];
    int craftW_ = 2;
    ItemStack craftOut_;
    ItemStack paletteTmp_;
    int scrollRow_ = 0;
    bool scrolling_ = false;
    int selectedTab_ = 0;
    ItemStack trashStack_;
    std::vector<ItemStack> palette_;
    std::vector<ItemStack> tabItems_[12];

    const std::vector<ItemStack>& currentPalette() const;
    void initTabs();

    float panelW() const;
    float panelH() const;
    std::vector<Slot> slots(GuiContext& ctx);
    Slot* slotAt(std::vector<Slot>& s, GuiContext& ctx);
    void clickSlot(GuiContext& ctx, Slot& s, int button, bool shift);
    void shiftMove(GuiContext& ctx, Slot& s);
    bool moveInto(ItemStack& src, ItemStack* dst, int n);
    void updateCraft();
    void consumeCraft();
    int maxScroll() const;
};
