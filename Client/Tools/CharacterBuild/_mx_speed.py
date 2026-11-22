# 개발용: 제자리 이동 애니메이션이 보여 주는 속도(발이 땅에 붙은 동안 뒤로 미끄러지는 속도)와 방향을 잰다
import sys, math, unreal
APE = unreal.AnimPoseExtensions
EAL = unreal.EditorAssetLibrary
opt = unreal.AnimPoseEvaluationOptions()
for path in sys.argv[1:]:
    a = EAL.load_asset(path)
    n = unreal.AnimationLibrary.get_num_frames(a)
    L = a.get_play_length()
    dt = L / n
    feet = {"LeftToeBase": [], "RightToeBase": []}
    for f in range(n + 1):
        pose = APE.get_anim_pose_at_time(a, f * dt, opt)
        for b in feet:
            feet[b].append(APE.get_bone_pose(pose, b, unreal.AnimPoseSpaces.WORLD).translation)
    vs = []
    for b, P in feet.items():
        zmin = min(p.z for p in P)
        for f in range(n):
            if P[f].z < zmin + 2.5 and P[f + 1].z < zmin + 2.5:
                vs.append(((P[f + 1].x - P[f].x) / dt, (P[f + 1].y - P[f].y) / dt))
    if not vs:
        unreal.log("[SPD] %s: 발 접지 없음" % a.get_name()); continue
    mags = sorted(math.hypot(*v) for v in vs)
    mx = sum(v[0] for v in vs) / len(vs); my = sum(v[1] for v in vs) / len(vs)
    unreal.log("[SPD] %-16s 길이 %.2f 속도 %.0f cm/s (중앙값), 발 미끄러짐 평균 방향 (%.0f, %.0f) → 몸 이동 (%.0f, %.0f)" % (a.get_name(), L, mags[len(mags) // 2], mx, my, -mx, -my))
