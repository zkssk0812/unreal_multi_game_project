# Package the client (Win64, Development) for teammates who do not have Unreal installed.
#   powershell -ExecutionPolicy Bypass -File Tools\Package\package_client.ps1 -OutDir C:\UnrealProjects\Builds\VaultHeist_Client_YYYY-MM-DD
# - No project settings are changed: maps and folders to cook are given on the command line.
# - Cooked maps: L_MainMenu (start) + L_Mansion_Shell (game). Old maps (L_Mansion_1F, L_InteractionTest, template levels) are left out.
# - Development build: the console (` key) and logs stay available. Debug symbols (.pdb) are not copied.
param(
    [Parameter(Mandatory = $true)][string]$OutDir,
    [string]$Engine = 'C:\Users\FORYOUCOM\Desktop\UE_5.8',
    [string]$Project = 'C:\UnrealProjects\MCPTest\MCPTest.uproject'
)
$ErrorActionPreference = 'Stop'
$content = Join-Path (Split-Path $Project) 'Content'
$maps = '/Game/VaultHeist/Maps/L_MainMenu+/Game/VaultHeist/Maps/Mansion/L_Mansion_Shell'
# Folders whose assets are loaded by path or by config at run time (not only through the two maps)
$cookDirs = @('VaultHeist\Blueprints', 'VaultHeist\Data', 'VaultHeist\Input', 'VaultHeist\Materials', 'VaultHeist\UI', 'VaultHeist\Weapons', 'VaultHeist\Characters', 'VaultHeist\Consumables', 'VaultHeist\FX', 'VaultHeist\Audio') |
    ForEach-Object { Join-Path $content $_ }
$archive = "$OutDir._uat"
if (Test-Path $archive) { Remove-Item $archive -Recurse -Force }
$log = "$OutDir.package.log"
$uat = Join-Path $Engine 'Engine\Build\BatchFiles\RunUAT.bat'
$uatArgs = @(
    'BuildCookRun', "-project=`"$Project`"", '-noP4', '-platform=Win64', '-clientconfig=Development',
    '-build', '-cook', "-map=$maps", "-cookdir=`"$($cookDirs -join '+')`"",
    '-stage', '-pak', '-prereqs', '-nodebuginfo',
    '-archive', "-archivedirectory=`"$archive`"",
    '-nocompileeditor', '-skipbuildeditor', '-unattended', '-utf8output'
)
"RunUAT $($uatArgs -join ' ')" | Out-File $log -Encoding utf8
& $uat @uatArgs 2>&1 | Out-File $log -Append -Encoding utf8
$code = $LASTEXITCODE
"UAT exit code $code" | Out-File $log -Append -Encoding utf8
if ($code -ne 0) { Write-Output "FAILED (exit $code). See $log"; exit $code }
# UAT archives into <archive>\Windows. Move it so that the exe sits directly in OutDir.
$win = Join-Path $archive 'Windows'
if (-not (Test-Path $win)) { Write-Output "FAILED: $win not found. See $log"; exit 1 }
if (Test-Path $OutDir) { Remove-Item $OutDir -Recurse -Force }
Move-Item $win $OutDir
Remove-Item $archive -Recurse -Force
Write-Output "OK: $OutDir"
