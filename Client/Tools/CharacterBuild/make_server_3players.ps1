# 실서버 봇 3명 화면 비교 (2026-11-21): 세 클라이언트(Ann=도적1, Bob=도적2, Cat=도적3) 화면 + 각 화면에서 세 사람을 확대
#   원본: Saved/V3Test/t3e_{ann,bob,cat}_03_close.png (t3_*.bat + t3_*_script.txt 로 찍음)
#   사람 위치(화면 픽셀)는 사진을 보고 적는다: $spots
param([string]$Out = "C:\UnrealProjects\MCPTest\Docs\Design\Characters_v3\05_real_server_3players.png")
Add-Type -AssemblyName System.Drawing
$src = "C:\UnrealProjects\MCPTest\Saved\V3Test"
$screens = @(
    @{ file = "t3e_ann_03_close.png"; title = "Ann 화면 (Ann=도적1 고름)"; spots = @(@{n = "Ann"; x = 640; y = 370 }, @{n = "Bob"; x = 810; y = 315 }, @{n = "Cat"; x = 478; y = 225 }) },
    @{ file = "t3e_bob_03_close.png"; title = "Bob 화면 (Bob=도적2 고름)"; spots = @(@{n = "Ann"; x = 288; y = 262 }, @{n = "Bob"; x = 400; y = 228 }, @{n = "Cat"; x = 190; y = 175 }) },
    @{ file = "t3e_cat_03_close.png"; title = "Cat 화면 (Cat=도적3 고름)"; spots = @(@{n = "Ann"; x = 818; y = 505 }, @{n = "Bob"; x = 975; y = 440 }, @{n = "Cat"; x = 640; y = 370 }) }
)
$who = @{ Ann = "도적1"; Bob = "도적2"; Cat = "도적3" }
$cellW = 640; $cellH = 360; $zoomW = 210; $zoomH = 260; $head = 34
$W = $cellW + 3 * $zoomW; $H = $head + 3 * ($cellH + 30)
$bmp = New-Object System.Drawing.Bitmap $W, $H
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.Clear([System.Drawing.Color]::FromArgb(24, 24, 28))
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
$f = New-Object System.Drawing.Font("Malgun Gothic", 13, [System.Drawing.FontStyle]::Bold)
$fs = New-Object System.Drawing.Font("Malgun Gothic", 11)
$g.DrawString("실서버(V3b) 봇 3명 — 세 화면 모두에서 Ann=도적1(검은 티·올리브 바지), Bob=도적2(금발·청바지), Cat=도적3(회색 스웨터·베이지 바지), 시작 무적 5초 안에 서로 떨어져 선 모습", $fs, [System.Drawing.Brushes]::White, 8, 8)
$row = 0
foreach ($s in $screens) {
    $img = [System.Drawing.Image]::FromFile("$src\$($s.file)")
    $k = $img.Width / 1280.0   # Bob 화면은 800x450
    $y0 = $head + $row * ($cellH + 30)
    $g.DrawString($s.title, $f, [System.Drawing.Brushes]::Gold, 6, $y0)
    $g.DrawImage($img, (New-Object System.Drawing.Rectangle 0, ($y0 + 26), $cellW, $cellH))
    $col = 0
    foreach ($p in $s.spots) {
        $cw = [int](100 * $k); $ch = [int](124 * $k)
        # spots 좌표는 그 그림 자기 픽셀 기준. 확대 칸 = 사람 발(고리) 위쪽
        $rect = New-Object System.Drawing.Rectangle ([int]($p.x - $cw / 2)), ([int]($p.y - $ch * 0.62)), $cw, $ch
        $dst = New-Object System.Drawing.Rectangle ($cellW + $col * $zoomW + 6), ($y0 + 26), ($zoomW - 12), ($zoomH)
        $g.DrawImage($img, $dst, $rect, [System.Drawing.GraphicsUnit]::Pixel)
        $g.DrawString("$($p.n) → $($who[$p.n])", $f, [System.Drawing.Brushes]::White, ($cellW + $col * $zoomW + 8), ($y0 + 26 + $zoomH + 4))
        $col++
    }
    $img.Dispose()
    $row++
}
$g.Dispose()
New-Item -ItemType Directory -Force (Split-Path $Out) | Out-Null
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
"saved $Out"
