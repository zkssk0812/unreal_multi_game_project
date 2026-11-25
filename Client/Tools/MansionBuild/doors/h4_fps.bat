@echo off
rem 2026-11-25 fps under door peek load: 1 client 1600x900 on team server (editor closed)
copy /Y "C:\UnrealProjects\MCPTest\Tools\MansionBuild\doors\h4_fps_script.txt" "C:\UnrealProjects\MCPTest\Saved\V3Test\h4_fps_script.txt" > NUL
start "" "C:\Users\FORYOUCOM\Desktop\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "C:\UnrealProjects\MCPTest\MCPTest.uproject" -game -windowed -ResX=1600 -ResY=900 -VHServer=127.0.0.1:9000 -VHLevel=/Game/VaultHeist/Maps/Mansion/L_Mansion_Shell -VHBot=Fpspeek -VHBotMin=1 -VHBotChar=1 -VHScript=C:\UnrealProjects\MCPTest\Saved\V3Test\h4_fps_script.txt -log -abslog=C:\UnrealProjects\MCPTest\Saved\V3Test\fps_Peek.log
