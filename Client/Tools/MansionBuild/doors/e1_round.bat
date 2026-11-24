@echo off
rem 2026-11-24 full round regression with F doors: 2 bots (loot + heist) on team server until the result screen
start "" "C:\Users\FORYOUCOM\Desktop\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "C:\UnrealProjects\MCPTest\MCPTest.uproject" -game -windowed -ResX=960 -ResY=540 -WinX=0 -WinY=0 -VHServer=127.0.0.1:9000 -VHBot=Lou -VHBotMin=2 -VHBotLoot -VHBotChar=2 -VHShotResult -log -abslog=C:\UnrealProjects\MCPTest\Saved\V3Test\e1r_Lou.log
timeout /t 8 > NUL
start "" "C:\Users\FORYOUCOM\Desktop\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "C:\UnrealProjects\MCPTest\MCPTest.uproject" -game -windowed -ResX=960 -ResY=540 -WinX=960 -WinY=0 -VHServer=127.0.0.1:9000 -VHBot=Hal -VHBotMin=2 -VHBotHeist -VHBotChar=3 -VHShotResult -log -abslog=C:\UnrealProjects\MCPTest\Saved\V3Test\e1r_Hal.log
