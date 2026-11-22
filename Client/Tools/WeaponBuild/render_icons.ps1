# 구상안 HTML 원본에서 게임에 쓰는 그림을 뽑는다 (모두 배경 투명 PNG).
#   powershell -ExecutionPolicy Bypass -File Tools\WeaponBuild\render_icons.ps1
# 결과 (Saved 는 Git 제외. 에디터로 임포트한 .uasset 만 커밋한다)
#   Saved\WeaponBuild\Icons\T_VHW_Icon_01_Baton.png …     HUD 아이콘: 구상안 옆모습 그림 그대로 (512×256)
#   Saved\WeaponBuild\FloorArt\T_VHW_Top_01_Baton.png …   바닥용: 구상안 탑뷰 그림 그대로 (512×256, 그림 120×60 단위 전체)
#   Saved\WeaponBuild\T_VHW_SideAtlas.png                 3D 모델 옆면에 입히는 그림: 옆모습 10종을 한 장에 (2048×2048, 2열×5행)
#   Saved\WeaponBuild\T_VHW_GlowAtlas.png                 같은 배치에 발광 부분(분류 색 선)만 → 재질의 발광 그림
#   Saved\WeaponBuild\Mask\side_NN.png                    3D 모델 윤곽을 따는 그림(본체만, 1단위 = 4픽셀) → trace_masks.ps1
#   Saved\WeaponBuild\FloorArt\T_VHW_TopGlow_09/10.png    지뢰 탑뷰의 발광 부분만
# 방법: HTML 원본의 그리기 코드를 그대로 쓰고 끝의 "페이지 만들기" 부분만 바꾼 임시 파일을 만든 뒤,
#       PC에 이미 있는 Chrome/Edge 의 화면 없는 모드(--headless)로 찍는다. 원본 HTML 은 고치지 않는다.
# -IconStyle Hud 를 주면 아이콘을 예전 방식(본체만 + 분류 색 후광)으로 뽑는다.
param(
	[ValidateSet("Concept", "Hud")] [string]$IconStyle = "Concept"
)
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Html = Join-Path $Root "Docs\Design\Weapons_v2.2\weapon_concepts_v22.html"
$OutRoot = Join-Path $Root "Saved\WeaponBuild"
New-Item -ItemType Directory -Force (Join-Path $OutRoot "Icons"), (Join-Path $OutRoot "FloorArt") | Out-Null

