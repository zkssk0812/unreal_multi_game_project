# -*- coding: utf-8 -*-
"""무기 에셋 준비 (아이콘 임포트 · 재질 · 데이터 에셋). 언리얼 에디터 안에서 실행한다.

  에디터 아래쪽 Cmd 칸:  py "C:/UnrealProjects/MCPTest/Tools/WeaponBuild/setup_weapon_assets.py" icons materials data
    icons     : Saved/WeaponBuild/Icons 의 PNG 10장(render_icons.ps1 결과)을 /Game/VaultHeist/Weapons/Icons 로 임포트
    art       : 구상안 탑뷰 그림 10장(바닥용)과 옆모습 모음 1장(3D 모델 옆면용)을 /Game/VaultHeist/Weapons/Art 로 임포트
    materials : 마스터 재질 M_VHWeapon + 공유 재질 인스턴스(블랙·스틸·카본·가죽·황동·발광 4색). 이미 있는 것은 그대로 둔다
    materials_reset : 재질 인스턴스 값을 이 스크립트의 표로 되돌린다
    data      : DA_VHWeaponCatalog(무기 10종 표) · DA_VHCharacterVisuals(캐릭터 표)를 만들고 채운다.
                이미 있으면 아이콘·메시 칸만 다시 잇고 사람이 고친 값(보정·배율)은 건드리지 않는다.
"""
import os
import sys

import unreal

# 플레이 중에는 에셋을 만들거나 저장할 수 없다(에디터가 "덮어쓸까요?" 창을 띄우고 멈춘다) → 먼저 플레이를 멈춘다
if unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor():
    raise RuntimeError("[WeaponBuild] 플레이(PIE) 중에는 실행할 수 없습니다. 플레이를 멈추고 다시 실행하세요.")
ROOT = "/Game/VaultHeist/Weapons"
ICON_DIR, MAT_DIR, MESH_DIR, DATA_DIR, ART_DIR = ROOT + "/Icons", ROOT + "/Materials", ROOT + "/Meshes", ROOT + "/Data", ROOT + "/Art"
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()

# (번호, 영문 이름, 열거형 이름, 분류)
WEAPONS = [
    (1, "Baton", "BATON", "melee"), (2, "Sword", "SWORD", "melee"), (3, "BeamSword", "BEAM_SWORD", "melee"),
    (4, "Pistol", "PISTOL", "ranged"), (5, "Flamethrower", "FLAMETHROWER", "ranged"), (6, "Railgun", "RAILGUN", "ranged"),
    (7, "Taser", "TASER", "ranged"), (8, "Grenade", "GRENADE", "thrown"), (9, "BlastMine", "BLAST_MINE", "placed"),
    (10, "ShockMine", "SHOCK_MINE", "placed"),
]
CATEGORY = {"melee": unreal.VHWeaponCategory.MELEE, "ranged": unreal.VHWeaponCategory.RANGED,
            "thrown": unreal.VHWeaponCategory.THROWN, "placed": unreal.VHWeaponCategory.PLACED}


def srgb(hex_color, alpha=1.0):
    """구상안의 색(sRGB 16진수) → 재질에 넣는 리니어 색"""
    def to_linear(v):
        v /= 255.0
        return v / 12.92 if v <= 0.04045 else pow((v + 0.055) / 1.055, 2.4)
    r, g, b = (to_linear(int(hex_color[i:i + 2], 16)) for i in (0, 2, 4))
    return unreal.LinearColor(r, g, b, alpha)


# 탑뷰 그림(120×60 단위) 한 장이 실제로 차지하는 크기(cm): 번호 → (가로, 세로). 그림 속 무기 길이를 3D 모델 길이에 맞춘 값
FLOOR_ART_SIZE = {1: (81.3, 40.6), 2: (106.9, 53.5), 3: (112.1, 56.1), 4: (41.3, 20.6), 5: (109.6, 54.8),
                  6: (126.3, 63.2), 7: (45.7, 22.9), 8: (22.1, 11.0), 9: (81.8, 40.9), 10: (78.5, 39.2)}
# 무기별 표시 배율 (든 모습, 바닥). 전체 배율(DA 의 HeldScale·FloorScale = 2배) 에 곱한다.
# 긴 무기는 2배면 캐릭터 키만큼 길어져서 줄인다 (권총 = 1 기준, 사용자 결정 2026-11-16: 전체 2배)
WEAPON_SCALE = {1: (0.85, 0.6), 2: (0.7, 0.55), 3: (0.65, 0.5), 4: (1.0, 1.0), 5: (0.6, 0.6), 6: (0.5, 0.5),
                7: (1.0, 1.0), 8: (0.8, 1.0), 9: (0.6, 0.75), 10: (0.6, 0.75)}
