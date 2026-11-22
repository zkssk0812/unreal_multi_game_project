# 개발용: 이 엔진 버전의 Geometry Script 파이썬 함수 이름·설명을 Saved/WeaponBuild/api_probe.txt 에 적는다.
#   py "C:/UnrealProjects/MCPTest/Tools/WeaponBuild/_probe_api.py" [찾을 단어 ...]
import os
import sys
import unreal

out_dir = os.path.join(unreal.Paths.project_saved_dir(), "WeaponBuild")
os.makedirs(out_dir, exist_ok=True)
words = [w.lower() for w in sys.argv[1:]]
lines = []
for name in sorted(dir(unreal)):
    if not (name.startswith("GeometryScript") or name in ("DynamicMesh", "DynamicMeshPool", "MaterialEditingLibrary",
                                                          "DataAssetFactory", "SkeletalMeshSocket", "StaticMeshEditorSubsystem")):
        continue
    obj = getattr(unreal, name)
    members = [m for m in dir(obj) if not m.startswith("_")]
    if words:
        members = [m for m in members if any(w in m.lower() or w in name.lower() for w in words)]
        if not members:
            continue
    lines.append("== " + name)
    for m in members:
        doc = ""
        if words:
            try:
                doc = (getattr(obj, m).__doc__ or "").strip().replace("\n", "\n      ")
            except Exception:
                doc = ""
        lines.append("   " + m + ((" :: " + doc) if doc else ""))
path = os.path.join(out_dir, "api_probe.txt")
with open(path, "w", encoding="utf-8") as f:
    f.write("\n".join(lines))
unreal.log("[WeaponBuild] api probe -> %s (%d lines)" % (path, len(lines)))
