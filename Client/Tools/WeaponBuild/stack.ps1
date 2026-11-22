param([string]$Out, [string[]]$Files, [string[]]$Labels)
Add-Type -AssemblyName System.Drawing
$imgs = $Files | ForEach-Object { [System.Drawing.Image]::FromFile($_) }
$w = [int](($imgs | Measure-Object -Property Width -Maximum).Maximum)
[int]$h = 0; foreach ($i in $imgs) { $h += $i.Height + 36 }
$bmp = New-Object System.Drawing.Bitmap $w, $h
$g = [System.Drawing.Graphics]::FromImage($bmp); $g.Clear([System.Drawing.Color]::FromArgb(24, 24, 28))
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAlias
$font = New-Object System.Drawing.Font "Malgun Gothic", 16, ([System.Drawing.FontStyle]::Bold)
$y = 0
for ($k = 0; $k -lt $imgs.Count; $k++) { $g.DrawString($Labels[$k], $font, [System.Drawing.Brushes]::White, 8, $y + 4); $y += 36; $g.DrawImage($imgs[$k], 0, $y, $imgs[$k].Width, $imgs[$k].Height); $y += $imgs[$k].Height }
$imgs | ForEach-Object { $_.Dispose() }; $g.Dispose(); $bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
