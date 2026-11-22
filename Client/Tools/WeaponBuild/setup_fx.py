# -*- coding: utf-8 -*-
"""
무기 이펙트 에셋 준비 (에디터 Cmd 칸: py "C:/UnrealProjects/MCPTest/Tools/WeaponBuild/setup_fx.py" 단계...)

단계
  shape : /Game/VaultHeist/FX/M_VHFxShape — 판정 범위를 그리는 도형 재질 하나 (부채꼴·링·원·별 모양 섬광을 파라미터로).
          평면(Plane) 위에 그린다. UV 가운데 = 중심, +U 쪽 = 앞(부채꼴의 가운데 방향).
  data  : DA_VHWeaponCatalog 의 이펙트 칸(도형 재질·나이아가라 4종)을 채운다

플레이(PIE) 중에는 돌리지 않는다.
"""
import sys
import unreal

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
FX_DIR = "/Game/VaultHeist/FX"
SHAPE = FX_DIR + "/M_VHFxShape"

# 부채꼴(반각 HalfAngle, 보이는 각 범위 SweepMin~SweepMax) · 테두리 띠(EdgeWidth) · 안쪽 구멍(Inner) · 가운데 밝은 원(Glow) · 별 모양(Spikes)
CODE = r"""
float2 p = UV * 2.0 - 1.0;
float r = length(p);
float a = degrees(atan2(p.y, p.x));
float fan = 1.0 - smoothstep(HalfAngle - 1.5, HalfAngle + 0.5, abs(a));
float sweep = step(SweepMin, a) * step(a, SweepMax);
float ring = step(Inner, r) * (1.0 - smoothstep(0.985, 1.0, r));
float edge = smoothstep(1.0 - EdgeWidth - 0.015, 1.0 - EdgeWidth, r);
float body = FillOpacity + edge * EdgeOpacity;
if (Glow > 0.0) { body = pow(saturate(1.0 - r), Glow); }
if (Spikes > 0.0) { float s = pow(abs(cos(radians(a) * Spikes * 0.5)), 6.0); body *= saturate(1.15 - r / (0.22 + 0.78 * s)); }
return saturate(fan * sweep * ring * body);
"""

SCALARS = [("HalfAngle", 180.0), ("SweepMin", -180.0), ("SweepMax", 180.0), ("FillOpacity", 0.15), ("EdgeOpacity", 1.0),
           ("EdgeWidth", 0.06), ("Inner", 0.0), ("Glow", 0.0), ("Spikes", 0.0)]


def in_pie():
    try:
        return unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    except Exception:
        return False


def do_shape():
    if not EAL.does_directory_exist(FX_DIR):
        EAL.make_directory(FX_DIR)
    if EAL.does_asset_exist(SHAPE):
        mat = EAL.load_asset(SHAPE)
        MEL.delete_all_material_expressions(mat)
    else:
        mat = TOOLS.create_asset("M_VHFxShape", FX_DIR, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)

    custom = MEL.create_material_expression(mat, unreal.MaterialExpressionCustom, -300, 200)
    custom.set_editor_property("code", CODE)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    custom.set_editor_property("description", "VHFxShape")
    names = ["UV"] + [n for n, _ in SCALARS]
    inputs = []
    for n in names:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        inputs.append(ci)
    custom.set_editor_property("inputs", inputs)

    uv = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -700, 0)
    MEL.connect_material_expressions(uv, "", custom, "UV")
    y = 60
    for n, v in SCALARS:
        e = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -700, y)
        e.set_editor_property("parameter_name", n)
        e.set_editor_property("default_value", v)
        MEL.connect_material_expressions(e, "", custom, n)
        y += 60

    color = MEL.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -300, -200)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    intensity = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -300, -60)
    intensity.set_editor_property("parameter_name", "Intensity")
    intensity.set_editor_property("default_value", 4.0)
    emis = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, 0, -150)
    MEL.connect_material_expressions(color, "", emis, "A")
    MEL.connect_material_expressions(intensity, "", emis, "B")
    MEL.connect_material_property(emis, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    opacity = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -300, 900)
    opacity.set_editor_property("parameter_name", "Opacity")
    opacity.set_editor_property("default_value", 1.0)
    op = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, 0, 300)
    MEL.connect_material_expressions(custom, "", op, "A")
    MEL.connect_material_expressions(opacity, "", op, "B")
    MEL.connect_material_property(op, "", unreal.MaterialProperty.MP_OPACITY)

    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat, False)
    unreal.log("[WeaponBuild] 도형 재질 준비: %s" % SHAPE)


