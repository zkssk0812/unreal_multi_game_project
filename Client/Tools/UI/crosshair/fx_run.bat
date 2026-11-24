@echo off
rem 2026-11-25 crosshair fx shots (Gal, 1920x1080, team server)
copy /Y "C:\UnrealProjects\MCPTest\Tools\UI\crosshair\fx_script.txt" "C:\UnrealProjects\MCPTest\Saved\V3Test\fx_script.txt" > NUL
start "" "C:\Users\FORYOUCOM\Desktop\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "C:\UnrealProjects\MCPTest\MCPTest.uproject" -game -windowed -ResX=1920 -ResY=1080 -WinX=0 -WinY=0 -VHServer=127.0.0.1:9000 -VHBot=Gal -VHBotMin=1 -VHBotChar=1 -VHScript=C:\UnrealProjects\MCPTest\Saved\V3Test\fx_script.txt -log -abslog=C:\UnrealProjects\MCPTest\Saved\V3Test\ch_Fx.log
