# -*- coding: utf-8 -*-
"""
에디터 아래쪽 Cmd 칸에 명령을 넣는 도우미 (Unreal MCP 의 SlateInspector 도구를 HTTP 로 직접 부른다).
  엔진파이썬 -I vhcmd.py "명령"            → Cmd 칸에 입력하고 Enter
  엔진파이썬 -I vhcmd.py --call 툴셋 도구 '{"인자":...}'  → 아무 MCP 도구나 부르고 결과 앞부분만 출력
에디터가 켜져 있고 MCP 서버(http://127.0.0.1:8000/mcp)가 살아 있어야 한다.
"""
import io
import json
import re
import sys
import urllib.request

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
URL = "http://127.0.0.1:8000/mcp"
SLATE = "SlateInspectorToolset.SlateInspectorToolset"


class Mcp:
    def __init__(self):
        self.sid = None
        self.n = 0
        r = self._post({"jsonrpc": "2.0", "id": self._id(), "method": "initialize",
                        "params": {"protocolVersion": "2025-03-26", "capabilities": {}, "clientInfo": {"name": "vhcmd", "version": "1"}}})
        self._post({"jsonrpc": "2.0", "method": "notifications/initialized"})

    def _id(self):
        self.n += 1
        return self.n

    def _post(self, body):
        req = urllib.request.Request(URL, data=json.dumps(body).encode("utf-8"), method="POST")
        req.add_header("Content-Type", "application/json")
        req.add_header("Accept", "application/json, text/event-stream")
        if self.sid:
            req.add_header("Mcp-Session-Id", self.sid)
        with urllib.request.urlopen(req, timeout=300) as resp:
            if not self.sid:
                self.sid = resp.headers.get("Mcp-Session-Id")
            raw = resp.read().decode("utf-8", errors="replace")
        if not raw.strip():
            return None
        if raw.lstrip().startswith("data:") or "\ndata:" in raw:
            raw = "\n".join(l[5:] for l in raw.splitlines() if l.startswith("data:"))
        return json.loads(raw)

    def call(self, toolset, tool, args):
        r = self._post({"jsonrpc": "2.0", "id": self._id(), "method": "tools/call",
                        "params": {"name": "call_tool", "arguments": {"toolset_name": toolset, "tool_name": tool, "arguments": args}}})
        content = r.get("result", {}).get("content", []) if r else []
        text = "\n".join(c.get("text", "") for c in content)
        try:
            v = json.loads(text).get("returnValue", text)
            return v if isinstance(v, str) else json.dumps(v, ensure_ascii=False)
        except Exception:
            return text


def find_cmd_textbox(m):
    snap = m.call(SLATE, "Snapshot", {"ref": "", "maxDepth": 30})
    # 아래쪽 Cmd 칸: 크기 382x22 정도의 menu (콘솔 입력 상자를 감싼 것)
    menus = re.findall(r"menu[^\n]*?size=(\d+),(\d+)\][^\n]*?\[ref=(\w+)\]", snap)
    cands = [ref for w, h, ref in menus if 300 <= int(w) <= 500 and 18 <= int(h) <= 26]
    for ref in cands:
        m.call(SLATE, "Observe", {"ref": ref})
        sub = m.call(SLATE, "Snapshot", {"ref": ref, "maxDepth": 30})
        tb = re.search(r"textbox[^\n]*?\[ref=(\w+)\]", sub)
        if tb:
            return tb.group(1)
    raise SystemExit("Cmd 칸을 찾지 못했습니다. menu 후보: %s\n%s" % (cands, snap[:1500]))


def main():
    m = Mcp()
    if sys.argv[1] == "--call":
        out = m.call(sys.argv[2], sys.argv[3], json.loads(sys.argv[4]) if len(sys.argv) > 4 else {})
        limit = int(sys.argv[5]) if len(sys.argv) > 5 else 3000
        print(out[:limit])
        return
    if sys.argv[1] == "--capture":
        # 에디터 화면 전체(UI 포함)를 PNG 로 저장: --capture 출력.png
        import base64
        r = m._post({"jsonrpc": "2.0", "id": m._id(), "method": "tools/call",
                     "params": {"name": "call_tool", "arguments": {"toolset_name": "EditorToolset.EditorAppToolset", "tool_name": "CaptureEditorImage", "arguments": {}}}})
        blob = json.dumps(r)
        mm = re.search(r'"data"\s*:\s*"([A-Za-z0-9+/=]{1000,})"', blob) or re.search(r'\\"data\\"\s*:\s*\\"([A-Za-z0-9+/=]{1000,})\\"', blob)
        if not mm:
            raise SystemExit("캡처 데이터를 찾지 못했습니다: " + blob[:500])
        open(sys.argv[2], "wb").write(base64.b64decode(mm.group(1)))
        print("저장:", sys.argv[2])
        return
    if sys.argv[1] == "--snapshot":
        print(m.call(SLATE, "Snapshot", {"ref": sys.argv[2] if len(sys.argv) > 2 else "", "maxDepth": 30})[:int(sys.argv[3]) if len(sys.argv) > 3 else 6000])
        return
    tb = find_cmd_textbox(m)
    m.call(SLATE, "Click", {"ref": tb})
    ok = m.call(SLATE, "Type", {"ref": tb, "text": sys.argv[1], "submit": True})
    if "false" in ok.lower():
        m.call(SLATE, "Click", {"ref": tb})
        ok = m.call(SLATE, "Type", {"ref": tb, "text": sys.argv[1], "submit": True})
    print("입력:", sys.argv[1], "→", ok[:200])


if __name__ == "__main__":
    main()
