param([string]$Dir, [string]$Frames, [int]$CX = 388, [int]$CY = 31, [int]$CW = 520, [int]$CH = 440, [double]$Scale = 0.5,
      [int]$Cols = 6, [string]$Out, [string]$TimesFile = 'sweep_times.txt', [string]$Mode = 'sweep')
# 연속 프레임 격자 (2026-11-25): Dir 의 sweep_NN.png 중 Frames("0,2,4") 를 같은 자리(CX,CY,CW,CH)로 잘라 격자로 놓고
# 칸마다 "번호 · 시각 · 조준 각도(0→180→0, 각 2초 가정)"를 쓴다
Add-Type -AssemblyName System.Drawing
$times = @{}
Get-Content (Join-Path $Dir $TimesFile) | ForEach-Object { $p = $_.Trim([char]0xFEFF).Split(' '); if ($p.Count -ge 2) { $times[$p[0]] = [double]($p[1] -replace 'ms', '') } }
$list = $Frames.Split(',') | ForEach-Object { [int]$_ }
$w = [int]($CW * $Scale); $h = [int]($CH * $Scale)
$rows = [int][Math]::Ceiling($list.Count / $Cols)
$bmp = New-Object System.Drawing.Bitmap ($w * $Cols), (($h + 22) * $rows)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.Clear([System.Drawing.Color]::FromArgb(30, 30, 34))
$g.InterpolationMode = 'HighQualityBicubic'
$font = New-Object System.Drawing.Font 'Malgun Gothic', 9, ([System.Drawing.FontStyle]::Bold)
for ($i = 0; $i -lt $list.Count; $i++) {
    $name = 'sweep_{0:D2}.png' -f $list[$i]
    $src = [System.Drawing.Image]::FromFile((Join-Path $Dir $name))
    $x = ($i % $Cols) * $w; $y = [Math]::Floor($i / $Cols) * ($h + 22)
    $g.DrawImage($src, (New-Object System.Drawing.Rectangle $x, ($y + 22), $w, $h), (New-Object System.Drawing.Rectangle $CX, $CY, $CW, $CH), 'Pixel')
    $src.Dispose()
    $t = $times[$name]
    $label = '#{0} {1:F0}ms' -f $list[$i], $t
    if ($Mode -eq 'sweep') {
        $a = if ($t -lt 2000) { 90.0 * $t / 1000 } else { [Math]::Max(0, 180 - 90.0 * ($t - 2000) / 1000) }
        $label += (' ~{0:F0}°' -f $a)
    }
    $g.DrawString($label, $font, [System.Drawing.Brushes]::White, $x + 4, $y + 3)
}
$g.Dispose()
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
"saved $Out"
