# -*- coding: utf-8 -*-
"""무기 모델용 작은 메시 도구 (언리얼 에디터 파이썬 + Geometry Script).

구상안 HTML 의 옆모습 윤곽(SVG path)을 그대로 단면으로 써서 입체를 만든다.
  - profile_solid : 옆모습 윤곽(X-Z)을 두께(Y) 방향으로 밀어내고 모서리를 깎는다(모따기)
  - section_solid : 단면(Y-Z)을 길이(X) 방향으로 밀어낸다. 구간마다 굵기를 바꿀 수 있다(원통·총열·캐릭터 라인이 있는 몸통)
약속: +X = 앞(총구·칼끝), +Z = 위, +Y = 오른쪽, 단위 cm, 원점 = 손잡이(설치형은 바닥 중앙).
"""
import math
import re
import unreal

GS_EDIT = unreal.GeometryScript_MeshEdits
GS_MAT = unreal.GeometryScript_Materials
GS_BOOL = unreal.GeometryScript_MeshBooleans
GS_NORMALS = unreal.GeometryScript_Normals
GS_QUERY = unreal.GeometryScript_MeshQueries
GS_NEW = unreal.GeometryScript_NewAssetUtils
GS_ASSET = unreal.GeometryScript_AssetUtils

# 재질 칸 순서 (모든 무기 공통. 메시의 재질 칸 번호 = 이 순서)
BLACK, STEEL, CARBON, LEATHER, BRASS, GLOW = range(6)
SLOT_NAMES = ["Black", "Steel", "Carbon", "Leather", "Brass", "Glow"]


# ───────────── 2D 윤곽 ─────────────

def svg_path(d, steps=8):
    """SVG path(M L H V C Z, 대문자=절대·소문자=상대) → 닫힌 윤곽 목록 [[(x, y), ...], ...]"""
    tokens = re.findall(r"[MLHVCZmlhvcz]|-?\d*\.?\d+(?:[eE]-?\d+)?", d)
    polys, cur, i, cmd = [], [], 0, None
    x = y = 0.0

    def num():
        nonlocal i
        v = float(tokens[i]); i += 1
        return v

    while i < len(tokens):
        if re.match(r"[A-Za-z]", tokens[i]):
            cmd = tokens[i]; i += 1
            if cmd in "Zz":
                if cur:
                    polys.append(cur)
                cur = []
                continue
        rel = cmd.islower()
        c = cmd.upper()
        if c == "M":
            nx, ny = num(), num()
            x, y = (x + nx, y + ny) if rel else (nx, ny)
            if cur:
                polys.append(cur)
            cur = [(x, y)]
            cmd = "l" if rel else "L"
        elif c == "L":
            nx, ny = num(), num()
            x, y = (x + nx, y + ny) if rel else (nx, ny)
            cur.append((x, y))
        elif c == "H":
            nx = num(); x = x + nx if rel else nx; cur.append((x, y))
        elif c == "V":
            ny = num(); y = y + ny if rel else ny; cur.append((x, y))
        elif c == "C":
            p = [num() for _ in range(6)]
            if rel:
                p = [p[0] + x, p[1] + y, p[2] + x, p[3] + y, p[4] + x, p[5] + y]
            x0, y0 = x, y
            for k in range(1, steps + 1):
                t = k / steps; u = 1 - t
                cur.append((u * u * u * x0 + 3 * u * u * t * p[0] + 3 * u * t * t * p[2] + t * t * t * p[4],
                            u * u * u * y0 + 3 * u * u * t * p[1] + 3 * u * t * t * p[3] + t * t * t * p[5]))
            x, y = p[4], p[5]
    if cur:
        polys.append(cur)
    return [clean(p) for p in polys]


def clean(poly, eps=1e-4):
    out = []
    for p in poly:
        if not out or abs(p[0] - out[-1][0]) > eps or abs(p[1] - out[-1][1]) > eps:
            out.append((float(p[0]), float(p[1])))
    if len(out) > 1 and abs(out[0][0] - out[-1][0]) <= eps and abs(out[0][1] - out[-1][1]) <= eps:
        out.pop()
    return out


def area(poly):
    return 0.5 * sum(poly[i][0] * poly[(i + 1) % len(poly)][1] - poly[(i + 1) % len(poly)][0] * poly[i][1] for i in range(len(poly)))


def ccw(poly):
    return poly if area(poly) > 0 else poly[::-1]


def inset(poly, d):
    """반시계 윤곽을 안쪽으로 d 만큼 줄인다 (d < 0 이면 바깥으로 키운다). 작은 모따기용 단순 방식"""
    n, out = len(poly), []
    for i in range(n):
        ax, ay = poly[i - 1]; bx, by = poly[i]; cx, cy = poly[(i + 1) % n]
        e1 = (bx - ax, by - ay); e2 = (cx - bx, cy - by)
        l1 = math.hypot(*e1) or 1.0; l2 = math.hypot(*e2) or 1.0
        n1 = (-e1[1] / l1, e1[0] / l1); n2 = (-e2[1] / l2, e2[0] / l2)   # 반시계일 때 안쪽
        bis = (n1[0] + n2[0], n1[1] + n2[1]); bl = math.hypot(*bis)
        if bl < 1e-6:
            out.append((bx + n1[0] * d, by + n1[1] * d)); continue
        cos_half = max(0.5, (bis[0] * n1[0] + bis[1] * n1[1]) / bl)
        out.append((bx + bis[0] / bl * d / cos_half, by + bis[1] / bl * d / cos_half))
    return out


