@echo off
rem Allow MiniCraftServer through Windows Firewall so friends on the same Wi-Fi can connect.
rem Run once on the PC that hosts the server. Needs administrator rights (asks via UAC).
net session >nul 2>&1
if %errorlevel% neq 0 (
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)
rem Old "block" rules for this exe (created if the Windows prompt was cancelled) would override the allow rule
powershell -NoProfile -Command "Get-NetFirewallApplicationFilter -Program '%~dp0MiniCraftServer.exe' -ErrorAction SilentlyContinue | Get-NetFirewallRule | Where-Object Action -eq 'Block' | Remove-NetFirewallRule"
netsh advfirewall firewall delete rule name="MiniCraft Server" >nul 2>&1
netsh advfirewall firewall add rule name="MiniCraft Server" dir=in action=allow program="%~dp0MiniCraftServer.exe" enable=yes profile=any
netsh advfirewall firewall add rule name="MiniCraft Server" dir=in action=allow protocol=TCP localport=25565 enable=yes profile=any
echo.
echo Done. Friends on your network can now connect to MiniCraftServer (port 25565).
pause
