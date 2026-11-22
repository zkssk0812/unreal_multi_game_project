import unreal
EAL = unreal.EditorAssetLibrary
for d in ("/Game/VaultHeist/Characters/Thief/Mixamo", "/Game/VaultHeist/Characters/Guard/Mixamo"):
    for p in EAL.list_assets(d, False, False):
        a = EAL.load_asset(p)
        if isinstance(a, unreal.AnimSequence):
            unreal.log("[LEN] %s %.2f" % (p.split("/")[-1].split(".")[0] + ("(G)" if "Guard" in d else ""), a.get_play_length()))
