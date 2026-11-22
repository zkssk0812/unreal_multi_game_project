# -*- coding: utf-8 -*-
"""
캐릭터별 무기 손 위치 비교 (dump_all_points.sh 결과): 엔진파이썬 -I compare_points.py 기준꼬리표 비교꼬리표...
  예) compare_points.py h1 h2 h3  → Saved/WeaponBuild/points_h1.json 기준으로 h2·h3 차이
무기마다 (캐릭터 기준 cm)
  R손목   = 오른손 뼈 ↔ 무기 Grip 소켓 거리 (손 크기가 다르면 달라지는 게 정상)
  R소켓   = 손 소켓 WeaponSocket_R(쥔 주먹 가운데) ↔ Grip 소켓 거리 (0 에 가까우면 손잡이가 주먹 안에 있다)
  L손     = 왼손 뼈 ↔ SupportHand 소켓 거리 (두 손 무기만)
"""
import json
import math
import os
import sys

SAVED = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Saved", "WeaponBuild")


def dist(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def load(tag):
    return json.load(open(os.path.join(SAVED, "points_%s.json" % tag), encoding="utf-8"))


def metrics(p):
    g = p.get("sock_Grip")
    s = p.get("sock_SupportHand")
    return {
        "R손목": dist(p["rhand"], g) if g else None,
        "R소켓": dist(p["rsock"], g) if g and "rsock" in p else None,
        "L손": dist(p["lhand"], s) if s else None,
    }


def f(v):
    return "  -  " if v is None else "%5.1f" % v


def main(tags):
    data = [load(t) for t in tags]
    base = data[0]
    print("캐릭터: " + ", ".join("%s=%s" % (t, next(iter(d.values())).get("char_mesh", "?")) for t, d in zip(tags, data)))
    head = "%-22s" % "무기" + "".join("| %-19s" % ("%s R손목/R소켓/L손" % t) for t in tags) + "| 최대 차이(기준 대비) R소켓 / L손 / 무기 위치"
    print(head)
    worst = {}
    for w in sorted(base):
        if w == "none":
            continue
        ms = [metrics(d[w]) if w in d else None for d in data]
        row = "%-22s" % w
        for m in ms:
            row += "| %s %s %s     " % (f(m["R손목"]), f(m["R소켓"]), f(m["L손"])) if m else "| (없음)              "
        b = ms[0]
        dr = max(abs((m["R소켓"] or 0) - (b["R소켓"] or 0)) for m in ms if m)
        dl = max(abs((m["L손"] or 0) - (b["L손"] or 0)) for m in ms if m)
        dp = max(dist(d[w]["pos"], base[w]["pos"]) for d in data if w in d and "pos" in d[w])
        worst[w] = (dr, dl, dp)
        print(row + "| %4.1f / %4.1f / %4.1f" % (dr, dl, dp))
    print("전체 최대: R소켓 %.1fcm, L손 %.1fcm, 무기 위치 %.1fcm" % tuple(max(v[i] for v in worst.values()) for i in range(3)))


main(sys.argv[1:] or ["h1", "h2", "h3"])
