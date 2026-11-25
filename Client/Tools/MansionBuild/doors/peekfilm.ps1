param([string]$Log, [string]$OutDir, [string]$Trigger = '\[Peek\] 조준 돌리기 ', [int]$Count = 45, [int]$IntervalMs = 90,
      [int]$X = 0, [int]$Y = 0, [int]$W = 1296, [int]$H = 759, [int]$TimeoutSec = 300)
# 문 너머 시야 연속 프레임 (2026-11-25): 봇 로그에 신호 줄이 나오면 그 순간부터 봇 창 영역을 IntervalMs 간격으로 Count 장 찍는다.
# 게임 안 화면 저장(프레임이 멈춤) 대신 바깥에서 찍어 실제 재생 그대로의 프레임을 얻는다 (Tools/UI/crosshair/film.ps1 변형)
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force $OutDir | Out-Null
$pos = 0
$sw = [System.Diagnostics.Stopwatch]::StartNew()
while ($sw.Elapsed.TotalSeconds -lt $TimeoutSec) {
    Start-Sleep -Milliseconds 20
    if (-not (Test-Path $Log)) { continue }
    $fs = [System.IO.File]::Open($Log, 'Open', 'Read', 'ReadWrite')
    if ($fs.Length -lt $pos) { $pos = 0 }
    $fs.Seek($pos, 'Begin') | Out-Null
    $buf = New-Object byte[] ($fs.Length - $pos)
    $read = $fs.Read($buf, 0, $buf.Length)
    $fs.Close()
    $pos += $read
    $text = [System.Text.Encoding]::UTF8.GetString($buf, 0, $read)
    if ($text -notmatch $Trigger) { continue }
    $frames = New-Object System.Collections.ArrayList
    $t0 = $sw.Elapsed.TotalMilliseconds
    for ($i = 0; $i -lt $Count; $i++) {
        $due = $t0 + $i * $IntervalMs
        while ($sw.Elapsed.TotalMilliseconds -lt $due) { Start-Sleep -Milliseconds 2 }
        $bmp = New-Object System.Drawing.Bitmap $W, $H
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.CopyFromScreen($X, $Y, 0, 0, $bmp.Size)
        $g.Dispose()
        [void]$frames.Add(@($bmp, ($sw.Elapsed.TotalMilliseconds - $t0)))
    }
    $lines = @()
    for ($i = 0; $i -lt $frames.Count; $i++) {
        $f = $frames[$i]
        $name = 'sweep_{0:D2}.png' -f $i
        $f[0].Save((Join-Path $OutDir $name), [System.Drawing.Imaging.ImageFormat]::Png)
        $f[0].Dispose()
        $lines += ('{0} {1:F0}ms' -f $name, $f[1])
    }
    $lines | Set-Content -Encoding UTF8 (Join-Path $OutDir 'sweep_times.txt')
    "captured $Count frames"
    break
}
