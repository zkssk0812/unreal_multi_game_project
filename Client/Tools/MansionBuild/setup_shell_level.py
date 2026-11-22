"""
새 맵 레벨(L_Mansion_Shell)을 우리 게임용으로 맞춘다 (에디터 안에서 실행, 여러 번 실행해도 같은 결과).

패키지 스크립트가 만든 것(아웃라이너 Mansion/ 폴더)은 건드리지 않고, 우리 것은 VaultHeist/ 폴더에 둔다.
(패키지 스크립트를 다시 돌리면 Mansion/ 폴더만 지워지므로 우리 것은 남는다. 단 PlayerStart는 다시 옮겨야 한다.)

실행: 에디터 아래쪽 Cmd 칸에
    py "C:/UnrealProjects/MCPTest/Tools/MansionBuild/setup_shell_level.py" base [yaw=각도]
    py "C:/UnrealProjects/MCPTest/Tools/MansionBuild/setup_shell_level.py" rooms
  base  : 게임 모드, 화면 설정 액터(탑뷰 방향·밝기 덮어쓰기 끔), PlayerStart를 현관 안으로, 서버에서 닫힌 바깥 문 2곳 막기,
          커튼 충돌 끔, 펜던트 조명 기구 모양을 게임에서 숨김(빛은 그대로)
  walls : Shell의 벽·기둥·창틀 재질 칸을 '캐릭터 앞 벽 투명' 재질(텍스처 유지)로 덮어쓰기
  lights [fill=N] : 채움광의 바닥 반사(흰 점) 끄기. fill=N 이면 채움광을 N개 중 1개만 남김(무거울 때)
  rooms : 서버 맵의 방 35개를 AVHRoomVolume으로 (같은 id·같은 범위·직사각형 조각). C++의 ServerRoomId·Rects가 있어야 한다
  doors : 문 위 벽(above_door) 39곳에 미닫이 문(AVHDoor, 보이는 연출만·충돌 없음). 바깥 문 2곳은 잠긴 문으로 놓고
          예전 어두운 판(ClosedDoor)은 게임에서 숨김(충돌은 그대로 = 서버처럼 막힘). 계산은 door_plan.py
결과는 출력 로그의 [ShellSetup] 줄.
"""
import json
import os
import sys

import unreal

LEVEL = "/Game/VaultHeist/Maps/Mansion/L_Mansion_Shell"
GAME_MODE = "/Game/VaultHeist/Blueprints/BP_VHInteractionTestGameMode.BP_VHInteractionTestGameMode_C"
HERE = os.path.dirname(os.path.abspath(__file__))
SERVER_MAP = os.path.join(HERE, "mansion_1f_server_1b53272.json")   # 서버 커밋 1b53272의 맵 사본(읽기 전용)
ROOM_NAMES = os.path.join(HERE, "room_names.json")                  # 방 id → 화면에 쓰는 한국어 이름
ROOM_HEIGHT = 400.0     # 벽 높이 (Mansion_1F_Collision.json meta.wall_height)

actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)


def log(msg):
    unreal.log(f"[ShellSetup] {msg}")


def open_level():
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if not (world and world.get_path_name().startswith(LEVEL + ".")):
        les.load_level(LEVEL)
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if not (world and world.get_path_name().startswith(LEVEL + ".")):
        raise RuntimeError(f"{LEVEL} 을 열지 못함")
    return world


def actors_in(folder):
    return [a for a in actor_sub.get_all_level_actors() if str(a.get_folder_path()) == folder]


def clear_folder(folder):
    old = actors_in(folder)
    for a in old:
        actor_sub.destroy_actor(a)
    return len(old)


