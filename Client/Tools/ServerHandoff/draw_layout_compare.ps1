# 서버 기존 배치(mansion_1f_server.json)와 클라 배치(mansion_1f_layout.json)를 벽 위에 겹쳐 그린다.
# 방향: 서버 문서와 같은 탑뷰 (위 = +X, 오른쪽 = +Y). 클라 게임 화면은 이 그림을 시계 반대 방향으로 90° 돌린 모양.
# 사용: powershell -File draw_layout_compare.ps1 [-Server <json>] [-Client <json>] [-Out <png>]
param(
  [string]$Server = "$PSScriptRoot\..\..\Docs\ServerRef\Project_Rob-master\Data\Maps\mansion_1f_server.json",
  [string]$Client = "$PSScriptRoot\..\..\Docs\ServerHandoff\mansion_1f_layout.json",
  [string]$Out = "$PSScriptRoot\..\..\Docs\ServerHandoff\배치_비교.png"
)
Add-Type -AssemblyName System.Drawing
$SV = Get-Content -Raw -Encoding UTF8 $Server | ConvertFrom-Json
$CL = Get-Content -Raw -Encoding UTF8 $Client | ConvertFrom-Json

# 좌표 → 픽셀 (위 = +X, 오른쪽 = +Y)
$k = 0.14; $XMAX = 10350; $YMIN = -7000; $M = 60; $TOP = 90
function PX([double]$y) { [float]($M + ($y - $YMIN) * $k) }
function PY([double]$x) { [float]($TOP + ($XMAX - $x) * $k) }
$mapW = [int](PX 150); $mapH = [int](PY -150)
$imgW = $mapW + 560; $imgH = [Math]::Max($mapH + 40, 1500)

