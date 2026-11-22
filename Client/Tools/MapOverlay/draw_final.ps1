# 팀 공유용 최종 배치안: level_export.csv(레벨에서 MCP로 읽은 실제 위치, 평면도 좌표 m) → PNG
# 평면도 위쪽 = 게임 화면 위쪽(W). 평면도 축 픽셀: x=-100 → 80px, x=0 → 1727px, y=0 → 45px, y=-70 → 1197px
param(
    [string]$Plan = "$PSScriptRoot\..\..\Docs\Mansion_1F_평면도.png",
    [string]$Data = "$PSScriptRoot\level_export.csv",
    [string]$Out = "$PSScriptRoot\..\..\Docs\Mansion_1F_배치안.png"
)
Add-Type -AssemblyName System.Drawing
$HEAD = 110
function PX([double]$x) { 80 + ($x + 100) * 16.47 }
function PY([double]$y) { $HEAD + 45 + (-$y) * 16.457 }
function C($a, $r, $g, $b) { [Drawing.Color]::FromArgb($a, $r, $g, $b) }
function F($size, $bold = $true) { New-Object Drawing.Font("Malgun Gothic", $size, $(if ($bold) { [Drawing.FontStyle]::Bold } else { [Drawing.FontStyle]::Regular }), [Drawing.GraphicsUnit]::Pixel) }

