# -*- coding: utf-8 -*-
"""
권총·테이저건이 탑뷰에서 가는 선으로 보이는 문제 (사용자 요청 2026-11-18)
총을 세워 들면(원거리 자세 Roll 0) 위에서 총의 윗면(슬라이드)만 보여 얇다 → 무기별 자세(Weapon Poses)에 권총·테이저만 Roll 을 줘
옆으로 기울여 들게 해 탑뷰에서 옆모습(손잡이·총몸)이 보이게 한다. 다른 총(화염방사기·레일건)은 이미 무기별 자세가 있어 그대로.
  에디터 Cmd 칸: py "C:/UnrealProjects/MCPTest/Tools/CharacterBuild/pistol_side.py" [Roll도=60]
"""
import sys
import unreal

DA = "/Game/VaultHeist/Weapons/Data/DA_VHCharacterVisuals"
ROLL = float(sys.argv[1]) if len(sys.argv) > 1 else 60.0
WEAPONS = (unreal.VHWeaponId.PISTOL, unreal.VHWeaponId.TASER)

da = unreal.EditorAssetLibrary.load_asset(DA)
d = da.get_editor_property("default")
base = d.get_editor_property("ranged_pose")
poses = d.get_editor_property("weapon_poses")
for w in WEAPONS:
    p = unreal.VHHoldPose()
    p.import_text(base.export_text())
    t = p.get_editor_property("weapon")
    r = t.rotation.rotator()
    t.rotation = unreal.Rotator(roll=ROLL, pitch=r.pitch, yaw=r.yaw).quaternion()
    p.set_editor_property("weapon", t)
    poses[w] = p
d.set_editor_property("weapon_poses", poses)
da.set_editor_property("default", d)
unreal.EditorAssetLibrary.save_loaded_asset(da, False)
unreal.log("[CharBuild] 권총·테이저 자세 Roll %.0f° (원거리 자세 기준) 저장" % ROLL)
