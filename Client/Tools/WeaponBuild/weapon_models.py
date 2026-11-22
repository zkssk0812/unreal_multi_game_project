# -*- coding: utf-8 -*-
"""
무기 모델 교체 도구 (2026-11-20). 에디터 아래쪽 Cmd 칸에서:
  py "C:/UnrealProjects/MCPTest/Tools/WeaponBuild/weapon_models.py" 단계 [인자...]

단계
  check                       : 무기 10종의 메시·소켓·크기를 출력 (소켓이 없으면 무엇을 대신 쓰는지)
  sockets                     : 지금 임시 모델 10개에 소켓을 넣는다 (지금 쓰는 자리와 똑같은 위치 → 화면은 그대로)
  import 번호 FBX경로 [rot=Yaw] [scale=배율]
                              : 새 FBX 를 /Game/VaultHeist/Weapons/Meshes/Modeler/NN_이름/ 에 임포트 → 재질 연결 → DA_VHWeaponCatalog 의 Mesh 를 새 모델로.
                                임시 모델(/Game/VaultHeist/Weapons/Meshes/SM_VHW_*)은 지우지 않는다 (백업, restore 로 되돌림)
  import_dir 폴더             : 폴더 안 FBX 를 파일 이름의 번호(VH-01 / VH01 / SM_VHW_01 / 01_)로 찾아 모두 import
  restore 번호|all            : DA 의 Mesh 를 임시 모델로 되돌린다 (손잡이 보정 GripOffset 도 원래 값으로)

재질: FBX 안의 재질 이름에 "Glow" 가 들어 있으면 분류 발광 재질(MI_VHW_Glow_근접·원거리·투척·설치)로 바꿔 끼운다
      (구상안 v2.2: 발광 색 = 분류). 나머지는 FBX 에서 만든 재질·텍스처를 그대로 쓴다.
소켓: FBX 의 빈 노드(Empty/Locator) 이름이 SOCKET_Muzzle 처럼 "SOCKET_" 으로 시작하면 언리얼 소켓이 된다 → Docs/무기_모델_제작규격.md
플레이(PIE) 중에는 돌리지 않는다. 교체 순서·확인 방법: Docs/무기_교체_가이드.md
"""
import json
import os
import re
import sys
import unreal

EAL = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
CATALOG = "/Game/VaultHeist/Weapons/Data/DA_VHWeaponCatalog"
CHARS = "/Game/VaultHeist/Weapons/Data/DA_VHCharacterVisuals"
TEMP_DIR = "/Game/VaultHeist/Weapons/Meshes"
NEW_DIR = "/Game/VaultHeist/Weapons/Meshes/Modeler"
MAT_DIR = "/Game/VaultHeist/Weapons/Materials"
BACKUP = os.path.join(PROJ, "Tools", "WeaponBuild", "weapon_mesh_backup.json")

# 번호 → (이름, 분류)
WEAPONS = {1: ("Baton", "Melee"), 2: ("Sword", "Melee"), 3: ("BeamSword", "Melee"), 4: ("Pistol", "Ranged"), 5: ("Flamethrower", "Ranged"),
           6: ("Railgun", "Ranged"), 7: ("Taser", "Ranged"), 8: ("Grenade", "Thrown"), 9: ("BlastMine", "Placed"), 10: ("ShockMine", "Placed")}
# 무기별로 있어야 하는 소켓 (제작 규격서와 같음). 없으면 경고만 하고 대신 값(자동 계산)을 쓴다
NEEDED = {"Melee": ["Grip", "BladeBase", "BladeTip"], "Ranged": ["Grip", "Muzzle"], "Thrown": ["Grip"], "Placed": []}
NEEDED_EXTRA = {5: ["SupportHand"], 6: ["SupportHand"], 9: ["SupportHand"], 10: ["SupportHand"]}
# 실제 크기 기준 (cm, 앞뒤 길이) — 구상안 v2.2 와 지금 임시 모델. 새 모델이 이것의 1/3 ~ 3배를 벗어나면 단위가 틀렸다고 경고
REAL_LENGTH = {1: 65, 2: 90, 3: 100, 4: 22, 5: 95, 6: 120, 7: 24, 8: 9, 9: 30, 10: 34}
POINTS = ["Muzzle", "BladeTip", "BladeBase", "Grip", "SupportHand"]


def log(m):
    unreal.log("[WeaponModel] " + m)


def warn(m):
    unreal.log_warning("[WeaponModel] " + m)


def in_pie():
    try:
        return unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    except Exception:
        return False


def V(x, y, z):
    return unreal.Vector(x, y, z)


def temp_path(n):
    name = WEAPONS[n][0]
    return "%s/SM_VHW_%02d_%s.SM_VHW_%02d_%s" % (TEMP_DIR, n, name, n, name)




