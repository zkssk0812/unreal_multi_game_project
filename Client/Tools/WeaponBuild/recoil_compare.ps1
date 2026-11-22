param([string]$Weapon = "6", [string]$Out = "", [double]$Scale = 0.75, [string]$Tags = "before after")
# 같은 무기의 변경 전·후 연속 프레임을 위아래로: Saved/WeaponBuild/Recoil/{tag}_w{N}_{i}.png → recoil_compare_w{N}.png
Add-Type -AssemblyName System.Drawing
$d = "C:\UnrealProjects\MCPTest\Saved\WeaponBuild\Recoil"
if ($Out -eq "") { $Out = Join-Path $d ("recoil_compare_w{0}.png" -f $Weapon) }
$tagList = $Tags.Split(" ")
$rows = @()
foreach ($t in $tagList) { $rows += ,@(Get-ChildItem $d -Filter ("{0}_w{1}_*.png" -f $t, $Weapon) | Where-Object { $_.Name -match ("^{0}_w{1}_\d+\.png$" -f $t, $Weapon) } | Sort-Object { [int]($_.BaseName -replace '.*_', '') }) }
$first = [System.Drawing.Image]::FromFile($rows[0][0].FullName)
$cw = [int]($first.Width * $Scale); $ch = [int]($first.Height * $Scale); $first.Dispose()
$cols = [int](($rows | ForEach-Object { $_.Count } | Measure-Object -Maximum).Maximum)
$bmp = New-Object System.Drawing.Bitmap ([int]($cw * $cols)), ([int](($ch + 30) * $rows.Count))
$g = [System.Drawing.Graphics]::FromImage($bmp); $g.Clear([System.Drawing.Color]::FromArgb(24, 24, 28))
$font = New-Object System.Drawing.Font "Malgun Gothic", 14, ([System.Drawing.FontStyle]::Bold)
$y = 0; $k = 0
foreach ($r in $rows) {
  $g.DrawString($tagList[$k], $font, [System.Drawing.Brushes]::White, 6, $y + 3); $y += 30; $x = 0
  foreach ($f in $r) { $img = [System.Drawing.Image]::FromFile($f.FullName); $g.DrawImage($img, $x, $y, $cw, $ch); $img.Dispose(); $x += $cw }
  $y += $ch; $k++
}
$g.Dispose(); $bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose(); Write-Output $Out
