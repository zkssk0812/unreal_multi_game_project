# -*- coding: utf-8 -*-
"""
Lyra Starter Game(C:/UnrealProjects/Lyra)에서 쓸 것만 꺼낸다 — Lyra 에디터를 명령줄(커맨드렛)로 열어 실행.
  UnrealEditor-Cmd.exe C:/UnrealProjects/Lyra/Lyra.uproject -run=pythonscript -script="C:/UnrealProjects/MCPTest/Tools/External/lyra_export.py 단계..." -unattended -nosplash

단계
  sounds : 아래 SOUNDS 표의 사운드 웨이브를 WAV 로 → MCPTest/SourceArt/Audio/Lyra/<이름>.wav
           (사운드 웨이브를 Migrate 하면 Lyra 의 사운드 클래스·서브믹스·모듈레이션이 줄줄이 따라와서 원본 WAV 만 꺼낸다)
  list   : 나이아가라·애니메이션 몽타주·시퀀스 목록 → MCPTest/Saved/External/lyra_assets.txt (고르기용)

라이선스: Lyra 콘텐츠는 에픽 콘텐츠(언리얼 엔진 프로젝트에서 사용 가능). 출처 Docs/외부에셋_출처.md
"""
import os
import sys
import unreal

OUT_AUDIO = "C:/UnrealProjects/MCPTest/SourceArt/Audio/Lyra"
OUT_LIST = "C:/UnrealProjects/MCPTest/Saved/External/lyra_assets.txt"

# (폴더, 이름 앞부분, 최대 개수) — 이름이 앞부분으로 시작하는 사운드 웨이브
SOUNDS = [
    ("/Game/Audio/Sounds/SFX", "Lyra_MeleeWhoosh", 4),
    ("/Game/Audio/Sounds/SFX", "Lyra_MeleeImpact", 3),
    ("/Game/Audio/Sounds/SFX", "Lyra_Dash", 2),
    ("/Game/Audio/Sounds/SFX", "Lyra_JumpPad_Whoosh", 2),
    ("/Game/Audio/Sounds/Weapons/Pistol", "Weapons_Pistol_Punch-Close", 4),
    ("/Game/Audio/Sounds/Weapons/Pistol", "Weapons_Pistol_Sweetener", 4),
    ("/Game/Audio/Sounds/Weapons/Pistol", "Weapons_Pistol_Mech", 3),
    ("/Game/Audio/SoundWaves/Weapons", "sfx_Weapon_Pistol_MainLayer_nl", 4),
    ("/Game/Audio/Sounds/Weapons/Rifle2", "Weapons_Rifle2_Punch", 4),
    ("/Game/Audio/Sounds/Weapons/Rifle2", "Weapons_Rifle2_Sweetener", 4),
    ("/Game/Audio/SoundWaves/Weapons", "sfx_Weapon_Shotgun_MainLayer_nl", 3),
    ("/Game/Audio/SoundWaves/Weapons", "sfx_Weapon_AutoRifle_MainTail", 2),
    ("/Game/Audio/Sounds/Grenade", "grenade_", 18),
    ("/Game/Audio/Sounds/Explosions", "Explosions_Grenade_Punch-Close", 4),
    ("/Game/Audio/Sounds/Explosions", "Explosions_Grenade_SFX", 4),
    ("/Game/Audio/Sounds/Impacts", "Lyra_Plyr_BulletImpact", 4),
    ("/Game/Audio/SoundWaves/Weapons", "SFX_BulletImpact", 4),
    ("/Game/Audio/Sounds/WhizBys", "Lyra_BulletIn_Close", 3),
    ("/Game/Audio/Sounds/Foley_Misc", "Lyra_WeaponSwap", 3),
]


def log(msg):
    unreal.log("[LyraExport] " + msg)


def do_sounds():
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    os.makedirs(OUT_AUDIO, exist_ok=True)
    n = 0
    for folder, prefix, limit in SOUNDS:
        datas = reg.get_assets_by_path(folder, recursive=False)
        names = sorted(str(d.asset_name) for d in datas if str(d.asset_class_path.asset_name) == "SoundWave" and str(d.asset_name).startswith(prefix))
        for name in names[:limit]:
            asset = unreal.load_asset("%s/%s" % (folder, name))
            task = unreal.AssetExportTask()
            task.object = asset
            task.filename = "%s/%s.wav" % (OUT_AUDIO, name)
            task.automated = True
            task.prompt = False
            task.replace_identical = True
            task.exporter = unreal.SoundExporterWAV()
            ok = unreal.Exporter.run_asset_export_task(task)
            n += 1 if ok else 0
            log("%s %s" % ("OK" if ok else "FAIL", name))
        if not names:
            log("없음: %s/%s*" % (folder, prefix))
    log("WAV %d개 → %s" % (n, OUT_AUDIO))


def do_list():
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    os.makedirs(os.path.dirname(OUT_LIST), exist_ok=True)
    rows = []
    for cls in ("NiagaraSystem", "AnimMontage", "AnimSequence"):
        flt = unreal.ARFilter(class_paths=[unreal.TopLevelAssetPath("/Script/Niagara" if cls == "NiagaraSystem" else "/Script/Engine", cls)],
                              package_paths=["/Game"], recursive_paths=True)
        for d in reg.get_assets(flt):
            rows.append("%s\t%s" % (cls, d.package_name))
    rows.sort()
    with open(OUT_LIST, "w", encoding="utf-8") as f:
        f.write("\n".join(rows))
    log("목록 %d줄 → %s" % (len(rows), OUT_LIST))


