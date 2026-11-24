@echo off
rem 2026-11-25 crosshair hit/kill marker test: Hun hunts Tar with melee (team server)
copy /Y "C:\UnrealProjects\MCPTest\Tools\UI\crosshair\hunt_hun_script.txt" "C:\UnrealProjects\MCPTest\Saved\V3Test\hunt_hun_script.txt" > NUL
copy /Y "C:\UnrealProjects\MCPTest\Tools\UI\crosshair\hunt_tar_script.txt" "C:\UnrealProjects\MCPTest\Saved\V3Test\hunt_tar_script.txt" > NUL
start "" "C:\Users\FORYOUCOM\Desktop\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "C:\UnrealProjects\MCPTest\MCPTest.uproject" -game -windowed -ResX=1280 -ResY=720 -WinX=0 -WinY=0 -VHServer=127.0.0.1:9000 -VHBot=Hun -VHBotMin=2 -VHBotChar=2 -VHBotHunt -VHCrosshairShots -VHCrosshairFollow -VHScript=C:\UnrealProjects\MCPTest\Saved\V3Test\hunt_hun_script.txt -log -abslog=C:\UnrealProjects\MCPTest\Saved\V3Test\ch_Hun.log
timeout /t 8 > NUL
start "" "C:\Users\FORYOUCOM\Desktop\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "C:\UnrealProjects\MCPTest\MCPTest.uproject" -game -windowed -ResX=960 -ResY=540 -WinX=960 -WinY=520 -VHServer=127.0.0.1:9000 -VHBot=Tar -VHBotMin=2 -VHBotChar=3 -VHBotStill -VHScript=C:\UnrealProjects\MCPTest\Saved\V3Test\hunt_tar_script.txt -log -abslog=C:\UnrealProjects\MCPTest\Saved\V3Test\ch_Tar.log
