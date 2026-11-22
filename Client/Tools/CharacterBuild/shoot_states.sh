#!/bin/bash
# 동작별 촬영 (게임 레벨 ▶ 상태): 이름 "명령" 대기초 → Saved/CharacterBuild/shots/s_{이름}_close.png / _top.png
cd /c/UnrealProjects/MCPTest
D=Saved/CharacterBuild/shots
shot() { # 이름 명령 대기 카메라
  WAIT=$3 bash Tools/CharacterBuild/gshot.sh "$4 | $2" "$D/s_$1_close.png" 2200 1800 2350 1200 0.45 >/dev/null
}
top() {
  WAIT=$3 bash Tools/CharacterBuild/gshot.sh "vh.Cam off | $2" "$D/s_$1_top.png" 1000 1000 2556 1293 0.45 >/dev/null
}
CAM="vh.Cam 230 -12 -150 30"
for mode in close top; do
  f=$([ $mode = close ] && echo shot || echo top)
  $f idle "vh.Weapon 0 | vh.Aim 90 | vh.Down off | vh.RollFreeze off | vh.WeaponAnim off" 2 "$CAM"
  $f walk "vh.Walk 0 1 4 0" 1.2 "$CAM"
  sleep 4
  $f run "vh.Walk 0 -1 4 1" 1.2 "$CAM"
  sleep 4
  $f roll "vh.Weapon 4 | vh.RollFreeze 0.45" 1 "$CAM"
  $f attack_sword "vh.RollFreeze off | vh.Weapon 2 | vh.WeaponAnim 2 0.14" 1.5 "$CAM"
  $f attack_pistol "vh.Weapon 4 | vh.WeaponAnim 4 0.05" 1.5 "$CAM"
  $f attack_grenade "vh.Weapon 8 | vh.WeaponAnim 8 0.3" 1.5 "$CAM"
  $f down "vh.WeaponAnim off | vh.Weapon 0 | vh.Down" 2.2 "$CAM"
  /c/Users/FORYOUCOM/Desktop/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe -I Tools/WeaponBuild/vhcmd.py "vh.Down off" >/dev/null
  echo $mode
done
