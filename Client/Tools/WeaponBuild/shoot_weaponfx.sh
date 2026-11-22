#!/bin/bash
# 무기 연출 비교 이미지용 촬영 (2026-11-19). 미리보기 레벨(L_CharPreview) ▶ 상태에서 실행.
#   WEAPONS="1 2 3" bash Tools/WeaponBuild/shoot_weaponfx.sh
# 프레임마다 vh.WeaponTest N at T (공격을 처음부터 다시 재생하고 T초에 게임 일시정지) → 탑뷰(1080p 크기)·가까이 촬영
# → 동작·궤적·맞음 이펙트가 같은 순간으로 정확히 찍힌다. 결과: Saved/WeaponBuild/FxShots/w{무기}_{번호}_{top|close}.png
cd /c/UnrealProjects/MCPTest
PY=/c/Users/FORYOUCOM/Desktop/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe
D=Saved/WeaponBuild/FxShots
mkdir -p $D
CMD() { $PY -I Tools/WeaponBuild/vhcmd.py "$1" >/dev/null; }
frames() {
  case $1 in
    1) echo "0.05 0.09 0.12 0.15 0.19 0.30";;     # 삼단봉: 들기 → 어깨 위 → 내려치는 중 → 맞음 → 따라감 → 돌아옴
    2) echo "0.08 0.17 0.21 0.25 0.31 0.40";;     # 도검: 들어 올림 → 오른쪽 끝 → 가운데 → 맞음 → 왼쪽 끝 → 돌아옴
    3) echo "0.06 0.11 0.15 0.19 0.23 0.30";;     # 빔 소드
    *) echo ${FRAMES:-"0.02 0.05 0.1 0.2 0.4 0.8"};;
  esac
}
TOPCROP=${TOPCROP:-"1500 1000 2556 1250 0.5"}
CLOSECROP="3400 2200 2150 1200 0.36"
CMD "vh.Ring 0 | vh.Aim 90"
for w in ${WEAPONS:-1 2 3}; do
  for mode in top close; do
    if [ $mode = close ]; then CAM="vh.Cam 470 -16 -125 20"; CROP=$CLOSECROP; else CAM="vh.Cam off"; CROP=$TOPCROP; fi
    i=0
    for t in $(frames $w); do
      CMD "vh.WeaponTest off | $CAM | vh.Weapon $w"
      sleep ${PRESLEEP:-0.6}   # 앞 프레임 시험의 남은 타이머(지뢰 3초 등)가 끝날 때까지
      WAIT=${FXWAIT:-1.5} bash Tools/CharacterBuild/gshot.sh "vh.WeaponTest $w ${EXTRA:-} at $t" "$D/w${w}_${i}_${mode}.png" $CROP >/dev/null
      i=$((i+1))
    done
  done
  echo "w$w done"
done
CMD "vh.WeaponTest off | vh.Cam off"
