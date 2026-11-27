@echo off
rem 2026-11-27 work speed test: two heist bots (safe -> treasure -> return), Ann without Tools perk, Bob with Tools perk
start "" "C:\Users\FORYOUCOM\Desktop\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "C:\UnrealProjects\MCPTest\MCPTest.uproject" -game -windowed -ResX=1280 -ResY=720 -WinX=0 -WinY=0 -VHServer=127.0.0.1:9000 -VHBot=Ann -VHBotMin=2 -VHBotChar=1 -VHBotPerks=1,2 -VHBotHeist -log -abslog=C:\UnrealProjects\MCPTest\Saved\V3Test\j2_Ann.log
timeout /t 8 > NUL
start "" "C:\Users\FORYOUCOM\Desktop\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "C:\UnrealProjects\MCPTest\MCPTest.uproject" -game -windowed -ResX=960 -ResY=540 -WinX=1280 -WinY=0 -VHServer=127.0.0.1:9000 -VHBot=Bob -VHBotMin=2 -VHBotChar=2 -VHBotPerks=3,5 -VHBotHeist -log -abslog=C:\UnrealProjects\MCPTest\Saved\V3Test\j2_Bob.log
