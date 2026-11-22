# 상자 자리 제안 그림: 맵 JSON(벽·방·금고 자리) + box_slots_proposal.csv → PNG
# 그림 방향은 Docs/Mansion_1F_배치안.png와 같다: 그림 위쪽 = 게임 화면 위쪽(W), 왼쪽 = 월드 +X, 위쪽 = 월드 +Y(0)
param(
    [string]$Map = "$PSScriptRoot\..\..\Docs\ServerHandoff\mansion_1f_server.json",
    [string]$Slots = "$PSScriptRoot\box_slots_proposal.csv",
    [string]$Out = "$PSScriptRoot\..\..\Docs\ServerHandoff\상자_자리_제안.png"
)
Add-Type -AssemblyName System.Drawing
$d = Get-Content $Map -Raw -Encoding UTF8 | ConvertFrom-Json
$boxes = Import-Csv $Slots -Encoding UTF8

$S = 0.17; $L = 90; $T = 120; $PANEL = 300
function PX([double]$x) { $L + (9788 - $x) * $S }
function PY([double]$y) { $T + (10 - $y) * $S }
function C($a, $r, $g, $b) { [Drawing.Color]::FromArgb($a, $r, $g, $b) }
function F($size, $bold = $true) { New-Object Drawing.Font("Malgun Gothic", $size, $(if ($bold) { [Drawing.FontStyle]::Bold } else { [Drawing.FontStyle]::Regular }), [Drawing.GraphicsUnit]::Pixel) }
function RectOf($xmin, $xmax, $ymin, $ymax) { New-Object Drawing.RectangleF((PX $xmax), (PY $ymax), (($xmax - $xmin) * $S), (($ymax - $ymin) * $S)) }