$bmp = New-Object Drawing.Bitmap $imgW, $imgH
$g = [Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = 'AntiAlias'; $g.TextRenderingHint = 'AntiAliasGridFit'
$g.Clear([Drawing.Color]::White)
function Col($a, $r, $gg, $b) { [Drawing.Color]::FromArgb($a, $r, $gg, $b) }
$fontT = New-Object Drawing.Font("Malgun Gothic", 17, [Drawing.FontStyle]::Bold)
$font = New-Object Drawing.Font("Malgun Gothic", 10)
$fontS = New-Object Drawing.Font("Malgun Gothic", 8)
$fontB = New-Object Drawing.Font("Malgun Gothic", 10, [Drawing.FontStyle]::Bold)

$RED = Col 255 215 40 40; $REDF = Col 60 215 40 40
$BLUE = Col 255 25 90 220; $BLUEF = Col 70 25 90 220
$GREEN = Col 255 20 150 60; $GREENF = Col 70 20 170 60
$GRAY = Col 255 70 70 70

function RectOf($b) {   # 상자 {xmin..} → 픽셀 사각형
  $l = PX $b.ymin; $r = PX $b.ymax; $t = PY $b.xmax; $bo = PY $b.xmin
  New-Object Drawing.RectangleF($l, $t, [Math]::Max(1, $r - $l), [Math]::Max(1, $bo - $t))
}
function FillBox($b, $color) { $g.FillRectangle((New-Object Drawing.SolidBrush $color), (RectOf $b)) }
function LineBox($b, $color, [float]$w = 2, [bool]$dash = $false) {
  $p = New-Object Drawing.Pen($color, $w); if ($dash) { $p.DashStyle = 'Dash' }
  $r = RectOf $b; $g.DrawRectangle($p, $r.X, $r.Y, $r.Width, $r.Height)
}
function Pt($pos) { New-Object Drawing.PointF((PX $pos[1]), (PY $pos[0])) }
function Dot($pos, $color, [float]$rad, [bool]$filled = $true) {
  $p = Pt $pos
  if ($filled) { $g.FillEllipse((New-Object Drawing.SolidBrush $color), $p.X - $rad, $p.Y - $rad, 2 * $rad, 2 * $rad) }
  else { $g.DrawEllipse((New-Object Drawing.Pen($color, 2)), $p.X - $rad, $p.Y - $rad, 2 * $rad, 2 * $rad) }
}
function Label($pos, [string]$text, $color, $f = $font, [float]$dx = 6, [float]$dy = -8) {
  $p = Pt $pos; $g.DrawString($text, $f, (New-Object Drawing.SolidBrush $color), $p.X + $dx, $p.Y + $dy)
}
function Square($pos, [double]$half, $color, $fill) {
  $b = [pscustomobject]@{ xmin = $pos[0] - $half; xmax = $pos[0] + $half; ymin = $pos[1] - $half; ymax = $pos[1] + $half }
  FillBox $b $fill; LineBox $b $color 2
}

# ── 바닥·방·벽 ──
foreach ($fl in $CL.floors) { FillBox $fl (Col 255 238 238 232) }
foreach ($rm in $CL.rooms) {
  if (-not $rm.return_point) {
    $hb = New-Object Drawing.Drawing2D.HatchBrush('BackwardDiagonal', (Col 90 120 120 120), (Col 0 0 0 0))
    $g.FillRectangle($hb, (RectOf $rm))
  }
  LineBox $rm (Col 120 25 90 220) 1 $true
  $r = RectOf $rm
  $g.DrawString(("{0}" -f $rm.no), $fontS, (New-Object Drawing.SolidBrush (Col 160 25 90 220)), $r.X + 3, $r.Y + 2)
}
foreach ($wl in ($CL.colliders.static | Where-Object { $_.kind -eq 'wall' })) { FillBox $wl $GRAY }

# ── 공통(서버 값): 순찰·경비원 ──
$nodes = @{}; foreach ($n in $SV.patrol.nodes) { $nodes[[int]$n.id] = $n }
$pp = New-Object Drawing.Pen((Col 110 150 150 150), 1.5)
foreach ($e in $SV.patrol.edges) { $a = Pt $nodes[[int]$e[0]].pos; $b = Pt $nodes[[int]$e[1]].pos; $g.DrawLine($pp, $a, $b) }
foreach ($n in $SV.patrol.nodes) { Dot $n.pos (Col 200 140 140 140) 3 }
foreach ($gs in $SV.spawns.guard) {
  $p = Pt $gs.pos
  $tri = [Drawing.PointF[]]@([Drawing.PointF]::new($p.X, $p.Y - 10), [Drawing.PointF]::new($p.X - 9, $p.Y + 7), [Drawing.PointF]::new($p.X + 9, $p.Y + 7))
  $g.FillPolygon([Drawing.Brush](New-Object Drawing.SolidBrush (Col 255 30 30 30)), [Drawing.PointF[]]$tri)
  Label $gs.pos ("경비원 " + $gs.id.Substring($gs.id.Length - 1)) (Col 255 30 30 30) $fontS 10 -6
}

# ── 서버 기존 배치 (빨강) ──
foreach ($v in $SV.vault_slots) { Square $v.pos 60 $RED $REDF; Label $v.pos ("S" + [int]$v.id.Substring(11)) $RED $fontB 8 4 }
foreach ($hs in $SV.hiding_spots) { Square $hs.pos 45 $RED (Col 0 0 0 0); Label $hs.pos ("h" + [int]$hs.id.Substring(13)) $RED $fontS 8 4 }
foreach ($t in $SV.spawns.thief) { Dot $t.pos $RED 7 $false }
foreach ($vt in $SV.vents) { Dot $vt.pos $RED 6 $false; Label $vt.pos "환풍구" $RED $fontS 8 -2 }
FillBox $SV.exit.escape_zone $REDF; LineBox $SV.exit.escape_zone $RED 2
Label $SV.exit.pos "옛 탈출구(정문)" $RED $fontB -40 6
FillBox $SV.jail.zone (Col 25 215 40 40); LineBox $SV.jail.zone $RED 2 $true
Label $SV.jail.door.pos "옛 창고(W3 전체)" $RED $fontB -60 -26
foreach ($cc in ($SV.colliders.static | Where-Object { $_.kind -eq 'blocker' })) { FillBox $cc $RED; Label @($cc.xmax, $cc.ymax) "옛 막음(②)" $RED $fontB 8 -10 }

# ── 클라 배치 (파랑, 탈출 관련 초록) ──
foreach ($cc in ($CL.colliders.dynamic | Where-Object { $_.kind -eq 'vault' })) { FillBox $cc $BLUEF; LineBox $cc $BLUE 2 }
foreach ($v in $CL.vault_slots) { Label $v.pos ("금고" + [int]$v.id.Substring(11)) $BLUE $fontB 8 -20 }
foreach ($cc in ($CL.colliders.static | Where-Object { $_.kind -eq 'wardrobe' })) { FillBox $cc $BLUEF; LineBox $cc $BLUE 2 }
foreach ($hs in $CL.hiding_spots) { Label $hs.pos ("숨기" + [int]$hs.id.Substring(12)) $BLUE $fontB 8 -20 }
foreach ($cc in ($CL.colliders.static | Where-Object { $_.kind -eq 'jail_wall' })) { FillBox $cc $BLUE }
foreach ($cc in ($CL.colliders.dynamic | Where-Object { $_.kind -eq 'jail_door' })) { FillBox $cc (Col 255 120 170 255) }
foreach ($cell in $CL.jail.cells) { Dot $cell $BLUE 4 }
Label $CL.jail.door.pos "창고(방 31 안 감옥 칸)" $BLUE $fontB -30 26
foreach ($t in $CL.spawns.thief) { Dot $t.pos $BLUE 7 }
Label $CL.spawns.thief[0].pos "시작(큰 홀)" $BLUE $fontB 10 -26
FillBox $CL.exit.escape_zone $GREENF; LineBox $CL.exit.escape_zone $GREEN 2
foreach ($fl in ($CL.floors | Where-Object { $_.id -eq 'floor_exit' })) { LineBox $fl $GREEN 1 $true }
foreach ($cc in ($CL.colliders.dynamic | Where-Object { $_.kind -eq 'exit_gate' })) { FillBox $cc $GREEN }
foreach ($cc in ($CL.colliders.static | Where-Object { $_.kind -eq 'exit_pillar' })) { FillBox $cc $GREEN }
Label $CL.exit.pos "탈출구 = 서쪽 옆문(②)" $GREEN $fontB -150 -46
foreach ($cc in ($CL.colliders.static | Where-Object { $_.kind -eq 'blocker' })) { FillBox $cc $BLUE; Label @($cc.xmin, $cc.ymin) "정문 막음(투명 벽)" $BLUE $fontB -150 40 }

# ── 제목·축·범례 ──
$g.DrawString("저택 1층 배치 비교 — 서버 기존(빨강) vs 클라 L_Mansion_1F(파랑·초록)", $fontT, (New-Object Drawing.SolidBrush (Col 255 20 20 20)), $M, 18)
$g.DrawString("좌표: 언리얼 월드 cm, 블록아웃 원점·회전 0 (서버 = 클라 같은 좌표).  그림 방향: 위 = +X, 오른쪽 = +Y (서버 문서 탑뷰)", $font, (New-Object Drawing.SolidBrush $GRAY), $M, 52)
$ap = New-Object Drawing.Pen((Col 255 0 0 0), 2); $ap.EndCap = 'ArrowAnchor'
$ox = $mapW - 70; $oy = $TOP + 80
$g.DrawLine($ap, $ox, $oy, $ox, $oy - 50); $g.DrawString("+X", $fontB, [Drawing.Brushes]::Black, $ox - 10, $oy - 72)
$g.DrawLine($ap, $ox, $oy, $ox + 50, $oy); $g.DrawString("+Y", $fontB, [Drawing.Brushes]::Black, $ox + 52, $oy - 9)

$lx = $mapW + 20; $ly = $TOP
function Leg([string]$t, $color, [string]$shape) {
  $b = New-Object Drawing.SolidBrush $color
  switch ($shape) {
    'box' { $g.FillRectangle($b, $lx, $script:ly + 3, 16, 12) }
    'dot' { $g.FillEllipse($b, $lx + 2, $script:ly + 3, 12, 12) }
    'ring' { $g.DrawEllipse((New-Object Drawing.Pen($color, 2)), $lx + 2, $script:ly + 3, 12, 12) }
    'line' { $g.DrawLine((New-Object Drawing.Pen($color, 2)), $lx, $script:ly + 9, $lx + 16, $script:ly + 9) }
    'tri' { $g.FillPolygon([Drawing.Brush]$b, [Drawing.PointF[]]@([Drawing.PointF]::new($lx + 8, $script:ly + 2), [Drawing.PointF]::new($lx + 1, $script:ly + 16), [Drawing.PointF]::new($lx + 15, $script:ly + 16))) }
    'head' { }
  }
  $f = if ($shape -eq 'head') { $fontB } else { $font }
  $g.DrawString($t, $f, [Drawing.Brushes]::Black, $lx + 24, $script:ly)
  $script:ly += 24
}
Leg "클라 배치 (이번에 서버로 보내는 것)" $BLUE 'head'
Leg "금고 후보 10 (매 판 5곳)" $BLUE 'box'
Leg "숨는 곳(옷장) 4" $BLUE 'box'
Leg "의적 시작 3 (레벨 PlayerStart 3개)" $BLUE 'dot'
Leg "창고: 감옥 칸 벽·문(하늘색)·감금 자리" $BLUE 'box'
Leg "탈출구 문·기둥, 탈출 구역(문 밖)" $GREEN 'box'
Leg "문 밖 바닥판 Floor_Exit (FBX에 없음)" $GREEN 'line'
Leg "정문 막음 (투명 벽)" $BLUE 'box'
Leg "방 33 (번호) / 빗금 = 반납 제외(문 없음)" (Col 160 25 90 220) 'line'
$ly += 10
Leg "서버 기존 배치 (바뀌는 것)" $RED 'head'
Leg "금고 후보 S1~S10" $RED 'box'
Leg "숨는 곳 h1~h8" $RED 'ring'
Leg "의적 시작 3" $RED 'ring'
Leg "탈출 구역(정문), 창고 구역(W3)" $RED 'box'
Leg "② 틈 막음 blocker_113" $RED 'box'
Leg "환풍구 3 (옛 규칙, 이제 안 씀)" $RED 'ring'
$ly += 10
Leg "그대로 쓰는 서버 값" $GRAY 'head'
Leg "경비원 시작 3" (Col 255 30 30 30) 'tri'
Leg "순찰 지점 47·연결 68" (Col 200 140 140 140) 'line'
Leg "벽 93 (FBX, 0.1cm 안 일치)" $GRAY 'box'
$ly += 14
$notes = @(
  "확인할 점",
  "· 경비원 2 시작(L2 = 클라 방 20)과 순찰 35·39·41은",
  "  문 없는 방(클라 11·13·20) 안 → 의적 쪽과 끊김",
  "· 순찰 연결 28→40이 금고4(방 12) 위를 지남",
  "· 탈출 구역은 문 밖 Floor_Exit 위 → fbx_to_map.py가",
  "  floors를 FBX로 덮으면 걸을 수 없는 칸이 됨",
  "· 클라 화면 = 이 그림을 시계 반대로 90° 돌린 모양",
  "  (화면 위 = +Y, 화면 왼쪽 = +X = 서쪽)"
)
foreach ($t in $notes) { $g.DrawString($t, $(if ($t -eq "확인할 점") { $fontB } else { $font }), [Drawing.Brushes]::Black, $lx, $ly); $ly += 20 }

$full = [IO.Path]::GetFullPath($Out)
$bmp.Save($full, [Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
"saved $full ($imgW x $imgH)"
