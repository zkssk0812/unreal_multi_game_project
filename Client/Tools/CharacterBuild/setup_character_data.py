# -*- coding: utf-8 -*-
"""
새 캐릭터를 게임에 연결 (에디터 Cmd 칸: py "C:/UnrealProjects/MCPTest/Tools/CharacterBuild/setup_character_data.py" 단계...)

단계
  ring     : 플레이어 구분 발밑 링 재질 M_VHPlayerRing (안 비치는 색 링, 파라미터 Color)
  gun      : 로봇 경비원 기관총 SM_VHGuardGun (Geometry Script 상자·원통, 무기 재질 재사용, +X = 총구, 원점 = 손잡이)
  socket   : SK_VHThief 의 WeaponSocket_R 값 (손가락 뼈로 계산: +X = 손목→가운뎃손가락, +Z = 새끼→집게, 위치 = 쥔 주먹 가운데)
             (소켓은 먼저 MCP SkeletalMeshTools.add_socket 으로 만들어 둔다)
  data     : DA_VHCharacterVisuals 기본 칸 = 의적(메시·ABP·이동·뼈 이름·든 자세·쥔 손가락·쓰러짐), Guard 칸 = 로봇
  grip     : DA_VHWeaponCatalog 근접 무기 손잡이 보정 (날이 주먹을 관통하는 축으로)
  guess    : 소켓·손가락 축 계산 값만 로그로
  (2026-11-20 여러 캐릭터)
  retargetmode : 캐릭터1 스켈레톤 뼈 이동 모드 Hips=AnimationScaled·나머지=Skeleton (도적2·3이 자기 뼈 길이로 같은 애니메이션)
  socket2  : 도적2·3 메시의 WeaponSocket_R (그 메시 손가락 관절로 계산)
  chars    : DA_VHCharacterVisuals.Characters 2·3 = 기본 칸 복사 + 메시·손 보정(CHAR_FIT). 메시 없으면 비움(도적1로 보임)
  gamedata : DA_VHGameData 캐릭터 목록 = 도적1~3
  portraits: SourceArt/Characters/Portraits/T_VHPortrait_Thief{N}.png → DA_VHGameData Portrait (없으면 비움 = "모델 준비 중")

플레이(PIE) 중에는 돌리지 않는다.
"""
import math
import os
import sys
import unreal

EAL = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
APE = unreal.AnimPoseExtensions
W = unreal.AnimPoseSpaces.WORLD
ROOT = "/Game/VaultHeist/Characters"
THIEF = ROOT + "/Thief/SK_VHThief"
GUARD = ROOT + "/Guard/SK_VHGuard"
DATA = "/Game/VaultHeist/Weapons/Data/DA_VHCharacterVisuals"
CATALOG = "/Game/VaultHeist/Weapons/Data/DA_VHWeaponCatalog"
RING = ROOT + "/Materials/M_VHPlayerRing"
GUN = ROOT + "/Guard/SM_VHGuardGun"
SOCKET = "WeaponSocket_R"
TA = ROOT + "/Thief/Anims/"
GA = ROOT + "/Guard/Anims/"


def log(m):
    unreal.log("[CharBuild] " + m)


def V(x, y, z):
    return unreal.Vector(x, y, z)


def cross(a, b):
    return V(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x)


# ───────────── 링 재질 ─────────────
def do_ring():
    MEL = unreal.MaterialEditingLibrary
    if EAL.does_asset_exist(RING):
        mat = EAL.load_asset(RING)
        MEL.delete_all_material_expressions(mat)
    else:
        mat = TOOLS.create_asset("M_VHPlayerRing", ROOT + "/Materials", unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property("two_sided", True)

    def E(cls, x, y):
        return MEL.create_material_expression(mat, cls, x, y)

    def C(v, x, y):
        e = E(unreal.MaterialExpressionConstant, x, y)
        e.set_editor_property("r", v)
        return e

    uv = E(unreal.MaterialExpressionTextureCoordinate, -1000, 0)
    c2 = E(unreal.MaterialExpressionConstant2Vector, -1000, 120)
    c2.set_editor_property("r", 0.5)
    c2.set_editor_property("g", 0.5)
    dist = E(unreal.MaterialExpressionDistance, -820, 40)
    MEL.connect_material_expressions(uv, "", dist, "A")
    MEL.connect_material_expressions(c2, "", dist, "B")
    # 바깥 테두리 0.5, 안쪽 0.36 → 링 (반지름 기준 72~100%)
    outer = E(unreal.MaterialExpressionSubtract, -640, -40)
    MEL.connect_material_expressions(C(0.5, -820, -60), "", outer, "A")
    MEL.connect_material_expressions(dist, "", outer, "B")
    inner = E(unreal.MaterialExpressionSubtract, -640, 120)
    MEL.connect_material_expressions(dist, "", inner, "A")
    MEL.connect_material_expressions(C(0.36, -820, 160), "", inner, "B")
    mn = E(unreal.MaterialExpressionMin, -480, 40)
    MEL.connect_material_expressions(outer, "", mn, "A")
    MEL.connect_material_expressions(inner, "", mn, "B")
    color = E(unreal.MaterialExpressionVectorParameter, -480, -200)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(1, 0.2, 0.2, 1))
    bright = E(unreal.MaterialExpressionMultiply, -280, -200)
    MEL.connect_material_expressions(color, "", bright, "A")
    MEL.connect_material_expressions(C(1.6, -480, -100), "", bright, "B")
    MP = unreal.MaterialProperty
    MEL.connect_material_property(bright, "", MP.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(mn, "", MP.MP_OPACITY_MASK)
    mat.set_editor_property("opacity_mask_clip_value", 0.0)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat, False)
    log("링 재질 " + RING)


