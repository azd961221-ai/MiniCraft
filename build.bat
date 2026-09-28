@echo off
cd /d "%~dp0"
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 || exit /b 1
cmake --build build --config Release || exit /b 1
echo.
echo Build OK: build\Release\MiniCraft.exe