def weapon_id(n):
    return getattr(unreal.VHWeaponId, ["", "BATON", "SWORD", "BEAM_SWORD", "PISTOL", "FLAMETHROWER", "RAILGUN", "TASER", "GRENADE", "BLAST_MINE", "SHOCK_MINE"][n])


def entry_index(ws, n):
    wid = weapon_id(n)
    for i in range(len(ws)):
        if ws[i].get_editor_property("id") == wid:
            return i
    return -1


def socket_names(mesh):
    return [p for p in POINTS if mesh and mesh.find_socket(p)]


def set_socket(mesh, name, loc):
    s = mesh.find_socket(name)
    if s is None:
        s = unreal.StaticMeshSocket(mesh)
        s.set_editor_property("socket_name", name)
        mesh.add_socket(s)
    s.set_editor_property("relative_location", loc)
    s.set_editor_property("relative_rotation", unreal.Rotator(0, 0, 0))
    s.set_editor_property("relative_scale", V(1, 1, 1))


def load_backup():
    if os.path.exists(BACKUP):
        with open(BACKUP, encoding="utf-8") as fh:
            return json.load(fh)
    return {}


def save_backup(data):
    with open(BACKUP, "w", encoding="utf-8") as fh:
        json.dump(data, fh, ensure_ascii=False, indent=2)


# ───────────── check ─────────────
def do_check():
    cat = EAL.load_asset(CATALOG)
    ws = cat.get_editor_property("weapons")
    for n in range(1, 11):
        i = entry_index(ws, n)
        if i < 0:
            warn("%02d: 무기 표에 칸이 없음" % n)
            continue
        w = ws[i]
        mesh = w.get_editor_property("mesh")
        if not mesh:
            warn("%02d %s: Mesh 칸이 비어 있음" % (n, WEAPONS[n][0]))
            continue
        b = mesh.get_bounding_box()
        length = (b.max.x - b.min.x) * w.get_editor_property("model_scale")
        have = socket_names(mesh)
        need = NEEDED[WEAPONS[n][1]] + NEEDED_EXTRA.get(n, [])
        missing = [p for p in need if p not in have]
        msg = "%02d %-12s %-28s 길이 %.0fcm (기준 %dcm) 소켓 %s" % (n, WEAPONS[n][0], mesh.get_name(), length, REAL_LENGTH[n], have or "없음")
        if missing:
            msg += " / 없음 → 대신 값 사용: %s" % missing
        log(msg)
        if length > REAL_LENGTH[n] * 3 or length < REAL_LENGTH[n] / 3:
            warn("%02d: 길이가 기준과 3배 넘게 다름 → 단위(cm)를 확인하거나 무기 표의 Model Scale 을 고치세요" % n)


# ───────────── sockets (지금 임시 모델) ─────────────
def do_sockets():
    """지금 화면과 같은 자리에 소켓을 만든다: 총구 = 앞 끝 가운데, 칼끝 = 앞 끝의 90%, 칼날 시작 = 25%, 손잡이 = 피벗,
    보조 손 = DA_VHCharacterVisuals 의 Left Hand On Weapon ÷ 표시 배율 (지뢰는 손잡이 보정 GripOffset 그대로라 Grip 소켓을 넣지 않는다)"""
    cat = EAL.load_asset(CATALOG)
    ws = cat.get_editor_property("weapons")
    rig = EAL.load_asset(CHARS).get_editor_property("default")
    poses = rig.get_editor_property("weapon_poses")
    rig_scale = rig.get_editor_property("weapon_scale")
    all_scale = cat.get_editor_property("held_scale")
    for n in range(1, 11):
        w = ws[entry_index(ws, n)]
        mesh = w.get_editor_property("mesh")
        if not mesh or not mesh.get_path_name().startswith(TEMP_DIR + "/SM_VHW_"):
            warn("%02d: 임시 모델이 아니라서 건너뜀 (%s)" % (n, mesh.get_path_name() if mesh else None))
            continue
        b = mesh.get_bounding_box()
        cy, cz = (b.min.y + b.max.y) * 0.5, (b.min.z + b.max.z) * 0.5
        cat_name = WEAPONS[n][1]
        made = []
        if cat_name == "Melee":
            set_socket(mesh, "BladeTip", V(b.max.x * 0.9, cy, cz))
            set_socket(mesh, "BladeBase", V(b.max.x * 0.25, cy, cz))
            made += ["BladeTip", "BladeBase"]
        if cat_name == "Ranged":
            set_socket(mesh, "Muzzle", V(b.max.x, cy, cz))
            made.append("Muzzle")
        if cat_name != "Placed":
            set_socket(mesh, "Grip", V(0, 0, 0))
            made.append("Grip")
        if n in NEEDED_EXTRA:
            wid = weapon_id(n)
            pose = poses[wid] if wid in poses else rig.get_editor_property("placed_pose" if cat_name == "Placed" else "ranged_pose")
            lh = pose.get_editor_property("left_hand_on_weapon")
            s = all_scale * w.get_editor_property("held_scale") * rig_scale
            set_socket(mesh, "SupportHand", V(lh.x / s, lh.y / s, lh.z / s))
            made.append("SupportHand(%.1f, %.1f, %.1f)" % (lh.x / s, lh.y / s, lh.z / s))
        EAL.save_loaded_asset(mesh, False)
        log("%02d %s 소켓: %s" % (n, mesh.get_name(), made))


