# -*- coding: utf-8 -*-
"""
Pistol and Rifle Locomotion Animations 1700 (Fab, UEFN 마네킹 기준) → 의적 이동 블렌드스페이스 2개 (권총형·라이플형)
에디터 Cmd 칸: py "C:/UnrealProjects/MCPTest/Tools/CharacterBuild/import_pistolrifle.py" 단계...

원본: SourceArt/Animations/PistolRifle/gaspfix.zip (Git 제외) 에서 필요한 FBX 35개만 GaspFix/ 로 풂
     (권총·라이플 × 걷기·달리기 8방향 + 서 있기 + UEFN 마네킹 메시). 판매자 안내대로 "Add to Project" 없이 FBX 임포트.

단계
  mesh     : UEFN 마네킹 메시 임포트 → /Game/External/PistolRifle/Mannequin (스켈레톤 생성)
  anims    : 34개 애니메이션을 그 스켈레톤으로 임포트 → /Game/External/PistolRifle/Source, 걸음 속도(cm/s)를 로그
  rig      : IK Rig (UEFN 마네킹, 자동 리타게팅 정의) + 리타게터 UEFN → 의적 (retarget.py 와 같은 설정)
  retarget : 의적 뼈대로 옮김 → /Game/VaultHeist/Characters/Thief/PistolRifle (_VHThief), 제자리 동작으로
  bs       : 블렌드스페이스 BS_VHThief_Pistol / BS_VHThief_Rifle (가로 이동 방향 −180~180, 세로 속도 0/300/600)
  data     : DA_VHCharacterVisuals 기본 칸의 Pistol/Rifle Locomotion 에 넣음
  all      : 위 전부
플레이(PIE) 중에는 돌리지 않는다. 라이선스·출처: Docs/외부에셋_출처.md
"""
import json
import os
import sys
import unreal

EAL = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SRC = os.path.join(PROJ, "SourceArt", "Animations", "PistolRifle", "GaspFix")
EXT = "/Game/External/PistolRifle"
MESH_DIR = EXT + "/Mannequin"
SRC_DIR = EXT + "/Source"
OUT_DIR = "/Game/VaultHeist/Characters/Thief/PistolRifle"
RIG_DIR = "/Game/VaultHeist/Characters/Retarget"
THIEF = "/Game/VaultHeist/Characters/Thief/SK_VHThief"
DATA = "/Game/VaultHeist/Weapons/Data/DA_VHCharacterVisuals"
SPEED_JSON = os.path.join(PROJ, "Tools", "CharacterBuild", "pistolrifle_speeds.json")
WALK, RUN = 300.0, 600.0
DIRS = {"F": [0], "FR": [45], "RR": [90], "BR": [135], "B": [-180, 180], "BL": [-135], "LL": [-90], "FL": [-45]}
KINDS = ("Pistol", "Rifle")


def log(m):
    unreal.log("[PistolRifle] " + m)


def ensure_dir(p):
    if not EAL.does_directory_exist(p):
        EAL.make_directory(p)


def files():
    out = []
    for k in KINDS:
        for m in ("Walk", "Run"):
            for d in DIRS:
                out.append((k, "_Fixed%s/%s/M_Neutral_%s_Loop_%s_%s.FBX" % (k, m, m, d, k), "%s_%s_%s" % (k, m, d)))
        out.append((k, "_Fixed%s/Idle/M_Neutral_Stand_Idle_Loop_%s.FBX" % (k, k), "%s_Idle" % k))
    return out


def legacy_fbx():
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")


def do_mesh():
    legacy_fbx()
    ensure_dir(MESH_DIR)
    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", True)
    ui.set_editor_property("import_as_skeletal", True)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    ui.set_editor_property("import_animations", False)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", os.path.join(SRC, "Characters", "UEFN_Mannequin", "Meshes", "SKM_UEFN_Mannequin.FBX"))
    t.set_editor_property("destination_path", MESH_DIR)
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("save", True)
    t.set_editor_property("options", ui)
    TOOLS.import_asset_tasks([t])
    log("메시: %s" % [str(p) for p in t.get_editor_property("imported_object_paths")])


def mesh_and_skeleton():
    mesh = EAL.load_asset(MESH_DIR + "/SKM_UEFN_Mannequin")
    return mesh, mesh.get_editor_property("skeleton") if mesh else None


