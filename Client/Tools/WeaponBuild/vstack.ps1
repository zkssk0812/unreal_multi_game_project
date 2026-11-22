# 이미지 여러 장을 위아래로 붙인다 (폭이 다르면 가장 넓은 것에 맞춰 왼쪽 정렬)
# 사용: & vstack.ps1 -Out 결과.png -Files a.png,b.png
param([string]$Out, [string[]]$Files)
Add-Type -AssemblyName System.Drawing
$imgs = $Files | ForEach-Object { [System.Drawing.Image]::FromFile($_) }
$w = [int]($imgs | Measure-Object -Property Width -Maximum).Maximum
$h = [int](($imgs | Measure-Object -Property Height -Sum).Sum + 6 * ($imgs.Count - 1))
$bmp = New-Object System.Drawing.Bitmap $w, $h
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.Clear([System.Drawing.Color]::FromArgb(24, 24, 28))
$y = 0
foreach ($img in $imgs) { $g.DrawImage($img, 0, $y, $img.Width, $img.Height); $y += $img.Height + 6; $img.Dispose() }
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
"saved $Out"
