# 캐릭터 텍스처 줄이기: SourceArt/Characters/<이름>/Original4k/*.jpg → Textures/ (긴 변 2048, jpg 품질 92)
# 사용: powershell -ExecutionPolicy Bypass -File resize_textures.ps1 Thief2 Thief3
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Names)
Add-Type -AssemblyName System.Drawing
$root = Join-Path $PSScriptRoot "..\..\SourceArt\Characters"
$codec = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object { $_.MimeType -eq "image/jpeg" }
$ep = New-Object System.Drawing.Imaging.EncoderParameters(1)
$ep.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter([System.Drawing.Imaging.Encoder]::Quality, [long]92)
foreach ($n in $Names) {
    $src = Join-Path $root "$n\Original4k"
    $dst = Join-Path $root "$n\Textures"
    New-Item -ItemType Directory -Force $dst | Out-Null
    Get-ChildItem $src -Filter *.jpg | ForEach-Object {
        $img = [System.Drawing.Image]::FromFile($_.FullName)
        $s = [Math]::Min(1.0, 2048.0 / [Math]::Max($img.Width, $img.Height))
        $w = [int]($img.Width * $s); $h = [int]($img.Height * $s)
        $bmp = New-Object System.Drawing.Bitmap($w, $h)
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $g.DrawImage($img, 0, 0, $w, $h)
        $out = Join-Path $dst $_.Name
        $bmp.Save($out, $codec, $ep)
        "{0}: {1}x{2} -> {3}x{4}" -f $_.Name, $img.Width, $img.Height, $w, $h
        $g.Dispose(); $bmp.Dispose(); $img.Dispose()
    }
}
