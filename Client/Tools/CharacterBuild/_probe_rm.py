# -*- coding: utf-8 -*-
# 개발용: 애니메이션의 뿌리 이동(루트 모션) 확인. py "..._probe_rm.py" 경로:뼈 ...
import sys
import unreal

L = unreal.AnimationLibrary
E = unreal.EditorAssetLibrary
for spec in sys.argv[1:]:
    p, b = spec.split(":")
    a = E.load_asset(p)
    n = L.get_num_frames(a)
    vals = []
    for f in (0, n // 4, n // 2, n - 1):
        t = L.get_bone_pose_for_frame(a, b, f, False).translation
        vals.append("(%.1f, %.1f, %.1f)" % (t.x, t.y, t.z))
    unreal.log("[RM] %s %s 프레임 %d 루트모션 %s: %s" % (a.get_name(), b, n, a.get_editor_property("enable_root_motion"), " ".join(vals)))
