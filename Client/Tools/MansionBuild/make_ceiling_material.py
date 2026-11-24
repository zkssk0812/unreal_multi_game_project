# -*- coding: utf-8 -*-
"""
천장 가림(방 단위 시야)용 재질을 만든다 (에디터 안에서 실행, 여러 번 실행해도 같은 결과. PIE 중에는 실행하지 않는다).
    py "C:/UnrealProjects/MCPTest/Tools/MansionBuild/make_ceiling_material.py"            (전부)
    py "C:/UnrealProjects/MCPTest/Tools/MansionBuild/make_ceiling_material.py" walls lock (일부만: ceiling silhouette walls lock)

M_VHCeiling (천장)
  - 언릿(조명 영향 없음) + 마스크. 색 = 파라미터 Color (C++ 이 DA_VHGameplayConfig.CeilingColor 로 덮어쓴다)
  - Opacity(0~1): 0.2초 사라지기/덮이기. 4x4 디더(화면 픽셀 격자)로 덮인 비율만큼 픽셀을 남긴다 → TSR 이 부드럽게 섞는다
  - (2026-11-21 (4)) ZoneCut: 내 구역 앞 가림 지우기 (아래)
  - 문간 가림막(문 자리의 세로 천장 조각)도 이 재질을 쓴다 (C++ UVHCeilingSubsystem::AddDoorCurtains)

M_VHSilhouette (후처리, 톤매핑 뒤)
  - 내 캐릭터(커스텀 뎁스 켬, 스텐실 0)의 픽셀 중 "커스텀 뎁스가 화면 뎁스보다 Bias 이상 뒤" = 천장·벽에 가려진 곳만
    Color 로 반투명하게 칠하고(FillOpacity) 가장자리는 진하게(EdgeOpacity). 스텐실 1~3 은 상호작용 테두리(M_VHOutline) 몫

ZoneCut (2026-11-21 (4), M_VHCeiling 과 벽 재질 M_VHWallFadeTex 에 같은 계산)
  - 카메라 → 이 픽셀 선을 CutPlaneZ 높이(기본 0 = 바닥)까지 늘린 점의 구역 번호를 구역 지도(ZoneTex, C++ 이 방 영역으로 만든 텍스처)에서 읽는다.
  - 그 구역이 내 구역(CutZoneA)이면 CutAlphaA 만큼(디더) 이 픽셀을 지운다. 직전 구역(CutZoneB)은 CutAlphaB 로 서서히 되돌린다(구역 전환 0.2초).
  - 자기 자신은 지우지 않는다: SelfZone > 0 = 그 구역 천장(자기 페이드로만), -1 = 벽·기둥(픽셀 자리의 좁은 구역(G 채널)이 내 구역이면 그대로 = 방 안 기둥), 0 = 문.
  - 구역 지도 채널: R = 넓은 구역(벽 면까지, 선을 늘린 점), G = 좁은 구역(서버 조각 + 메운 기둥 자리, 벽·기둥 자기 자리).
  - 파라미터 기본값은 "지우지 않음"(CutZoneA 0)이라 옛 레벨·에디터 화면은 그대로다.
  - (2026-11-24) 엿보기: PeekOn = 1 이면 바닥 점이 엿보기 마스크(PeekTex, 열린 문 너머 부채꼴, C++ 이 그리는 렌더 타깃) 안일 때 지운다.
    벽·기둥(SelfZone -1)은 자기 자리도 마스크 안이면 남긴다(문 너머 방 안 기둥). 자기 구역 천장도 지운다(문 너머 구역 천장에 부채꼴 구멍).

MI_VHDoorLock: 잠긴 문 표시(빨강), 부모 M_VHWallFadeTex
M_VHDoorMark (2026-11-21 (5)): 문 위치 표시(바닥 문턱 띠·화살표·잠긴 문 X·천장 위 밝은 틈), 반투명 언릿. 단계 이름 doormark
결과는 출력 로그의 [CeilingMat] 줄.
"""
import sys

import unreal

PATH = "/Game/VaultHeist/Materials"
CEILING = "M_VHCeiling"
SILHOUETTE = "M_VHSilhouette"
WALL = "/Game/VaultHeist/Materials/M_VHWallFadeTex"
LOCK_MI = "/Game/VaultHeist/Materials/MansionShell/MI_VHDoorLock"

CEILING_CODE = r"""
float2 px = floor(Parameters.SvPosition.xy);
uint2 p = (uint2)px;
uint a = ((p.x ^ p.y) & 1u) * 2u + (p.y & 1u);
uint b = (((p.x >> 1) ^ (p.y >> 1)) & 1u) * 2u + ((p.y >> 1) & 1u);
float thr = ((float)(a * 4u + b) + 0.5) / 16.0;
return (Op > thr) ? 1.0 : 0.0;
"""

