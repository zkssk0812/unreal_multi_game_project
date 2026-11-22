#!/bin/bash
# 캐릭터별 동작 촬영 (미리보기 레벨 L_CharPreview ▶ 상태): shoot_chars.sh "1 2" → Saved/CharacterBuild/chars/c{N}_{동작}.png
#   대기 · 걷기 · 달리기 · 근접 공격(도검) · 총 공격(권총) · 맞음 · 쓰러짐. 같은 카메라라 나란히 비교할 수 있다
cd /c/UnrealProjects/MCPTest
PY=/c/Users/FORYOUCOM/Desktop/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe
D=Saved/CharacterBuild/chars
mkdir -p $D
CAM="${CAM:-vh.Cam 330 -14 -125 20}"
cmd() { $PY -I Tools/WeaponBuild/vhcmd.py "$1" >/dev/null; }
shot() { # 캐릭터 이름 명령 대기
  WAIT=$4 bash Tools/CharacterBuild/gshot.sh "$3" "$D/c$1_$2.png" 2000 2400 2556 1300 0.4 >/dev/null
}
for c in ${1:-1 2}; do
  cmd "vh.Char $c | vh.WeaponAnim off | vh.Down off | vh.Weapon 0 | vh.Aim 90 | slomo 1 | $CAM"
  sleep 1
  shot $c idle "vh.Weapon 0" 2
  shot $c walk "vh.Walk 0 1 4 0" 1.4
  sleep 4
  shot $c run "vh.Walk 0 -1 4 1" 1.4
  sleep 4
  shot $c melee "vh.Weapon 2 | vh.Aim 90 | vh.WeaponAnim 2 0.14" 1.5
  shot $c gun "vh.Weapon 4 | vh.WeaponAnim 4 0.05" 1.5
  cmd "vh.WeaponAnim off | vh.Weapon 0"
  sleep 1
  shot $c hit "vh.Body hit | slomo 0.2" ${HITWAIT:-0.9}
  cmd "slomo 1"
  sleep 2
  shot $c down "vh.Down" 2.2
  cmd "vh.Down off"
  sleep 1
  echo "캐릭터 $c 끝"
done
