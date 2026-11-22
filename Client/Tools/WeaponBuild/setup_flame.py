# -*- coding: utf-8 -*-
"""
화염방사기 불꽃 재질 (2026-11-20, 에디터 Cmd 칸: py "C:/UnrealProjects/MCPTest/Tools/WeaponBuild/setup_flame.py")

나이아가라 MCP 로는 색 커브를 넣을 수 없어서, 불꽃 색 변화를 재질이 입자 나이(Particle Relative Time 0→1)로 계산한다.
  M_VHFlameCore  : 가산(Additive). 흰·노랑 심지 → 주황 → 빨강. 밝은 바닥에서도 빛나 보인다. 불씨(Embers)도 이것 (Sharp 값으로 작고 또렷하게)
  M_VHFlameBody  : 반투명. 주황 → 빨강 → 검붉은 그을음으로 어두워진다 (밝은 바닥에서 불꽃 윤곽이 보이게 바닥을 덮는다)
  M_VHFlameSmoke : 반투명. 처음엔 안 보이다가 나이 0.35 부터 옅은 회색 연기 → 사라짐 (불꽃 끝에서 연기로)
  M_VHHeatHaze   : 굴절(2D Offset). 뒤 화면을 조금 일렁이게 (열기). 프레임이 떨어지면 나이아가라에서 Haze 이미터를 끈다
입자 색(Particle Color)의 RGB = 밝기 배율(입력 C), 알파 = 불투명 배율(입력 A, 노드의 기본 출력은 RGB 뿐이라 따로 연결). 입자마다 Particle Random 으로 모양을 조금씩 다르게.
플레이(PIE) 중에는 돌리지 않는다. 나이아가라 쪽은 setup_niagara.py flame (에디터 밖에서).
"""
import unreal

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
FX_DIR = "/Game/VaultHeist/FX"

# 공통: 부드러운 둥근 덩어리 + 살짝 일그러짐(입자마다 다름). 각도 무늬를 세게 하면 별 모양이 된다 (2026-11-20 화면 확인)
SHAPE = r"""
float2 p = UV * 2.0 - 1.0;
float r = length(p);
float a = atan2(p.y, p.x);
float wob = 0.86 + 0.14 * sin(a * 2.0 + R * 31.0 + T * 5.0) * cos(a * 3.0 - R * 17.0);
float m = saturate(1.0 - r / max(wob, 0.05));
m = m * m * (3.0 - 2.0 * m);
"""

CORE = SHAPE + r"""
m = pow(m, Sharp);
float3 c0 = float3(1.0, 0.92, 0.72) * 7.0;
float3 c1 = float3(1.0, 0.42, 0.05) * 4.0;
float3 c2 = float3(0.9, 0.07, 0.01) * 1.6;
float3 col = T < 0.22 ? lerp(c0, c1, T / 0.22) : lerp(c1, c2, saturate((T - 0.22) / 0.5));
float fade = saturate((1.0 - T) / 0.4) * saturate(0.35 + T * 12.0);
return col * m * fade * C * A;
"""

BODY_RGB = r"""
float3 c0 = float3(1.0, 0.55, 0.12) * 2.2;
float3 c1 = float3(0.95, 0.16, 0.02) * 1.2;
float3 c2 = float3(0.08, 0.03, 0.02);
float3 col = T < 0.35 ? lerp(c0, c1, T / 0.35) : lerp(c1, c2, saturate((T - 0.35) / 0.55));
return col * C;
"""
BODY_OP = SHAPE + r"""
m = pow(m, 1.4);
return saturate(m * 0.85 * saturate((1.0 - T) / 0.35) * A);
"""

SMOKE_RGB = r"""
return float3(0.07, 0.065, 0.06) * C;
"""
SMOKE_OP = SHAPE + r"""
m = pow(m, 1.8);
float vis = smoothstep(0.3, 0.5, T) * saturate((1.0 - T) / 0.45);
return saturate(m * 0.42 * vis * A);
"""

HAZE = SHAPE + r"""
m = pow(m, 1.5);
float s1 = sin(UV.x * 21.0 + Time * 13.0 + R * 40.0);
float s2 = cos(UV.y * 17.0 - Time * 11.0 + R * 23.0);
float fade = saturate(T * 6.0) * saturate((1.0 - T) / 0.5);
return float2(s1, s2) * (Strength * m * fade * A);
"""


def in_pie():
    try:
        return unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    except Exception:
        return False


