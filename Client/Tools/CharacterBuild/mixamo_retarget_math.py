# -*- coding: utf-8 -*-
"""쿼터니언 계산만 (unreal 모듈 없이 엔진파이썬 -I 로 쓰는 비교 스크립트용). mixamo_retarget.py 와 같은 식"""
import math


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


def qnorm(q):
    n = math.sqrt(sum(x * x for x in q)) or 1.0
    return tuple(x / n for x in q)


def qangle(a, b):
    d = abs(sum(x * y for x, y in zip(a, b)))
    return math.degrees(2.0 * math.acos(min(1.0, d)))


def euler_xyz(e):
    e = e or [0.0, 0.0, 0.0]
    return qmul(qaxis(2, e[2]), qmul(qaxis(1, e[1]), qaxis(0, e[0])))


def rest_q(b):
    q = qmul(euler_xyz(b.get("pre")), euler_xyz(b.get("r")))
    if b.get("post"):
        q = qmul(q, qinv(euler_xyz(b.get("post"))))
    return qnorm(q)
