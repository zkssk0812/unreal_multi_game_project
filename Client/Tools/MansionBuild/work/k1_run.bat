@echo off
rem 2026-11-27 drop-on-down test (team server 16244a7): Vicky collects treasure + 6 valuables, then gets downed by Hunty (player) then by a guard
rem Hunty waits (HuntNear 250), downs Vicky, then picks up every dropped valuable nearby (HuntLootAll)
start "" "C:\Users\FORYOUCOM\Desktop\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "C:\UnrealProjects\MCPTest\MCPTest.uproject" -game -windowed -ResX=1280 -ResY=720 -WinX=0 -WinY=0 -VHServer=127.0.0.1:9000 -VHBot=Vicky -VHBotMin=2 -VHBotChar=1 -VHBotHeist -VHBotDownTest=Player,Guard -VHBotDownValuables=6 -log -abslog=C:\UnrealProjects\MCPTest\Saved\V3Test\k1_Vicky.log
timeout /t 8 > NUL
start "" "C:\Users\FORYOUCOM\Desktop\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "C:\UnrealProjects\MCPTest\MCPTest.uproject" -game -windowed -ResX=960 -ResY=540 -WinX=1280 -WinY=0 -VHServer=127.0.0.1:9000 -VHBot=Hunty -VHBotMin=2 -VHBotChar=2 -VHBotHunt -VHBotHuntNear=250 -VHBotHuntLoot=3 -VHBotHuntLootAll -log -abslog=C:\UnrealProjects\MCPTest\Saved\V3Test\k1_Hunty.log
