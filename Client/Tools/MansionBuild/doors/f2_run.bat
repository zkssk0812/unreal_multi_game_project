@echo off
rem 2026-11-24 closed door shooting test: Dee loots weapon boxes for pistol/railgun and shoots through wall_119; Bob is the target
copy /Y "C:\UnrealProjects\MCPTest\Tools\MansionBuild\doors\f2_bob_script.txt" "C:\UnrealProjects\MCPTest\Saved\V3Test\f2_bob_script.txt" > NUL
start "" "C:\Users\FORYOUCOM\Desktop\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "C:\UnrealProjects\MCPTest\MCPTest.uproject" -game -windowed -ResX=1280 -ResY=720 -WinX=0 -WinY=0 -VHServer=127.0.0.1:9000 -VHBot=Dee -VHBotMin=2 -VHBotChar=3 -VHBotDoorShot -log -abslog=C:\UnrealProjects\MCPTest\Saved\V3Test\f2_Dee.log
timeout /t 8 > NUL
start "" "C:\Users\FORYOUCOM\Desktop\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "C:\UnrealProjects\MCPTest\MCPTest.uproject" -game -windowed -ResX=960 -ResY=540 -WinX=1280 -WinY=0 -VHServer=127.0.0.1:9000 -VHBot=Bob -VHBotMin=2 -VHBotChar=2 -VHScript=C:\UnrealProjects\MCPTest\Saved\V3Test\f2_bob_script.txt -log -abslog=C:\UnrealProjects\MCPTest\Saved\V3Test\f2_Bob.log
