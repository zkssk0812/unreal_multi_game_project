# -*- coding: utf-8 -*-
# 개발용: 로봇 경비원을 애니메이션별로 나란히 세우고 DA 의 기관총(Guard → Gun Mesh/Bone/Offset)을 손에 붙여 비스듬한 앞에서 본다. 레벨은 저장하지 않는다.
#   py "..._stage_guard.py" [q|top|side]   /   py "..._stage_guard.py" clear
import sys
import unreal

TAG = "VHCharPreview"
GA = "/Game/VaultHeist/Characters/Guard/"
POSES = [("Anims/MM_Idle_VHGuard", 0.5), ("Anims/MF_Rifle_Idle_ADS_VHGuard", 0.2), ("Mixamo/A_Rifle_Idle", 1.0), ("Mixamo/A_Rifle_FireWalk", 0.3), ("Mixamo/A_Rifle_Run", 0.2), ("Mixamo/A_Rifle_Fire", 0.05), ("Mixamo/A_Electrocuted", 0.5)]
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for a in actors.get_all_level_actors():
    if a.actor_has_tag(TAG):
        actors.destroy_actor(a)
args = sys.argv[1:]
if not args or args[0] != "clear":
    view = args[0] if args else "q"
    da = unreal.EditorAssetLibrary.load_asset("/Game/VaultHeist/Weapons/Data/DA_VHCharacterVisuals")
    g = da.get_editor_property("guard")
    mesh = unreal.EditorAssetLibrary.load_asset("/Game/VaultHeist/Characters/Guard/SK_VHGuard")
    gun = unreal.EditorAssetLibrary.load_asset("/Game/VaultHeist/Characters/Guard/SM_VHGuardGun")
    gap = 150.0
    n = len(POSES)
    for i, (name, t) in enumerate(POSES):
        loc = unreal.Vector(0.0, (i - (n - 1) / 2.0) * gap, 0.0)
        a = actors.spawn_actor_from_class(unreal.SkeletalMeshActor, loc, unreal.Rotator(roll=0.0, pitch=0.0, yaw=-90.0))
        smc = a.skeletal_mesh_component
        smc.set_skeletal_mesh_asset(mesh)
        smc.set_animation_mode(unreal.AnimationMode.ANIMATION_SINGLE_NODE)
        smc.set_animation(unreal.EditorAssetLibrary.load_asset(GA + name))
        smc.set_position(t, False)
        smc.set_play_rate(0.0)
        smc.set_update_animation_in_editor(True)
        a.tags = [TAG]
        ga = actors.spawn_actor_from_object(gun, loc, unreal.Rotator(0, 0, 0))
        ga.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
        ga.attach_to_component(smc, str(g.get_editor_property("gun_bone")), unreal.AttachmentRule.SNAP_TO_TARGET, unreal.AttachmentRule.SNAP_TO_TARGET, unreal.AttachmentRule.KEEP_WORLD, False)
        ga.root_component.set_editor_property("relative_location", g.get_editor_property("gun_offset").translation)
        ga.root_component.set_editor_property("relative_rotation", g.get_editor_property("gun_offset").rotation.rotator())
        ga.tags = [TAG]
    span = n * gap
    if view == "top":
        cam, rot = unreal.Vector(0, 0, span * 1.3 + 200), unreal.Rotator(roll=0.0, pitch=-90.0, yaw=0.0)
    elif view == "side":
        cam, rot = unreal.Vector(0, -span * 1.1 - 150, 110), unreal.Rotator(roll=0.0, pitch=0.0, yaw=90.0)
    else:
        d = span * 0.7 + 150
        cam, rot = unreal.Vector(d * 0.75, -d * 0.75, 220), unreal.Rotator(roll=0.0, pitch=-12.0, yaw=135.0)
    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(cam, rot)
    unreal.log("[CharBuild] guard preview %s" % view)
