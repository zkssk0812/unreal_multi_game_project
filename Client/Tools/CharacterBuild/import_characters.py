# -*- coding: utf-8 -*-
"""
모델러 캐릭터(의적)·NPC(로봇 경비원) 임포트 (에디터 Cmd 칸: py "C:/UnrealProjects/MCPTest/Tools/CharacterBuild/import_characters.py" 단계...)

원본: SourceArt/Characters/Thief/Thief.fbx, SourceArt/Characters/Guard/Guard.fbx (Tripo AI, Mixamo 뼈대 65개, 애니메이션 없음)
      텍스처 = 같은 폴더 Textures/T_<이름>_Color·Normal·Metallic·Roughness.jpg (2048로 줄인 것. 4096 원본은 Original4k/, Git 제외)

단계
  mesh       : FBX → 스켈레탈 메시 + 스켈레톤 + 피직스 (재질·텍스처는 FBX 에서 가져오지 않는다)
  fixscale   : 뿌리 뼈 크기(배율)를 뼈 거리로 굽는다 (안 하면 애니메이션에서 몸이 뭉친다)
  textures   : 텍스처 4장씩 임포트 (최대 2048, 노멀·마스크 설정)
  material   : 공용 재질 M_VHCharacter (Color·Normal·Metallic·Roughness + 하늘색 발광 마스크 + 옷 색 바꾸기) → 캐릭터별 인스턴스, 메시에 연결
  inspect    : 크기·방향·주요 뼈 위치를 로그에 적는다
  all        : mesh textures material inspect

플레이(PIE) 중에는 돌리지 않는다 (에셋 저장이 실패한다).
"""
import os
import sys
import unreal

EAL = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SRC = os.path.join(PROJ, "SourceArt", "Characters")
ROOT = "/Game/VaultHeist/Characters"
MAT_DIR = ROOT + "/Materials"
MASTER = MAT_DIR + "/M_VHCharacter"

# 이름 → (폴더, 메시 이름, 발광 세기)
CHARS = {
    "Thief": dict(dest=ROOT + "/Thief", mesh="SK_VHThief", glow=0.0),
    "Guard": dict(dest=ROOT + "/Guard", mesh="SK_VHGuard", glow=4.0),
    # 도적2 = 캐릭터2 (2026-11-20). share = 캐릭터1 스켈레톤을 함께 쓴다 (애니메이션·블렌드스페이스·몽타주 공용)
    "Thief2": dict(dest=ROOT + "/Thief2", mesh="SK_VHThief2", glow=0.0, share="Thief", scale=1.8),  # FBX 단위가 cm(UnitScaleFactor 1) → 1.8 (캐릭터1은 m 단위라 180)
    # 도적3 = 캐릭터3 리깅본 (2026-11-21): 뼈 78개 = 캐릭터1 이름 65개 + 가중치 없는 끝 뼈 13개(*_end, share 단계에서 지움), FBX 단위 cm → 1.8
    "Thief3": dict(dest=ROOT + "/Thief3", mesh="SK_VHThief3", glow=0.0, share="Thief", scale=1.8),
}
# 끝 뼈 이름이 다른 Tripo 뼈대 → 캐릭터1 이름으로 (스켈레톤을 함께 쓰려면 이름이 같아야 한다)
END_BONE_RENAME = {"Head_end": "HeadTop_End", "LeftToeBase_end": "LeftToe_End", "RightToeBase_end": "RightToe_End"}
for _side in ("Left", "Right"):
    for _f in ("Thumb", "Index", "Middle", "Ring", "Pinky"):
        END_BONE_RENAME["%sHand%s3_end" % (_side, _f)] = "%sHand%s4" % (_side, _f)
# Tripo FBX 는 미터 단위로 키 1.0 (발 = 0, 앞 = +Y = 마네킹과 같은 방향, T자세) → 배율 180 = 키 180cm (서버 몸 크기·캡슐 반높이 96)
IMPORT_SCALE = 180.0
MAPS = ["Color", "Normal", "Metallic", "Roughness"]


def log(msg):
    unreal.log("[CharBuild] " + msg)


