# Сборка раздачи для другого компьютера: dist\MiniCraft.zip (и обновление папки dist\MiniCraft)
# Внутри: MiniCraft.exe, MiniCraftServer.exe, glfw3.dll, библиотеки Visual C++, папка assets,
# allow-firewall.bat и инструкция по сетевой игре (dist-files\).
# ВАЖНО: папка dist\MiniCraft не удаляется — в ней могут быть сохранения (saves, world, options.txt).
# Обновляются только программа и assets; в zip личные файлы не попадают.
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$rel = Join-Path $root 'build\Release'
$assetsSrc = Join-Path $root 'assets'
$out = Join-Path $root 'dist\MiniCraft'
$stageRoot = Join-Path $root 'dist\_stage'
$stage = Join-Path $stageRoot 'MiniCraft'

cmake --build (Join-Path $root 'build') --config Release
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }

if (Test-Path $stageRoot) { Remove-Item -Recurse -Force $stageRoot }
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item (Join-Path $rel 'MiniCraft.exe'), (Join-Path $rel 'MiniCraftServer.exe'), (Join-Path $rel 'glfw3.dll') $stage

# Библиотеки Visual C++ рядом с exe (app-local): берём из Redist установленных Build Tools
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -property installationPath
$crt = Get-ChildItem (Join-Path $vs 'VC\Redist\MSVC') -Directory |
    ForEach-Object { Join-Path $_.FullName 'x64\Microsoft.VC143.CRT' } |
    Where-Object { Test-Path $_ } | Select-Object -Last 1
if (-not $crt) { throw 'Visual C++ runtime not found (VC\Redist\MSVC\...\x64\Microsoft.VC143.CRT)' }
foreach ($dll in 'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll') { Copy-Item (Join-Path $crt $dll) $stage }

# Брандмауэр и инструкция по сетевой игре
Copy-Item (Join-Path $root 'dist-files\*') $stage

# Ресурсы, которые читает игра: папка assets\ в корне проекта (текстуры 1.4.2 + sounds\), без .import-файлов.
# Раньше копировались src\terrain.png, src\gui и т.д. — после переезда ассетов в src\1.4.2 и assets\ этих путей нет
$assets = Join-Path $stage 'assets'
if (-not (Test-Path (Join-Path $assetsSrc 'terrain.png'))) { throw "Assets not found: $assetsSrc\terrain.png" }
robocopy $assetsSrc $assets /E /XF *.import /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw 'Failed to copy assets' }

$zip = Join-Path $root 'dist\MiniCraft.zip'
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $stage -DestinationPath $zip

# Играбельная копия dist\MiniCraft: программа и assets обновляются, сохранения и настройки остаются
New-Item -ItemType Directory -Force $out | Out-Null
robocopy $stage $out /E /XD assets /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw 'Failed to update dist\MiniCraft' }
robocopy $assets (Join-Path $out 'assets') /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw 'Failed to update dist\MiniCraft\assets' }
$global:LASTEXITCODE = 0
Remove-Item -Recurse -Force $stageRoot

$size = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Host "Done: $zip ($size MB); dist\MiniCraft updated, saves kept"
