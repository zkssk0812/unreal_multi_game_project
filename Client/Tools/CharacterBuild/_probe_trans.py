# -*- coding: utf-8 -*-
"""의적 스켈레톤의 모든 애니메이션: Hips 말고 뼈 이동값이 기본 자세와 얼마나 다른가 (Skeleton 이동 모드로 바꿔도 캐릭터1이 그대로인지 확인)"""
import unreal
EAL = unreal.EditorAssetLibrary
L = unreal.AnimationLibrary
APE = unreal.AnimPoseExtensions
def log(m): unreal.log("[Trans] " + m)
SK = "/Game/VaultHeist/Characters/Thief/SK_VHThief_Skeleton"
ref = APE.get_reference_pose(EAL.load_asset(SK))
names = [str(b) for b in APE.get_bone_names(ref)]
reft = {n: APE.get_bone_pose(ref, n, unreal.AnimPoseSpaces.LOCAL).translation for n in names}
worst_all = 0
for p in EAL.list_assets("/Game/VaultHeist/Characters/Thief", recursive=True):
    a = EAL.load_asset(p)
    if not isinstance(a, unreal.AnimSequence):
        continue
    if str(a.get_editor_property("skeleton").get_path_name()).split(".")[0] != SK:
        continue
    nf = L.get_num_frames(a)
    worst, wb = 0.0, ""
    tracks = {str(t).lower() for t in L.get_animation_track_names(a)}
    for n in names:
        if n == "Hips" or n.lower() not in tracks:
            continue
        for f in (0, nf // 2):
            t = L.get_bone_pose_for_frame(a, n, f, False).translation
            d = (t - reft[n]).length()
            if d > worst:
                worst, wb = d, n
    worst_all = max(worst_all, worst)
    if worst > 0.5:
        log("  %s: 최대 %.2fcm (%s)" % (a.get_name(), worst, wb))
log("전체 최대 차이 %.3fcm" % worst_all)
log("끝")
