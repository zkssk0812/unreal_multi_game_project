# -*- coding: utf-8 -*-
"""
무기 든 자세용 애니메이션 에셋 준비 (에디터 Cmd 칸: py "C:/UnrealProjects/MCPTest/Tools/WeaponBuild/setup_anim.py" 단계...)

단계
  abp     : 템플릿 ABP_Unarmed 를 /Game/VaultHeist/Animation/ABP_VHThief 로 복제하고 부모를 VHThiefAnimInstance 로 바꾼다
  data    : DA_VHCharacterVisuals 기본 칸의 Anim Class = ABP_VHThief, 손 소켓 방식
  guess   : 기준 자세의 손가락 뼈 위치로 손 소켓 추천 값을 로그에 적는다
  autosocket / socket X Y Z PITCH YAW ROLL : 캐릭터 메시의 WeaponSocket_R(본 hand_r) 값을 추천 값 / 준 값으로 고친다 (소켓은 미리 있어야 한다)

플레이(PIE) 중에는 돌리지 않는다 (에셋 저장이 실패한다).
"""
import sys
import unreal

EAL = unreal.EditorAssetLibrary
ANIM_DIR = "/Game/VaultHeist/Animation"
ABP = ANIM_DIR + "/ABP_VHThief"
SRC_ABP = "/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed"
DATA = "/Game/VaultHeist/Weapons/Data/DA_VHCharacterVisuals"
MESHES = ["/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple", "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"]
SOCKET = "WeaponSocket_R"


def in_pie():
    try:
        return unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    except Exception:
        return False


def do_abp():
    if not EAL.does_asset_exist(ABP):
        if not EAL.does_directory_exist(ANIM_DIR):
            EAL.make_directory(ANIM_DIR)
        EAL.duplicate_asset(SRC_ABP, ABP)
    bp = EAL.load_asset(ABP)
    parent = unreal.VHThiefAnimInstance.static_class()
    unreal.BlueprintEditorLibrary.reparent_blueprint(bp, parent)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    EAL.save_loaded_asset(bp, False)
    unreal.log("[WeaponBuild] ABP_VHThief 준비: 부모 = VHThiefAnimInstance")


def do_data():
    da = EAL.load_asset(DATA)
    bp = EAL.load_asset(ABP)
    default = da.get_editor_property("default")
    if bp is not None:
        default.set_editor_property("anim_class", bp.generated_class())
    default.set_editor_property("hold_mode", unreal.VHWeaponHoldMode.HAND_SOCKET)
    default.set_editor_property("weapon_socket_name", SOCKET)
    da.set_editor_property("default", default)
    EAL.save_loaded_asset(da, False)
    unreal.log("[WeaponBuild] DA_VHCharacterVisuals: Anim Class = %s, 손 소켓 %s" % (bp.get_name() if bp else "없음", SOCKET))


def do_socket(args):
    x, y, z, pitch, yaw, roll = [float(a) for a in args[:6]]
    for path in MESHES:
        mesh = EAL.load_asset(path)
        if mesh is None:
            continue
        sock = mesh.find_socket(SOCKET)
        if sock is None:
            # 파이썬에서는 소켓 이름을 정할 수 없다 → MCP SkeletalMeshTools.add_socket 또는 메시 에디터(스켈레톤 트리 → hand_r 우클릭 → 소켓 추가)로 먼저 만든다
            unreal.log_warning("[WeaponBuild] %s 에 %s 소켓이 없습니다. 먼저 만든 뒤 다시 실행하세요." % (mesh.get_name(), SOCKET))
            continue
        sock.set_editor_property("relative_location", unreal.Vector(x, y, z))
        sock.set_editor_property("relative_rotation", unreal.Rotator(roll=roll, pitch=pitch, yaw=yaw))
        EAL.save_loaded_asset(mesh, False)
        unreal.log("[WeaponBuild] %s: %s (hand_r) 위치 (%.1f, %.1f, %.1f) 회전 P%.1f Y%.1f R%.1f" % (mesh.get_name(), SOCKET, x, y, z, pitch, yaw, roll))


def do_guess():
    """기준 자세의 손 뼈 위치로 소켓 추천 값을 계산한다: +X = 손목→가운뎃손가락, +Z = 새끼→집게 쪽(엄지 쪽), 위치 = 손바닥 가운데"""
    mesh = EAL.load_asset(MESHES[0])
    skel = mesh.get_editor_property("skeleton")
    pose = unreal.AnimPoseExtensions.get_reference_pose(skel)
    W = unreal.AnimPoseSpaces.WORLD
    hand = unreal.AnimPoseExtensions.get_bone_pose(pose, "hand_r", W)
    mid = unreal.AnimPoseExtensions.get_bone_pose(pose, "middle_metacarpal_r", W).translation
    idx = unreal.AnimPoseExtensions.get_bone_pose(pose, "index_metacarpal_r", W).translation
    pky = unreal.AnimPoseExtensions.get_bone_pose(pose, "pinky_metacarpal_r", W).translation
    fwd = (mid - hand.translation).normal()
    up = idx - pky
    up = (up - fwd * up.dot(fwd)).normal()
    rot = unreal.MathLibrary.make_rot_from_xz(fwd, up)
    grip = mid + fwd * 2.0
    world = unreal.Transform(location=grip, rotation=rot, scale=[1, 1, 1])
    local = world.make_relative(hand)
    r = local.rotation.rotator()
    t = local.translation
    unreal.log("[WeaponBuild] 손 소켓 추천: 위치 %.2f %.2f %.2f 회전(P Y R) %.2f %.2f %.2f" % (t.x, t.y, t.z, r.pitch, r.yaw, r.roll))
    return [t.x, t.y, t.z, r.pitch, r.yaw, r.roll]


if in_pie():
    unreal.log_warning("[WeaponBuild] 플레이 중입니다. 플레이를 멈춘 뒤 다시 실행하세요.")
else:
    argv = sys.argv[1:]
    i = 0
    while i < len(argv):
        step = argv[i]
        if step == "abp":
            do_abp()
        elif step == "data":
            do_data()
        elif step == "guess":
            do_guess()
        elif step == "autosocket":
            do_socket([str(v) for v in do_guess()])
        elif step == "socket":
            do_socket(argv[i + 1:i + 7])
            i += 6
        else:
            unreal.log_warning("[WeaponBuild] 모르는 단계: " + step)
        i += 1