ZONE_CUT_CODE = r"""
if (A < 0.5 && B < 0.5 && PK < 0.5) return 1.0;
float3 d = P.xyz - Cam.xyz;
if (d.z > -1.0) return 1.0;
float t = (PZ - Cam.z) / d.z;
if (t <= 0.0) return 1.0;
float2 q = Cam.xy + d.xy * t;
if (PK > 0.5) {
  float2 u = (q - Map.xy) * Map.z / Size.xy;
  if (u.x > 0.0 && u.y > 0.0 && u.x < 1.0 && u.y < 1.0 && Texture2DSampleLevel(Peek, PeekSampler, u, 0).r > 0.5) {
    bool keep = false;
    if (Self < -0.5) {
      float2 u2 = (P.xy - Map.xy) * Map.z / Size.xy;
      keep = u2.x > 0.0 && u2.y > 0.0 && u2.x < 1.0 && u2.y < 1.0 && Texture2DSampleLevel(Peek, PeekSampler, u2, 0).r > 0.5;
    }
    if (!keep) return 0.0;
  }
}
if (A < 0.5 && B < 0.5) return 1.0;
float2 px = floor(Parameters.SvPosition.xy);
uint2 p = (uint2)px;
uint a = ((p.x ^ p.y) & 1u) * 2u + (p.y & 1u);
uint b = (((p.x >> 1) ^ (p.y >> 1)) & 1u) * 2u + ((p.y >> 1) & 1u);
float thr = ((float)(a * 4u + b) + 0.5) / 16.0;
float2 c = floor((q - Map.xy) * Map.z);
if (c.x < 0.0 || c.y < 0.0 || c.x >= Size.x || c.y >= Size.y) return 1.0;
float id = floor(Texture2DSampleLevel(Tex, TexSampler, (c + 0.5) / Size.xy, 0).r * 255.0 + 0.5);
if (id < 0.5) return 1.0;
float own = Self;
if (Self < -0.5) {
  own = 0.0;
  float2 c2 = floor((P.xy - Map.xy) * Map.z);
  if (c2.x >= 0.0 && c2.y >= 0.0 && c2.x < Size.x && c2.y < Size.y)
    own = floor(Texture2DSampleLevel(Tex, TexSampler, (c2 + 0.5) / Size.xy, 0).g * 255.0 + 0.5);
}
if (abs(id - own) < 0.5) return 1.0;
float cut = 0.0;
if (abs(id - A) < 0.5) cut = AA;
else if (abs(id - B) < 0.5) cut = BA;
return (cut > thr) ? 0.0 : 1.0;
"""
ZONE_CUT_INPUTS = ["P", "Cam", "Tex", "Map", "Size", "A", "AA", "B", "BA", "PZ", "Self", "Peek", "PK"]

SILHOUETTE_CODE = r"""
float2 uv = GetDefaultSceneTextureUV(Parameters, 13);
float cd = CD.r;
float sd = SD.r;
if (!(cd < 1000000.0) || ST.r > 0.5 || cd <= sd + Bias) return SceneColor.rgb;
float2 o = InvSize * Thickness;
float e = 0.0;
float n0 = SceneTextureLookup(uv + float2(o.x, 0), 13, false).r;
float n1 = SceneTextureLookup(uv - float2(o.x, 0), 13, false).r;
float n2 = SceneTextureLookup(uv + float2(0, o.y), 13, false).r;
float n3 = SceneTextureLookup(uv - float2(0, o.y), 13, false).r;
if (!(n0 < 1000000.0) || !(n1 < 1000000.0) || !(n2 < 1000000.0) || !(n3 < 1000000.0)) e = 1.0;
float s0 = SceneTextureLookup(uv + float2(o.x, 0), 25, false).r;
float s1 = SceneTextureLookup(uv - float2(o.x, 0), 25, false).r;
float s2 = SceneTextureLookup(uv + float2(0, o.y), 25, false).r;
float s3 = SceneTextureLookup(uv - float2(0, o.y), 25, false).r;
if (max(max(s0, s1), max(s2, s3)) > 0.5) e = 1.0;
return lerp(SceneColor.rgb, Color.rgb, e > 0.5 ? EdgeOpacity : FillOpacity);
"""


def log(msg):
    unreal.log(f"[CeilingMat] {msg}")


