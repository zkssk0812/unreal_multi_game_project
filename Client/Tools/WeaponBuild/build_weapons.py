# -*- coding: utf-8 -*-
"""무기 3D 모델 만들기 (구상안 v2.2 → 스태틱 메시). 언리얼 에디터 안에서 실행한다.

  에디터 아래쪽 Cmd 칸:  py "C:/UnrealProjects/MCPTest/Tools/WeaponBuild/build_weapons.py" pistol
  (이름을 여러 개 주거나 all 을 주면 모두 만든다. 먼저 setup_weapon_assets.py materials 로 재질을 만들어 둔다)

모양은 구상안 HTML(Docs/Design/Weapons_v2.2/weapon_concepts_v22.html)의 옆모습 좌표를 그대로 옮긴 것이다.
크기는 실제 cm, +X = 총구·칼끝, +Z = 위, 원점 = 손잡이(설치형은 바닥 중앙).
모델러가 만든 모델이 오면 이 스크립트는 필요 없다: DA_VHWeaponCatalog 의 Mesh 칸만 바꾸면 된다.
"""
import importlib
import json
import math
import os
import sys

import unreal

# 플레이 중에는 에셋을 만들거나 저장할 수 없다(에디터가 "덮어쓸까요?" 창을 띄우고 멈춘다) → 먼저 플레이를 멈춘다
if unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor():
    raise RuntimeError("[WeaponBuild] 플레이(PIE) 중에는 실행할 수 없습니다. 플레이를 멈추고 다시 실행하세요.")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vh_meshlib as ml
importlib.reload(ml)

MESH_DIR = "/Game/VaultHeist/Weapons/Meshes"
MAT_DIR = "/Game/VaultHeist/Weapons/Materials"
GLOW_OF = {"melee": "MI_VHW_Glow_Melee", "ranged": "MI_VHW_Glow_Ranged", "thrown": "MI_VHW_Glow_Thrown", "placed": "MI_VHW_Glow_Placed"}


def materials(category):
    names = ["MI_VHW_Black", "MI_VHW_Steel", "MI_VHW_Carbon", "MI_VHW_Leather", "MI_VHW_Brass", GLOW_OF[category]]
    out = []
    for n in names:
        m = unreal.EditorAssetLibrary.load_asset("%s/%s" % (MAT_DIR, n))
        if m is None:
            raise RuntimeError("재질이 없습니다: %s (setup_weapon_assets.py materials 를 먼저 실행)" % n)
        out.append(m)
    return out


class Sheet:
    """구상안 그림 좌표(SVG, y 아래쪽) → 모델 좌표(cm). (gx, gy) = 손잡이(원점), scale = cm / 그림 단위"""

    def __init__(self, gx, gy, scale):
        self.gx, self.gy, self.s = gx, gy, scale

    def pt(self, x, y):
        return ((x - self.gx) * self.s, (self.gy - y) * self.s)

    def x(self, x):
        return (x - self.gx) * self.s

    def z(self, y):
        return (self.gy - y) * self.s

    def path(self, d, index=0):
        return [self.pt(px, py) for (px, py) in ml.svg_path(d)[index]]

    def poly(self, pts):
        return [self.pt(px, py) for (px, py) in pts]

    def atlas_uv(self, number):
        """모델 좌표 → 옆모습 모음 그림(T_VHW_SideAtlas, 2열×5행, 칸 하나 = 그림 600×240 단위)의 UV.
        옆에서 본 그림을 Y 방향으로 그대로 비추는 것과 같다 → 모델 옆면에 구상안 그림이 그대로 입혀진다."""
        col, row = (number - 1) % 2, (number - 1) // 2
        return lambda x, y, z: ((col + (x / self.s + self.gx) / 600.0) / 2.0, (row + (self.gy - z / self.s) / 240.0) / 5.0)


