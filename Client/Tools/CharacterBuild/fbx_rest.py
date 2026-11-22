# -*- coding: utf-8 -*-
"""
FBX(바이너리) 뼈 기본값 읽기 — Mixamo 애니메이션 FBX 의 뼈 기본 자세(Lcl Translation/Rotation, PreRotation)와 BindPose 를 JSON 으로.
  엔진파이썬 -I fbx_rest.py 입력.fbx [출력.json]
외부 라이브러리 없이 FBX 바이너리 구조(노드 트리)만 읽는다.
"""
import json
import struct
import sys
import zlib


def read_fbx(path):
    data = open(path, "rb").read()
    if not data.startswith(b"Kaydara FBX Binary"):
        raise ValueError("binary FBX 아님")
    ver = struct.unpack_from("<I", data, 23)[0]
    big = ver >= 7500
    pos = 27

    def prop(p):
        t = chr(data[p]); p += 1
        if t == "Y": return struct.unpack_from("<h", data, p)[0], p + 2
        if t == "C": return bool(data[p]), p + 1
        if t == "I": return struct.unpack_from("<i", data, p)[0], p + 4
        if t == "F": return struct.unpack_from("<f", data, p)[0], p + 4
        if t == "D": return struct.unpack_from("<d", data, p)[0], p + 8
        if t == "L": return struct.unpack_from("<q", data, p)[0], p + 8
        if t in "SR":
            n = struct.unpack_from("<I", data, p)[0]; p += 4
            raw = data[p:p + n]
            return (raw.decode("utf-8", "replace") if t == "S" else raw), p + n
        if t in "fdlib":
            n, enc, clen = struct.unpack_from("<III", data, p); p += 12
            raw = data[p:p + clen]; p += clen
            if enc == 1:
                raw = zlib.decompress(raw)
            fmt = {"f": "f", "d": "d", "l": "q", "i": "i", "b": "?"}[t]
            return list(struct.unpack("<%d%s" % (n, fmt), raw)), p
        raise ValueError("모르는 속성 " + t)

    def node(p):
        if big:
            end, nprop, plen = struct.unpack_from("<QQQ", data, p); p += 24
        else:
            end, nprop, plen = struct.unpack_from("<III", data, p); p += 12
        nlen = data[p]; p += 1
        name = data[p:p + nlen].decode("ascii", "replace"); p += nlen
        if end == 0:
            return None, p
        props = []
        for _ in range(nprop):
            v, p = prop(p)
            props.append(v)
        kids = []
        sentinel = 25 if big else 13
        while p < end - sentinel or (p < end and data[p:p + sentinel] != b"\0" * sentinel):
            k, p = node(p)
            if k is None:
                break
            kids.append(k)
        return {"name": name, "props": props, "kids": kids}, end

    top = []
    while pos < len(data):
        n, pos = node(pos)
        if n is None:
            break
        top.append(n)
    return top


def p70(n):
    out = {}
    for k in n["kids"]:
        if k["name"] == "Properties70":
            for pp in k["kids"]:
                if pp["name"] == "P":
                    out[pp["props"][0]] = pp["props"][4:]
    return out


def rest_of(path):
    """뼈 이름 → {t, r, pre, post, parent, ...} (FBX 좌표, 도 단위 오일러)"""
    return _collect(read_fbx(path))["bones"]


def main():
    out = _collect(read_fbx(sys.argv[1]))
    s = json.dumps(out, indent=1, ensure_ascii=False, default=str)
    if len(sys.argv) > 2:
        open(sys.argv[2], "w", encoding="utf-8").write(s)
    else:
        print(s[:4000])


def _collect(top):
    objs = next(n for n in top if n["name"] == "Objects")
    bones = {}
    for m in objs["kids"]:
        if m["name"] == "Model":
            nm = m["props"][1].split("\x00")[0]
            if nm.startswith("Model::"):
                nm = nm[7:]
            p = p70(m)
            bones[nm.split(":")[-1]] = {
                "id": m["props"][0], "type": m["props"][2],
                "t": p.get("Lcl Translation"), "r": p.get("Lcl Rotation"), "pre": p.get("PreRotation"),
                "post": p.get("PostRotation"), "order": p.get("RotationOrder"), "s": p.get("Lcl Scaling"),
            }
    # 부모: Connections 의 "OO" (자식 id → 부모 id)
    by_id = {b["id"]: n for n, b in bones.items()}
    conn = next((n for n in top if n["name"] == "Connections"), None)
    if conn:
        for c in conn["kids"]:
            if c["name"] == "C" and c["props"][0] == "OO" and c["props"][1] in by_id:
                bones[by_id[c["props"][1]]]["parent"] = by_id.get(c["props"][2], "")
    poses = []
    for m in objs["kids"]:
        if m["name"] == "Pose":
            poses.append({"name": m["props"][1], "n": len([k for k in m["kids"] if k["name"] == "PoseNode"])})
    gs = next((n for n in top if n["name"] == "GlobalSettings"), None)
    return {"bones": bones, "poses": poses, "global": p70(gs) if gs else {}}


if __name__ == "__main__":
    main()
