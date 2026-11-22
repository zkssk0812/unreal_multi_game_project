# -*- coding: utf-8 -*-
"""소비 아이템 4종 바닥 모델·아이콘 만들기 (구상안 Docs/Design/Consumables_v1). 언리얼 에디터 안에서 실행한다.

  에디터 아래쪽 Cmd 칸:  py "C:/UnrealProjects/MCPTest/Tools/WeaponBuild/build_consumables.py" materials meshes icons
    materials : 재질 인스턴스(무광 블랙·스틸·황동 + 종류별 발광 띠 4색). 부모 = 무기 마스터 재질 M_VHWeapon (그림 입히기 끔)
    meshes    : 스태틱 메시 SM_VHC_1_HealKit … SM_VHC_4_Jetpack (실제 크기보다 키운 "보이는 크기", 원점 = 바닥 가운데, +Z = 위)
    icons     : Saved/WeaponBuild/Consumables 의 아이콘 PNG(render_consumables.ps1 결과)를 임포트
  모델러가 만든 모델이 오면 이 스크립트는 필요 없다: DA_VHWeaponCatalog 의 Consumables 칸만 바꾸면 된다.

모양은 구상안의 탑뷰(위에서 본 모습)·옆모습 좌표를 그대로 옮겼다: 그림 1단위 = 0.3cm, 탑뷰 그림의 오른쪽 = +Y, 위쪽 = +X.
"""
import importlib
import math
import os
import sys

import unreal

if unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor():
    raise RuntimeError("[ConsumableBuild] 플레이(PIE) 중에는 실행할 수 없습니다. 플레이를 멈추고 다시 실행하세요.")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vh_meshlib as ml
importlib.reload(ml)

ROOT = "/Game/VaultHeist/Consumables"
MESH_DIR, MAT_DIR, ICON_DIR = ROOT + "/Meshes", ROOT + "/Materials", ROOT + "/Icons"
MASTER = "/Game/VaultHeist/Weapons/Materials/M_VHWeapon"
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()

# (번호, 이름, 발광 띠 색 sRGB) — 서버 ConsumableId 순서
ITEMS = [(1, "HealKit", "2BF5B0"), (2, "Smoke", "B07CFF"), (3, "Flashbang", "FFE45C"), (4, "Jetpack", "4C7DFF")]
U = 0.3     # 구상안 그림 1단위 = 0.3cm

BLACK, STEEL, CARBON, LEATHER, BRASS, GLOW = ml.BLACK, ml.STEEL, ml.CARBON, ml.LEATHER, ml.BRASS, ml.GLOW


def srgb(hex_color, alpha=1.0):
    def to_linear(v):
        v /= 255.0
        return v / 12.92 if v <= 0.04045 else pow((v + 0.055) / 1.055, 2.4)
    r, g, b = (to_linear(int(hex_color[i:i + 2], 16)) for i in (0, 2, 4))
    return unreal.LinearColor(r, g, b, alpha)


# ───────────── 재질 ─────────────
# 이름: (바탕색, 금속, 거칠기, 반사, 발광색 또는 None, 발광 세기)
MATS = {
    "MI_VHC_Black": ("101113", 0.10, 0.52, 0.18, None, 0.0),
    "MI_VHC_Steel": ("9EA3AA", 1.00, 0.36, 0.5, None, 0.0),
    "MI_VHC_Brass": ("CFA650", 1.00, 0.30, 0.5, None, 0.0),
}
for (_, name, color) in ITEMS:
    MATS["MI_VHC_Band_" + name] = ("0A0A0C", 0.0, 0.4, 0.5, color, 3.0)   # 7 은 탑뷰에서 하얗게 떠서 색이 안 보였다


