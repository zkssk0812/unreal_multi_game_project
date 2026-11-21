# -*- coding: utf-8 -*-
"""
Mansion_1F_Collision.json (클라 팀 충돌 정보) → 서버 맵 JSON (Data/Maps/mansion_1f_server.json).

사용:
    python tools/collision_to_map.py                       # 기본 경로
    python tools/collision_to_map.py 입력.json 출력.json

충돌 파일에는 벽·유리·기둥·소품만 있다. 서버가 더 필요한 것은 이 스크립트가 벽 모양으로 만든다.
  - 방: 문을 닫았다고 보고(문 위 벽 above_door를 바닥까지 내림) 벽·유리·기둥으로 막힌 공간을 25cm 격자로 칠해서 찾는다.
        이름은 그 안에 놓인 소품의 room 값(가장 많은 것). 소품이 없으면 Area_n.
        방 모양은 직사각형이 아닐 수 있어(복도) 직사각형 여러 개(rects)로 나눠 적는다.
  - 문: above_door 조각. 바깥 벽의 문(exterior)은 닫는다(건물 밖은 게임 공간이 아님) → 막는 충돌체를 더한다.
  - 순찰 지점: 방마다 걸을 수 있는 지점 몇 개(큰 방은 7m 간격) + 안쪽 문마다 하나. 같은 방 안 가까운 지점끼리, 문은 양쪽 방과 잇는다.
  - 스폰: 아래 THIEF_SPAWNS / GUARD_SPAWNS. 의적 시작·리스폰은 서버가 방 안 무작위 칸에서 고르므로(A-26)
    THIEF_SPAWNS는 '걸어서 닿는 영역'을 정하는 기준 지점일 뿐이다. GUARD_SPAWNS는 임시 (클라 팀 값이 오면 바꾼다).
금고·상자·의적 위치는 맵에 적지 않는다. 서버가 게임마다 방 안의 빈 칸에서 무작위로 고른다.
"""

import json
import math
import os
import sys
from collections import Counter, deque

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "Data", "Maps", "Mansion_1F_Collision.json")
DST = os.path.join(ROOT, "Data", "Maps", "mansion_1f_server.json")

ROOM_CELL = 25.0          # 방 찾기 격자
NAV_CELL = 50.0           # 서버 경로 격자 (서버가 칸을 다시 만든다. 틀만 적음)
NAV_PAD = 25.0            # 서버 GameData::NavWallPaddingCm 과 같게
MIN_ROOM_M2 = 4.0         # 이보다 작은 닫힌 공간은 벽 사이 틈으로 보고 버림
PATROL_SPACING = 700.0    # 큰 방의 순찰 지점 간격
LOW_PROP_CM = 100.0       # 이보다 낮은 소품은 시야를 막지 않음 (가슴 높이 100cm)
NOT_ROOMS = {"Corridor"}  # 반납 지점·상자·금고를 놓지 않는 공간 (복도)

# 의적 기준 지점(현관, 실제 시작은 서버가 무작위) · 임시 경비원 스폰(A-26): 경비실·식당·중앙 홀에 하나씩
THIEF_SPAWNS = [(-400.0, 2700.0, -90.0), (-200.0, 2700.0, -90.0), (0.0, 2700.0, -90.0)]
# 서버 맵 버전 (SC_GAME_LOAD.map_version). 클라 레벨 설정 GameLevelMapVersion과 같아야 한다.
# 예전 블록아웃 맵(v2·v3)과 겹치지 않게 새 맵은 10부터. 맵이 바뀌면 올리고 클라 팀에 알린다
MAP_VERSION = 10
GUARD_SPAWNS = [(-4300.0, 1750.0, 0.0), (2500.0, -1200.0, 180.0), (-300.0, -700.0, 90.0)]