def setup_base(world, yaw):
    # 1) 게임 모드: 옛 레벨(L_Mansion_1F)과 같은 것
    gm = unreal.load_class(None, GAME_MODE)
    ws = world.get_world_settings()
    ws.set_editor_property("default_game_mode", gm)
    log(f"게임 모드 = {gm.get_name() if gm else None}")

    # 2) 화면 설정 액터: 탑뷰 방향. 밝기는 패키지 조명을 그대로 쓰므로 달빛 덮어쓰기를 끈다
    view_class = unreal.load_class(None, "/Script/MCPTest.VHLevelViewSettings")
    views = [a for a in actor_sub.get_all_level_actors() if a.get_class() == view_class]
    view = views[0] if views else actor_sub.spawn_actor_from_class(view_class, unreal.Vector(0, 0, 500))
    view.set_actor_label("VHLevelViewSettings")
    view.set_folder_path("VaultHeist/Settings")
    if yaw is not None:
        view.set_editor_property("TopViewYaw", float(yaw))
    view.set_editor_property("bApplyConfigLighting", False)
    log(f"화면 설정: TopViewYaw={view.get_editor_property('TopViewYaw')}, 밝기 덮어쓰기 끔")

    # 3) PlayerStart: 패키지는 현관문 밖(-200, 3200)에 둔다. 서버는 그 문을 닫으므로 현관 안으로 옮긴다
    #    (실서버에서는 SC_GAME_START의 위치로 다시 옮겨지므로, 패킷이 오기 전과 가짜 서버에서만 쓰인다)
    for a in actor_sub.get_all_level_actors():
        if isinstance(a, unreal.PlayerStart):
            a.set_actor_location(unreal.Vector(-200.0, 2650.0, 100.0), False, True)
            a.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=0.0, yaw=-90.0), True)
            log(f"PlayerStart '{a.get_actor_label()}' → (-200, 2650, 100) yaw -90")

    # 4) 바깥 문: 서버는 건물 밖을 게임 공간으로 보지 않고 바깥 문 2곳을 닫는다(closed_exterior_door).
    #    같은 자리에 닫힌 문(어두운 판)을 놓아 화면과 충돌을 서버와 맞춘다
    removed = clear_folder("VaultHeist/ClosedDoors")
    cube = unreal.load_asset("/Engine/BasicShapes/Cube")
    dark = unreal.load_asset("/Game/Mansion/Building/M_Metal_Black")
    data = json.load(open(SERVER_MAP, encoding="utf-8"))
    doors = [c for c in data["colliders"]["static"] if c.get("kind") == "closed_exterior_door"]
    for c in doors:
        cx, cy, cz = (c["xmin"] + c["xmax"]) / 2, (c["ymin"] + c["ymax"]) / 2, (c["zmin"] + c["zmax"]) / 2
        a = actor_sub.spawn_actor_from_object(cube, unreal.Vector(cx, cy, cz))
        a.set_actor_scale3d(unreal.Vector((c["xmax"] - c["xmin"]) / 100.0, (c["ymax"] - c["ymin"]) / 100.0, (c["zmax"] - c["zmin"]) / 100.0))
        a.set_actor_label("ClosedDoor_" + c["id"].split("_")[-1])
        a.set_folder_path("VaultHeist/ClosedDoors")
        comp = a.get_component_by_class(unreal.StaticMeshComponent)
        if dark:
            comp.set_material(0, dark)
        comp.set_collision_profile_name("BlockAll")
    log(f"닫힌 바깥 문 {len(doors)}개 (이전 것 {removed}개 지움), 재질 {'M_Metal_Black' if dark else '기본'}")

    # 5) 커튼: 서버 충돌 파일에 없는 소품(9개)이라 서버에서는 지나갈 수 있다 → 클라에서도 막지 않는다
    curtains = 0
    for a in actor_sub.get_all_level_actors():
        if str(a.get_folder_path()).startswith("Mansion/Props") and a.get_actor_label().startswith("Sheer_Curtains"):
            for comp in a.get_components_by_class(unreal.StaticMeshComponent):
                comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
            curtains += 1
    log(f"커튼 {curtains}개 충돌 끔 (서버 충돌체에 없음)")

    # 6) 펜던트 조명 기구(14개, 높이 220~255cm): 탑뷰에서 바로 아래 바닥과 캐릭터를 가린다
    #    → 기구 모양만 게임에서 숨긴다. 빛(Mansion/Lights 의 Pendant_* 점 조명)은 다른 액터라 그대로 켜져 있다 [사용자 결정 2026-11-16]
    pendants = 0
    for a in actor_sub.get_all_level_actors():
        if str(a.get_folder_path()).startswith("Mansion/Props") and a.get_actor_label().startswith("Pendant_"):
            a.set_actor_hidden_in_game(True)
            pendants += 1
    pendant_lights = [a for a in actor_sub.get_all_level_actors()
                      if str(a.get_folder_path()) == "Mansion/Lights" and a.get_actor_label().startswith("Pendant_")]
    lit = sum(1 for a in pendant_lights
              if not a.get_editor_property("hidden") and a.get_component_by_class(unreal.PointLightComponent).is_visible())
    log(f"펜던트 기구 {pendants}개 게임에서 숨김, 펜던트 빛 {len(pendant_lights)}개 중 {lit}개 켜짐(그대로)")


