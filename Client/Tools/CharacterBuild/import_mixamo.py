# -*- coding: utf-8 -*-
"""
Mixamo 애니메이션(FBX, Without Skin) → 의적·경비원 스켈레톤에 바로 임포트
에디터 Cmd 칸: py "C:/UnrealProjects/MCPTest/Tools/CharacterBuild/import_mixamo.py" 단계...

원본: SourceArt/Animations/animation.zip 을 SourceArt/Animations/Mixamo/ 에 푼 FBX 28개 (Git 제외, zip 이 원본)

단계
  probe NAME      : 하나만 Thief 로 임포트(_Probe 폴더)하고 뼈 위치를 로그에 적는다
  thief / guard   : 표(THIEF / GUARD)의 애니메이션을 임포트 → Characters/<Thief|Guard>/Mixamo/A_<이름>
  probeasset PATH : 에셋 하나의 뼈 위치를 로그에 적는다
  inplace         : 이동 동작의 Hips 앞으로 나아가는 이동을 지우고(제자리) 속도를 로그에 적는다
  retarget        : (임포트 직후 한 번만) Mixamo 기본 자세 → 우리 기본 자세로 회전 옮기기 (mixamo_retarget.py)
  all             : thief guard retarget inplace

플레이(PIE) 중에는 돌리지 않는다.
"""
import os
import sys
import unreal

EAL = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SRC = os.path.join(PROJ, "SourceArt", "Animations", "Mixamo")
ROOT = "/Game/VaultHeist/Characters"
SKEL = {
    "Thief": ROOT + "/Thief/SK_VHThief_Skeleton",
    "Guard": ROOT + "/Guard/SK_VHGuard_Skeleton",
}

# FBX 이름 → 에셋 이름 (A_ 접두사), 이동 동작 여부(제자리로 만들 것)
THIEF = {
    "Breathing Idle": ("Idle", False),
    "Walking": ("Walk_Fwd", True),
    "Walking Backwards": ("Walk_Bwd", True),
    "Left Strafe Walking": ("Walk_Left", True),
    "Right Strafe Walking": ("Walk_Right", True),
    "Running": ("Run_Fwd", True),
    "Running Backward": ("Run_Bwd", True),
    "Left Strafe": ("Run_Left", True),
    "Right Strafe": ("Run_Right", True),
    "Stand To Roll": ("Roll", True),
    "Being Electrocuted": ("Electrocuted", False),
    "Drinking": ("Drinking", False),
    "Dying": ("Dying", False),
    "Walking To Dying": ("WalkDying", False),
    "Hit Reaction": ("HitReact", False),
    "Kneeling Down": ("KneelDown", False),
    "Kneeling Inspecting": ("KneelInspect", False),
    "Picking Up": ("PickUp", False),
    "Pistol Idle": ("Pistol_Idle", False),
    "Pistol Walk": ("Pistol_Walk", True),
    "Pistol Run": ("Pistol_Run", True),
    "Shooting": ("Pistol_Shoot", False),
    "Rifle Idle": ("Rifle_Idle", False),
    "Rifle Run": ("Rifle_Run", True),
    "Firing Rifle": ("Rifle_Fire", False),
    "Standing Melee Attack Horizontal": ("Melee_Horizontal", False),
    "Throw": ("Throw", False),
}
GUARD = {
    "Rifle Idle": ("Rifle_Idle", False),
    "Rifle Run": ("Rifle_Run", True),
    "Firing Rifle": ("Rifle_Fire", False),
    "Firing Rifle Walking": ("Rifle_FireWalk", True),
    "Being Electrocuted": ("Electrocuted", False),
    "Hit Reaction": ("HitReact", False),
}
TABLES = {"Thief": THIEF, "Guard": GUARD}


def log(msg):
    unreal.log("[Mixamo] " + msg)


def ensure_dir(p):
    if not EAL.does_directory_exist(p):
        EAL.make_directory(p)


