# 고해상도 화면에서 가운데(내 캐릭터) 부분을 잘라 저장한다: crop.ps1 입력.png 출력.png [가로] [세로] [가운데X] [가운데Y] [축소배율]
param([string]$In, [string]$Out, [int]$W = 900, [int]$H = 700, [int]$CX = 2556, [int]$CY = 1320, [double]$Scale = 0.6)
Add-Type -AssemblyName System.Drawing
$src = [System.Drawing.Image]::FromFile($In)
$ow = [int]($W * $Scale); $oh = [int]($H * $Scale)
$dst = New-Object System.Drawing.Bitmap $ow, $oh
$g = [System.Drawing.Graphics]::FromImage($dst)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g.DrawImage($src, (New-Object System.Drawing.Rectangle 0, 0, $ow, $oh), (New-Object System.Drawing.Rectangle ($CX - $W / 2), ($CY - $H / 2), $W, $H), [System.Drawing.GraphicsUnit]::Pixel)
$g.Dispose(); $src.Dispose()
$dst.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png); $dst.Dispose()
