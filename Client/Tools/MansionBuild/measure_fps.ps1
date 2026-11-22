# Measure client frame rate in the new level with different numbers of fill lights (one bot, real server on 127.0.0.1:9000).
# usage: measure_fps.ps1 -Tag f1 -Extra "-VHFill=2" [-Seconds 60] [-Mock] [-Char 2]  (-Char = bot character 1~3)
# Result: the "[성능]" lines of Saved/V3Test/fps_<Tag>.log (first two samples are skipped: loading / shader warm-up).
param([string]$Tag, [string]$Extra = "", [int]$Seconds = 60, [switch]$Mock, [switch]$OldLevel, [int]$Char = 1)
$ue = 'C:\Users\FORYOUCOM\Desktop\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe'
$prj = 'C:\UnrealProjects\MCPTest\MCPTest.uproject'
$log = "C:\UnrealProjects\MCPTest\Saved\V3Test\fps_$Tag.log"
if (Test-Path $log) { Remove-Item $log }
$net = if ($Mock) { '-VHMock' } else { '-VHServer=127.0.0.1:9000' }
$level = if ($OldLevel) { '' } else { '-VHLevel=/Game/VaultHeist/Maps/Mansion/L_Mansion_Shell' }
$args = "`"$prj`" -game -windowed -ResX=1600 -ResY=900 $net $level -VHBot=Fps$Tag -VHBotMin=1 -VHBotHeist -VHBotChar=$Char -log -abslog=$log $Extra"
$p = Start-Process -FilePath $ue -ArgumentList $args -PassThru
$started = $false
for ($i = 0; $i -lt 60; $i++) {
    Start-Sleep -Seconds 2
    if ((Test-Path $log) -and (Select-String -Path $log -Pattern '\[성능\]' -Quiet -Encoding UTF8)) { $started = $true; break }
}
if ($started) { Start-Sleep -Seconds $Seconds }
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
$vals = @(Select-String -Path $log -Pattern '\[성능\] 5초 평균 (\d+) fps' -Encoding UTF8 | ForEach-Object { [int]$_.Matches[0].Groups[1].Value })
$fill = (Select-String -Path $log -Pattern '-VHFill=\d+:.*' -Encoding UTF8 | Select-Object -First 1 | ForEach-Object { $_.Matches[0].Value })
if ($vals.Count -gt 2) {
    $use = $vals[2..($vals.Count - 1)]
    $avg = [math]::Round(($use | Measure-Object -Average).Average, 1)
    $min = ($use | Measure-Object -Minimum).Minimum
    "$Tag : avg $avg fps, min $min fps, samples $($use -join ' ')  $fill"
} else { "$Tag : not enough samples ($($vals -join ' ')) started=$started" }