def fresh_material(name):
    eal = unreal.EditorAssetLibrary
    full = f"{PATH}/{name}"
    if eal.does_asset_exist(full):
        m = unreal.load_asset(full)
        unreal.MaterialEditingLibrary.delete_all_material_expressions(m)
        log(f"{name}: 기존 재질의 노드를 지우고 다시 만듦")
    else:
        m = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, PATH, unreal.Material, unreal.MaterialFactoryNew())
        log(f"{name}: 새 재질 만듦")
    return m


def custom_node(m, code, names, x, y, out_type, desc):
    mel = unreal.MaterialEditingLibrary
    c = mel.create_material_expression(m, unreal.MaterialExpressionCustom, x, y)
    c.set_editor_property("code", code)
    c.set_editor_property("output_type", out_type)
    c.set_editor_property("description", desc)
    inputs = []
    for nm in names:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", nm)
        inputs.append(ci)
    c.set_editor_property("inputs", inputs)
    return c


def scalar(m, name, value, x, y):
    s = unreal.MaterialEditingLibrary.create_material_expression(m, unreal.MaterialExpressionScalarParameter, x, y)
    s.set_editor_property("parameter_name", name)
    s.set_editor_property("default_value", value)
    return s


def vector(m, name, value, x, y):
    v = unreal.MaterialEditingLibrary.create_material_expression(m, unreal.MaterialExpressionVectorParameter, x, y)
    v.set_editor_property("parameter_name", name)
    v.set_editor_property("default_value", value)
    return v


def zone_cut_node(m, x, y, self_default):
    """ZoneCut 커스텀 노드와 입력 파라미터들을 만들고 커스텀 노드를 돌려준다"""
    mel = unreal.MaterialEditingLibrary
    c = custom_node(m, ZONE_CUT_CODE, ZONE_CUT_INPUTS, x, y, unreal.CustomMaterialOutputType.CMOT_FLOAT1, "ZoneCut")
    wp = mel.create_material_expression(m, unreal.MaterialExpressionWorldPosition, x - 500, y)
    cam = mel.create_material_expression(m, unreal.MaterialExpressionCameraPositionWS, x - 500, y + 80)
    tex = mel.create_material_expression(m, unreal.MaterialExpressionTextureObjectParameter, x - 500, y + 160)
    tex.set_editor_property("parameter_name", "ZoneTex")
    tex.set_editor_property("texture", unreal.load_asset("/Engine/EngineResources/Black"))
    peek = mel.create_material_expression(m, unreal.MaterialExpressionTextureObjectParameter, x - 500, y + 600)
    peek.set_editor_property("parameter_name", "PeekTex")
    peek.set_editor_property("texture", unreal.load_asset("/Engine/EngineResources/Black"))
    zmap = vector(m, "ZoneMap", unreal.LinearColor(0, 0, 0.1, 0), x - 500, y + 300)
    zsize = vector(m, "ZoneSize", unreal.LinearColor(1, 1, 0, 0), x - 500, y + 450)
    params = {
        "P": wp, "Cam": cam, "Tex": tex, "Map": zmap, "Size": zsize,
        "A": scalar(m, "CutZoneA", 0.0, x - 800, y),
        "AA": scalar(m, "CutAlphaA", 0.0, x - 800, y + 80),
        "B": scalar(m, "CutZoneB", 0.0, x - 800, y + 160),
        "BA": scalar(m, "CutAlphaB", 0.0, x - 800, y + 240),
        "PZ": scalar(m, "CutPlaneZ", 0.0, x - 800, y + 320),
        "Self": scalar(m, "SelfZone", self_default, x - 800, y + 400),
        "Peek": peek,
        "PK": scalar(m, "PeekOn", 0.0, x - 800, y + 480),
    }
    for nm, node in params.items():
        mel.connect_material_expressions(node, "", c, nm)
    return c


def build_ceiling():
    mel = unreal.MaterialEditingLibrary
    m = fresh_material(CEILING)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    m.set_editor_property("opacity_mask_clip_value", 0.5)
    m.set_editor_property("two_sided", False)
    m.set_editor_property("used_with_instanced_static_meshes", True)

    color = vector(m, "Color", unreal.LinearColor(0.30, 0.31, 0.33, 1.0), -600, -200)
    mel.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    opacity = scalar(m, "Opacity", 1.0, -900, 200)
    dither = custom_node(m, CEILING_CODE, ["Op"], -400, 200, unreal.CustomMaterialOutputType.CMOT_FLOAT1, "CeilingDither")
    mel.connect_material_expressions(opacity, "", dither, "Op")
    cut = zone_cut_node(m, -400, 450, 0.0)
    mul = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -150, 300)
    mel.connect_material_expressions(dither, "", mul, "A")
    mel.connect_material_expressions(cut, "", mul, "B")
    mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_OPACITY_MASK)
    mel.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    log(f"{CEILING} 저장 (언릿·마스크·디더 × ZoneCut)")


