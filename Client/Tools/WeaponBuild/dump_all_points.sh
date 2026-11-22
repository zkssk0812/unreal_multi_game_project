#!/bin/bash
# 무기 10종 위치 기록 (PIE 중): bash Tools/WeaponBuild/dump_all_points.sh 꼬리표  → Saved/WeaponBuild/points_<꼬리표>.json
cd /c/UnrealProjects/MCPTest
PY=/c/Users/FORYOUCOM/Desktop/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe
rm -f "Saved/WeaponBuild/points_$1.json"
$PY -I Tools/WeaponBuild/vhcmd.py "vh.WeaponTest off | vh.Cam off | vh.Aim 90" >/dev/null
for n in 1 2 3 4 5 6 7 8 9 10; do
  $PY -I Tools/WeaponBuild/vhcmd.py "vh.Weapon $n" >/dev/null
  sleep 1.5
  $PY -I Tools/WeaponBuild/vhcmd.py "py \"C:/UnrealProjects/MCPTest/Tools/WeaponBuild/dump_weapon_points.py\" $1" >/dev/null
  sleep 0.5
done
echo "saved points_$1.json"
