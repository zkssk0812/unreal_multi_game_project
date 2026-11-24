# -*- coding: utf-8 -*-
"""
효과음 표 DA_VHSoundConfig 만들기·채우기 (에디터 Cmd 칸: py "C:/UnrealProjects/MCPTest/Tools/Audio/setup_sounds.py" 단계...)

단계
  asset   : /Game/VaultHeist/Audio/DA_VHSoundConfig (모든 종류 칸을 빈 상태로) + 거리 감쇠 ATT_VHSfx (3m까지 그대로, 25m에서 0)
  import  : SourceArt/Audio/<팩>/ 의 .ogg/.wav 를 /Game/VaultHeist/Audio/<팩> 으로 임포트 (사용자 확인 후 받은 CC0 파일만)
  fill    : 아래 PLAN 표대로 칸에 넣는다 (없는 파일은 건너뜀)
  all     : asset import fill

소리 목록·출처: Docs/효과음_목록.md. 플레이 중에는 돌리지 않는다.
"""
import os
import sys
import unreal

EAL = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SRC = os.path.join(PROJ, "SourceArt", "Audio")
DIR = "/Game/VaultHeist/Audio"
DA = DIR + "/DA_VHSoundConfig"
ATT = DIR + "/ATT_VHSfx"

# 종류 → (파일들(팩/이름, 확장자 없이), 음량, 높낮이 최소, 최대, 겹침 방지 초, 2D)  — Docs/효과음_목록.md 와 같은 표
PLAN = {
    "SWING_BATON": (["RPG/knifeSlice", "RPG/knifeSlice2"], 0.7, 0.8, 0.9, 0.0, False),
    "SWING_SWORD": (["RPG/drawKnife1", "RPG/drawKnife2", "RPG/drawKnife3"], 0.8, 0.95, 1.1, 0.0, False),
    "SWING_BEAM": (["SciFi/forceField_000", "SciFi/forceField_001"], 0.6, 1.3, 1.5, 0.0, False),
    "FIRE_PISTOL": (["SciFi/laserSmall_000", "SciFi/laserSmall_001", "SciFi/laserSmall_002"], 0.6, 0.95, 1.05, 0.0, False),
    "FIRE_FLAME": (["SciFi/thrusterFire_000", "SciFi/thrusterFire_001"], 0.5, 0.9, 1.1, 0.18, False),
    "FIRE_RAILGUN": (["SciFi/laserLarge_000", "SciFi/laserLarge_001"], 0.9, 0.9, 1.0, 0.0, False),
    "FIRE_TASER": (["Digital/zap1", "Digital/zap2"], 0.7, 0.95, 1.05, 0.0, False),
    "THROW_GRENADE": (["RPG/cloth1", "RPG/cloth2"], 0.6, 0.9, 1.1, 0.0, False),
    "PLACE_MINE": (["Impact/impactMetal_light_000", "Impact/impactMetal_light_001"], 0.6, 0.9, 1.0, 0.0, False),
    "GUARD_GUN": (["SciFi/laserRetro_000", "SciFi/laserRetro_001", "SciFi/laserRetro_002"], 0.5, 0.95, 1.05, 0.08, False),
    "EXPLOSION": (["SciFi/explosionCrunch_000", "SciFi/explosionCrunch_001", "SciFi/lowFrequency_explosion_000"], 1.0, 0.9, 1.05, 0.0, False),
    "ELECTRIC_SHOCK": (["Digital/zapThreeToneDown", "SciFi/forceField_002"], 0.8, 0.9, 1.0, 0.3, False),
    "FLASHBANG": (["Digital/phaserUp3", "SciFi/explosionCrunch_002"], 0.9, 1.1, 1.2, 0.0, False),
    "SMOKE": (["SciFi/slime_000"], 0.6, 0.6, 0.7, 0.0, False),
    "HIT": (["Impact/impactPunch_medium_000", "Impact/impactPunch_medium_001", "Impact/impactPunch_medium_002"], 0.8, 0.95, 1.05, 0.05, False),
    "DOWNED": (["Impact/impactSoft_heavy_000", "Impact/impactSoft_heavy_001"], 1.0, 0.85, 0.95, 0.0, False),
    "FOOTSTEP": (["Impact/footstep_wood_000", "Impact/footstep_wood_001", "Impact/footstep_wood_002", "Impact/footstep_wood_003", "Impact/footstep_wood_004"], 0.35, 0.9, 1.1, 0.12, False),
    "ROLL": (["RPG/cloth3", "RPG/cloth4"], 0.6, 0.9, 1.0, 0.0, False),
    "SAFE_DIAL": (["Interface/tick_001", "Interface/tick_002", "RPG/metalClick"], 0.5, 0.9, 1.1, 0.0, False),
    "SAFE_OPEN": (["RPG/metalLatch", "RPG/doorOpen_1"], 0.9, 0.95, 1.0, 0.0, False),
    "BOX_OPEN": (["Impact/impactPlank_medium_000", "RPG/creak1"], 0.9, 0.95, 1.05, 0.0, False),
    "PICK_UP": (["RPG/handleCoins", "RPG/handleCoins2"], 0.7, 0.95, 1.05, 0.0, False),
    "RETURN": (["Casino/chips-stack-1", "Interface/confirmation_001"], 0.9, 1.0, 1.0, 0.0, True),
    "HEAL": (["Interface/confirmation_002"], 0.7, 1.0, 1.0, 0.0, False),
    "UI_CLICK": (["Interface/click_001", "Interface/click_002"], 0.5, 1.0, 1.0, 0.0, True),
}

