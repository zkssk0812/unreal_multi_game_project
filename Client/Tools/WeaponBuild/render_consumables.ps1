# 소비 아이템 구상안 HTML 에서 그림을 뽑는다 (Edge/Chrome 화면 없는 모드).
#   powershell -ExecutionPolicy Bypass -File Tools\WeaponBuild\render_consumables.ps1
# 결과 (Saved 는 Git 제외. 에디터로 임포트한 .uasset 만 커밋한다)
#   Saved\WeaponBuild\Consumables\consumable_sheet.png             전체 시트 1600×1000
#   Saved\WeaponBuild\Consumables\T_VHC_Icon_1_HealKit.png …       HUD·정보창 아이콘 (512×512, 배경 투명)
#   Saved\WeaponBuild\Consumables\top_1_HealKit.png …              탑뷰 그림 (확인용)
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Html = Join-Path $Root "Docs\Design\Consumables_v1\consumable_concepts_v1.html"
$Out = Join-Path $Root "Saved\WeaponBuild\Consumables"
New-Item -ItemType Directory -Force $Out | Out-Null
$Browser = @(
	"C:\Program Files\Google\Chrome\Application\chrome.exe",
	"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
	"C:\Program Files\Microsoft\Edge\Application\msedge.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $Browser) { throw "Chrome 또는 Edge 를 찾지 못했습니다." }
$Url = "file:///" + ($Html -replace "\\", "/")
$Profile = Join-Path $env:TEMP "vh_render_profile"

function Shot([string]$Query, [string]$File, [int]$W, [int]$H, [bool]$Transparent) {
	$a = @("--headless=new", "--disable-gpu", "--hide-scrollbars", "--user-data-dir=$Profile", "--force-device-scale-factor=1",
		"--window-size=$W,$H", "--screenshot=$File", "--virtual-time-budget=2000")
	if ($Transparent) { $a += "--default-background-color=00000000" }
	$a += "$Url$Query"
	$p = Start-Process -FilePath $Browser -ArgumentList $a -Wait -PassThru -WindowStyle Hidden
	if (-not (Test-Path $File)) { throw "그림을 만들지 못했습니다: $File" }
	"saved $File"
}

$Names = @{ 1 = "HealKit"; 2 = "Smoke"; 3 = "Flashbang"; 4 = "Jetpack" }
Shot "" (Join-Path $Out "consumable_sheet.png") 1600 1000 $false
foreach ($c in 1..4) {
	Shot "?mode=icon&c=$c&s=128" (Join-Path $Out ("T_VHC_Icon_{0}_{1}.png" -f $c, $Names[$c])) 128 128 $true
	Shot "?mode=top&c=$c" (Join-Path $Out ("top_{0}_{1}.png" -f $c, $Names[$c])) 512 512 $true
}