def do_materials():
    master = EAL.load_asset(MASTER)
    if master is None:
        raise RuntimeError("무기 마스터 재질이 없습니다: " + MASTER)
    for name, (color, metal, rough, spec, glow, power) in MATS.items():
        path = "%s/%s" % (MAT_DIR, name)
        if EAL.does_asset_exist(path):
            mi = EAL.load_asset(path)
        else:
            mi = TOOLS.create_asset(name, MAT_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
            MEL.set_material_instance_parent(mi, master)
        MEL.set_material_instance_vector_parameter_value(mi, "BaseColor", srgb(color))
        MEL.set_material_instance_scalar_parameter_value(mi, "Metallic", metal)
        MEL.set_material_instance_scalar_parameter_value(mi, "Roughness", rough)
        MEL.set_material_instance_scalar_parameter_value(mi, "Specular", spec)
        MEL.set_material_instance_scalar_parameter_value(mi, "ArtAmount", 0.0)       # 무기 옆모습 그림은 쓰지 않는다 (단색)
        MEL.set_material_instance_scalar_parameter_value(mi, "GlowArtStrength", 0.0)
        MEL.set_material_instance_vector_parameter_value(mi, "EmissiveColor", srgb(glow) if glow else unreal.LinearColor(0, 0, 0, 1))
        MEL.set_material_instance_scalar_parameter_value(mi, "EmissiveStrength", power)
        MEL.update_material_instance(mi)
        EAL.save_loaded_asset(mi, False)
    unreal.log("[ConsumableBuild] 재질 %d개 → %s" % (len(MATS), MAT_DIR))


def materials_for(name):
    names = ["MI_VHC_Black", "MI_VHC_Steel", "MI_VHC_Black", "MI_VHC_Black", "MI_VHC_Brass", "MI_VHC_Band_" + name]
    out = []
    for n in names:
        m = EAL.load_asset("%s/%s" % (MAT_DIR, n))
        if m is None:
            raise RuntimeError("재질이 없습니다: %s (materials 단계를 먼저 실행)" % n)
        out.append(m)
    return out


# ───────────── 모양 도우미 ─────────────
def circ(x, y, r, n=28):
    return ml.circle(x, y, r, n)


def cyl(b, x, y, r, z0, z1, mat, chamfer=0.0, n=28):
    return b.add(b.top_part(circ(x, y, r, n), z0, z1, chamfer, mat))


def ring(b, x, y, r_out, r_in, z0, z1, mat, n=28):
    part = b.top_part(circ(x, y, r_out, n), z0, z1, 0.0, mat)
    tool = b.top_part(circ(x, y, r_in, n), z0 - 1.0, z1 + 1.0, 0.0, mat)
    return b.add(ml.Builder.subtract(part, tool))


def box(b, x0, y0, x1, y1, z0, z1, mat, r=0.0, chamfer=0.0):
    outline = ml.rounded_rect(x0, y0, x1, y1, r) if r > 0 else ml.rect(x0, y0, x1, y1)
    return b.add(b.top_part(outline, z0, z1, chamfer, mat))


def lying(b, x0, x1, r, zc, mat, end_scale=1.0, n=24):
    """X 방향으로 누운 원통 (가운데 높이 zc). end_scale < 1 이면 양 끝을 둥글게 줄인다"""
    sec = circ(0.0, 0.0, r, n)
    if end_scale < 1.0:
        c = min(1.0, (x1 - x0) * 0.25)
        st = [(x0, end_scale, zc), (x0 + c, 1.0, zc), (x1 - c, 1.0, zc), (x1, end_scale, zc)]
    else:
        st = [(x0, 1.0, zc), (x1, 1.0, zc)]
    return b.section(sec, st, mat)


def top_xy(sx, sy):
    """구상안 탑뷰 그림 좌표(200×200, 가운데 100,100) → 모델 (X = 위쪽, Y = 오른쪽) cm"""
    return (-(sy - 100.0) * U, (sx - 100.0) * U)


# ───────────── 01 회복 키트: 손잡이 달린 네모 상자 + 뚜껑의 민트 십자 ─────────────
def build_healkit():
    b = ml.Builder()
    hx, hy, h = 56 * U, 76 * U, 23.4          # 탑뷰 152×112 단위, 옆모습 높이 78 단위
    box(b, -hx, -hy, hx, hy, 0.0, h, BLACK, r=14 * U, chamfer=1.2)
    box(b, -hx - 0.35, -hy - 0.35, hx + 0.35, hy + 0.35, 15.6, 17.4, STEEL, r=14 * U)       # 뚜껑 이음매 스틸 띠
    box(b, -hx - 0.3, -hy - 0.3, hx + 0.3, hy + 0.3, 19.8, 20.7, GLOW, r=14 * U)            # 옆면 발광 줄
    # 뚜껑 위 스틸 테두리 (탑뷰 안쪽 테)
    part = b.top_part(ml.rounded_rect(-hx + 2.0, -hy + 2.0, hx - 2.0, hy - 2.0, 9 * U), h - 0.2, h + 0.5, 0.0, STEEL)
    tool = b.top_part(ml.rounded_rect(-hx + 3.0, -hy + 3.0, hx - 3.0, hy - 3.0, 7 * U), h - 1.0, h + 1.5, 0.0, STEEL)
    b.add(ml.Builder.subtract(part, tool))
    # 황동 모서리 4개
    for sx in (-1, 1):
        for sy in (-1, 1):
            cyl(b, sx * (hx - 4.2), sy * (hy - 4.2), 2.7, 0.0, h + 0.8, BRASS, chamfer=0.5)
    # 스틸 걸쇠 2개 (앞 = +X 면)
    for sy in (-1, 1):
        box(b, hx - 0.5, sy * 16.0 - 2.1, hx + 0.9, sy * 16.0 + 2.1, 13.0, 18.6, STEEL, chamfer=0.3)
    # 접어 둔 손잡이 (뚜껑 위 앞쪽) + 스틸 끝
    box(b, hx - 6.0, -7.8, hx - 2.4, 7.8, h, h + 1.6, BLACK, r=1.5, chamfer=0.4)
    for sy in (-1, 1):
        box(b, hx - 6.4, sy * 8.4 - 1.5, hx - 2.0, sy * 8.4 + 1.5, h, h + 1.9, STEEL, chamfer=0.3)
    # 민트 십자 (팔 굵기 28 단위, 길이 72 단위)
    a, t = 36 * U, 14 * U
    cross = [(t, -a), (t, -t), (a, -t), (a, t), (t, t), (t, a), (-t, a), (-t, t), (-a, t), (-a, -t), (-t, -t), (-t, -a)]
    cross = [(x - 1.5, y) for (x, y) in cross]
    b.add(b.top_part(cross, h, h + 1.0, 0.2, GLOW))
    return b.finish()


# ───────────── 02 연막탄: 세운 깡통 + 옆으로 나온 레버·고리 ─────────────
def build_smoke():
    b = ml.Builder()
    r, h = 13.0, 34.0
    cyl(b, 0, 0, r + 0.5, 0.0, 2.4, STEEL, chamfer=0.5)                  # 바닥 스틸 테
    cyl(b, 0, 0, r, 2.0, h - 4.0, BLACK, chamfer=0.4)                    # 몸통
    cyl(b, 0, 0, r + 0.25, h - 13.0, h - 10.4, GLOW)                     # 위 띠
    cyl(b, 0, 0, r + 0.25, 6.0, 7.5, GLOW)                               # 아래 띠
    cyl(b, 0, 0, r + 0.9, h - 4.4, h, STEEL, chamfer=0.6)                # 뚜껑 (스틸)
    ring(b, 0, 0, r + 0.9, r - 2.6, h, h + 0.6, GLOW)                    # 뚜껑 둘레 발광 고리 (탑뷰에서 보이는 고리)
    for k in range(8):                                                   # 연기 구멍 8개
        a = k * math.pi / 4.0
        cyl(b, math.cos(a) * 7.5, math.sin(a) * 7.5, 1.55, h - 0.3, h + 0.35, BLACK, n=12)
    cyl(b, 0, 0, 3.2, h, h + 1.6, BRASS, chamfer=0.5)                    # 가운데 황동 마개
    box(b, -1.7, r - 1.0, 1.7, r + 7.2, h - 9.0, h - 1.0, BRASS, r=0.8, chamfer=0.4)   # 레버
    ring(b, 0, r + 10.4, 3.4, 1.8, h - 3.6, h - 2.2, BRASS, n=20)        # 안전 고리
    return b.finish()


# ───────────── 03 섬광탄: 눕힌 원통 + 양 끝 노란 띠 + 위의 황동 레버 ─────────────
def build_flashbang():
    b = ml.Builder()
    half, r = 25.8, 7.8
    zc = r
    lying(b, -half + 5.0, half - 5.0, r, zc, BLACK)
    lying(b, -half, -half + 5.6, r + 0.25, zc, STEEL, end_scale=0.82)
    lying(b, half - 5.6, half, r + 0.25, zc, STEEL, end_scale=0.82)
    lying(b, -17.4, -14.7, r + 0.3, zc, GLOW)
    lying(b, 14.7, 17.4, r + 0.3, zc, GLOW)
    # 몸통 구멍 줄 (위에서 보이는 두 줄): 스틸 테 + 검은 구멍
    for row in (-1, 1):
        for k in range(7):
            x = -10.5 + k * 3.5
            y = row * 4.2
            zt = zc + math.sqrt(max(0.0, r * r - y * y))
            cyl(b, x, y, 1.15, zt - 1.0, zt + 0.25, STEEL, n=10)
            cyl(b, x, y, 0.7, zt - 1.0, zt + 0.32, BLACK, n=10)
    box(b, -12.6, -2.1, 13.2, 2.1, 2 * r - 0.3, 2 * r + 1.3, BRASS, r=1.2, chamfer=0.4)   # 레버
    ring(b, -half - 2.6, 0, 3.0, 1.6, zc + 2.0, zc + 3.4, BRASS, n=20)                    # 고리
    return b.finish()


# ───────────── 04 제트팩: 쌍둥이 탱크 + 가운데 등판 ─────────────
def build_jetpack():
    b = ml.Builder()
    r, y = 10.0, 10.8
    box(b, -4.8, -y, 4.8, y, 6.0, 33.0, BLACK, r=1.5, chamfer=0.6)        # 등판
    for z in (10.0, 25.0):                                                 # 황동 띠
        box(b, -5.4, -1.8, 5.4, 1.8, z, z + 2.0, BRASS, chamfer=0.3)
    box(b, -6.0, -1.8, 6.0, 1.8, 33.0, 34.0, BRASS, chamfer=0.3)
    for sy in (-1, 1):
        cy = sy * y
        cyl(b, 0, cy, 6.0, 0.0, 0.9, GLOW)                                  # 분사구 발광
        cyl(b, 0, cy, 7.0, 0.8, 5.4, STEEL, chamfer=0.6)                    # 분사구
        cyl(b, 0, cy, r, 4.8, 35.6, BLACK, chamfer=0.5)                     # 탱크
        cyl(b, 0, cy, r + 0.25, 15.6, 17.7, GLOW)                           # 탱크 띠
        cyl(b, 0, cy, r + 0.2, 35.4, 36.4, GLOW)                            # 어깨 발광 고리 (탑뷰)
        cyl(b, 0, cy, 7.0, 36.3, 39.0, STEEL, chamfer=0.8)                  # 돔
        cyl(b, 0, cy, 5.6, 39.0, 41.0, STEEL, chamfer=0.8)
        cyl(b, 0, cy, 3.6, 41.0, 42.3, STEEL, chamfer=0.5)
        cyl(b, 0, cy, 2.2, 42.3, 44.6, BRASS, chamfer=0.4)                  # 밸브
    return b.finish()


BUILDERS = {"HealKit": build_healkit, "Smoke": build_smoke, "Flashbang": build_flashbang, "Jetpack": build_jetpack}


def do_meshes():
    for (num, name, _) in ITEMS:
        mesh = BUILDERS[name]()
        path = "%s/SM_VHC_%d_%s" % (MESH_DIR, num, name)
        ml.save_static_mesh(mesh, path, materials_for(name))
        box_ = EAL.load_asset(path).get_bounding_box()
        unreal.log("[ConsumableBuild] %s 삼각형 %d, 크기 %s ~ %s" % (path, GS_tri(mesh), box_.min, box_.max))


def GS_tri(mesh):
    q = unreal.GeometryScript_MeshQueries
    return q.get_num_triangle_i_ds(mesh) if hasattr(q, "get_num_triangle_i_ds") else -1


def do_icons():
    src = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "WeaponBuild", "Consumables")
    tasks = []
    for (num, name, _) in ITEMS:
        png = os.path.join(src, "T_VHC_Icon_%d_%s.png" % (num, name))
        if not os.path.exists(png):
            unreal.log_warning("[ConsumableBuild] 아이콘 PNG 없음: %s (render_consumables.ps1 먼저)" % png)
            continue
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", png)
        t.set_editor_property("destination_path", ICON_DIR)
        t.set_editor_property("destination_name", "T_VHC_Icon_%d_%s" % (num, name))
        t.set_editor_property("automated", True)
        t.set_editor_property("replace_existing", True)
        t.set_editor_property("save", False)
        tasks.append(t)
    TOOLS.import_asset_tasks(tasks)
    for (num, name, _) in ITEMS:
        path = "%s/T_VHC_Icon_%d_%s" % (ICON_DIR, num, name)
        tex = EAL.load_asset(path) if EAL.does_asset_exist(path) else None
        if tex is None:
            continue
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP)
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
        tex.set_editor_property("never_stream", True)
        tex.set_editor_property("srgb", True)
        EAL.save_loaded_asset(tex, False)
    unreal.log("[ConsumableBuild] 아이콘 임포트 → %s" % ICON_DIR)


STEPS = {"materials": do_materials, "meshes": do_meshes, "icons": do_icons}
for step in (sys.argv[1:] or ["materials", "meshes", "icons"]):
    STEPS[step]()
unreal.log("[ConsumableBuild] 끝: %s" % " ".join(sys.argv[1:]))