# ───────────────────────── 04 권총 (SLAB PISTOL) ─────────────────────────
def build_pistol():
    sh = Sheet(176.0, 150.0, 21.0 / 334.0)     # 슬라이드 길이 21cm, 원점 = 그립 가운데
    b = ml.Builder(sh.atlas_uv(4))             # 옆면에 구상안 04번 옆모습 그림을 입힌다
    T = 1.2                                     # 프레임 반 두께

    frame = ('M148 66L134.4 66L134.8 76C135.2 86 143 95 147 107C148.5 120 140 140 136 160C133 175 129.5 196 128.3 212C127.6 218 130.5 222.6 135.5 223'
             'L200 225C205 225.2 209 222.2 209.8 217.5C212 198 217 168 223 144C224.5 138.5 231 137 236 141C246 149 260 153.5 278 153.5L300 153.5'
             'C318 153.5 331 143 332 127C332.5 112 337 97.5 349 95.5L444 90L459.5 78C461.5 76.4 462.3 74.8 462.2 73L462 66Z')
    guard_hole = 'M250 99L318 96.4C322.6 96.2 324.6 100 324.4 106C324 126 314 144.5 298 144.5L280 144.5C262 144.5 247 136 243.6 120C242 110 244 99.3 250 99Z'
    plate = 'M131.5 222.6L204 224.6L211.5 226.8C213 227.3 213.6 229 212.8 230.3C212.2 231.4 211 232 209.6 232L132.5 230.6C129.1 230.5 126.9 228.5 127.2 226C127.5 223.8 129.1 222.6 131.5 222.6Z'
    inlay = 'M160.5 121.5L213 130.5C215.5 131 216.8 133 216.4 135.5C213.5 160 207.5 186 202 209C201.3 211.5 199.3 213 196.8 212.9L142.5 211.2C139.9 211.1 138.4 209 138.8 206.5C140.5 192 143.5 176 147 160C150 148 157 136 157.6 125C157.7 123 158.8 121.6 160.5 121.5Z'
    trigger = 'M273 94L281.5 94L284 124C284.3 128 281.5 130.5 278 129C275 127.5 273.6 124 273.4 120Z'
    carbon = 'M358 76.5L450 74.5L455 79L446 86.2L356 90.6Z'
    port = 'M300 35.3L346 35.9L346 49C346 51 344.6 52.4 342.6 52.4L303.4 52.4C301.4 52.4 300 51 300 49Z'
    rear_sight = 'M141 34L141 30.4C141 29.3 141.9 28.5 143 28.5L151.5 28.5L157 33.8Z'
    front_sight = 'M444 37L446.6 32.8C447 32.1 447.8 31.7 448.7 31.7L451.6 31.7C452.5 31.7 453.2 32.1 453.7 32.9L456.4 37.7Z'

    # 프레임 + 그립 (무광 블랙): 방아쇠울 구멍을 뚫는다
    body = b.profile_part(sh.path(frame), -T, T, 0.24, ml.BLACK)
    b.subtract(body, b.profile_part(sh.path(guard_hole), -3.0, 3.0, 0.0, ml.BLACK))
    b.add(body)

    # 슬라이드 (무광 블랙): 단면을 총 길이 방향으로 밀어낸다. 어깨를 깎은 면 = 위에서 보이는 캐릭터 라인 두 줄
    w, h = 1.38, sh.s * 35.0
    section = [(-w + 0.10, -h / 2), (w - 0.10, -h / 2), (w - 0.10, -h * 0.16), (w, -h * 0.08), (w, h * 0.22),
               (w * 0.72, h / 2 - 0.05), (w * 0.52, h / 2), (-w * 0.52, h / 2), (-w * 0.72, h / 2 - 0.05), (-w, h * 0.22),
               (-w, -h * 0.08), (-w + 0.10, -h * 0.16)]
    zc = sh.z(53.4)
    x0, x1 = sh.x(132.6), sh.x(468.4)
    b.section(section, [(x0, 0.86, zc), (x0 + 0.30, 1.0, zc), (x1 - 0.45, 1.0, zc), (x1, 0.88, zc)], ml.BLACK)

    # 탄피 배출구 (브러시드 스틸): 슬라이드 위·옆으로 조금 솟아 위에서도 보인다
    b.profile(sh.path(port), -(w + 0.03), w + 0.03, 0.06, ml.STEEL)
    # 총구 (스틸 고리)
    b.section(ml.circle(0.0, 0.0, 0.46, 16), [(x1 - 0.2, 1.0, sh.z(50.4)), (x1 + 0.14, 1.0, sh.z(50.4)), (x1 + 0.18, 0.8, sh.z(50.4))], ml.STEEL)
    b.section(ml.circle(0.0, 0.0, 0.26, 12), [(x1 + 0.10, 1.0, sh.z(50.4)), (x1 + 0.20, 1.0, sh.z(50.4))], ml.BLACK)
    # 가늠자·가늠쇠
    b.profile(sh.path(rear_sight), -0.55, 0.55, 0.05, ml.BLACK)
    b.profile(sh.path(front_sight), -0.22, 0.22, 0.04, ml.BLACK)

    # 12칸 발광 채널 (탄창 12발): 슬라이드 양옆으로 조금 솟은 발광 띠 → 옆에서도 위에서도 점선으로 보인다
    gx0, gy0, gx1, gy1 = 214.0, 65.3, 448.0, 63.5
    dash, gap, half = 15.0, 4.9, 2.3
    for i in range(12):
        a = i * (dash + gap)
        xa, xb = gx0 + a, gx0 + a + dash
        ya = gy0 + (gy1 - gy0) * (xa - gx0) / (gx1 - gx0)
        yb = gy0 + (gy1 - gy0) * (xb - gx0) / (gx1 - gx0)
        b.profile(sh.poly([(xa, ya - half), (xb, yb - half), (xb, yb + half), (xa, ya + half)]), -(w + 0.07), w + 0.07, 0.03, ml.GLOW)

    # 황동 바닥판, 가죽 그립 패널(황동 테두리 + 금고 다이얼 메달), 황동 방아쇠
    b.profile(sh.path(plate), -(T + 0.22), T + 0.22, 0.09, ml.BRASS)
    inlay_pts = sh.path(inlay)
    b.profile(ml.inset(ml.ccw(inlay_pts), -0.13), -(T + 0.05), T + 0.05, 0.03, ml.BRASS)
    b.profile(inlay_pts, -(T + 0.13), T + 0.13, 0.07, ml.LEATHER)
    b.profile(ml.circle(sh.x(178), sh.z(170), 10.5 * sh.s, 20), -(T + 0.22), T + 0.22, 0.05, ml.BRASS)
    b.profile(sh.path(trigger), -0.28, 0.28, 0.06, ml.BRASS)
    # 프레임 앞쪽 카본 패널, 스틸 레일, 나사·핀
    b.profile(sh.path(carbon), -(T + 0.04), T + 0.04, 0.02, ml.CARBON)
    b.profile(sh.poly(ml.rect(290, 82.4, 316, 88.0)), -(T + 0.05), T + 0.05, 0.02, ml.STEEL)
    b.profile(ml.circle(sh.x(340), sh.z(86.4), 2.5 * sh.s, 10), -(T + 0.06), T + 0.06, 0.02, ml.BRASS)
    b.profile(ml.circle(sh.x(166), sh.z(91.5), 3.7 * sh.s, 12), -(T + 0.06), T + 0.06, 0.02, ml.BRASS)
    b.profile(ml.circle(sh.x(236), sh.z(88.0), 1.8 * sh.s, 10), -(T + 0.05), T + 0.05, 0.02, ml.STEEL)
    return b, "SM_VHW_04_Pistol", "ranged"