# ───────────── 경비원 기관총 ─────────────
def do_gun():
    GP = unreal.GeometryScript_Primitives
    mesh = unreal.DynamicMesh()
    opts = unreal.GeometryScriptPrimitiveOptions()

    def box(cx, cy, cz, sx, sy, sz, mat_id):
        o = unreal.GeometryScriptPrimitiveOptions()
        o.set_editor_property("material_id", mat_id)
        GP.append_box(mesh, o, unreal.Transform(location=V(cx, cy, cz - sz / 2.0)), sx, sy, sz)

    def cyl(x0, length, cy, cz, r, mat_id, steps=12):
        o = unreal.GeometryScriptPrimitiveOptions()
        o.set_editor_property("material_id", mat_id)
        # 원통은 +Z 로 서 있으므로 Pitch -90 으로 눕혀 +X 로
        t = unreal.Transform(location=V(x0, cy, cz), rotation=unreal.Rotator(roll=0.0, pitch=-90.0, yaw=0.0))
        GP.append_cylinder(mesh, o, t, r, length, steps, 0, True, unreal.GeometryScriptPrimitiveOriginMode.BASE)

    # 원점 = 손잡이 윗부분(쥐는 자리), +X = 총구, +Z = 위. 실제 크기(cm) — 로봇 손에 맞춰 조금 큼직하게
    BLACK, STEEL, GLOW = 0, 1, 2
    box(10, 0, 9, 44, 9, 11, BLACK)        # 몸통(기관부)
    box(-18, 0, 8, 16, 7, 9, BLACK)        # 개머리
    box(2, 0, 0, 6, 5, 12, BLACK)          # 손잡이 (원점 아래로)
    box(16, 0, -1, 8, 4, 14, STEEL)        # 탄창
    cyl(32, 34, 0, 9, 2.0, STEEL)          # 총열
    cyl(30, 10, 0, 9, 3.4, BLACK)          # 총열 덮개
    box(18, 0, 15.5, 26, 2.2, 1.5, GLOW)   # 위쪽 하늘색 띠 (로봇 발광과 같은 색)
    unreal.GeometryScript_Normals.compute_split_normals(mesh, unreal.GeometryScriptSplitNormalsOptions(), unreal.GeometryScriptCalculateNormalsOptions())
    mats = [EAL.load_asset("/Game/VaultHeist/Weapons/Materials/MI_VHW_Black"),
            EAL.load_asset("/Game/VaultHeist/Weapons/Materials/MI_VHW_Steel"),
            EAL.load_asset("/Game/VaultHeist/Weapons/Materials/MI_VHW_Glow_Ranged")]
    asset = EAL.load_asset(GUN) if EAL.does_asset_exist(GUN) else None
    if asset is None:
        no = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
        no.set_editor_property("enable_collision", False)
        asset, outcome = unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(mesh, GUN, no)
    co = unreal.GeometryScriptCopyMeshToAssetOptions()
    co.set_editor_property("enable_recompute_normals", False)
    co.set_editor_property("enable_recompute_tangents", True)
    co.set_editor_property("replace_materials", True)
    co.set_editor_property("new_materials", mats)
    co.set_editor_property("new_material_slot_names", [unreal.Name(n) for n in ("Black", "Steel", "Glow")])
    unreal.GeometryScript_AssetUtils.copy_mesh_to_static_mesh(mesh, asset, co, unreal.GeometryScriptMeshWriteLOD())
    EAL.save_loaded_asset(asset, False)
    log("기관총 " + GUN)


# ───────────── 손 계산 ─────────────
def hand_frame(mesh_path, side="Right"):
    mesh = EAL.load_asset(mesh_path)
    pose = APE.get_reference_pose(mesh.get_editor_property("skeleton"))
    g = lambda b: APE.get_bone_pose(pose, b, W)
    hand = g(side + "Hand")
    mid = g(side + "HandMiddle1").translation
    idx = g(side + "HandIndex1").translation
    pky = g(side + "HandPinky1").translation
    fwd = (mid - hand.translation).normal()
    up = idx - pky
    up = (up - fwd * up.dot(fwd)).normal()
    # 손바닥 쪽 = 가운뎃손가락 방향 × 엄지 쪽 (오른손). T자세에서 손바닥은 아래(-Z)를 본다 → 부호 확인
    palm = cross(fwd, up).normal() if side == "Right" else cross(up, fwd).normal()
    if palm.z > 0:
        palm = palm * -1.0
    return mesh, pose, hand, mid, fwd, up, palm


def socket_values():
    mesh, pose, hand, mid, fwd, up, palm = hand_frame(THIEF)
    rot = unreal.MathLibrary.make_rot_from_xz(fwd, up)
    # 쥔 주먹 가운데: 손목→가운뎃손가락 뿌리의 70% 지점에서 손바닥 쪽으로 3cm (손가락을 말면 손잡이가 여기 들어온다)
    grip = hand.translation + (mid - hand.translation) * 0.7 + palm * 3.0
    local = unreal.Transform(location=grip, rotation=rot, scale=[1, 1, 1]).make_relative(hand)
    return local


def finger_curls(mesh_path, side, angles):
    """손가락 뼈마다 '손바닥 쪽으로 마는' 회전축을 뼈 자기 좌표로 계산 → FVHFingerCurl 목록. angles = {손가락: (뿌리, 가운데, 끝)}"""
    mesh, pose, hand, mid, fwd, up, palm = hand_frame(mesh_path, side)
    out = []
    for finger, degs in angles.items():
        for j in range(3):
            bone = "%sHand%s%d" % (side, finger, j + 1)
            child = "%sHand%s%d" % (side, finger, j + 2)
            bt = APE.get_bone_pose(pose, bone, W)
            ct = APE.get_bone_pose(pose, child, W)
            v = (ct.translation - bt.translation).normal()
            n = palm
            if finger == "Thumb":
                # 엄지는 손바닥 가운데(가운뎃손가락 뿌리 아래) 쪽으로 감는다
                n = ((mid + palm * 2.0) - bt.translation).normal()
            axis_cs = cross(v, n).normal()
            axis_local = bt.rotation.unrotate_vector(axis_cs)
            h = math.radians(degs[j]) * 0.5
            q = unreal.Quat(axis_local.x * math.sin(h), axis_local.y * math.sin(h), axis_local.z * math.sin(h), math.cos(h))
            fc = unreal.VHFingerCurl()
            fc.set_editor_property("bone", bone)
            fc.set_editor_property("rotation", q.rotator())
            out.append(fc)
    return out


