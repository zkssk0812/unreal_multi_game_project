# -*- coding: utf-8 -*-
# 개발용: 애니메이션의 한 순간 주요 뼈 위치(컴포넌트 기준)를 로그에 적는다.  py "..._probe_anim.py" 애니메이션경로 [시간]
import sys
import unreal

APE = unreal.AnimPoseExtensions
anim = unreal.EditorAssetLibrary.load_asset(sys.argv[1])
t = float(sys.argv[2]) if len(sys.argv) > 2 else 0.0
opt = unreal.AnimPoseEvaluationOptions()
pose = APE.get_anim_pose_at_time(anim, t, opt)
names = [str(n) for n in APE.get_bone_names(pose)]
unreal.log("[PR] %s t=%.2f 뼈 %d개, 길이 %.2f" % (anim.get_name(), t, len(names), anim.get_play_length()))
for bn in names:
    if bn in ("root", "pelvis", "Hips", "head", "Head", "hand_r", "RightHand", "foot_l", "LeftFoot", "spine_03", "Spine2", "RightArm", "upperarm_r"):
        w = APE.get_bone_pose(pose, bn, unreal.AnimPoseSpaces.WORLD).translation
        l = APE.get_bone_pose(pose, bn, unreal.AnimPoseSpaces.LOCAL).translation
        unreal.log("[PR]   %-12s world (%.1f, %.1f, %.1f) local (%.1f, %.1f, %.1f)" % (bn, w.x, w.y, w.z, l.x, l.y, l.z))
