# -*- coding: utf-8 -*-
"""
Mixamo 애니메이션 → 우리 뼈대(Tripo, 이름은 Mixamo 와 같음) 리타게팅 계산 (import_mixamo.py 의 retarget 단계가 부른다)

왜: 우리 뼈대는 Mixamo 와 뼈 이름은 같지만 뼈 자기 축 방향(어깨·목·허벅지 등)이 달라 회전을 그대로 넣으면 몸이 뒤로 젖혀진다.
방법: 두 뼈대의 기본 자세(둘 다 T자세)를 기준으로 "월드 회전 차이"를 옮긴다.
  - Mixamo 기본 자세는 FBX 의 PreRotation(+Lcl Rotation 기본값)에 있다 → fbx_rest.py 로 읽어 언리얼 좌표로 바꾼다.
    언리얼 FBX 임포터 변환: 위치 (x, -y, z), 회전 쿼터니언 (-x, y, -z, w), 뿌리 뼈에는 Y-위 → Z-위 회전 C 가 더 붙는다
    (C 는 축 회전 24개 중 임포트한 첫 프레임과 가장 가까운 것으로 고른다).
  - 뼈마다 Q = Wm_rest^-1 · F^-1 · Wo_rest  (F = Mixamo 몸 방향 → 우리 몸 방향 Yaw)
    우리 로컬 회전 = Q_부모^-1 · Mixamo 로컬 회전 · Q      (뿌리는 Mixamo 로컬 · Q)
  - 뼈 길이(이동) = 우리 기본 자세 값. Hips 이동만 F 로 돌리고 골반 높이 비율(우리 / Mixamo)을 곱한다.
"""
import importlib
import math
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fbx_rest  # noqa: E402

importlib.reload(fbx_rest)
EAL = unreal.EditorAssetLibrary


def log(msg):
    unreal.log("[Mixamo] " + msg)


def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)


def qinv(q):
    return (-q[0], -q[1], -q[2], q[3])


def qaxis(ax, deg):
    h = math.radians(deg) / 2.0
    v = [0.0, 0.0, 0.0]
    v[ax] = math.sin(h)
    return (v[0], v[1], v[2], math.cos(h))


def qrot(q, v):
    p = qmul(qmul(q, (v[0], v[1], v[2], 0.0)), qinv(q))
    return (p[0], p[1], p[2])


def qnorm(q):
    n = math.sqrt(sum(x * x for x in q)) or 1.0
    return tuple(x / n for x in q)


def qangle(a, b):
    d = abs(sum(x * y for x, y in zip(a, b)))
    return math.degrees(2.0 * math.acos(min(1.0, d)))


def euler_xyz(e):
    """FBX 기본 회전 순서 XYZ (X 먼저): R = Rz · Ry · Rx"""
    e = e or [0.0, 0.0, 0.0]
    return qmul(qaxis(2, e[2]), qmul(qaxis(1, e[1]), qaxis(0, e[0])))


def fbx_q(q):
    return (-q[0], q[1], -q[2], q[3])


def fbx_v(v):
    return (v[0], -v[1], v[2])


def to_q(r):
    return (r.x, r.y, r.z, r.w)


def mixamo_rest(fbx_path):
    """Mixamo 기본 자세 (언리얼 좌표, 뿌리 변환 C 제외): 이름 → (로컬 회전, 로컬 위치, 부모)"""
    bones = fbx_rest.rest_of(fbx_path)
    out = {}
    for n, b in bones.items():
        q = qmul(euler_xyz(b.get("pre")), euler_xyz(b.get("r")))
        if b.get("post"):
            q = qmul(q, qinv(euler_xyz(b.get("post"))))
        out[n] = (fbx_q(qnorm(q)), fbx_v(b.get("t") or (0, 0, 0)), b.get("parent", ""))
    return out


def world_of(local, root_c):
    """로컬 → 컴포넌트 기준 (회전, 위치). 뿌리(부모 없음)에 C 를 곱한다"""
    w = {}

    def get(n):
        if n in w:
            return w[n]
        q, t, par = local[n]
        if not par or par not in local:
            res = (qnorm(qmul(root_c, q)), qrot(root_c, t))
        else:
            pq, pt = get(par)
            v = qrot(pq, t)
            res = (qnorm(qmul(pq, q)), (pt[0] + v[0], pt[1] + v[1], pt[2] + v[2]))
        w[n] = res
        return res

    for n in local:
        get(n)
    return w