# 주먹(근접·투척·설치): 뿌리·가운데·끝 마디 각도
FIST = {"Index": (75, 90, 55), "Middle": (80, 95, 55), "Ring": (80, 95, 55), "Pinky": (80, 95, 55), "Thumb": (25, 35, 25)}
# 총 손잡이: 집게손가락은 방아쇠 쪽으로 반쯤 펴고 나머지는 감싼다
GUN_GRIP = {"Index": (35, 35, 15), "Middle": (80, 95, 55), "Ring": (80, 95, 55), "Pinky": (80, 95, 55), "Thumb": (25, 30, 20)}
# 왼손 받치기
LEFT = {"Index": (40, 45, 30), "Middle": (45, 50, 30), "Ring": (45, 50, 30), "Pinky": (45, 50, 30), "Thumb": (15, 20, 10)}


def do_guess():
    l = socket_values()
    r = l.rotation.rotator()
    log("소켓 추천 위치 (%.2f, %.2f, %.2f) 회전 P%.1f Y%.1f R%.1f" % (l.translation.x, l.translation.y, l.translation.z, r.pitch, r.yaw, r.roll))
    for fc in finger_curls(THIEF, "Right", FIST)[:4]:
        rr = fc.get_editor_property("rotation")
        log("  %s P%.1f Y%.1f R%.1f" % (fc.get_editor_property("bone"), rr.pitch, rr.yaw, rr.roll))


def do_socket():
    mesh = EAL.load_asset(THIEF)
    sock = mesh.find_socket(SOCKET)
    if sock is None:
        unreal.log_warning("[CharBuild] %s 에 %s 소켓이 없습니다 (MCP SkeletalMeshTools.add_socket 으로 먼저 만든다)" % (mesh.get_name(), SOCKET))
        return
    l = socket_values()
    r = l.rotation.rotator()
    sock.set_editor_property("relative_location", l.translation)
    sock.set_editor_property("relative_rotation", r)
    EAL.save_loaded_asset(mesh, False)
    log("소켓 %s: 위치 (%.1f, %.1f, %.1f) 회전 P%.1f Y%.1f R%.1f" % (SOCKET, l.translation.x, l.translation.y, l.translation.z, r.pitch, r.yaw, r.roll))


# ───────────── 데이터 ─────────────
def pose(rot, loc, elbow, left=False, left_on=(26, 0, -2)):
    p = unreal.VHHoldPose()
    p.set_editor_property("weapon", unreal.Transform(location=V(*loc), rotation=unreal.Rotator(roll=rot[2], pitch=rot[0], yaw=rot[1]), scale=[1, 1, 1]))
    p.set_editor_property("elbow_direction", V(*elbow))
    p.set_editor_property("left_hand_support", left)
    p.set_editor_property("left_hand_on_weapon", V(*left_on))
    return p


