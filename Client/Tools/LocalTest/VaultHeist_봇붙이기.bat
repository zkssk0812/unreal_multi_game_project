@echo off
title VaultHeist 봇 붙이기
rem 자동 진행 봇 클라를 1~2개 띄운다. 봇은 로그인, 빠른 찾기, 캐릭터·퍽, 인원이 모이면 준비까지 알아서 한다. (서버 v3.0 기준)
set "UE=C:\Users\FORYOUCOM\Desktop\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
set "PROJ=C:\UnrealProjects\MCPTest\MCPTest.uproject"
set "OPT=-game -windowed -ResX=640 -ResY=360 -VHServer=127.0.0.1:9000"

if not exist "%UE%" goto noue

tasklist /FI "IMAGENAME eq GameServer.exe" | find /I "GameServer.exe" > nul
if errorlevel 1 echo [주의] 서버가 꺼져 있는 것 같습니다. 바탕화면 VaultHeist_서버켜기.bat 를 먼저 실행하세요.

echo ==================================================
echo   봇을 몇 개 붙일까요?
echo     1 = 봇 1개  - 나 + 봇 = 2명 판
echo     2 = 봇 2개  - 나 + 봇 2개 = 3명 판
echo   먼저 에디터에서 방을 만들어 두면 봇이 그 방으로 들어옵니다.
echo   봇 이름은 Carl, Dave 이므로 내 이름으로 쓰지 마세요.
echo ==================================================
set "N="
set /p N=숫자를 입력하고 Enter:
if "%N%"=="1" goto ask
if "%N%"=="2" goto ask
echo 1 또는 2 만 입력해 주세요.
pause
exit /b 1

:ask
echo.
echo ==================================================
echo   봇이 게임에서 무엇을 할까요?
echo     1 = 돌아다니기만 함  - 조용히 시험할 때
echo     2 = 금고를 털고 반납함  - 경쟁 상대, 결과창 순위 확인용
echo     3 = 다른 플레이어를 찾아가 공격함  - 전투, 쓰러짐 확인용
echo ==================================================
set "B="
set /p B=숫자를 입력하고 Enter (그냥 Enter = 1):
set "ACT="
if "%B%"=="2" set "ACT=-VHBotHeist"
if "%B%"=="3" set "ACT=-VHBotHunt"

if "%N%"=="2" goto two

start "" "%UE%" "%PROJ%" %OPT% -VHBot=Carl -VHBotMin=2 %ACT%
goto done

:two
start "" "%UE%" "%PROJ%" %OPT% -VHBot=Carl -VHBotMin=3 %ACT%
start "" "%UE%" "%PROJ%" %OPT% -VHBot=Dave -VHBotMin=3 %ACT%
goto done

:done
echo.
echo 봇 창이 뜨기까지 20~40초 걸립니다. 작은 게임 창이 봇입니다.
echo 봇은 방에 사람이 다 모이면 준비합니다. 나도 캐릭터를 고르고 [준비]를 눌러야 시작됩니다.
echo 봇 끄기: 봇 창의 X 를 누르세요. 이 창은 닫아도 됩니다.
pause
exit /b 0

:noue
echo 언리얼 엔진을 찾을 수 없습니다: %UE%
pause
exit /b 1