# 2026-11-17: 근접 3종은 손에 들면 탑뷰에서 가는 선이라 길이(0.85/0.7/0.65 → 화면 110~130cm, 캐릭터 키 170cm 아래)와
# 두께(HELD_THICK, 폭·높이에만 곱함)를 따로 키운다. 수류탄은 1.4배가 도시락통만 해 보여 0.8배 (2026-11-20)
HELD_THICK = {1: 1.8, 2: 1.6, 3: 1.6}
# 손잡이 맞춤(무기 = 이 보정 × 손 소켓). 지뢰는 피벗이 바닥 가운데라 오른손이 오른쪽 가장자리를 쥐게 왼쪽(−Y)으로 민다
GRIP_OFFSET = {9: (0.0, -18.0, -3.0), 10: (0.0, -19.0, -3.0)}
# 지뢰 2종의 발광 그림 세기 (1.4 는 가까이서 하얗게 떠서 낮춤, 2026-11-17)
MINE_GLOW = 0.6
# 바닥 방식: "B" = 3D 모델(눕힘), "A" = 구상안 탑뷰 그림 판. 무기마다 탑뷰에서 찍어 비교해 고른다 (사용자 결정 2026-11-16)
FLOOR_MODE = {1: "B", 2: "B", 3: "B", 4: "B", 5: "B", 6: "B", 7: "A", 8: "B", 9: "B", 10: "B"}   # 고른 이유는 Docs/작업보고_2026-11-16.md 의 바닥 방식 표


def _import_texture(png, folder, name, ui):
    if not os.path.exists(png):
        unreal.log_warning("[WeaponBuild] PNG 없음: %s (Tools/WeaponBuild/render_icons.ps1 을 먼저 실행)" % png)
        return None
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", png)
    t.set_editor_property("destination_path", folder)
    t.set_editor_property("destination_name", name)
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("save", False)
    TOOLS.import_asset_tasks([t])
    tex = EAL.load_asset("%s/%s" % (folder, name))
    if tex is None:
        return None
    if ui:      # HUD 아이콘: 압축으로 뭉개지지 않게, 밉맵·스트리밍 없이
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
        tex.set_editor_property("never_stream", True)
    tex.set_editor_property("srgb", True)
    EAL.save_loaded_asset(tex, False)
    return tex


# ───────────── 구상안 그림: 바닥용 탑뷰 그림 10장 + 3D 모델 옆면용 옆모습 모음 1장 ─────────────
def do_art():
    src = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "WeaponBuild")
    count = 0
    for (num, name, _, _) in WEAPONS:
        if _import_texture(os.path.join(src, "FloorArt", "T_VHW_Top_%02d_%s.png" % (num, name)), ART_DIR, "T_VHW_Top_%02d_%s" % (num, name), False):
            count += 1
    atlas = _import_texture(os.path.join(src, "T_VHW_SideAtlas.png"), ART_DIR, "T_VHW_SideAtlas", False)
    _import_texture(os.path.join(src, "T_VHW_GlowAtlas.png"), ART_DIR, "T_VHW_GlowAtlas", False)
    for (num, name) in ((9, "BlastMine"), (10, "ShockMine")):
        _import_texture(os.path.join(src, "FloorArt", "T_VHW_TopGlow_%02d_%s.png" % (num, name)), ART_DIR, "T_VHW_TopGlow_%02d_%s" % (num, name), False)
    unreal.log("[WeaponBuild] 탑뷰 그림 %d장, 옆모습 모음 %s → %s" % (count, "임포트" if atlas else "없음", ART_DIR))