def do_data():
    da = EAL.load_asset(DATA)
    d = da.get_editor_property("default")
    abp = EAL.load_asset(TA + "ABP_VHThief_VHThief")
    d.set_editor_property("mesh", EAL.load_asset(THIEF))
    if abp:
        d.set_editor_property("anim_class", abp.generated_class())
    d.set_editor_property("idle_anim", EAL.load_asset(TA + "MM_Idle_VHThief"))
    d.set_editor_property("walk_anim", EAL.load_asset(TA + "MF_Unarmed_Walk_Fwd_VHThief"))
    d.set_editor_property("run_anim", EAL.load_asset(TA + "MF_Unarmed_Jog_Fwd_VHThief"))
    d.set_editor_property("down_anim", EAL.load_asset(TA + "MM_Death_Front_01_VHThief"))
    d.set_editor_property("hold_mode", unreal.VHWeaponHoldMode.HAND_SOCKET)
    d.set_editor_property("weapon_socket_name", SOCKET)
    for k, v in {"right_upper_arm_bone": "RightArm", "right_lower_arm_bone": "RightForeArm", "right_hand_bone": "RightHand",
                 "left_upper_arm_bone": "LeftArm", "left_lower_arm_bone": "LeftForeArm", "left_hand_bone": "LeftHand",
                 "spine_bone": "Spine2"}.items():
        d.set_editor_property(k, v)
    # 든 자세 (발 기준 앞 X·오른쪽 Y·높이 Z cm). 이 모델은 어깨 약 147cm, 팔(어깨→손목) 약 33cm 로 마네킹보다 팔이 짧다 → 몸 가까이
    # 근접: 날을 옆으로 눕혀(Roll 90) 탑뷰에서 날 옆면이 보이게. 원거리: 총을 세워(Roll 0) 손잡이 아래·총구 앞
    d.set_editor_property("melee_pose", pose((10, 15, 90), (30, 24, 112), (-0.3, 0.6, -0.75)))
    d.set_editor_property("ranged_pose", pose((0, 0, 0), (40, 16, 128), (-0.2, 0.75, -0.6)))
    d.set_editor_property("thrown_pose", pose((10, 10, 0), (26, 24, 118), (-0.3, 0.6, -0.75)))
    d.set_editor_property("placed_pose", pose((0, 0, 0), (34, 4, 104), (-0.2, 0.6, -0.8), True, (0, -16, 2)))
    wp = d.get_editor_property("weapon_poses")
    wp[unreal.VHWeaponId.FLAMETHROWER] = pose((0, 0, 0), (34, 14, 112), (-0.3, 0.7, -0.6), True, (34, 0, -2))
    wp[unreal.VHWeaponId.RAILGUN] = pose((0, 0, 0), (34, 14, 118), (-0.3, 0.7, -0.6), True, (38, 0, -2))
    d.set_editor_property("weapon_poses", wp)
    d.set_editor_property("grip_fingers", finger_curls(THIEF, "Right", FIST))
    d.set_editor_property("ranged_grip_fingers", finger_curls(THIEF, "Right", GUN_GRIP))
    d.set_editor_property("left_grip_fingers", finger_curls(THIEF, "Left", LEFT))
    roll = d.get_editor_property("roll_motion")
    roll.set_editor_property("spine_bones", ["Spine", "Spine1", "Spine2"])
    roll.set_editor_property("head_bones", ["Neck", "Head"])
    for k, v in {"left_thigh_bone": "LeftUpLeg", "right_thigh_bone": "RightUpLeg", "left_calf_bone": "LeftLeg",
                 "right_calf_bone": "RightLeg", "left_foot_bone": "LeftFoot", "right_foot_bone": "RightFoot"}.items():
        roll.set_editor_property(k, v)
    d.set_editor_property("roll_motion", roll)
    da.set_editor_property("default", d)

    g = da.get_editor_property("guard")
    g.set_editor_property("mesh", EAL.load_asset(GUARD))
    g.set_editor_property("idle_anim", EAL.load_asset(GA + "MM_Idle_VHGuard"))
    g.set_editor_property("walk_anim", EAL.load_asset(GA + "MF_Rifle_Walk_Fwd_VHGuard"))
    g.set_editor_property("run_anim", EAL.load_asset(GA + "MF_Rifle_Jog_Fwd_VHGuard"))
    g.set_editor_property("attack_anim", EAL.load_asset(GA + "MF_Rifle_Idle_ADS_VHGuard"))
    g.set_editor_property("gun_mesh", EAL.load_asset(GUN))
    g.set_editor_property("gun_bone", "RightHand")
    da.set_editor_property("guard", g)
    EAL.save_loaded_asset(da, False)
    log("DA_VHCharacterVisuals: 기본 = 의적(%s), 경비원 = 로봇" % ("ABP 있음" if abp else "ABP 없음!"))


# 근접 무기를 날 쪽(소켓 +Z)으로 미는 거리의 반대 부호 (cm). 음수 = 앞으로 → 손잡이 끝이 주먹 바로 뒤에 오게 (화면 보고 맞춤)
MELEE_SHIFT = {"BATON": -8.0, "SWORD": -3.0, "BEAM_SWORD": -6.0}


def do_grip():
    cat = EAL.load_asset(CATALOG)
    ws = cat.get_editor_property("weapons")
    n = 0
    out = []
    for w in ws:
        if w.get_editor_property("category") == unreal.VHWeaponCategory.MELEE:
            # 근접 무기: 날(+X)이 주먹을 관통하는 축(소켓 +Z, 엄지 쪽)으로 → 주먹으로 감싸 쥔 모양.
            # 위치 = 손잡이 가운데가 주먹에 오게 날 쪽으로 MELEE_SHIFT cm (소켓 기준, 무기 배율과 무관)
            shift = MELEE_SHIFT.get(str(w.get_editor_property("id")).split(".")[-1].split(":")[0], 0.0)
            w.set_editor_property("grip_offset", unreal.Transform(location=V(0.0, 0.0, -shift), rotation=unreal.Rotator(roll=0.0, pitch=90.0, yaw=0.0), scale=[1, 1, 1]))
            n += 1
        out.append(w)
    cat.set_editor_property("weapons", out)
    EAL.save_loaded_asset(cat, False)
    log("근접 무기 %d종 손잡이 보정: 날 = 소켓 +Z" % n)


# 총 위치를 계산하는 조준 자세 (2026-11-18: Mixamo 라이플 사격으로 바꿈 — 경비원 대기·이동·달리기·사격이 모두 Mixamo 라이플 동작이라 같은 손 모양)
GUN_FIT_ANIM = ("/Game/VaultHeist/Characters/Guard/Mixamo/A_Rifle_Fire", 0.0)


def gun_offset():
    """라이플 조준 자세에서 오른손 → 왼손 방향 = 총열(+X), 위 = 몸 위쪽. 손잡이(원점) = 오른손바닥 → RightHand 뼈 기준 보정"""
    anim = EAL.load_asset(GUN_FIT_ANIM[0])
    pose = APE.get_anim_pose_at_time(anim, GUN_FIT_ANIM[1], unreal.AnimPoseEvaluationOptions())
    rh = APE.get_bone_pose(pose, "RightHand", W)
    lh = APE.get_bone_pose(pose, "LeftHand", W)
    mid = APE.get_bone_pose(pose, "RightHandMiddle1", W).translation
    fwd = (lh.translation - rh.translation).normal()
    up = V(0, 0, 1)
    up = (up - fwd * up.dot(fwd)).normal()
    rot = unreal.MathLibrary.make_rot_from_xz(fwd, up)
    grip = rh.translation + (mid - rh.translation) * 0.6 + V(0, 0, -2.0)
    local = unreal.Transform(location=grip, rotation=rot, scale=[1, 1, 1]).make_relative(rh)
    return local


def do_gunfit():
    l = gun_offset()
    da = EAL.load_asset(DATA)
    g = da.get_editor_property("guard")
    g.set_editor_property("gun_offset", l)
    da.set_editor_property("guard", g)
    EAL.save_loaded_asset(da, False)
    r = l.rotation.rotator()
    log("경비원 총 보정: 위치 (%.1f, %.1f, %.1f) 회전 P%.1f Y%.1f R%.1f" % (l.translation.x, l.translation.y, l.translation.z, r.pitch, r.yaw, r.roll))


