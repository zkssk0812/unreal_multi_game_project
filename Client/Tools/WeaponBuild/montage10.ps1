param([string]$Prefix, [string]$Out, [string]$Title = "")
$d = "C:\UnrealProjects\MCPTest\Saved\WeaponBuild\Anim"
$files = 1..10 | ForEach-Object { Join-Path $d ("{0}_{1}.png" -f $Prefix, $_) }
$labels = "01 삼단봉","02 도검","03 빔 소드","04 권총","05 화염방사기","06 레일건","07 테이저건","08 수류탄","09 폭발 지뢰","10 감전 지뢰"
& "C:\UnrealProjects\MCPTest\Tools\WeaponBuild\montage.ps1" -Out (Join-Path $d $Out) -Cols 5 -Files $files -Labels $labels -Title $Title
