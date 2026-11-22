# 캐릭터 비교 이미지 (촬영 원본: Saved/CharacterBuild/chars, shoot_chars.sh)
#   기본(11-20): 도적1·2 → Docs/Design/Characters_v2/01_thief1_vs_thief2.png
#   11-21: -CharList 1,2,3 -OutDir ...\Characters_v3 -Name 01_thief1_2_3.png
param([string]$CharList = "1,2", [string]$OutDir = "C:\UnrealProjects\MCPTest\Docs\Design\Characters_v2", [string]$Name = "01_thief1_vs_thief2.png")
$Chars = $CharList -split "[, ]+" | Where-Object { $_ }
$s = "C:\UnrealProjects\MCPTest\Saved\CharacterBuild\chars"
New-Item -ItemType Directory -Force $OutDir | Out-Null
$m = "C:\UnrealProjects\MCPTest\Tools\WeaponBuild\montage.ps1"
$states = "idle", "walk", "run", "melee", "gun", "hit", "down"
$names = "대기", "걷기", "달리기", "근접 공격(도검)", "총 공격(권총)", "맞음", "쓰러짐"
$files = @(); $labels = @()
foreach ($c in $Chars) {
    for ($i = 0; $i -lt $states.Count; $i++) {
        $files += "$s\c$($c)_$($states[$i]).png"
        $labels += "도적$c · $($names[$i])"
    }
}
$who = ($Chars | ForEach-Object { "도적$_" }) -join " · "
& $m -Out "$OutDir\$Name" -Cols 7 -Title "$who (위에서부터) — 같은 스켈레톤·같은 애니메이션, 뼈 길이는 각자 메시 값" -Files $files -Labels $labels
