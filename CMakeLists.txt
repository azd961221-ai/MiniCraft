cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "C:/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "vcpkg toolchain")
endif()

project(MiniCraft C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

add_executable(MiniCraft
    src/main.cpp
    src/World.cpp
    src/WorldGen.cpp
    src/WorldGen10.cpp
    src/Mesher.cpp
    src/Liquid.cpp
    src/Physics.cpp
    src/Fire.cpp
    src/Redstone.cpp
    src/Piston.cpp
    src/Portal.cpp
    src/Enchant.cpp
    src/Vehicle.cpp
    src/Player.cpp
    src/Item.cpp
    src/Crafting.cpp
    src/Entity.cpp
    src/Gui.cpp
    src/Model.cpp
    src/Mob.cpp
    src/Options.cpp
    src/Saves.cpp
    src/Menu.cpp
    src/Image.cpp
    src/Audio.cpp
    src/Net.cpp
    src/GameLogic.cpp
    src/Map.cpp
    src/Village10.cpp
    src/Stronghold10.cpp
    src/Fortress10.cpp
)

target_include_directories(MiniCraft PRIVATE third_party)
target_link_libraries(MiniCraft PRIVATE glfw glad::glad glm::glm windowscodecs ole32 ws2_32 iphlpapi)
# Ассеты (текстуры, звуки) берутся из src/, если рядом с exe нет папки assets/
target_compile_definitions(MiniCraft PRIVATE MC_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src")

# Выделенный сервер для мультиплеера: build\Release\MiniCraftServer.exe (консоль, без графики)
add_executable(MiniCraftServer
    server/Server.cpp
    src/Net.cpp
    src/World.cpp
    src/WorldGen.cpp
    src/WorldGen10.cpp
    src/Mesher.cpp
    src/Liquid.cpp
    src/Physics.cpp
    src/Fire.cpp
    src/Redstone.cpp
    src/Piston.cpp
    src/Portal.cpp
    src/Item.cpp
    src/Crafting.cpp
    src/Enchant.cpp
    src/Saves.cpp
    src/Vehicle.cpp
    src/Mob.cpp
    src/Entity.cpp
    src/Player.cpp
    src/Model.cpp
    src/GameLogic.cpp
    src/Village10.cpp
    src/Stronghold10.cpp
    src/Fortress10.cpp
)
target_link_libraries(MiniCraftServer PRIVATE glad::glad glm::glm ws2_32 iphlpapi)
if(MSVC)
    target_compile_options(MiniCraftServer PRIVATE /W3 /utf-8 /O2)
    target_compile_definitions(MiniCraftServer PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()

# Тестовый бот мультиплеера (подключается к серверу и проверяет сущности, удары, выброс и ломание): build\Release\mpbot.exe
add_executable(mpbot tests/mpbot.cpp src/Net.cpp src/Item.cpp)
target_link_libraries(mpbot PRIVATE glm::glm ws2_32 iphlpapi)
if(MSVC)
    target_compile_options(mpbot PRIVATE /W3 /utf-8)
    target_compile_definitions(mpbot PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()

# Проверки логики мира без окна (вода, редстоун, рельсы, грядки, лава, сохранения): build\Release\worldtest.exe
add_executable(worldtest
    tests/worldtest.cpp
    src/World.cpp src/WorldGen.cpp src/WorldGen10.cpp src/Mesher.cpp src/Liquid.cpp src/Physics.cpp src/Fire.cpp
    src/Redstone.cpp src/Piston.cpp src/Portal.cpp src/Item.cpp src/Crafting.cpp src/Enchant.cpp src/Saves.cpp
    src/Vehicle.cpp src/Mob.cpp src/Entity.cpp src/Player.cpp src/Model.cpp src/GameLogic.cpp src/Map.cpp src/Village10.cpp src/Stronghold10.cpp src/Fortress10.cpp
)
target_link_libraries(worldtest PRIVATE glad::glad glm::glm)
if(MSVC)
    target_compile_options(worldtest PRIVATE /W3 /utf-8 /O2)
    target_compile_definitions(worldtest PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()

# Проверки игровой логики без окна: build\Release\selftest.exe
add_executable(selftest tests/selftest.cpp src/Item.cpp src/Crafting.cpp src/Enchant.cpp)
target_link_libraries(selftest PRIVATE glm::glm)
if(MSVC)
    target_compile_options(selftest PRIVATE /W3 /utf-8)
endif()

if(MSVC)
    target_compile_options(MiniCraft PRIVATE /W3 /utf-8 /O2)
    target_compile_definitions(MiniCraft PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
    # miniaudio + stb_vorbis — чужой код, предупреждения не нужны
    set_source_files_properties(src/Audio.cpp PROPERTIES COMPILE_OPTIONS "/W0")
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT MiniCraft)
    set_target_properties(MiniCraft PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:MiniCraft>")
endif()
