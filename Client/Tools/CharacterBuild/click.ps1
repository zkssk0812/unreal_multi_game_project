# 화면 좌표(픽셀)를 왼쪽 클릭한다 (에디터 모달 창 버튼 누르기용):  click.ps1 X Y
param([int]$X, [int]$Y)
Add-Type @"
using System; using System.Runtime.InteropServices;
public class VHC { [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
[DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, UIntPtr e); }
"@
[VHC]::SetCursorPos($X, $Y) | Out-Null
Start-Sleep -Milliseconds 150
[VHC]::mouse_event(0x2, 0, 0, 0, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 60
[VHC]::mouse_event(0x4, 0, 0, 0, [UIntPtr]::Zero)
"clicked $X,$Y"