def in_pie():
    try:
        return unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    except Exception:
        return False


def ensure_dir(path):
    if not EAL.does_directory_exist(path):
        EAL.make_directory(path)


# ───────────── 메시 ─────────────
def do_mesh(names):
    for name in names:
        import_fbx(name, os.path.join(SRC, name, name + ".fbx"), CHARS[name].get("scale", IMPORT_SCALE))


def import_fbx(name, fbx, scale, skeleton=None):
    # 옛 FBX 임포터를 쓴다 (FbxImportUI 옵션이 그대로 먹게). 인터체인지는 끝나고 원래대로 돌린다
    # skeleton: 주면 그 스켈레톤을 함께 쓴다 (새 스켈레톤을 만들지 않음, 기준 자세도 바꾸지 않음)
    sys_lib = unreal.SystemLibrary
    sys_lib.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
    try:
        if True:
            c = CHARS[name]
            ensure_dir(c["dest"])
            # 다시 임포트할 때 옛 에셋의 임포트 설정(배율)이 다시 쓰이므로 메시·스켈레톤·피직스를 지우고 새로 만든다
            for suffix in ("", "_Skeleton", "_PhysicsAsset"):
                old = "%s/%s%s" % (c["dest"], c["mesh"], suffix)
                if EAL.does_asset_exist(old):
                    EAL.delete_asset(old)
            ui = unreal.FbxImportUI()
            ui.set_editor_property("import_mesh", True)
            ui.set_editor_property("import_as_skeletal", True)
            ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
            ui.set_editor_property("import_animations", False)
            ui.set_editor_property("import_materials", False)
            ui.set_editor_property("import_textures", False)
            ui.set_editor_property("create_physics_asset", True)
            if skeleton is not None:
                ui.set_editor_property("skeleton", skeleton)
            sk = ui.get_editor_property("skeletal_mesh_import_data")
            if skeleton is not None:
                sk.set_editor_property("update_skeleton_reference_pose", False)
            sk.set_editor_property("import_morph_targets", False)
            sk.set_editor_property("convert_scene", True)
            sk.set_editor_property("force_front_x_axis", False)
            sk.set_editor_property("convert_scene_unit", False)
            sk.set_editor_property("use_t0_as_ref_pose", False)
            sk.set_editor_property("import_uniform_scale", scale)
            sk.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
            t = unreal.AssetImportTask()
            t.set_editor_property("filename", fbx)
            t.set_editor_property("destination_path", c["dest"])
            t.set_editor_property("destination_name", c["mesh"])
            t.set_editor_property("automated", True)
            t.set_editor_property("replace_existing", True)
            t.set_editor_property("save", True)
            t.set_editor_property("options", ui)
            TOOLS.import_asset_tasks([t])
            log("%s 임포트: %s" % (name, list(t.get_editor_property("imported_object_paths"))))
    finally:
        sys_lib.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 1")


