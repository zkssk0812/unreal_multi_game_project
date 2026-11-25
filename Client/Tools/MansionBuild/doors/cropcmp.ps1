param([string]$Spec, [double]$Scale = 3, [string]$Out)
# 같은 자리 비교 그림 (2026-11-25): -Spec "파일|x|y|w|h|이름;…" 각 칸을 Scale 배로 키워(가장 가까운 픽셀) 가로로 늘어놓는다
Add-Type -AssemblyName System.Drawing
$items = $Spec.Split(';') | Where-Object { $_ -ne '' } | ForEach-Object { , ($_.Split('|')) }
$cw = 0; $ch = 0
foreach ($it in $items) { $cw += [int]([int]$it[3] * $Scale) + 8; $ch = [Math]::Max($ch, [int]([int]$it[4] * $Scale)) }
$bmp = New-Object System.Drawing.Bitmap $cw, ($ch + 24)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.Clear([System.Drawing.Color]::FromArgb(30, 30, 34))
$g.InterpolationMode = 'NearestNeighbor'
$g.PixelOffsetMode = 'Half'
$font = New-Object System.Drawing.Font 'Malgun Gothic', 10, ([System.Drawing.FontStyle]::Bold)
$x = 0
foreach ($it in $items) {
    $src = [System.Drawing.Image]::FromFile($it[0])
    $w = [int]([int]$it[3] * $Scale); $h = [int]([int]$it[4] * $Scale)
    $g.DrawImage($src, (New-Object System.Drawing.Rectangle $x, 24, $w, $h), (New-Object System.Drawing.Rectangle ([int]$it[1]), ([int]$it[2]), ([int]$it[3]), ([int]$it[4])), 'Pixel')
    $src.Dispose()
    $g.DrawString($it[5], $font, [System.Drawing.Brushes]::White, $x + 4, 3)
    $x += $w + 8
}
$g.Dispose()
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
"saved $Out"
