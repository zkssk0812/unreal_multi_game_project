# 개발용: 무기 메시를 에디터 레벨에 임시로 띄우고 에디터 뷰포트 카메라를 그 무기 옆(지뢰는 비스듬히 위)으로 옮긴다. 레벨은 저장하지 않는다.
#   py "..._stage_preview.py" /Game/.../SM_X [top]   /   py "..._stage_preview.py" clear
import sys
import unreal

TAG = "VHWPreview"
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for a in actors.get_all_level_actors():
    if a.actor_has_tag(TAG):
        actors.destroy_actor(a)
if len(sys.argv) > 1 and sys.argv[1] != "clear":
    mesh = unreal.EditorAssetLibrary.load_asset(sys.argv[1])
    base = unreal.Vector(0.0, 0.0, 0.0)
    for a in actors.get_all_level_actors():
        if isinstance(a, unreal.PlayerStart):
            base = a.get_actor_location()
            break
    loc = unreal.Vector(base.x, base.y, base.z + 120.0)
    actor = actors.spawn_actor_from_object(mesh, loc, unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0))
    actor.tags = [TAG]
    box = mesh.get_bounding_box()
    c = (box.min + box.max) * 0.5
    size = box.max - box.min
    span = max(size.x, size.z)
    if len(sys.argv) > 2 and sys.argv[2] == "top":     # 지뢰: 앞쪽 위에서 비스듬히
        span = max(size.x, size.y)
        cam = unreal.Vector(loc.x + c.x, loc.y + c.y + span * 0.75, loc.z + c.z + span * 0.9)
        rot = unreal.Rotator(roll=0.0, pitch=-50.0, yaw=-90.0)
    else:
        cam = unreal.Vector(loc.x + c.x, loc.y + c.y + span * 1.25, loc.z + c.z)
        rot = unreal.Rotator(roll=0.0, pitch=0.0, yaw=-90.0)
    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(cam, rot)
    unreal.log("[WeaponBuild] preview %s span %.1f" % (mesh.get_name(), span))
