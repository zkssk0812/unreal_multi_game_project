# -*- coding: utf-8 -*-
"""
의적 이동 블렌드스페이스 만들기 (에디터 Cmd 칸: py "C:/UnrealProjects/MCPTest/Tools/CharacterBuild/make_blendspaces.py")

BS_VHThief_Locomotion (/Game/VaultHeist/Characters/Thief/Mixamo)
  가로 Direction −180~180° (몸이 보는 쪽 기준 이동 방향: 0 앞 · 90 오른쪽 · −90 왼쪽 · ±180 뒤, 끝이 이어짐)
  세로 Speed 0~600 cm/s (서버 GameData: 걷기 300 · 달리기 600)
  샘플: 속도 0 = 대기(Breathing Idle), 300 = 걷기 앞/뒤/좌/우, 600 = 달리기 앞/뒤/좌/우
  샘플 재생 빠르기(Rate Scale) = 게임 속도 ÷ 애니메이션이 보여 주는 걸음 속도 (발이 땅에 붙어 미끄러지는 속도를 잰 값, _mx_speed.py)
  → 걸음이 실제 이동 속도와 맞는다. 블렌드스페이스는 UVHThiefAnimInstance 가 직접 평가한다(내 캐릭터·다른 플레이어 같은 코드).

파이썬에는 블렌드스페이스 샘플 함수가 없어 속성(blend_parameters, sample_data)을 텍스트로 넣는다. 플레이 중에는 돌리지 않는다.
"""
import unreal

EAL = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
DIR = "/Game/VaultHeist/Characters/Thief/Mixamo"
NAME = "BS_VHThief_Locomotion"
SKEL = "/Game/VaultHeist/Characters/Thief/SK_VHThief_Skeleton"
WALK, RUN = 300.0, 600.0
# 애니메이션 → (방향, 속도 칸, 애니메이션이 보여 주는 속도 cm/s — 2026-11-18 측정, 의적 비율로 옮긴 뒤)
SAMPLES = [
    ("A_Idle", [-180, -90, 0, 90, 180], 0.0, 0.0),
    ("A_Walk_Fwd", [0], WALK, 186.0),
    ("A_Walk_Right", [90], WALK, 183.0),
    ("A_Walk_Left", [-90], WALK, 179.0),
    ("A_Walk_Bwd", [-180, 180], WALK, 150.0),
    ("A_Run_Fwd", [0], RUN, 481.0),
    ("A_Run_Right", [90], RUN, 460.0),
    ("A_Run_Left", [-90], RUN, 455.0),
    ("A_Run_Bwd", [-180, 180], RUN, 324.0),
]


def main():
    try:
        if unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor():
            unreal.log_error("[BS] 플레이 중에는 돌리지 않습니다")
            return
    except Exception:
        pass
    path = "%s/%s" % (DIR, NAME)
    bs = EAL.load_asset(path) if EAL.does_asset_exist(path) else None
    if bs is None:
        f = unreal.BlendSpaceFactoryNew()
        f.set_editor_property("target_skeleton", EAL.load_asset(SKEL))
        bs = TOOLS.create_asset(NAME, DIR, unreal.BlendSpace, f)
    params = list(bs.get_editor_property("blend_parameters"))
    params[0].import_text('(DisplayName="Direction",Min=-180.000000,Max=180.000000,GridNum=4,bSnapToGrid=False,bWrapInput=True)')
    params[1].import_text('(DisplayName="Speed",Min=0.000000,Max=%f,GridNum=4,bSnapToGrid=False,bWrapInput=False)' % RUN)
    bs.set_editor_property("blend_parameters", params)
    samples = []
    for anim, dirs, speed, anim_speed in SAMPLES:
        a = EAL.load_asset("%s/%s" % (DIR, anim))
        if not a:
            unreal.log_warning("[BS] 없음: " + anim)
            continue
        rate = speed / anim_speed if anim_speed > 0 else 1.0
        for d in dirs:
            s = unreal.BlendSample()
            s.import_text('(Animation="%s",SampleValue=(X=%f,Y=%f,Z=0.000000),RateScale=%f,bIsValid=True)' % (a.get_path_name(), d, speed, rate))
            samples.append(s)
    bs.set_editor_property("sample_data", samples)
    # 방향이 바뀔 때 무게를 부드럽게 (초당)
    try:
        ip = list(bs.get_editor_property("interpolation_param"))
        ip[0].import_text("(InterpolationTime=0.150000,InterpolationType=BSIT_SpringDamper)")
        ip[1].import_text("(InterpolationTime=0.150000,InterpolationType=BSIT_SpringDamper)")
        bs.set_editor_property("interpolation_param", ip)
    except Exception as e:
        unreal.log_warning("[BS] interpolation_param 실패: %s" % e)
    # 속성만 넣으면 삼각형 분할(BlendSpaceData)이 만들어지지 않아 게임에서 T자세가 된다 → 편집 창을 한 번 열면 다시 계산된다 → 저장 → 닫기
    editors = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)
    editors.open_editor_for_assets([bs])
    EAL.save_loaded_asset(bs, False)
    editors.close_all_editors_for_asset(bs)
    got = bs.get_editor_property("sample_data")
    unreal.log("[BS] %s 샘플 %d개 저장 (첫 샘플: %s)" % (path, len(got), got[0].export_text() if got else "-"))


main()
