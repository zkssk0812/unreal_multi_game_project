# -*- coding: utf-8 -*-
"""
서버 맵(mansion_1f_server_1b53272.json)에서 문 자리를 계산한다 (언리얼 없이도 돌아간다: 엔진 파이썬 -I door_plan.py → 표 출력).
setup_shell_level.py doors 가 이 결과로 AVHDoor 를 놓는다.

  - 문 자리 = 문 위 벽(kind "above_door", 39개). 폭 = 긴 변(모두 200cm), 벽 두께 = 짧은 변(40, 홀 50, 바깥 60)
  - 양쪽 구역 = 문 가운데에서 벽 두께/2 + 40cm 앞뒤 점이 들어 있는 방(서버 rooms 조각, 30cm 여유)
  - 바깥 문 2곳(closed_exterior_door 와 같은 자리)은 서버가 막는다 → 잠긴 문
  - 양문/한 짝: 공용 공간(현관·중앙 홀·라운지·갤러리·식당·사무실)에 닿은 문은 양문, 나머지(객실·창고·서재·빈 방 등)는 한 짝
  - 미닫이(벽 속으로 들어가는 포켓 도어): 문짝이 들어갈 쪽 벽이 막혀 있는지(창·다른 문이 아닌지) 서버 벽 조각으로 확인해서 방향을 고른다
"""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
SERVER_MAP = os.path.join(HERE, "mansion_1f_server_1b53272.json")
PUBLIC = {"Entrance", "CentralHall", "Lounge1", "Lounge2", "Gallery1", "Gallery2", "DiningRoom", "Office"}


def room_at(rooms, x, y, tol=30.0):
    best, best_d = None, tol + 1
    for r in rooms:
        for q in r["rects"]:
            dx = max(q["xmin"] - x, 0.0, x - q["xmax"])
            dy = max(q["ymin"] - y, 0.0, y - q["ymax"])
            d = (dx * dx + dy * dy) ** 0.5
            if d < best_d:
                best, best_d = r["id"], d
    return best if best_d <= tol else None


def wall_cover(walls, axis, line_lo, line_hi, a, b):
    """벽 줄(axis 방향으로 뻗은 벽, 두께 범위 line_lo~line_hi) 위 구간 [a, b] 가 문짝을 숨길 '막힌 벽'으로 얼마나 덮였나 (cm)"""
    segs = []
    for w in walls:
        if w["kind"] != "wall":
            continue
        if axis == "y":   # 벽이 Y 로 뻗음: 두께 = X
            if w["xmax"] <= line_lo + 1 or w["xmin"] >= line_hi - 1:
                continue
            lo, hi = w["ymin"], w["ymax"]
        else:
            if w["ymax"] <= line_lo + 1 or w["ymin"] >= line_hi - 1:
                continue
            lo, hi = w["xmin"], w["xmax"]
        lo, hi = max(lo, a), min(hi, b)
        if hi > lo:
            segs.append((lo, hi))
    segs.sort()
    total, cur = 0.0, None
    for lo, hi in segs:
        if cur is None or lo > cur[1]:
            if cur:
                total += cur[1] - cur[0]
            cur = [lo, hi]
        else:
            cur[1] = max(cur[1], hi)
    if cur:
        total += cur[1] - cur[0]
    return total


def plan(path=SERVER_MAP):
    data = json.load(open(path, encoding="utf-8"))
    st = data["colliders"]["static"]
    rooms = data["rooms"]
    closed = [c for c in st if c.get("kind") == "closed_exterior_door"]
    doors = []
    for c in [c for c in st if c.get("kind") == "above_door"]:
        sx, sy = c["xmax"] - c["xmin"], c["ymax"] - c["ymin"]
        cx, cy = (c["xmin"] + c["xmax"]) / 2, (c["ymin"] + c["ymax"]) / 2
        if sx < sy:      # 벽이 Y 로 뻗음 → 문 폭 = Y, 법선 = X. 액터 yaw 0: 로컬 Y = 월드 +Y, 로컬 X = 월드 +X
            width, thick, yaw, axis = sy, sx, 0.0, "y"
            n = (1.0, 0.0)
            line = (c["xmin"], c["xmax"])
            along = cy
        else:            # 벽이 X 로 뻗음 → yaw 90: 로컬 X = 월드 +Y, 로컬 Y = 월드 -X
            width, thick, yaw, axis = sx, sy, 90.0, "x"
            n = (0.0, 1.0)
            line = (c["ymin"], c["ymax"])
            along = cx
        off = thick / 2 + 40.0
        za = room_at(rooms, cx - n[0] * off, cy - n[1] * off)   # 로컬 -X 쪽
        zb = room_at(rooms, cx + n[0] * off, cy + n[1] * off)   # 로컬 +X 쪽
        locked = any(abs((k["xmin"] + k["xmax"]) / 2 - cx) < 5 and abs((k["ymin"] + k["ymax"]) / 2 - cy) < 5 for k in closed)
        double = (za in PUBLIC) or (zb in PUBLIC)
        # 문짝이 들어갈 벽: 한 짝 = 한쪽에 폭만큼, 양문 = 양쪽에 반씩
        need = width if not double else width / 2
        lo_cover = wall_cover(st, axis, line[0], line[1], along - width / 2 - need, along - width / 2)
        hi_cover = wall_cover(st, axis, line[0], line[1], along + width / 2, along + width / 2 + need)
        # 월드 축 방향 → 로컬 Y 부호 (yaw 90 이면 로컬 +Y = 월드 -X)
        world_sign = 1.0 if hi_cover >= lo_cover else -1.0
        slide = world_sign if axis == "y" else -world_sign
        doors.append({
            "id": c["id"], "x": cx, "y": cy, "yaw": yaw, "width": width, "thick": thick,
            "zone_a": za or "", "zone_b": zb or "", "double": double, "slide": slide, "locked": locked,
            "type": c.get("type", ""), "cover": (lo_cover, hi_cover), "need": need,
        })
    return doors


if __name__ == "__main__":
    import io
    import sys
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8")
    names = json.load(open(os.path.join(HERE, "room_names.json"), encoding="utf-8"))
    ds = plan()
    for d in ds:
        print("%-9s (%6.0f,%6.0f) yaw%3.0f 폭%3.0f 두께%2.0f  %-6s ↔ %-6s %s %s 덮임 %s/%s%s" % (
            d["id"], d["x"], d["y"], d["yaw"], d["width"], d["thick"], names.get(d["zone_a"], d["zone_a"] or "바깥"),
            names.get(d["zone_b"], d["zone_b"] or "바깥"), "양문" if d["double"] else "한짝", "+" if d["slide"] > 0 else "-",
            int(d["cover"][0]), int(d["cover"][1]), " 잠김" if d["locked"] else ""))
    print("문 %d개: 양문 %d, 한 짝 %d, 잠김 %d, 벽이 모자란 문 %d" % (
        len(ds), sum(d["double"] for d in ds), sum(not d["double"] for d in ds), sum(d["locked"] for d in ds),
        sum(1 for d in ds if max(d["cover"]) < d["need"] - 1 or (d["double"] and min(d["cover"]) < d["need"] - 1))))
    used = set()
    for d in ds:
        used.update([d["zone_a"], d["zone_b"]])
    print("문이 없는 방:", [names.get(r["id"], r["id"]) for r in json.load(open(SERVER_MAP, encoding="utf-8"))["rooms"] if r["id"] not in used])
