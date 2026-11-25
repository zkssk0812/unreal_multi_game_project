@echo off
rem 2026-11-25 door peek facing + outline test: 2 clients on team server (server must already run)
copy /Y "C:\UnrealProjects\MCPTest\Tools\MansionBuild\doors\h1_ann_script.txt" "C:\UnrealProjects\MCPTest\Saved\V3Test\h1_ann_script.txt" > NUL
copy /Y "C:\UnrealProjects\MCPTest\Tools\MansionBuild\doors\h1_bob_script.txt" "C:\UnrealProjects\MCPTest\Saved\V3Test\h1_bob_script.txt" > NUL
start "" "C:\Users\FORYOUCOM\Desktop\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "C:\UnrealProjects\MCPTest\MCPTest.uproject" -game -windowed -ResX=1280 -ResY=720 -WinX=0 -WinY=0 -VHServer=127.0.0.1:9000 -VHBot=Ann -VHBotMin=2 -VHBotChar=1 -VHScript=C:\UnrealProjects\MCPTest\Saved\V3Test\h1_ann_script.txt -log -abslog=C:\UnrealProjects\MCPTest\Saved\V3Test\h1_Ann.log
timeout /t 8 > NUL
start "" "C:\Users\FORYOUCOM\Desktop\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "C:\UnrealProjects\MCPTest\MCPTest.uproject" -game -windowed -ResX=960 -ResY=540 -WinX=1280 -WinY=0 -VHServer=127.0.0.1:9000 -VHBot=Bob -VHBotMin=2 -VHBotChar=2 -VHScript=C:\UnrealProjects\MCPTest\Saved\V3Test\h1_bob_script.txt -log -abslog=C:\UnrealProjects\MCPTest\Saved\V3Test\h1_Bob.log