# ───────────── 뼈 크기 굽기 ─────────────
def do_fixscale(names):
    """
    Tripo FBX 는 임포트 배율이 뿌리 뼈(Hips)의 크기(scale)로 들어가고 자식 뼈 거리는 원래(미터) 크기로 남는다.
    애니메이션은 뼈 크기를 1로 쓰므로 그대로 두면 몸이 1/배율로 뭉친다 (2026-11-17 확인) →
    Hips 크기를 1로, 모든 자식 뼈의 위치를 × 그 크기로 바꿔 메시에 다시 굽는다 (월드 위치·모양은 그대로).
    """
    for name in names:
        c = CHARS[name]
        mesh = EAL.load_asset("%s/%s" % (c["dest"], c["mesh"]))
        m = unreal.SkeletonModifier()
        m.set_skeletal_mesh(mesh)
        bones = [str(b) for b in m.get_all_bone_names()]
        root = bones[0]
        rt = m.get_bone_transform(root, False)
        k = rt.scale3d.x
        if abs(k - 1.0) < 1e-3:
            log("%s 뼈 크기 이미 1 (건너뜀)" % name)
            continue
        locals_ = {b: m.get_bone_transform(b, False) for b in bones}
        m.set_bone_transform(root, unreal.Transform(rt.translation, rt.rotation.rotator(), unreal.Vector(1, 1, 1)), False)
        for b in bones[1:]:
            t = locals_[b]
            m.set_bone_transform(b, unreal.Transform(t.translation * k, t.rotation.rotator(), t.scale3d), False)
        ok = m.commit_skeleton_to_skeletal_mesh()
        log("%s 뼈 크기 굽기: %s 크기 %.1f → 1, 자식 뼈 거리 × %.1f (commit %s)" % (name, root, k, k, ok))
        # 스켈레톤 에셋의 기준 자세는 옛 값으로 남으므로, 고친 메시를 FBX 로 내보내 새로 임포트한다 (스켈레톤·피직스 새로 만듦)
        out = baked_fbx(name)
        os.makedirs(os.path.dirname(out), exist_ok=True)
        opt = unreal.FbxExportOption()
        opt.set_editor_property("collision", False)
        opt.set_editor_property("export_morph_targets", False)
        opt.set_editor_property("level_of_detail", False)
        task = unreal.AssetExportTask()
        task.set_editor_property("object", mesh)
        task.set_editor_property("filename", out)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_identical", True)
        task.set_editor_property("prompt", False)
        task.set_editor_property("options", opt)
        task.set_editor_property("exporter", unreal.SkeletalMeshExporterFBX())
        exported = unreal.Exporter.run_asset_export_task(task)
        log("%s 내보내기 %s → %s" % (name, exported, out))
        if exported:
            import_fbx(name, out, 1.0)


def baked_fbx(name, tag="baked"):
    # 크기를 구운 중간 FBX (다시 만들 수 있으므로 Git 제외: Saved 아래)
    return os.path.join(PROJ, "Saved", "CharacterBuild", "%s_%s.fbx" % (name, tag))


