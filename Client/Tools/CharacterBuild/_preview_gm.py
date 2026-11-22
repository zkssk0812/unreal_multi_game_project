# 개발용: 미리보기 레벨(L_CharPreview)에서 ▶ 하면 게임 캐릭터가 나오게 게임 모드를 게임 레벨과 같게 (넓은 바닥에서 동작 확인)
import unreal
ws = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_world_settings()
ws.set_editor_property("default_game_mode", unreal.load_class(None, "/Game/VaultHeist/Blueprints/BP_VHInteractionTestGameMode.BP_VHInteractionTestGameMode_C"))
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
unreal.log("[PGM] preview game mode set")
