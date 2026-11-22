<#
  push_to_team_repo.ps1
  MCPTest(작업 폴더) → 팀 통합 저장소(zkssk0812/unreal_multi_game_project) Client/ 로 복사하고
  Client/ 만 커밋해서 main 에 push 한다.

  사용법 (PowerShell):
    powershell -ExecutionPolicy Bypass -File Tools\push_to_team_repo.ps1            # 복사 → 커밋 → push
    powershell -ExecutionPolicy Bypass -File Tools\push_to_team_repo.ps1 -DryRun    # 복사·스테이징·점검만 (커밋 안 함)
    powershell -ExecutionPolicy Bypass -File Tools\push_to_team_repo.ps1 -NoPush    # 커밋까지만 (push 안 함)

  규칙
    - 원래 저장소(LeeJuneWu/project_Unreal_Engine5.8)에는 push 하지 않는다. MCPTest 는 로컬 커밋만.
    - 팀 저장소의 Server/, Common/, Docs/, 공용 .gitignore/.gitattributes/README 는 건드리지 않는다.
    - 올리는 파일 = MCPTest 에서 Git 이 추적하는 파일 + 새 파일(.gitignore 에 걸리지 않는 것) - 아래 제외 목록.
    - 커밋 메시지 = MCPTest 마지막 커밋 메시지 앞에 "[client] ".
    - GitHub 한 파일 100MB 한도를 넘는 파일이 있으면 멈춘다.