def circle(cx, cy, r, n=20):
    return [(cx + r * math.cos(2 * math.pi * i / n), cy + r * math.sin(2 * math.pi * i / n)) for i in range(n)]


def rect(x0, y0, x1, y1):
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]


def rounded_rect(x0, y0, x1, y1, r, n=4):
    r = min(r, (x1 - x0) / 2, (y1 - y0) / 2)
    pts = []
    for cx, cy, a0 in ((x1 - r, y0 + r, -90), (x1 - r, y1 - r, 0), (x0 + r, y1 - r, 90), (x0 + r, y0 + r, 180)):
        for k in range(n + 1):
            a = math.radians(a0 + 90 * k / n)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def earclip(poly):
    """단순 다각형 삼각분할 → [(i, j, k), ...] (반시계 기준)"""
    idx = list(range(len(poly)))
    if area(poly) < 0:
        idx.reverse()
    tris, guard = [], 0
    while len(idx) > 3 and guard < 20000:
        guard += 1
        clipped = False
        for k in range(len(idx)):
            i0, i1, i2 = idx[k - 1], idx[k], idx[(k + 1) % len(idx)]
            a, b, c = poly[i0], poly[i1], poly[i2]
            cross = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
            if cross <= 1e-12:
                continue
            ok = True
            for j in idx:
                if j in (i0, i1, i2):
                    continue
                p = poly[j]
                d1 = (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0])
                d2 = (c[0] - b[0]) * (p[1] - b[1]) - (c[1] - b[1]) * (p[0] - b[0])
                d3 = (a[0] - c[0]) * (p[1] - c[1]) - (a[1] - c[1]) * (p[0] - c[0])
                if d1 >= 0 and d2 >= 0 and d3 >= 0:
                    ok = False; break
            if ok:
                tris.append((i0, i1, i2)); idx.pop(k); clipped = True; break
        if not clipped:
            idx.pop(0)      # 퇴화한 점은 건너뛴다
    if len(idx) == 3:
        tris.append(tuple(idx))
    return tris


# ───────────── 입체 만들기 ─────────────

