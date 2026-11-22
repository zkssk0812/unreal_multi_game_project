# -*- coding: utf-8 -*-
"""
클라 레벨(L_Mansion_1F) 배치 → 서버용 배치 JSON (Docs/ServerHandoff/mansion_1f_layout.json)

입력
  - Tools/ServerHandoff/l_mansion_1f_export.json : MCP로 레벨에서 뽑은 액터 위치·방향·경계 (언리얼 월드 cm)
  - Tools/MapOverlay/rooms.csv                    : 방 번호·이름·문 없음 표시
  - 서버 맵 파일(참고 전용, 읽기만)                : 벽·바닥(FBX에서 나온 값), 경비원 시작·순찰 (클라에 없음)
출력
  - 서버 형식(mansion_1f_server.json과 같은 이름)의 배치 JSON

좌표: 2026-10-27부터 L_Mansion_1F의 맵은 원점·회전 0 → 언리얼 월드 좌표 = 서버 좌표 (변환 없음).
오브젝트 모양(크기)은 클라 C++ 코드의 값(VHSafe.cpp, VHHideSpot.cpp, VHJailDoor.cpp, VHExitGate.cpp)을 그대로 옮겼다.

사용: python -I Tools/ServerHandoff/make_server_layout.py [--server-map <mansion_1f_server.json>] [--out <json>]
"""
import argparse, csv, json, math, os

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
EXPORT = os.path.join(ROOT, "Tools", "ServerHandoff", "l_mansion_1f_export.json")
ROOMS_CSV = os.path.join(ROOT, "Tools", "MapOverlay", "rooms.csv")
SERVER_MAP = os.path.join(ROOT, "Docs", "ServerRef", "Project_Rob-master", "Data", "Maps", "mansion_1f_server.json")
OUT = os.path.join(ROOT, "Docs", "ServerHandoff", "mansion_1f_layout.json")

# ── 클라 C++ 코드의 모양 값 (cm, 액터 기준 로컬: +X = 앞면) ──
SAFE_BODY = (90.0, 90.0, 110.0)          # VHSafe.cpp Body 0.9×0.9×1.1 (기본 큐브 100cm)
HIDE_BODY = (70.0, 120.0, 210.0)         # VHHideSpot.cpp Body 0.7×1.2×2.1
STAND_GAP = 70.0                         # VHGoto의 PlaceInFront: 대상 앞면에서 70cm 앞에 선다
JAIL_DEPTH, JAIL_WIDTH, JAIL_HEIGHT = 300.0, 300.0, 250.0   # VHJailDoor.cpp
JAIL_CELL_GAP = 80.0                     # GetCellLocation: 감금 자리 좌우 간격
JAIL_RELEASE = 120.0                     # GetReleaseLocation: 문 앞 120cm (구출하는 사람이 서는 곳)
GATE_WIDTH, GATE_HEIGHT = 300.0, 280.0   # VHExitGate.cpp
GATE_THICK = 15.0                        # Body 0.15
PILLAR = 40.0                            # 기둥 0.4×0.4, 높이 GateHeight+40
ESCAPE_CENTER, ESCAPE_HALF_DEPTH = -160.0, 110.0   # EscapeTrigger: 로컬 X -160, 반 크기 110 (문 너머 = 앞면 반대쪽)
ESCAPE_HALF_WIDTH = GATE_WIDTH * 0.5 - 10.0        # 140
EXIT_INSIDE = 120.0                      # 서버 검사용 "탈출구 안쪽에 서는 곳" (클라는 쓰지 않음)


def r1(v):
    return round(float(v), 1) + 0.0


def fwd(yaw):
    a = math.radians(yaw)
    return math.cos(a), math.sin(a)


def local_to_world(px, py, yaw, lx, ly):
    """로컬(앞 +X, 오른쪽 +Y) → 월드 XY (언리얼은 왼손 좌표계: 오른쪽 = yaw+90)"""
    fx, fy = fwd(yaw)
    rx, ry = fwd(yaw + 90.0)
    return px + fx * lx + rx * ly, py + fy * lx + ry * ly


def box_world(px, py, yaw, cx, cy, sx, sy):
    """로컬 중심(cx,cy)·크기(sx,sy) 상자의 월드 축 정렬 경계 (yaw는 90° 단위)"""
    pts = [local_to_world(px, py, yaw, cx + dx * sx / 2, cy + dy * sy / 2) for dx in (-1, 1) for dy in (-1, 1)]
    xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
    return min(xs), max(xs), min(ys), max(ys)