# ───────────── 캐릭터1 스켈레톤 함께 쓰기 ─────────────
def do_share(names):
    """
    같은 Tripo(Mixamo 이름) 뼈대라도 캐릭터마다 뼈 자기 축 방향이 다르다 (캐릭터2: 평균 10°, 오른 아래팔 52°, compare_rigs.py).
    애니메이션은 뼈 로컬 회전을 그대로 쓰므로 축이 다르면 자세가 틀어진다 →
      ① 끝 뼈 이름을 캐릭터1 이름으로 (END_BONE_RENAME)
      ② 뼈마다 "위치는 이 캐릭터 관절 그대로, 축 방향(월드 회전)은 캐릭터1 기본 자세와 같게" 바인드 자세를 다시 잡는다
         (메시 정점은 움직이지 않는다 = 다시 바인드. 모양 그대로)
      ③ FBX 로 내보내 캐릭터1 스켈레톤으로 다시 임포트 → 캐릭터1 애니메이션·블렌드스페이스·몽타주를 그대로 쓴다
    뼈 길이(팔 길이 등)는 스켈레톤의 이동 리타게팅 모드(Hips = AnimationScaled, 나머지 = Skeleton)로 각 메시 자기 값을 쓴다
    (setup_character_data.py retargetmode. 캐릭터1 애니메이션의 뼈 이동 = 캐릭터1 기본 자세라 캐릭터1은 바뀌지 않음, _probe_trans.py 로 0cm 확인)
    """
    APE = unreal.AnimPoseExtensions
    for name in names:
        c = CHARS[name]
        if not c.get("share"):
            continue
        share = CHARS[c["share"]]
        skel = EAL.load_asset("%s/%s_Skeleton" % (share["dest"], share["mesh"]))
        ref = APE.get_reference_pose(skel)
        target_rot = {}
        for bn in APE.get_bone_names(ref):
            target_rot[str(bn)] = APE.get_bone_pose(ref, bn, unreal.AnimPoseSpaces.WORLD).rotation
        mesh = EAL.load_asset("%s/%s" % (c["dest"], c["mesh"]))
        m = unreal.SkeletonModifier()
        m.set_skeletal_mesh(mesh)
        bones = [str(b) for b in m.get_all_bone_names()]
        old = [b for b in bones if b in END_BONE_RENAME]
        if old:
            m.rename_bones(old, [END_BONE_RENAME[b] for b in old])
            log("%s 끝 뼈 이름 %d개 → 캐릭터1 이름" % (name, len(old)))
        bones = [str(b) for b in m.get_all_bone_names()]
        # 캐릭터3 처럼 캐릭터1 끝 뼈(HeadTop_End·*4·*Toe_End) 아래에 끝 뼈가 하나 더 붙은 뼈대(*_end, 13개):
        # 스킨 가중치가 없는 표시용 뼈라(FBX 클러스터 없음, 2026-11-21 확인) 지운다 → 캐릭터1 스켈레톤에 뼈가 추가되지 않는다
        children = {}
        for b in bones:
            children.setdefault(str(m.get_parent_name(b)), []).append(b)
        drop = [b for b in bones if b not in target_rot and b.endswith("_end") and not children.get(b)]
        if drop:
            ok_rm = m.remove_bones(drop, False)
            log("%s 캐릭터1에 없는 끝 뼈 %d개 지움 (가중치 없음): %s, %s" % (name, len(drop), ", ".join(drop), ok_rm))
            bones = [str(b) for b in m.get_all_bone_names()]
        missing = [b for b in bones if b not in target_rot]
        extra = [b for b in target_rot if b not in bones]
        if missing:
            unreal.log_error("[CharBuild] %s: 캐릭터1 스켈레톤에 없는 뼈 %s → 함께 쓸 수 없음" % (name, missing))
            continue
        if extra:
            log("%s: 캐릭터1에만 있는 뼈 %s (괜찮음)" % (name, extra))
        world = {b: m.get_bone_transform(b, True) for b in bones}
        new_world = {b: unreal.Transform(world[b].translation, target_rot[b].rotator(), unreal.Vector(1, 1, 1)) for b in bones}
        parent = {b: str(m.get_parent_name(b)) for b in bones}
        locals_ = []
        worst = 0.0
        for b in bones:
            p = parent[b]
            if p and p in new_world and p != "None":
                lt = new_world[b].make_relative(new_world[p])
            else:
                lt = new_world[b]
            locals_.append(lt)
            d = world[b].rotation.angular_distance(target_rot[b])
            worst = max(worst, d)
        m.set_bones_transforms(bones, locals_, True)
        ok = m.commit_skeleton_to_skeletal_mesh()
        log("%s 뼈 축을 캐릭터1과 같게 (가장 많이 돈 뼈 %.0f°), commit %s" % (name, worst * 57.2958, ok))
        out = baked_fbx(name, "shared")
        os.makedirs(os.path.dirname(out), exist_ok=True)
        opt = unreal.FbxExportOption()
        opt.set_editor_property("collision", False)
        opt.set_editor_property("export_morph_targets", False)
        opt.set_editor_property("level_of_detail", False)
        task = unreal.AssetExportTask()
        task.set_editor_property("object", mesh)
        task.set_editor_property("filename", out)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_identical", True)
        task.set_editor_property("prompt", False)
        task.set_editor_property("options", opt)
        task.set_editor_property("exporter", unreal.SkeletalMeshExporterFBX())
        exported = unreal.Exporter.run_asset_export_task(task)
        log("%s 내보내기 %s → %s" % (name, exported, out))
        if exported:
            import_fbx(name, out, 1.0, skeleton=skel)
            got = EAL.load_asset("%s/%s" % (c["dest"], c["mesh"]))
            gs = got.get_editor_property("skeleton") if got else None
            log("%s 다시 임포트: 스켈레톤 = %s" % (name, gs.get_path_name() if gs else None))