# 2026-11-19: 처음 5개(+ 콘크리트 피격·수류탄 폭발)는 의존 290MB(연기 플립북 146MB·4K 돌 텍스처) → 총구·탄 궤적·몸 피격 불꽃만
FX = ["/Game/Effects/Particles/Weapons/NS_WeaponFire_MuzzleFlash_Rifle", "/Game/Effects/Particles/Weapons/NS_WeaponFire_Tracer",
      "/Game/Effects/Particles/Impacts/NS_ImactSparksCharacter"]
OUT_DEPS = "C:/UnrealProjects/MCPTest/Saved/External/lyra_fx_deps.txt"


def deps_of(roots):
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    opt = unreal.AssetRegistryDependencyOptions(include_soft_package_references=False, include_hard_package_references=True,
                                                include_searchable_names=False, include_soft_management_references=False,
                                                include_hard_management_references=False)
    seen, todo = set(), list(roots)
    while todo:
        p = todo.pop()
        if p in seen or not p.startswith("/Game/"):
            continue
        seen.add(p)
        for d in reg.get_dependencies(p, opt) or []:
            todo.append(str(d))
    return sorted(seen)


def do_deps():
    """고른 나이아가라가 끌고 오는 패키지와 디스크 크기 (Migrate 전에 확인)"""
    rows, total = [], 0
    for p in deps_of(FX):
        f = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir()) + p[len("/Game/"):] + ".uasset"
        size = os.path.getsize(f) if os.path.exists(f) else 0
        total += size
        rows.append("%8.1fKB  %s" % (size / 1024.0, p))
    os.makedirs(os.path.dirname(OUT_DEPS), exist_ok=True)
    with open(OUT_DEPS, "w", encoding="utf-8") as fh:
        fh.write("\n".join(rows) + "\n합계 %.1fMB, %d개\n" % (total / 1048576.0, len(rows)))
    log("의존 %d개 %.1fMB → %s" % (len(rows), total / 1048576.0, OUT_DEPS))


def do_migrate():
    """고른 나이아가라 + 의존 패키지를 MCPTest/Content 로 복사 (원래 /Game 경로 그대로). 같은 이름이 있으면 덮어쓰지 않는다"""
    dst = "C:/UnrealProjects/MCPTest/Content/"
    src_root = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())
    import shutil
    n = 0
    for p in deps_of(FX):
        rel = p[len("/Game/"):]
        for ext in (".uasset", ".uexp", ".ubulk"):
            s = src_root + rel + ext
            d = dst + rel + ext
            if os.path.exists(s):
                if os.path.exists(d):
                    log("이미 있음(건너뜀): " + d)
                    continue
                os.makedirs(os.path.dirname(d), exist_ok=True)
                shutil.copy2(s, d)
                n += 1
    log("복사 %d개 파일" % n)


# 반동 곡선 뽑기: Lyra 마네킹(앞 = 메시 +Y, 오른쪽 = 메시 −X, 위 = +Z) 의 오른손·허리 움직임을 첫 프레임 기준으로
RECOIL = {"pistol": "/Game/Characters/Heroes/Mannequin/Animations/Actions/MM_Pistol_Fire",
          "rifle": "/Game/Characters/Heroes/Mannequin/Animations/Actions/MM_Rifle_Fire",
          "toss": "/Game/Characters/Heroes/Mannequin/Animations/Actions/MM_Rifle_GrenadeToss"}
OUT_RECOIL = "C:/UnrealProjects/MCPTest/Tools/External/lyra_recoil.json"


def do_recoil():
    import json
    import math
    APE = unreal.AnimPoseExtensions
    out = {}
    for key, path in RECOIL.items():
        anim = unreal.load_asset(path)
        if not anim:
            log("없음: " + path)
            continue
        length = anim.get_play_length()
        frames = []
        base = None
        n = max(2, int(length * 30))
        for i in range(n + 1):
            t = length * i / n
            pose = APE.get_anim_pose_at_time(anim, t, unreal.AnimPoseEvaluationOptions())
            hr = APE.get_bone_pose(pose, "hand_r", unreal.AnimPoseSpaces.WORLD)
            hl = APE.get_bone_pose(pose, "hand_l", unreal.AnimPoseSpaces.WORLD)
            sp = APE.get_bone_pose(pose, "spine_05", unreal.AnimPoseSpaces.WORLD)
            if base is None:
                base = (hr, hl, sp)
            def loc(tr, b):
                d = tr.translation - b.translation
                return [round(d.y, 2), round(-d.x, 2), round(d.z, 2)]    # 앞·오른쪽·위
            def rot(tr, b):
                # 첫 프레임에서 앞(+Y)을 가리키던 손 기준 방향이 지금 어디를 가리키나
                f = tr.transform_direction(b.inverse_transform_direction(unreal.Vector(0, 1, 0)))
                pitch = math.degrees(math.asin(max(-1.0, min(1.0, f.z))))
                yaw = math.degrees(math.atan2(-f.x, f.y))
                return [round(pitch, 2), round(yaw, 2)]
            frames.append({"t": round(t, 4), "hand_r": loc(hr, base[0]), "rot_r": rot(hr, base[0]),
                           "hand_l": loc(hl, base[1]), "spine": rot(sp, base[2])})
        out[key] = {"path": path, "length": length, "frames": frames}
        log("반동 %s: %.2f초 %d프레임" % (key, length, len(frames)))
    with open(OUT_RECOIL, "w", encoding="utf-8") as fh:
        json.dump(out, fh, ensure_ascii=False, indent=1)
    log("→ " + OUT_RECOIL)


STEPS = {"sounds": do_sounds, "list": do_list, "deps": do_deps, "migrate": do_migrate, "recoil": do_recoil}
for a in sys.argv[1:]:
    if a in STEPS:
        STEPS[a]()
