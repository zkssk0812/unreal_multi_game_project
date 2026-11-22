# 캐릭터 무기 10종 비교 이미지 (촬영 원본: Saved/CharacterBuild/charweapons, shoot_char_weapons.sh)
#   기본(11-20): 도적1 들기 · 도적2 들기 · 도적2 공격 → Docs/Design/Characters_v2/02·03
#   11-21: -Char 3 -OutDir ...\Characters_v3 -Prefix 02  (도적1 들기 · 도적3 들기 · 도적3 공격)
param([int]$Char = 2, [string]$OutDir = "C:\UnrealProjects\MCPTest\Docs\Design\Characters_v2", [string]$CloseName = "02_weapons_close.png", [string]$TopName = "03_weapons_top.png")
$s = "C:\UnrealProjects\MCPTest\Saved\CharacterBuild\charweapons"
$m = "C:\UnrealProjects\MCPTest\Tools\WeaponBuild\montage.ps1"
New-Item -ItemType Directory -Force $OutDir | Out-Null
$wn = "삼단봉", "도검", "빔 소드", "권총", "화염방사기", "레일건", "테이저건", "수류탄", "폭발 지뢰", "전기 지뢰"
$files = @(); $labels = @()
foreach ($row in "c1_hold", "c$($Char)_hold", "c$($Char)_atk_close") {
    for ($w = 1; $w -le 10; $w++) {
        $p = $row.Split("_")
        $files += "$s\$($p[0])_w$($w)_$($row.Substring(3)).png"
        $who = "도적" + $p[0].Substring(1)
        $what = if ($row -like "*atk*") { "공격" } else { "들기" }
        $labels += "$who $what · $w $($wn[$w-1])"
    }
}
& $m -Out "$OutDir\$CloseName" -Cols 10 -Title "무기 10종 가까이: 위 = 도적1 든 모습, 가운데 = 도적$Char 든 모습, 아래 = 도적$Char 공격 순간 (손 소켓·왼손 위치는 캐릭터별 보정 없이 맞음)" -Files $files -Labels $labels
$files = @(); $labels = @()
for ($w = 1; $w -le 10; $w++) { $files += "$s\c$($Char)_w$($w)_atk_top.png"; $labels += "도적$Char · $w $($wn[$w-1])" }
& $m -Out "$OutDir\$TopName" -Cols 5 -Title "도적$Char 무기 10종 공격 순간 (탑뷰, 게임 카메라)" -Files $files -Labels $labels
