#!/bin/bash
# 근접 3종 든 모습 + 공격 연속 프레임 (게임 레벨 ▶ 상태). 내 캐릭터 + 시험용 다른 플레이어 표시(vh.ProxyTest)를 나란히.
# 결과: Saved/CharacterBuild/shots/m{무기}_{프레임}_{close|top}.png
cd /c/UnrealProjects/MCPTest
D=Saved/CharacterBuild/shots
FR="hold 0.10 0.17 0.21 0.24 0.28 0.31"
for w in ${WEAPONS:-1 2 3}; do
  # 빔 소드는 키 시간 × 0.75
  scale=$([ $w = 3 ] && echo 0.75 || echo 1)
  for mode in close top; do
    if [ $mode = close ]; then CAM="vh.Cam 470 -16 -125 20"; else CAM="vh.Cam off"; fi
    first=1
    for f in $FR; do
      if [ $f = hold ]; then CMD="vh.WeaponAnim off | vh.Ring 0 | vh.Aim 90 | vh.Weapon $w | vh.ProxyTest $w -200 0 | vh.Weapon $w"
      else t=$(awk "BEGIN{printf \"%.3f\", $f*$scale}"); CMD="vh.WeaponAnim $w $t"; fi
      if [ $mode = close ]; then CROP="3400 2200 2150 1200 0.36"; else CROP="1200 1100 2556 1350 0.5"; fi
      WAIT=$([ $first = 1 ] && echo 2.5 || echo 1) bash Tools/CharacterBuild/gshot.sh "$CAM | $CMD" "$D/m${w}_${f}_${mode}.png" $CROP >/dev/null
      first=0
    done
    echo "w$w $mode"
  done
done
/c/Users/FORYOUCOM/Desktop/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe -I Tools/WeaponBuild/vhcmd.py "vh.WeaponAnim off" >/dev/null
