# -*- coding: utf-8 -*-
"""
마네킹 애니메이션을 새 캐릭터(Mixamo 뼈대) 용으로 리타게팅 (에디터 Cmd 칸: py "C:/UnrealProjects/MCPTest/Tools/CharacterBuild/retarget.py" 단계...)

단계
  rigs    : IK Rig 3개 (마네킹 IK_VHMannequin, IK_VHThief, IK_VHGuard) — 자동 리타게팅 정의(팔·다리·척추 체인)
  rtg     : 리타게터 2개 (RTG_VHMannequin_To_Thief / _To_Guard) — 체인 자동 연결, 대상 T자세 → 마네킹 A자세로 자동 정렬
  anims   : 애니메이션 리타게팅
            의적: ABP_VHThief(그래프 + 참조하는 이동 애니메이션 전부) → ABP_VHThief_Mixamo, 대기·걷기·달리기·쓰러짐
            경비원: 라이플 대기·걷기·달리기·사격
  all     : rigs rtg anims

플레이(PIE) 중에는 돌리지 않는다.
"""
import sys
import unreal

EAL = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
ROOT = "/Game/VaultHeist/Characters"
RIG_DIR = ROOT + "/Retarget"
MANNY = "/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple"
TARGETS = {
    "Thief": "/Game/VaultHeist/Characters/Thief/SK_VHThief",
    "Guard": "/Game/VaultHeist/Characters/Guard/SK_VHGuard",
}
A = "/Game/Characters/Mannequins/Anims"
THIEF_ANIMS = [A + "/Unarmed/MM_Idle", A + "/Unarmed/Walk/MF_Unarmed_Walk_Fwd", A + "/Unarmed/Jog/MF_Unarmed_Jog_Fwd",
               A + "/Death/MM_Death_Front_01", A + "/Death/MM_Death_Back_01"]
THIEF_ABP = "/Game/VaultHeist/Animation/ABP_VHThief"
GUARD_ANIMS = [A + "/Rifle/MF_Rifle_Idle_ADS", A + "/Rifle/Walk/MF_Rifle_Walk_Fwd", A + "/Rifle/Jog/MF_Rifle_Jog_Fwd",
               A + "/Rifle/MM_Rifle_Fire", A + "/Unarmed/MM_Idle", A + "/Death/MM_Death_Front_01"]


def log(msg):
    unreal.log("[CharBuild] " + msg)


def ensure_dir(p):
    if not EAL.does_directory_exist(p):
        EAL.make_directory(p)


def make_rig(name, mesh_path, root_bone):
    path = "%s/%s" % (RIG_DIR, name)
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    rig = TOOLS.create_asset(name, RIG_DIR, unreal.IKRigDefinition, unreal.IKRigDefinitionFactory())
    c = unreal.IKRigController.get_controller(rig)
    c.set_skeletal_mesh(EAL.load_asset(mesh_path))
    c.apply_auto_generated_retarget_definition()
    c.set_retarget_root(root_bone)
    chains = [str(x.get_editor_property("chain_name")) for x in c.get_retarget_chains()]
    EAL.save_loaded_asset(rig, False)
    log("IK Rig %s: 체인 %d개 %s, 루트 %s" % (name, len(chains), chains, c.get_retarget_root()))
    return rig


def do_rigs():
    ensure_dir(RIG_DIR)
    make_rig("IK_VHMannequin", MANNY, "pelvis")
    for n, p in TARGETS.items():
        make_rig("IK_VH%s" % n, p, "Hips")


def do_rtg():
    src = EAL.load_asset(RIG_DIR + "/IK_VHMannequin")
    for n in TARGETS:
        name = "RTG_VHMannequin_To_%s" % n
        path = "%s/%s" % (RIG_DIR, name)
        if EAL.does_asset_exist(path):
            EAL.delete_asset(path)
        rtg = TOOLS.create_asset(name, RIG_DIR, unreal.IKRetargeter, unreal.IKRetargetFactory())
        c = unreal.IKRetargeterController.get_controller(rtg)
        c.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, src)
        c.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, EAL.load_asset("%s/IK_VH%s" % (RIG_DIR, n)))
        c.set_preview_mesh(unreal.RetargetSourceOrTarget.SOURCE, EAL.load_asset(MANNY))
        c.set_preview_mesh(unreal.RetargetSourceOrTarget.TARGET, EAL.load_asset(TARGETS[n]))
        if c.get_num_retarget_ops() == 0:
            c.add_default_ops()
        c.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.SOURCE, src)
        c.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.TARGET, EAL.load_asset("%s/IK_VH%s" % (RIG_DIR, n)))
        # 대상(Mixamo)은 따로 된 뿌리 뼈가 없어 Root Motion 연산이 Hips 에 겹쳐 써서 몸이 뜬다 → 뺀다 (앞으로 나아가는 이동은 strip_drift 로 지움)
        for i in reversed(range(c.get_num_retarget_ops())):
            if str(c.get_op_name(i)) == "Root Motion":
                c.remove_retarget_op(i)
        c.auto_map_chains(unreal.AutoMapChainType.FUZZY, True)
        # 대상은 T자세, 마네킹은 A자세 → 대상의 리타게팅 자세를 마네킹 쪽으로 자동 정렬
        c.create_retarget_pose("VH_Align", unreal.RetargetSourceOrTarget.TARGET)
        c.auto_align_all_bones(unreal.RetargetSourceOrTarget.TARGET)
        c.snap_bone_to_ground("LeftToeBase", unreal.RetargetSourceOrTarget.TARGET) if hasattr(c, "snap_bone_to_ground") else None
        ops = [str(c.get_op_name(i)) for i in range(c.get_num_retarget_ops())]
        EAL.save_loaded_asset(rtg, False)
        log("리타게터 %s: 연산 %s" % (name, ops))


