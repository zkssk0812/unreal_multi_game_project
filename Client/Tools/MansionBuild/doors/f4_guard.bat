@echo off
rem 2026-11-24 guard opens a closed door: watcher on kitchen side of wall_105
copy /Y "C:\UnrealProjects\MCPTest\Tools\MansionBuild\doors\f4_guard_script.txt" "C:\UnrealProjects\MCPTest\Saved\V3Test\f4_guard_script.txt" > NUL
start "" "C:\Users\FORYOUCOM\Desktop\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "C:\UnrealProjects\MCPTest\MCPTest.uproject" -game -windowed -ResX=1280 -ResY=720 -WinX=0 -WinY=0 -VHServer=127.0.0.1:9000 -VHBot=Gus -VHBotMin=1 -VHBotChar=1 -VHScript=C:\UnrealProjects\MCPTest\Saved\V3Test\f4_guard_script.txt -log -abslog=C:\UnrealProjects\MCPTest\Saved\V3Test\f4_Gus.log