$Browser = @(
	"C:\Program Files\Google\Chrome\Application\chrome.exe",
	"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
	"C:\Program Files\Microsoft\Edge\Application\msedge.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $Browser) { throw "Chrome 또는 Edge 를 찾지 못했습니다." }

$Text = [IO.File]::ReadAllText($Html, [Text.Encoding]::UTF8)
$Cut = $Text.IndexOf("const q=new URLSearchParams")
$End = $Text.LastIndexOf("</script>")
if ($Cut -lt 0 -or $End -lt $Cut) { throw "HTML 구조가 바뀌었습니다(페이지 만들기 부분을 찾지 못함)." }

$Js = @'
const q=new URLSearchParams(location.search),root=document.getElementById('root'),mode=q.get('mode'),hud=q.get('style')==='Hud';
document.head.insertAdjacentHTML('beforeend','<style>html,body{background:transparent!important;overflow:hidden}</style>');
// 바닥 그림자(검은 타원·흐린 그림자)와 설명 글자("수납 시")는 무기 그림이 아니므로 뺀다
function dropExtras(el){
  el.querySelectorAll('ellipse').forEach(e=>{if((e.getAttribute('fill')||'')==='#000'&&parseFloat(e.getAttribute('cy'))>200)e.remove();});
  el.querySelectorAll('[filter="url(#fBlurL)"],[filter="url(#fBlurT)"]').forEach(e=>e.remove());
  el.querySelectorAll('text').forEach(t=>{if(/수납/.test(t.textContent))t.remove();});
}
// 무기 본체만: 삼단봉의 수납 모습(translate(292,72)), 도검의 칼집(<!-- 칼집 --> ~ <!-- 검 --> 사이)을 뺀다
function bodyOnly(el){
  el.querySelectorAll('[transform*="translate(292,72)"]').forEach(e=>e.remove());
  let drop=false;[...el.childNodes].forEach(n=>{if(n.nodeType===8){drop=n.data.includes('칼집');return;}if(drop)n.remove();});
}
// 발광만: 분류 색(var(--g))으로 그린 선·면만 남긴다 → 재질의 발광(Emissive) 그림
const glowCss='<style>.glowonly *{visibility:hidden}.glowonly [stroke="var(--g)"],.glowonly [fill="var(--g)"],.glowonly [style*="var(--g)"]{visibility:visible}.glowonly *{filter:none!important}</style>';
if(mode==='atlas'||mode==='glowatlas'){
  // 옆모습 10종을 2열×5행으로. 칸 하나 = 그림 600×240 단위 전체 → 모델의 UV 는 그림 좌표로 바로 계산한다.
  // fPad: 그림 가장자리 색을 바깥으로 조금 번지게 해서 모델 윤곽에서 검은 테가 생기지 않게 한다
  let s='<svg width="2048" height="2048" viewBox="0 0 2048 2048" style="display:block"><defs><filter id="fPad" filterUnits="userSpaceOnUse" x="-20" y="-20" width="640" height="280" color-interpolation-filters="sRGB"><feMorphology in="SourceGraphic" operator="dilate" radius="2.2" result="d"/><feMerge><feMergeNode in="d"/><feMergeNode in="SourceGraphic"/></feMerge></filter></defs>';
  const glow=mode==='glowatlas';
  W.forEach((o,k)=>{s+='<svg x="'+((k%2)*1024)+'" y="'+(Math.floor(k/2)*409.6)+'" width="1024" height="409.6" viewBox="0 0 600 240" style="--g:'+CAT[o.cat].color+'"><g class="art'+(glow?' glowonly':'')+'"'+(glow?'':' filter="url(#fPad)"')+'>'+fnOf(o,'Side')()+'</g></svg>';});
  root.innerHTML=(glow?glowCss:'')+s+'</svg>';
  root.querySelectorAll('.art').forEach(dropExtras);
}else if(mode==='mask'){
  // 3D 모델 윤곽용: 본체만, 그림 좌표 600×240 단위 = 2400×960 픽셀 (1단위 = 4픽셀). 투명도만 쓴다
  const o=W.find(x=>x.id===q.get('w'));
  root.innerHTML='<svg width="2400" height="960" viewBox="0 0 600 240" style="display:block;--g:'+CAT[o.cat].color+'"><g id="art">'+fnOf(o,'Side')()+'</g></svg>';
  const art=document.getElementById('art');dropExtras(art);bodyOnly(art);
}else if(mode==='topglow'){
  const o=W.find(x=>x.id===q.get('w'));
  root.innerHTML=glowCss+'<svg width="512" height="256" viewBox="0 0 120 60" style="display:block;--g:'+CAT[o.cat].color+'"><g class="glowonly">'+fnOf(o,'Top')()+'</g></svg>';
}else{
  const o=W.find(x=>x.id===q.get('w')),top=mode==='top',IW=512,IH=256;
  const halo='<defs><filter id="fIconHalo" filterUnits="userSpaceOnUse" x="-60" y="-60" width="720" height="360" color-interpolation-filters="sRGB">'
   +'<feMorphology in="SourceAlpha" operator="dilate" radius="1.4" result="d"/><feGaussianBlur in="d" stdDeviation="4.5" result="b"/>'
   +'<feFlood style="flood-color:var(--g)" flood-opacity=".5"/><feComposite in2="b" operator="in" result="halo"/>'
   +'<feMorphology in="SourceAlpha" operator="dilate" radius=".9" result="d2"/><feFlood flood-color="#dfe6f2" flood-opacity=".42"/><feComposite in2="d2" operator="in" result="rim"/>'
   +'<feMerge><feMergeNode in="halo"/><feMergeNode in="rim"/><feMergeNode in="SourceGraphic"/></feMerge></filter></defs>';
  root.innerHTML='<svg id="ic" width="'+IW+'" height="'+IH+'" viewBox="'+(top?'0 0 120 60':'0 0 600 240')+'" style="display:block;--g:'+CAT[o.cat].color+'">'+halo+'<g id="art">'+fnOf(o,top?'Top':'Side')()+'</g></svg>';
  const art=document.getElementById('art');
  dropExtras(art);
  if(!top){
    bodyOnly(art);	// 아이콘은 본체만 (사용자 결정 2026-11-16: 수납 모습·칼집은 빼고 그림은 구상안 그대로, 후광 없음)
    // 그림 크기에 맞춰 화면을 채운다 (탑뷰는 실제 크기를 맞춰야 하므로 그림 전체 120×60 그대로 둔다)
    const bb=art.getBBox(),pad=hud?14:8,asp=IW/IH;
    let w=bb.width+pad*2,h=bb.height+pad*2;
    if(w/h<asp)w=h*asp;else h=w/asp;
    document.getElementById('ic').setAttribute('viewBox',(bb.x+bb.width/2-w/2)+' '+(bb.y+bb.height/2-h/2)+' '+w+' '+h);
    if(hud)art.setAttribute('filter','url(#fIconHalo)');
  }
}
'@
$Temp = Join-Path $OutRoot "_art_page.html"
[IO.File]::WriteAllText($Temp, $Text.Substring(0, $Cut) + $Js + $Text.Substring($End), (New-Object Text.UTF8Encoding $false))
$Url = "file:///" + ($Temp -replace "\\", "/")

function Shot([string]$Png, [string]$Query, [int]$W, [int]$H) {
	if (Test-Path $Png) { Remove-Item $Png }
	$BrowserArgs = @("--headless", "--disable-gpu", "--hide-scrollbars", "--force-device-scale-factor=1",
		"--default-background-color=00000000", "--window-size=$W,$H", "--virtual-time-budget=4000",
		"--screenshot=`"$Png`"", "`"$Url`?$Query`"")
	Start-Process -FilePath $script:Browser -ArgumentList $BrowserArgs -Wait -WindowStyle Hidden
	if (-not (Test-Path $Png)) { throw "그림을 만들지 못했습니다: $Png" }
}

$Names = [ordered]@{ "01"="Baton"; "02"="Sword"; "03"="BeamSword"; "04"="Pistol"; "05"="Flamethrower"; "06"="Railgun"; "07"="Taser"; "08"="Grenade"; "09"="BlastMine"; "10"="ShockMine" }
foreach ($Id in $Names.Keys) {
	Shot (Join-Path $OutRoot ("Icons\T_VHW_Icon_{0}_{1}.png" -f $Id, $Names[$Id])) "w=$Id&mode=icon&style=$IconStyle" 512 256
	Shot (Join-Path $OutRoot ("FloorArt\T_VHW_Top_{0}_{1}.png" -f $Id, $Names[$Id])) "w=$Id&mode=top" 512 256
}
Shot (Join-Path $OutRoot "T_VHW_SideAtlas.png") "mode=atlas" 2048 2048
Shot (Join-Path $OutRoot "T_VHW_GlowAtlas.png") "mode=glowatlas" 2048 2048
New-Item -ItemType Directory -Force (Join-Path $OutRoot "Mask") | Out-Null
foreach ($Id in $Names.Keys) { Shot (Join-Path $OutRoot ("Mask\side_{0}.png" -f $Id)) "w=$Id&mode=mask" 2400 960 }
foreach ($Id in "09", "10") { Shot (Join-Path $OutRoot ("FloorArt\T_VHW_TopGlow_{0}_{1}.png" -f $Id, $Names[$Id])) "w=$Id&mode=topglow" 512 256 }
Remove-Item $Temp
"아이콘 {0}장({1}), 탑뷰 그림 {2}장, 옆모습 모음 {3} KB" -f (Get-ChildItem (Join-Path $OutRoot "Icons") -Filter *.png).Count, $IconStyle,
	(Get-ChildItem (Join-Path $OutRoot "FloorArt") -Filter *.png).Count, [int]((Get-Item (Join-Path $OutRoot "T_VHW_SideAtlas.png")).Length / 1KB)
