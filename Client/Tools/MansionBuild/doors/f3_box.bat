@echo off
rem 2026-11-24 two box types on mock server
copy /Y "C:\UnrealProjects\MCPTest\Tools\MansionBuild\doors\f3_box_script.txt" "C:\UnrealProjects\MCPTest\Saved\V3Test\f3_box_script.txt" > NUL
start "" "C:\Users\FORYOUCOM\Desktop\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "C:\UnrealProjects\MCPTest\MCPTest.uproject" -game -windowed -ResX=1280 -ResY=720 -WinX=0 -WinY=0 -VHMock -VHBot=Mia -VHBotMin=1 -VHScript=C:\UnrealProjects\MCPTest\Saved\V3Test\f3_box_script.txt -log -abslog=C:\UnrealProjects\MCPTest\Saved\V3Test\f3_Mia.log