def setup_rooms(world):
    room_class = unreal.load_class(None, "/Script/MCPTest.VHRoomVolume")
    names = json.load(open(ROOM_NAMES, encoding="utf-8")) if os.path.isfile(ROOM_NAMES) else {}
    data = json.load(open(SERVER_MAP, encoding="utf-8"))
    removed = clear_folder("VaultHeist/Rooms")
    total_rects = 0
    for r in data["rooms"]:
        rects = r.get("rects") or [r]
        xmin = min(a["xmin"] for a in rects); xmax = max(a["xmax"] for a in rects)
        ymin = min(a["ymin"] for a in rects); ymax = max(a["ymax"] for a in rects)
        a = actor_sub.spawn_actor_from_class(room_class, unreal.Vector((xmin + xmax) / 2, (ymin + ymax) / 2, ROOM_HEIGHT / 2))
        a.set_actor_label("Room_" + r["id"])
        a.set_folder_path("VaultHeist/Rooms")
        area = a.get_component_by_class(unreal.BoxComponent)
        area.set_box_extent(unreal.Vector((xmax - xmin) / 2, (ymax - ymin) / 2, ROOM_HEIGHT / 2), False)
        a.set_editor_property("ServerRoomId", r["id"])
        a.set_editor_property("RoomName", names.get(r["id"], r["id"]))
        a.set_editor_property("bAllowReturnPoint", bool(r.get("return_point", True)))
        boxes = []
        for q in rects:
            b = unreal.Box2D()
            b.set_editor_property("min", unreal.Vector2D(q["xmin"], q["ymin"]))
            b.set_editor_property("max", unreal.Vector2D(q["xmax"], q["ymax"]))
            b.set_editor_property("is_valid", True)
            boxes.append(b)
        a.set_editor_property("Rects", boxes)
        total_rects += len(boxes)
    log(f"방 영역 {len(data['rooms'])}개, 직사각형 조각 {total_rects}개 (이전 것 {removed}개 지움)")


FADE_MASTER_SRC = "/Game/VaultHeist/Materials/M_VHWallFade"       # 기존 벽 투명 재질(단색)
FADE_MASTER = "/Game/VaultHeist/Materials/M_VHWallFadeTex"        # 텍스처를 쓰는 사본
FADE_DIR = "/Game/VaultHeist/Materials/MansionShell"              # Shell 벽 재질 칸마다 하나씩 만드는 인스턴스
FADE_PREFIXES = ("M_Wall_", "M_Column", "M_Metal_Black")          # 캐릭터를 가리는 것: 벽·기둥·창틀 (바닥·유리는 그대로)