# ───────────── import ─────────────
def import_fbx(fbx, folder, name):
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")   # 레거시 FBX (SOCKET_ 노드 → 소켓)
    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", True)
    ui.set_editor_property("import_as_skeletal", False)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
    ui.set_editor_property("import_animations", False)
    ui.set_editor_property("import_materials", True)
    ui.set_editor_property("import_textures", True)
    sm = ui.get_editor_property("static_mesh_import_data")
    sm.set_editor_property("combine_meshes", True)            # 여러 조각이어도 메시 하나로
    sm.set_editor_property("auto_generate_collision", False)  # 손에 드는 표시용이라 충돌 없음
    sm.set_editor_property("generate_lightmap_u_vs", False)
    ui.set_editor_property("static_mesh_import_data", sm)
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", fbx)
    t.set_editor_property("destination_path", folder)
    t.set_editor_property("destination_name", name)
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("save", True)
    t.set_editor_property("options", ui)
    TOOLS.import_asset_tasks([t])
    paths = [str(p) for p in t.get_editor_property("imported_object_paths")]
    meshes = [p for p in paths if isinstance(EAL.load_asset(p), unreal.StaticMesh)]
    return EAL.load_asset(meshes[0]) if meshes else None


def connect_materials(mesh, cat_name):
    glow = EAL.load_asset("%s/MI_VHW_Glow_%s" % (MAT_DIR, cat_name))
    mats = mesh.get_editor_property("static_materials")
    changed = []
    for i in range(len(mats)):
        m = mats[i]
        slot = str(m.get_editor_property("material_slot_name"))
        cur = m.get_editor_property("material_interface")
        if "glow" in slot.lower() or (cur and "glow" in cur.get_name().lower()):
            m.set_editor_property("material_interface", glow)
            mats[i] = m
            changed.append(slot)
    mesh.set_editor_property("static_materials", mats)
    return [str(m.get_editor_property("material_slot_name")) for m in mats], changed


def do_import(n, fbx, rot=None, scale=None):
    n = int(n)
    if n not in WEAPONS or not os.path.exists(fbx):
        unreal.log_error("[WeaponModel] 번호(1~10) 또는 파일이 틀림: %s %s" % (n, fbx))
        return
    name, cat_name = WEAPONS[n]
    folder = "%s/%02d_%s" % (NEW_DIR, n, name)
    asset_name = "SM_VHW_%02d_%s" % (n, name)
    mesh = import_fbx(fbx, folder, asset_name)
    if not mesh:
        unreal.log_error("[WeaponModel] %02d: FBX 에서 스태틱 메시를 찾지 못함 (%s)" % (n, fbx))
        return
    slots, glowed = connect_materials(mesh, cat_name)
    EAL.save_loaded_asset(mesh, False)

    cat = EAL.load_asset(CATALOG)
    ws = cat.get_editor_property("weapons")
    i = entry_index(ws, n)
    w = ws[i]
    backup = load_backup()
    key = "%02d" % n
    if key not in backup:   # 처음 바꿀 때의 값 = 임시 모델 값
        old = w.get_editor_property("mesh")
        g = w.get_editor_property("grip_offset")
        backup[key] = {"mesh": old.get_path_name() if old else temp_path(n),
                       "grip_offset": [g.translation.x, g.translation.y, g.translation.z, g.rotation.rotator().roll, g.rotation.rotator().pitch, g.rotation.rotator().yaw],
                       "model_rotation_yaw": w.get_editor_property("model_rotation").yaw, "model_scale": w.get_editor_property("model_scale")}
        save_backup(backup)
    w.set_editor_property("mesh", mesh)
    have = socket_names(mesh)
    if "Grip" in have:
        # Grip 소켓이 손 자리를 정하므로 임시 모델용 손잡이 보정(위치)은 0 으로 (회전은 그대로)
        g = w.get_editor_property("grip_offset")
        w.set_editor_property("grip_offset", unreal.Transform(location=V(0, 0, 0), rotation=g.rotation.rotator(), scale=[1, 1, 1]))
    if rot is not None:
        w.set_editor_property("model_rotation", unreal.Rotator(roll=0.0, pitch=0.0, yaw=float(rot)))
    if scale is not None:
        w.set_editor_property("model_scale", float(scale))
    ws[i] = w
    cat.set_editor_property("weapons", ws)
    EAL.save_loaded_asset(cat, False)
    need = NEEDED[cat_name] + NEEDED_EXTRA.get(n, [])
    missing = [p for p in need if p not in have]
    log("%02d %s ← %s : 재질 칸 %s (발광으로 바꿈 %s), 소켓 %s%s" % (n, name, os.path.basename(fbx), slots, glowed or "없음", have or "없음",
                                                      (" / 없음 → 대신 값: %s" % missing) if missing else ""))
    do_check_one(n)