$src = [Drawing.Image]::FromFile((Resolve-Path $Plan))
$W = $src.Width
$mapH = $src.Height
$panelH = 600
$bmp = New-Object Drawing.Bitmap($W, ($HEAD + $mapH + $panelH))
$g = [Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = 'AntiAlias'; $g.TextRenderingHint = 'AntiAliasGridFit'
$g.Clear([Drawing.Color]::White)
$g.DrawImage($src, 0, $HEAD, $W, $mapH)

$cStart = C 255 0 160 70; $cExit = C 255 0 120 230; $cWall = C 255 230 0 140; $cJail = C 255 130 50 190
$cSafe = C 255 235 150 0; $cHide = C 255 120 75 30; $cGuard = C 255 215 30 30; $cRoom = C 255 25 60 140; $cNo = C 255 200 25 25
$white = [Drawing.Brushes]::White
$halo = New-Object Drawing.SolidBrush(C 225 255 255 255)
$sfC = New-Object Drawing.StringFormat; $sfC.Alignment = 'Center'; $sfC.LineAlignment = 'Center'

# ── 기호 그리기 ──
function Star($cx, $cy, $r, $col) {
    $pts = New-Object 'System.Collections.Generic.List[Drawing.PointF]'
    for ($i = 0; $i -lt 10; $i++) { $rr = $(if ($i % 2 -eq 0) { $r } else { $r * 0.45 }); $a = -[Math]::PI / 2 + $i * [Math]::PI / 5
        $pts.Add((New-Object Drawing.PointF(($cx + $rr * [Math]::Cos($a)), ($cy + $rr * [Math]::Sin($a))))) }
    $g.FillPolygon((New-Object Drawing.SolidBrush($col)), $pts.ToArray()); $g.DrawPolygon((New-Object Drawing.Pen([Drawing.Color]::White, 2)), $pts.ToArray())
}
function SafeIcon($cx, $cy, $n) {
    $r = New-Object Drawing.RectangleF(($cx - 17), ($cy - 17), 34, 34)
    $g.FillRectangle((New-Object Drawing.SolidBrush($cSafe)), $r); $g.DrawRectangle((New-Object Drawing.Pen([Drawing.Color]::FromArgb(255, 120, 70, 0), 2.5)), $r.X, $r.Y, $r.Width, $r.Height)
    $g.DrawString("$n", (F 19), [Drawing.Brushes]::Black, $cx, ($cy + 1), $sfC)
}
function HideIcon($cx, $cy, $n) {
    $pts = [Drawing.PointF[]]@((New-Object Drawing.PointF($cx, ($cy - 19))), (New-Object Drawing.PointF(($cx + 18), ($cy + 14))), (New-Object Drawing.PointF(($cx - 18), ($cy + 14))))
    $g.FillPolygon((New-Object Drawing.SolidBrush($cHide)), $pts); $g.DrawPolygon((New-Object Drawing.Pen([Drawing.Color]::White, 2)), $pts)
    $g.DrawString("$n", (F 15), $white, $cx, ($cy + 4), $sfC)
}
function GuardIcon($cx, $cy) {
    $g.FillEllipse((New-Object Drawing.SolidBrush($cGuard)), ($cx - 17), ($cy - 17), 34, 34); $g.DrawEllipse((New-Object Drawing.Pen([Drawing.Color]::White, 2)), ($cx - 17), ($cy - 17), 34, 34)
    $g.DrawString("경", (F 17), $white, $cx, ($cy + 1), $sfC)
}
function Label($text, $x, $y, $col, $size = 20, $align = "left") {
    $f = F $size; $sz = $g.MeasureString($text, $f)
    $lx = $(if ($align -eq "right") { $x - $sz.Width } elseif ($align -eq "center") { $x - $sz.Width / 2 } else { $x })
    $g.FillRectangle($halo, $lx, ($y - $sz.Height / 2), $sz.Width, $sz.Height)
    $g.DrawString($text, $f, (New-Object Drawing.SolidBrush($col)), $lx, ($y - $sz.Height / 2))
}

$rows = Import-Csv $Data -Encoding UTF8

# ── 방 ──
$fillRoom = New-Object Drawing.SolidBrush(C 60 60 130 255)
$penRoom = New-Object Drawing.Pen((C 200 40 100 220), 2.5)
$hatch = New-Object Drawing.Drawing2D.HatchBrush('WideUpwardDiagonal', (C 170 220 40 40), (C 45 220 40 40))
$penNo = New-Object Drawing.Pen($cNo, 3.5)
foreach ($r in ($rows | Where-Object kind -eq "room")) {
    $rx = PX ([double]$r.x1); $ry = PY ([double]$r.ytop); $rw = (PX ([double]$r.x2)) - $rx; $rh = (PY ([double]$r.ybot)) - $ry
    if ($r.allow -eq "0") { $g.FillRectangle($hatch, $rx, $ry, $rw, $rh); $g.DrawRectangle($penNo, $rx, $ry, $rw, $rh) }
    else { $g.FillRectangle($fillRoom, $rx, $ry, $rw, $rh); $g.DrawRectangle($penRoom, $rx, $ry, $rw, $rh) }
}
foreach ($r in ($rows | Where-Object kind -eq "room")) {
    $n = [int]($r.label -replace "Room_", "")
    $cx = ((PX ([double]$r.x1)) + (PX ([double]$r.x2))) / 2
    # 오브젝트 기호와 겹치지 않게 방 번호는 방 위에서 40% 지점에
    $frac = $(if ($r.label -eq "Room_15") { 0.22 } else { 0.4 })	# 큰 홀은 가운데 시작 표시와 겹치지 않게 더 위에
    $cy = (PY ([double]$r.ytop)) + ((PY ([double]$r.ybot)) - (PY ([double]$r.ytop))) * $frac
    $col = $(if ($r.allow -eq "0") { $cNo } else { $cRoom })
    $f = F 22; $t = "$n"; $sz = $g.MeasureString($t, $f)
    $g.FillEllipse($halo, ($cx - $sz.Width / 2 - 5), ($cy - $sz.Height / 2 - 1), ($sz.Width + 10), ($sz.Height + 2))
    $g.DrawString($t, $f, (New-Object Drawing.SolidBrush($col)), $cx, $cy, $sfC)
    if ($r.allow -eq "0") { Label "문 없음" $cx ($cy + 30) $cNo 17 "center" }
    elseif ($r.name -notmatch "^방 \d+$") { Label $r.name $cx ($cy + 28) $cRoom 18 "center" }
}

# ── 정문 투명 벽 ──
$wr = $rows | Where-Object kind -eq "wall"
$penWall = New-Object Drawing.Pen($cWall, 7); $penWall.DashStyle = 'Dash'
$g.DrawLine($penWall, (PX ([double]$wr.x1)), (PY ([double]$wr.y)), (PX ([double]$wr.x2)), (PY ([double]$wr.y)))
Label "정문 투명 벽(막힘)" ((PX ([double]$wr.x2)) + 14) ((PY ([double]$wr.y)) - 8) $cWall 20

# ── 탈출구 + 바깥 바닥판 ──
$ef = $rows | Where-Object kind -eq "exitfloor"; $eg = $rows | Where-Object kind -eq "exit"
$g.FillRectangle((New-Object Drawing.SolidBrush(C 255 140 195 245)), (PX ([double]$ef.x1)), (PY ([double]$ef.ytop)), ((PX ([double]$ef.x2)) - (PX ([double]$ef.x1))), ((PY ([double]$ef.ybot)) - (PY ([double]$ef.ytop))))
# 바닥판이 평면도의 세로축 눈금 "-45"를 덮으므로 왼쪽에 다시 쓴다
$tickF = New-Object Drawing.Font("Malgun Gothic", 15, [Drawing.FontStyle]::Regular, [Drawing.GraphicsUnit]::Pixel)
$tsz = $g.MeasureString("-45", $tickF)
$g.DrawString("-45", $tickF, [Drawing.Brushes]::Black, ((PX ([double]$ef.x1)) - $tsz.Width - 2), ((PY -45) - $tsz.Height / 2))
$g.DrawLine((New-Object Drawing.Pen($cExit, 9)), (PX ([double]$eg.x)), (PY ([double]$eg.ytop)), (PX ([double]$eg.x)), (PY ([double]$eg.ybot)))
$ax = PX ([double]$ef.x1); $ay = PY ([double]$eg.y)
$pa = New-Object Drawing.Pen($cExit, 5); $pa.EndCap = 'ArrowAnchor'
$g.DrawLine($pa, ($ax + 44), $ay, ($ax + 10), $ay)
Label "탈출구 (서쪽 옆문)" ((PX ([double]$eg.x)) + 14) ($ay + 42) $cExit 20

# ── 창고(감옥 칸 + 창고 문) ──
$jr = $rows | Where-Object kind -eq "jail"
$jx = PX ([double]$jr.x1); $jy = PY ([double]$jr.ytop); $jw = (PX ([double]$jr.x2)) - $jx; $jh = (PY ([double]$jr.ybot)) - $jy
$g.FillRectangle((New-Object Drawing.SolidBrush(C 110 130 50 190)), $jx, $jy, $jw, $jh)
$g.DrawRectangle((New-Object Drawing.Pen($cJail, 3)), $jx, $jy, $jw, $jh)
$g.DrawLine((New-Object Drawing.Pen($cJail, 8)), $jx, $jy, ($jx + $jw), $jy)
Label "창고 문" ($jx + $jw + 10) ($jy) $cJail 19
Label "창고(감옥)" ($jx + $jw + 10) ($jy + 28) $cJail 17

# ── 시작 ──
$st = $rows | Where-Object kind -eq "start"
Star (PX ([double]$st.x)) (PY ([double]$st.y)) 24 $cStart
Label "시작 (빈 거대 금고)" ((PX ([double]$st.x)) + 30) (PY ([double]$st.y)) $cStart 21

# ── 금고·숨는 곳·경비원 ──
foreach ($r in ($rows | Where-Object kind -eq "safe")) { SafeIcon (PX ([double]$r.x)) (PY ([double]$r.y)) ([int]($r.label -replace "Safe_", "")) }
foreach ($r in ($rows | Where-Object kind -eq "hide")) { HideIcon (PX ([double]$r.x)) (PY ([double]$r.y)) ([int]($r.label -replace "HideSpot_", "")) }
$gd = $rows | Where-Object kind -eq "guard"
GuardIcon (PX ([double]$gd.x)) (PY ([double]$gd.y))
Label "더미 경비원" ((PX ([double]$gd.x)) + 24) (PY ([double]$gd.y)) $cGuard 19

# ── 제목 ──
$g.DrawString("「털어라! 금고!」 저택 1층 배치안 (L_Mansion_1F)", (F 38), (New-Object Drawing.SolidBrush(C 255 30 42 68)), 24, 14)
$g.DrawString("방향: 그림 위쪽 = 게임 화면 위쪽(W 키)  ·  레벨에 실제로 놓인 위치 기준  ·  격자 1칸 = 5m", (F 22 $false), (New-Object Drawing.SolidBrush(C 255 70 70 70)), 26, 66)

# ── 아래 패널: 범례 + 보물 표 ──
$py0 = $HEAD + $mapH
$g.FillRectangle((New-Object Drawing.SolidBrush(C 255 243 244 247)), 0, $py0, $W, $panelH)
$g.DrawLine((New-Object Drawing.Pen((C 255 30 42 68), 3)), 0, $py0, $W, $py0)
$g.DrawString("범례", (F 28), [Drawing.Brushes]::Black, 40, ($py0 + 20))
$lx = 60; $ly = $py0 + 90; $step = 50; $tf = F 22 $false; $tb = [Drawing.Brushes]::Black
Star ($lx + 10) $ly 20 $cStart; $g.DrawString("시작 지점 (큰 홀 = 비어 있는 거대한 금고)", $tf, $tb, ($lx + 45), ($ly - 15)); $ly += $step
$g.DrawLine((New-Object Drawing.Pen($cExit, 9)), ($lx + 10), ($ly - 16), ($lx + 10), ($ly + 16)); $g.FillRectangle((New-Object Drawing.SolidBrush(C 255 140 195 245)), ($lx - 14), ($ly - 16), 22, 32)
$g.DrawString("탈출구 (서쪽 옆문) + 바깥 탈출 바닥", $tf, $tb, ($lx + 45), ($ly - 15)); $ly += $step
$pw = New-Object Drawing.Pen($cWall, 7); $pw.DashStyle = 'Dash'; $g.DrawLine($pw, ($lx - 12), $ly, ($lx + 32), $ly)

$g.DrawString("정문 투명 벽 (지나갈 수 없음, 게임 중 안 보임)", $tf, $tb, ($lx + 45), ($ly - 15)); $ly += $step
$g.FillRectangle((New-Object Drawing.SolidBrush(C 110 130 50 190)), ($lx - 8), ($ly - 12), 36, 26); $g.DrawLine((New-Object Drawing.Pen($cJail, 8)), ($lx - 8), ($ly - 12), ($lx + 28), ($ly - 12))
$g.DrawString("창고(감옥 칸) — 굵은 선이 창고 문", $tf, $tb, ($lx + 45), ($ly - 15)); $ly += $step
SafeIcon ($lx + 10) $ly 1; $g.DrawString("금고 후보 1~10 (매 판 5곳만 랜덤 등장)", $tf, $tb, ($lx + 45), ($ly - 15)); $ly += $step
HideIcon ($lx + 10) $ly 1; $g.DrawString("숨는 곳(옷장) 1~4", $tf, $tb, ($lx + 45), ($ly - 15)); $ly += $step
GuardIcon ($lx + 10) $ly; $g.DrawString("더미 경비원 (테스트용, 움직이지 않음)", $tf, $tb, ($lx + 45), ($ly - 15)); $ly += $step
$g.FillRectangle($fillRoom, ($lx - 12), ($ly - 13), 44, 26); $g.DrawRectangle($penRoom, ($lx - 12), ($ly - 13), 44, 26)
$g.DrawString("방 번호 (반납지점은 방마다 최대 1곳, 항상 5곳)", $tf, $tb, ($lx + 45), ($ly - 15)); $ly += $step
$g.FillRectangle($hatch, ($lx - 12), ($ly - 13), 44, 26); $g.DrawRectangle($penNo, ($lx - 12), ($ly - 13), 44, 26)
$g.DrawString("문 없는 방 11·13·20 — 반납지점 안 나옴", $tf, $tb, ($lx + 45), ($ly - 15)); $ly += $step
$g.DrawString("칠하지 않은 곳 = 복도", (F 20 $false), (New-Object Drawing.SolidBrush(C 255 90 90 90)), ($lx + 45), ($ly - 15))

# 보물 표
$tx = 1000; $ty = $py0 + 20
$g.DrawString("금고 보물 10종", (F 28), [Drawing.Brushes]::Black, $tx, $ty)
$g.DrawString("등장한 금고 5곳에 1개씩 · 아무 5개 합계 ≥ 11,000$ (목표 10,000$)", (F 18 $false), (New-Object Drawing.SolidBrush(C 255 70 70 70)), $tx, ($ty + 40))
$treasures = @(@("은 촛대", "4kg", "2,000$"), @("진주 목걸이", "1kg", "2,100$"), @("금화 주머니", "5kg", "2,200$"), @("에메랄드 브로치", "1kg", "2,300$"), @("루비 반지", "1kg", "2,400$"),
               @("황금 회중시계", "3kg", "2,600$"), @("사파이어 왕관", "6kg", "2,800$"), @("황금 카피바라 조각상", "8kg", "3,000$"), @("금괴", "10kg", "3,200$"), @("바바라의 다이아몬드", "2kg", "3,500$"))
$rowH = 40; $c1 = $tx; $c2 = $tx + 380; $c3 = $tx + 520; $tw = 700
$hy = $ty + 78
$g.FillRectangle((New-Object Drawing.SolidBrush(C 255 30 42 68)), $c1, $hy, $tw, $rowH)
$hf = F 21; $g.DrawString("보물", $hf, $white, ($c1 + 14), ($hy + 7)); $g.DrawString("무게", $hf, $white, ($c2 + 14), ($hy + 7)); $g.DrawString("가치", $hf, $white, ($c3 + 14), ($hy + 7))
$cf = F 21 $false
for ($i = 0; $i -lt $treasures.Count; $i++) {
    $yy = $hy + $rowH * ($i + 1)
    $g.FillRectangle($(if ($i % 2 -eq 0) { [Drawing.Brushes]::White } else { New-Object Drawing.SolidBrush(C 255 232 235 242) }), $c1, $yy, $tw, $rowH)
    $g.DrawString($treasures[$i][0], $cf, $tb, ($c1 + 14), ($yy + 7)); $g.DrawString($treasures[$i][1], $cf, $tb, ($c2 + 14), ($yy + 7)); $g.DrawString($treasures[$i][2], $cf, $tb, ($c3 + 14), ($yy + 7))
}
$g.DrawRectangle((New-Object Drawing.Pen((C 255 30 42 68), 2)), $c1, $hy, $tw, ($rowH * 11))

$g.DrawString("임시 배치, 기획 확정 전  ·  2026-10-26 기준 (client/menu-ui)", (F 17 $false), (New-Object Drawing.SolidBrush(C 255 110 110 110)), 40, ($py0 + $panelH - 34))

$bmp.Save($Out, [Drawing.Imaging.ImageFormat]::Png)
"saved $Out  ($($bmp.Width)x$($bmp.Height))"
