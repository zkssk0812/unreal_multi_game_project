# -*- coding: utf-8 -*-
"""
배치 JSON이 서버에서 읽히고 규칙 검사를 통과하는지 미리 확인한다 (서버 코드는 고치지 않고 읽기만).

1) 서버 MapLoader.cpp Parse()가 요구하는 항목·타입 확인
2) BuildNavGrid / LabelRegions / BuildReturnCells / ValidateMap / MapWarnings 를 파이썬으로 옮겨 실행
   (Content/Game/MapLoader.cpp, MapData.cpp 2026-10-27 master 기준, 벽 넓힘 NavWallPaddingCm = 25)
3) 경비원 시작·순찰 지점·순찰 연결선이 클라 배치(옷장·창고 벽·금고·정문 막음 등)와 겹치거나 막히는지

사용: python -I Tools/ServerHandoff/check_server_layout.py <map.json> [--report <txt>]
"""
import argparse, json, math, sys

PAD = 25.0
THIEF_SPAWNS_MIN, SAFE_COUNT, RETURN_POINT_COUNT = 3, 5, 5


def need(d, path):
    cur = d
    for k in path.split("."):
        if not isinstance(cur, dict) or k not in cur:
            raise KeyError("항목 없음: " + path)
        cur = cur[k]
    return cur


def check_schema(d):
    # MapLoader.cpp Parse()에서 at()으로 읽는 것(없으면 예외)
    for p in ["meta.map_id", "meta.version", "meta.bounds", "spawns.thief", "spawns.guard", "vault_slots", "hiding_spots",
              "rooms", "exit.pos", "exit.interact_pos", "exit.escape_zone", "jail.door.pos", "jail.door.interact_pos",
              "jail.cells", "jail.zone", "patrol.nodes", "patrol.edges", "colliders.static", "nav_grid.cell", "nav_grid.rows",
              "nav_grid.cols", "nav_grid.origin_x", "nav_grid.origin_y"]:
        need(d, p)
    for a in ("xmin", "xmax", "ymin", "ymax"):
        need(d, "meta.bounds." + a); need(d, "exit.escape_zone." + a); need(d, "jail.zone." + a)
    for s in d["spawns"]["thief"] + d["spawns"]["guard"]:
        assert len(s["pos"]) == 3
    for s in d["vault_slots"] + d["hiding_spots"]:
        assert len(s["pos"]) == 3 and len(s["interact_pos"]) == 3
    for r in d["rooms"]:
        for a in ("id", "xmin", "xmax", "ymin", "ymax"):
            assert a in r, "rooms." + a
    for c in d["colliders"]["static"] + d["colliders"].get("dynamic", []):
        for a in ("zmin", "zmax", "blocks_move", "blocks_sight"):
            assert a in c, c["id"] + "." + a
        if c.get("shape", "box") == "box":
            for a in ("xmin", "xmax", "ymin", "ymax"):
                assert a in c, c["id"] + "." + a
    ids = sorted(n["id"] for n in d["patrol"]["nodes"])
    assert ids == list(range(len(ids))), "patrol.nodes id가 0..N-1이 아님"