def build_silhouette():
    mel = unreal.MaterialEditingLibrary
    m = fresh_material(SILHOUETTE)
    m.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    m.set_editor_property("blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)

    def tex(tid, x, y):
        t = mel.create_material_expression(m, unreal.MaterialExpressionSceneTexture, x, y)
        t.set_editor_property("scene_texture_id", tid)
        return t
    cd = tex(unreal.SceneTextureId.PPI_CUSTOM_DEPTH, -1000, -200)
    sd = tex(unreal.SceneTextureId.PPI_SCENE_DEPTH, -1000, -50)
    st = tex(unreal.SceneTextureId.PPI_CUSTOM_STENCIL, -1000, 100)
    sc = tex(unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0, -1000, 250)
    color = vector(m, "Color", unreal.LinearColor(0.55, 0.85, 1.0, 1.0), -1000, 400)
    fill = scalar(m, "FillOpacity", 0.45, -1000, 550)
    edge = scalar(m, "EdgeOpacity", 0.95, -1000, 650)
    thick = scalar(m, "Thickness", 2.0, -1000, 750)
    bias = scalar(m, "Bias", 15.0, -1000, 850)

    custom = custom_node(m, SILHOUETTE_CODE, ["CD", "SD", "ST", "SceneColor", "InvSize", "Color", "FillOpacity", "EdgeOpacity", "Thickness", "Bias"],
                         -400, 200, unreal.CustomMaterialOutputType.CMOT_FLOAT3, "MySilhouette")
    mel.connect_material_expressions(cd, "Color", custom, "CD")
    mel.connect_material_expressions(sd, "Color", custom, "SD")
    mel.connect_material_expressions(st, "Color", custom, "ST")
    mel.connect_material_expressions(sc, "Color", custom, "SceneColor")
    mel.connect_material_expressions(cd, "InvSize", custom, "InvSize")
    mel.connect_material_expressions(color, "", custom, "Color")
    mel.connect_material_expressions(fill, "", custom, "FillOpacity")
    mel.connect_material_expressions(edge, "", custom, "EdgeOpacity")
    mel.connect_material_expressions(thick, "", custom, "Thickness")
    mel.connect_material_expressions(bias, "", custom, "Bias")
    mel.connect_material_property(custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    log(f"{SILHOUETTE} 저장 (후처리·톤매핑 뒤, 가려진 내 캐릭터만)")


def desc_of(node):
    try:
        return str(node.get_editor_property("description"))
    except Exception:
        return ""


def patch_walls():
    """벽 투명 재질(M_VHWallFadeTex)의 OpacityMask = WallFade × ZoneCut 으로 (여러 번 실행해도 ZoneCut 은 하나)"""
    mel = unreal.MaterialEditingLibrary
    m = unreal.load_asset(WALL)
    cur = mel.get_material_property_input_node(m, unreal.MaterialProperty.MP_OPACITY_MASK)
    wall_fade = None
    if cur is not None and desc_of(cur) == "WallFade":
        wall_fade = cur
    elif cur is not None and isinstance(cur, unreal.MaterialExpressionMultiply):
        # 예전에 붙인 것: Multiply(WallFade, ZoneCut) → ZoneCut 과 그 입력·Multiply 를 지우고 다시
        for inp in mel.get_inputs_for_material_expression(m, cur):
            if inp is None:
                continue
            if desc_of(inp) == "WallFade":
                wall_fade = inp
            elif desc_of(inp) == "ZoneCut":
                for sub in mel.get_inputs_for_material_expression(m, inp):
                    if sub is not None:
                        mel.delete_material_expression(m, sub)
                mel.delete_material_expression(m, inp)
        mel.delete_material_expression(m, cur)
    if wall_fade is None:
        log(f"벽 재질의 WallFade 노드를 못 찾음 ({cur.get_name() if cur else None}) → 바꾸지 않음")
        return
    cut = zone_cut_node(m, -200, 700, -1.0)
    mul = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, 100, 400)
    mel.connect_material_expressions(wall_fade, "", mul, "A")
    mel.connect_material_expressions(cut, "", mul, "B")
    mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_OPACITY_MASK)
    mel.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    log(f"{WALL}: OpacityMask = WallFade × ZoneCut (SelfZone 기본 -1)")