def do_anims():
    legacy_fbx()
    ensure_dir(SRC_DIR)
    mesh, skel = mesh_and_skeleton()
    speeds = {}
    for kind, rel, name in files():
        fbx = os.path.join(SRC, rel.replace("/", os.sep))
        if not os.path.exists(fbx):
            unreal.log_warning("[PistolRifle] 파일 없음: " + fbx)
            continue
        ui = unreal.FbxImportUI()
        ui.set_editor_property("import_mesh", False)
        ui.set_editor_property("import_as_skeletal", True)
        ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_ANIMATION)
        ui.set_editor_property("import_animations", True)
        ui.set_editor_property("import_materials", False)
        ui.set_editor_property("import_textures", False)
        ui.set_editor_property("skeleton", skel)
        ad = ui.get_editor_property("anim_sequence_import_data")
        ad.set_editor_property("animation_length", unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
        ad.set_editor_property("import_bone_tracks", True)
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", fbx)
        t.set_editor_property("destination_path", SRC_DIR)
        t.set_editor_property("destination_name", "A_" + name)
        t.set_editor_property("automated", True)
        t.set_editor_property("replace_existing", True)
        t.set_editor_property("save", True)
        t.set_editor_property("options", ui)
        TOOLS.import_asset_tasks([t])
        a = EAL.load_asset("%s/A_%s" % (SRC_DIR, name))
        if not isinstance(a, unreal.AnimSequence):
            unreal.log_warning("[PistolRifle] 임포트 실패: " + name)
            continue
        # 걸음 속도: root 뼈가 처음→끝 이동한 거리 ÷ 길이 (블렌드스페이스 재생 빠르기 계산용)
        L = unreal.AnimationLibrary
        n = L.get_num_frames(a)
        p0 = L.get_bone_pose_for_frame(a, "root", 0, False).translation
        p1 = L.get_bone_pose_for_frame(a, "root", n - 1, False).translation
        length = a.get_play_length()
        speeds[name] = round(((p1.x - p0.x) ** 2 + (p1.y - p0.y) ** 2) ** 0.5 / max(length, 0.01), 1)
    with open(SPEED_JSON, "w", encoding="utf-8") as fh:
        json.dump(speeds, fh, indent=1)
    log("애니메이션 %d개, 걸음 속도 %s" % (len(speeds), speeds))


def do_rig():
    ensure_dir(RIG_DIR)
    mesh, _ = mesh_and_skeleton()
    name = "IK_VHUEFNMannequin"
    path = "%s/%s" % (RIG_DIR, name)
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    rig = TOOLS.create_asset(name, RIG_DIR, unreal.IKRigDefinition, unreal.IKRigDefinitionFactory())
    c = unreal.IKRigController.get_controller(rig)
    c.set_skeletal_mesh(mesh)
    c.apply_auto_generated_retarget_definition()
    c.set_retarget_root("pelvis")
    EAL.save_loaded_asset(rig, False)
    log("IK Rig 체인 %s" % [str(x.get_editor_property("chain_name")) for x in c.get_retarget_chains()])
    rname = "RTG_VHUEFN_To_Thief"
    rpath = "%s/%s" % (RIG_DIR, rname)
    if EAL.does_asset_exist(rpath):
        EAL.delete_asset(rpath)
    rtg = TOOLS.create_asset(rname, RIG_DIR, unreal.IKRetargeter, unreal.IKRetargetFactory())
    rc = unreal.IKRetargeterController.get_controller(rtg)
    tgt = EAL.load_asset(RIG_DIR + "/IK_VHThief")
    rc.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, rig)
    rc.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, tgt)
    rc.set_preview_mesh(unreal.RetargetSourceOrTarget.SOURCE, mesh)
    rc.set_preview_mesh(unreal.RetargetSourceOrTarget.TARGET, EAL.load_asset(THIEF))
    if rc.get_num_retarget_ops() == 0:
        rc.add_default_ops()
    rc.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.SOURCE, rig)
    rc.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.TARGET, tgt)
    for i in reversed(range(rc.get_num_retarget_ops())):
        if str(rc.get_op_name(i)) == "Root Motion":
            rc.remove_retarget_op(i)    # 대상(Mixamo 뼈대)은 뿌리 뼈가 없어 Hips 에 겹쳐 써서 몸이 뜬다 (retarget.py 와 같음)
    rc.auto_map_chains(unreal.AutoMapChainType.FUZZY, True)
    rc.create_retarget_pose("VH_Align", unreal.RetargetSourceOrTarget.TARGET)
    rc.auto_align_all_bones(unreal.RetargetSourceOrTarget.TARGET)
    EAL.save_loaded_asset(rtg, False)
    log("리타게터 %s: 연산 %s" % (rname, [str(rc.get_op_name(i)) for i in range(rc.get_num_retarget_ops())]))


def do_retarget():
    ensure_dir(OUT_DIR)
    mesh, _ = mesh_and_skeleton()
    rtg = EAL.load_asset(RIG_DIR + "/RTG_VHUEFN_To_Thief")
    datas = [EAL.find_asset_data("%s/A_%s" % (SRC_DIR, n)) for _, _, n in files()]
    datas = [d for d in datas if d.is_valid()]
    res = unreal.IKRetargetBatchOperation.duplicate_and_retarget(datas, mesh, EAL.load_asset(THIEF), rtg, "", "", "", "_VHThief", OUT_DIR, False, True, True)
    log("리타게팅 %d개 → %s" % (len(res), OUT_DIR))
    # 제자리로: Hips 의 처음→끝 직선 이동(XY)을 뺀다 (retarget.py strip_drift 와 같은 방법)
    L = unreal.AnimationLibrary
    for path in EAL.list_assets(OUT_DIR, False, False):
        a = EAL.load_asset(path)
        if not isinstance(a, unreal.AnimSequence):
            continue
        frames = L.get_num_frames(a)
        poses = [L.get_bone_pose_for_frame(a, "Hips", f, False) for f in range(frames)]
        dx = poses[-1].translation.x - poses[0].translation.x
        dy = poses[-1].translation.y - poses[0].translation.y
        pos = [unreal.Vector(t.translation.x - dx * f / max(frames - 1, 1), t.translation.y - dy * f / max(frames - 1, 1), t.translation.z) for f, t in enumerate(poses)]
        a.get_editor_property("controller").set_bone_track_keys("Hips", pos, [t.rotation for t in poses], [t.scale3d for t in poses])
        a.set_editor_property("enable_root_motion", False)
        EAL.save_loaded_asset(a, False)
    log("제자리 동작으로 바꿈")


