# 개발용: 동작의 머리·오른손 높이와 앞 거리 곡선 (구간 고르기용)
import sys, unreal
APE = unreal.AnimPoseExtensions
EAL = unreal.EditorAssetLibrary
opt = unreal.AnimPoseEvaluationOptions()
step = float(sys.argv[1])
for path in sys.argv[2:]:
    a = EAL.load_asset(path)
    L = a.get_play_length(); t = 0.0; row = []
    while t <= L + 1e-3:
        pose = APE.get_anim_pose_at_time(a, t, opt)
        h = APE.get_bone_pose(pose, "Head", unreal.AnimPoseSpaces.WORLD).translation
        r = APE.get_bone_pose(pose, "RightHand", unreal.AnimPoseSpaces.WORLD).translation
        row.append("%.1f:%d/%d" % (t, h.z, r.z))
        t += step
    unreal.log("[CRV] %s %s" % (a.get_name(), " ".join(row)))
