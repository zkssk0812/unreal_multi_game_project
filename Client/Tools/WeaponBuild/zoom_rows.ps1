param([string]$Weapon = "5", [string]$Tags = "before after", [int]$X = 350, [int]$Y = 250, [int]$W = 500, [int]$H = 300, [string]$Out = "")
# 연속 프레임의 같은 부분(총 주변)만 잘라 크게 이어 붙인다 (작은 떨림 비교용)
Add-Type -AssemblyName System.Drawing
$d = "C:\UnrealProjects\MCPTest\Saved\WeaponBuild\Recoil"
if ($Out -eq "") { $Out = Join-Path $d ("zoom_w{0}.png" -f $Weapon) }
$tl = $Tags.Split(" ")
$b = New-Object System.Drawing.Bitmap ([int]($W * 6)), ([int]($H * $tl.Count))
$g = [System.Drawing.Graphics]::FromImage($b)
$src = New-Object System.Drawing.Rectangle $X, $Y, $W, $H
for ($k = 0; $k -lt $tl.Count; $k++) {
  for ($i = 0; $i -lt 6; $i++) {
    $p = Join-Path $d ("{0}_w{1}_{2}.png" -f $tl[$k], $Weapon, $i)
    if (-not (Test-Path $p)) { continue }
    $img = [System.Drawing.Image]::FromFile($p)
    $g.DrawImage($img, (New-Object System.Drawing.Rectangle ($i * $W), ($k * $H), $W, $H), $src, [System.Drawing.GraphicsUnit]::Pixel)
    $g.DrawLine([System.Drawing.Pens]::Yellow, $i * $W, $k * $H + 150, ($i + 1) * $W, $k * $H + 150)
    $img.Dispose()
  }
}
$g.Dispose(); $b.Save($Out); $b.Dispose(); Write-Output $Out
