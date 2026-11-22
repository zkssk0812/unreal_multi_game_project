# 그림 여러 장을 격자로 합치고 아래에 이름표를 단다 (비교 이미지용)
#   grid.ps1 -List 목록.txt -Out 출력.png -Cols 5 -CellW 400 [-Title 제목파일.txt]
#   목록.txt (UTF-8): 한 줄에 "그림경로|이름표"
param([string]$List, [string]$Out, [int]$Cols = 5, [int]$CellW = 400, [string]$TitleFile = "")
Add-Type -AssemblyName System.Drawing
$lines = [System.IO.File]::ReadAllLines($List, [System.Text.Encoding]::UTF8) | Where-Object { $_.Trim() -ne "" }
$imgs = @(); $labs = @()
foreach ($l in $lines) { $p = $l.Split("|"); $imgs += [System.Drawing.Image]::FromFile($p[0]); $labs += $(if ($p.Count -gt 1) { $p[1] } else { "" }) }
$Title = if ($TitleFile) { [System.IO.File]::ReadAllText($TitleFile, [System.Text.Encoding]::UTF8).Trim() } else { "" }
$ratio = $imgs[0].Height / $imgs[0].Width
$cellH = [int]($CellW * $ratio)
$labH = 34; $titleH = $(if ($Title) { 48 } else { 0 })
$rows = [Math]::Ceiling($imgs.Count / $Cols)
$bmp = New-Object System.Drawing.Bitmap ([int]($Cols * $CellW)), ([int]($titleH + $rows * ($cellH + $labH)))
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.Clear([System.Drawing.Color]::FromArgb(24, 24, 28))
$g.InterpolationMode = 'HighQualityBicubic'; $g.TextRenderingHint = 'AntiAliasGridFit'
$font = New-Object System.Drawing.Font("Malgun Gothic", 13, [System.Drawing.FontStyle]::Bold)
$tfont = New-Object System.Drawing.Font("Malgun Gothic", 18, [System.Drawing.FontStyle]::Bold)
$brush = [System.Drawing.Brushes]::White
if ($Title) { $g.DrawString($Title, $tfont, $brush, 12, 10) }
for ($i = 0; $i -lt $imgs.Count; $i++) {
  $x = ($i % $Cols) * $CellW; $y = $titleH + [Math]::Floor($i / $Cols) * ($cellH + $labH)
  $g.DrawImage($imgs[$i], $x, $y, $CellW, $cellH)
  $g.DrawString($labs[$i], $font, $brush, ($x + 8), ($y + $cellH + 5))
}
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$imgs | ForEach-Object { $_.Dispose() }; $g.Dispose(); $bmp.Dispose()
"saved $Out"
