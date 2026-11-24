param([string]$Log, [string]$OutDir, [int]$Count = 40, [int]$TimeoutSec = 400)
# 조준점 연속 프레임 촬영 (2026-11-25): 봇 로그에 신호 줄이 나오면 그 순간부터 화면(1920x1080)을 Count 장 연속으로 찍는다.
# 게임 안 화면 저장(프레임이 멈춤) 대신 바깥에서 찍어 실제 재생 그대로의 프레임을 얻는다. 신호: 권총 연사 시작 / 레일건 SC_FIRE(충전 시작)
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force $OutDir | Out-Null
$triggers = [ordered]@{ 'pistol' = '\[문 사격\] 열린 문으로 권총 연사'; 'rail' = '\[레일건\] 서버 SC_FIRE' }
$done = @{}
$pos = 0
$sw = [System.Diagnostics.Stopwatch]::StartNew()
while ($sw.Elapsed.TotalSeconds -lt $TimeoutSec -and $done.Count -lt $triggers.Count) {
    Start-Sleep -Milliseconds 25
    if (-not (Test-Path $Log)) { continue }
    $fs = [System.IO.File]::Open($Log, 'Open', 'Read', 'ReadWrite')
    if ($fs.Length -lt $pos) { $pos = 0 }
    $fs.Seek($pos, 'Begin') | Out-Null
    $buf = New-Object byte[] ($fs.Length - $pos)
    $read = $fs.Read($buf, 0, $buf.Length)
    $fs.Close()
    $pos += $read
    $text = [System.Text.Encoding]::UTF8.GetString($buf, 0, $read)
    foreach ($k in $triggers.Keys) {
        if ($done.ContainsKey($k) -or $text -notmatch $triggers[$k]) { continue }
        $frames = New-Object System.Collections.ArrayList
        $t0 = $sw.Elapsed.TotalMilliseconds
        for ($i = 0; $i -lt $Count; $i++) {
            $bmp = New-Object System.Drawing.Bitmap 1920, 1080
            $g = [System.Drawing.Graphics]::FromImage($bmp)
            $g.CopyFromScreen(0, 0, 0, 0, $bmp.Size)
            $g.Dispose()
            [void]$frames.Add(@($bmp, ($sw.Elapsed.TotalMilliseconds - $t0)))
        }
        $lines = @()
        for ($i = 0; $i -lt $frames.Count; $i++) {
            $f = $frames[$i]
            $name = '{0}_{1:D2}.png' -f $k, $i
            $f[0].Save((Join-Path $OutDir $name), [System.Drawing.Imaging.ImageFormat]::Png)
            $f[0].Dispose()
            $lines += ('{0} {1:F0}ms' -f $name, $f[1])
        }
        $lines | Set-Content -Encoding UTF8 (Join-Path $OutDir "$k`_times.txt")
        $done[$k] = $true
        "captured $k : $Count frames in {0:F0} ms" -f ($frames[$frames.Count - 1][1])
    }
}
"film done: $($done.Keys -join ',')"