def do_check_one(n):
    cat = EAL.load_asset(CATALOG)
    ws = cat.get_editor_property("weapons")
    w = ws[entry_index(ws, n)]
    mesh = w.get_editor_property("mesh")
    b = mesh.get_bounding_box()
    length = (b.max.x - b.min.x) * w.get_editor_property("model_scale")
    log("%02d 크기 %.0f × %.0f × %.0f cm (앞뒤 기준 %dcm)" % (n, length, (b.max.y - b.min.y), (b.max.z - b.min.z), REAL_LENGTH[n]))
    if length > REAL_LENGTH[n] * 3 or length < REAL_LENGTH[n] / 3:
        warn("%02d: 길이가 기준과 3배 넘게 다름 → 단위 확인, 또는 import 에 scale=배율" % n)
    if (b.max.y - b.min.y) > (b.max.x - b.min.x) * 1.5 and WEAPONS[n][1] != "Placed":
        warn("%02d: 옆(Y)이 앞뒤(X)보다 훨씬 김 → 앞 방향이 +Y 같음. import 에 rot=-90 (또는 90)" % n)


def number_of(filename):
    base = os.path.basename(filename)
    for pat in (r"VH[-_ ]?(\d{1,2})", r"SM_VHW_(\d{2})", r"^(\d{1,2})[_\- ]"):
        mm = re.search(pat, base, re.IGNORECASE)
        if mm and 1 <= int(mm.group(1)) <= 10:
            return int(mm.group(1))
    return None


def do_import_dir(folder):
    files = [os.path.join(folder, f) for f in sorted(os.listdir(folder)) if f.lower().endswith(".fbx")]
    for f in files:
        n = number_of(f)
        if n is None:
            warn("번호를 알 수 없어 건너뜀: %s (파일 이름에 VH-01 처럼 번호를 넣어 주세요)" % os.path.basename(f))
            continue
        do_import(n, f)


def do_restore(which):
    backup = load_backup()
    cat = EAL.load_asset(CATALOG)
    ws = cat.get_editor_property("weapons")
    nums = range(1, 11) if which == "all" else [int(which)]
    for n in nums:
        b = backup.get("%02d" % n)
        i = entry_index(ws, n)
        w = ws[i]
        mesh_path = b["mesh"] if b else temp_path(n)
        w.set_editor_property("mesh", EAL.load_asset(mesh_path))
        if b:
            g = b["grip_offset"]
            w.set_editor_property("grip_offset", unreal.Transform(location=V(g[0], g[1], g[2]), rotation=unreal.Rotator(roll=g[3], pitch=g[4], yaw=g[5]), scale=[1, 1, 1]))
            w.set_editor_property("model_rotation", unreal.Rotator(roll=0.0, pitch=0.0, yaw=b.get("model_rotation_yaw", 0.0)))
            w.set_editor_property("model_scale", b.get("model_scale", 1.0))
        ws[i] = w
        log("%02d 되돌림 → %s" % (n, mesh_path))
    cat.set_editor_property("weapons", ws)
    EAL.save_loaded_asset(cat, False)


def main(argv):
    if in_pie():
        unreal.log_error("[WeaponModel] 플레이 중에는 돌리지 않습니다 (■ 정지 후)")
        return
    if not argv:
        log(__doc__)
        return
    step = argv[0]
    opts = dict(a.split("=", 1) for a in argv[1:] if "=" in a)
    plain = [a for a in argv[1:] if "=" not in a]
    if step == "check":
        do_check()
    elif step == "sockets":
        do_sockets()
    elif step == "import":
        do_import(plain[0], plain[1], opts.get("rot"), opts.get("scale"))
    elif step == "import_dir":
        do_import_dir(plain[0])
    elif step == "restore":
        do_restore(plain[0] if plain else "all")
    else:
        unreal.log_error("[WeaponModel] 모르는 단계: %s" % step)


main(sys.argv[1:])
