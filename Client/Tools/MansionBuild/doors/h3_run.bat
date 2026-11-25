@echo off
rem 2026-11-25 outline compare: 1 client on team server (box focus outline vs door outline)
copy /Y "C:\UnrealProjects\MCPTest\Tools\MansionBuild\doors\h3_ann_script.txt" "C:\UnrealProjects\MCPTest\Saved\V3Test\h3_ann_script.txt" > NUL
start "" "C:\Users\FORYOUCOM\Desktop\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "C:\UnrealProjects\MCPTest\MCPTest.uproject" -game -windowed -ResX=1280 -ResY=720 -WinX=0 -WinY=0 -VHServer=127.0.0.1:9000 -VHBot=Ann -VHBotMin=1 -VHBotChar=1 -VHScript=C:\UnrealProjects\MCPTest\Saved\V3Test\h3_ann_script.txt -log -abslog=C:\UnrealProjects\MCPTest\Saved\V3Test\h3_Ann.log