def do_bs():
    with open(SPEED_JSON, encoding="utf-8") as fh:
        speeds = json.load(fh)
    editors = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)
    for kind in KINDS:
        name = "BS_VHThief_%s" % kind
        path = "%s/%s" % (OUT_DIR, name)
        bs = EAL.load_asset(path) if EAL.does_asset_exist(path) else None
        if bs is None:
            f = unreal.BlendSpaceFactoryNew()
            f.set_editor_property("target_skeleton", EAL.load_asset(THIEF).get_editor_property("skeleton"))
            bs = TOOLS.create_asset(name, OUT_DIR, unreal.BlendSpace, f)
        params = list(bs.get_editor_property("blend_parameters"))
        params[0].import_text('(DisplayName="Direction",Min=-180.000000,Max=180.000000,GridNum=8,bSnapToGrid=False,bWrapInput=True)')
        params[1].import_text('(DisplayName="Speed",Min=0.000000,Max=%f,GridNum=4,bSnapToGrid=False,bWrapInput=False)' % RUN)
        bs.set_editor_property("blend_parameters", params)
        samples = []
        idle = EAL.load_asset("%s/A_%s_Idle_VHThief" % (OUT_DIR, kind))
        for d in [-180, -135, -90, -45, 0, 45, 90, 135, 180]:
            s = unreal.BlendSample()
            s.import_text('(Animation="%s",SampleValue=(X=%f,Y=0.000000,Z=0.000000),RateScale=1.000000,bIsValid=True)' % (idle.get_path_name(), d))
            samples.append(s)
        for m, speed in (("Walk", WALK), ("Run", RUN)):
            for dn, angles in DIRS.items():
                a = EAL.load_asset("%s/A_%s_%s_%s_VHThief" % (OUT_DIR, kind, m, dn))
                if not a:
                    unreal.log_warning("[PistolRifle] 없음 %s %s %s" % (kind, m, dn))
                    continue
                anim_speed = speeds.get("%s_%s_%s" % (kind, m, dn), 0.0) * 0.95    # 의적이 마네킹보다 조금 작다 (골반 높이 비율 근사)
                rate = speed / anim_speed if anim_speed > 1.0 else 1.0
                for ang in angles:
                    s = unreal.BlendSample()
                    s.import_text('(Animation="%s",SampleValue=(X=%f,Y=%f,Z=0.000000),RateScale=%f,bIsValid=True)' % (a.get_path_name(), ang, speed, rate))
                    samples.append(s)
        bs.set_editor_property("sample_data", samples)
        ip = list(bs.get_editor_property("interpolation_param"))
        ip[0].import_text("(InterpolationTime=0.150000,InterpolationType=BSIT_SpringDamper)")
        ip[1].import_text("(InterpolationTime=0.150000,InterpolationType=BSIT_SpringDamper)")
        bs.set_editor_property("interpolation_param", ip)
        EAL.save_loaded_asset(bs, False)
        # 편집 창을 한 번 열어야 삼각형 분할이 저장된다 (make_blendspaces.py 와 같음)
        editors.open_editor_for_assets([bs])
        editors.close_all_editors_for_asset(bs)
        EAL.save_loaded_asset(bs, False)
        log("%s 샘플 %d개" % (name, len(samples)))


def do_data():
    da = EAL.load_asset(DATA)
    d = da.get_editor_property("default")
    d.set_editor_property("pistol_locomotion", EAL.load_asset(OUT_DIR + "/BS_VHThief_Pistol"))
    d.set_editor_property("rifle_locomotion", EAL.load_asset(OUT_DIR + "/BS_VHThief_Rifle"))
    da.set_editor_property("default", d)
    EAL.save_loaded_asset(da, False)
    log("DA: 권총형(권총·테이저)·라이플형(화염방사기·레일건) 이동 블렌드스페이스 연결")


def main(argv):
    if unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor():
        unreal.log_error("[PistolRifle] 플레이 중에는 돌리지 않습니다")
        return
    steps = argv or ["all"]
    if "all" in steps:
        steps = ["mesh", "anims", "rig", "retarget", "bs", "data"]
    for s in steps:
        {"mesh": do_mesh, "anims": do_anims, "rig": do_rig, "retarget": do_retarget, "bs": do_bs, "data": do_data}[s]()


main(sys.argv[1:])
