#!/bin/bash
# 에디터를 (꺼져 있으면) 열고 MCP 를 기다린 뒤 미리보기 레벨(L_CharPreview)을 ▶ 한다. 창은 최대화 (2026-11-20)
#   bash Tools/WeaponBuild/open_preview_pie.sh [레벨 경로]
cd /c/UnrealProjects/MCPTest
PY=/c/Users/FORYOUCOM/Desktop/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe
LEVEL=${1:-/Game/VaultHeist/Maps/Dev/L_CharPreview}
if ! tasklist //FI "IMAGENAME eq UnrealEditor.exe" | grep -q UnrealEditor; then
  cmd //c "Saved\\V3Test\\open_editor.bat" > /dev/null 2>&1 < /dev/null
fi
for i in $(seq 1 90); do
  sleep 4
  if curl -s -m 3 -X POST http://127.0.0.1:8000/mcp -H "Content-Type: application/json" -H "Accept: application/json, text/event-stream" \
     -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"t","version":"1"}}}' | grep -q result; then
    echo "MCP 연결 ($((i*4))초)"; break
  fi
done
sleep 15
$PY -I Tools/WeaponBuild/vhcmd.py --call editor_toolset.toolsets.scene.SceneTools load_level "{\"level_path\":\"$LEVEL\"}" > /dev/null
$PY -I Tools/WeaponBuild/vhcmd.py --call EditorToolset.EditorAppToolset StartPIE '{"options":{"bSimulate":false,"playMode":"PlayMode_InViewPort","warmupSeconds":3}}' > /dev/null
powershell -Command "Add-Type -Name W -Namespace U -MemberDefinition '[DllImport(\"user32.dll\")] public static extern bool ShowWindow(IntPtr h,int c);'; [U.W]::ShowWindow((Get-Process UnrealEditor).MainWindowHandle,3)" > /dev/null
powershell -ExecutionPolicy Bypass -File Tools/CharacterBuild/fg_editor.ps1 > /dev/null
sleep 3
$PY -I Tools/WeaponBuild/vhcmd.py "vh.Ring 0" > /dev/null || { sleep 3; $PY -I Tools/WeaponBuild/vhcmd.py "vh.Ring 0" > /dev/null; }
echo "PIE 시작: $LEVEL"
