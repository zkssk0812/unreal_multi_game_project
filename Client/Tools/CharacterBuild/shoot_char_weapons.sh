#!/bin/bash
# 캐릭터별 무기 10종 촬영 (미리보기 레벨 L_CharPreview ▶ 상태): CHARS="1 2" ATTACK=2 bash shoot_char_weapons.sh
#   c{N}_w{무기}_hold.png = 든 모습(가까이), ATTACK 캐릭터는 c{N}_w{무기}_atk_close/top.png = 공격 순간(vh.WeaponTest at)
cd /c/UnrealProjects/MCPTest
PY=/c/Users/FORYOUCOM/Desktop/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe
D=Saved/CharacterBuild/charweapons
mkdir -p $D
CMD() { $PY -I Tools/WeaponBuild/vhcmd.py "$1" >/dev/null; }
CLOSE="vh.Cam 300 -14 -125 20"
CLOSECROP="2400 2200 2400 1250 0.36"
TOPCROP="1400 1000 2800 1250 0.5"
attack_time() {  # 무기별 공격 촬영 순간 (궤적·총구 불꽃·손을 떠나는 순간)
  case $1 in 1) echo 0.15;; 2) echo 0.21;; 3) echo 0.15;; 4) echo 0.05;; 5) echo 0.4;; 6) echo 0.2;; 7) echo 0.06;; 8) echo 0.29;; 9) echo 0.35;; 10) echo 0.35;; esac
}
CMD "vh.Ring 0 | vh.Aim 90 | vh.WeaponTest off | vh.Down off"
for c in ${CHARS:-1 2}; do
  CMD "vh.Char $c"
  sleep 1
  for w in ${WEAPONS:-1 2 3 4 5 6 7 8 9 10}; do
    CMD "vh.WeaponTest off | $CLOSE | vh.Weapon $w"
    sleep 1.2
    WAIT=1 bash Tools/CharacterBuild/gshot.sh "vh.Aim 90" "$D/c${c}_w${w}_hold.png" $CLOSECROP >/dev/null
    if [ "$c" = "${ATTACK:-2}" ]; then
      t=$(attack_time $w)
      for mode in close top; do
        if [ $mode = close ]; then CAM=$CLOSE; CROP=$CLOSECROP; else CAM="vh.Cam off"; CROP=$TOPCROP; fi
        CMD "vh.WeaponTest off | $CAM | vh.Weapon $w"
        sleep ${PRESLEEP:-1}
        WAIT=1.6 bash Tools/CharacterBuild/gshot.sh "vh.WeaponTest $w at $t" "$D/c${c}_w${w}_atk_${mode}.png" $CROP >/dev/null
      done
      [ $w = 9 ] || [ $w = 10 ] && sleep 3   # 지뢰 타이머
    fi
    echo "c$c w$w"
  done
done
CMD "vh.WeaponTest off | vh.Cam off | vh.Weapon 0"