def import_one(fbx_name, who, asset_name, dest):
    fbx = os.path.join(SRC, fbx_name + ".fbx")
    if not os.path.exists(fbx):
        unreal.log_warning("[Mixamo] 파일 없음: " + fbx)
        return None
    ensure_dir(dest)
    path = "%s/%s" % (dest, asset_name)
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", False)
    ui.set_editor_property("import_as_skeletal", True)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_ANIMATION)
    ui.set_editor_property("import_animations", True)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("skeleton", EAL.load_asset(SKEL[who]))
    ad = ui.get_editor_property("anim_sequence_import_data")
    ad.set_editor_property("import_uniform_scale", 1.0)
    ad.set_editor_property("animation_length", unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
    ad.set_editor_property("remove_redundant_keys", False)
    ad.set_editor_property("import_bone_tracks", True)
    ad.set_editor_property("use_default_sample_rate", False)
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", fbx)
    t.set_editor_property("destination_path", dest)
    t.set_editor_property("destination_name", asset_name)
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("save", True)
    t.set_editor_property("options", ui)
    TOOLS.import_asset_tasks([t])
    got = [str(p) for p in t.get_editor_property("imported_object_paths")]
    seq = None
    for p in got:
        a = EAL.load_asset(p)
        if isinstance(a, unreal.AnimSequence):
            seq = a
    if seq is None:
        unreal.log_warning("[Mixamo] 임포트 실패: %s → %s (%s)" % (fbx_name, path, got))
        return None
    # 같은 폴더에 메시·스켈레톤이 딸려 오면 안 된다 (스켈레톤 지정이 틀렸을 때)
    for p in got:
        if not isinstance(EAL.load_asset(p), unreal.AnimSequence):
            unreal.log_warning("[Mixamo] 애니메이션 아닌 에셋도 생김: " + p)
    return seq


def fbx_probe(name):
    seq = import_one(name, "Thief", "A_Probe", ROOT + "/Thief/_Probe")
    if not seq:
        return
    probe(seq)


def probe(seq, t=0.0):
    APE = unreal.AnimPoseExtensions
    opt = unreal.AnimPoseEvaluationOptions()
    L = unreal.AnimationLibrary
    log("%s 길이 %.2f초, 프레임 %d, 뼈 트랙 %d" % (seq.get_name(), seq.get_play_length(), L.get_num_frames(seq),
                                             len(L.get_animation_track_names(seq))))
    for tt in (t, seq.get_play_length() * 0.5):
        pose = APE.get_anim_pose_at_time(seq, tt, opt)
        for bn in ("Hips", "Spine2", "Head", "RightArm", "RightHand", "LeftFoot", "RightFoot"):
            try:
                w = APE.get_bone_pose(pose, bn, unreal.AnimPoseSpaces.WORLD).translation
                l = APE.get_bone_pose(pose, bn, unreal.AnimPoseSpaces.LOCAL)
                log("  t=%.2f %-10s world (%.1f, %.1f, %.1f) local (%.1f, %.1f, %.1f) s=%.2f" % (
                    tt, bn, w.x, w.y, w.z, l.translation.x, l.translation.y, l.translation.z, l.scale3d.x))
            except Exception as e:
                log("  %s 없음 (%s)" % (bn, e))


def do_import(who):
    dest = "%s/%s/Mixamo" % (ROOT, who)
    n = 0
    for fbx_name, (asset, _) in TABLES[who].items():
        if import_one(fbx_name, who, "A_" + asset, dest):
            n += 1
    log("%s 임포트 %d/%d → %s" % (who, n, len(TABLES[who]), dest))


def do_inplace():
    """Hips 의 처음→끝 직선 이동(XY)을 빼서 제자리 동작으로 만든다. 빠진 거리 / 길이 = 그 동작이 보여 주는 속도"""
    L = unreal.AnimationLibrary
    for who, table in TABLES.items():
        dest = "%s/%s/Mixamo" % (ROOT, who)
        for fbx_name, (asset, moving) in table.items():
            if not moving:
                continue
            path = "%s/A_%s" % (dest, asset)
            a = EAL.load_asset(path)
            if not a:
                continue
            frames = L.get_num_frames(a)
            poses = [L.get_bone_pose_for_frame(a, "Hips", f, False) for f in range(frames + 1)]
            first, last = poses[0].translation, poses[-1].translation
            dx, dy = last.x - first.x, last.y - first.y
            length = a.get_play_length()
            speed = (dx * dx + dy * dy) ** 0.5 / max(length, 0.01)
            if abs(dx) < 3.0 and abs(dy) < 3.0:
                log("  %s/%s: 이미 제자리 (이동 %.1f, %.1f)" % (who, asset, dx, dy))
                continue
            pos, rot, scl = [], [], []
            for f, t in enumerate(poses):
                u = f / float(len(poses) - 1)
                v = t.translation
                pos.append(unreal.Vector(v.x - dx * u, v.y - dy * u, v.z))
                rot.append(t.rotation)
                scl.append(t.scale3d)
            ctrl = a.get_editor_property("controller")
            ctrl.set_bone_track_keys("Hips", pos, rot, scl)
            a.set_editor_property("enable_root_motion", False)
            EAL.save_loaded_asset(a, False)
            log("  %s/%s: 제자리로 (빠진 이동 %.0f, %.0f cm, 길이 %.2f초 → 보여 주는 속도 %.0f cm/s)" % (who, asset, dx, dy, length, speed))


def do_retarget(only=None):
    """Mixamo 기본 자세 → 우리 기본 자세로 회전 옮기기 (mixamo_retarget.py). 임포트 직후 한 번만"""
    import importlib
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import mixamo_retarget as MR
    importlib.reload(MR)
    for who, table in TABLES.items():
        if only and who not in only:
            continue
        dest = "%s/%s/Mixamo" % (ROOT, who)
        rig = MR.OurRig(SKEL[who])
        idle_fbx = "Breathing Idle" if who == "Thief" else "Rifle Idle"
        idle = EAL.load_asset("%s/A_%s" % (dest, table[idle_fbx][0]))
        root_c = MR.find_root_c(idle, os.path.join(SRC, idle_fbx + ".fbx"))
        n = 0
        for fbx_name, (asset, _) in table.items():
            a = EAL.load_asset("%s/A_%s" % (dest, asset))
            if a:
                MR.retarget_one(a, os.path.join(SRC, fbx_name + ".fbx"), rig, root_c, log_it=(n == 0))
                n += 1
        log("%s 리타게팅 %d개" % (who, n))


def main(argv):
    try:
        if unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor():
            unreal.log_error("[Mixamo] 플레이 중에는 돌리지 않습니다")
            return
    except Exception:
        pass
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
    if not argv:
        argv = ["all"]
    if argv[0] == "probe":
        fbx_probe(" ".join(argv[1:]) or "Walking")
        return
    if argv[0] == "probeasset":
        probe(EAL.load_asset(argv[1]), float(argv[2]) if len(argv) > 2 else 0.0)
        return
    steps = argv
    if "all" in steps:
        steps = ["thief", "guard", "retarget", "inplace"]
    for s in steps:
        if s == "thief":
            do_import("Thief")
        elif s == "guard":
            do_import("Guard")
        elif s == "retarget":
            do_retarget()
        elif s == "inplace":
            do_inplace()
    log("끝: %s" % steps)


main(sys.argv[1:])
