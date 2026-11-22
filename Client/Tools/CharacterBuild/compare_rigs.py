# -*- coding: utf-8 -*-
"""
두 캐릭터 FBX 의 뼈대 비교 (엔진파이썬 -I compare_rigs.py A.fbx B.fbx [C.fbx ...])
- 뼈(LimbNode) 수, 이름 차이, 뼈별 기본 회전(PreRotation+Lcl Rotation) 차이(도), 위치 비율
- 한 파일만 주면 노드 종류·Deformer(Skin/Cluster)·Pose 수까지 적는다 (리깅 여부 확인)
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fbx_rest  # noqa: E402
import mixamo_retarget_math as M  # noqa: E402


def info(path):
    top = fbx_rest.read_fbx(path)
    objs = next(n for n in top if n["name"] == "Objects")
    kinds = {}
    for m in objs["kids"]:
        key = m["name"]
        if m["name"] in ("Model", "Deformer", "NodeAttribute", "Geometry"):
            key = "%s/%s" % (m["name"], m["props"][2] if len(m["props"]) > 2 else "?")
        kinds[key] = kinds.get(key, 0) + 1
    data = fbx_rest._collect(top)
    return kinds, data


def short(n):
    return n.split(":")[-1]


def main(paths):
    datas = []
    for p in paths:
        kinds, data = info(p)
        limbs = [n for n, b in data["bones"].items() if b["type"] == "LimbNode"]
        print("== %s" % os.path.basename(p))
        print("  노드 종류: %s" % ", ".join("%s %d" % kv for kv in sorted(kinds.items())))
        print("  Pose: %s" % data["poses"])
        print("  뼈(LimbNode) %d개" % len(limbs))
        g = data["global"]
        print("  UpAxis %s FrontAxis %s UnitScaleFactor %s" % (g.get("UpAxis"), g.get("FrontAxis"), g.get("UnitScaleFactor")))
        datas.append((p, data, set(limbs)))
    if len(datas) < 2:
        return
    a_p, a, a_l = datas[0]
    for b_p, b, b_l in datas[1:]:
        print("== 비교 %s ↔ %s" % (os.path.basename(a_p), os.path.basename(b_p)))
        print("  A에만: %s" % sorted(a_l - b_l))
        print("  B에만: %s" % sorted(b_l - a_l))
        worst = []
        for n in sorted(a_l & b_l):
            ba, bb = a["bones"][n], b["bones"][n]
            qa = M.rest_q(ba)
            qb = M.rest_q(bb)
            d = M.qangle(qa, qb)
            ta = ba.get("t") or [0, 0, 0]
            tb = bb.get("t") or [0, 0, 0]
            la = math.sqrt(sum(x * x for x in ta))
            lb = math.sqrt(sum(x * x for x in tb))
            pa = a["bones"][n].get("parent", "")
            pb = b["bones"][n].get("parent", "")
            worst.append((d, n, la, lb, pa == pb, ta, tb))
        worst.sort(reverse=True)
        print("  기본 회전 차이 큰 순 (뼈, 차이°, 길이 A→B, 부모 같음):")
        for d, n, la, lb, same, ta, tb in worst[:20]:
            print("   %-22s %6.2f°  %.4f → %.4f (%.2f배) %s" % (n, d, la, lb, lb / la if la > 1e-6 else 0, "" if same else "부모 다름!"))
        print("  회전 차이 평균 %.2f°, 최대 %.2f°" % (sum(w[0] for w in worst) / len(worst), worst[0][0]))
        # 방향(뼈 위치 벡터) 각도 차이 — 길이 무시
        dirs = []
        for d, n, la, lb, same, ta, tb in worst:
            if la > 1e-4 and lb > 1e-4:
                c = sum(x * y for x, y in zip(ta, tb)) / (la * lb)
                dirs.append((math.degrees(math.acos(max(-1, min(1, c)))), n))
        dirs.sort(reverse=True)
        print("  뼈 위치 방향 차이 큰 순: %s" % ", ".join("%s %.1f°" % (n, x) for x, n in dirs[:12]))


if __name__ == "__main__":
    main(sys.argv[1:])