def retarget(assets, n, suffix):
    rtg = EAL.load_asset("%s/RTG_VHMannequin_To_%s" % (RIG_DIR, n))
    out_dir = "%s/%s/Anims" % (ROOT, n)
    ensure_dir(out_dir)
    if not suffix.endswith("_ABP"):
        pass
    datas = []
    for p in assets:
        d = EAL.find_asset_data(p)
        if d.is_valid():
            datas.append(d)
        else:
            unreal.log_warning("[CharBuild] 없음: " + p)
    res = unreal.IKRetargetBatchOperation.duplicate_and_retarget(
        datas, EAL.load_asset(MANNY), EAL.load_asset(TARGETS[n]), rtg,
        "", "", "", suffix, out_dir, False, True, True)
    names = [str(r.asset_name) for r in res]
    log("%s 리타게팅 %d개 → %s: %s" % (n, len(names), out_dir, names))


def strip_drift(n):
    """
    마네킹 걷기·달리기는 루트 모션(앞으로 나아가는 이동)이 있다. 대상은 뿌리 뼈가 없어 그 이동이 Hips 에 들어가므로
    Hips 위치에서 처음→끝 직선 이동(XY)을 빼 제자리 동작으로 만든다 (이동은 게임 코드가 한다). 높이(Z)는 그대로.
    """
    L = unreal.AnimationLibrary
    d = "%s/%s/Anims" % (ROOT, n)
    count = 0
    for path in EAL.list_assets(d, False, False):
        a = EAL.load_asset(path)
        if not isinstance(a, unreal.AnimSequence) or "Death" in a.get_name():
            continue    # 쓰러짐은 실제로 넘어지는 이동이라 그대로 둔다
        frames = L.get_num_frames(a)
        if frames < 2:
            continue
        poses = [L.get_bone_pose_for_frame(a, "Hips", f, False) for f in range(frames)]
        first, last = poses[0].translation, poses[-1].translation
        dx, dy = last.x - first.x, last.y - first.y
        if abs(dx) < 5.0 and abs(dy) < 5.0 and not a.get_editor_property("enable_root_motion"):
            continue
        pos, rot, scl = [], [], []
        for f, t in enumerate(poses):
            u = f / float(frames - 1)
            v = t.translation
            pos.append(unreal.Vector(v.x - dx * u, v.y - dy * u, v.z))
            rot.append(t.rotation)
            scl.append(t.scale3d)
        ctrl = a.get_editor_property("controller")
        ctrl.set_bone_track_keys("Hips", pos, rot, scl)
        a.set_editor_property("enable_root_motion", False)
        EAL.save_loaded_asset(a, False)
        count += 1
        log("  제자리로: %s (빠진 이동 %.0f, %.0f cm)" % (a.get_name(), dx, dy))
    log("%s 제자리 동작 %d개" % (n, count))


def do_anims():
    # 다시 만들 때는 덮어쓴다 (스켈레톤을 새로 만들었다면 Anims 폴더를 에디터에서 먼저 지운다)
    retarget(THIEF_ANIMS + [THIEF_ABP], "Thief", "_VHThief")
    retarget(GUARD_ANIMS, "Guard", "_VHGuard")
    for n in TARGETS:
        strip_drift(n)
    EAL.save_directory(ROOT, only_if_is_dirty=True, recursive=True)


def main(argv):
    try:
        if unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor():
            unreal.log_error("[CharBuild] 플레이 중에는 돌리지 않습니다")
            return
    except Exception:
        pass
    steps = argv or ["all"]
    if "all" in steps:
        steps = ["rigs", "rtg", "anims"]
    for s in steps:
        {"rigs": do_rigs, "rtg": do_rtg, "anims": do_anims}[s]()
    log("리타게팅 끝: %s" % steps)


main(sys.argv[1:])
