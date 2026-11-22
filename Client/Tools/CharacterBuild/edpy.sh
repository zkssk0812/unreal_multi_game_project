#!/bin/bash
# 에디터 Cmd 칸에 py 스크립트를 넣고, 끝 표시(TAG 끝)가 로그에 나올 때까지 기다려 그 사이 TAG 줄만 보여 준다.
#   edpy.sh TAG 초 "C:/.../script.py" 인자...
TAG="$1"; WAIT="$2"; shift 2
PY=/c/Users/FORYOUCOM/Desktop/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe
LOG=${VHLOG:-$(ls -t /c/UnrealProjects/MCPTest/Saved/Logs/MCPTest.log /c/UnrealProjects/MCPTest/Saved/Logs/MCPTest_2.log 2>/dev/null | head -1)}  # 가장 최근에 쓰인 에디터 로그
START=$(wc -l < "$LOG")
SCRIPT="$1"; shift
"$PY" -I /c/UnrealProjects/MCPTest/Tools/WeaponBuild/vhcmd.py "py \"$SCRIPT\" $*" > /dev/null
for i in $(seq 1 "$WAIT"); do
  if tail -n +"$START" "$LOG" | grep -q "\[$TAG\] 끝\|Traceback\|Error: "; then break; fi
  sleep 1
done
tail -n +"$START" "$LOG" | grep -a "\[$TAG\]\|Traceback\|Error\|LogPython: Warning" | sed 's/^\[[^]]*\]\[[^]]*\]//' | head -${LINES_MAX:-200}
