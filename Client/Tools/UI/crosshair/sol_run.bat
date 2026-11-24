@echo off
rem 2026-11-25 crosshair single bot: prompt overlap, range, OS cursor in game / pause / result / lobby (team server, 1080p)
copy /Y "C:\UnrealProjects\MCPTest\Tools\UI\crosshair\sol_script.txt" "C:\UnrealProjects\MCPTest\Saved\V3Test\sol_script.txt" > NUL
start "" "C:\Users\FORYOUCOM\Desktop\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "C:\UnrealProjects\MCPTest\MCPTest.uproject" -game -windowed -ResX=1280 -ResY=720 -WinX=0 -WinY=0 -VHServer=127.0.0.1:9000 -VHBot=Sol -VHBotMin=1 -VHBotChar=1 -VHScript=C:\UnrealProjects\MCPTest\Saved\V3Test\sol_script.txt -log -abslog=C:\UnrealProjects\MCPTest\Saved\V3Test\ch_Sol.log
