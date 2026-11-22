"""
새 맵 레벨 만들기 (에디터 안에서 실행).

모델러 패키지의 원본 스크립트(SourceArt/Mansion_1F_Modular/Mansion_1F_Build.py)는 "지금 열려 있는 레벨"에
건물·가구·조명을 놓고, 다시 실행하면 그 레벨의 Mansion/ 폴더 액터를 전부 지운다.
그래서 다른 레벨에서 잘못 실행되지 않도록 이 래퍼가
  1) 새 레벨 /Game/VaultHeist/Maps/Mansion/L_Mansion_Shell 을 만들거나(있으면 연다)
  2) 지금 열린 레벨이 정말 그 레벨인지 확인한 뒤
  3) 원본 스크립트를 고치지 않고 그대로 실행하고
  4) 레벨과 /Game/Mansion 에셋을 저장한다.

실행: 에디터 아래쪽 Cmd 칸에
    py "C:/UnrealProjects/MCPTest/Tools/MansionBuild/build_shell_level.py"
  임포트를 건너뛰고 배치만 다시 하려면 끝에 skipimport 를 붙인다.
결과는 출력 로그의 [MansionWrap] 줄.
"""
import os
import sys

import unreal

LEVEL = "/Game/VaultHeist/Maps/Mansion/L_Mansion_Shell"


def _main():
    project = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    script = os.path.normpath(os.path.join(project, "SourceArt", "Mansion_1F_Modular", "Mansion_1F_Build.py"))
    if not os.path.isfile(script):
        unreal.log_error(f"[MansionWrap] 원본 스크립트가 없음: {script}")
        return
    skip_import = any(a.lower() == "skipimport" for a in sys.argv[1:])

    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if unreal.EditorAssetLibrary.does_asset_exist(LEVEL):
        ok = les.load_level(LEVEL)
        unreal.log(f"[MansionWrap] 기존 레벨 열기 {LEVEL}: {ok}")
    else:
        ok = les.new_level(LEVEL)
        unreal.log(f"[MansionWrap] 새 빈 레벨 만들기 {LEVEL}: {ok}")

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    path = world.get_path_name() if world else ""
    if not path.startswith(LEVEL + "."):
        unreal.log_error(f"[MansionWrap] 중단: 지금 열린 레벨이 {LEVEL} 이 아님 ({path})")
        return

    src = open(script, encoding="utf-8").read()
    if skip_import:
        src = src.replace("SKIP_IMPORT = False", "SKIP_IMPORT = True", 1)
    scope = {"__file__": script, "__name__": "__mansion_build__"}
    unreal.log(f"[MansionWrap] 원본 스크립트 실행 시작 (skip_import={skip_import}): {script}")
    exec(compile(src, script, "exec"), scope)

    missing = scope.get("missing", [])
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    count = {}
    for a in actors:
        folder = str(a.get_folder_path())
        if folder.startswith("Mansion/"):
            key = "/".join(folder.split("/")[:2])
            count[key] = count.get(key, 0) + 1
    unreal.log(f"[MansionWrap] 축 보정 CORR_R={scope.get('CORR_R')} CORR_S={scope.get('CORR_S')}")
    unreal.log(f"[MansionWrap] 못 찾은 메시 {len(missing)}개: {missing}")
    unreal.log(f"[MansionWrap] 폴더별 액터 수: {count}")

    saved_level = les.save_current_level()
    saved_assets = unreal.EditorAssetLibrary.save_directory("/Game/Mansion", only_if_is_dirty=True, recursive=True)
    unreal.log(f"[MansionWrap] 완료: 레벨 저장 {saved_level}, 에셋 저장 {saved_assets}, 전체 액터 {len(actors)}개")


try:
    _main()
except Exception as e:  # 실패해도 에디터 로그에 이유가 남게
    import traceback
    unreal.log_error(f"[MansionWrap] 실패: {e}\n{traceback.format_exc()}")