class Builder:
    """부품을 하나씩 더해 무기 하나의 DynamicMesh 를 만든다. 부품마다 재질 칸 번호를 정한다."""

    def __init__(self, uv_of=None):
        """uv_of(x, y, z) → (u, v): 꼭짓점마다 구상안 옆모습 그림(T_VHW_SideAtlas)의 어디를 입힐지. 없으면 UV 를 넣지 않는다"""
        self.mesh = unreal.DynamicMesh()
        GS_MAT.enable_material_i_ds(self.mesh)
        self.tri_count = 0
        self.uv_of = uv_of

    def _new_part(self, rings, to3d, cap2d, material):
        """rings: 같은 점 개수의 닫힌 2D 고리들. to3d(고리 번호, (u, v)) → (x, y, z). 양 끝은 막는다."""
        n = len(rings[0])
        verts = []
        for r, ring in enumerate(rings):
            for p in ring:
                verts.append(to3d(r, p))
        tris = []
        for r in range(len(rings) - 1):
            a0, b0 = r * n, (r + 1) * n
            for i in range(n):
                j = (i + 1) % n
                tris.append((a0 + i, b0 + i, b0 + j)); tris.append((a0 + i, b0 + j, a0 + j))
        last = (len(rings) - 1) * n
        # 옆면과 방향이 맞게: 시작 쪽 마개는 그대로, 끝 쪽 마개는 뒤집는다 (전체 안팎은 아래에서 부피로 바로잡는다)
        for (i, j, k) in earclip(cap2d(rings[0])):
            tris.append((i, j, k))
        for (i, j, k) in earclip(cap2d(rings[-1])):
            tris.append((last + i, last + k, last + j))
        part = unreal.DynamicMesh()
        GS_MAT.enable_material_i_ds(part)
        buf = unreal.GeometryScriptSimpleMeshBuffers()
        buf.set_editor_property("vertices", [unreal.Vector(*v) for v in verts])
        buf.set_editor_property("triangles", [unreal.IntVector(*t) for t in tris])
        if self.uv_of:
            buf.set_editor_property("uv0", [unreal.Vector2D(*self.uv_of(*v)) for v in verts])
        GS_EDIT.append_buffers_to_mesh(part, buf, material)
        # 면이 뒤집혀 있으면(부피가 음수) 바로잡는다
        _, volume = GS_QUERY.get_mesh_volume_area(part)
        if volume < 0:
            GS_NORMALS.flip_normals(part)
        return part

    def profile_part(self, outline, y0, y1, chamfer, material):
        """옆모습 윤곽(X-Z, cm)을 Y 방향 y0~y1 로 밀어낸 부품(양쪽 모서리 모따기) → DynamicMesh (아직 더하지 않음)"""
        outline = ccw(clean(outline))
        c = max(0.0, min(chamfer, (y1 - y0) * 0.45))
        if c > 1e-4:
            small = inset(outline, c)
            rings, ys = [small, outline, outline, small], [y0, y0 + c, y1 - c, y1]
        else:
            rings, ys = [outline, outline], [y0, y1]
        return self._new_part(rings, lambda r, p: (p[0], ys[r], p[1]), lambda ring: ring, material)

    def top_part(self, outline, z0, z1, chamfer, material):
        """위에서 본 윤곽(X-Y, cm)을 Z 방향 z0~z1 로 밀어낸 부품(위·아래 모서리 모따기) → DynamicMesh (지뢰처럼 납작한 것)"""
        outline = ccw(clean(outline))
        c = max(0.0, min(chamfer, (z1 - z0) * 0.45))
        rings, zs = ([inset(outline, c), outline, outline, inset(outline, c)], [z0, z0 + c, z1 - c, z1]) if c > 1e-4 else ([outline, outline], [z0, z1])
        return self._new_part(rings, lambda r, p: (p[0], p[1], zs[r]), lambda ring: ring, material)

    @staticmethod
    def intersect(part, tool):
        GS_BOOL.apply_mesh_boolean(part, unreal.Transform(), tool, unreal.Transform(),
                                   unreal.GeometryScriptBooleanOperation.INTERSECTION, unreal.GeometryScriptMeshBooleanOptions())
        return part

    def section_part(self, section, stations, material):
        """단면(Y-Z, cm)을 X 방향으로 밀어낸 부품. stations = [(x, 배율, z 이동), ...] → DynamicMesh"""
        section = ccw(clean(section))
        rings = [[(p[0] * s, p[1] * s + dz) for p in section] for (_, s, dz) in stations]
        xs = [st[0] for st in stations]
        return self._new_part(rings, lambda r, p: (xs[r], p[0], p[1]), lambda ring: ring, material)

    def add(self, part):
        GS_EDIT.append_mesh(self.mesh, part, unreal.Transform())
        return part

    def profile(self, outline, y0, y1, chamfer, material):
        return self.add(self.profile_part(outline, y0, y1, chamfer, material))

    def section(self, section, stations, material):
        return self.add(self.section_part(section, stations, material))

    @staticmethod
    def subtract(part, tool):
        opts = unreal.GeometryScriptMeshBooleanOptions()
        GS_BOOL.apply_mesh_boolean(part, unreal.Transform(), tool, unreal.Transform(),
                                   unreal.GeometryScriptBooleanOperation.SUBTRACT, opts)
        return part

    def finish(self, split_angle=38.0):
        split = unreal.GeometryScriptSplitNormalsOptions()
        split.set_editor_property("split_by_opening_angle", True)
        split.set_editor_property("opening_angle_deg", split_angle)
        split.set_editor_property("split_by_face_group", False)
        GS_NORMALS.compute_split_normals(self.mesh, split, unreal.GeometryScriptCalculateNormalsOptions())
        self.tri_count = GS_QUERY.get_num_triangle_i_ds(self.mesh) if hasattr(GS_QUERY, "get_num_triangle_i_ds") else self.mesh.get_triangle_count()
        return self.mesh


def save_static_mesh(mesh, asset_path, materials):
    """DynamicMesh → 스태틱 메시 에셋(있으면 덮어쓴다). materials = 재질 칸 순서대로 MaterialInterface 목록"""
    asset = unreal.EditorAssetLibrary.load_asset(asset_path) if unreal.EditorAssetLibrary.does_asset_exist(asset_path) else None
    if asset is None:
        new_opts = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
        new_opts.set_editor_property("enable_collision", False)
        asset, outcome = GS_NEW.create_new_static_mesh_asset_from_mesh(mesh, asset_path, new_opts)
        if asset is None:
            raise RuntimeError("스태틱 메시를 만들지 못했습니다: %s (%s)" % (asset_path, outcome))
    opts = unreal.GeometryScriptCopyMeshToAssetOptions()
    opts.set_editor_property("enable_recompute_normals", False)
    opts.set_editor_property("enable_recompute_tangents", True)
    opts.set_editor_property("replace_materials", True)
    opts.set_editor_property("new_materials", materials)
    opts.set_editor_property("new_material_slot_names", [unreal.Name(n) for n in (SLOT_NAMES[:len(materials)] if len(materials) > 1 else ["Art"])])
    GS_ASSET.copy_mesh_to_static_mesh(mesh, asset, opts, unreal.GeometryScriptMeshWriteLOD())
    unreal.EditorAssetLibrary.save_loaded_asset(asset, False)
    return asset
