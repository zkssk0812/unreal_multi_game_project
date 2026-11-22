# 에디터 창을 최대화하고 앞으로 가져온다 (뒤에 있으면 에디터가 초당 몇 프레임으로 느려져 HighResShot 이 끝나지 않는다)
Add-Type @"
using System; using System.Runtime.InteropServices;
public class VHW { [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c); [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
[DllImport("user32.dll")] public static extern void keybd_event(byte v, byte s, uint f, UIntPtr e); }
"@
$p = Get-Process UnrealEditor -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 -and $_.MainWindowTitle -like '*MCPTest*' } | Select-Object -First 1
if ($p) {
  [VHW]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero); [VHW]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)   # Alt 를 눌렀다 떼야 SetForegroundWindow 가 먹는다
  [VHW]::ShowWindow($p.MainWindowHandle, 9) | Out-Null   # 최소화돼 있으면 먼저 복원 (최소화 상태면 Cmd 칸을 못 찾는다)
  [VHW]::ShowWindow($p.MainWindowHandle, 3) | Out-Null
  [VHW]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
  "editor $($p.Id) foreground"
}
