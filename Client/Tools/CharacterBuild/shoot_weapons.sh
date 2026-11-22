#!/bin/bash
# 10종 무기 쥔 모양 촬영 (게임 레벨 ▶ 상태에서): 가까이(3/4 옆) + 탑뷰. 결과 Saved/CharacterBuild/shots/w{N}_close.png, w{N}_top.png
cd /c/UnrealProjects/MCPTest
D=Saved/CharacterBuild/shots
for n in ${@:-1 2 3 4 5 6 7 8 9 10}; do
  WAIT=2 bash Tools/CharacterBuild/gshot.sh "vh.Ring 0 | vh.Aim 90 | vh.Weapon $n | vh.Cam 210 -12 -150 30" "$D/w${n}_close.png" 2200 1800 2350 1200 0.45 >/dev/null
  WAIT=2 bash Tools/CharacterBuild/gshot.sh "vh.Cam off" "$D/w${n}_top.png" 1000 1000 2556 1293 0.45 >/dev/null
  echo "w$n"
done
