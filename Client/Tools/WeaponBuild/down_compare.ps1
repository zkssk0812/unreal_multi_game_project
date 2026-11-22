# 쓰러짐 창 비교 그림: 위 = 쓰러짐 창(이전/이후, 경비원·플레이어), 아래 = 리스폰 뒤 화면(방향 표시·미니맵·남이 주움)
# 사용: powershell -File down_compare.ps1 -Dir <down_*.png 폴더> -Out 결과.png
param([string]$Dir, [string]$Out)
Add-Type -AssemblyName System.Drawing
$W = 2420; $H = 1265
$bmp = New-Object System.Drawing.Bitmap $W, $H
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
$g.Clear([System.Drawing.Color]::FromArgb(24, 24, 28))
$fTitle = New-Object System.Drawing.Font "Malgun Gothic", 22, ([System.Drawing.FontStyle]::Bold)
$fLabel = New-Object System.Drawing.Font "Malgun Gothic", 15, ([System.Drawing.FontStyle]::Bold)
$white = [System.Drawing.Brushes]::White
$gray = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(170, 170, 180))

function Put([string]$File, [int]$SX, [int]$SY, [int]$SW, [int]$SH, [int]$DX, [int]$DY, [double]$Scale, [string]$Label) {
    $p = Join-Path $Dir $File
    if (-not (Test-Path $p)) { $g.DrawString("(없음) $File", $fLabel, $gray, $DX, $DY + 20); return }
    $img = [System.Drawing.Image]::FromFile($p)
    $dw = [int]($SW * $Scale); $dh = [int]($SH * $Scale)
    $g.DrawImage($img, (New-Object System.Drawing.Rectangle $DX, ($DY + 30), $dw, $dh), (New-Object System.Drawing.Rectangle $SX, $SY, $SW, $SH), [System.Drawing.GraphicsUnit]::Pixel)
    $img.Dispose()
    $g.DrawString($Label, $fLabel, $white, $DX, $DY)
}

# ── 위: 쓰러짐 창 (1080p 화면 가운데 부분, 0.8배) ──
$g.DrawString("① 쓰러짐 창 — 이전(같은 순간, vh.OldDeathPanel 1) / 이후", $fTitle, $white, 20, 12)
$cx = 300; $cy = 380; $cw = 1320; $ch = 330; $s = 0.85
$colW = [int]($cw * $s) + 30
Put "down_guard_0_before.png" $cx $cy $cw $ch 20 60 $s "경비원에게 · 이전: '반납 금액 -`$X' 한 줄 (보물이 사라진 것처럼 보임)"
Put "down_guard_1_downed.png" $cx $cy $cw $ch (20 + $colW) 60 $s "경비원에게 · 이후: 반납한 돈 손실 40% / 보물은 떨어지고 환금 아이템은 사라짐"
$y2 = 60 + [int]($ch * $s) + 50
Put "down_player_0_before.png" $cx $cy $cw $ch 20 $y2 $s "플레이어에게 · 이전"
Put "down_player_1_downed.png" $cx $cy $cw $ch (20 + $colW) $y2 $s "플레이어에게 · 이후: 손실 10% / 보물 + 환금 아이템을 쓰러진 자리에 떨어뜨림"

# ── 아래: 리스폰 뒤 (1080p 전체, 0.4배) ──
$y3 = $y2 + [int]($ch * $s) + 60
$g.DrawString("② 리스폰 뒤 — 미니맵 보라색 ◇ + 내 캐릭터 둘레 화살표 '내 보물 `$X · Nm' (8초), 다른 플레이어가 주우면 사라짐", $fTitle, $white, 20, $y3)
$s2 = 0.4; $w2 = [int](1920 * $s2) + 24
Put "down_guard_2_respawn1s.png" 0 0 1920 1080 20 ($y3 + 50) $s2 "경비원에게 · 리스폰 1초: 멀리 떨어진 보물 쪽 화살표"
Put "down_player_2_respawn1s.png" 0 0 1920 1080 (20 + $w2) ($y3 + 50) $s2 "플레이어에게 · 리스폰 1초: 보물이 가까우면 그 위에 ◇"
Put "down_player_3_respawn4s.png" 0 0 1920 1080 (20 + 2 * $w2) ($y3 + 50) $s2 "플레이어에게 · 리스폰 4초: Hunty가 주움 → 표시 사라짐·알림"

$g.Dispose()
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
"saved $Out"
