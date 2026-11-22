# -*- coding: utf-8 -*-
"""
무기 이펙트 나이아가라 4종을 엔진 기본 템플릿으로 만들고 색·크기 변수를 연결한다 (에디터 밖에서 Unreal MCP 로).
  엔진파이썬 -I Tools/WeaponBuild/setup_niagara.py hitspark explosion flame smoke

- 템플릿: /Niagara/DefaultAssets/Templates/Systems (엔진에 들어 있음, 유료·외부 에셋 없음)
- 사용자 변수: User.Color (LinearColor), User.Radius (float, cm) → 코드(UVHWeaponFxSubsystem)가 이펙트마다 넣는다
- 판정보다 크게 보이지 않게: 퍼지는 속도·시간을 User.Radius 에 맞춰 판정 반지름 안쪽에 머물게 한다
에디터가 켜져 있어야 하고 플레이(PIE) 중이 아니어야 한다.
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vhcmd import Mcp  # noqa: E402

NS = "NiagaraToolsets.NiagaraToolset_System"
FX = "/Game/VaultHeist/FX"
TEMPLATES = "/Niagara/DefaultAssets/Templates/Systems"
LC = "/Script/CoreUObject.LinearColor"
FLOAT = "/Script/Niagara.NiagaraFloat"
INT = "/Script/Niagara.NiagaraInt32"
VEC = "/Script/CoreUObject.Vector3f"

m = Mcp()


def call(tool, args, show=False):
    out = m.call(NS, tool, args)
    if show or "rror" in out[:200] or "ailed" in out[:200]:
        print("  %s → %s" % (tool, out[:300]))
    return out


def sysref(name):
    return {"refPath": "%s/%s.%s" % (FX, name, name)}


def create(name, template):
    # 이미 있으면 새로 만들지 않는다(데이터 에셋이 참조 중일 때 지우면 확인 창이 떠서 에디터가 멈춘다). 다시 만들려면 에디터에서 지운 뒤 실행
    exists = m.call("editor_toolset.toolsets.asset.AssetTools", "exists", {"path": "%s/%s" % (FX, name)})
    if "true" not in exists.lower():
        call("CreateNiagaraSystem", {"assetName": name, "assetPath": FX, "templateSystem": {"refPath": "%s/%s.%s" % (TEMPLATES, template, template)}}, True)
    call("AddUserVariables", {"system": sysref(name), "variablesToAdd": [
        {"name": "Color", "description": "이펙트 색 (코드가 분류 색을 넣는다)", "type": {"classStructOrEnum": {"refPath": LC}},
         "defaultValue": {"struct": {"refPath": LC}, "value": {"r": 1, "g": 0.5, "b": 0.1, "a": 1}}},
        {"name": "Radius", "description": "판정 반지름(cm). 이펙트가 이 안쪽에 머문다", "type": {"classStructOrEnum": {"refPath": "/Script/Niagara.NiagaraFloat"}},
         "defaultValue": {"struct": {"refPath": FLOAT}, "value": {"value": 100}}},
    ]}, True)


def ref(name, emitter, script, module, inp):
    return {"system": sysref(name), "emitterName": emitter, "scriptName": script, "moduleName": module, "inputNameStack": [inp]}


def link(name, emitter, script, module, inp, var, typ):
    call("SetStackInputData", {"stackInputRef": ref(name, emitter, script, module, inp), "inputData": {
        "struct": {"refPath": "/Script/NiagaraEditor.NiagaraExt_StackInputData_Linked"},
        "value": {"linkedVariable": {"name": var, "type": {"classStructOrEnum": {"refPath": typ}}}}}})


def hlsl(name, emitter, script, module, inp, expr):
    call("SetStackInputData", {"stackInputRef": ref(name, emitter, script, module, inp), "inputData": {
        "struct": {"refPath": "/Script/NiagaraEditor.NiagaraExt_StackInputData_HlslExpression"}, "value": {"hlslExpression": expr}}})


def setv(name, emitter, script, module, inp, struct, value):
    call("SetStackInputData", {"stackInputRef": ref(name, emitter, script, module, inp), "inputData": {"struct": {"refPath": struct}, "value": value}})


def f(name, emitter, script, module, inp, v):
    setv(name, emitter, script, module, inp, FLOAT, {"value": v})


def remove_emitter(name, emitter):
    call("RemoveEmitter", {"emitterToRemove": {"system": sysref(name), "emitterName": emitter}}, True)


def save(name):
    m.call("editor_toolset.toolsets.asset.AssetTools", "save_assets", {"asset_paths": ["%s/%s" % (FX, name)]})
    print("  저장:", name, call("GetStackIssues", {"system": sysref(name)})[:400])


def burst(name, em, count, life_min, life_max, size_min, size_max, speed_expr, gravity_z, radius_expr=None, drag=None):
    """한 번 터지는 불꽃 이미터 공통 설정"""
    setv(name, em, "EmitterUpdateScript", "SpawnBurst_Instantaneous", "Spawn Count", INT, {"value": count})
    f(name, em, "ParticleSpawnScript", "InitializeParticle", "Lifetime Min", life_min)
    f(name, em, "ParticleSpawnScript", "InitializeParticle", "Lifetime Max", life_max)
    f(name, em, "ParticleSpawnScript", "InitializeParticle", "Uniform Sprite Size Min", size_min)
    f(name, em, "ParticleSpawnScript", "InitializeParticle", "Uniform Sprite Size Max", size_max)
    link(name, em, "ParticleSpawnScript", "InitializeParticle", "Color", "User.Color", LC)
    hlsl(name, em, "ParticleSpawnScript", "AddVelocity", "Velocity Speed", speed_expr)
    setv(name, em, "ParticleUpdateScript", "GravityForce", "Gravity", VEC, {"x": 0, "y": 0, "z": gravity_z})
    if radius_expr:
        hlsl(name, em, "ParticleSpawnScript", "ShapeLocation", "Sphere Radius", radius_expr)
    if drag is not None:
        f(name, em, "ParticleUpdateScript", "Drag", "Drag", drag)


def do_hitspark():
    # 피격 불꽃: RadialBurst 에서 리본 2개를 빼고 작은 불꽃 터짐 하나. 0.12~0.3초, 반지름(User.Radius, 25~35cm) 안쪽
    n = "NS_VH_HitSpark"
    create(n, "RadialBurst")
    remove_emitter(n, "RibbonTrailFollower")
    remove_emitter(n, "Ribbon_Trail_Leader")
    burst(n, "OmnidirectionalBurst", 16, 0.15, 0.35, 14.0, 24.0, "User.Radius * 7.0", -300.0, "User.Radius * 0.15", 4.0)
    save(n)


# 2026-11-20 불꽃 다시: 이미터 5겹. 색 변화는 재질이 입자 나이로 계산한다 (setup_flame.py)
# (이름, 재질, 생성/초, 수명 최소·최대, 속도 = User.Radius × 배율, 공기 저항, 원뿔 각도(전체 각: 28 = 좌우 14°), 위로 뜨는 힘, 크기 처음→끝, 그리는 순서, 입자 색 밝기)
# 이동 거리 = 속도/저항 × (1 − e^(−저항×수명)) ≈ 판정 길이(User.Radius) 안쪽 → 판정 원뿔(600cm, 반각 15°)을 넘지 않는다
FLAME_LAYERS = [
    ("Smoke", "M_VHFlameSmoke", 26.0, 0.8, 1.1, 3.1, 3.0, 26.0, 70.0, (40.0, 170.0), 0, 1.0),    # 끝에서 옅은 연기 (재질이 나이 0.3 부터 보이게)
    ("Flame", "M_VHFlameBody", 95.0, 0.30, 0.42, 3.0, 1.5, 26.0, 60.0, (24.0, 135.0), 1, 1.0),   # 몸통: 주황 → 빨강 → 그을음 (반투명, 밝은 바닥 위 윤곽)
    ("Haze", "M_VHHeatHaze", 20.0, 0.40, 0.55, 2.6, 1.5, 22.0, 40.0, (50.0, 150.0), 2, 1.0),    # 열기 일렁임 (굴절)
    ("Core", "M_VHFlameCore", 150.0, 0.18, 0.27, 3.0, 1.5, 16.0, 30.0, (16.0, 70.0), 3, 1.0),    # 심지: 흰·노랑 → 주황 → 빨강 (가산)
    ("Embers", "M_VHFlameEmber", 60.0, 0.35, 0.55, 2.5, 0.8, 28.0, 90.0, (11.0, 5.0), 4, 1.3),  # 작고 빠르게 흩어지는 불씨 (가산)
]


def do_flame():
    # 화염방사기 불꽃: 엔진 Fountain 이미터(계속 뿜기) 5겹을 앞(+X, User.Dir)으로 뿜는다.
    # 속도·수명을 User.Radius(총구에서 판정 끝까지 길이)에 맞춰 판정 부채꼴(600cm, 반각 15°) 안쪽에 머물게 한다
    n = "NS_VH_Flame"
    create(n, "DirectionalBurst")
    remove_emitter(n, "LocationBasedRibbon")
    remove_emitter(n, "DirectionalBurst")
    # 뿜는 방향: Fountain 은 회전을 따르지 않아 코드가 User.Dir 을 넣는다 (이미터 Local 공간 앞 = (1,0,0))
    call("AddUserVariables", {"system": sysref(n), "variablesToAdd": [
        {"name": "Dir", "description": "뿜는 방향(이미터 공간, 코드가 (1,0,0)을 넣는다)", "type": {"classStructOrEnum": {"refPath": VEC}},
         "defaultValue": {"struct": {"refPath": VEC}, "value": {"x": 1, "y": 0, "z": 0}}}]}, True)
    import re as _re
    for (em, mat, rate, lmin, lmax, speed, drag, cone, up, size, order, bright) in FLAME_LAYERS:
        # 다시 돌려도 이미터가 늘지 않게 (2026-11-19: Flame001·Flame002 가 붙어 하얀 알갱이가 위로 뿜어지던 원인)
        if ('"emitterName": "%s"' % em) not in call("GetSystemSummary", {"system": sysref(n)}):
            call("AddEmitter", {"system": sysref(n), "templateEmitter": {"refPath": "/Niagara/DefaultAssets/Templates/Emitters/Fountain.Fountain"}, "emitterName": em}, True)
        f(n, em, "EmitterUpdateScript", "SpawnRate", "SpawnRate", rate)
        f(n, em, "ParticleSpawnScript", "InitializeParticle", "Lifetime Min", lmin)
        f(n, em, "ParticleSpawnScript", "InitializeParticle", "Lifetime Max", lmax)
        setv(n, em, "ParticleSpawnScript", "InitializeParticle", "Color", LC, {"r": bright, "g": bright, "b": bright, "a": 1.0})
        f(n, em, "ParticleSpawnScript", "ShapeLocation", "Sphere Radius", 3.0)
        hlsl(n, em, "ParticleSpawnScript", "AddVelocity", "Velocity Speed", "User.Radius * %.2f" % speed)
        link(n, em, "ParticleSpawnScript", "AddVelocity", "Cone Axis", "User.Dir", VEC)
        f(n, em, "ParticleSpawnScript", "AddVelocity", "Cone Angle", cone)
        setv(n, em, "ParticleUpdateScript", "GravityForce", "Gravity", VEC, {"x": 0, "y": 0, "z": up})
        f(n, em, "ParticleUpdateScript", "Drag", "Drag", drag)
        # 색·투명도는 재질이 정한다 → 템플릿의 ScaleColor(알파 커브) 끔
        call("SetModuleEnabled", {"moduleRef": {"system": sysref(n), "emitterName": em, "scriptName": "ParticleUpdateScript", "moduleName": "ScaleColor",
                                                "rendererIndex": -1, "inputNameStack": []}, "bEnabled": False}, True)
        # 크기: 나이에 따라 처음 → 끝 (Set Parameters 로 Particles.SpriteSize 를 직접 쓴다)
        sref = {"system": sysref(n), "emitterName": em, "scriptName": "ParticleUpdateScript", "moduleName": "", "rendererIndex": -1, "inputNameStack": []}
        topo = call("GetScriptStackTopology", {"scriptRef": sref})
        mods = _re.findall(r'"moduleName": *"(SetVariables_[0-9A-F]+)"', topo)
        if not mods:
            out = call("AddSetParametersModule", {"moduleLocationRef": sref, "parameters": [
                {"variable": {"name": "Particles.SpriteSize", "type": {"classStructOrEnum": {"refPath": "/Script/CoreUObject.Vector2f"}}},
                 "defaultValue": {"struct": {"refPath": "/Script/CoreUObject.Vector2f"}, "value": {"x": 20, "y": 20}}}]}, True)
            mods = _re.findall(r'"moduleName": *"(SetVariables_[0-9A-F]+)"', out)
        if mods:
            hlsl(n, em, "ParticleUpdateScript", mods[0], "Particles.SpriteSize",
                 "float2(1.0, 1.0) * lerp(%.1f, %.1f, Particles.NormalizedAge)" % size)
        call("SetRendererData", {"renderer": {"system": sysref(n), "emitterName": em, "scriptName": "", "moduleName": "", "rendererIndex": 0, "inputNameStack": []},
                                 "rendererData": {"propertyValues": json.dumps({"Material": {"refPath": "%s/%s.%s" % (FX, mat, mat)}, "SortOrderHint": order})}}, True)
    save(n)


def do_explosion():
    # 폭발 꾸밈: SimpleExplosion 템플릿. 날아가는 조각(메시)은 판정 밖으로 나가므로 뺀다.
    # 불꽃 알갱이는 User.Radius(판정 반지름)의 약 0.6배까지만 퍼지게 (속도 1.8R, 강한 공기 저항)
    n = "NS_VH_Explosion"
    create(n, "SimpleExplosion")
    remove_emitter(n, "UpwardMeshBurst")
    burst(n, "OmnidirectionalBurst", 18, 0.35, 0.7, 50.0, 90.0, "User.Radius * 1.8", -150.0, "User.Radius * 0.08", 3.0)
    burst(n, "SimpleSpriteBurst", 5, 0.25, 0.45, 120.0, 220.0, "User.Radius * 0.4", 0.0, None, 3.0)
    save(n)


def do_smoke():
    # 연막: Fountain 이미터를 위로 천천히, 반지름(User.Radius = 500cm)의 0.75배 원 안에서 큰 회색 덩어리가 계속 생긴다
    n = "NS_VH_Smoke"
    create(n, "DirectionalBurst")
    remove_emitter(n, "LocationBasedRibbon")
    remove_emitter(n, "DirectionalBurst")
    # 다시 돌려도 이미터가 늘지 않게 (2026-11-19: Smoke001·Smoke002 가 붙어 하얀 알갱이가 위로 뿜어지던 원인)
    if '"emitterName": "Smoke"' not in call("GetSystemSummary", {"system": sysref(n)}):
        call("AddEmitter", {"system": sysref(n), "templateEmitter": {"refPath": "/Niagara/DefaultAssets/Templates/Emitters/Fountain.Fountain"}, "emitterName": "Smoke"})
    em = "Smoke"
    f(n, em, "EmitterUpdateScript", "SpawnRate", "SpawnRate", 6.0)
    f(n, em, "ParticleSpawnScript", "InitializeParticle", "Lifetime Min", 2.5)
    f(n, em, "ParticleSpawnScript", "InitializeParticle", "Lifetime Max", 3.5)
    f(n, em, "ParticleSpawnScript", "InitializeParticle", "Uniform Sprite Size Min", 220.0)
    f(n, em, "ParticleSpawnScript", "InitializeParticle", "Uniform Sprite Size Max", 340.0)
    link(n, em, "ParticleSpawnScript", "InitializeParticle", "Color", "User.Color", LC)
    hlsl(n, em, "ParticleSpawnScript", "ShapeLocation", "Sphere Radius", "User.Radius * 0.75")
    hlsl(n, em, "ParticleSpawnScript", "AddVelocity", "Velocity Speed", "30.0")
    setv(n, em, "ParticleUpdateScript", "GravityForce", "Gravity", VEC, {"x": 0, "y": 0, "z": 0})
    f(n, em, "ParticleUpdateScript", "Drag", "Drag", 0.5)
    save(n)


def do_trail():
    # 근접 궤적 리본 (2026-11-19): DirectionalBurst 시스템의 LocationBasedRibbon 이미터만 남긴다 → 붙인 자리(날·봉 끝)가 움직인 길을 리본으로.
    # User.Width(폭 cm)·User.Life(꼬리 길이 초)·User.Color 를 코드가 무기마다 넣는다 (DA_VHWeaponCatalog → Weapon Fx)
    n = "NS_VH_Trail"
    create(n, "DirectionalBurst")
    remove_emitter(n, "DirectionalBurst")
    call("AddUserVariables", {"system": sysref(n), "variablesToAdd": [
        {"name": "Width", "description": "리본 폭(cm)", "type": {"classStructOrEnum": {"refPath": FLOAT}},
         "defaultValue": {"struct": {"refPath": FLOAT}, "value": {"value": 10}}},
        {"name": "Life", "description": "꼬리 길이(초)", "type": {"classStructOrEnum": {"refPath": FLOAT}},
         "defaultValue": {"struct": {"refPath": FLOAT}, "value": {"value": 0.12}}}]}, True)
    em = "LocationBasedRibbon"
    # 템플릿에서는 다른 이미터가 리본을 만들어 줘서 생성 모듈이 없다 → SpawnRate 를 붙인다 (월드 공간 = bLocalSpace false 그대로)
    topo = call("GetScriptStackTopology", {"scriptRef": {"system": sysref(n), "emitterName": em, "scriptName": "EmitterUpdateScript",
                                                         "moduleName": "", "rendererIndex": -1, "inputNameStack": []}})
    if '"SpawnRate"' not in topo:
        call("AddModule", {"moduleLocationRef": {"system": sysref(n), "emitterName": em, "scriptName": "EmitterUpdateScript", "moduleName": "",
                                                 "rendererIndex": -1, "inputNameStack": []},
                           "moduleAsset": {"refPath": "/Niagara/Modules/Emitter/SpawnRate.SpawnRate"}}, True)
    f(n, em, "EmitterUpdateScript", "SpawnRate", "SpawnRate", 240.0)
    link(n, em, "ParticleSpawnScript", "InitializeParticle", "Lifetime", "User.Life", FLOAT)
    link(n, em, "ParticleSpawnScript", "InitializeParticle", "Color", "User.Color", LC)
    # 리본 폭은 모드가 "Unset"이면 무시된다 → Direct Set 으로 바꾼 뒤 연결
    setv(n, em, "ParticleSpawnScript", "InitializeParticle", "Ribbon Width Mode", "/Script/NiagaraEditor.NiagaraExt_StackInputData_Enum",
         {"enum": {"refPath": "/Niagara/Enums/Ribbons/ENiagara_UnsetDirectSet.ENiagara_UnsetDirectSet"}, "enumName": "ENiagara_UnsetDirectSet::NewEnumerator1"})
    link(n, em, "ParticleSpawnScript", "InitializeParticle", "Ribbon Width", "User.Width", FLOAT)
    # 그래도 폭이 반영되지 않았다(2026-11-19 화면 확인) → 생성 단계 끝에 Particles.RibbonWidth = User.Width 를 직접 쓰는 Set Parameters 모듈
    sref = {"system": sysref(n), "emitterName": em, "scriptName": "ParticleSpawnScript", "moduleName": "", "rendererIndex": -1, "inputNameStack": []}
    topo = call("GetScriptStackTopology", {"scriptRef": sref})
    import re as _re
    mods = _re.findall(r'"moduleName": *"(SetVariables_[0-9A-F]+)"', topo)
    if not mods:
        out = call("AddSetParametersModule", {"moduleLocationRef": sref, "parameters": [
            {"variable": {"name": "Particles.RibbonWidth", "type": {"classStructOrEnum": {"refPath": FLOAT}}},
             "defaultValue": {"struct": {"refPath": FLOAT}, "value": {"value": 10}}}]}, True)
        mods = _re.findall(r'"moduleName": *"(SetVariables_[0-9A-F]+)"', out)
    if mods:
        link(n, em, "ParticleSpawnScript", mods[0], "Particles.RibbonWidth", "User.Width", FLOAT)
    save(n)


STEPS = {"hitspark": do_hitspark, "flame": do_flame, "explosion": do_explosion, "smoke": do_smoke, "trail": do_trail}
for step in sys.argv[1:]:
    print("[%s]" % step)
    STEPS[step]()