def setup_walls(world):
    """Shell의 벽·기둥 재질 칸을 '캐릭터 앞 벽 투명' 재질로 바꾼다(레벨 액터의 재질 덮어쓰기, 메시 에셋은 그대로)."""
    eal = unreal.EditorAssetLibrary
    mel = unreal.MaterialEditingLibrary
    tools = unreal.AssetToolsHelpers.get_asset_tools()

    # 1) 텍스처를 쓰는 벽 투명 재질: 기존 재질을 복사해 색(WallColor)에 텍스처(WallTexture)를 곱한다. 지우는 계산은 그대로
    if not eal.does_asset_exist(FADE_MASTER):
        eal.duplicate_asset(FADE_MASTER_SRC, FADE_MASTER)
        m = unreal.load_asset(FADE_MASTER)
        color = mel.get_material_property_input_node(m, unreal.MaterialProperty.MP_BASE_COLOR)
        tex = mel.create_material_expression(m, unreal.MaterialExpressionTextureSampleParameter2D, -900, -400)
        tex.set_editor_property("parameter_name", "WallTexture")
        tex.set_editor_property("texture", unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture"))
        mul = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -600, -400)
        mel.connect_material_expressions(tex, "RGB", mul, "A")
        mel.connect_material_expressions(color, "", mul, "B")
        mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_BASE_COLOR)
        mel.recompile_material(m)
        eal.save_loaded_asset(m)
        log(f"재질 생성 {FADE_MASTER} (기존 색 노드 = {color.get_class().get_name() if color else None})")
    master = unreal.load_asset(FADE_MASTER)

    # 2) Shell 액터의 재질 칸마다 인스턴스를 만들어 덮어쓴다
    shell = [a for a in actor_sub.get_all_level_actors() if a.get_actor_label() == "Mansion_Shell"]
    if not shell:
        raise RuntimeError("Mansion_Shell 액터가 없음")
    comp = shell[0].get_component_by_class(unreal.StaticMeshComponent)
    mesh = comp.get_editor_property("static_mesh")
    done = []
    for slot in comp.get_material_slot_names():
        name = str(slot)
        if not name.startswith(FADE_PREFIXES):
            continue
        index = comp.get_material_index(slot)
        src = mesh.get_material(index)
        tex = None
        tint = unreal.LinearColor(1, 1, 1, 1)
        if isinstance(src, unreal.MaterialInstance):
            tex = mel.get_material_instance_texture_parameter_value(src, "DiffuseColorMap")
            weight = mel.get_material_instance_scalar_parameter_value(src, "DiffuseColorMapWeight")
            if not tex or weight < 0.5:
                tex = None
                tint = mel.get_material_instance_vector_parameter_value(src, "DiffuseColor")
        mi_name = "MI_Shell_" + name[2:]
        mi_path = f"{FADE_DIR}/{mi_name}"
        mi = unreal.load_asset(mi_path) if eal.does_asset_exist(mi_path) else tools.create_asset(
            mi_name, FADE_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        mel.set_material_instance_parent(mi, master)
        mel.set_material_instance_vector_parameter_value(mi, "WallColor", tint)
        if tex:
            mel.set_material_instance_texture_parameter_value(mi, "WallTexture", tex)
        mel.update_material_instance(mi)
        eal.save_loaded_asset(mi)
        comp.set_material(index, mi)
        done.append(f"{name}({'텍스처' if tex else '색 %.2f,%.2f,%.2f' % (tint.r, tint.g, tint.b)})")
    log(f"벽 투명 재질 {len(done)}칸: {', '.join(done)}")


def setup_lights(world, keep_every):
    """
    패키지 조명(낮 분위기)은 그대로 쓰되 탑뷰 게임에 맞게 손본다.
      - 채움광(Fill_*, 277개, 그림자 없음)의 반사광을 끈다: 탑뷰에서는 광원이 바닥에 흰 점으로 비쳐 보인다
      - keep_every=N (fill=N): 채움광을 N개 중 1개만 남기고 나머지는 끈다(무거울 때). 남긴 것은 밝기를 N배로 올려 전체 밝기를 비슷하게 맞춘다
        원래 밝기는 액터 태그(VHBaseIntensity=값)에 적어 두어 여러 번 실행해도 값이 쌓이지 않는다
    """
    fills = sorted([a for a in actor_sub.get_all_level_actors()
                    if str(a.get_folder_path()) == "Mansion/Lights" and a.get_actor_label().startswith("Fill_")],
                   key=lambda a: a.get_actor_label())
    kept = 0
    for i, a in enumerate(fills):
        c = a.get_component_by_class(unreal.PointLightComponent)
        base = None
        for t in a.tags:
            if str(t).startswith("VHBaseIntensity="):
                base = float(str(t).split("=", 1)[1])
        tags = list(a.tags)
        if base is None:
            base = c.get_editor_property("intensity")
            tags.append(unreal.Name(f"VHBaseIntensity={base}"))
        if not any(str(t) == "VHFill" for t in tags):
            tags.append(unreal.Name("VHFill"))      # 게임 중 실행 옵션 -VHFill=N 이 채움광을 찾는 표시
        a.set_editor_property("tags", tags)
        on = (i % keep_every) == 0
        c.set_editor_property("specular_scale", 0.0)
        c.set_visibility(on)
        c.set_intensity(base * keep_every if on else base)
        a.set_actor_hidden_in_game(not on)
        kept += 1 if on else 0
    log(f"채움광 {len(fills)}개: 반사광 끔, {kept}개 켬 (fill={keep_every})")


def setup_doors(world):
    """방 입구 문 39개 (door_plan.py 계산: 위치·방향·폭·벽 두께·양쪽 구역·양문/한 짝·미닫이 방향·잠김)"""
    sys.path.insert(0, HERE)
    import importlib
    import door_plan
    importlib.reload(door_plan)
    door_class = unreal.load_class(None, "/Script/MCPTest.VHDoor")
    if door_class is None:
        raise RuntimeError("AVHDoor 클래스가 없음 (C++ 빌드 필요)")
    removed = clear_folder("VaultHeist/Doors")
    plan = door_plan.plan(SERVER_MAP)
    for d in plan:
        a = actor_sub.spawn_actor_from_class(door_class, unreal.Vector(d["x"], d["y"], 0.0), unreal.Rotator(roll=0.0, pitch=0.0, yaw=d["yaw"]))
        a.set_editor_property("Width", float(d["width"]))
        a.set_editor_property("WallThickness", float(d["thick"]))
        a.set_editor_property("OpeningHeight", 240.0)
        a.set_editor_property("bDouble", bool(d["double"]))
        a.set_editor_property("SlideSign", float(d["slide"]))
        a.set_editor_property("bLocked", bool(d["locked"]))
        a.set_editor_property("ZoneA", d["zone_a"])
        a.set_editor_property("ZoneB", d["zone_b"])
        a.set_editor_property("ServerId", d["id"])
        a.set_actor_label(f"Door_{d['zone_a'] or 'Outside'}_{d['zone_b'] or 'Outside'}")
        a.set_folder_path("VaultHeist/Doors")
        a.set_actor_location(unreal.Vector(d["x"], d["y"], 0.0), False, False)   # 에디터 화면 갱신 (게임에서는 BeginPlay 가 다시 배치)
    # 예전 바깥 문 판: 게임에서는 숨기고(잠긴 문이 대신 보임) 충돌은 남긴다(서버도 막으므로 클라 이동도 막힘)
    plates = actors_in("VaultHeist/ClosedDoors")
    for a in plates:
        a.set_actor_hidden_in_game(True)
    log(f"문 {len(plan)}개 (양문 {sum(d['double'] for d in plan)}, 한 짝 {sum(not d['double'] for d in plan)}, "
        f"잠김 {sum(d['locked'] for d in plan)}), 이전 것 {removed}개 지움, 바깥 문 판 {len(plates)}개 게임에서 숨김(충돌 유지)")


def main():
    args = [a.lower() for a in sys.argv[1:]]
    yaw = None
    fill = None
    for a in args:
        if a.startswith("yaw="):
            yaw = float(a.split("=", 1)[1])
        if a.startswith("fill="):
            fill = max(1, int(a.split("=", 1)[1]))
    world = open_level()
    if "base" in args:
        setup_base(world, yaw)
    if "walls" in args:
        setup_walls(world)
    if "lights" in args or fill is not None:
        setup_lights(world, fill or 1)
    if "rooms" in args:
        setup_rooms(world)
    if "doors" in args:
        setup_doors(world)
    saved = les.save_current_level()
    log(f"완료 ({' '.join(args)}): 레벨 저장 {saved}")


try:
    main()
except Exception as e:
    import traceback
    unreal.log_error(f"[ShellSetup] 실패: {e}\n{traceback.format_exc()}")
