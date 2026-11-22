# -*- coding: utf-8 -*-
"""
Mixamo 동작 → 몽타주 + DA_VHCharacterVisuals 연결 (에디터 Cmd 칸: py "C:/UnrealProjects/MCPTest/Tools/CharacterBuild/setup_body_anims.py" 단계...)

단계
  montages : /Game/VaultHeist/Characters/Thief/Mixamo/Montages 에 AM_* 몽타주 (슬롯 DefaultSlot, 섞기 시간 지정)
  data     : DA_VHCharacterVisuals
             - Default: Anim Class = VHThiefAnimInstance(그래프 없이 블렌드스페이스 + 몽타주 슬롯을 코드가 섞음),
               Locomotion Blend Space, Idle/Walk/Run(블렌드스페이스가 없을 때 예비), Body Motion 몽타주 7칸, Down Anim(Dying),
               Roll Motion Montage(Stand To Roll)
             - Guard: Mixamo 라이플 대기·이동 사격·달리기·사격·감전
  all      : montages data

앞의 준비 단계: import_mixamo.py all → make_blendspaces.py. 플레이 중에는 돌리지 않는다.
"""
import sys
import unreal

EAL = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
TM = "/Game/VaultHeist/Characters/Thief/Mixamo"
GM = "/Game/VaultHeist/Characters/Guard/Mixamo"
MDIR = TM + "/Montages"
DA = "/Game/VaultHeist/Weapons/Data/DA_VHCharacterVisuals"

# 몽타주 이름 → (애니메이션, 섞여 들어오기·나가기 초)
MONTAGES = {
    "AM_HitReact": ("A_HitReact", 0.08, 0.15),
    "AM_PickUp": ("A_PickUp", 0.12, 0.2),
    "AM_Drink": ("A_Drinking", 0.15, 0.25),
    "AM_Throw": ("A_Throw", 0.1, 0.2),
    "AM_KneelDown": ("A_KneelDown", 0.2, 0.25),
    "AM_KneelInspect": ("A_KneelInspect", 0.25, 0.3),
    "AM_Electrocuted": ("A_Electrocuted", 0.1, 0.25),
    "AM_Roll": ("A_Roll", 0.05, 0.1),
}

# DA 칸 → (몽타주, 상체만, 팔 IK 풀기, 재생 빠르기, 시작 초, 길이 초)  — 구간은 동작의 머리·손 높이 곡선(_mx_curve.py)으로 골랐다
BODY = {
    "hit_react_montage": ("AM_HitReact", True, False, 1.0, 0.0, 0.0),      # 0.43초, 무기는 든 채로 움찔
    "pick_up_montage": ("AM_PickUp", True, True, 4.0, 0.8, 4.4),           # 숙였다 일어서기 0.8~5.2초 → 1.1초
    "drink_montage": ("AM_Drink", True, True, 2.0, 2.0, 2.8),              # 마시는 구간 2.0~4.8초 → 1.4초
    "throw_montage": ("AM_Throw", True, True, 1.8, 0.0, 1.7),              # 던지기 0~1.7초 → 0.95초
    "interact_start_montage": ("AM_KneelDown", False, True, 1.6, 0.6, 0.0),  # 무릎 꿇기 → 약 1.4초
    "interact_loop_montage": ("AM_KneelInspect", False, True, 1.0, 0.0, 0.0),
    "stunned_montage": ("AM_Electrocuted", False, True, 1.2, 0.0, 0.0),
}


def log(m):
    unreal.log("[BodyAnim] " + m)


def in_pie():
    try:
        return unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    except Exception:
        return False