def do_data():
    catalog = EAL.load_asset("/Game/VaultHeist/Weapons/Data/DA_VHWeaponCatalog")
    catalog.set_editor_property("fx_shape_material", EAL.load_asset(SHAPE))
    # 화염방사기는 도형 방식(2026-11-17: NS_VH_Flame 은 Fountain 템플릿의 뿜는 방향이 캐릭터 회전을 따르지 않아 보류) → 칸을 비운다
    # (2026-11-19) 화염 나이아가라는 setup_fx.py ranged 에서 켠다
    for prop, name in (("explosion_niagara", "NS_VH_Explosion"), ("smoke_niagara", "NS_VH_Smoke"), ("hit_spark_niagara", "NS_VH_HitSpark")):
        path = "%s/%s" % (FX_DIR, name)
        catalog.set_editor_property(prop, EAL.load_asset(path) if EAL.does_asset_exist(path) else None)
    EAL.save_loaded_asset(catalog, False)
    unreal.log("[WeaponBuild] 이펙트 칸 연결 완료")


SCORCH = FX_DIR + "/M_VHScorchDecal"


def do_decal():
    """빔 소드 그을음 자국: 바닥 데칼 (가운데 진하고 바깥으로 옅어지는 얼룩, 데칼 수명에 맞춰 사라짐)"""
    if EAL.does_asset_exist(SCORCH):
        unreal.log("[WeaponBuild] 데칼 재질 있음: " + SCORCH)
        return
    m = TOOLS.create_asset("M_VHScorchDecal", FX_DIR, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)   # 데칼은 반투명이어야 해서 먼저 (불투명인 채 데칼로 바꾸면 컴파일 오류)
    m.set_editor_property("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)
    uv = MEL.create_material_expression(m, unreal.MaterialExpressionTextureCoordinate, -700, 0)
    code = MEL.create_material_expression(m, unreal.MaterialExpressionCustom, -450, 0)
    code.set_editor_property("code", "float2 d = UV - 0.5; float r = length(d) * 2.0;\n"
                                     "float a = atan2(d.y, d.x);\n"
                                     "float edge = 0.75 + 0.12 * sin(a * 5.0) + 0.08 * sin(a * 11.0 + 1.3);\n"
                                     "return saturate((edge - r) / 0.35) * 0.85;")
    code.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    inp = unreal.CustomInput()
    inp.set_editor_property("input_name", "UV")
    code.set_editor_property("inputs", [inp])
    MEL.connect_material_expressions(uv, "", code, "UV")
    life = MEL.create_material_expression(m, unreal.MaterialExpressionDecalLifetimeOpacity, -450, 200)
    mul = MEL.create_material_expression(m, unreal.MaterialExpressionMultiply, -200, 100)
    MEL.connect_material_expressions(code, "", mul, "A")
    MEL.connect_material_expressions(life, "", mul, "B")
    col = MEL.create_material_expression(m, unreal.MaterialExpressionConstant3Vector, -200, -150)
    col.set_editor_property("constant", unreal.LinearColor(0.02, 0.015, 0.012, 1.0))
    MEL.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(mul, "", unreal.MaterialProperty.MP_OPACITY)
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m, False)
    unreal.log("[WeaponBuild] 데칼 재질 만듦: " + SCORCH)


def fx_set(**kw):
    s = unreal.VHWeaponFxSet()
    for k, v in kw.items():
        if isinstance(v, str):
            v = EAL.load_asset(v) if EAL.does_asset_exist(v) else None
        s.set_editor_property(k, v)
    return s