class Nav:
    def __init__(self, d):
        g = d["nav_grid"]
        self.cell, self.rows, self.cols, self.ox, self.oy = g["cell"], g["rows"], g["cols"], g["origin_x"], g["origin_y"]
        floors = d.get("floors", [])
        self.cells = [["#" if floors else "." for _ in range(self.cols)] for _ in range(self.rows)]
        if floors:
            for r in range(self.rows):
                for k in range(self.cols):
                    x, y = self.to_world(r, k)
                    if any(f["xmin"] <= x <= f["xmax"] and f["ymin"] <= y <= f["ymax"] for f in floors):
                        self.cells[r][k] = "."
        for c in d["colliders"]["static"]:            # dynamic(금고·창고 문·탈출구 문)은 막지 않는다
            if not c["blocks_move"]:
                continue
            x0, x1, y0, y1 = c["xmin"] - PAD, c["xmax"] + PAD, c["ymin"] - PAD, c["ymax"] + PAD
            rlo = max(0, math.floor((self.ox - x1) / self.cell)); rhi = min(self.rows - 1, math.ceil((self.ox - x0) / self.cell) - 1)
            clo = max(0, math.floor((y0 - self.oy) / self.cell)); chi = min(self.cols - 1, math.ceil((y1 - self.oy) / self.cell) - 1)
            mark = "#" if c["blocks_sight"] else "~"
            for r in range(rlo, rhi + 1):
                for k in range(clo, chi + 1):
                    if self.cells[r][k] != "#":
                        self.cells[r][k] = mark
        self.label()

    def to_world(self, r, k):
        return self.ox - (r + 0.5) * self.cell, self.oy + (k + 0.5) * self.cell

    def to_cell(self, p):
        r = math.floor((self.ox - p[0]) / self.cell); k = math.floor((p[1] - self.oy) / self.cell)
        return (r, k) if 0 <= r < self.rows and 0 <= k < self.cols else None

    def at(self, r, k):
        return self.cells[r][k] if 0 <= r < self.rows and 0 <= k < self.cols else "#"

    def label(self):
        self.reg = [[0] * self.cols for _ in range(self.rows)]
        nxt = 0
        for r0 in range(self.rows):
            for k0 in range(self.cols):
                if self.reg[r0][k0] or self.at(r0, k0) != ".":
                    continue
                nxt += 1
                self.reg[r0][k0] = nxt
                q = [(r0, k0)]
                for r, k in q:
                    for dr, dk in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                        nr, nk = r + dr, k + dk
                        if self.at(nr, nk) == "." and not self.reg[nr][nk]:
                            self.reg[nr][nk] = nxt
                            q.append((nr, nk))

    def walkable(self, p):
        c = self.to_cell(p)
        return c is not None and self.at(*c) == "."

    def region(self, p):
        c = self.to_cell(p)
        return self.reg[c[0]][c[1]] if c else 0

    def connected(self, a, b):
        ra = self.region(a)
        return ra != 0 and ra == self.region(b)


def inside(a, x, y):
    return a["xmin"] <= x <= a["xmax"] and a["ymin"] <= y <= a["ymax"]