def new_material(name):
    path = FX_DIR + "/" + name
    if EAL.does_asset_exist(path):
        mat = EAL.load_asset(path)
        MEL.delete_all_material_expressions(mat)
    else:
        mat = TOOLS.create_asset(name, FX_DIR, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("used_with_niagara_sprites", True)
    return mat


def custom(mat, code, out, inputs, x, y, desc):
    c = MEL.create_material_expression(mat, unreal.MaterialExpressionCustom, x, y)
    c.set_editor_property("code", code)
    c.set_editor_property("output_type", out)
    c.set_editor_property("description", desc)
    lst = []
    for n in inputs:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        lst.append(ci)
    c.set_editor_property("inputs", lst)
    return c


def particle_inputs(mat):
    return {
        "UV": MEL.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -900, -200),
        "T": MEL.create_material_expression(mat, unreal.MaterialExpressionParticleRelativeTime, -900, -100),
        "C": MEL.create_material_expression(mat, unreal.MaterialExpressionParticleColor, -900, 0),
        "R": MEL.create_material_expression(mat, unreal.MaterialExpressionParticleRandom, -900, 100),
    }


def wire(src, dst, names):
    for n in names:
        if n == "A":
            MEL.connect_material_expressions(src["C"], "A", dst, "A")   # 입자 색의 알파 (기본 출력은 RGB 뿐)
        else:
            MEL.connect_material_expressions(src[n], "", dst, n)


def scalar(mat, name, value, y):
    e = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -900, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", value)
    return e


def finish(mat):
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat, False)
    unreal.log("[Flame] 재질 저장: %s" % mat.get_path_name())


def make_core(name, sharp):
    mat = new_material(name)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    src = particle_inputs(mat)
    c = custom(mat, CORE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["UV", "T", "C", "A", "R", "Sharp"], -400, 0, "VHFlameCore")
    wire(src, c, ["UV", "T", "C", "A", "R"])
    MEL.connect_material_expressions(scalar(mat, "Sharp", sharp, 220), "", c, "Sharp")
    MEL.connect_material_property(c, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(mat)


def make_translucent(name, rgb_code, op_code):
    mat = new_material(name)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    src = particle_inputs(mat)
    rgb = custom(mat, rgb_code, unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["T", "C"], -400, -150, name + "Color")
    wire(src, rgb, ["T", "C"])
    op = custom(mat, op_code, unreal.CustomMaterialOutputType.CMOT_FLOAT1, ["UV", "T", "A", "R"], -400, 150, name + "Opacity")
    wire(src, op, ["UV", "T", "A", "R"])
    MEL.connect_material_property(rgb, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(op, "", unreal.MaterialProperty.MP_OPACITY)
    finish(mat)


def make_haze():
    mat = new_material("M_VHHeatHaze")
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    # 굴절은 조명 재질에서만 컴파일된다 (Unlit 이면 실패, 2026-11-20). 불투명도 0 이라 조명 값은 보이지 않는다
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    # 굴절 방식 = 2D Offset (불투명도와 관계없이 화면을 밀어낸다)
    for prop, enum_name in (("refraction_method", "RefractionMode"), ("refraction_mode", "RefractionMode")):
        try:
            mat.set_editor_property(prop, getattr(getattr(unreal, enum_name), "RM_2D_OFFSET"))
            unreal.log("[Flame] 굴절 방식: %s = RM_2D_OFFSET" % prop)
            break
        except Exception as e:
            unreal.log_warning("[Flame] %s 설정 실패: %s" % (prop, e))
    src = particle_inputs(mat)
    t = MEL.create_material_expression(mat, unreal.MaterialExpressionTime, -900, 200)
    h = custom(mat, HAZE, unreal.CustomMaterialOutputType.CMOT_FLOAT2, ["UV", "T", "A", "R", "Time", "Strength"], -400, 0, "VHHeatHaze")
    wire(src, h, ["UV", "T", "A", "R"])
    MEL.connect_material_expressions(t, "", h, "Time")
    MEL.connect_material_expressions(scalar(mat, "Strength", 0.018, 300), "", h, "Strength")
    MEL.connect_material_property(h, "", unreal.MaterialProperty.MP_REFRACTION)
    zero = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 300)
    zero.set_editor_property("r", 0.0)
    MEL.connect_material_property(zero, "", unreal.MaterialProperty.MP_OPACITY)
    MEL.connect_material_property(zero, "", unreal.MaterialProperty.MP_BASE_COLOR)
    finish(mat)


import sys
ONLY = sys.argv[1:]
if in_pie():
    unreal.log_error("[Flame] 플레이 중에는 돌리지 않습니다")
elif ONLY == ["haze"]:
    make_haze()
else:
    make_core("M_VHFlameCore", 1.6)
    make_core("M_VHFlameEmber", 3.0)
    make_translucent("M_VHFlameBody", BODY_RGB, BODY_OP)
    make_translucent("M_VHFlameSmoke", SMOKE_RGB, SMOKE_OP)
    make_haze()