# ───────────── 텍스처 ─────────────
def do_textures(names):
    for name in names:
        c = CHARS[name]
        tdir = c["dest"] + "/Textures"
        ensure_dir(tdir)
        for m in MAPS:
            src = os.path.join(SRC, name, "Textures", "T_%s_%s.jpg" % (name, m))
            asset = "T_VH%s_%s" % (name, m)
            if not os.path.exists(src):
                log("%s: %s 텍스처 없음 → 건너뜀 (재질에서 평평한 기본값)" % (name, m))
                continue
            t = unreal.AssetImportTask()
            t.set_editor_property("filename", src)
            t.set_editor_property("destination_path", tdir)
            t.set_editor_property("destination_name", asset)
            t.set_editor_property("automated", True)
            t.set_editor_property("replace_existing", True)
            t.set_editor_property("save", False)
            TOOLS.import_asset_tasks([t])
            tex = EAL.load_asset("%s/%s" % (tdir, asset))
            if tex is None:
                unreal.log_warning("[CharBuild] 텍스처 임포트 실패: " + src)
                continue
            tex.set_editor_property("max_texture_size", 2048)
            tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_CHARACTER)
            if m == "Normal":
                tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
                tex.set_editor_property("srgb", False)
            elif m in ("Metallic", "Roughness"):
                tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
                tex.set_editor_property("srgb", False)
            else:
                tex.set_editor_property("srgb", True)
            EAL.save_loaded_asset(tex, False)
        log("%s 텍스처 4장 → %s" % (name, tdir))


# ───────────── 재질 ─────────────
def _expr(mat, cls, x, y):
    return unreal.MaterialEditingLibrary.create_material_expression(mat, cls, x, y)


