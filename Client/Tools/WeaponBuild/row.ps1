param([string]$Prefix, [int]$N, [string]$Out, [string]$Labels)
$d = "C:\UnrealProjects\MCPTest\Saved\WeaponBuild\Anim"
$files = 1..$N | ForEach-Object { Join-Path $d ("{0}_{1}.png" -f $Prefix, $_) }
& "C:\UnrealProjects\MCPTest\Tools\WeaponBuild\montage.ps1" -Out (Join-Path $d $Out) -Cols $N -Files $files -Labels ($Labels.Split("|"))