# ── 2026-11-19 무기별 소리 (Docs/무기_연출_계획_2026-11-19.md) ──
# 7번째 값(선택): {"layers": [(파일들, 음량, 높낮이 배율, 늦게 초)], "loop": True(반복 소리 → <이름>_Loop 사본을 Looping 으로), "att": "small"(6m 감쇠)}
# 파일 이름 끝의 * = 그 앞부분으로 시작하는 파일 전부. Lyra = Lyra Starter Game 에서 꺼낸 WAV (Tools/External/lyra_export.py)
# 음량 기준: 권총 1.0 → 근접 0.7~0.8, 레일건·폭발 1.2~1.4, 반복음 0.2~0.6
PLAN.update({
    # 삼단봉: 짧고 높은 바람 "휙" / 둔탁한 "퍽"
    "SWING_BATON": (["Lyra/Lyra_MeleeWhoosh*"], 0.6, 1.3, 1.45, 0.0, False),
    "HIT_BATON": (["Lyra/Lyra_MeleeImpact*"], 0.85, 0.8, 0.9, 0.05, False,
                  {"layers": [(["Impact/impactPunch_heavy_000", "Impact/impactPunch_heavy_001", "Impact/impactPunch_heavy_002"], 0.6, 0.85, 0.0)]}),
    # 도검: 금속 칼날 "슈악" (칼 뽑는 쇳소리 + 낮은 바람) / 베는 금속음
    "SWING_SWORD": (["RPG/drawKnife1", "RPG/drawKnife2", "RPG/drawKnife3"], 0.65, 1.05, 1.15, 0.0, False,
                    {"layers": [(["Lyra/Lyra_MeleeWhoosh*"], 0.45, 0.75, 0.0)]}),
    "HIT_SWORD": (["RPG/knifeSlice", "RPG/knifeSlice2"], 0.8, 0.95, 1.05, 0.05, False,
                  {"layers": [(["Impact/impactMetal_light_000", "Impact/impactMetal_light_001", "Impact/impactMetal_light_002"], 0.45, 1.25, 0.0)]}),
    # 빔 소드: 낮은 "부웅" / 들고 있는 동안 "웅—" 반복 / 맞으면 "지직"
    "SWING_BEAM": (["SciFi/forceField_000", "SciFi/forceField_001"], 0.75, 0.7, 0.8, 0.0, False,
                   {"layers": [(["Lyra/Lyra_Dash*"], 0.45, 0.75, 0.0)]}),
    "BEAM_HUM": (["SciFi/spaceEngineLow_000"], 0.22, 0.55, 0.6, 0.0, False, {"loop": True, "att": "small"}),
    "HIT_BEAM": (["SciFi/laserSmall_000", "SciFi/laserSmall_001"], 0.6, 0.55, 0.65, 0.05, False,
                 {"layers": [(["Digital/zap1", "Digital/zap2"], 0.4, 0.8, 0.0)]}),
})
# 원거리 4종 (2단계)
PLAN.update({
    # 권총: Lyra 실제 총성 (펀치 + 단맛 + 기계음) — 기준 음량 1.0
    "FIRE_PISTOL": (["Lyra/Weapons_Pistol_Punch-Close*"], 1.0, 0.97, 1.03, 0.0, False,
                    {"layers": [(["Lyra/Weapons_Pistol_Sweetener*"], 0.5, 1.0, 0.0), (["Lyra/Weapons_Pistol_Mech*"], 0.3, 1.0, 0.03)]}),
    "HIT_PISTOL": (["Lyra/Lyra_Plyr_BulletImpact*"], 0.7, 0.95, 1.05, 0.05, False),
    # 화염방사기: 시작(점화 + 바람) → 누르는 동안 반복 → 끝(바람이 빠짐)
    "FLAME_START": (["SciFi/thrusterFire_001"], 0.55, 0.95, 1.05, 0.0, False, {"layers": [(["Lyra/Lyra_Dash*"], 0.3, 0.6, 0.0)]}),
    "FLAME_LOOP": (["SciFi/thrusterFire_000"], 0.5, 0.85, 0.9, 0.0, False, {"loop": True}),
    "FLAME_END": (["Lyra/Lyra_Dash*"], 0.35, 0.65, 0.7, 0.0, False),
    # 레일건: 묵직한 "쾅"(라이플 총성을 낮게 + 저음 폭발 + 레이저) + 빔 동안 높은 방전음
    "FIRE_RAILGUN": (["Lyra/Weapons_Rifle2_Punch*"], 1.2, 0.6, 0.65, 0.0, False,
                     {"layers": [(["SciFi/lowFrequency_explosion_000", "SciFi/lowFrequency_explosion_001"], 0.9, 0.9, 0.0),
                                 (["SciFi/laserLarge_000", "SciFi/laserLarge_001"], 0.6, 0.7, 0.0),
                                 (["SciFi/spaceEngineLarge_000"], 0.4, 1.6, 0.06, 0.8, 1.0)]}),   # 방전 "위이잉↓": 높낮이 1.6 → 0.8 을 1초 동안 (2026-11-20)
    "HIT_RAILGUN": (["Impact/impactMetal_heavy_000", "Impact/impactMetal_heavy_001"], 0.8, 0.9, 1.0, 0.1, False,
                    {"layers": [(["SciFi/explosionCrunch_000"], 0.5, 1.2, 0.0)]}),
    # 테이저건: "치직" / 맞으면 지지직 + 경직 2초 동안 작게 반복
    "FIRE_TASER": (["Digital/zapTwoTone", "Digital/zapTwoTone2"], 0.8, 1.1, 1.2, 0.0, False,
                   {"layers": [(["Digital/zap1", "Digital/zap2"], 0.6, 1.0, 0.0), (["SciFi/forceField_002"], 0.35, 1.6, 0.0)]}),
    "HIT_TASER": (["Digital/zapThreeToneDown"], 0.7, 0.95, 1.05, 0.1, False),
    "STUN_LOOP": (["SciFi/computerNoise_000"], 0.25, 1.4, 1.5, 0.0, False, {"loop": True}),
})
# 투척·설치 3종 (3단계)
PLAN.update({
    # 수류탄: 핀 "딸깍" → 0.12초 뒤 던지는 바람 / 날아가는 휙휙 / 폭발 = Lyra 수류탄 폭발 여러 겹 (가장 큰 소리 1.35)
    "THROW_GRENADE": (["RPG/metalClick"], 0.7, 1.1, 1.2, 0.0, False,
                      {"layers": [(["Lyra/Lyra_MeleeWhoosh*"], 0.55, 0.65, 0.12)]}),
    "GRENADE_FLIGHT": (["Lyra/Lyra_JumpPad_Whoosh*"], 0.45, 1.3, 1.45, 0.0, False),
    "EXPLOSION": (["Lyra/Explosions_Grenade_Punch-Close*"], 1.35, 0.95, 1.05, 0.0, False,
                  {"layers": [(["Lyra/grenade_explode_body*"], 0.9, 1.0, 0.0), (["Lyra/grenade_explode_whack*"], 0.7, 1.0, 0.0),
                              (["SciFi/lowFrequency_explosion_000", "SciFi/lowFrequency_explosion_001"], 0.6, 0.9, 0.0),
                              (["Lyra/grenade_explode_sizzle*"], 0.45, 1.0, 0.12)]}),
    # 폭발 지뢰: 설치 "철컥" / 감지 "삑삑삑"(0.5초 안 3번, 점점 높게) / 폭발 = 수류탄과 같은 급, 한 단계 작게
    "PLACE_MINE": (["RPG/metalLatch"], 0.75, 0.95, 1.05, 0.0, False,
                   {"layers": [(["Impact/impactMetal_light_000", "Impact/impactMetal_light_001"], 0.5, 0.8, 0.05)]}),
    "MINE_WARN": (["Interface/tick_001"], 0.8, 1.6, 1.6, 0.4, False,
                  {"layers": [(["Interface/tick_001"], 0.85, 1.12, 0.17), (["Interface/tick_001"], 0.9, 1.25, 0.33)]}),
    "BLAST_MINE_EXPLOSION": (["Lyra/Explosions_Grenade_Punch-Close*"], 1.15, 1.05, 1.12, 0.0, False,
                             {"layers": [(["Lyra/grenade_explode_thump*"], 0.8, 1.0, 0.0), (["Lyra/grenade_explode_whack*"], 0.55, 1.1, 0.0)]}),
    # 감전 지뢰: 작동 중 낮은 웅웅(가까이서만) / 터질 때 "파지직" (경직 시작도 같은 칸)
    "SHOCK_MINE_HUM": (["SciFi/computerNoise_001"], 0.22, 0.55, 0.6, 0.0, False, {"loop": True, "att": "small"}),
    "ELECTRIC_SHOCK": (["Digital/zapThreeToneDown"], 0.9, 0.9, 1.0, 0.3, False,
                       {"layers": [(["Digital/zap1", "Digital/zap2"], 0.8, 0.8, 0.0), (["SciFi/forceField_002"], 0.6, 1.3, 0.0),
                                   (["Digital/zapTwoTone", "Digital/zapTwoTone2"], 0.5, 0.7, 0.08)]}),
})
# 레일건 충전 (2026-11-20): 클릭 → 1초 동안 "위이잉↑" 에너지 모으는 소리 → 발사(위의 FIRE_RAILGUN). 이미 받은 Kenney 소리만 씀
#   본 소리 Digital/powerUp3 (1.15초, 올라가는 신스음) + 방전과 같은 spaceEngineLarge 를 높낮이 0.55 → 1.5배로 1초 동안 올림(방전 1.6→0.8 과 거울)
#   + forceField_003 (0.95초, 전기 지지직) 작게. 발사·취소 순간에 끊는다(UVHSoundConfig::PlayAttached)
PLAN.update({
    "RAILGUN_CHARGE": (["Digital/powerUp3"], 0.5, 1.0, 1.0, 0.0, False,
                       {"layers": [(["SciFi/spaceEngineLarge_000"], 0.45, 0.55, 0.0, 1.5, 1.0),
                                   (["SciFi/forceField_003"], 0.3, 1.15, 0.0)]}),
})
# 방 입구 미닫이 문 (2026-11-21): Kenney RPG Audio(CC0)의 문 소리. 열림 2종·닫힘 4종 중 무작위, 15m 쯤까지(공통 감쇠)
PLAN.update({
    "DOOR_OPEN": (["RPG/doorOpen_1", "RPG/doorOpen_2"], 0.55, 1.05, 1.2, 0.15, False),
    "DOOR_CLOSE": (["RPG/doorClose_1", "RPG/doorClose_2", "RPG/doorClose_3", "RPG/doorClose_4"], 0.5, 1.0, 1.15, 0.15, False),
})
# 닫힌 문에 총알·테이저가 맞음 (2026-11-24, 서버가 닫힌 문을 벽처럼 침): 나무판 "턱" + 작은 금속음. 이미 받은 Kenney Impact 소리만 씀
PLAN.update({
    "DOOR_HIT": (["Impact/impactPlank_medium_000"], 0.75, 0.9, 1.1, 0.05, False,
                 {"layers": [(["Impact/impactMetal_light_000", "Impact/impactMetal_light_001", "Impact/impactMetal_light_002"], 0.3, 1.3, 0.0)]}),
})
ATT_SMALL = DIR + "/ATT_VHSfxSmall"


