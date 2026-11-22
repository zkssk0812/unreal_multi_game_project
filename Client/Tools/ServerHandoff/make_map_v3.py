# -*- coding: utf-8 -*-
"""
맵 배치 JSON v2 → v3 (2026-11-13)

바꾸는 것
  1. meta.version 2 → 3
  2. box_slots 추가 (Tools/MapOverlay/box_slots_proposal.csv = 레벨에 놓은 Box_01~20)
  3. 경비원 스폰 guard_spawn_2: 문 없는 방 20(L2) → 방 21(L1) 중앙
  4. 문 없는 방(20·13·11) 안의 순찰 지점 35·39·41 삭제. 서버는 "id == 배열 위치"여야 하므로
     남은 지점의 id를 0부터 다시 매기고 연결선(edges)의 번호도 같이 바꾼다.
  5. 레벨에 없는 고정 충돌체 삭제: 옷장 4, 감옥 벽 3, 탈출구 기둥 2

사용: python -I Tools/ServerHandoff/make_map_v3.py <json> [<json> ...]
      (이미 v3인 파일은 건드리지 않는다)
"""
import csv, json, os, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SLOTS_CSV = os.path.join(ROOT, "Tools", "MapOverlay", "box_slots_proposal.csv")

REMOVE_PATROL = {35, 39, 41}
REMOVE_COLLIDERS = {
    "wardrobe_01", "wardrobe_02", "wardrobe_03", "wardrobe_04",
    "jail_wall_back", "jail_wall_left", "jail_wall_right",
    "exit_pillar_left", "exit_pillar_right",
}
GUARD2 = {"room": "L1", "pos": [2343.5, -4926, 0]}


def dump(data):
    # 기존 파일과 같은 모양: 들여쓰기 1칸, 한글 그대로, CRLF, 끝 줄바꿈 없음
    return json.dumps(data, ensure_ascii=False, indent=1).replace("\n", "\r\n")


def load_slots():
    with open(SLOTS_CSV, encoding="utf-8-sig", newline="") as f:
        return [{
            "id": r["id"], "client": r["client"], "room": r["room"],
            "pos": [float(r["x"]), float(r["y"]), 0.0], "yaw": float(r["yaw"]),
        } for r in csv.DictReader(f)]


def convert(path):
    with open(path, encoding="utf-8", newline="") as f:
        text = f.read()
    d = json.loads(text)
    name = os.path.basename(path)
    if dump(d) != text:
        raise SystemExit(f"{name}: 다시 쓰면 모양이 달라짐 → 중단 (파일을 건드리지 않음)")
    if d["meta"]["version"] != 2:
        print(f"{name}: version {d['meta']['version']} → 건너뜀")
        return

    report = []
    d["meta"]["version"] = 3
    d["meta"]["v3_note"] = ("2026-11-13: box_slots 20곳 추가, guard_spawn_2를 방 21로 이동, "
                           "문 없는 방의 순찰 지점 3개 삭제(id 다시 매김), 레벨에 없는 충돌체 9개 삭제")

    # 2. box_slots: vault_slots 바로 뒤에 넣는다
    slots = load_slots()
    out = {}
    for k, v in d.items():
        out[k] = v
        if k == "vault_slots":
            out["box_slots"] = slots
    d = out
    report.append(f"box_slots {len(slots)}곳")

    # 3. 경비원 스폰 2
    g = next(s for s in d["spawns"]["guard"] if s["id"] == "guard_spawn_2")
    report.append(f"guard_spawn_2 {g['room']} {g['pos']} → {GUARD2['room']} {GUARD2['pos']}")
    g["room"], g["pos"] = GUARD2["room"], GUARD2["pos"]

    # 4. 순찰 지점
    nodes = d["patrol"]["nodes"]
    removed = [n for n in nodes if n["id"] in REMOVE_PATROL]
    kept = [n for n in nodes if n["id"] not in REMOVE_PATROL]
    if len(removed) != len(REMOVE_PATROL):
        raise SystemExit(f"{name}: 지울 순찰 지점을 다 찾지 못함")
    remap = {n["id"]: i for i, n in enumerate(kept)}
    for n in kept:
        n["id"] = remap[n["id"]]
    edges = d["patrol"]["edges"]
    kept_edges = [e for e in edges if e[0] in remap and e[1] in remap]
    for e in kept_edges:
        e[0], e[1] = remap[e[0]], remap[e[1]]
    d["patrol"]["nodes"], d["patrol"]["edges"] = kept, kept_edges
    report.append("순찰 지점 %d → %d (삭제: %s), 연결선 %d → %d" % (
        len(nodes), len(kept), ", ".join(f"{n['id']}({n['room']})" for n in removed), len(edges), len(kept_edges)))
    moved = {old: new for old, new in remap.items() if old != new}
    report.append("id가 바뀐 순찰 지점: " + ", ".join(f"{o}→{n}" for o, n in moved.items()))

    # 5. 고정 충돌체
    static = d["colliders"]["static"]
    kept_static = [c for c in static if c["id"] not in REMOVE_COLLIDERS]
    if len(static) - len(kept_static) != len(REMOVE_COLLIDERS):
        raise SystemExit(f"{name}: 지울 충돌체를 다 찾지 못함")
    d["colliders"]["static"] = kept_static
    report.append(f"고정 충돌체 {len(static)} → {len(kept_static)}")

    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(dump(d))
    print(f"{name}: v3 저장")
    for line in report:
        print("  -", line)


if __name__ == "__main__":
    for p in sys.argv[1:]:
        convert(p)