def make_lock_mi():
    eal = unreal.EditorAssetLibrary
    mel = unreal.MaterialEditingLibrary
    folder, name = LOCK_MI.rsplit("/", 1)
    mi = unreal.load_asset(LOCK_MI) if eal.does_asset_exist(LOCK_MI) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, unreal.load_asset(WALL))
    mel.set_material_instance_vector_parameter_value(mi, "WallColor", unreal.LinearColor(0.85, 0.04, 0.03, 1.0))
    mel.update_material_instance(mi)
    eal.save_loaded_asset(mi)
    log(f"{LOCK_MI} 저장 (잠긴 문 표시, 빨강)")


# 문 위치 표시 (2026-11-21 (5)): 반투명 언릿, 엔진 Plane(100x100) 위에 모양을 그린다.
#   Shape 0 = 문턱 띠(V 방향 가장자리가 부드러움), 1 = 화살표(>> 모양, +U 쪽을 가리킴), 2 = X(잠긴 문), 3 = 천장 위 밝은 틈
#   Color·Intensity·Opacity 는 C++ 이 DA_VHGameplayConfig(Camera|Ceiling 의 DoorMark*)로 덮어쓴다
DOOR_MARK = "M_VHDoorMark"
DOOR_MARK_CODE = r"""
float2 p = UV - 0.5;
float aa = 0.025;
if (Shape < 0.5) {
  float e = 0.5 - abs(p.y);
  float s = 0.5 - abs(p.x);
  return smoothstep(0.0, 0.18, e) * (0.7 + 0.3 * smoothstep(0.25, 0.4, e)) * smoothstep(0.0, 0.04, s);
}
if (Shape < 1.5) {
  float m = 0.0;
  [unroll] for (int i = 0; i < 2; i++) {
    float ox = 0.02 + 0.24 * i;
    float d = abs(p.x + 0.85 * abs(p.y) - ox);
    float band = (1.0 - smoothstep(0.085, 0.085 + aa, d)) * (1.0 - smoothstep(0.30, 0.30 + aa, abs(p.y)));
    m = max(m, band);
  }
  return m;
}
if (Shape < 2.5) {
  float d = min(abs(p.x - p.y), abs(p.x + p.y));
  return (1.0 - smoothstep(0.07, 0.07 + aa, d)) * (1.0 - smoothstep(0.30, 0.30 + aa, max(abs(p.x), abs(p.y))));
}
float e = 0.5 - abs(p.y);
float s = 0.5 - abs(p.x);
return saturate(0.45 * smoothstep(0.0, 0.5, e) + 0.55 * smoothstep(0.3, 0.38, e)) * smoothstep(0.0, 0.06, s);
"""


def build_door_mark():
    mel = unreal.MaterialEditingLibrary
    m = fresh_material(DOOR_MARK)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("two_sided", False)
    color = vector(m, "Color", unreal.LinearColor(0.35, 0.85, 1.0, 1.0), -700, -200)
    inten = scalar(m, "Intensity", 2.0, -700, -50)
    mul = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -400, -150)
    mel.connect_material_expressions(color, "", mul, "A")
    mel.connect_material_expressions(inten, "", mul, "B")
    mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    uv = mel.create_material_expression(m, unreal.MaterialExpressionTextureCoordinate, -900, 200)
    shape = scalar(m, "Shape", 0.0, -900, 300)
    custom = custom_node(m, DOOR_MARK_CODE, ["UV", "Shape"], -600, 250, unreal.CustomMaterialOutputType.CMOT_FLOAT1, "DoorMarkShape")
    mel.connect_material_expressions(uv, "", custom, "UV")
    mel.connect_material_expressions(shape, "", custom, "Shape")
    opacity = scalar(m, "Opacity", 0.6, -600, 450)
    mul2 = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -300, 300)
    mel.connect_material_expressions(custom, "", mul2, "A")
    mel.connect_material_expressions(opacity, "", mul2, "B")
    mel.connect_material_property(mul2, "", unreal.MaterialProperty.MP_OPACITY)
    mel.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    log(f"{DOOR_MARK} 저장 (반투명 언릿, 띠·화살표·X·틈)")


def main():
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():
        log("PIE 중이라 실행하지 않음")
        return
    args = [a.lower() for a in sys.argv[1:]] or ["ceiling", "silhouette", "walls", "lock", "doormark"]
    if "doormark" in args:
        build_door_mark()
    if "ceiling" in args:
        build_ceiling()
    if "silhouette" in args:
        build_silhouette()
    if "walls" in args:
        patch_walls()
    if "lock" in args:
        make_lock_mi()


main()
log("끝")
