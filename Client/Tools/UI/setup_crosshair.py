# -*- coding: utf-8 -*-
"""
조준점 무기별 값 채우기 (2026-11-25): DA_VHWeaponCatalog → Weapons[i].Crosshair, UnarmedCrosshair, SkillTaserCrosshair.
자동(-1) 칸을 실제 숫자로 적어 둬서 DA 에서 바로 보이고 고칠 수 있게 한다 (값은 C++ UVHWeaponCatalog::ResolveCrosshair 의 자동 값과 같다).
에디터 Cmd 칸: py "C:/UnrealProjects/MCPTest/Tools/UI/setup_crosshair.py"
커맨드렛: UnrealEditor-Cmd.exe MCPTest.uproject -run=pythonscript -script="C:/UnrealProjects/MCPTest/Tools/UI/setup_crosshair.py"
플레이 중에는 돌리지 않는다. 공통 크기·굵기·색은 DA_VHUIStyle 의 HUD|Crosshair (C++ 기본값 그대로).
"""
import unreal

DA = "/Game/VaultHeist/Weapons/Data/DA_VHWeaponCatalog"
S = unreal.VHCrosshairShape

# 무기 → (모양, 사거리 cm, 반각, 폭발 반지름 cm, 반동 px, 복귀 초)  [서버 GameData.h WeaponTypes·GrenadeRadiusCm·MineRadiusCm]
TABLE = {
    "BATON": (S.BRACKETS, 200, 0, 0, 7, 0.16),
    "SWORD": (S.BRACKETS, 300, 0, 0, 8, 0.18),
    "BEAM_SWORD": (S.BRACKETS, 400, 0, 0, 9, 0.18),
    "PISTOL": (S.CROSS, 0, 0, 0, 9, 0.14),
    "FLAMETHROWER": (S.CONE, 600, 15, 0, 3, 0.10),
    "RAILGUN": (S.RAIL_RING, 0, 0, 0, 26, 0.45),
    "TASER": (S.CROSS, 300, 0, 0, 13, 0.22),
    "GRENADE": (S.AREA, 600, 0, 600, 10, 0.25),
    "BLAST_MINE": (S.AREA, 0, 0, 300, 8, 0.25),
    "SHOCK_MINE": (S.AREA, 0, 0, 300, 8, 0.25),
}


def spec(shape, rng, half, area, kick, rec, scale=1.0):
    c = unreal.VHCrosshairSpec()
    c.set_editor_property("shape", shape)
    c.set_editor_property("range_cm", float(rng))
    c.set_editor_property("half_angle_deg", float(half))
    c.set_editor_property("area_radius_cm", float(area))
    c.set_editor_property("kick_px", float(kick))
    c.set_editor_property("kick_recover_seconds", float(rec))
    c.set_editor_property("size_scale", float(scale))
    return c


def main():
    cat = unreal.EditorAssetLibrary.load_asset(DA)
    if not cat:
        unreal.log_error("[Crosshair] DA 없음 " + DA)
        return
    weapons = cat.get_editor_property("weapons")
    done = []
    for w in weapons:
        wid = w.get_editor_property("id")
        name = str(wid).split(".")[-1].split(":")[0].strip()
        key = None
        for k in TABLE:
            if k in name.upper().replace(" ", "_"):
                key = k
        # BEAM_SWORD 가 SWORD 보다 먼저 맞도록 긴 이름 우선
        cands = [k for k in TABLE if k in name.upper()]
        if cands:
            key = max(cands, key=len)
        if not key:
            unreal.log_warning("[Crosshair] 표에 없는 무기 %s" % name)
            continue
        scale = 1.15 if key == "RAILGUN" else 1.0
        w.set_editor_property("crosshair", spec(*TABLE[key], scale=scale))
        done.append(key)
    cat.set_editor_property("weapons", weapons)
    cat.set_editor_property("unarmed_crosshair", spec(S.BRACKETS, 0, 0, 0, 6, 0.16))
    cat.set_editor_property("skill_taser_crosshair", spec(S.LIGHTNING, 300, 0, 0, 12, 0.22))
    unreal.EditorAssetLibrary.save_loaded_asset(cat, False)
    unreal.log("[Crosshair] 무기 %d개 조준점 값 채움: %s" % (len(done), ", ".join(done)))


main()
