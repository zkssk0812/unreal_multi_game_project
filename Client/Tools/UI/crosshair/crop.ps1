param([string]$In, [string]$Out, [int]$X, [int]$Y, [int]$W, [int]$H, [double]$Scale = 1.0)
# 그림 일부를 잘라(배율) 저장 — 조준점 확인용
Add-Type -AssemblyName System.Drawing
$src = [System.Drawing.Image]::FromFile($In)
$dw = [int]($W * $Scale); $dh = [int]($H * $Scale)
$bmp = New-Object System.Drawing.Bitmap $dw, $dh
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g.DrawImage($src, (New-Object System.Drawing.Rectangle 0, 0, $dw, $dh), (New-Object System.Drawing.Rectangle $X, $Y, $W, $H), [System.Drawing.GraphicsUnit]::Pixel)
$g.Dispose(); $src.Dispose()
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