def collider(cid, kind, b, zmin, zmax, move=True, sight=True, ref=None, **extra):
    c = {"id": cid, "kind": kind, "xmin": r1(b[0]), "xmax": r1(b[1]), "ymin": r1(b[2]), "ymax": r1(b[3]),
         "zmin": r1(zmin), "zmax": r1(zmax), "blocks_move": move, "blocks_sight": sight}
    if ref:
        c["ref"] = ref
    c.update(extra)
    return c


def norm_yaw(y):
    y = y % 360.0
    return r1(0.0 if abs(y) < 1e-6 or abs(y - 360.0) < 1e-6 else y)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--export", default=EXPORT)
    ap.add_argument("--server-map", default=SERVER_MAP)
    ap.add_argument("--out", default=OUT)
    a = ap.parse_args()

    exp = json.load(open(a.export, encoding="utf-8"))
    srv = json.load(open(a.server_map, encoding="utf-8"))
    actors = {e["label"]: e for e in exp["actors"]}

    def P(label):
        e = actors[label]
        L = e["xf"]["location"]
        return L["x"], L["y"], e["xf"]["rotation"]["yaw"]

    # ── 방 33 ──
    names = {}
    for row in csv.DictReader(open(ROOMS_CSV, encoding="utf-8-sig")):
        n = int(row["n"])
        names[n] = row["name"] + (" (문 없음)" if row["closed"] == "1" else "")
    rooms = []
    for n in range(1, 34):
        e = actors["Room_%02d" % n]
        b = e["bounds"]
        rooms.append({"id": "Room_%02d" % n, "no": n, "name": names[n],
                      "xmin": r1(b["min"]["x"]), "xmax": r1(b["max"]["x"]), "ymin": r1(b["min"]["y"]), "ymax": r1(b["max"]["y"]),
                      "return_point": bool(e["props"]["bAllowReturnPoint"])})

    def room_of(x, y):
        for rm in rooms:
            if rm["xmin"] <= x <= rm["xmax"] and rm["ymin"] <= y <= rm["ymax"]:
                return rm["id"]
        return "corridor"

    static, dynamic = [], []

    # ── 시작 위치 ──
    # 레벨의 PlayerStart 3개 (2026-10-27 사용자 결정: 레벨에 2개 추가, 큰 홀 가운데에서 화면 좌우 ±200cm)
    thief = []
    for i, label in enumerate(("PlayerStart", "PlayerStart_2", "PlayerStart_3")):
        sx, sy, syaw = P(label)
        thief.append({"id": "thief_start_%d" % (i + 1), "pos": [r1(sx), r1(sy), 0.0], "yaw": norm_yaw(syaw), "client": label})

    # ── 금고 후보 10 ──
    vaults = []
    for n in range(1, 11):
        label = "Safe_%02d" % n
        x, y, yaw = P(label)
        fx, fy = fwd(yaw)
        reach = SAFE_BODY[0] / 2 + STAND_GAP
        vaults.append({"id": "vault_slot_%02d" % n, "client": label, "room": room_of(x, y), "pos": [r1(x), r1(y), 0.0],
                       "yaw": norm_yaw(yaw), "interact_pos": [r1(x + fx * reach), r1(y + fy * reach), 0.0],
                       "size": list(SAFE_BODY)})
        dynamic.append(collider("vault_%d" % (100 + n), "vault", box_world(x, y, yaw, 0, 0, SAFE_BODY[0], SAFE_BODY[1]),
                                0, SAFE_BODY[2], ref="vault_slot_%02d" % n))

    # ── 숨는 곳 ──
    hides = []
    hide_labels = sorted(l for l in actors if l.startswith("HideSpot_"))
    for i, label in enumerate(hide_labels):
        x, y, yaw = P(label)
        fx, fy = fwd(yaw)
        reach = HIDE_BODY[0] / 2 + STAND_GAP
        hid = "hiding_spot_%02d" % (i + 1)
        hides.append({"id": hid, "client": label, "room": room_of(x, y), "pos": [r1(x), r1(y), 0.0], "yaw": norm_yaw(yaw),
                      "interact_pos": [r1(x + fx * reach), r1(y + fy * reach), 0.0], "size": list(HIDE_BODY)})
        static.append(collider("wardrobe_%02d" % (i + 1), "wardrobe", box_world(x, y, yaw, 0, 0, HIDE_BODY[0], HIDE_BODY[1]),
                               0, HIDE_BODY[2], ref=hid))

    # ── 탈출구 + 탈출 구역 ──
    gx, gy, gyaw = P("ExitGate")
    fx, fy = fwd(gyaw)
    ez = box_world(gx, gy, gyaw, ESCAPE_CENTER, 0, ESCAPE_HALF_DEPTH * 2, ESCAPE_HALF_WIDTH * 2)
    exit_ = {"id": "exit_gate", "client": "ExitGate", "pos": [r1(gx), r1(gy), 0.0], "yaw": norm_yaw(gyaw), "width": GATE_WIDTH,
             "interact_pos": [r1(gx + fx * EXIT_INSIDE), r1(gy + fy * EXIT_INSIDE), 0.0],
             "escape_zone": {"xmin": r1(ez[0]), "xmax": r1(ez[1]), "ymin": r1(ez[2]), "ymax": r1(ez[3])},
             "note": "서쪽 옆문 = 서버 문서 ②(x≈9778, y −4635~−4355). 앞면(yaw)은 건물 안쪽. 탈출 구역은 문 너머(건물 밖, Floor_Exit 위). "
                     "interact_pos는 클라에서 쓰지 않음(서버 ValidateMap용 안쪽 지점)"}
    dynamic.append(collider("exit_gate_1", "exit_gate", box_world(gx, gy, gyaw, 0, 0, GATE_THICK, GATE_WIDTH), 0, GATE_HEIGHT,
                            ref="exit_gate"))
    for side, ly in (("left", -GATE_WIDTH / 2), ("right", GATE_WIDTH / 2)):
        static.append(collider("exit_pillar_" + side, "exit_pillar", box_world(gx, gy, gyaw, 0, ly, PILLAR, PILLAR), 0,
                               GATE_HEIGHT + 40, ref="exit_gate"))
    fe = actors["Floor_Exit"]["bounds"]
    floor_exit = {"id": "floor_exit", "xmin": r1(fe["min"]["x"]), "xmax": r1(fe["max"]["x"]), "ymin": r1(fe["min"]["y"]),
                  "ymax": r1(fe["max"]["y"]), "client": "Floor_Exit",
                  "note": "FBX에 없는 바닥(클라가 레벨에 추가). 탈출 구역이 이 위에 있다"}

    # ── 창고 ──
    jx, jy, jyaw = P("Jail")
    jfx, jfy = fwd(jyaw)
    cell_c = local_to_world(jx, jy, jyaw, -JAIL_DEPTH / 2, 0)
    cells = []
    for i in range(3):
        off = (i - 1) * JAIL_CELL_GAP
        cx, cy = local_to_world(jx, jy, jyaw, -JAIL_DEPTH / 2, off)
        cells.append([r1(cx), r1(cy), 0.0])
    jz = box_world(jx, jy, jyaw, -JAIL_DEPTH / 2, 0, JAIL_DEPTH - 20, JAIL_WIDTH - 20)   # 벽 두께 20 안쪽
    jail = {"room": room_of(*cell_c), "client": "Jail",
            "door": {"pos": [r1(jx), r1(jy), 0.0], "yaw": norm_yaw(jyaw), "width": JAIL_WIDTH,
                     "interact_pos": [r1(jx + jfx * JAIL_RELEASE), r1(jy + jfy * JAIL_RELEASE), 0.0]},
            "cells": cells,
            "zone": {"xmin": r1(jz[0]), "xmax": r1(jz[1]), "ymin": r1(jz[2]), "ymax": r1(jz[3])},
            "note": "창고 = 방 31 안에 놓은 3m×3m 감옥 칸(문 + 벽 3면). 앞면(yaw) = 문 바깥쪽. 구출은 문 앞 interact_pos에서 길게 5초"}
    dynamic.append(collider("jail_door_1", "jail_door", box_world(jx, jy, jyaw, 0, 0, 10, JAIL_WIDTH), 0, JAIL_HEIGHT, ref="jail"))
    for wid, (lx, ly, wx, wy) in {"back": (-JAIL_DEPTH, 0, 20, JAIL_WIDTH),
                                  "left": (-JAIL_DEPTH / 2, -JAIL_WIDTH / 2, JAIL_DEPTH, 20),
                                  "right": (-JAIL_DEPTH / 2, JAIL_WIDTH / 2, JAIL_DEPTH, 20)}.items():
        static.append(collider("jail_wall_" + wid, "jail_wall", box_world(jx, jy, jyaw, lx, ly, wx, wy), 0, JAIL_HEIGHT, ref="jail"))

    # ── 보이지 않는 벽 (정문 막음) ──
    ib = actors["InvisibleWall_FrontDoor"]["bounds"]
    blocker = collider("blocker_front_door", "blocker",
                       (ib["min"]["x"], ib["max"]["x"], ib["min"]["y"], ib["max"]["y"]), ib["min"]["z"], ib["max"]["z"],
                       move=True, sight=False, ref="front_door",
                       client="InvisibleWall_FrontDoor", note="현관 정문(②가 아닌 ①)을 막음. 캐릭터만 막고 시야·손전등은 통과")
    static.append(blocker)

    # ── 서버 값을 그대로 쓰는 것: 벽(FBX), 바닥(FBX), 경비원 시작, 순찰 ──
    walls = [c for c in srv["colliders"]["static"] if c["kind"] == "wall"]
    floors = [dict(f) for f in srv.get("floors", [])] + [floor_exit]

    nav = dict(srv["nav_grid"])
    # 탈출 구역(문 너머, x 최대 ~10180)까지 격자가 덮도록 원점을 400cm(8칸) 늘린다. 칸 경계는 그대로(50cm 간격 유지)
    nav["origin_x"] = nav["origin_x"] + 400.0
    xmin = min(c["xmin"] for c in walls + floors)
    ymax = max(c["ymax"] for c in walls + floors)
    nav["rows"] = int(math.ceil((nav["origin_x"] - xmin) / nav["cell"]))
    nav["cols"] = int(math.ceil((ymax - nav["origin_y"]) / nav["cell"]))
    nav["note"] = "칸 데이터는 서버가 충돌체(25cm 넓힘)와 바닥(floors)으로 직접 만든다. origin_x는 탈출 구역을 덮게 기존 9790 → 10190"

    meta = dict(srv["meta"])
    meta.update({"version": 2, "display_name": "저택 1층",
                 "origin": "Unreal world, blockout at (0,0,0) rot 0 scale 1 — 클라 L_Mansion_1F와 같은 좌표(2026-10-27부터)",
                 "bounds": {"xmin": math.floor(xmin), "xmax": math.ceil(max(c["xmax"] for c in walls + floors)),
                            "ymin": math.floor(min(c["ymin"] for c in walls + floors)), "ymax": math.ceil(ymax)},
                 "source": {"client_level": "/Game/VaultHeist/Maps/Mansion/L_Mansion_1F", "fbx": "Mansion_1F_Blockout.fbx",
                            "walls": len(walls), "floors": len(floors),
                            "generated_by": "MCPTest/Tools/ServerHandoff/make_server_layout.py",
                            "note": "게임 지점·소품은 클라 레벨에서, 벽·바닥은 FBX(서버 맵과 0.1cm 안 일치 확인), 경비원 시작·순찰은 서버 기존 값"}})

    out = {
        "meta": meta,
        "rules": {"note": "참고용 = 클라 설정값(DA_VHGameplayConfig·DA_VHInteractionConfig). 서버는 GameData.h를 쓴다",
                  "vault_total": 10, "vault_active": 5, "vault_open_sec": 30, "vault_max_interactors": 1, "treasure_per_vault": 1,
                  "escape_value_needed": 10000, "exit_auto_open": True, "max_carry_count": 10, "max_carry_weight_kg": 20,
                  "return_point_count": 5, "return_point_size_cm": 100, "return_point_red_sec": 50, "return_point_life_sec": 60,
                  "hide_enter_sec": 1.0, "hide_exit_sec": 0.5, "rescue_sec": 5, "max_thieves": 3},
        "rooms": rooms,
        "spawns": {"thief": thief, "guard": srv["spawns"]["guard"]},
        "vault_slots": vaults,
        "hiding_spots": hides,
        "exit": exit_,
        "jail": jail,
        "blockers": [blocker],
        "patrol": srv["patrol"],
        "colliders": {"static": walls + static, "dynamic": dynamic},
        "floors": floors,
        "nav_grid": nav,
    }
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    json.dump(out, open(a.out, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    print("rooms %d (반납 제외 %d) / 시작 %d / 금고 %d / 숨는 곳 %d / 충돌체 static %d (벽 %d) dynamic %d / 순찰 %d·%d → %s" % (
        len(rooms), sum(not r["return_point"] for r in rooms), len(thief), len(vaults), len(hides),
        len(walls) + len(static), len(walls), len(dynamic), len(srv["patrol"]["nodes"]), len(srv["patrol"]["edges"]), a.out))


if __name__ == "__main__":
    main()