LC = unreal.LinearColor
TRAIL = FX_DIR + "/NS_VH_Trail"


def do_melee():
    """근접 3종 이펙트 묶음 (2026-11-19): 삼단봉 짧고 흰 궤적 / 도검 은빛 + 빨강 테두리 / 빔 소드 플라스마 + 번짐 + 그을음·섬광"""
    cat = EAL.load_asset("/Game/VaultHeist/Weapons/Data/DA_VHWeaponCatalog")
    fx = cat.get_editor_property("weapon_fx")
    W = unreal.VHWeaponId
    fx[W.BATON] = fx_set(trail=TRAIL, trail_color=LC(0.95, 0.95, 1.0, 1.0), trail_width=14.0, trail_life=0.1,
                         impact_color=LC(1.0, 0.9, 0.4, 1.0))
    fx[W.SWORD] = fx_set(trail=TRAIL, extra=TRAIL, trail_color=LC(0.8, 0.86, 1.0, 1.0), trail_width=16.0, trail_life=0.16,
                         impact_color=LC(1.0, 0.85, 0.45, 1.0))
    fx[W.BEAM_SWORD] = fx_set(trail=TRAIL, extra=TRAIL, trail_color=LC(1.0, 0.5, 0.6, 1.0), trail_width=20.0, trail_life=0.2,
                              impact_color=LC(1.0, 0.4, 0.3, 1.0), impact_decal=SCORCH, decal_size=45.0, decal_life=2.5,
                              flash_color=LC(1.0, 0.35, 0.3, 1.0), flash_intensity=8000.0, flash_radius=320.0, flash_seconds=0.12)
    cat.set_editor_property("weapon_fx", fx)
    EAL.save_loaded_asset(cat, False)
    unreal.log("[WeaponBuild] 근접 이펙트 묶음 3종 (궤적 %s)" % ("있음" if EAL.does_asset_exist(TRAIL) else "없음!"))


def do_ranged():
    """원거리 4종 (2026-11-19): 총구 섬광 조명(권총·테이저·레일건), 화염 나이아가라 다시 켬(뿜는 방향 User.Dir 를 코드가 넣게 고침)"""
    cat = EAL.load_asset("/Game/VaultHeist/Weapons/Data/DA_VHWeaponCatalog")
    fx = cat.get_editor_property("weapon_fx")
    W = unreal.VHWeaponId
    fx[W.PISTOL] = fx_set(flash_color=LC(1.0, 0.75, 0.4, 1.0), flash_intensity=9000.0, flash_radius=350.0, flash_seconds=0.06)
    fx[W.TASER] = fx_set(flash_color=LC(0.45, 0.75, 1.0, 1.0), flash_intensity=5000.0, flash_radius=250.0, flash_seconds=0.12)
    fx[W.RAILGUN] = fx_set(flash_color=LC(0.55, 0.85, 1.0, 1.0), flash_intensity=20000.0, flash_radius=600.0, flash_seconds=0.2)
    cat.set_editor_property("weapon_fx", fx)
    flame = FX_DIR + "/NS_VH_Flame"
    cat.set_editor_property("flame_niagara", EAL.load_asset(flame) if EAL.does_asset_exist(flame) else None)
    EAL.save_loaded_asset(cat, False)
    unreal.log("[WeaponBuild] 원거리 이펙트 묶음 (권총·테이저·레일건 섬광), 화염 나이아가라 %s" % ("켬" if EAL.does_asset_exist(flame) else "없음"))


if in_pie():
    unreal.log_warning("[WeaponBuild] 플레이 중입니다. 플레이를 멈춘 뒤 다시 실행하세요.")
else:
    for step in sys.argv[1:]:
        {"shape": do_shape, "data": do_data, "decal": do_decal, "melee": do_melee, "ranged": do_ranged}.get(step, lambda: unreal.log_warning("[WeaponBuild] 모르는 단계: " + step))()