# ───────────── 검 자세 · 베기 동작 (사용자 요청 2026-11-17 밤) ─────────────
# 든 자세: 칼날이 위, 칼끝을 조준 쪽으로 40° 기울임(= 수평에서 50° 세움) → 탑뷰에서도 칼날 길이의 약 64%가 보인다. 손잡이 = 오른손 가슴~허리 높이.
# Roll 90 = 칼날 넓은 면이 옆을 보게 → 수평으로 벨 때는 넓은 면이 위(카메라)를 보고 날이 왼쪽(베는 쪽)을 향한다.
SWORD_HOLD = ((50, 0, 90), (24, 22, 112), (-0.3, 0.6, -0.75))
# 베기 키: (시간, 손 이동 앞·오른쪽·위 cm, 칼 회전 (Pitch + = 위로, Yaw + = 오른쪽), 허리 비틀기 + = 오른쪽)
# 세움 → 들어 올림(오른쪽 뒤로 젖힘) → 오른쪽 끝(부채꼴 +60°)에서 수평으로 → 앞(0°) → 왼쪽 끝(−60°)에서 수평으로 끝남 → 다시 세움
SWORD_KEYS = [
    (0.00, (0, 0, 0), (0, 0), 0),
    (0.10, (-4, 10, 18), (25, 55), 15),
    (0.17, (6, 18, -4), (-45, 60), 12),
    (0.24, (22, 0, -8), (-50, 0), 0),
    (0.31, (12, -24, -8), (-48, -60), -18),
    (0.42, (0, 0, 0), (0, 0), 0),
]
SWORD_EFFECT = (0.17, 0.14)   # 연출 순간(칼날이 오른쪽 끝) · 오른쪽 끝 → 왼쪽 끝 시간. 서버 간격 도검 0.5초 안
BEAM_SCALE = 0.75             # 빔 소드는 서버 간격 0.333초 → 키 시간 × 0.75 (0.315초)

# ───────────── 2026-11-19 근접 3종 나누기 (사용자 요청: 삼단봉이 검과 같으면 안 됨) ─────────────
# 키: (시간, 손 이동 앞·오른쪽·위 cm, 무기 회전 (Pitch + = 위, Yaw + = 오른쪽), 허리 비틀기 + = 오른쪽, 허리 숙임 + = 앞으로)
# Effect = (빠른 구간 시작, 빠른 구간 길이). 궤적 리본·부채꼴이 이 구간을 따른다. 삼단봉은 구간 끝 = 내려쳐 맞는 순간
# 삼단봉(둔기): 낮게 앞으로 든 자세 → 오른쪽 어깨 위로 번쩍 → 왼쪽 아래 사선으로 짧고 빠르게 내려치기. 0.34초 (서버 간격 0.5초)
BATON_HOLD = ((20, 10, 90), (28, 26, 104), (-0.3, 0.6, -0.75))
BATON_KEYS = [
    (0.00, (0, 0, 0), (0, 0), 0, 0),
    (0.09, (-6, 6, 30), (70, 25), 15, -10),
    (0.15, (24, -4, -14), (-70, -15), -8, 18),
    (0.20, (20, -8, -18), (-78, -20), -10, 16),
    (0.34, (0, 0, 0), (0, 0), 0, 0),
]
BATON_EFFECT = (0.09, 0.06)
# 빔 소드: 도검과 같은 세워 들기(40° 기울임)에서 뒤로 크게 감았다가 앞으로 내딛듯 숙이며 넓게 베고 왼쪽 뒤까지 따라감. 0.32초 (서버 간격 0.333초)
BEAM_KEYS = [
    (0.00, (0, 0, 0), (0, 0), 0, 0),
    (0.08, (-10, 20, 20), (30, 100), 35, -6),
    (0.13, (8, 26, -4), (-48, 65), 22, 4),
    (0.17, (34, 2, -8), (-52, 0), 0, 14),
    (0.21, (20, -32, -6), (-50, -70), -30, 10),
    (0.25, (8, -34, 0), (-30, -95), -38, 4),
    (0.32, (0, 0, 0), (0, 0), 0, 0),
]
BEAM_EFFECT = (0.13, 0.08)


def motion(keys, effect, scale=1.0):
    m = unreal.VHWeaponMotion()
    ks = []
    for key in keys:
        t, hand, rot, spine = key[:4]
        k = unreal.VHMotionKey()
        k.set_editor_property("time", t * scale)
        k.set_editor_property("hand_offset", V(*hand))
        k.set_editor_property("weapon_rotation", unreal.Rotator(roll=rot[2] if len(rot) > 2 else 0.0, pitch=rot[0], yaw=rot[1]))
        k.set_editor_property("spine_yaw", spine)
        k.set_editor_property("spine_pitch", key[4] if len(key) > 4 else 0.0)
        ks.append(k)
    m.set_editor_property("keys", ks)
    m.set_editor_property("effect_time", effect[0] * scale)
    m.set_editor_property("effect_duration", effect[1] * scale)
    return m