# ───────────────────────── 윤곽으로 만드는 무기 (권총 외 9종) ─────────────────────────
# 구상안 그림의 윤곽(trace_masks.ps1 → Saved/WeaponBuild/Contours)을 그대로 밀어내 입체로 만들고, 옆면에 구상안 그림을 입힌다.
# 두께는 무기 길이 방향(그림 x 좌표)의 구간마다 다르게 준다: 손잡이는 두껍게, 날은 얇게.
#   rot   = 구상안 그림에서 무기가 기울어 그려진 각도 (근접 무기는 비스듬히 그려져 있다) → 모델은 수평으로 바로 세운다
#   grip  = 손잡이(원점)의 그림 좌표, length = 실제 길이 cm, seg = [(그림 x 시작, 끝, 반두께 cm), ...]
SIDE = {
    "baton":   dict(num=1, name="Baton", cat="melee", rot=(-10, 300, 118), grip=(130, 96), length=65,
                    seg=[(-1e4, 228, 1.5), (228, 358, 1.05), (358, 472, 0.85), (472, 1e4, 0.65)]),
    "sword":   dict(num=2, name="Sword", cat="melee", rot=(-9, 300, 120), grip=(110, 92), length=90,
                    seg=[(-1e4, 170, 1.3), (170, 182, 2.2), (182, 198, 0.9), (198, 1e4, 0.35)]),
    "beam":    dict(num=3, name="BeamSword", cat="melee", rot=(-8, 300, 122), grip=(115, 108), length=100,
                    seg=[(-1e4, 215, 1.4), (215, 250, 1.8), (250, 1e4, 0.55)]),
    "flamer":  dict(num=5, name="Flamethrower", cat="ranged", rot=None, grip=(222, 160), length=95,
                    seg=[(-1e4, 1e4, 2.4)]),
    "rail":    dict(num=6, name="Railgun", cat="ranged", rot=None, grip=(262, 185), length=120,
                    seg=[(-1e4, 300, 2.6), (300, 1e4, 2.1)]),
    "taser":   dict(num=7, name="Taser", cat="ranged", rot=None, grip=(178, 165), length=24,
                    seg=[(-1e4, 380, 1.5), (380, 1e4, 2.0)]),
    "grenade": dict(num=8, name="Grenade", cat="thrown", rot=None, grip=(300, 145), length=None, height=13, chamfer=0.0,
                    seg=[(-1e4, 256, 0.3), (256, 344, 2.8), (344, 1e4, 0.6)]),
}
# 지뢰: 위에서 본 탑뷰 그림의 윤곽을 높이 방향으로 밀어내고 탑뷰 그림을 위에서 입힌다 (피벗 = 바닥 가운데)
TOP = {
    "blast": dict(num=9, name="BlastMine", size=30, height=4.0, mat="MI_VHW_Art_Top09"),
    "shock": dict(num=10, name="ShockMine", size=34, height=3.2, mat="MI_VHW_Art_Top10"),
}
CONTOURS = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "WeaponBuild", "Contours")
MIN_HOLE = 300.0     # 이보다 작은 구멍(그림 단위²)은 무시