$W = [int]($L + 9800 * $S + 40); $mapH = [int](6900 * $S)
$bmp = New-Object Drawing.Bitmap($W, ($T + $mapH + $PANEL))
$g = [Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = 'AntiAlias'; $g.TextRenderingHint = 'AntiAliasGridFit'
$g.Clear([Drawing.Color]::White)
$sfC = New-Object Drawing.StringFormat; $sfC.Alignment = 'Center'; $sfC.LineAlignment = 'Center'
$ink = New-Object Drawing.SolidBrush(C 255 30 40 70)
$gray = New-Object Drawing.SolidBrush(C 255 95 100 115)

# 방별 상자 수, 금고 방
$count = @{}; foreach ($b in $boxes) { $count[$b.room] = 1 + [int]$count[$b.room] }
$vaultRoom = @{}; foreach ($v in $d.vault_slots) { $vaultRoom[$v.room] = $true }

# 제목
$g.DrawString("상자 자리 제안 — 저택 1층 (L_Mansion_1F)", (F 30), $ink, 24, 18)
$g.DrawString("총 $($boxes.Count)곳 · 방마다 0~2개 · 복도 없음 · 그림 위쪽 = 게임 화면 위쪽(W) · 격자 1칸 = 5m · 제안(레벨 배치 전)", (F 17 $false), $gray, 26, 66)

# 바닥(복도 색) → 방 → 벽
$floorB = New-Object Drawing.SolidBrush(C 255 236 230 212)
foreach ($f in $d.floors) { if ($f.id -ne 'floor_exit') { $g.FillRectangle($floorB, (RectOf $f.xmin $f.xmax $f.ymin $f.ymax)) } }
$b0 = New-Object Drawing.SolidBrush(C 255 222 226 233)      # 상자 0개
$b1 = New-Object Drawing.SolidBrush(C 255 196 232 205)      # 1개
$b2 = New-Object Drawing.SolidBrush(C 255 128 205 150)      # 2개
$hall = New-Object Drawing.SolidBrush(C 255 214 226 246)
$hatch = New-Object Drawing.Drawing2D.HatchBrush('WideUpwardDiagonal', (C 200 215 60 60), (C 255 248 226 226))
foreach ($r in $d.rooms) {
    $rc = RectOf $r.xmin $r.xmax $r.ymin $r.ymax
    $n = [int]$count[$r.id]
    $br = $(if ($r.name -match '문 없음') { $hatch } elseif ($n -ge 2) { $b2 } elseif ($n -eq 1) { $b1 } elseif ($r.id -eq 'Room_15') { $hall } else { $b0 })
    $g.FillRectangle($br, $rc)
}
# 격자 5m
$penGrid = New-Object Drawing.Pen((C 28 0 0 0), 1)
for ($x = 0; $x -le 9500; $x += 500) { $g.DrawLine($penGrid, (PX $x), (PY 10), (PX $x), (PY -6885)) }
for ($y = 0; $y -ge -6500; $y -= 500) { $g.DrawLine($penGrid, (PX 9788), (PY $y), (PX -10), (PY $y)) }
$wallB = New-Object Drawing.SolidBrush(C 255 45 50 70)
foreach ($c in $d.colliders.static) {
    if ($c.kind -ne 'wall') { continue }
    $rc = RectOf $c.xmin $c.xmax $c.ymin $c.ymax
    if ($rc.Width -lt 4) { $rc.X -= (4 - $rc.Width) / 2; $rc.Width = 4 }
    if ($rc.Height -lt 4) { $rc.Y -= (4 - $rc.Height) / 2; $rc.Height = 4 }
    $g.FillRectangle($wallB, $rc)
}
# 정문 투명 벽
$bk = $d.blockers[0]; $penBk = New-Object Drawing.Pen((C 255 230 0 140), 4); $penBk.DashStyle = 'Dash'
$g.DrawLine($penBk, (PX $bk.xmax), (PY -383), (PX $bk.xmin), (PY -383))

# 방 번호 (+ 상자 수)
foreach ($r in $d.rooms) {
    $cx = ((PX $r.xmin) + (PX $r.xmax)) / 2; $cy = ((PY $r.ymin) + (PY $r.ymax)) / 2
    $no = ($r.name -match '문 없음'); $n = [int]$count[$r.id]
    if ($r.id -eq 'Room_15') { $cy -= 110 }     # 시작 지점 점과 겹치지 않게 위로
    if ($no) { $g.FillRectangle((New-Object Drawing.SolidBrush(C 235 255 255 255)), ($cx - 38), ($cy - 28), 76, 56) }
    $col = $(if ($no) { C 255 190 30 30 } else { C 255 30 50 110 })
    $g.DrawString("$($r.no)", (F 24), (New-Object Drawing.SolidBrush($col)), $cx, ($cy - 10), $sfC)
    $sub = $(if ($no) { "문 없음" } elseif ($r.id -eq 'Room_15') { "큰 홀 · 시작 지점 · 상자 0" } elseif ($r.id -eq 'Room_03') { "현관 · 0" } elseif ($n -gt 0) { "상자 $n" } elseif ($vaultRoom[$r.id]) { "금고 방 · 0" } else { "통로 방 · 0" })
    $fs = $(if ($n -gt 0) { 16 } else { 13 })
    $sc = $(if ($n -gt 0) { C 255 10 105 50 } elseif ($no) { C 255 190 30 30 } else { C 255 95 100 115 })
    $g.DrawString($sub, (F $fs ($n -gt 0)), (New-Object Drawing.SolidBrush($sc)), $cx, ($cy + 14), $sfC)
}

# 시작 지점
# (주의: PowerShell 변수는 대소문자를 구분하지 않는다. 배율 $S와 겹치지 않게 $sp를 쓴다)
foreach ($sp in $d.spawns.thief) { $g.FillEllipse((New-Object Drawing.SolidBrush(C 255 0 150 220)), ((PX $sp.pos[0]) - 6), ((PY $sp.pos[1]) - 6), 12, 12) }

# 금고 자리
function Icon($cx, $cy, $text, $fill, $edge, $half, $fsize, $textBrush) {
    $g.FillRectangle((New-Object Drawing.SolidBrush($fill)), ($cx - $half), ($cy - $half), (2 * $half), (2 * $half))
    $g.DrawRectangle((New-Object Drawing.Pen($edge, 2.5)), ($cx - $half), ($cy - $half), (2 * $half), (2 * $half))
    $g.DrawString($text, (F $fsize), $textBrush, $cx, ($cy + 1), $sfC)
}
foreach ($v in $d.vault_slots) {
    Icon (PX $v.pos[0]) (PY $v.pos[1]) ($v.client -replace 'Safe_0?', '') (C 255 240 160 20) (C 255 125 75 0) 13 15 ([Drawing.Brushes]::Black)
}
# 상자 자리 (실제 크기 1m 원 + 번호 아이콘)
$boxFill = C 255 20 135 70; $boxEdge = C 255 5 70 30
foreach ($b in $boxes) {
    $cx = PX ([double]$b.x); $cy = PY ([double]$b.y)
    Icon $cx $cy ($b.client -replace 'Box_0?', '') $boxFill $boxEdge 14 16 ([Drawing.Brushes]::White)
}

# 범례 + 방별 표
$py = $T + $mapH + 26
$g.DrawLine((New-Object Drawing.Pen((C 255 30 40 70), 2)), 0, ($py - 12), $W, ($py - 12))
Icon 44 ($py + 16) "1" $boxFill $boxEdge 14 16 ([Drawing.Brushes]::White)
$g.DrawString("상자 자리 1~$($boxes.Count) (벽에서 80cm 띄운 구석, 상자 지름 1m)", (F 18), $ink, 70, ($py + 4))
Icon 574 ($py + 16) "1" (C 255 240 160 20) (C 255 125 75 0) 13 15 ([Drawing.Brushes]::Black)
$g.DrawString("금고 후보 1~10 (기존)", (F 18), $ink, 598, ($py + 4))
$g.FillEllipse((New-Object Drawing.SolidBrush(C 255 0 150 220)), 852, ($py + 10), 12, 12)
$g.DrawString("시작 지점", (F 18), $ink, 872, ($py + 4))
$lx = 1010
foreach ($it in @(@($b2, "상자 2개 방"), @($b1, "상자 1개 방"), @($b0, "상자 0개 방"), @($hatch, "문 없는 방"))) {
    $g.FillRectangle($it[0], $lx, ($py + 4), 30, 24); $g.DrawRectangle((New-Object Drawing.Pen((C 255 120 125 140), 1)), $lx, ($py + 4), 30, 24)
    $g.DrawString($it[1], (F 17), $ink, ($lx + 36), ($py + 5)); $lx += 170
}
function RoomList($n) { (($d.rooms | Where-Object { [int]$count[$_.id] -eq $n -and $_.name -notmatch '문 없음' } | ForEach-Object { $_.no }) -join ", ") }
$r2 = RoomList 2; $r1 = RoomList 1
$n2 = ($d.rooms | Where-Object { [int]$count[$_.id] -eq 2 }).Count; $n1 = ($d.rooms | Where-Object { [int]$count[$_.id] -eq 1 }).Count
$vr = (($d.rooms | Where-Object { $vaultRoom[$_.id] } | ForEach-Object { $_.no }) -join ", ")
$rows = @(
    @("2개", "방 $r2", "$($n2)곳 × 2 = $($n2 * 2)개", "넓고 문에서 먼 구석이 두 군데 있는 방"),
    @("1개", "방 $r1", "$($n1)곳 × 1 = $($n1)개", "금고 후보가 없는 방 — 문 반대편 구석"),
    @("0개", "방 $vr", "금고 후보 방 10곳", "금고가 뜨는 방은 금고에 집중(상자와 겹치지 않게)"),
    @("0개", "방 15(큰 홀) · 3(현관) · 9 · 22(통로 방)", "4곳", "시작 지점과 그 앞, 지나가는 방은 비움"),
    @("0개", "방 11, 13, 20", "문 없는 방 3곳", "들어갈 수 없음")
)
$ty = $py + 52
foreach ($row in $rows) {
    $g.DrawString($row[0], (F 18), $ink, 30, $ty)
    $g.DrawString($row[1], (F 18), $ink, 92, $ty)
    $g.DrawString($row[2], (F 17 $false), $ink, 640, $ty)
    $g.DrawString($row[3], (F 17 $false), $gray, 870, $ty)
    $ty += 34
}
$g.DrawString("합계 $($boxes.Count)곳 (서버의 지금 상자 수 20개와 같음) · 상자는 부서진 같은 자리에 30~60초 뒤 다시 생김", (F 19), (New-Object Drawing.SolidBrush(C 255 10 105 50)), 30, ($ty + 8))

$bmp.Save($Out, [Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
"저장: $Out ($W x $($T + $mapH + $PANEL))"

# ── 검사: 방 안인지, 벽·소품 충돌체와의 거리, 문·금고와의 거리 ──
$bad = 0
foreach ($b in $boxes) {
    $x = [double]$b.x; $y = [double]$b.y
    $r = $d.rooms | Where-Object id -eq $b.room
    $inRoom = ($x -ge $r.xmin + 75 -and $x -le $r.xmax - 75 -and $y -ge $r.ymin + 75 -and $y -le $r.ymax - 75)
    $minC = 1e9; $near = ''
    foreach ($c in $d.colliders.static) {
        $dx = [Math]::Max([Math]::Max($c.xmin - $x, 0), $x - $c.xmax); $dy = [Math]::Max([Math]::Max($c.ymin - $y, 0), $y - $c.ymax)
        $dist = [Math]::Sqrt($dx * $dx + $dy * $dy); if ($dist -lt $minC) { $minC = $dist; $near = $c.id }
    }
    $minV = ($d.vault_slots | ForEach-Object { [Math]::Sqrt([Math]::Pow($_.pos[0] - $x, 2) + [Math]::Pow($_.pos[1] - $y, 2)) } | Measure-Object -Minimum).Minimum
    $minB = ($boxes | Where-Object { $_.id -ne $b.id } | ForEach-Object { [Math]::Sqrt([Math]::Pow([double]$_.x - $x, 2) + [Math]::Pow([double]$_.y - $y, 2)) } | Measure-Object -Minimum).Minimum
    $ok = $inRoom -and $minC -ge 70 -and $minV -ge 300 -and $minB -ge 300
    if (-not $ok) { $bad++ }
    "{0} {1} ({2},{3}) 방 안={4} 가장 가까운 충돌체={5:N0}cm({6}) 금고={7:N0}cm 다른 상자={8:N0}cm {9}" -f $b.client, $b.room, $x, $y, $inRoom, $minC, $near, $minV, $minB, $(if ($ok) { 'OK' } else { '확인!' })
}
"검사: $($boxes.Count)곳 중 문제 $($bad)곳"
