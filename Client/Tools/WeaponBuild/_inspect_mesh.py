# 개발용: 스태틱 메시의 재질 칸·섹션 연결을 로그에 적는다.  py "..._inspect_mesh.py" /Game/.../SM_X
import sys
import unreal

path = sys.argv[1]
sm = unreal.EditorAssetLibrary.load_asset(path)
sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
mats = sm.get_editor_property("static_materials")
names = ["%d:%s=%s" % (i, m.get_editor_property("material_slot_name"), m.get_editor_property("material_interface").get_name() if m.get_editor_property("material_interface") else "None") for i, m in enumerate(mats)]
secs = ["sec%d->slot%d" % (s, sub.get_lod_material_slot(sm, 0, s)) for s in range(sm.get_num_sections(0))]
unreal.log("[WeaponBuild] %s | slots: %s | sections: %s | tris: %d" % (sm.get_name(), ", ".join(names), ", ".join(secs), sm.get_num_triangles(0)))
