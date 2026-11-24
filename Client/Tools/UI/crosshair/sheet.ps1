param([string]$Spec, [int]$Size = 200, [string]$Out, [int]$Rows = 1)
# 조준점 비교 그림: 여러 화면의 여러 자리를 Size×Size(배율 1 = 실제 1080p 크기)로 잘라 격자로 늘어놓는다.
# -Spec "파일|x|y|이름|커서;…"  커서 = 1 이면 그 칸 가운데에 운영체제 십자 커서(지금 게임 커서)를 실제 크기로 그린다
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
$items = $Spec.Split(';') | Where-Object { $_ -ne '' }
$n = $items.Count
$cols = [int][Math]::Ceiling($n / $Rows)
$cell = $Size + 24
$bmp = New-Object System.Drawing.Bitmap ($Size * $cols), ($cell * $Rows)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.Clear([System.Drawing.Color]::FromArgb(30, 30, 34))
$font = New-Object System.Drawing.Font "Malgun Gothic", 10, ([System.Drawing.FontStyle]::Bold)
$cache = @{}
for ($i = 0; $i -lt $n; $i++) {
    $p = $items[$i].Split('|')
    if (-not $cache.ContainsKey($p[0])) { $cache[$p[0]] = [System.Drawing.Image]::FromFile($p[0]) }
    $src = $cache[$p[0]]
    $ox = ($i % $cols) * $Size; $oy = [int][Math]::Floor($i / $cols) * $cell
    $dst = New-Object System.Drawing.Rectangle $ox, $oy, $Size, $Size
    $srcR = New-Object System.Drawing.Rectangle ([int]$p[1] - $Size / 2), ([int]$p[2] - $Size / 2), $Size, $Size
    $g.DrawImage($src, $dst, $srcR, [System.Drawing.GraphicsUnit]::Pixel)
    if ($p.Count -gt 4 -and $p[4] -eq '1') {
        $c = [System.Windows.Forms.Cursors]::Cross
        $hs = $c.HotSpot
        $c.Draw($g, (New-Object System.Drawing.Rectangle ($ox + $Size / 2 - $hs.X), ($oy + $Size / 2 - $hs.Y), $c.Size.Width, $c.Size.Height))
    }
    $g.DrawString($p[3], $font, [System.Drawing.Brushes]::White, ($ox + 4), ($oy + $Size + 3))
}
foreach ($k in $cache.Keys) { $cache[$k].Dispose() }
$g.Dispose()
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
