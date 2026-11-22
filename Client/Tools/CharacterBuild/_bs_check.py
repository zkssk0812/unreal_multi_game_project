import unreal
bs = unreal.EditorAssetLibrary.load_asset("/Game/VaultHeist/Characters/Thief/Mixamo/BS_VHThief_Locomotion")
for p in ("blend_space_data", "sample_data", "blend_parameters", "grid_samples"):
    try:
        v = bs.get_editor_property(p)
        txt = v.export_text() if hasattr(v, "export_text") else str(v)
        unreal.log("[BSC] %s: %s" % (p, (txt if isinstance(txt, str) else str(txt))[:300]))
    except Exception as e:
        unreal.log("[BSC] %s ERR %s" % (p, e))
da = unreal.EditorAssetLibrary.load_asset("/Game/VaultHeist/Weapons/Data/DA_VHCharacterVisuals")
d = da.get_editor_property("default")
unreal.log("[BSC] DA bs=%s anim_class=%s" % (d.get_editor_property("locomotion_blend_space"), d.get_editor_property("anim_class")))
