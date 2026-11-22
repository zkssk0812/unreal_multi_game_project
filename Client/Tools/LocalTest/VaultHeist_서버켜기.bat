@echo off
title VaultHeist 테스트 서버 v3 - 127.0.0.1:9000
rem 정호님 서버 v3.0(PvPvE) 시험 사본을 우리 PC에서 켠다. 서버 폴더: C:\UnrealProjects\VaultHeistServerV3
rem (옛 서버 VaultHeistServerLatest 는 프로토콜 v2.1 이라 지금 클라와 버전 불일치로 접속이 안 된다.)
set "SERVER_DIR=C:\UnrealProjects\VaultHeistServerV3\Project_Rob\Binaries\x64\Release"
set "MAP_DIR=%SERVER_DIR%\Data\Maps"
set "MAP_DST=%MAP_DIR%\mansion_1f_server.json"
set "MAP_SRC=C:\UnrealProjects\MCPTest\Docs\ServerHandoff\mansion_1f_server.json"

if not exist "%SERVER_DIR%\GameServer.exe" goto nofile

tasklist /FI "IMAGENAME eq GameServer.exe" | find /I "GameServer.exe" > nul
if not errorlevel 1 goto running

rem 서버를 다시 빌드하면 옛 맵(v1)이 복사된다. 켤 때마다 우리 레벨과 같은 맵(v3)인지 확인하고 다르면 바꾼다.
if not exist "%MAP_SRC%" goto mapskip
fc /b "%MAP_SRC%" "%MAP_DST%" > nul 2>&1
if not errorlevel 1 goto mapok
echo [맵] 서버 맵이 우리 레벨과 달라서 v3 맵으로 바꿉니다.
echo      원래 파일은 mansion_1f_server.before_swap.json.bak 으로 보관합니다.
if exist "%MAP_DST%" copy /y "%MAP_DST%" "%MAP_DIR%\mansion_1f_server.before_swap.json.bak" > nul
copy /y "%MAP_SRC%" "%MAP_DST%" > nul
if errorlevel 1 echo [주의] 맵 파일을 바꾸지 못했습니다. 금고와 상자 위치가 화면과 다를 수 있습니다.
goto mapdone
:mapok
echo [맵] v3 맵 확인 완료
goto mapdone
:mapskip
echo [주의] 기준 맵 파일이 없어 맵 확인을 건너뜁니다: %MAP_SRC%
:mapdone

echo ==================================================
echo   털어라! 금고! 테스트 서버 v3.0
echo   주소: 127.0.0.1:9000
echo.
echo   - 이 창은 게임하는 동안 계속 열어 두세요.
echo   - 끄기: 창 오른쪽 위 X 를 누르거나 quit 입력 후 Enter
echo   - 주의: 아무것도 안 쓰고 Enter 만 누르면 서버가 꺼집니다.
echo   - 아래에 "protocol 0x00030000", "mansion_1f v3",
echo     "Listener: 포트 9000" 이 보이면 준비 완료
echo   - "server_config.json 없음" 경고는 정상입니다.
echo     DB 없이 켜서 최고 기록은 서버를 끄면 사라집니다.
echo ==================================================
echo.
cd /d "%SERVER_DIR%"
"%SERVER_DIR%\GameServer.exe"
echo.
echo 서버가 꺼졌습니다. 아무 키나 누르면 창이 닫힙니다.
pause > nul
exit /b 0

:running
echo 서버가 이미 켜져 있습니다. 켜져 있는 서버 위치:
powershell -NoProfile -Command "Get-Process GameServer | ForEach-Object { '  ' + $_.Path }"
echo.
echo 위 위치가 VaultHeistServerV3 가 아니면 옛 서버입니다. 옛 서버는 버전 불일치로 접속이 안 됩니다.
echo 그 서버 창을 닫고 이 파일을 다시 실행하세요.
pause
exit /b 0

:nofile
echo 서버 실행 파일을 찾을 수 없습니다:
echo %SERVER_DIR%\GameServer.exe
echo 서버를 다시 빌드해야 합니다. Visual Studio 로
echo C:\UnrealProjects\VaultHeistServerV3\Project_Rob\Project_Rob.sln 을 열고
echo 위쪽 구성을 Release / x64 로 바꾼 뒤 빌드 - 솔루션 빌드를 누르세요.
pause
exit /b 1
