# 연속 화면을 잘라 격자로 붙인다: strip.ps1 -Out 출력.png -Files a.png,b.png -Labels "가","나" -Crop x,y,w,h [-Scale 0.5] [-Cols 4] [-Title 제목]
param([string]$Out, [string[]]$Files, [string[]]$Labels, [int[]]$Crop = @(4, 135, 1906, 860), [double]$Scale = 0.5, [int]$Cols = 4, [string]$Title = "")
Add-Type -AssemblyName System.Drawing
$cw = [int]($Crop[2] * $Scale); $ch = [int]($Crop[3] * $Scale)
$rows = [math]::Ceiling($Files.Count / $Cols)
$top = if ($Title) { 40 } else { 0 }
$bmp = New-Object System.Drawing.Bitmap ($cw * $Cols), ($ch * $rows + $top)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.Clear([System.Drawing.Color]::FromArgb(24, 24, 28))
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAlias
$font = New-Object System.Drawing.Font "Malgun Gothic", 13, ([System.Drawing.FontStyle]::Bold)
$tfont = New-Object System.Drawing.Font "Malgun Gothic", 16, ([System.Drawing.FontStyle]::Bold)
if ($Title) { $g.DrawString($Title, $tfont, [System.Drawing.Brushes]::White, 10, 8) }
for ($i = 0; $i -lt $Files.Count; $i++) {
  $img = [System.Drawing.Image]::FromFile($Files[$i])
  $x = ($i % $Cols) * $cw; $y = [math]::Floor($i / $Cols) * $ch + $top
  $src = New-Object System.Drawing.Rectangle $Crop[0], $Crop[1], $Crop[2], $Crop[3]
  $dst = New-Object System.Drawing.Rectangle $x, $y, $cw, $ch
  $g.DrawImage($img, $dst, $src, [System.Drawing.GraphicsUnit]::Pixel)
  $g.DrawRectangle([System.Drawing.Pens]::Black, $x, $y, $cw - 1, $ch - 1)
  if ($Labels -and $i -lt $Labels.Count) {
    $sz = $g.MeasureString($Labels[$i], $font)
    $g.FillRectangle((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(180, 0, 0, 0))), $x + 4, $y + 4, $sz.Width + 8, $sz.Height + 2)
    $g.DrawString($Labels[$i], $font, [System.Drawing.Brushes]::White, $x + 8, $y + 5)
  }
  $img.Dispose()
}
$g.Dispose(); $bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