#>
param(
    [string]$Source   = 'C:\UnrealProjects\MCPTest',
    [string]$TeamRepo = 'C:\UnrealProjects\unreal_multi_game_project',
    [string]$Branch   = 'main',
    [switch]$DryRun,
    [switch]$NoPush,
    [switch]$AllowDirty   # MCPTest 에 커밋 안 한 변경이 있어도 진행
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
$OutputEncoding = New-Object Text.UTF8Encoding $false
$TeamUrl = 'https://github.com/zkssk0812/unreal_multi_game_project'

function Write-Step($msg) { Write-Host "`n== $msg" -ForegroundColor Cyan }
function Stop-Script($msg) { Write-Host "`n[중단] $msg" -ForegroundColor Red; exit 1 }

# git 실행 (실패하면 중단). 출력은 줄 배열.
function Invoke-Git {
    param([string]$Repo, [Parameter(ValueFromRemainingArguments = $true)][string[]]$GitArgs)
    $eap = $ErrorActionPreference; $ErrorActionPreference = 'Continue'   # PS 5.1: git 의 stderr 를 오류로 보지 않게
    $out = & git -C $Repo -c core.quotepath=false @GitArgs 2>&1
    $code = $LASTEXITCODE; $ErrorActionPreference = $eap
    if ($code -ne 0) { Stop-Script ("git " + ($GitArgs -join ' ') + " 실패:`n" + ($out | Out-String)) }
    return $out | ForEach-Object { "$_" }
}

# ---------------------------------------------------------------- 제외 규칙
# 폴더(Source 기준 상대 경로, 앞부분 일치)
$ExcludeDirs = @(
    'Binaries/', 'Intermediate/', 'Saved/', 'DerivedDataCache/', 'Build/', '.vs/', '.git/',
    'Docs/ServerRef/'          # 서버 팀원 저장소 사본(참고 전용) - 팀 저장소에는 Server/ 가 따로 있음
)
# 파일(정규식, 상대 경로 전체에 대해)
$ExcludeRegex = @(
    '(^|/)Binaries/', '(^|/)Intermediate/',          # Plugins/*/Binaries 등
    '\.sln$', '\.slnx$', '\.suo$', '\.VC\.db$', '\.VC\.opendb$',
    '^Config/DefaultEditor\.ini$',                   # 개인 설정
    '^SourceArt/.*\.zip$',                           # 모델러·애니메이션 원본 압축(이미 풀어 둠)
    '^\.gitignore$', '^\.gitattributes$'             # MCPTest 전용 규칙 - 팀 저장소는 공용 규칙을 쓴다
)
$MaxBytes  = 100MB   # GitHub 한 파일 한도
$WarnBytes = 50MB    # GitHub 경고 기준
$LfsExt = @('uasset','umap','png','jpg','jpeg','tga','psd','exr','hdr','fbx','obj','blend','wav','mp3','ogg','mp4','pdf','docx','pptx','xlsx','hwp','lib','dll')

function Test-Excluded([string]$rel) {
    foreach ($d in $ExcludeDirs) { if ($rel.StartsWith($d, [StringComparison]::OrdinalIgnoreCase)) { return $true } }
    foreach ($r in $ExcludeRegex) { if ($rel -imatch $r) { return $true } }
    return $false
}

# ---------------------------------------------------------------- 0. 준비 확인
Write-Step '0. 준비 확인'
if (-not (Test-Path "$Source\MCPTest.uproject")) { Stop-Script "작업 폴더를 찾을 수 없음: $Source" }
if (-not (Test-Path "$TeamRepo\.git")) { Stop-Script "팀 저장소가 없음: $TeamRepo (git clone $TeamUrl `"$TeamRepo`")" }
$remote = (Invoke-Git $TeamRepo remote get-url origin) | Select-Object -First 1
if ($remote -notmatch 'zkssk0812/unreal_multi_game_project') { Stop-Script "팀 저장소 origin 이 예상과 다름: $remote" }
$cur = (Invoke-Git $TeamRepo rev-parse --abbrev-ref HEAD) | Select-Object -First 1
if ($cur -ne $Branch) { Stop-Script "팀 저장소 브랜치가 $Branch 가 아님: $cur" }
$teamDirty = Invoke-Git $TeamRepo status --porcelain --untracked-files=no
if ($teamDirty) { Stop-Script ("팀 저장소에 커밋 안 한 변경이 있음 (먼저 정리 필요):`n" + ($teamDirty -join "`n")) }

# MCPTest 미커밋 변경(올리는 파일 기준)
$srcDirty = Invoke-Git $Source status --porcelain | Where-Object { $_.Length -gt 3 } |
    Where-Object { -not (Test-Excluded ($_.Substring(3).Trim('"'))) }
if ($srcDirty) {
    Write-Host "MCPTest 에 커밋 안 한 변경(올라갈 파일):" -ForegroundColor Yellow
    $srcDirty | ForEach-Object { Write-Host "   $_" }
    if (-not $AllowDirty) { Stop-Script 'MCPTest 에서 먼저 커밋하세요. (그대로 올리려면 -AllowDirty)' }
}

# ---------------------------------------------------------------- 1. pull
Write-Step "1. 팀 저장소 $Branch pull (다른 팀원의 Server/Docs 변경 받기)"
Invoke-Git $TeamRepo pull --ff-only origin $Branch | ForEach-Object { Write-Host "   $_" }

# ---------------------------------------------------------------- 2. 올릴 파일 목록
Write-Step '2. 올릴 파일 목록 만들기'
$all = Invoke-Git $Source ls-files --cached --others --exclude-standard
$files = New-Object System.Collections.Generic.List[string]
$excluded = New-Object System.Collections.Generic.List[string]
$seen = @{}
foreach ($rel in $all) {
    if ([string]::IsNullOrWhiteSpace($rel) -or $seen.ContainsKey($rel)) { continue }
    $seen[$rel] = $true
    if (-not (Test-Path -LiteralPath (Join-Path $Source $rel) -PathType Leaf)) { continue }  # 지웠지만 커밋 전
    if (Test-Excluded $rel) { $excluded.Add($rel) } else { $files.Add($rel) }
}
Write-Host "   올릴 파일 $($files.Count)개, 목록에서 뺀 파일 $($excluded.Count)개"
if ($excluded.Count) { $excluded | ForEach-Object { Write-Host "     - 뺌: $_" } }

# 크기·LFS 포인터 점검
$tooBig = @(); $big = @(); $pointers = @(); $total = 0
foreach ($rel in $files) {
    $fi = Get-Item -LiteralPath (Join-Path $Source $rel)
    $total += $fi.Length
    if ($fi.Length -gt $MaxBytes) { $tooBig += "$rel ($([math]::Round($fi.Length/1MB,1))MB)" }
    elseif ($fi.Length -gt $WarnBytes) { $big += "$rel ($([math]::Round($fi.Length/1MB,1))MB)" }
    $ext = [IO.Path]::GetExtension($rel).TrimStart('.').ToLower()
    if ($LfsExt -contains $ext -and $fi.Length -lt 300) {
        $head = [IO.File]::ReadAllText($fi.FullName)
        if ($head.StartsWith('version https://git-lfs')) { $pointers += $rel }
    }
}
if ($tooBig)   { Stop-Script ("100MB 를 넘는 파일 (GitHub 에 올릴 수 없음):`n  " + ($tooBig -join "`n  ")) }
if ($pointers) { Stop-Script ("내용 대신 LFS 포인터만 있는 파일 (MCPTest 에서 git lfs pull 필요):`n  " + ($pointers -join "`n  ")) }
if ($big) { Write-Host ("   [주의] 50MB 넘는 파일:`n     " + ($big -join "`n     ")) -ForegroundColor Yellow }
Write-Host ("   원본 합계 {0:N1} MB" -f ($total / 1MB))

# ---------------------------------------------------------------- 3. Client/ 로 동기화
Write-Step '3. Client/ 로 복사 (바뀐 파일만, 지운 파일은 Client/ 안에서만 반영)'
$clientRoot = Join-Path $TeamRepo 'Client'
New-Item -ItemType Directory -Force $clientRoot | Out-Null
$keep = @{}
$copied = 0
foreach ($rel in $files) {
    $keep[$rel.ToLowerInvariant()] = $true
    $src = Join-Path $Source $rel
    $dst = Join-Path $clientRoot $rel
    $s = Get-Item -LiteralPath $src
    $need = $true
    if (Test-Path -LiteralPath $dst -PathType Leaf) {
        $d = Get-Item -LiteralPath $dst
        $need = ($d.Length -ne $s.Length) -or ($d.LastWriteTimeUtc -ne $s.LastWriteTimeUtc)
    }
    if ($need) {
        $dir = Split-Path -Parent $dst
        if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }
        Copy-Item -LiteralPath $src -Destination $dst -Force
        $copied++
    }
}
# 팀 저장소에 올라가 있는 Client/ 파일 중 MCPTest 에 없는 것 → 삭제
$removed = 0
$tracked = Invoke-Git $TeamRepo ls-files -- Client
foreach ($t in $tracked) {
    $rel = $t.Substring('Client/'.Length)
    if ($rel -eq '.gitkeep') { continue }
    if (-not $keep.ContainsKey($rel.ToLowerInvariant())) {
        $p = Join-Path $clientRoot $rel
        if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Force }
        $removed++
        Write-Host "     - 삭제 반영: Client/$rel"
    }
}
Write-Host "   복사 ${copied}개, 삭제 반영 ${removed}개"

# ---------------------------------------------------------------- 4. 스테이징 (Client/ 만)
Write-Step '4. Client/ 스테이징'
Invoke-Git $TeamRepo add -A -- Client | Out-Null
# 공용 .gitignore 에 걸리는 파일 확인 (*.obj 는 LFS 로 강제 추가)
$forced = 0; $ignoredOther = @()
# (PS 5.1 은 git 표준 입력에 CRLF 를 보내므로 check-ignore --stdin 대신 무시된 파일 목록에서 고른다)
$ign = @(Invoke-Git $TeamRepo ls-files --others --ignored --exclude-standard -- Client |
    Where-Object { $keep.ContainsKey($_.Substring('Client/'.Length).ToLowerInvariant()) })
foreach ($p in $ign) {
    if ($p -imatch '\.obj$') {
        Invoke-Git $TeamRepo add -f -- $p | Out-Null; $forced++
    } else { $ignoredOther += $p }
}
Write-Host "   .obj 강제 추가 ${forced}개"
if ($ignoredOther) { Write-Host ("   [주의] 공용 .gitignore 때문에 안 올라가는 파일:`n     " + ($ignoredOther -join "`n     ")) -ForegroundColor Yellow }

$staged = @(Invoke-Git $TeamRepo diff --cached --name-only)
$outside = $staged | Where-Object { -not $_.StartsWith('Client/') }
if ($outside) { Stop-Script ("Client/ 밖의 파일이 스테이징됨 (커밋 안 함):`n  " + ($outside -join "`n  ")) }

# ---------------------------------------------------------------- 5. LFS 점검
Write-Step '5. LFS 점검'
$lfsBad = @()
$lfsBytes = 0; $gitBytes = 0; $lfsCount = 0; $gitCount = 0
if ($staged) {
    $status = Invoke-Git $TeamRepo lfs status
    $inCommit = $false
    foreach ($line in $status) {
        if ($line -match '^Objects to be committed') { $inCommit = $true; continue }
        if ($line -match '^Objects not staged') { $inCommit = $false; continue }
        if (-not $inCommit) { continue }
        if ($line -match '^\s+(.+?) \((LFS|Git): ') {
            $p = $Matches[1]; $kind = $Matches[2]
            if ($p -match ' -> ') { $p = ($p -split ' -> ')[-1] }
            $ext = [IO.Path]::GetExtension($p).TrimStart('.').ToLower()
            if ($kind -eq 'Git' -and $LfsExt -contains $ext) { $lfsBad += $p }
        }
    }
}
foreach ($rel in $files) {
    $len = (Get-Item -LiteralPath (Join-Path $Source $rel)).Length
    $ext = [IO.Path]::GetExtension($rel).TrimStart('.').ToLower()
    if ($LfsExt -contains $ext) { $lfsBytes += $len; $lfsCount++ } else { $gitBytes += $len; $gitCount++ }
}
Write-Host ("   LFS 파일 {0}개 {1:N1} MB / 일반 git 파일 {2}개 {3:N1} MB (Client/ 전체 기준)" -f $lfsCount, ($lfsBytes/1MB), $gitCount, ($gitBytes/1MB))
Write-Host "   이번 커밋에 바뀐 파일: $($staged.Count)개"
if ($lfsBad) { Stop-Script ("LFS 로 가야 하는데 일반 git 으로 잡힌 파일 (push 안 함):`n  " + ($lfsBad -join "`n  ")) }

if ($DryRun) {
    Write-Host "`n[DryRun] 스테이징까지만 했습니다. 되돌리려면: git -C `"$TeamRepo`" reset -q -- Client ; git -C `"$TeamRepo`" checkout -- Client" -ForegroundColor Yellow
    exit 0
}

# ---------------------------------------------------------------- 6. 커밋
Write-Step '6. 커밋'
if ($staged) {
    $msg = (Invoke-Git $Source log -1 --format=%B) -join "`n"
    $msg = "[client] " + $msg.Trim()
    $tmp = [IO.Path]::GetTempFileName()
    [IO.File]::WriteAllText($tmp, $msg + "`n", (New-Object Text.UTF8Encoding $false))
    Invoke-Git $TeamRepo commit -q -F $tmp | Out-Null
    Remove-Item $tmp
    Write-Host ("   커밋: " + ((Invoke-Git $TeamRepo log -1 --format='%h %s') | Select-Object -First 1))
} else {
    Write-Host '   바뀐 파일 없음 (새 커밋 없음)'
}

# ---------------------------------------------------------------- 7. push
$ahead = [int]((Invoke-Git $TeamRepo rev-list --count "origin/$Branch..HEAD") | Select-Object -First 1)
if ($ahead -eq 0) { Write-Host "`n완료: push 할 커밋 없음."; exit 0 }
if ($NoPush) { Write-Host "`n[NoPush] 커밋 ${ahead}개가 push 대기 중입니다. 확인 뒤 다시 실행하면 push 합니다." -ForegroundColor Yellow; exit 0 }

Write-Step "7. push → $TeamUrl ($Branch)"
$ErrorActionPreference = 'Continue'
& git -C $TeamRepo push origin $Branch 2>&1 | ForEach-Object { Write-Host "   $_" }
$pushCode = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($pushCode -ne 0) {
    Write-Host '   push 거절 → 최신 main 위로 다시 올린 뒤 재시도' -ForegroundColor Yellow
    Invoke-Git $TeamRepo pull --rebase origin $Branch | ForEach-Object { Write-Host "   $_" }
    $ErrorActionPreference = 'Continue'
    & git -C $TeamRepo push origin $Branch 2>&1 | ForEach-Object { Write-Host "   $_" }
    $pushCode = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($pushCode -ne 0) { Stop-Script 'push 실패. 위 메시지를 확인하세요.' }
}
Write-Host ("`n완료: " + ((Invoke-Git $TeamRepo log -1 --format='%H %s') | Select-Object -First 1)) -ForegroundColor Green