# ───────────── 아이콘 ─────────────
def do_icons():
    src = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "WeaponBuild", "Icons")
    tasks = []
    for (num, name, _, _) in WEAPONS:
        png = os.path.join(src, "T_VHW_Icon_%02d_%s.png" % (num, name))
        if not os.path.exists(png):
            unreal.log_warning("[WeaponBuild] 아이콘 PNG 없음: %s (Tools/WeaponBuild/render_icons.ps1 을 먼저 실행)" % png)
            continue
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", png)
        t.set_editor_property("destination_path", ICON_DIR)
        t.set_editor_property("destination_name", "T_VHW_Icon_%02d_%s" % (num, name))
        t.set_editor_property("automated", True)
        t.set_editor_property("replace_existing", True)
        t.set_editor_property("save", False)
        tasks.append(t)
    TOOLS.import_asset_tasks(tasks)
    count = 0
    for (num, name, _, _) in WEAPONS:
        path = "%s/T_VHW_Icon_%02d_%s" % (ICON_DIR, num, name)
        tex = EAL.load_asset(path) if EAL.does_asset_exist(path) else None
        if tex is None:
            continue
        # UI용: 압축으로 뭉개지지 않게, 밉맵·스트리밍 없이
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
        tex.set_editor_property("never_stream", True)
        tex.set_editor_property("srgb", True)
        EAL.save_loaded_asset(tex, False)
        count += 1
    unreal.log("[WeaponBuild] 아이콘 %d개 임포트 → %s" % (count, ICON_DIR))


# 3D 모델에 입히는 구상안 그림의 밝기 (1 = 그림 색 그대로). 맵 조명이 밝고 따뜻해서 검은 부품은 그림 색 그대로 두면 회갈색으로 떠 보인다 → 낮춘다
ART_BRIGHTNESS = {"MI_VHW_Black": 0.38, "MI_VHW_Carbon": 0.45, "MI_VHW_Leather": 0.9}


