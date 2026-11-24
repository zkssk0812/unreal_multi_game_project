param([string]$Log, [string]$Bot = "Sol", [int]$TimeoutSec = 420, [string]$Out)
# 운영체제 커서 실제 확인 (2026-11-25): 봇 창 위에 실제 마우스를 올려 GetCursorInfo 로 커서가 보이는지·어떤 모양인지 읽는다.
# 화면 저장에는 운영체제 커서가 찍히지 않으므로 Windows 에게 직접 묻는다. 확인 시점은 봇 로그의 신호 줄로 정한다.
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class VHCur {
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X; public int Y; }
  [StructLayout(LayoutKind.Sequential)] public struct CURSORINFO { public int cbSize; public int flags; public IntPtr hCursor; public POINT pt; }
  [DllImport("user32.dll")] public static extern bool GetCursorInfo(ref CURSORINFO pci);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(int f, int dx, int dy, int d, IntPtr e);
  [DllImport("user32.dll")] public static extern IntPtr LoadCursor(IntPtr h, int id);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
}
"@
$arrow = [VHCur]::LoadCursor([IntPtr]::Zero, 32512)
$cross = [VHCur]::LoadCursor([IntPtr]::Zero, 32515)
function Find-Bot {
    $p = Get-CimInstance Win32_Process | Where-Object { $_.Name -eq 'UnrealEditor.exe' -and $_.CommandLine -match "-VHBot=$Bot " } | Select-Object -First 1
    if ($p) { return (Get-Process -Id $p.ProcessId).MainWindowHandle } else { return [IntPtr]::Zero }
}
function Check([string]$When) {
    $h = Find-Bot
    if ($h -ne [IntPtr]::Zero) { [void][VHCur]::SetForegroundWindow($h) }
    Start-Sleep -Milliseconds 300
    [void][VHCur]::SetCursorPos(1500, 300)
    Start-Sleep -Milliseconds 80
    [VHCur]::mouse_event(1, 6, 0, 0, [IntPtr]::Zero)     # 조금 움직여 커서 모양을 다시 묻게 한다
    Start-Sleep -Milliseconds 120
    [VHCur]::mouse_event(1, -6, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 400
    $ci = New-Object VHCur+CURSORINFO
    $ci.cbSize = [System.Runtime.InteropServices.Marshal]::SizeOf($ci)
    [void][VHCur]::GetCursorInfo([ref]$ci)
    $shape = if ($ci.hCursor -eq $arrow) { '화살표' } elseif ($ci.hCursor -eq $cross) { '십자' } elseif ($ci.hCursor -eq [IntPtr]::Zero) { '없음' } else { '기타 0x{0:X}' -f $ci.hCursor.ToInt64() }
    $fg = if ([VHCur]::GetForegroundWindow() -eq $h) { '봇 창이 앞' } else { '다른 창이 앞!' }
    $line = '{0} {1}: 커서 {2}, 모양 {3} ({4}, 위치 {5},{6})' -f (Get-Date -Format 'HH:mm:ss.fff'), $When, $(if ($ci.flags -band 1) { '보임' } else { '숨김' }), $shape, $fg, $ci.pt.X, $ci.pt.Y
    $line
    Add-Content -Encoding UTF8 $Out $line
    [void][VHCur]::SetCursorPos(1910, 1070)   # 끝나면 구석으로 (봇 조준을 덮지 않게)
}
Set-Content -Encoding UTF8 $Out "운영체제 커서 확인 ($Bot)"
$steps = @(
    @('게임 중 (조준점)', '\[개발 스크립트\] 12\.0초'),
    @('일시정지 중', '\[조준점\] 일시정지 4\.0초'),
    @('일시정지 푼 뒤', '\[조준점\] 일시정지 풂'),
    @('결과창 (메뉴 레벨)', '화면 전환: \d → 5'),
    @('로비·대기실 (메뉴 레벨)', '화면 전환: 5 → [34]')
)
$pos = 0; $i = 0
$sw = [System.Diagnostics.Stopwatch]::StartNew()
while ($i -lt $steps.Count -and $sw.Elapsed.TotalSeconds -lt $TimeoutSec) {
    Start-Sleep -Milliseconds 100
    if (-not (Test-Path $Log)) { continue }
    $fs = [System.IO.File]::Open($Log, 'Open', 'Read', 'ReadWrite')
    if ($fs.Length -lt $pos) { $pos = 0 }
    $fs.Seek($pos, 'Begin') | Out-Null
    $buf = New-Object byte[] ($fs.Length - $pos)
    $read = $fs.Read($buf, 0, $buf.Length); $fs.Close(); $pos += $read
    $text = [System.Text.Encoding]::UTF8.GetString($buf, 0, $read)
    while ($i -lt $steps.Count -and $text -match $steps[$i][1]) {
        if ($i -eq 1) { Start-Sleep -Milliseconds 500 }
        if ($i -eq 3 -or $i -eq 4) { Start-Sleep -Milliseconds 1500 }
        Check $steps[$i][0]
        $i++
    }
}
"cursor check done ($i/$($steps.Count))"
