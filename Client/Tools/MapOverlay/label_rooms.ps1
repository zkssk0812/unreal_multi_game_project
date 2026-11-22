# Draw server room ids on a top-down editor capture (camera straight down, yaw 0 => image up = +X, right = +Y).
# usage: label_rooms.ps1 -Image in.png -Rooms rooms.csv -Out out.png -CamZ 10600 [-Fov 90] [-Rotate 0|90|180|270]
param([string]$Image, [string]$Rooms, [string]$Out, [double]$CamZ = 10600, [double]$Fov = 90, [int]$Rotate = 0, [string]$Title = "")
Add-Type -AssemblyName System.Drawing
$src = [System.Drawing.Image]::FromFile($Image)
$bmp = New-Object System.Drawing.Bitmap($src)
$src.Dispose()
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = 'AntiAlias'
$g.TextRenderingHint = 'AntiAlias'
$f = ($bmp.Width / 2.0) / [Math]::Tan($Fov * [Math]::PI / 360.0)
$font = New-Object System.Drawing.Font('Malgun Gothic', 9, [System.Drawing.FontStyle]::Bold)
$back = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(190, 0, 0, 0))
$white = [System.Drawing.Brushes]::White
$gray = [System.Drawing.Brushes]::Silver
foreach ($line in Get-Content $Rooms) {
    $p = $line.Split(',')
    $x = [double]$p[1]; $y = [double]$p[2]
    $px = $bmp.Width / 2.0 + $y * $f / $CamZ
    $py = $bmp.Height / 2.0 - $x * $f / $CamZ
    $size = $g.MeasureString($p[0], $font)
    $g.FillRectangle($back, [float]($px - $size.Width / 2), [float]($py - $size.Height / 2), $size.Width, $size.Height)
    $brush = if ($p[3] -eq '1') { $white } else { $gray }
    $g.DrawString($p[0], $font, $brush, [float]($px - $size.Width / 2), [float]($py - $size.Height / 2))
}
$g.Dispose()
if ($Rotate -eq 90) { $bmp.RotateFlip('Rotate90FlipNone') }
elseif ($Rotate -eq 180) { $bmp.RotateFlip('Rotate180FlipNone') }
elseif ($Rotate -eq 270) { $bmp.RotateFlip('Rotate270FlipNone') }
if ($Title -ne "") {
    $g2 = [System.Drawing.Graphics]::FromImage($bmp)
    $tf = New-Object System.Drawing.Font('Malgun Gothic', 14, [System.Drawing.FontStyle]::Bold)
    $ts = $g2.MeasureString($Title, $tf)
    $g2.FillRectangle($back, 8, 8, $ts.Width + 8, $ts.Height + 4)
    $g2.DrawString($Title, $tf, [System.Drawing.Brushes]::Yellow, 12, 10)
    $g2.Dispose()
}
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
"saved $Out"