def do_sword():
    da = EAL.load_asset(DATA)
    d = da.get_editor_property("default")
    wp = d.get_editor_property("weapon_poses")
    am = d.get_editor_property("attack_motions")
    # 2026-11-19: 근접 3종을 나눔 (사용자 요청). 도검 = 세워 들기 → 수평 베기 그대로, 삼단봉 = 사선 내려치기, 빔 소드 = 크게 넓은 베기
    # 서버 간격 삼단봉·도검 0.5초, 빔 소드 0.333초 — 키 끝 시간이 모두 그 안
    wp[unreal.VHWeaponId.BATON] = pose(*BATON_HOLD)
    am[unreal.VHWeaponId.BATON] = motion(BATON_KEYS, BATON_EFFECT)
    wp[unreal.VHWeaponId.SWORD] = pose(*SWORD_HOLD)
    am[unreal.VHWeaponId.SWORD] = motion(SWORD_KEYS, SWORD_EFFECT)
    wp[unreal.VHWeaponId.BEAM_SWORD] = pose(*SWORD_HOLD)
    am[unreal.VHWeaponId.BEAM_SWORD] = motion(BEAM_KEYS, BEAM_EFFECT)
    d.set_editor_property("weapon_poses", wp)
    d.set_editor_property("attack_motions", am)
    da.set_editor_property("default", d)
    EAL.save_loaded_asset(da, False)
    log("근접 자세·동작: 삼단봉 내려치기 %.2f초 / 도검 수평 베기 %.2f초 / 빔 소드 넓은 베기 %.2f초" % (BATON_KEYS[-1][0], SWORD_KEYS[-1][0], BEAM_KEYS[-1][0]))


# ───────────── 2026-11-19 원거리 반동: Lyra 사격 동작에서 뽑은 곡선(Tools/External/lyra_recoil.json) ─────────────
# Lyra 동작을 통째로 쓰면 손 위치(코드 IK)와 부딪혀서 오른손·허리 움직임만 숫자로 가져와 키 프레임으로 (탑뷰에서 보이게 키움)
# (이름, Lyra 곡선, 시간 배율, 손 이동 배율, 총구 회전 배율, 허리 배율, 덧붙이는 위로 튐(도), 원래 곡선을 쓰는 길이(초))
RECOIL_PLAN = [
    ("PISTOL", "pistol", 0.8, 3.0, 1.3, 2.0, 0.0, 0.34),     # 짧고 날카로운 한 발 + 작은 반동, 0.27초 (서버 간격 0.25초 → 자동 빠르기)
    ("TASER", "pistol", 1.0, 1.5, 0.7, 1.0, 0.0, 0.34),      # 권총 계열, 반동 작게
    ("RAILGUN", "rifle", 1.2, 4.5, 3.0, 3.0, 14.0, 0.53),    # 큰 반동 (뒤로 9cm·허리 비틀림) 0.64초, 그 뒤 1초 잠금 자세
]


PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())


def recoil_motion(curve, tscale, hscale, rscale, sscale, kick, until):
    import json
    with open(os.path.join(PROJ, "Tools", "External", "lyra_recoil.json"), encoding="utf-8") as fh:
        data = json.load(fh)[curve]
    keys = []
    frames = [f for f in data["frames"] if f["t"] <= until + 1e-3]
    peak = max(frames, key=lambda f: abs(f["rot_r"][0]))["t"] if frames else 0.0
    for i, f in enumerate(frames):
        if i % 2 and i != len(frames) - 1:
            continue
        h = f["hand_r"]
        k = kick * max(0.0, 1.0 - abs(f["t"] - peak) / max(peak * 3.0, 0.05)) if i else 0.0
        keys.append((f["t"] * tscale, (h[0] * hscale, h[1] * hscale, h[2] * hscale), (f["rot_r"][0] * rscale + k, f["rot_r"][1] * rscale),
                     f["spine"][1] * sscale, -f["spine"][0] * sscale))
    keys[-1] = (keys[-1][0], (0, 0, 0), (0, 0), 0, 0)
    return keys


def do_ranged():
    da = EAL.load_asset(DATA)
    d = da.get_editor_property("default")
    am = d.get_editor_property("attack_motions")
    for name, curve, ts, hs, rs, ss, kick, until in RECOIL_PLAN:
        keys = recoil_motion(curve, ts, hs, rs, ss, kick, until)
        am[getattr(unreal.VHWeaponId, name)] = motion(keys, (0.02, 0.05))
        log("반동 %s ← Lyra %s: %d키 %.2f초, 최대 위로 %.0f°" % (name, curve, len(keys), keys[-1][0], max(k[2][0] for k in keys)))
    d.set_editor_property("attack_motions", am)
    da.set_editor_property("default", d)
    EAL.save_loaded_asset(da, False)


# ───────────── 2026-11-20 수류탄: 핀 뽑기 → 뒤로 젖히기 → 던지기 (Lyra MM_Rifle_GrenadeToss 곡선 모양을 참고, lyra_recoil.json toss) ─────────────
# Lyra: 0.1~0.21초 손이 몸 쪽·아래로 모이고 허리가 오른쪽(−19°)으로 감김 → 0.31~0.52초 허리가 반대로(+28°) 풀리며 손이 앞으로.
# 탑뷰에서 보이게 손 이동을 키우고, 지금 소리 타이밍에 맞췄다: 핀 "딸깍" = 0초(가슴 앞으로 모아 핀 뽑기),
# 던지는 바람(0.12초 늦게 시작) = 젖혔다가 앞으로 휘두르는 0.20~0.29초, 0.29초에 손을 떠남(= 날아가는 소리·비행 시작). 서버 간격 1초 안
# (시간, 손 이동 앞·오른쪽·위 cm, 수류탄 회전 Pitch·Yaw·Roll, 허리 Yaw, 허리 Pitch)
GRENADE_KEYS = [
    (0.00, (0, 0, 0), (0, 0), 0, 0),
    (0.07, (-4, -14, 4), (15, -30, 0), 6, 4),       # 핀 뽑기: 수류탄을 가슴 앞 가운데로 (왼손이 핀을 뽑는 자리)
    (0.11, (-6, -12, 6), (18, -25, 0), 8, 2),       # 잠깐 멈춤 (딸깍)
    (0.20, (-34, 16, 36), (48, 0, 0), 30, -12),     # 뒤로 젖히기: 오른쪽 어깨 뒤·위, 허리를 오른쪽으로 감고 뒤로 젖힘
    (0.29, (30, -6, 24), (-28, 0, 0), -20, 10),     # 던지기: 앞으로 휘둘러 손을 떠남
    (0.45, (22, -16, -6), (-42, 0, 0), -26, 14),    # 따라 나가기 (허리가 왼쪽으로 풀림)
    (0.75, (0, 0, 0), (0, 0), 0, 0),
]
GRENADE_RELEASE = 0.29
# 손에 든·날아가는 수류탄 크기: 무기별 배율 (전체 배율 2 × 이 값). 1.4(실제의 2.8배)는 도시락통만 해 보여서 줄임
GRENADE_HELD_SCALE = 0.8