def box(item):
    (x0, y0, z0), (x1, y1, z1) = item["min"], item["max"]
    return x0, x1, y0, y1, z0, z1


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else SRC
    dst = sys.argv[2] if len(sys.argv) > 2 else DST
    with open(src, encoding="utf-8") as f:
        col = json.load(f)
    meta = col["meta"]
    (bx0, by0), (bx1, by1) = meta["bounds"]["min"], meta["bounds"]["max"]

    # ── 1) 서버 충돌체 ──
    colliders = []

    def add(cid, kind, x0, x1, y0, y1, z0, z1, move, sight, extra=None):
        c = {"id": cid, "kind": kind, "xmin": x0, "xmax": x1, "ymin": y0, "ymax": y1,
             "zmin": z0, "zmax": z1, "blocks_move": move, "blocks_sight": sight}
        if extra:
            c.update(extra)
        colliders.append(c)

    doors = []
    for i, w in enumerate(col["walls"]):
        x0, x1, y0, y1, z0, z1 = box(w)
        part = w.get("part", "wall")
        # 문 위·창 위 벽은 사람 높이보다 위라 이동을 막지 않음. 창 아래(5cm)는 유리와 함께 막는다
        add(f"wall_{i:03d}", part, x0, x1, y0, y1, z0, z1, z0 < 180.0, True, {"type": w.get("type")})
        if part == "above_door":
            doors.append({"x0": x0, "x1": x1, "y0": y0, "y1": y1, "exterior": w.get("type") == "exterior"})
    for i, g in enumerate(col["glass"]):
        x0, x1, y0, y1, z0, z1 = box(g)
        add(f"glass_{i:03d}", "glass", x0, x1, y0, y1, z0, z1, True, False)   # 유리: 지나갈 수 없고 보임
    for i, c in enumerate(col["columns"]):
        if c.get("shape") == "cylinder":
            cx, cy, _ = c["center"]
            colliders.append({"id": f"column_{i:03d}", "kind": "column", "shape": "circle", "cx": cx, "cy": cy,
                              "r": c["radius"], "zmin": 0.0, "zmax": c["height"], "blocks_move": True, "blocks_sight": True})
        else:
            x0, x1, y0, y1, z0, z1 = box(c)
            add(f"column_{i:03d}", "column", x0, x1, y0, y1, z0, z1, True, True)
    for i, p in enumerate(col["props"]):
        x0, x1, y0, y1, z0, z1 = box(p)
        add(f"prop_{i:03d}", p.get("prop", "prop"), x0, x1, y0, y1, z0, z1, True, z1 >= LOW_PROP_CM,
            {"room": p.get("room")})
    # 바깥 문은 닫는다 (건물 밖은 게임 공간이 아님)
    for i, d in enumerate(dd for dd in doors if dd["exterior"]):
        add(f"closed_door_{i}", "closed_exterior_door", d["x0"], d["x1"], d["y0"], d["y1"], 0.0, 240.0, True, True)

    # ── 2) 방 찾기: 문을 닫은 채로 막힌 공간을 칠한다 ──
    cols_n = int(math.ceil((by1 - by0) / ROOM_CELL))
    rows_n = int(math.ceil((bx1 - bx0) / ROOM_CELL))   # 여기 격자는 (i: x, j: y) 단순 배치

    def cell_center(i, j):
        return bx0 + (i + 0.5) * ROOM_CELL, by0 + (j + 0.5) * ROOM_CELL

    blocked = [bytearray(cols_n) for _ in range(rows_n)]

    def block_rect(x0, x1, y0, y1):
        i0 = max(0, int(math.floor((x0 - bx0) / ROOM_CELL)))
        i1 = min(rows_n - 1, int(math.ceil((x1 - bx0) / ROOM_CELL)) - 1)
        j0 = max(0, int(math.floor((y0 - by0) / ROOM_CELL)))
        j1 = min(cols_n - 1, int(math.ceil((y1 - by0) / ROOM_CELL)) - 1)
        for i in range(i0, i1 + 1):
            row = blocked[i]
            for j in range(j0, j1 + 1):
                row[j] = 1

    for w in col["walls"]:
        x0, x1, y0, y1, _, _ = box(w)
        block_rect(x0, x1, y0, y1)          # 문 위 벽도 막음 = 문을 닫음
    for g in col["glass"]:
        x0, x1, y0, y1, _, _ = box(g)
        block_rect(x0, x1, y0, y1)
    for c in col["columns"]:
        x0, x1, y0, y1, _, _ = box(c)
        block_rect(x0, x1, y0, y1)

    comp = [[-1] * cols_n for _ in range(rows_n)]
    comps = []
    for si in range(rows_n):
        for sj in range(cols_n):
            if blocked[si][sj] or comp[si][sj] != -1:
                continue
            cid = len(comps)
            cells, border = [], False
            q = deque([(si, sj)])
            comp[si][sj] = cid
            while q:
                i, j = q.popleft()
                cells.append((i, j))
                if i in (0, rows_n - 1) or j in (0, cols_n - 1):
                    border = True
                for ni, nj in ((i + 1, j), (i - 1, j), (i, j + 1), (i, j - 1)):
                    if 0 <= ni < rows_n and 0 <= nj < cols_n and not blocked[ni][nj] and comp[ni][nj] == -1:
                        comp[ni][nj] = cid
                        q.append((ni, nj))
            comps.append({"cells": cells, "outside": border})

    def comp_at(x, y):
        i, j = int((x - bx0) // ROOM_CELL), int((y - by0) // ROOM_CELL)
        if 0 <= i < rows_n and 0 <= j < cols_n:
            return comp[i][j]
        return -1

    votes = [Counter() for _ in comps]
    for p in col["props"]:
        x0, x1, y0, y1, _, _ = box(p)
        c = comp_at((x0 + x1) / 2, (y0 + y1) / 2)
        if c >= 0 and p.get("room"):
            votes[c][p["room"]] += 1

    rooms = []
    comp_room = {}
    unnamed = 0
    for cid, c in enumerate(comps):
        area_m2 = len(c["cells"]) * ROOM_CELL * ROOM_CELL / 10000.0
        if c["outside"] or area_m2 < MIN_ROOM_M2:
            continue
        if votes[cid]:
            name = votes[cid].most_common(1)[0][0]
        else:
            unnamed += 1
            name = f"Area_{unnamed}"
        comp_room[cid] = len(rooms)
        rooms.append({"comp": cid, "name": name, "area_m2": area_m2, "labels": dict(votes[cid])})

    # 같은 이름이 둘이면 뒤에 번호
    seen = Counter(r["name"] for r in rooms)
    count = Counter()
    for r in rooms:
        if seen[r["name"]] > 1:
            count[r["name"]] += 1
            r["id"] = f'{r["name"]}_{count[r["name"]]}'
        else:
            r["id"] = r["name"]

    # 방 모양 → 직사각형들 (행마다 연속 구간, 위아래가 같은 구간이면 합침)
    for r in rooms:
        cid = r["comp"]
        by_row = {}
        for i, j in comps[cid]["cells"]:
            by_row.setdefault(i, []).append(j)
        runs_by_row = {}
        for i, js in by_row.items():
            js.sort()
            runs, start, prev = [], js[0], js[0]
            for j in js[1:]:
                if j != prev + 1:
                    runs.append((start, prev))
                    start = j
                prev = j
            runs.append((start, prev))
            runs_by_row[i] = runs
        open_rects = {}   # (j0, j1) → i0
        rects = []
        for i in range(min(runs_by_row), max(runs_by_row) + 2):
            runs = set(runs_by_row.get(i, []))
            for key in list(open_rects):
                if key not in runs:
                    rects.append((open_rects.pop(key), i - 1, key[0], key[1]))
            for key in runs:
                if key not in open_rects:
                    open_rects[key] = i
        r["rects"] = [{"xmin": bx0 + i0 * ROOM_CELL, "xmax": bx0 + (i1 + 1) * ROOM_CELL,
                       "ymin": by0 + j0 * ROOM_CELL, "ymax": by0 + (j1 + 1) * ROOM_CELL} for i0, i1, j0, j1 in rects]

    # ── 3) 걸을 수 있는 곳: 서버 BuildNavGrid와 똑같은 50cm 격자를 만들고, 첫 의적 스폰과 이어진 칸만 쓴다 ──
    #   행 r은 x ∈ [bx1 − (r+1)·cell, bx1 − r·cell], 열 k는 y ∈ [by0 + k·cell, by0 + (k+1)·cell]
    nav_rows = int(math.ceil((bx1 - bx0) / NAV_CELL))
    nav_cols = int(math.ceil((by1 - by0) / NAV_CELL))
    nav = [bytearray(nav_cols) for _ in range(nav_rows)]     # 1 = 막힘
    for c in colliders:
        if not c["blocks_move"]:
            continue
        if c.get("shape") == "circle":
            x0, x1, y0, y1 = c["cx"] - c["r"], c["cx"] + c["r"], c["cy"] - c["r"], c["cy"] + c["r"]
        else:
            x0, x1, y0, y1 = c["xmin"], c["xmax"], c["ymin"], c["ymax"]
        x0, x1, y0, y1 = x0 - NAV_PAD, x1 + NAV_PAD, y0 - NAV_PAD, y1 + NAV_PAD
        r0 = max(0, int(math.floor((bx1 - x1) / NAV_CELL)))
        r1 = min(nav_rows - 1, int(math.ceil((bx1 - x0) / NAV_CELL)) - 1)
        k0 = max(0, int(math.floor((y0 - by0) / NAV_CELL)))
        k1 = min(nav_cols - 1, int(math.ceil((y1 - by0) / NAV_CELL)) - 1)
        for r in range(r0, r1 + 1):
            for k in range(k0, k1 + 1):
                nav[r][k] = 1

    def nav_cell(x, y):
        return int((bx1 - x) // NAV_CELL), int((y - by0) // NAV_CELL)

    def nav_center(r, k):
        return bx1 - (r + 0.5) * NAV_CELL, by0 + (k + 0.5) * NAV_CELL

    sr, sk = nav_cell(THIEF_SPAWNS[0][0], THIEF_SPAWNS[0][1])
    if nav[sr][sk]:
        raise SystemExit("첫 의적 스폰이 막힌 칸에 있음")
    reach = [bytearray(nav_cols) for _ in range(nav_rows)]   # 1 = 의적이 걸어서 닿는 칸
    reach[sr][sk] = 1
    q = deque([(sr, sk)])
    while q:
        r, k = q.popleft()
        for nr, nk in ((r + 1, k), (r - 1, k), (r, k + 1), (r, k - 1)):
            if 0 <= nr < nav_rows and 0 <= nk < nav_cols and not nav[nr][nk] and not reach[nr][nk]:
                reach[nr][nk] = 1
                q.append((nr, nk))

    def walkable(x, y):          # 서버 칸 기준: 막히지 않고 의적 쪽과 이어짐
        r, k = nav_cell(x, y)
        return 0 <= r < nav_rows and 0 <= k < nav_cols and reach[r][k] == 1

    # ── 4) 안쪽 문: 어느 두 방을 잇나 ──
    inner_doors = []          # (방 a, 방 b, 문 가운데 x, y)
    for d in doors:
        if d["exterior"]:
            continue
        mx, my = (d["x0"] + d["x1"]) / 2, (d["y0"] + d["y1"]) / 2
        thin_x = (d["x1"] - d["x0"]) < (d["y1"] - d["y0"])     # 벽이 X로 얇다 = 문을 X 방향으로 지난다
        off = 80.0
        a = (mx - off, my) if thin_x else (mx, my - off)
        b = (mx + off, my) if thin_x else (mx, my + off)
        ca, cb = comp_room.get(comp_at(*a)), comp_room.get(comp_at(*b))
        if ca is None or cb is None or ca == cb:
            continue
        inner_doors.append((ca, cb, mx, my))

    # 의적 스폰(현관)에서 문으로 닿는 방만 순찰한다 (문 없는 방은 빼고)
    start_room = comp_room.get(comp_at(THIEF_SPAWNS[0][0], THIEF_SPAWNS[0][1]))
    reachable, q = {start_room}, deque([start_room])
    while q:
        cur = q.popleft()
        for ca, cb, _, _ in inner_doors:
            for here, there in ((ca, cb), (cb, ca)):
                if here == cur and there not in reachable:
                    reachable.add(there)
                    q.append(there)
    for ri, r in enumerate(rooms):
        r["reachable"] = ri in reachable

    # ── 5) 순찰 지점 ──
    nodes, edges = [], set()

    def add_node(x, y, room):
        nodes.append({"id": len(nodes), "room": room, "pos": [round(x, 1), round(y, 1), 0]})
        return len(nodes) - 1

    def r_comp(ri):
        return rooms[ri]["comp"]

    room_nodes = {}
    for ri, r in enumerate(rooms):
        if not r["reachable"]:
            continue
        cells = comps[r["comp"]]["cells"]
        cx = sum(cell_center(i, j)[0] for i, j in cells) / len(cells)
        cy = sum(cell_center(i, j)[1] for i, j in cells) / len(cells)
        # 후보: 방 안에 있는 서버 칸 가운데 중 걸을 수 있는 것
        cand = set()
        for i, j in cells:
            gr, gk = nav_cell(*cell_center(i, j))
            if 0 <= gr < nav_rows and 0 <= gk < nav_cols:
                p = nav_center(gr, gk)
                if comp_at(*p) == r_comp(ri) and walkable(*p):
                    cand.add(p)
        cand = sorted(cand)
        if not cand:
            continue
        # 중심에 가장 가까운 점 하나 + 큰 방은 PATROL_SPACING 간격으로 더
        picked = [min(cand, key=lambda p: (p[0] - cx) ** 2 + (p[1] - cy) ** 2)]
        if r["area_m2"] > 40.0:
            for p in sorted(cand, key=lambda p: (round(p[0] / PATROL_SPACING), round(p[1] / PATROL_SPACING))):
                if all(math.hypot(p[0] - q[0], p[1] - q[1]) >= PATROL_SPACING for q in picked):
                    picked.append(p)
        ids = [add_node(x, y, r["id"]) for x, y in picked]
        room_nodes[ri] = ids
        # 같은 방 안: 가장 가까운 2개와 잇는다
        for a in ids:
            pa = nodes[a]["pos"]
            near = sorted((b for b in ids if b != a),
                          key=lambda b: math.hypot(nodes[b]["pos"][0] - pa[0], nodes[b]["pos"][1] - pa[1]))[:2]
            for b in near:
                edges.add((min(a, b), max(a, b)))

    for ca, cb, mx, my in inner_doors:
        if ca not in room_nodes or cb not in room_nodes:
            continue
        r0, k0 = nav_cell(mx, my)
        near = [nav_center(r, k) for r in range(r0 - 1, r0 + 2) for k in range(k0 - 1, k0 + 2)]
        near = [p for p in near if walkable(*p)]
        if not near:
            continue          # 가구 등으로 막힌 문
        mx, my = min(near, key=lambda p: (p[0] - mx) ** 2 + (p[1] - my) ** 2)
        dn = add_node(mx, my, "door")
        for side in (ca, cb):
            nearest = min(room_nodes[side],
                          key=lambda n: math.hypot(nodes[n]["pos"][0] - mx, nodes[n]["pos"][1] - my))
            edges.add((min(dn, nearest), max(dn, nearest)))

    edge_list = []
    for a, b in sorted(edges):
        pa, pb = nodes[a]["pos"], nodes[b]["pos"]
        edge_list.append([a, b, round(math.hypot(pa[0] - pb[0], pa[1] - pb[1]), 1)])

    # ── 6) 스폰 확인 ──
    def spawn_list(points, prefix):
        out = []
        for k, (x, y, yaw) in enumerate(points):
            if not walkable(x, y):
                raise SystemExit(f"{prefix} 스폰 {k} ({x}, {y})가 걸을 수 없는 곳")
            c = comp_room.get(comp_at(x, y))
            out.append({"id": f"{prefix}_{k + 1}", "room": rooms[c]["id"] if c is not None else "?",
                        "pos": [x, y, 0], "yaw": yaw})
        return out

    thief = spawn_list(THIEF_SPAWNS, "thief_start")
    guard = spawn_list(GUARD_SPAWNS, "guard_spawn")

    # ── 7) 쓰기 ──
    out = {
        "meta": {
            "map_id": "mansion_1f",
            "display_name": "저택 1층",
            "version": MAP_VERSION,
            "units": "cm",
            "origin": "Unreal world, Mansion_Shell at (0,0,0)",
            "yaw": "degrees, 0=+X, 90=+Y",
            "bounds": {"xmin": bx0, "xmax": bx1, "ymin": by0, "ymax": by1},
            "floor_z": meta.get("floor_z", 0.0),
            "source": {"collision": os.path.basename(src), "tool": "tools/collision_to_map.py",
                       "note": "벽·유리·기둥·소품은 충돌 파일, 방·순찰은 벽 모양으로 계산, 스폰은 임시(A-26). 금고·상자는 서버가 무작위로 놓음"},
        },
        "spawns": {"thief": thief, "guard": guard},
        "rooms": [{"id": r["id"], "name": r["name"], "return_point": r["name"] not in NOT_ROOMS and r["reachable"],
                   "area_m2": round(r["area_m2"], 1), "rects": r["rects"]} for r in rooms],
        "patrol": {"nodes": nodes, "edges": edge_list},
        "colliders": {"static": colliders},
        "nav_grid": {"cell": NAV_CELL, "origin_x": bx1, "origin_y": by0,
                     "rows": int(math.ceil((bx1 - bx0) / NAV_CELL)), "cols": int(math.ceil((by1 - by0) / NAV_CELL)),
                     "cell_center": "x = origin_x - (row+0.5)*cell, y = origin_y + (col+0.5)*cell",
                     "note": "칸은 서버가 충돌체(25cm 넓힘)로 만든다"},
    }
    with open(dst, "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, indent=1)

    # ── 요약 ──
    print(f"충돌체 {len(colliders)}개 (닫은 바깥 문 {sum(1 for d in doors if d['exterior'])}개), 안쪽 문 {len(inner_doors)}개")
    print(f"방 {len(rooms)}개:")
    for r in rooms:
        flag = "" if r["name"] not in NOT_ROOMS else "  (반납·아이템 제외)"
        flag += "" if r["reachable"] else "  (문 없음: 갈 수 없음)"
        extra = f"  소품 이름 {r['labels']}" if len(r["labels"]) > 1 else ""
        print(f"  {r['id']:<14} {r['area_m2']:7.1f}m²  직사각형 {len(r['rects'])}개{flag}{extra}")
    print(f"순찰 지점 {len(nodes)}개, 연결 {len(edge_list)}개")
    # 순찰 그래프가 이어져 있는지
    adj = {n["id"]: set() for n in nodes}
    for a, b, _ in edge_list:
        adj[a].add(b)
        adj[b].add(a)
    seen_n, q = {0}, deque([0])
    while q:
        for n in adj[q.popleft()]:
            if n not in seen_n:
                seen_n.add(n)
                q.append(n)
    if len(seen_n) != len(nodes):
        lonely = sorted({nodes[n]["room"] for n in adj if n not in seen_n})
        print(f"  ! 순찰 그래프가 끊김: {len(nodes) - len(seen_n)}개 지점이 따로 있음 → {lonely}")
    print(f"스폰: 의적 {[t['room'] for t in thief]}, 경비원 {[g['room'] for g in guard]}")
    print(f"→ {dst}")


if __name__ == "__main__":
    main()
