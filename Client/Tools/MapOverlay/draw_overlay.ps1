# 평면도 PNG 위에 방 지역(rooms.csv)과 배치(placements.csv, 있으면)를 그린다.
# 평면도 좌표(m): 격자 그림의 x=-100 → 80px, x=0 → 1727px, y=0 → 45px, y=-70 → 1197px
# 사용: powershell -File draw_overlay.ps1 -Out ..\..\Docs\Mansion_1F_지역.png [-Placements placements.csv]
param(
    [string]$Plan = "$PSScriptRoot\..\..\Docs\Mansion_1F_평면도.png",
    [string]$Rooms = "$PSScriptRoot\rooms.csv",
    [string]$Placements = "",
    [string]$Out = "$PSScriptRoot\..\..\Docs\Mansion_1F_지역.png",
    [string]$Title = "저택 1층 방(지역) — 파랑: 반납지점 가능, 빨강 빗금: 문 없는 방(제외), 칠하지 않은 곳: 복도"
)
Add-Type -AssemblyName System.Drawing
function PX([double]$x) { 80 + ($x + 100) * 16.47 }
function PY([double]$y) { 45 + (-$y) * 16.457 }

$src = [Drawing.Image]::FromFile((Resolve-Path $Plan))
$headH = 60
$bmp = New-Object Drawing.Bitmap($src.Width, ($src.Height + $headH))
$g = [Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = 'AntiAlias'; $g.TextRenderingHint = 'AntiAliasGridFit'
$g.Clear([Drawing.Color]::White)
$g.DrawImage($src, 0, $headH, $src.Width, $src.Height)
$g.TranslateTransform(0, $headH)

$fontBig = New-Object Drawing.Font("Malgun Gothic", 20, [Drawing.FontStyle]::Bold)
$fontSm = New-Object Drawing.Font("Malgun Gothic", 11, [Drawing.FontStyle]::Bold)
$fontHead = New-Object Drawing.Font("Malgun Gothic", 16, [Drawing.FontStyle]::Bold)
$fill = New-Object Drawing.SolidBrush([Drawing.Color]::FromArgb(70, 40, 120, 255))
$edge = New-Object Drawing.Pen([Drawing.Color]::FromArgb(220, 30, 90, 220), 3)
$hatch = New-Object Drawing.Drawing2D.HatchBrush('WideUpwardDiagonal', [Drawing.Color]::FromArgb(150, 220, 40, 40), [Drawing.Color]::FromArgb(40, 220, 40, 40))
$edgeRed = New-Object Drawing.Pen([Drawing.Color]::FromArgb(230, 200, 30, 30), 3)
$txt = New-Object Drawing.SolidBrush([Drawing.Color]::FromArgb(255, 20, 40, 110))
$txtRed = New-Object Drawing.SolidBrush([Drawing.Color]::FromArgb(255, 170, 20, 20))
$halo = New-Object Drawing.SolidBrush([Drawing.Color]::FromArgb(210, 255, 255, 255))
$sf = New-Object Drawing.StringFormat; $sf.Alignment = 'Center'; $sf.LineAlignment = 'Center'

foreach ($r in (Import-Csv $Rooms -Encoding UTF8)) {
    $x = PX ([double]$r.x1); $y = PY ([double]$r.ytop)
    $w = (PX ([double]$r.x2)) - $x; $h = (PY ([double]$r.ybot)) - $y
    $rect = New-Object Drawing.RectangleF(($x + 4), ($y + 4), ($w - 8), ($h - 8))
    if ($r.closed -eq "1") { $g.FillRectangle($hatch, $rect); $g.DrawRectangle($edgeRed, $rect.X, $rect.Y, $rect.Width, $rect.Height) }
    else { $g.FillRectangle($fill, $rect); $g.DrawRectangle($edge, $rect.X, $rect.Y, $rect.Width, $rect.Height) }
    $cx = $x + $w / 2; $cy = $y + $h / 2
    $label = "$($r.n)"
    $sz = $g.MeasureString($label, $fontBig)
    $g.FillEllipse($halo, ($cx - $sz.Width / 2 - 4), ($cy - $sz.Height / 2 - 2), ($sz.Width + 8), ($sz.Height + 4))
    $g.DrawString($label, $fontBig, $(if ($r.closed -eq "1") { $txtRed } else { $txt }), $cx, $cy, $sf)
    if ($r.name -notmatch "^방 \d+$") { $g.DrawString($r.name, $fontSm, $txt, $cx, ($cy + 24), $sf) }
    if ($r.closed -eq "1") { $g.DrawString("문 없음", $fontSm, $txtRed, $cx, ($cy + 24), $sf) }
}

if ($Placements -and (Test-Path $Placements)) {
    $colors = @{ "start" = [Drawing.Color]::FromArgb(255, 0, 170, 80); "exit" = [Drawing.Color]::FromArgb(255, 0, 150, 255);
                 "safe" = [Drawing.Color]::FromArgb(255, 230, 160, 0); "jail" = [Drawing.Color]::FromArgb(255, 150, 60, 200);
                 "hide" = [Drawing.Color]::FromArgb(255, 120, 80, 40); "guard" = [Drawing.Color]::FromArgb(255, 220, 30, 30) }
    $fontP = New-Object Drawing.Font("Malgun Gothic", 12, [Drawing.FontStyle]::Bold)
    foreach ($p in (Import-Csv $Placements -Encoding UTF8)) {
        $px = PX ([double]$p.x); $py = PY ([double]$p.y)
        $c = $colors[$p.kind]; if (-not $c) { $c = [Drawing.Color]::Black }
        $b = New-Object Drawing.SolidBrush($c); $pen = New-Object Drawing.Pen([Drawing.Color]::White, 2)
        $g.FillEllipse($b, ($px - 13), ($py - 13), 26, 26); $g.DrawEllipse($pen, ($px - 13), ($py - 13), 26, 26)
        $sz = $g.MeasureString($p.label, $fontP)
        $lx = $px + 15; $ly = $py - $sz.Height / 2
        if ($lx + $sz.Width -gt $src.Width - 10) { $lx = $px - 15 - $sz.Width }	# 오른쪽 끝이면 왼쪽에
        $g.FillRectangle($halo, $lx, $ly, $sz.Width, $sz.Height)
        $g.DrawString($p.label, $fontP, $b, $lx, $ly)
    }
}

$g.ResetTransform()
$g.DrawString($Title, $fontHead, [Drawing.Brushes]::Black, 20, 16)
$bmp.Save($Out, [Drawing.Imaging.ImageFormat]::Png)
"saved $Out"
