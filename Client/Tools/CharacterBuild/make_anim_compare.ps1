# 애니메이션(Mixamo 연결) 비교 이미지 4장 → Docs/Design/Animations_v1  (촬영 원본: Saved/CharacterBuild/loco·body·roll, anim_guard.png)
$s = "C:\UnrealProjects\MCPTest\Saved\CharacterBuild"
$o = "C:\UnrealProjects\MCPTest\Docs\Design\Animations_v1"
$m = "C:\UnrealProjects\MCPTest\Tools\WeaponBuild\montage.ps1"
$l = "$s\loco"
& $m -Out "$o\01_locomotion_8dir.png" -Cols 5 -Title "이동 블렌드스페이스: 몸은 커서(→ 원뿔 쪽)를 보고, 이동 방향만 바꿈 (위 = 걷기 300, 아래 = 달리기 600 cm/s)" `
  -Files "$l\idle.png","$l\fwd.png","$l\bwd.png","$l\right.png","$l\left.png","$l\idle.png","$l\runfwd.png","$l\runbwd.png","$l\runright.png","$l\runleft.png" `
  -Labels "대기 (Breathing Idle)","걷기 앞","걷기 뒤","걷기 오른쪽 (카메라 쪽)","걷기 왼쪽","대기","달리기 앞","달리기 뒤","달리기 오른쪽","달리기 왼쪽"
$b = "$s\body"
& $m -Out "$o\02_body_actions.png" -Cols 5 -Title "몸 동작 몽타주 (왼쪽 = 내 캐릭터, 오른쪽 파란 링 = 다른 플레이어 표시, 권총을 든 상태)" `
  -Files "$b\hold.png","$b\hit.png","$b\pickup.png","$b\drink.png","$b\throw.png","$b\kneel.png","$b\kneelloop.png","$b\afterkneel.png","$b\stun.png","$b\down.png" `
  -Labels "권총 든 자세 (팔 IK 그대로)","맞음 (상체, 총 든 채)","줍기 (상체)","마시기 (상체)","던지기 (상체)","금고·상자 작업: 무릎 꿇기","작업 반복: 살피기","작업 끝 → 다시 총 쥐기","경직: 감전 (반복)","쓰러짐 (Dying)"
$r = "$s\roll"
& $m -Out "$o\03_roll_montage.png" -Cols 5 -Title "구르기 몽타주 (Stand To Roll, 0.5초): 커서는 → 쪽, 구르는 방향(카메라 쪽)으로 몸을 돌려 구름, 무기는 숨김" `
  -Files "$r\r1.png","$r\r2.png","$r\r3.png","$r\r4.png","$r\r5.png" -Labels "1","2","3","4","5"
& $m -Out "$o\04_guard_poses.png" -Cols 1 -Title "로봇 경비원 (오른쪽부터): 예전 대기(총 늘어뜨림) · 예전 조준 · 새 대기(Rifle Idle) · 새 순찰(Firing Rifle Walking) · 새 추적(Rifle Run) · 새 사격 · 새 감전" `
  -Files "$s\anim_guard.png" -Labels " "
