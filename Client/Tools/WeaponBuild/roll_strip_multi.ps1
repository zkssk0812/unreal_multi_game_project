# 파일마다 자를 가운데가 다른 연속 프레임 줄 이미지 (다른 플레이어 촬영용: 관찰자 카메라 기준 위치가 바뀐다)
# 사용: & roll_strip_multi.ps1 -Out 결과.png -Title "제목" -Items "파일|cx|cy|라벨", ... [-Size 300 -Scale 1.5]
param([string]$Out, [string]$Title, [string[]]$Items, [int]$Size = 300, [double]$Scale = 1.5)
Add-Type -AssemblyName System.Drawing
$cell = [int]($Size * $Scale)
$top = 40; $bottom = 34
$bmp = New-Object System.Drawing.Bitmap ($cell * $Items.Count), ($cell + $top + $bottom)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g.Clear([System.Drawing.Color]::FromArgb(24, 24, 28))
$fontT = New-Object System.Drawing.Font('Malgun Gothic', 16, [System.Drawing.FontStyle]::Bold)
$font = New-Object System.Drawing.Font('Malgun Gothic', 13, [System.Drawing.FontStyle]::Bold)
$g.DrawString($Title, $fontT, [System.Drawing.Brushes]::White, 10, 8)
for ($i = 0; $i -lt $Items.Count; $i++) {
    $p = $Items[$i].Split("|")
    $img = [System.Drawing.Image]::FromFile($p[0])
    $cx = [int]$p[1]; $cy = [int]$p[2]
    # "파일|cx|cy|자를크기|라벨" 처럼 5칸이면 그 칸만 자르는 크기를 바꾼다 (이미 확대된 예전 비교 그림과 섞을 때)
    $sz = $Size
    if ($p.Count -ge 5) { $sz = [int]$p[3]; $p = @($p[0], $p[1], $p[2], $p[4]) }
    $src = New-Object System.Drawing.Rectangle ($cx - $sz / 2), ($cy - $sz / 2), $sz, $sz
    $dst = New-Object System.Drawing.Rectangle ($i * $cell), $top, $cell, $cell
    $g.DrawImage($img, $dst, $src, [System.Drawing.GraphicsUnit]::Pixel)
    $img.Dispose()
    $g.DrawRectangle([System.Drawing.Pens]::Gray, $i * $cell, $top, $cell - 1, $cell - 1)
    $g.DrawString($p[3], $font, [System.Drawing.Brushes]::Yellow, $i * $cell + 8, $top + $cell + 6)
}
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
"saved $Out"