def build_master():
    MEL = unreal.MaterialEditingLibrary
    ensure_dir(MAT_DIR)
    if EAL.does_asset_exist(MASTER):
        mat = EAL.load_asset(MASTER)
        MEL.delete_all_material_expressions(mat)
    else:
        mat = TOOLS.create_asset("M_VHCharacter", MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    # 기본 텍스처는 샘플러 종류(색·노멀·마스크)와 압축이 맞아야 재질이 컴파일된다 → 의적 텍스처를 기본값으로 쓴다
    def thief_tex(m):
        return EAL.load_asset("%s/Textures/T_VHThief_%s" % (CHARS["Thief"]["dest"], m))

    def tex_param(name, x, y, sampler, tex):
        e = _expr(mat, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
        e.set_editor_property("parameter_name", name)
        e.set_editor_property("sampler_type", sampler)
        if tex:
            e.set_editor_property("texture", tex)
        return e

    def scalar(name, v, x, y):
        e = _expr(mat, unreal.MaterialExpressionScalarParameter, x, y)
        e.set_editor_property("parameter_name", name)
        e.set_editor_property("default_value", v)
        return e

    def vec(name, v, x, y):
        e = _expr(mat, unreal.MaterialExpressionVectorParameter, x, y)
        e.set_editor_property("parameter_name", name)
        e.set_editor_property("default_value", v)
        return e

    ST = unreal.MaterialSamplerType
    color = tex_param("Color", -1400, -300, ST.SAMPLERTYPE_COLOR, thief_tex("Color"))
    normal = tex_param("Normal", -600, 300, ST.SAMPLERTYPE_NORMAL, thief_tex("Normal"))
    metal = tex_param("Metallic", -600, 0, ST.SAMPLERTYPE_MASKS, thief_tex("Metallic"))
    rough = tex_param("Roughness", -600, 150, ST.SAMPLERTYPE_MASKS, thief_tex("Roughness"))

    # 채널 나누기: R, G, B
    def mask(src, r, g, b, x, y):
        e = _expr(mat, unreal.MaterialExpressionComponentMask, x, y)
        e.set_editor_property("r", r)
        e.set_editor_property("g", g)
        e.set_editor_property("b", b)
        e.set_editor_property("a", False)
        MEL.connect_material_expressions(src, "RGB", e, "")
        return e

    def op(cls, a, b, x, y, a_out="", b_out=""):
        e = _expr(mat, cls, x, y)
        MEL.connect_material_expressions(a, a_out, e, "A")
        if b is not None:
            MEL.connect_material_expressions(b, b_out, e, "B")
        return e

    def const(v, x, y):
        e = _expr(mat, unreal.MaterialExpressionConstant, x, y)
        e.set_editor_property("r", v)
        return e

    cr = mask(color, True, False, False, -1150, -500)
    cg = mask(color, False, True, False, -1150, -420)
    cb = mask(color, False, False, True, -1150, -340)

    # 발광 마스크 = 하늘색(빨강은 낮고 초록·파랑은 높음): saturate((min(G,B) - R - 0.25) * 6)
    gb = op(unreal.MaterialExpressionMin, cg, cb, -950, -620)
    d = op(unreal.MaterialExpressionSubtract, gb, cr, -800, -620)
    d2 = op(unreal.MaterialExpressionSubtract, d, const(0.25, -950, -540), -650, -620)
    d3 = op(unreal.MaterialExpressionMultiply, d2, const(6.0, -800, -540), -500, -620)
    glow_mask = _expr(mat, unreal.MaterialExpressionSaturate, -350, -620)
    MEL.connect_material_expressions(d3, "", glow_mask, "")
    glow_strength = scalar("GlowStrength", 0.0, -350, -540)
    glow_tint = vec("GlowTint", unreal.LinearColor(0.35, 0.85, 1.0, 1.0), -350, -460)
    g1 = op(unreal.MaterialExpressionMultiply, glow_mask, glow_strength, -150, -620)
    emissive = op(unreal.MaterialExpressionMultiply, g1, glow_tint, 0, -620)

    # 옷 색 바꾸기 (선택, 기본 꺼짐): 올리브색 옷(초록이 가장 크고 채도 있음) 마스크 → ClothTint 쪽으로 섞기
    # 마스크 = saturate((G - max(R,B)) * 8) × ClothTintAmount
    rb = op(unreal.MaterialExpressionMax, cr, cb, -950, -200)
    gm = op(unreal.MaterialExpressionSubtract, cg, rb, -800, -200)
    gm2 = op(unreal.MaterialExpressionMultiply, gm, const(8.0, -950, -120), -650, -200)
    cloth_mask = _expr(mat, unreal.MaterialExpressionSaturate, -500, -200)
    MEL.connect_material_expressions(gm2, "", cloth_mask, "")
    amount = scalar("ClothTintAmount", 0.0, -500, -120)
    cm = op(unreal.MaterialExpressionMultiply, cloth_mask, amount, -350, -200)
    # 밝기는 원래 옷 밝기를 살린다: tint × (원래 밝기 × 2)
    lum = _expr(mat, unreal.MaterialExpressionDesaturation, -950, -40)
    MEL.connect_material_expressions(color, "RGB", lum, "")
    tint = vec("ClothTint", unreal.LinearColor(0.8, 0.2, 0.2, 1.0), -800, 40)
    lum2 = op(unreal.MaterialExpressionMultiply, lum, const(2.2, -950, 40), -650, -40)
    tinted = op(unreal.MaterialExpressionMultiply, lum2, tint, -500, -40)
    lerp = _expr(mat, unreal.MaterialExpressionLinearInterpolate, -200, -300)
    MEL.connect_material_expressions(color, "RGB", lerp, "A")
    MEL.connect_material_expressions(tinted, "", lerp, "B")
    MEL.connect_material_expressions(cm, "", lerp, "Alpha")

    rough_scale = scalar("RoughnessScale", 1.0, -400, 220)
    rough_mul = op(unreal.MaterialExpressionMultiply, rough, rough_scale, -250, 150, "R")

    MP = unreal.MaterialProperty
    MEL.connect_material_property(lerp, "", MP.MP_BASE_COLOR)
    MEL.connect_material_property(metal, "R", MP.MP_METALLIC)
    MEL.connect_material_property(rough_mul, "", MP.MP_ROUGHNESS)
    MEL.connect_material_property(normal, "RGB", MP.MP_NORMAL)
    MEL.connect_material_property(emissive, "", MP.MP_EMISSIVE_COLOR)
    mat.set_editor_property("used_with_skeletal_mesh", True)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat, False)
    log("공용 재질 " + MASTER)
    return mat


def do_material(names):
    master = build_master()
    MEL = unreal.MaterialEditingLibrary
    for name in names:
        c = CHARS[name]
        mi_name = "MI_VH%s" % name
        mi_path = "%s/%s" % (c["dest"], mi_name)
        if EAL.does_asset_exist(mi_path):
            mi = EAL.load_asset(mi_path)
        else:
            mi = TOOLS.create_asset(mi_name, c["dest"], unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        MEL.set_material_instance_parent(mi, master)
        for m in MAPS:
            tp = "%s/Textures/T_VH%s_%s" % (c["dest"], name, m)
            tex = EAL.load_asset(tp) if EAL.does_asset_exist(tp) else None
            if tex is None and m == "Normal":
                # 노멀 맵이 없는 캐릭터(캐릭터2·3): 재질 기본값(캐릭터1 노멀)이 쓰이면 무늬가 틀리므로 평평한 노멀로
                tex = EAL.load_asset("/Engine/EngineMaterials/FlatNormal")
            if tex:
                MEL.set_material_instance_texture_parameter_value(mi, m, tex)
        MEL.set_material_instance_scalar_parameter_value(mi, "GlowStrength", c["glow"])
        MEL.update_material_instance(mi)
        EAL.save_loaded_asset(mi, False)
        mesh = EAL.load_asset("%s/%s" % (c["dest"], c["mesh"]))
        if mesh:
            mats = mesh.get_editor_property("materials")
            for i in range(len(mats)):
                sm = mats[i]
                sm.set_editor_property("material_interface", mi)
                mats[i] = sm
            mesh.set_editor_property("materials", mats)
            EAL.save_loaded_asset(mesh, False)
            log("%s 재질 슬롯 %d개 → %s" % (name, len(mats), mi_name))


# ───────────── 확인 ─────────────
KEY_BONES = ["mixamorig:Hips", "mixamorig:Head", "mixamorig:HeadTop_End", "mixamorig:LeftHand", "mixamorig:RightHand",
             "mixamorig:LeftToeBase", "mixamorig:RightToeBase", "mixamorig:LeftFoot", "mixamorig:RightArm", "mixamorig:RightForeArm"]


def do_inspect(names):
    APE = unreal.AnimPoseExtensions
    for name in names:
        c = CHARS[name]
        mesh = EAL.load_asset("%s/%s" % (c["dest"], c["mesh"]))
        if mesh is None:
            unreal.log_warning("[CharBuild] 메시 없음: " + name)
            continue
        b = mesh.get_bounds()
        log("%s 경계: 가운데 (%.1f, %.1f, %.1f) 반크기 (%.1f, %.1f, %.1f)" % (name, b.origin.x, b.origin.y, b.origin.z, b.box_extent.x, b.box_extent.y, b.box_extent.z))
        skel = mesh.get_editor_property("skeleton")
        pose = APE.get_reference_pose(skel)
        bones = [str(x) for x in APE.get_bone_names(pose)]
        log("%s 뼈 %d개: %s" % (name, len(bones), ", ".join(bones)))
        for bn in bones:
            if any(k in bn for k in ("Hips", "Spine", "Neck", "Head", "Arm", "Hand", "Leg", "Foot", "Toe", "Shoulder")) and "Thumb" not in bn:
                t = APE.get_ref_bone_pose(pose, bn, unreal.AnimPoseSpaces.WORLD)
                l = t.translation
                r = t.rotation.rotator()
                log("  %-28s (%7.1f, %7.1f, %7.1f) rot(p %.0f y %.0f r %.0f)" % (bn, l.x, l.y, l.z, r.pitch, r.yaw, r.roll))


def main(argv):
    if in_pie():
        unreal.log_error("[CharBuild] 플레이(PIE) 중에는 돌리지 않습니다. 플레이를 멈추고 다시 실행하세요.")
        return
    steps = [a for a in argv if a in ("mesh", "fixscale", "share", "textures", "material", "inspect", "all")]
    names = [a for a in argv if a in CHARS] or list(CHARS.keys())
    if "all" in steps:
        steps = ["mesh", "fixscale", "share", "textures", "material", "inspect"]
    for s in steps:
        {"mesh": do_mesh, "fixscale": do_fixscale, "share": do_share, "textures": do_textures, "material": do_material, "inspect": do_inspect}[s](names)
    log("끝: %s %s" % (steps, names))


main(sys.argv[1:])
