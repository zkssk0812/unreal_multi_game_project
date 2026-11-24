@echo off
rem 2026-11-25 crosshair recoil/charge film: Bob stands behind wall_119, Dee loots pistol/railgun and shoots (fullscreen 1080p, crosshair follows Bob)
copy /Y "C:\UnrealProjects\MCPTest\Tools\UI\crosshair\shot_bob_script.txt" "C:\UnrealProjects\MCPTest\Saved\V3Test\shot_bob_script.txt" > NUL
start "" "C:\Users\FORYOUCOM\Desktop\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "C:\UnrealProjects\MCPTest\MCPTest.uproject" -game -windowed -ResX=640 -ResY=360 -WinX=1280 -WinY=700 -VHServer=127.0.0.1:9000 -VHBot=Bob -VHBotMin=2 -VHBotChar=2 -VHScript=C:\UnrealProjects\MCPTest\Saved\V3Test\shot_bob_script.txt -log -abslog=C:\UnrealProjects\MCPTest\Saved\V3Test\ch_Bob.log
timeout /t 8 > NUL
start "" "C:\Users\FORYOUCOM\Desktop\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "C:\UnrealProjects\MCPTest\MCPTest.uproject" -game -windowed -ResX=1280 -ResY=720 -WinX=0 -WinY=0 -VHServer=127.0.0.1:9000 -VHBot=Dee -VHBotMin=2 -VHBotChar=3 -VHBotDoorShot -VHDoorShotHold=1.6 -VHCrosshairFollow -VHSetRes=1920x1080wf -log -abslog=C:\UnrealProjects\MCPTest\Saved\V3Test\ch_Dee.log