def log(m):
    unreal.log("[Sound] " + m)


def ensure_dir(p):
    if not EAL.does_directory_exist(p):
        EAL.make_directory(p)


def do_asset():
    ensure_dir(DIR)
    if not EAL.does_asset_exist(ATT):
        att = TOOLS.create_asset("ATT_VHSfx", DIR, unreal.SoundAttenuation, unreal.SoundAttenuationFactory())
        s = att.get_editor_property("attenuation")
        s.import_text("(bAttenuate=True,bSpatialize=True,AttenuationShape=Sphere,AttenuationShapeExtents=(X=300.000000,Y=0,Z=0),FalloffDistance=2200.000000,DistanceAlgorithm=ATTENUATION_Linear)")
        att.set_editor_property("attenuation", s)
        EAL.save_loaded_asset(att, False)
    da = EAL.load_asset(DA) if EAL.does_asset_exist(DA) else None
    if da is None:
        da = TOOLS.create_asset("DA_VHSoundConfig", DIR, unreal.VHSoundConfig, unreal.DataAssetFactory())
    da.set_editor_property("attenuation", EAL.load_asset(ATT))
    sounds = da.get_editor_property("sounds")
    for name in PLAN:
        key = getattr(unreal.VHSound, name)
        if key not in sounds:
            sounds[key] = unreal.VHSoundSlot()
    da.set_editor_property("sounds", sounds)
    EAL.save_loaded_asset(da, False)
    log("DA 칸 %d개, 감쇠 %s" % (len(sounds), ATT))