def do_grenade():
    da = EAL.load_asset(DATA)
    d = da.get_editor_property("default")
    am = d.get_editor_property("attack_motions")
    am[unreal.VHWeaponId.GRENADE] = motion(GRENADE_KEYS, (GRENADE_RELEASE, 0.05))
    d.set_editor_property("attack_motions", am)
    da.set_editor_property("default", d)
    EAL.save_loaded_asset(da, False)
    cat = EAL.load_asset(CATALOG)
    ws = cat.get_editor_property("weapons")
    for i in range(len(ws)):
        w = ws[i]    # 배열에서 꺼낸 구조체는 복사본 → 고친 뒤 다시 넣어야 저장된다
        if w.get_editor_property("id") == unreal.VHWeaponId.GRENADE:
            log("수류탄 손 배율 %.2f → %.2f" % (w.get_editor_property("held_scale"), GRENADE_HELD_SCALE))
            w.set_editor_property("held_scale", GRENADE_HELD_SCALE)
            ws[i] = w
    cat.set_editor_property("weapons", ws)
    EAL.save_loaded_asset(cat, False)
    log("수류탄 던지기: 핀 뽑기 0.07 → 젖히기 0.20 → 손을 떠남 %.2f → 끝 %.2f초" % (GRENADE_RELEASE, GRENADE_KEYS[-1][0]))


# ───────────── 여러 캐릭터 (2026-11-20: 도적1~3) ─────────────
GAMEDATA = "/Game/VaultHeist/Data/DA_VHGameData"
THIEF_SKELETON = ROOT + "/Thief/SK_VHThief_Skeleton"
# 캐릭터 번호 → 메시. 1 = Default 칸(도적1). 2·3 은 Characters 칸. 메시 에셋이 없으면 그 칸 Mesh 를 비운다 → 게임은 도적1 모습
EXTRA_CHARS = {2: ROOT + "/Thief2/SK_VHThief2", 3: ROOT + "/Thief3/SK_VHThief3"}
# 캐릭터별 손 보정 (무기 10종을 들어 보고 맞춘 값). socket_offset = 손 소켓 추가 보정(위치 cm, 회전 도), weapon_scale = 무기 크기 배율
CHAR_FIT = {
    2: dict(socket_offset=((0, 0, 0), (0, 0, 0)), weapon_scale=1.0),
    3: dict(socket_offset=((0, 0, 0), (0, 0, 0)), weapon_scale=1.0),
}


def mesh_ref(mesh_path):
    """메시 자기 기준 자세(뼈 이름 → 컴포넌트 기준 Transform). 스켈레톤을 함께 써도 메시마다 관절 위치가 다르다"""
    mesh = EAL.load_asset(mesh_path)
    m = unreal.SkeletonModifier()
    m.set_skeletal_mesh(mesh)
    out = {str(b): m.get_bone_transform(b, True) for b in m.get_all_bone_names()}
    return mesh, out


def socket_values_for(mesh_path):
    """socket_values() 와 같은 계산을 그 메시 자기 관절로"""
    mesh, ref = mesh_ref(mesh_path)
    hand = ref["RightHand"]
    mid = ref["RightHandMiddle1"].translation
    idx = ref["RightHandIndex1"].translation
    pky = ref["RightHandPinky1"].translation
    fwd = (mid - hand.translation).normal()
    up = idx - pky
    up = (up - fwd * up.dot(fwd)).normal()
    palm = cross(fwd, up).normal()
    if palm.z > 0:
        palm = palm * -1.0
    rot = unreal.MathLibrary.make_rot_from_xz(fwd, up)
    grip = hand.translation + (mid - hand.translation) * 0.7 + palm * 3.0
    return mesh, unreal.Transform(location=grip, rotation=rot, scale=[1, 1, 1]).make_relative(hand)


def do_retargetmode():
    """캐릭터1 스켈레톤을 여러 메시가 함께 쓰도록: Hips = AnimationScaled, 나머지 = Skeleton (각 메시 자기 뼈 길이)"""
    sk = EAL.load_asset(THIEF_SKELETON)
    n = unreal.VHCharacterDevLibrary.set_shared_skeleton_retargeting(sk, "Hips")
    EAL.save_loaded_asset(sk, False)
    desc = unreal.VHCharacterDevLibrary.describe_retargeting(sk)
    log("뼈 이동 리타게팅: %d개 바꿈 → %s ..." % (n, desc[:160]))


