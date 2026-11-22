# 구르기 연속 프레임을 캐릭터 둘레만 잘라 한 줄로 붙인다.
# 사용: powershell -ExecutionPolicy Bypass -File roll_strip.ps1 -Out 결과.png -Title "제목" -Files a.png,b.png,... -Labels "0%|20%|..." [-Cx 770 -Cy 430 -Size 400 -Scale 1.2]
param([string]$Out, [string]$Title, [string[]]$Files, [string]$Labels, [int]$Cx = 770, [int]$Cy = 430, [int]$Size = 400, [double]$Scale = 1.2)
Add-Type -AssemblyName System.Drawing
$labelList = $Labels.Split("|")
$cell = [int]($Size * $Scale)
$top = 40; $bottom = 34
$bmp = New-Object System.Drawing.Bitmap ($cell * $Files.Count), ($cell + $top + $bottom)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g.Clear([System.Drawing.Color]::FromArgb(24, 24, 28))
$fontT = New-Object System.Drawing.Font('Malgun Gothic', 16, [System.Drawing.FontStyle]::Bold)
$font = New-Object System.Drawing.Font('Malgun Gothic', 13, [System.Drawing.FontStyle]::Bold)
$g.DrawString($Title, $fontT, [System.Drawing.Brushes]::White, 10, 8)
for ($i = 0; $i -lt $Files.Count; $i++) {
    $img = [System.Drawing.Image]::FromFile($Files[$i])
    $src = New-Object System.Drawing.Rectangle ($Cx - $Size / 2), ($Cy - $Size / 2), $Size, $Size
    $dst = New-Object System.Drawing.Rectangle ($i * $cell), $top, $cell, $cell
    $g.DrawImage($img, $dst, $src, [System.Drawing.GraphicsUnit]::Pixel)
    $img.Dispose()
    $g.DrawRectangle([System.Drawing.Pens]::Gray, $i * $cell, $top, $cell - 1, $cell - 1)
    if ($i -lt $labelList.Count) { $g.DrawString($labelList[$i], $font, [System.Drawing.Brushes]::Yellow, $i * $cell + 8, $top + $cell + 6) }
}
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
"saved $Out"