def ensure_small_att():
    """가까이서만 들리는 감쇠 (3m 까지 그대로 → 6m 에서 0): 빔 소드 웅·감전 지뢰 웅웅"""
    if not EAL.does_asset_exist(ATT_SMALL):
        att = TOOLS.create_asset("ATT_VHSfxSmall", DIR, unreal.SoundAttenuation, unreal.SoundAttenuationFactory())
        st = att.get_editor_property("attenuation")
        st.import_text("(bAttenuate=True,bSpatialize=True,AttenuationShape=Sphere,AttenuationShapeExtents=(X=300.000000,Y=0,Z=0),FalloffDistance=300.000000,DistanceAlgorithm=ATTENUATION_Linear)")
        att.set_editor_property("attenuation", st)
        EAL.save_loaded_asset(att, False)
    return EAL.load_asset(ATT_SMALL)


def do_import_new():
    do_import(only_new=True)


def do_import(only_new=False):
    if not os.path.isdir(SRC):
        log("SourceArt/Audio 없음 (아직 받지 않음)")
        return
    # 팩 전체(약 470개)가 아니라 PLAN 표에 쓴 파일만 임포트한다
    wanted = set()
    loops = set()
    for name, entry in PLAN.items():
        extra = entry[6] if len(entry) > 6 else {}
        files = expand(entry[0])
        wanted.update(files)
        for lf, *_ in extra.get("layers", []):
            wanted.update(expand(lf))
        if extra.get("loop"):
            loops.update(files)
    tasks = []
    for rel, suffix in [(r, "") for r in sorted(wanted)] + [(r, "_Loop") for r in sorted(loops)]:
        pack, name = rel.split("/")
        if only_new and EAL.does_asset_exist("%s/%s/%s" % (DIR, pack, clean(name) + suffix)):
            continue    # importnew: 이미 있는 에셋은 다시 임포트하지 않는다 (바뀌지 않은 에셋이 커밋에 섞이지 않게)
        for ext in (".ogg", ".wav"):
            path = os.path.join(SRC, pack, name + ext)
            if os.path.exists(path):
                t = unreal.AssetImportTask()
                t.set_editor_property("filename", path)
                t.set_editor_property("destination_path", "%s/%s" % (DIR, pack))
                t.set_editor_property("destination_name", clean(name) + suffix)
                t.set_editor_property("automated", True)
                t.set_editor_property("replace_existing", True)
                t.set_editor_property("save", True)
                tasks.append(t)
    TOOLS.import_asset_tasks(tasks)
    # 반복 소리 사본은 Looping 을 켠다 (같은 파일의 한 번 소리와 따로)
    for rel in loops:
        path = asset_name(rel) + "_Loop"
        w = EAL.load_asset(path) if EAL.does_asset_exist(path) else None
        if w:
            w.set_editor_property("looping", True)
            # 들리지 않는 거리로 가도 멈추지 않고 조용히 계속 (멈추면 컴포넌트가 지워져서 매번 다시 시작됐다)
            w.set_editor_property("virtualization_mode", unreal.VirtualizationMode.PLAY_WHEN_SILENT)
            EAL.save_loaded_asset(w, False)
    log("임포트 %d개 (반복 사본 %d)" % (len(tasks), len(loops)))


