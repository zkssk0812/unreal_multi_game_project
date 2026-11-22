#!/bin/bash
# 플레이(PIE) 화면 고해상도 촬영: gshot.sh "Cmd 명령(| 로 이어도 됨)" 출력.png [가로 세로 가운데X 가운데Y 배율]   (HighResShot 3 = 5112x2586, 내 캐릭터 ≈ 2556,1320)
cd /c/UnrealProjects/MCPTest
PY=/c/Users/FORYOUCOM/Desktop/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe
L=$(ls -t Saved/Screenshots/WindowsEditor/ | head -1)
powershell -ExecutionPolicy Bypass -File Tools/CharacterBuild/fg_editor.ps1 >/dev/null
if [ -n "$1" ]; then $PY -I Tools/WeaponBuild/vhcmd.py "$1" >/dev/null; fi
sleep ${WAIT:-1}
$PY -I Tools/WeaponBuild/vhcmd.py 'HighResShot 3' >/dev/null
powershell -ExecutionPolicy Bypass -File Tools/CharacterBuild/fg_editor.ps1 >/dev/null
for i in $(seq 1 30); do [ "$(ls -t Saved/Screenshots/WindowsEditor/ | head -1)" != "$L" ] && break; sleep 2; done
sleep 2
f=$(ls -t Saved/Screenshots/WindowsEditor/ | head -1)
cp "Saved/Screenshots/WindowsEditor/$f" "Saved/CharacterBuild/last_full.png"
powershell -ExecutionPolicy Bypass -File Tools/WeaponBuild/crop.ps1 "Saved/Screenshots/WindowsEditor/$f" "$2" ${3:-900} ${4:-800} ${5:-2556} ${6:-1320} ${7:-0.8}
echo "$f -> $2"
