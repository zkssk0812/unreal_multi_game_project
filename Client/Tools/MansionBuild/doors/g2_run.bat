@echo off
rem 2026-11-25 door request while working on a box (server InvalidState)
copy /Y "C:\UnrealProjects\MCPTest\Tools\MansionBuild\doors\g2_ann_script.txt" "C:\UnrealProjects\MCPTest\Saved\V3Test\g2_ann_script.txt" > NUL
start "" "C:\Users\FORYOUCOM\Desktop\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "C:\UnrealProjects\MCPTest\MCPTest.uproject" -game -windowed -ResX=1280 -ResY=720 -WinX=0 -WinY=0 -VHServer=127.0.0.1:9000 -VHBot=Ann -VHBotMin=1 -VHBotChar=1 -VHScript=C:\UnrealProjects\MCPTest\Saved\V3Test\g2_ann_script.txt -log -abslog=C:\UnrealProjects\MCPTest\Saved\V3Test\g2_Ann.log