def clean(name):
    return name.replace("-", "_").replace(" ", "_").replace("(", "").replace(")", "")


def asset_name(rel):
    pack, name = rel.split("/")
    return "%s/%s/%s" % (DIR, pack, clean(name))


def expand(files):
    """'팩/앞부분*' → 그 팩 폴더에서 앞부분으로 시작하는 파일 전부 (확장자 뺀 '팩/이름')"""
    out = []
    for f in files:
        if not f.endswith("*"):
            out.append(f)
            continue
        pack, prefix = f[:-1].split("/")
        folder = os.path.join(SRC, pack)
        if os.path.isdir(folder):
            out += sorted("%s/%s" % (pack, os.path.splitext(n)[0]) for n in os.listdir(folder)
                          if n.startswith(prefix) and n.lower().endswith((".wav", ".ogg")))
    return out


def do_fill(only=None):
    da = EAL.load_asset(DA)
    sounds = da.get_editor_property("sounds")
    filled = 0
    small = ensure_small_att()
    for name, entry in PLAN.items():
        if only and name not in only:
            continue    # fillone: 이름을 준 칸만 다시 채운다 (다른 칸은 그대로)
        files, vol, pmin, pmax, gap, two_d = entry[:6]
        extra = entry[6] if len(entry) > 6 else {}
        key = getattr(unreal.VHSound, name)
        slot = unreal.VHSoundSlot()
        suffix = "_Loop" if extra.get("loop") else ""
        files = expand(files)
        waves = [EAL.load_asset(asset_name(f) + suffix) for f in files if EAL.does_asset_exist(asset_name(f) + suffix)]
        layers = []
        for lentry in extra.get("layers", []):
            # (파일, 음량, 높낮이, 늦게) [+ (끝 높낮이, 미끄러지는 초): 재생하는 동안 높낮이가 내려간다]
            lfiles, lvol, lpitch, ldelay = lentry[:4]
            lglide = lentry[4:6]
            lw = [EAL.load_asset(asset_name(f)) for f in expand(lfiles) if EAL.does_asset_exist(asset_name(f))]
            if not lw:
                unreal.log_warning("[Sound] %s: 겹 파일 없음 %s" % (name, lfiles))
                continue
            layer = unreal.VHSoundLayer()
            layer.set_editor_property("sounds", lw)
            layer.set_editor_property("volume", lvol)
            layer.set_editor_property("pitch_scale", lpitch)
            layer.set_editor_property("delay", ldelay)
            if len(lglide) == 2:
                layer.set_editor_property("pitch_end_scale", lglide[0])
                layer.set_editor_property("pitch_glide_seconds", lglide[1])
            layers.append(layer)
        slot.set_editor_property("layers", layers)
        if extra.get("att") == "small":
            slot.set_editor_property("attenuation_override", small)
        slot.set_editor_property("sounds", [w for w in waves if w])
        slot.set_editor_property("volume", vol)
        slot.set_editor_property("pitch_min", pmin)
        slot.set_editor_property("pitch_max", pmax)
        slot.set_editor_property("min_interval", gap)
        for prop in ("b2d", "2d", "b2D"):    # C++ 이름 b2D 의 파이썬 이름이 엔진 버전마다 다를 수 있다
            try:
                slot.set_editor_property(prop, two_d)
                break
            except Exception:
                continue
        sounds[key] = slot
        filled += 1 if waves else 0
        if not waves:
            unreal.log_warning("[Sound] %s: 파일 없음 %s" % (name, files))
    da.set_editor_property("sounds", sounds)
    EAL.save_loaded_asset(da, False)
    log("소리를 넣은 칸 %d / %d" % (filled, len(PLAN)))


def main(argv):
    # nocheck: 커맨드렛(-run=pythonscript)에서 돌릴 때. LevelEditorSubsystem 이 없어 확인 호출이 에디터를 죽인다
    if "nocheck" in argv:
        argv = [a for a in argv if a != "nocheck"]
    else:
        try:
            if unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor():
                unreal.log_error("[Sound] 플레이 중에는 돌리지 않습니다")
                return
        except Exception:
            pass
    steps = argv or ["asset"]
    if steps[0] == "fillone":
        # py setup_sounds.py fillone DOOR_HIT [이름 …] : 그 칸만 만들고 채운다
        names = steps[1:]
        do_asset()
        do_fill(set(names))
        return
    if "all" in steps:
        steps = ["asset", "import", "fill"]
    for s in steps:
        {"asset": do_asset, "import": do_import, "importnew": do_import_new, "fill": do_fill}[s]()


main(sys.argv[1:])
