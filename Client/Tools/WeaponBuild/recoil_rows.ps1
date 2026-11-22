param([string]$Tag = "before", [string]$Weapons = "4 5 6 7", [string]$Out = "")
# 반동 연속 프레임을 무기별 한 줄로 이어 붙인다: Saved/WeaponBuild/Recoil/{Tag}_w{N}_{0..5}.png → {Tag}_rows.png
Add-Type -AssemblyName System.Drawing
$d = "C:\UnrealProjects\MCPTest\Saved\WeaponBuild\Recoil"
if ($Out -eq "") { $Out = Join-Path $d ("{0}_rows.png" -f $Tag) }
$names = @{ "4" = "권총"; "5" = "화염방사기"; "6" = "레일건"; "7" = "테이저건" }
$frames = @{ "4" = "0 / 0.03 / 0.06 / 0.10 / 0.15 / 0.24초"; "5" = "누르는 중 0.30~0.60초 (0.06초 간격)"; "6" = "0 / 0.05 / 0.12 / 0.25 / 0.45 / 0.80초"; "7" = "0 / 0.03 / 0.06 / 0.10 / 0.16 / 0.30초" }
$rows = @()
foreach ($w in $Weapons.Split(" ")) {
  $files = @(Get-ChildItem $d -Filter ("{0}_w{1}_*.png" -f $Tag, $w) | Where-Object { $_.Name -match ("_w{0}_\d+\.png$" -f $w) } | Sort-Object { [int]($_.BaseName -replace '.*_', '') })
  if ($files.Count -eq 0) { continue }
  $rows += ,@($w, $files)
}
$sc = 0.5
$first = [System.Drawing.Image]::FromFile($rows[0][1][0].FullName)
$cw = [int]($first.Width * $sc); $ch = [int]($first.Height * $sc); $first.Dispose()
$cols = ($rows | ForEach-Object { $_[1].Count } | Measure-Object -Maximum).Maximum
$bmp = New-Object System.Drawing.Bitmap ([int]($cw * $cols)), ([int](($ch + 34) * $rows.Count))
$g = [System.Drawing.Graphics]::FromImage($bmp); $g.Clear([System.Drawing.Color]::FromArgb(24, 24, 28))
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAlias
$font = New-Object System.Drawing.Font "Malgun Gothic", 15, ([System.Drawing.FontStyle]::Bold)
$y = 0
foreach ($r in $rows) {
  $w = $r[0]
  $g.DrawString(("{0} {1} — {2}" -f $Tag, $names[$w], $frames[$w]), $font, [System.Drawing.Brushes]::White, 8, $y + 5)
  $y += 34
  $x = 0
  foreach ($f in $r[1]) { $img = [System.Drawing.Image]::FromFile($f.FullName); $g.DrawImage($img, $x, $y, $cw, $ch); $img.Dispose(); $x += $cw }
  $y += $ch
}
$g.Dispose(); $bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
Write-Output $Out
