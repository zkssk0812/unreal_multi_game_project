#!/bin/bash
# 에디터 화면 고해상도 촬영 → 잘라 저장: shot.sh 출력.png [가로 세로 가운데X 가운데Y 배율]  (에디터 화면 3408x1724 기준)
cd /c/UnrealProjects/MCPTest
PY=/c/Users/FORYOUCOM/Desktop/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe
L=$(ls -t Saved/Screenshots/WindowsEditor/ | head -1)
powershell -ExecutionPolicy Bypass -File Tools/CharacterBuild/fg_editor.ps1 >/dev/null
sleep ${WAIT:-3}
$PY -I Tools/WeaponBuild/vhcmd.py 'HighResShot 2' >/dev/null
powershell -ExecutionPolicy Bypass -File Tools/CharacterBuild/fg_editor.ps1 >/dev/null
for i in $(seq 1 60); do [ "$(ls -t Saved/Screenshots/WindowsEditor/ | head -1)" != "$L" ] && break; sleep 2; done
sleep 2
f=$(ls -t Saved/Screenshots/WindowsEditor/ | head -1)
powershell -ExecutionPolicy Bypass -File Tools/WeaponBuild/crop.ps1 "Saved/Screenshots/WindowsEditor/$f" "$1" ${2:-1700} ${3:-1500} ${4:-1704} ${5:-900} ${6:-0.55}
echo "$f -> $1"
