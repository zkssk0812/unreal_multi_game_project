# 여러 그림을 격자로 이어 붙이고 칸마다 이름을 적는다: montage.ps1 -Out 출력.png -Cols 5 -Files a.png,b.png -Labels "가","나" [-Title 제목]
param([string]$Out, [int]$Cols = 5, [string[]]$Files, [string[]]$Labels, [string]$Title = "")
Add-Type -AssemblyName System.Drawing
$imgs = $Files | ForEach-Object { [System.Drawing.Image]::FromFile($_) }
$cw = $imgs[0].Width; $ch = $imgs[0].Height
$rows = [math]::Ceiling($imgs.Count / $Cols)
$top = if ($Title) { 44 } else { 0 }
$bmp = New-Object System.Drawing.Bitmap ($cw * $Cols), ($ch * $rows + $top)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.Clear([System.Drawing.Color]::FromArgb(24, 24, 28))
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAlias
$font = New-Object System.Drawing.Font "Malgun Gothic", 15, ([System.Drawing.FontStyle]::Bold)
$tfont = New-Object System.Drawing.Font "Malgun Gothic", 18, ([System.Drawing.FontStyle]::Bold)
if ($Title) { $g.DrawString($Title, $tfont, [System.Drawing.Brushes]::White, 10, 8) }
for ($i = 0; $i -lt $imgs.Count; $i++) {
  $x = ($i % $Cols) * $cw; $y = [math]::Floor($i / $Cols) * $ch + $top
  $g.DrawImage($imgs[$i], $x, $y, $cw, $ch)
  $g.DrawRectangle([System.Drawing.Pens]::Black, $x, $y, $cw - 1, $ch - 1)
  if ($Labels -and $i -lt $Labels.Count) {
    $sz = $g.MeasureString($Labels[$i], $font)
    $g.FillRectangle((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(170, 0, 0, 0))), $x + 4, $y + 4, $sz.Width + 8, $sz.Height + 2)
    $g.DrawString($Labels[$i], $font, [System.Drawing.Brushes]::White, $x + 8, $y + 5)
  }
}
$imgs | ForEach-Object { $_.Dispose() }
$g.Dispose(); $bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