def _loops(kind, num):
    d = json.load(open(os.path.join(CONTOURS, "%s_%02d.json" % (kind, num)), encoding="utf-8"))
    loops = d["loops"]
    sign = 1 if max(loops, key=lambda l: abs(l["area"]))["area"] > 0 else -1
    outers = [l["pts"] for l in loops if l["area"] * sign > 0]
    holes = [l["pts"] for l in loops if l["area"] * sign < 0 and abs(l["area"]) >= MIN_HOLE]
    return outers, holes


def _rotate(p, theta, cx, cy):
    a = math.radians(theta)
    x, y = p[0] - cx, p[1] - cy
    return (cx + x * math.cos(a) - y * math.sin(a), cy + x * math.sin(a) + y * math.cos(a))


def build_side(key):
    p = SIDE[key]
    outers, holes = _loops("side", p["num"])
    rot = p["rot"]
    if rot:     # 기울어 그려진 그림 → 수평으로 바로 세운 좌표
        outers = [[_rotate(q, -rot[0], rot[1], rot[2]) for q in o] for o in outers]
        holes = [[_rotate(q, -rot[0], rot[1], rot[2]) for q in h] for h in holes]
    xs = [q[0] for o in outers for q in o]
    ys = [q[1] for o in outers for q in o]
    s = p["length"] / (max(xs) - min(xs)) if p["length"] else p["height"] / (max(ys) - min(ys))
    gx, gy = p["grip"]
    col, row = (p["num"] - 1) % 2, (p["num"] - 1) // 2

    def uv(x, y, z):    # 모델 좌표 → (수평 그림 좌표) → 기울어진 원래 그림 좌표 → 옆모습 모음 그림의 UV
        q = (x / s + gx, gy - z / s)
        if rot:
            q = _rotate(q, rot[0], rot[1], rot[2])
        return ((col + q[0] / 600.0) / 2.0, (row + q[1] / 240.0) / 5.0)

    b = ml.Builder(uv)
    to_model = lambda pts: [((q[0] - gx) * s, (gy - q[1]) * s) for q in pts]
    for (x0, x1, half) in p["seg"]:
        for o in outers:
            # 모서리 깎기: 고리·핀처럼 아주 가는 부분이 있으면 안쪽으로 줄이다 꼬이므로 무기별로 끌 수 있다 (chamfer=0)
            part = b.profile_part(to_model(o), -half, half, min(p.get("chamfer", 0.12), half * 0.3), 0)
            for h in holes:
                b.subtract(part, b.profile_part(to_model(h), -half - 1.0, half + 1.0, 0.0, 0))
            if x0 > -1e3 or x1 < 1e3:   # 이 구간만 남긴다
                lo, hi = (max(x0, -5000) - gx) * s, (min(x1, 5000) - gx) * s
                b.intersect(part, b.profile_part(ml.rect(lo, -500.0, hi, 500.0), -half - 1.0, half + 1.0, 0.0, 0))
            b.add(part)
    return b, "SM_VHW_%02d_%s" % (p["num"], p["name"]), ["MI_VHW_Art"]


