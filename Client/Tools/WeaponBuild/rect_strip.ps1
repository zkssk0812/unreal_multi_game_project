# 여러 그림에서 직사각형을 잘라 한 줄로 붙인다 (모두 같은 높이로 맞춤). 아래에 라벨
# 사용: & rect_strip.ps1 -Out 결과.png -Title "제목" -Items "파일|x|y|w|h|라벨", ... [-Height 120]
param([string]$Out, [string]$Title, [string[]]$Items, [int]$Height = 120, [int]$Gap = 8)
Add-Type -AssemblyName System.Drawing
$parts = @()
foreach ($it in $Items) {
    $p = $it.Split("|")
    $w = [int]$p[3]; $h = [int]$p[4]
    $parts += [pscustomobject]@{ File = $p[0]; X = [int]$p[1]; Y = [int]$p[2]; W = $w; H = $h; Label = $p[5]; OutW = [int]([double]$w * $Height / $h) }
}
$top = 40; $bottom = 30
$total = ($parts | Measure-Object -Property OutW -Sum).Sum + $Gap * ($parts.Count + 1)
$bmp = New-Object System.Drawing.Bitmap ([int]$total), ($Height + $top + $bottom)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g.Clear([System.Drawing.Color]::FromArgb(24, 24, 28))
$fontT = New-Object System.Drawing.Font('Malgun Gothic', 16, [System.Drawing.FontStyle]::Bold)
$font = New-Object System.Drawing.Font('Malgun Gothic', 12, [System.Drawing.FontStyle]::Bold)
$g.DrawString($Title, $fontT, [System.Drawing.Brushes]::White, 10, 8)
$x = $Gap
foreach ($p in $parts) {
    $img = [System.Drawing.Image]::FromFile($p.File)
    $src = New-Object System.Drawing.Rectangle $p.X, $p.Y, $p.W, $p.H
    $dst = New-Object System.Drawing.Rectangle $x, $top, $p.OutW, $Height
    $g.DrawImage($img, $dst, $src, [System.Drawing.GraphicsUnit]::Pixel)
    $img.Dispose()
    $g.DrawRectangle([System.Drawing.Pens]::Gray, $x, $top, $p.OutW - 1, $Height - 1)
    $g.DrawString($p.Label, $font, [System.Drawing.Brushes]::Yellow, $x + 4, $top + $Height + 5)
    $x += $p.OutW + $Gap
}
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
"saved $Out"
