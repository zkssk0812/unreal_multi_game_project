# -*- coding: utf-8 -*-
"""
원거리 무기 반동·레일건 충전 값을 무기 표 DA_VHWeaponCatalog 에 넣는다 (2026-11-20).
  에디터 Cmd 칸: py "C:/UnrealProjects/MCPTest/Tools/WeaponBuild/setup_recoil.py" [show]
  show = 넣지 않고 지금 값만 로그
플레이 중에는 돌리지 않는다. 숫자는 나중에 DA 에서 직접 고쳐도 된다 (Weapons → 무기 → Recoil).

기준: Lyra 사격 동작에서 뽑은 곡선 (Tools/External/lyra_recoil.json, lyra_export.py recoil)
  - MM_Pistol_Fire: 총구 들림 18.8° (0.067초), 손이 뒤로 2.7cm (0.133초 = 총구의 2배 늦게), 0.30초에 거의 제자리, 허리 0.6°
  - MM_Rifle_Fire : 손이 뒤로 2.2cm (0.10초), 총구 1.75° (0.133초), 허리 비틀림 3.5° (0.167초), 0.5초에 제자리
  탑뷰에서는 작은 움직임이 안 보여서 손 이동은 2.5~7배, 상체는 3배 정도로 키웠다(예전 키 프레임의 3배 기준과 같음).
  화면 흔들림: 카메라 1500cm·FOV 70 → 1080p 에서 1px ≈ 1.1cm. 권총 1cm(≈1px), 레일건 4cm(≈3~4px)로 아주 약하게.
"""
import sys
import unreal

EAL = unreal.EditorAssetLibrary
DA = "/Game/VaultHeist/Weapons/Data/DA_VHWeaponCatalog"

# 무기 → 반동 칸 값
RECOIL = {
    # 권총: 짧고 날카롭게 튀었다가 빠르게 복귀. Lyra 권총 그대로의 모양, 빠르게(총 0.25초 = 서버 연사 간격)
    "PISTOL": dict(kick_back_cm=7.0, muzzle_up_deg=17.0, kick_up_cm=1.5, side_deg=2.0, spine_back_deg=2.0, spine_twist_deg=1.0,
                   rise_seconds=0.045, kick_lag=2.0, recover_seconds=0.16, max_stack=1.5,
                   camera_shake_cm=1.0, camera_shake_seconds=0.08),
    # 테이저건: 권총 계열, 가볍게
    "TASER": dict(kick_back_cm=3.0, muzzle_up_deg=8.0, kick_up_cm=0.8, side_deg=1.5, spine_back_deg=0.8, spine_twist_deg=0.3,
                  rise_seconds=0.04, kick_lag=2.0, recover_seconds=0.14, max_stack=1.2,
                  camera_shake_cm=0.6, camera_shake_seconds=0.06),
    # 레일건: 한 발이 크고 무겁게. 상체가 뒤로 밀리고(라이플처럼 어깨가 비틀림) 천천히 복귀 → 그 뒤 1초 버팀 자세(서버 잠금)
    "RAILGUN": dict(kick_back_cm=16.0, muzzle_up_deg=12.0, kick_up_cm=3.0, side_deg=1.0, spine_back_deg=7.0, spine_twist_deg=4.5,
                    rise_seconds=0.07, kick_lag=1.7, recover_seconds=0.55, max_stack=1.0,
                    camera_shake_cm=4.0, camera_shake_seconds=0.3),
    # 화염방사기: 튀는 반동 없음, 쏘는 동안 잔잔한 떨림
    "FLAMETHROWER": dict(kick_back_cm=0.0, muzzle_up_deg=0.0, sustain_shake_cm=1.8, sustain_shake_deg=2.0, sustain_shake_hz=12.0,
                         camera_shake_cm=0.0),
}
RAILGUN_CHARGE_SECONDS = 1.0


def log(m):
    unreal.log("[Recoil] " + m)


def main(argv):
    try:
        if unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor():
            unreal.log_error("[Recoil] 플레이 중에는 돌리지 않습니다")
            return
    except Exception:
        pass
    da = EAL.load_asset(DA)
    ws = da.get_editor_property("weapons")
    show = "show" in argv
    for i in range(len(ws)):
        w = ws[i]
        wid = w.get_editor_property("id")
        name = str(wid).split(".")[-1].split(":")[0].strip("<> ").upper()
        key = next((k for k in RECOIL if k in name), None)
        r = w.get_editor_property("recoil")
        if key and not show:
            for prop, val in RECOIL[key].items():
                r.set_editor_property(prop, val)
            w.set_editor_property("recoil", r)
            ws[i] = w    # 배열의 구조체는 복사본 → 다시 넣어야 저장된다
        r = ws[i].get_editor_property("recoil")
        log("%s: 뒤로 %.1fcm 총구 %.1f° 상체 %.1f° 비틀림 %.1f° / 튐 %.3f초 ×%.1f 복귀 %.2f초 / 떨림 %.1fcm %.1f° / 화면 %.1fcm %.2f초" % (
            name, r.kick_back_cm, r.muzzle_up_deg, r.spine_back_deg, r.spine_twist_deg, r.rise_seconds, r.kick_lag, r.recover_seconds,
            r.sustain_shake_cm, r.sustain_shake_deg, r.camera_shake_cm, r.camera_shake_seconds))
    if not show:
        da.set_editor_property("weapons", ws)
        da.set_editor_property("railgun_charge_seconds", RAILGUN_CHARGE_SECONDS)
        EAL.save_loaded_asset(da, False)
    log("레일건 충전 %.2f초" % da.get_editor_property("railgun_charge_seconds"))
    log("끝")


main(sys.argv[1:])
