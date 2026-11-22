#!/bin/bash
# 원거리 무기 반동 연속 프레임 촬영 (2026-11-20). 미리보기 레벨(L_CharPreview) ▶ 상태에서 실행.
#   TAG=before WEAPONS="4 5 6 7" bash Tools/WeaponBuild/shoot_recoil.sh
# 프레임마다 vh.WeaponTest N [EXTRA] at T (공격을 처음부터 재생하고 T초에 일시정지) → 옆에서 가까이 촬영
# 결과: Saved/WeaponBuild/Recoil/{TAG}_w{무기}_{번호}.png  (+ 무기별 한 줄 {TAG}_w{무기}_row.png)
cd /c/UnrealProjects/MCPTest
PY=/c/Users/FORYOUCOM/Desktop/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe
D=Saved/WeaponBuild/Recoil
mkdir -p $D
CMD() { $PY -I Tools/WeaponBuild/vhcmd.py "$1" >/dev/null; }
frames() {
  if [ -n "$FRAMES" ]; then echo "$FRAMES"; return; fi
  case $1 in
    4) echo "0.0 0.03 0.06 0.10 0.15 0.24";;       # 권총: 튐 → 복귀
    5) echo "0.30 0.36 0.42 0.48 0.54 0.60";;      # 화염방사기: 누르는 동안 떨림
    6) echo "0.0 0.05 0.12 0.25 0.45 0.80";;       # 레일건: 큰 튐 → 천천히 복귀 → 버팀
    7) echo "0.0 0.03 0.06 0.10 0.16 0.30";;       # 테이저건
    *) echo "0.0 0.05 0.1 0.2 0.4 0.8";;
  esac
}
CAM=${CAM:-"vh.Cam 230 -6 180 30"}
CROP=${CROP:-"1700 1500 2556 1150 0.4"}
CMD "vh.Ring 0 | vh.Aim 90 | vh.WeaponTest off"
for w in ${WEAPONS:-4 5 6 7}; do
  i=0; files=(); labels=()
  for t in $(frames $w); do
    CMD "vh.WeaponTest off | $CAM | vh.Weapon $w"
    sleep ${PRESLEEP:-0.8}
    WAIT=${FXWAIT:-1.4} bash Tools/CharacterBuild/gshot.sh "vh.WeaponTest $w ${EXTRA:-} at $t" "$D/${TAG:-shot}_w${w}_${i}.png" $CROP >/dev/null
    files+=("C:\\UnrealProjects\\MCPTest\\Saved\\WeaponBuild\\Recoil\\${TAG:-shot}_w${w}_${i}.png"); labels+=("${t}s")
    i=$((i+1))
  done
  echo "w$w done"
done
CMD "vh.WeaponTest off | vh.Cam off"