def do_socket2():
    """도적2·3 메시에 WeaponSocket_R (그 메시 손가락 관절로 계산). 소켓은 MCP add_socket 으로 먼저 만들어 둔다"""
    for cid, path in EXTRA_CHARS.items():
        if not EAL.does_asset_exist(path):
            log("캐릭터 %d 메시 없음 (%s) → 소켓 건너뜀" % (cid, path))
            continue
        mesh, l = socket_values_for(path)
        sock = mesh.find_socket(SOCKET)
        if sock is None or sock.get_outer() != mesh:
            unreal.log_warning("[CharBuild] 캐릭터 %d: %s 소켓이 없습니다 → MCP SkeletalMeshTools.add_socket(%s, %s, RightHand) 로 먼저 만든다" % (cid, SOCKET, path, SOCKET))
            continue
        r = l.rotation.rotator()
        sock.set_editor_property("relative_location", l.translation)
        sock.set_editor_property("relative_rotation", r)
        EAL.save_loaded_asset(mesh, False)
        t1 = EAL.load_asset(THIEF).find_socket(SOCKET)
        log("캐릭터 %d 소켓 %s: 위치 (%.1f, %.1f, %.1f) 회전 P%.1f Y%.1f R%.1f (도적1: (%.1f, %.1f, %.1f))" % (
            cid, SOCKET, l.translation.x, l.translation.y, l.translation.z, r.pitch, r.yaw, r.roll,
            t1.relative_location.x, t1.relative_location.y, t1.relative_location.z))


def do_chars():
    """DA_VHCharacterVisuals.Characters: 2·3 = Default(도적1) 복사 + 그 캐릭터 메시·손 보정. 메시가 없으면 Mesh 비움(= 도적1로 보임)"""
    da = EAL.load_asset(DATA)
    base = da.get_editor_property("default")
    chars = da.get_editor_property("characters")
    for cid, path in EXTRA_CHARS.items():
        v = base.copy()
        have = EAL.does_asset_exist(path)
        v.set_editor_property("mesh", EAL.load_asset(path) if have else None)
        fit = CHAR_FIT.get(cid, {})
        (loc, rot) = fit.get("socket_offset", ((0, 0, 0), (0, 0, 0)))
        v.set_editor_property("socket_offset", unreal.Transform(location=V(*loc), rotation=unreal.Rotator(roll=rot[2], pitch=rot[0], yaw=rot[1]), scale=[1, 1, 1]))
        v.set_editor_property("weapon_scale", fit.get("weapon_scale", 1.0))
        chars[cid] = v
        log("캐릭터 %d 칸: %s" % (cid, ("메시 " + path) if have else "메시 비움 → 도적1로 보임"))
    for k in list(chars.keys()):
        if k not in EXTRA_CHARS:
            log("캐릭터 %d 칸 지움 (도적1~3만)" % k)
            chars.pop(k)
    da.set_editor_property("characters", chars)
    EAL.save_loaded_asset(da, False)


def do_gamedata():
    """DA_VHGameData 캐릭터 목록 = 도적1~3 (대기실 캐릭터 선택 창에 이 세 개만 나온다)"""
    gd = EAL.load_asset(GAMEDATA)
    cs = gd.get_editor_property("characters")
    before = [(c.get_editor_property("id"), str(c.get_editor_property("display_name"))) for c in cs]
    keep = [c for c in cs if 1 <= c.get_editor_property("id") <= 3]
    for c in keep:
        c.set_editor_property("display_name", "도적%d" % c.get_editor_property("id"))
    gd.set_editor_property("characters", keep)
    EAL.save_loaded_asset(gd, False)
    log("DA_VHGameData 캐릭터 %s → %s" % (before, [(c.get_editor_property("id"), str(c.get_editor_property("display_name"))) for c in keep]))


PORTRAIT_SRC = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "SourceArt", "Characters", "Portraits")
PORTRAIT_DIR = "/Game/VaultHeist/UI/Portraits"


def do_portraits():
    """대기실 캐릭터 버튼·HUD 얼굴 그림: SourceArt/Characters/Portraits/T_VHPortrait_Thief{N}.png → DA_VHGameData 캐릭터 N 의 Portrait.
    그림이 없는 캐릭터(도적3)는 Portrait 를 비운다 → 버튼에 "모델 준비 중". 그림은 미리보기 레벨에서 vh.Char N + vh.Cam 130 -4 -100 66 로 찍는다"""
    if not EAL.does_directory_exist(PORTRAIT_DIR):
        EAL.make_directory(PORTRAIT_DIR)
    gd = EAL.load_asset(GAMEDATA)
    cs = gd.get_editor_property("characters")
    for i, c in enumerate(cs):
        cid = c.get_editor_property("id")
        src = os.path.join(PORTRAIT_SRC, "T_VHPortrait_Thief%d.png" % cid)
        tex = None
        if os.path.exists(src):
            t = unreal.AssetImportTask()
            t.set_editor_property("filename", src)
            t.set_editor_property("destination_path", PORTRAIT_DIR)
            t.set_editor_property("destination_name", "T_VHPortrait_Thief%d" % cid)
            t.set_editor_property("automated", True)
            t.set_editor_property("replace_existing", True)
            t.set_editor_property("save", False)
            TOOLS.import_asset_tasks([t])
            tex = EAL.load_asset("%s/T_VHPortrait_Thief%d" % (PORTRAIT_DIR, cid))
            if tex:
                tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
                tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
                tex.set_editor_property("never_stream", True)
                EAL.save_loaded_asset(tex, False)
        c.set_editor_property("portrait", tex)
        cs[i] = c
        log("캐릭터 %d 얼굴 그림: %s" % (cid, tex.get_path_name() if tex else "없음 (모델 준비 중)"))
    gd.set_editor_property("characters", cs)
    EAL.save_loaded_asset(gd, False)


def main(argv):
    try:
        if unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor():
            unreal.log_error("[CharBuild] 플레이 중에는 돌리지 않습니다")
            return
    except Exception:
        pass
    for s in argv:
        {"ring": do_ring, "gun": do_gun, "socket": do_socket, "data": do_data, "grip": do_grip, "guess": do_guess, "gunfit": do_gunfit, "sword": do_sword, "ranged": do_ranged, "grenade": do_grenade, "retargetmode": do_retargetmode, "socket2": do_socket2, "chars": do_chars, "gamedata": do_gamedata, "portraits": do_portraits}[s]()
    log("끝: %s" % argv)


main(sys.argv[1:])
