# 시험용 봇 클라이언트(-game 으로 띄운 UnrealEditor.exe)만 끈다. 에디터 창은 건드리지 않는다
Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe'" |
  Where-Object { $_.CommandLine -like '*-game*' } |
  ForEach-Object { Stop-Process -Id $_.ProcessId -Force; "stopped $($_.ProcessId)" }