# ───────────── 재질 ─────────────
def do_materials(force=False):
    atlas = EAL.load_asset(ART_DIR + "/T_VHW_SideAtlas") if EAL.does_asset_exist(ART_DIR + "/T_VHW_SideAtlas") else None
    if atlas is None:
        raise RuntimeError("[WeaponBuild] 옆모습 모음 그림이 없습니다. 먼저 art 단계를 실행하세요.")

    # 바닥 그림 판 재질: 조명과 관계없이 그림 그대로(Unlit), 그림의 투명한 곳은 뚫린다(Masked → 노란 테두리도 그림 윤곽을 따른다)
    floor_path = MAT_DIR + "/M_VHWeaponFloorArt"
    if not EAL.does_asset_exist(floor_path):
        fm = TOOLS.create_asset("M_VHWeaponFloorArt", MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
        fm.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        fm.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
        fm.set_editor_property("two_sided", True)
        tex = MEL.create_material_expression(fm, unreal.MaterialExpressionTextureSampleParameter2D, -600, 0)
        tex.set_editor_property("parameter_name", "Art")
        tex.set_editor_property("texture", EAL.load_asset(ART_DIR + "/T_VHW_Top_04_Pistol"))
        bright = MEL.create_material_expression(fm, unreal.MaterialExpressionScalarParameter, -600, 250)
        bright.set_editor_property("parameter_name", "Brightness")
        bright.set_editor_property("default_value", 1.0)
        fmul = MEL.create_material_expression(fm, unreal.MaterialExpressionMultiply, -300, 100)
        MEL.connect_material_expressions(tex, "RGB", fmul, "A")
        MEL.connect_material_expressions(bright, "", fmul, "B")
        MEL.connect_material_property(fmul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        MEL.connect_material_property(tex, "A", unreal.MaterialProperty.MP_OPACITY_MASK)
        MEL.recompile_material(fm)
        EAL.save_loaded_asset(fm, False)

    master_path = MAT_DIR + "/M_VHWeapon"
    master = EAL.load_asset(master_path) if EAL.does_asset_exist(master_path) else None
    glow_atlas = EAL.load_asset(ART_DIR + "/T_VHW_GlowAtlas")
    if master is not None and "GlowArtStrength" not in [str(n) for n in MEL.get_scalar_parameter_names(master)]:
        MEL.delete_all_material_expressions(master)     # 예전 판(그림 입히기 없음) → 그래프를 다시 짠다. 인스턴스 값은 그대로 남는다
        rebuild = True
    else:
        rebuild = master is None
    if master is None:
        master = TOOLS.create_asset("M_VHWeapon", MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    if rebuild:

        def vec(name, default, y):
            n = MEL.create_material_expression(master, unreal.MaterialExpressionVectorParameter, -700, y)
            n.set_editor_property("parameter_name", name)
            n.set_editor_property("default_value", default)
            return n

        def scalar(name, default, y):
            n = MEL.create_material_expression(master, unreal.MaterialExpressionScalarParameter, -700, y)
            n.set_editor_property("parameter_name", name)
            n.set_editor_property("default_value", default)
            return n

        # 바탕색 = 단색(BaseColor) 과 구상안 옆모습 그림(Art × ArtBrightness) 을 ArtAmount 로 섞는다 (1 = 그림 그대로)
        base = vec("BaseColor", unreal.LinearColor(0.02, 0.02, 0.022, 1.0), -300)
        art = MEL.create_material_expression(master, unreal.MaterialExpressionTextureSampleParameter2D, -700, -700)
        art.set_editor_property("parameter_name", "Art")
        art.set_editor_property("texture", atlas)
        # ArtPower: 어두운 색일수록 더 어둡게 (밝은 맵 조명에서 검은 부품이 회갈색으로 뜨지 않게, 황동·스틸은 덜 줄어든다)
        art_pow = MEL.create_material_expression(master, unreal.MaterialExpressionPower, -560, -680)
        MEL.connect_material_expressions(art, "RGB", art_pow, "Base")
        MEL.connect_material_expressions(scalar("ArtPower", 1.0, -560), "", art_pow, "Exp")
        art_mul = MEL.create_material_expression(master, unreal.MaterialExpressionMultiply, -450, -600)
        MEL.connect_material_expressions(art_pow, "", art_mul, "A")
        MEL.connect_material_expressions(scalar("ArtBrightness", 1.0, -500), "", art_mul, "B")
        mix = MEL.create_material_expression(master, unreal.MaterialExpressionLinearInterpolate, -250, -400)
        MEL.connect_material_expressions(base, "", mix, "A")
        MEL.connect_material_expressions(art_mul, "", mix, "B")
        MEL.connect_material_expressions(scalar("ArtAmount", 0.0, -400), "", mix, "Alpha")
        MEL.connect_material_property(mix, "", unreal.MaterialProperty.MP_BASE_COLOR)
        MEL.connect_material_property(scalar("Metallic", 0.0, -100), "", unreal.MaterialProperty.MP_METALLIC)
        MEL.connect_material_property(scalar("Specular", 0.5, 0), "", unreal.MaterialProperty.MP_SPECULAR)
        MEL.connect_material_property(scalar("Roughness", 0.5, 100), "", unreal.MaterialProperty.MP_ROUGHNESS)
        emissive = vec("EmissiveColor", unreal.LinearColor(0.0, 0.0, 0.0, 1.0), 250)
        strength = scalar("EmissiveStrength", 0.0, 450)
        mul = MEL.create_material_expression(master, unreal.MaterialExpressionMultiply, -400, 300)
        MEL.connect_material_expressions(emissive, "", mul, "A")
        MEL.connect_material_expressions(strength, "", mul, "B")
        # 발광 그림(구상안에서 분류 색으로 그린 선만 모은 그림) × GlowArtStrength 를 더한다
        glow = MEL.create_material_expression(master, unreal.MaterialExpressionTextureSampleParameter2D, -700, 650)
        glow.set_editor_property("parameter_name", "GlowArt")
        glow.set_editor_property("texture", glow_atlas)
        glow_mul = MEL.create_material_expression(master, unreal.MaterialExpressionMultiply, -400, 650)
        MEL.connect_material_expressions(glow, "RGB", glow_mul, "A")
        MEL.connect_material_expressions(scalar("GlowArtStrength", 0.0, 850), "", glow_mul, "B")
        add = MEL.create_material_expression(master, unreal.MaterialExpressionAdd, -200, 450)
        MEL.connect_material_expressions(mul, "", add, "A")
        MEL.connect_material_expressions(glow_mul, "", add, "B")
        MEL.connect_material_property(add, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        MEL.recompile_material(master)
        EAL.save_loaded_asset(master, False)

    # 이름: (바탕색 sRGB, 금속, 거칠기, 발광색 sRGB 또는 None, 발광 세기)
    table = {
        "MI_VHW_Black":   ("101113", 0.10, 0.52, None, 0.0),      # 무광 블랙(숯색). 밝은 방 조명에서도 검게 보이도록 구상안 색보다 어둡게
        "MI_VHW_Steel":   ("9EA3AA", 1.00, 0.36, None, 0.0),      # 브러시드 스틸
        "MI_VHW_Carbon":  ("0A0B0D", 0.35, 0.26, None, 0.0),      # 카본 (반광)
        "MI_VHW_Leather": ("6E4630", 0.00, 0.74, None, 0.0),      # 짙은 가죽
        "MI_VHW_Brass":   ("CFA650", 1.00, 0.30, None, 0.0),      # 황동
        "MI_VHW_Glow_Melee":  ("0A0A0C", 0.0, 0.4, "FF2D55", 9.0),
        "MI_VHW_Glow_Ranged": ("0A0A0C", 0.0, 0.4, "35D6FF", 9.0),
        "MI_VHW_Glow_Thrown": ("0A0A0C", 0.0, 0.4, "FF9A1F", 9.0),
        "MI_VHW_Glow_Placed": ("0A0A0C", 0.0, 0.4, "B6FF3A", 9.0),
    }
    for name, (color, metal, rough, glow, power) in table.items():
        path = "%s/%s" % (MAT_DIR, name)
        if EAL.does_asset_exist(path):
            if not force:
                continue    # 이미 있으면 그대로 둔다(에디터에서 조정한 값 보존). 표의 값으로 되돌리려면 materials_reset
            mi = EAL.load_asset(path)
        else:
            mi = TOOLS.create_asset(name, MAT_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
            MEL.set_material_instance_parent(mi, master)
        MEL.set_material_instance_vector_parameter_value(mi, "BaseColor", srgb(color))
        MEL.set_material_instance_scalar_parameter_value(mi, "Metallic", metal)
        MEL.set_material_instance_scalar_parameter_value(mi, "Roughness", rough)
        # 검은 부품은 밝은 방 조명이 겉면에 비쳐 갈색으로 떠 보이지 않게 반사를 낮춘다
        MEL.set_material_instance_scalar_parameter_value(mi, "Specular", 0.18 if name in ("MI_VHW_Black", "MI_VHW_Carbon") else 0.5)
        # 발광 칸만 단색(분류 색으로 빛남), 나머지 칸은 구상안 옆모습 그림을 그대로 입힌다
        MEL.set_material_instance_scalar_parameter_value(mi, "ArtAmount", 0.0 if glow else 1.0)
        MEL.set_material_instance_scalar_parameter_value(mi, "ArtBrightness", ART_BRIGHTNESS.get(name, 1.0))
        if glow:
            MEL.set_material_instance_vector_parameter_value(mi, "EmissiveColor", srgb(glow))
            MEL.set_material_instance_scalar_parameter_value(mi, "EmissiveStrength", power)
        MEL.update_material_instance(mi)
        EAL.save_loaded_asset(mi, False)
    # 윤곽으로 만든 무기(권총 외 9종)가 쓰는 재질: 칸 하나에 구상안 그림 전체 + 발광 그림.
    # 지뢰 2종은 탑뷰 그림을 위에서 입히므로 그림만 바꾼 인스턴스를 따로 둔다
    for name, art_tex, glow_tex in (("MI_VHW_Art", None, None),
                                    ("MI_VHW_Art_Top09", "T_VHW_Top_09_BlastMine", "T_VHW_TopGlow_09_BlastMine"),
                                    ("MI_VHW_Art_Top10", "T_VHW_Top_10_ShockMine", "T_VHW_TopGlow_10_ShockMine")):
        path = "%s/%s" % (MAT_DIR, name)
        if EAL.does_asset_exist(path) and not force:
            continue
        mi = EAL.load_asset(path) if EAL.does_asset_exist(path) else TOOLS.create_asset(name, MAT_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        MEL.set_material_instance_parent(mi, master)
        for k, v in (("ArtAmount", 1.0), ("ArtBrightness", 1.0), ("ArtPower", 1.5), ("Metallic", 0.25), ("Roughness", 0.45),
                     ("Specular", 0.3), ("GlowArtStrength", MINE_GLOW if art_tex else 2.5)):
            MEL.set_material_instance_scalar_parameter_value(mi, k, v)
        if art_tex:
            MEL.set_material_instance_texture_parameter_value(mi, "Art", EAL.load_asset("%s/%s" % (ART_DIR, art_tex)))
            MEL.set_material_instance_texture_parameter_value(mi, "GlowArt", EAL.load_asset("%s/%s" % (ART_DIR, glow_tex)))
        MEL.update_material_instance(mi)
        EAL.save_loaded_asset(mi, False)
    unreal.log("[WeaponBuild] 재질 준비 완료 → %s" % MAT_DIR)


def do_mine_glow():
    for name in ("MI_VHW_Art_Top09", "MI_VHW_Art_Top10"):
        mi = EAL.load_asset("%s/%s" % (MAT_DIR, name))
        MEL.set_material_instance_scalar_parameter_value(mi, "GlowArtStrength", MINE_GLOW)
        MEL.update_material_instance(mi)
        EAL.save_loaded_asset(mi, False)
    unreal.log("[WeaponBuild] 지뢰 발광 세기 = %.2f" % MINE_GLOW)


# ───────────── 데이터 에셋 ─────────────
def _data_asset(name, cls):
    path = "%s/%s" % (DATA_DIR, name)
    if EAL.does_asset_exist(path):
        return EAL.load_asset(path), False
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", cls)
    return TOOLS.create_asset(name, DATA_DIR, cls, factory), True


def do_data():
    catalog, _ = _data_asset("DA_VHWeaponCatalog", unreal.VHWeaponCatalog)
    rows = list(catalog.get_editor_property("weapons"))
    by_id = {int(r.get_editor_property("id").value): r for r in rows}
    out = []
    for (num, name, enum_name, cat) in WEAPONS:
        row = by_id.get(num)
        if row is None:
            row = unreal.VHWeaponVisual()
            row.set_editor_property("id", getattr(unreal.VHWeaponId, enum_name))
            row.set_editor_property("category", CATEGORY[cat])
            # 바닥 자세: 총·칼은 옆으로 눕혀 탑뷰에서 옆모습이 보이게, 지뢰는 그대로 놓는다
            lying = unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0) if cat == "placed" else unreal.Rotator(roll=90.0, pitch=0.0, yaw=0.0)
            row.set_editor_property("floor_offset", unreal.Transform(location=[0.0, 0.0, 12.0], rotation=lying, scale=[1.0, 1.0, 1.0]))
        icon_path = "%s/T_VHW_Icon_%02d_%s" % (ICON_DIR, num, name)
        if EAL.does_asset_exist(icon_path):
            row.set_editor_property("icon", EAL.load_asset(icon_path))
        top_path = "%s/T_VHW_Top_%02d_%s" % (ART_DIR, num, name)
        if EAL.does_asset_exist(top_path):
            row.set_editor_property("floor_art", EAL.load_asset(top_path))
        if num in FLOOR_ART_SIZE:
            row.set_editor_property("floor_art_size", unreal.Vector2D(*FLOOR_ART_SIZE[num]))
        mesh_path = "%s/SM_VHW_%02d_%s" % (MESH_DIR, num, name)
        if EAL.does_asset_exist(mesh_path):
            row.set_editor_property("mesh", EAL.load_asset(mesh_path))
        held, floor = WEAPON_SCALE.get(num, (1.0, 1.0))
        row.set_editor_property("held_scale", held)
        row.set_editor_property("held_thickness", HELD_THICK.get(num, 1.0))
        g = GRIP_OFFSET.get(num, (0.0, 0.0, 0.0))
        row.set_editor_property("grip_offset", unreal.Transform(location=list(g), rotation=unreal.Rotator(0.0, 0.0, 0.0), scale=[1.0, 1.0, 1.0]))
        row.set_editor_property("floor_scale", floor)
        if num in FLOOR_MODE:
            row.set_editor_property("floor_mode", unreal.VHWeaponFloorMode.FLAT_ART if FLOOR_MODE[num] == "A" else unreal.VHWeaponFloorMode.MODEL)
        out.append(row)
    catalog.set_editor_property("weapons", out)
    EAL.save_loaded_asset(catalog, False)

    visuals, created = _data_asset("DA_VHCharacterVisuals", unreal.VHCharacterVisuals)
    EAL.save_loaded_asset(visuals, False)
    unreal.log("[WeaponBuild] 데이터 에셋 준비 완료: 무기 %d종 (메시 %d, 아이콘 %d), 캐릭터 표 %s" % (
        len(out), sum(1 for r in out if r.get_editor_property("mesh") is not None),
        sum(1 for r in out if r.get_editor_property("icon") is not None), "새로 만듦" if created else "그대로"))


STEPS = {"icons": do_icons, "materials": do_materials, "materials_reset": lambda: do_materials(True), "data": do_data, "art": do_art, "mine_glow": do_mine_glow}
for step in sys.argv[1:]:
    if step in STEPS:
        STEPS[step]()
    else:
        unreal.log_warning("[WeaponBuild] 모르는 단계: %s (가능: %s)" % (step, ", ".join(STEPS)))