def build_top(key):
    p = TOP[key]
    outers, holes = _loops("top", p["num"])
    xs = [q[0] for o in outers for q in o]
    s = p["size"] / (max(xs) - min(xs))
    uv = lambda x, y, z: ((x / s + 60.0) / 120.0, (y / s + 30.0) / 60.0)
    b = ml.Builder(uv)
    for o in outers:
        part = b.top_part([((q[0] - 60.0) * s, (q[1] - 30.0) * s) for q in o], 0.0, p["height"], 0.0, 0)   # 전극 끝처럼 뾰족한 곳이 있어 모서리 깎기는 하지 않는다
        for h in holes:
            b.subtract(part, b.top_part([((q[0] - 60.0) * s, (q[1] - 30.0) * s) for q in h], -1.0, p["height"] + 1.0, 0.0, 0))
        b.add(part)
    return b, "SM_VHW_%02d_%s" % (p["num"], p["name"]), [p["mat"]]


BUILDERS = {"pistol": build_pistol}
BUILDERS.update({k: (lambda k=k: build_side(k)) for k in SIDE})
BUILDERS.update({k: (lambda k=k: build_top(k)) for k in TOP})


def main(names):
    if not names or names == ["all"]:
        names = list(BUILDERS.keys())
    for name in names:
        if name not in BUILDERS:
            unreal.log_warning("[WeaponBuild] 모르는 무기: %s (가능: %s)" % (name, ", ".join(BUILDERS)))
            continue
        builder, asset_name, mats = BUILDERS[name]()
        mesh = builder.finish()
        if isinstance(mats, str):       # 권총: 분류 → 재질 6칸
            mat_list = materials(mats)
        else:                           # 윤곽 무기: 그림 재질 1칸
            mat_list = [unreal.EditorAssetLibrary.load_asset("%s/%s" % (MAT_DIR, m)) for m in mats]
        asset = ml.save_static_mesh(mesh, "%s/%s" % (MESH_DIR, asset_name), mat_list)
        box = asset.get_bounding_box()
        size = box.max - box.min
        unreal.log("[WeaponBuild] %s: 삼각형 %d, 크기 %.1f x %.1f x %.1f cm → %s" % (name, builder.tri_count, size.x, size.y, size.z, asset.get_path_name()))


main(sys.argv[1:])
