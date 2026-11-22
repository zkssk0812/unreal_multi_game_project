# 화면 전체를 찍어 PNG 로 저장하고 보이는 창 제목을 적는다 (에디터 모달 창 확인용):  screen.ps1 출력.png [배율]
param([string]$Out, [double]$Scale = 0.5)
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
$b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)
$w = [int]($b.Width * $Scale); $h = [int]($b.Height * $Scale)
$small = New-Object System.Drawing.Bitmap $bmp, $w, $h
$small.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose(); $small.Dispose()
Get-Process | Where-Object { $_.MainWindowTitle -ne '' } | ForEach-Object { "$($_.ProcessName) [$($_.Id)] $($_.MainWindowTitle)" }