def do_montages():
    if not EAL.does_directory_exist(MDIR):
        EAL.make_directory(MDIR)
    for name, (anim, bin_, bout) in MONTAGES.items():
        path = "%s/%s" % (MDIR, name)
        seq = EAL.load_asset("%s/%s" % (TM, anim))
        if not seq:
            unreal.log_warning("[BodyAnim] 없음 " + anim)
            continue
        m = EAL.load_asset(path) if EAL.does_asset_exist(path) else None
        if m is None:
            f = unreal.AnimMontageFactory()
            f.set_editor_property("source_animation", seq)
            f.set_editor_property("target_skeleton", seq.get_editor_property("skeleton"))
            m = TOOLS.create_asset(name, MDIR, unreal.AnimMontage, f)
        try:
            b = m.get_editor_property("blend_in")
            b.import_text("(BlendTime=%f)" % bin_)
            m.set_editor_property("blend_in", b)
            b = m.get_editor_property("blend_out")
            b.import_text("(BlendTime=%f)" % bout)
            m.set_editor_property("blend_out", b)
            m.set_editor_property("enable_auto_blend_out", True)
        except Exception as e:
            unreal.log_warning("[BodyAnim] 섞기 시간 실패 %s: %s" % (name, e))
        EAL.save_loaded_asset(m, False)
        log("%s ← %s (%.2f초, 들어오기 %.2f 나가기 %.2f)" % (name, anim, m.get_play_length(), bin_, bout))


def body_entry(struct, spec):
    mont, upper, release, rate, start, dur = spec
    struct.set_editor_property("montage", EAL.load_asset("%s/%s" % (MDIR, mont)))
    struct.set_editor_property("upper_body_only", upper)
    struct.set_editor_property("release_arms", release)
    struct.set_editor_property("play_rate", rate)
    struct.set_editor_property("start_time", start)
    struct.set_editor_property("duration", dur)
    return struct


def do_data():
    da = EAL.load_asset(DA)
    d = da.get_editor_property("default")
    d.set_editor_property("anim_class", unreal.load_class(None, "/Script/MCPTest.VHThiefAnimInstance"))
    d.set_editor_property("locomotion_blend_space", EAL.load_asset(TM + "/BS_VHThief_Locomotion"))
    d.set_editor_property("idle_anim", EAL.load_asset(TM + "/A_Idle"))
    d.set_editor_property("walk_anim", EAL.load_asset(TM + "/A_Walk_Fwd"))
    d.set_editor_property("run_anim", EAL.load_asset(TM + "/A_Run_Fwd"))
    d.set_editor_property("upper_body_bone", "Spine")
    for field, spec in BODY.items():
        d.set_editor_property(field, body_entry(d.get_editor_property(field), spec))
    # 쓰러짐: Mixamo Dying (비틀 1.4초 → 쓰러짐 2.8초). 2배 빠르기로 1.6초 보이면 바닥까지 쓰러진다
    d.set_editor_property("down_anim", EAL.load_asset(TM + "/A_Dying"))
    d.set_editor_property("down_play_rate", 2.0)
    d.set_editor_property("down_show_seconds", 1.6)
    # 구르기: Stand To Roll (0.25초부터 = 웅크리기부터, 나머지 2.1초를 구르기 시간 0.5초에)
    roll = d.get_editor_property("roll_motion")
    roll.set_editor_property("montage", EAL.load_asset(MDIR + "/AM_Roll"))
    roll.set_editor_property("montage_start_time", 0.25)
    d.set_editor_property("roll_motion", roll)
    da.set_editor_property("default", d)

    g = da.get_editor_property("guard")
    g.set_editor_property("idle_anim", EAL.load_asset(GM + "/A_Rifle_Idle"))
    g.set_editor_property("walk_anim", EAL.load_asset(GM + "/A_Rifle_FireWalk"))
    g.set_editor_property("run_anim", EAL.load_asset(GM + "/A_Rifle_Run"))
    g.set_editor_property("attack_anim", EAL.load_asset(GM + "/A_Rifle_Fire"))
    g.set_editor_property("stun_anim", EAL.load_asset(GM + "/A_Electrocuted"))
    # 순찰 210 · 추적 420 (서버 GameData). 걷기 → 달리기 280 cm/s 부터, 애니메이션이 보여 주는 속도(측정)
    g.set_editor_property("run_speed", 280.0)
    g.set_editor_property("walk_anim_speed", 160.0)
    g.set_editor_property("run_anim_speed", 430.0)
    da.set_editor_property("guard", g)
    EAL.save_loaded_asset(da, False)
    log("DA 연결 끝 (기본 칸·경비원)")


def main(argv):
    if in_pie():
        unreal.log_error("[BodyAnim] 플레이 중에는 돌리지 않습니다")
        return
    steps = argv or ["all"]
    if "all" in steps:
        steps = ["montages", "data"]
    for s in steps:
        {"montages": do_montages, "data": do_data}[s]()


main(sys.argv[1:])
