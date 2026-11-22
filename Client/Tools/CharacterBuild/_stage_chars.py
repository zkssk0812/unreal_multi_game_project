# -*- coding: utf-8 -*-
# 개발용: 캐릭터 메시를 에디터 레벨에 임시로 나란히 세우고 뷰포트 카메라를 앞(또는 위)으로 옮긴다. 레벨은 저장하지 않는다.
#   py "..._stage_chars.py" [front|top|side] [메시경로...]   /   py "..._stage_chars.py" clear
import sys
import unreal

TAG = "VHCharPreview"
DEFAULT = ["/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple",
           "/Game/VaultHeist/Characters/Thief/SK_VHThief",
           "/Game/VaultHeist/Characters/Guard/SK_VHGuard"]
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for a in actors.get_all_level_actors():
    if a.actor_has_tag(TAG):
        actors.destroy_actor(a)
args = sys.argv[1:]
if not args or args[0] != "clear":
    view = args[0] if args and args[0] in ("front", "top", "side", "back", "q") else "front"
    paths = [a for a in args if a.startswith("/Game")] or DEFAULT
    base = unreal.Vector(0.0, 0.0, 0.0)
    for a in actors.get_all_level_actors():
        if isinstance(a, unreal.PlayerStart):
            base = a.get_actor_location() - unreal.Vector(0, 0, 96)
            break
    gap = 140.0
    n = len(paths)
    for i, spec in enumerate(paths):
        # "메시경로" 또는 "메시경로,애니메이션경로,시간(초)" (애니메이션의 그 순간 자세로 멈춤)
        parts = spec.split(",")
        mesh = unreal.EditorAssetLibrary.load_asset(parts[0])
        loc = unreal.Vector(base.x, base.y + (i - (n - 1) / 2.0) * gap, base.z)
        # 메시 앞 = +Y → 액터 Yaw -90 이면 화면 +X 를 본다 (게임의 캐릭터 메시 컴포넌트와 같은 회전)
        actor = actors.spawn_actor_from_class(unreal.SkeletalMeshActor, loc, unreal.Rotator(roll=0.0, pitch=0.0, yaw=-90.0))
        smc = actor.skeletal_mesh_component
        smc.set_skeletal_mesh_asset(mesh)
        if len(parts) > 1:
            anim = unreal.EditorAssetLibrary.load_asset(parts[1])
            smc.set_animation_mode(unreal.AnimationMode.ANIMATION_SINGLE_NODE)
            smc.set_animation(anim)
            smc.set_position(float(parts[2]) if len(parts) > 2 else 0.0, False)
            smc.set_play_rate(0.0)
            smc.set_update_animation_in_editor(True)
        actor.tags = [TAG]
    span = n * gap
    if view == "top":
        cam = unreal.Vector(base.x, base.y, base.z + span * 1.4 + 200)
        rot = unreal.Rotator(roll=0.0, pitch=-90.0, yaw=0.0)
    elif view == "side":
        cam = unreal.Vector(base.x, base.y - span * 1.2 - 150, base.z + 95)
        rot = unreal.Rotator(roll=0.0, pitch=0.0, yaw=90.0)
    elif view == "q":     # 비스듬한 앞 (오른쪽 앞 45도, 살짝 위)
        d = span * 0.75 + 150
        cam = unreal.Vector(base.x + d * 0.75, base.y - d * 0.75, base.z + 200)
        rot = unreal.Rotator(roll=0.0, pitch=-12.0, yaw=135.0)
    elif view == "back":
        cam = unreal.Vector(base.x - span * 0.9 - 150, base.y, base.z + 95)
        rot = unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0)
    else:
        cam = unreal.Vector(base.x + span * 0.9 + 150, base.y, base.z + 95)
        rot = unreal.Rotator(roll=0.0, pitch=0.0, yaw=180.0)
    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(cam, rot)
    unreal.log("[CharBuild] preview %s %d" % (view, n))
