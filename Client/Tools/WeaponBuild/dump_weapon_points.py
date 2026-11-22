# -*- coding: utf-8 -*-
"""
플레이(PIE) 중 내 캐릭터가 든 무기의 위치를 기록한다 (무기 모델 교체·소켓 작업 전후 비교용, 2026-11-20).
  Cmd 칸: vh.Weapon N  →  py "C:/UnrealProjects/MCPTest/Tools/WeaponBuild/dump_weapon_points.py" 꼬리표
기록(캐릭터 기준 cm): 무기 메시 위치·회전·배율, 총구/칼끝 자리(소켓 또는 메시 앞 끝), 오른손·왼손 뼈 위치
→ Saved/WeaponBuild/points_<꼬리표>.json 에 무기별로 쌓인다
"""
import json, os, sys
import unreal

tag = sys.argv[1] if len(sys.argv) > 1 else "x"
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
pawn = unreal.GameplayStatics.get_player_controller(world, 0).get_controlled_pawn()
inv = pawn.get_actor_transform()
loc = lambda p: [round(v, 2) for v in (lambda q: (q.x, q.y, q.z))(inv.inverse_transform_location(p))]
mesh_comp = None
for c in pawn.get_components_by_class(unreal.StaticMeshComponent):
    sm = c.get_editor_property("static_mesh")
    if sm and sm.get_name().startswith("SM_VHW_") and c.is_visible():
        mesh_comp = c
skel = pawn.get_components_by_class(unreal.SkeletalMeshComponent)[0]
out = {"rhand": loc(skel.get_socket_location("RightHand")), "lhand": loc(skel.get_socket_location("LeftHand")),
       "rsock": loc(skel.get_socket_location("WeaponSocket_R")), "char_mesh": skel.get_editor_property("skeletal_mesh_asset").get_name() if skel.get_editor_property("skeletal_mesh_asset") else ""}  # rsock = 손 소켓(쥔 주먹 가운데, 메시마다 따로)
if mesh_comp:
    sm = mesh_comp.get_editor_property("static_mesh")
    t = mesh_comp.get_world_transform()
    out["mesh"] = sm.get_name()
    out["pos"] = loc(t.translation)
    r = t.rotation.rotator()
    out["rot"] = [round(r.pitch - inv.rotation.rotator().pitch, 1), round(r.yaw - inv.rotation.rotator().yaw, 1), round(r.roll, 1)]
    out["scale"] = [round(v, 3) for v in (t.scale3d.x, t.scale3d.y, t.scale3d.z)]
    b = sm.get_bounding_box()
    front = unreal.Vector(b.max.x, (b.min.y + b.max.y) / 2, (b.min.z + b.max.z) / 2)
    out["front"] = loc(t.transform_location(front))
    tip = unreal.Vector(b.max.x * 0.9, (b.min.y + b.max.y) / 2, (b.min.z + b.max.z) / 2)
    out["tip90"] = loc(t.transform_location(tip))
    for s in ("Muzzle", "BladeTip", "Grip", "SupportHand"):
        if sm.find_socket(s):
            out["sock_" + s] = loc(mesh_comp.get_socket_location(s))
path = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "WeaponBuild", "points_%s.json" % tag)
data = json.load(open(path, encoding="utf-8")) if os.path.exists(path) else {}
data[out.get("mesh", "none")] = out
json.dump(data, open(path, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
unreal.log("[Points] %s %s" % (tag, out))