def axis_rotations():
    """축에 맞춘 회전 24개 (뿌리 변환 C 후보)"""
    cands = []
    for a in (0, 1, 2):
        for d in (0, 90, 180, 270):
            for b in (0, 1, 2):
                for e in (0, 90, 180, 270):
                    q = qnorm(qmul(qaxis(a, d), qaxis(b, e)))
                    if not any(qangle(q, c) < 1.0 for c in cands):
                        cands.append(q)
    return cands


class OurRig:
    """우리 스켈레톤 기본 자세 (로컬 위치, 컴포넌트 기준 회전·위치)"""

    def __init__(self, skeleton_path):
        APE = unreal.AnimPoseExtensions
        ref = APE.get_reference_pose(EAL.load_asset(skeleton_path))
        self.local_t, self.world_q, self.world_t = {}, {}, {}
        for bn in APE.get_bone_names(ref):
            n = str(bn)
            self.local_t[n] = APE.get_bone_pose(ref, bn, unreal.AnimPoseSpaces.LOCAL).translation
            w = APE.get_bone_pose(ref, bn, unreal.AnimPoseSpaces.WORLD)
            self.world_q[n] = to_q(w.rotation)
            self.world_t[n] = (w.translation.x, w.translation.y, w.translation.z)


def horiz(v):
    n = math.hypot(v[0], v[1]) or 1.0
    return (v[0] / n, v[1] / n)


def find_root_c(seq, fbx_path):
    L = unreal.AnimationLibrary
    h0 = to_q(L.get_bone_pose_for_frame(seq, "Hips", 0, False).rotation)
    hq = mixamo_rest(fbx_path)["Hips"][0]
    best = min(axis_rotations(), key=lambda c: qangle(qmul(c, hq), h0))
    log("  뿌리 변환: 첫 프레임 Hips 와 %.1f° 차이" % qangle(qmul(best, hq), h0))
    return best


def retarget_one(a, fbx_path, rig, root_c, log_it=False):
    L = unreal.AnimationLibrary
    mloc = mixamo_rest(fbx_path)
    mw = world_of(mloc, root_c)
    # 몸 방향 맞추기: 왼쪽 허벅지 - 오른쪽 허벅지 (수평)
    ml = horiz(tuple(mw["LeftUpLeg"][1][i] - mw["RightUpLeg"][1][i] for i in range(3)))
    ol = horiz(tuple(rig.world_t["LeftUpLeg"][i] - rig.world_t["RightUpLeg"][i] for i in range(3)))
    yaw = math.degrees(math.atan2(ol[1], ol[0]) - math.atan2(ml[1], ml[0]))
    F = qaxis(2, yaw)
    Q = {}
    for n in mloc:
        if n in rig.world_q:
            Q[n] = qnorm(qmul(qmul(qinv(mw[n][0]), qinv(F)), rig.world_q[n]))
    ratio = rig.world_t["Hips"][2] / max(mw["Hips"][1][2], 1.0)
    if log_it:
        log("  몸 방향 차이 %.0f°, 골반 높이 우리 %.1f / Mixamo %.1f → 비율 %.3f" % (yaw, rig.world_t["Hips"][2], mw["Hips"][1][2], ratio))
    frames = L.get_num_frames(a) + 1
    ctrl = a.get_editor_property("controller")
    # 트랙 이름은 FName 이라 대소문자가 처음 등록된 대로 나온다("hips") → 대소문자 무시로 맞춘다
    canon = {k.lower(): k for k in Q}
    for track in [str(t) for t in L.get_animation_track_names(a)]:
        n = canon.get(track.lower())
        if n is None:
            continue
        par = mloc[n][2]
        qp = Q.get(par) if par else None
        rots, pos, scl = [], [], []
        for f in range(frames):
            p = L.get_bone_pose_for_frame(a, n, f, False)
            lm = to_q(p.rotation)
            lo = qnorm(qmul(lm, Q[n]) if qp is None else qmul(qmul(qinv(qp), lm), Q[n]))
            rots.append(unreal.Quat(lo[0], lo[1], lo[2], lo[3]))
            if n == "Hips":
                v = qrot(F, (p.translation.x, p.translation.y, p.translation.z))
                pos.append(unreal.Vector(v[0] * ratio, v[1] * ratio, v[2] * ratio))
            else:
                r = rig.local_t[n]
                pos.append(unreal.Vector(r.x, r.y, r.z))
            scl.append(unreal.Vector(1.0, 1.0, 1.0))
        ctrl.set_bone_track_keys(track, pos, rots, scl)
    a.set_editor_property("enable_root_motion", False)
    EAL.save_loaded_asset(a, False)
