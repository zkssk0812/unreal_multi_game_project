# 개발용: 스켈레톤 기본 자세(로컬·월드) + 애니메이션 첫 프레임 로컬 회전을 JSON 으로.  py "..._dump_ref.py" 스켈레톤 출력.json [애니메이션...]
import sys, json, unreal
APE = unreal.AnimPoseExtensions
EAL = unreal.EditorAssetLibrary
sk = EAL.load_asset(sys.argv[1])
ref = APE.get_reference_pose(sk)
out = {"ref": {}, "anims": {}}
def q(t):
    r = t.rotation; v = t.translation
    return [r.x, r.y, r.z, r.w, v.x, v.y, v.z]
for bn in APE.get_bone_names(ref):
    out["ref"][str(bn)] = {"local": q(APE.get_bone_pose(ref, bn, unreal.AnimPoseSpaces.LOCAL)),
                           "world": q(APE.get_bone_pose(ref, bn, unreal.AnimPoseSpaces.WORLD)),
                           "parent": str(APE.get_parent_bone_name(ref, bn)) if hasattr(APE, "get_parent_bone_name") else ""}
opt = unreal.AnimPoseEvaluationOptions()
for ap in sys.argv[3:]:
    a = EAL.load_asset(ap)
    pose = APE.get_anim_pose_at_time(a, 0.0, opt)
    out["anims"][a.get_name()] = {str(bn): q(APE.get_bone_pose(pose, bn, unreal.AnimPoseSpaces.LOCAL)) for bn in APE.get_bone_names(pose)}
open(sys.argv[2], "w").write(json.dumps(out))
unreal.log("[DUMP] %d bones" % len(out["ref"]))