def seg_hits_box(p, q, b, pad):
    """선분 p→q가 넓힌 상자와 만나는가 (slab 방식)"""
    x0, x1, y0, y1 = b["xmin"] - pad, b["xmax"] + pad, b["ymin"] - pad, b["ymax"] + pad
    t0, t1 = 0.0, 1.0
    for s, d, lo, hi in ((p[0], q[0] - p[0], x0, x1), (p[1], q[1] - p[1], y0, y1)):
        if abs(d) < 1e-9:
            if s < lo or s > hi:
                return False
        else:
            a, b2 = (lo - s) / d, (hi - s) / d
            if a > b2:
                a, b2 = b2, a
            t0, t1 = max(t0, a), min(t1, b2)
            if t0 > t1:
                return False
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("map")
    ap.add_argument("--report")
    a = ap.parse_args()
    d = json.load(open(a.map, encoding="utf-8"))
    out = []
    say = out.append

    check_schema(d)
    say("[형식] MapLoader Parse()가 읽는 항목 모두 있음")
    nav = Nav(d)
    spawn0 = d["spawns"]["thief"][0]["pos"]

    # BuildReturnCells
    rp_rooms = 0
    for rm in d["rooms"]:
        cnt = 0
        if rm.get("return_point", True) and nav.region(spawn0):
            for r in range(nav.rows):
                for k in range(nav.cols):
                    x, y = nav.to_world(r, k)
                    if inside(rm, x, y) and nav.reg[r][k] == nav.region(spawn0) and not inside(d["jail"]["zone"], x, y) \
                            and not inside(d["exit"]["escape_zone"], x, y):
                        cnt += 1
        rm["_cells"] = cnt
        rp_rooms += cnt > 0

    # ValidateMap
    errors = []
    if len(d["spawns"]["thief"]) < THIEF_SPAWNS_MIN: errors.append("의적 스폰 부족")
    if len(d["vault_slots"]) < SAFE_COUNT: errors.append("금고 후보 부족")
    if rp_rooms <= RETURN_POINT_COUNT: errors.append("반납 가능 방 부족")
    if not d["jail"]["cells"]: errors.append("감금 자리 없음")

    def chk(p, what):
        if not nav.walkable(p): errors.append(what + " 이동할 수 없는 칸")
        elif not nav.connected(spawn0, p): errors.append(what + " 의적 스폰에서 못 감")
    for i, s in enumerate(d["spawns"]["thief"]): chk(s["pos"], "의적 스폰 %d" % i)
    for i, s in enumerate(d["vault_slots"]): chk(s["interact_pos"], "금고 자리 %d(%s) 상호작용 위치" % (i, s.get("client", s["id"])))
    for i, s in enumerate(d["hiding_spots"]): chk(s["interact_pos"], "숨는 곳 %d(%s) 상호작용 위치" % (i, s.get("client", s["id"])))
    for i, c in enumerate(d["jail"]["cells"]): chk(c, "감금 자리 %d" % i)
    chk(d["exit"]["interact_pos"], "탈출구 상호작용 위치")
    chk(d["jail"]["door"]["interact_pos"], "창고 문 상호작용 위치")
    for i, g in enumerate(d["spawns"]["guard"]):
        if not nav.walkable(g["pos"]): errors.append("경비원 스폰 %d 이동할 수 없는 칸" % i)
    say("[ValidateMap] " + ("통과" if not errors else "실패: " + " / ".join(errors)))
    say("  반납 지점을 놓을 수 있는 방: %d곳 (return_point=false %d곳)" % (rp_rooms, sum(not r.get("return_point", True) for r in d["rooms"])))

    # MapWarnings
    warns = []
    for i, g in enumerate(d["spawns"]["guard"]):
        if not nav.connected(spawn0, g["pos"]): warns.append("경비원 스폰 %d (%s, %s) 의적 쪽과 이어지지 않음" % (i, g.get("room", ""), g["pos"][:2]))
    for n in d["patrol"]["nodes"]:
        p = n["pos"]
        if not nav.walkable(p): warns.append("순찰 지점 %d (%s, %s) 이동할 수 없는 칸" % (n["id"], n.get("room", ""), p[:2]))
        elif not nav.connected(spawn0, p): warns.append("순찰 지점 %d (%s, %s) 의적 쪽과 이어지지 않음" % (n["id"], n.get("room", ""), p[:2]))
    for rm in d["rooms"]:
        if rm.get("return_point", True) and rm["_cells"] == 0: warns.append("방 %s 반납 칸 없음" % rm["id"])
    say("[MapWarnings] %d개" % len(warns))
    for w in warns: say("  - " + w)

    # 탈출 구역: 걸을 수 있고 의적 스폰과 이어진 칸이 있는가 (탈출구 문은 dynamic이라 격자를 막지 않는다)
    ez = d["exit"]["escape_zone"]
    total = ok = 0
    for r in range(nav.rows):
        for k in range(nav.cols):
            x, y = nav.to_world(r, k)
            if inside(ez, x, y):
                total += 1
                ok += nav.reg[r][k] != 0 and nav.reg[r][k] == nav.region(spawn0)
    say("[탈출 구역] 칸 %d개 중 걸을 수 있고 스폰과 이어진 칸 %d개%s" % (total, ok, "" if ok else "  ← 서버에서 걸어 들어갈 수 없음"))

    # 클라 배치와 서버 경비원 값의 겹침 (벽 제외 충돌체 = 클라가 놓은 것)
    props = [c for c in d["colliders"]["static"] + d["colliders"].get("dynamic", []) if c["kind"] != "wall"]
    nodes = {n["id"]: n for n in d["patrol"]["nodes"]}
    say("[겹침] 경비원 시작·순찰 지점 50cm 안의 클라 오브젝트 / 클라 오브젝트를 지나는 순찰 연결선 (넓힘 %dcm)" % PAD)
    hits = 0
    for g in d["spawns"]["guard"]:
        for c in props:
            if seg_hits_box(g["pos"], g["pos"], c, 50):
                say("  - 경비원 시작 %s %s ↔ %s" % (g["id"], g["pos"][:2], c["id"])); hits += 1
    for n in d["patrol"]["nodes"]:
        for c in props:
            if seg_hits_box(n["pos"], n["pos"], c, 50):
                say("  - 순찰 지점 %d %s ↔ %s" % (n["id"], n["pos"][:2], c["id"])); hits += 1
    for e in d["patrol"]["edges"]:
        p, q = nodes[e[0]]["pos"], nodes[e[1]]["pos"]
        for c in props:
            if seg_hits_box(p, q, c, PAD):
                say("  - 순찰 연결 %d→%d ↔ %s (%s)" % (e[0], e[1], c["id"], c["kind"])); hits += 1
    if not hits:
        say("  없음")

    text = "\n".join(out)
    if a.report:
        open(a.report, "w", encoding="utf-8").write(text + "\n")
    sys.stdout.buffer.write((text + "\n").encode("utf-8"))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
