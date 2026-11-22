"""
대저택 1층 — 원클릭 빌드 (Unreal Engine 5)
  1) Building/ 의 건물 뼈대·천장, Props/ 의 가구·식물 OBJ 를 임포트
  2) 건물을 원점에, 가구·식물을 각각 개별 액터로 배치 (아웃라이너: Mansion/Props/<방 이름>)
  3) 충돌 설정, 흐르는 물·발광 재질 생성/적용
  4) 조명(채움광·펜던트·수중등) + 스카이라이트 + 태양 + 노출 보정 + PlayerStart
사용법: Edit > Plugins 에서 'Python Editor Script Plugin' 활성화 → 빈 레벨을 연 상태에서
        Tools > Execute Python Script... 로 이 파일 실행 (압축 푼 폴더 구조 그대로 두세요)
다시 실행하면 Mansion/ 폴더의 기존 액터를 지우고 새로 배치합니다.
SKIP_IMPORT = True 로 바꾸면 임포트를 건너뛰고 배치만 다시 합니다 (가구를 지운 뒤 되살릴 때 등).
"""
import os, math, unreal

SKIP_IMPORT = False
HERE = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else r"C:\Mansion_1F_Modular"
ROOT = "/Game/Mansion"
tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

PROPS = ['BarStool', 'Bed_King', 'Bed_Wall_OakSlat', 'Bench_Bed', 'Chair_Fabric_Oat', 'Chair_Leather_Tan', 'CoffeeTable_0p45', 'CoffeeTable_0p55', 'Desk_1p6_0p65', 'Desk_2_0p8', 'Desk_2p4_0p9', 'DiningTable_6', 'FloorLamp', 'Jail_Bench', 'Kitchen_Island', 'Kitchen_TallUnit', 'Locker', 'LoungeChair_Fabric_Charcoal', 'LoungeChair_Fabric_Oat', 'LoungeChair_Fabric_Stone', 'LoungeChair_Leather_Tan', 'Nightstand_Floating', 'Pendant_Globes_12_0p9_11_8', 'Pendant_Globes_12_0p9_12_8', 'Pendant_Globes_12_0p9_13_8', 'Pendant_Globes_12_0p9_14_8', 'Pendant_Globes_30_2p6_7_8', 'Pendant_Globes_8_0p6_21_4', 'Pendant_Globes_9_0p7_3_4', 'Pendant_Linear_1p6_2p3', 'Pendant_Linear_2_2p4', 'Pendant_Linear_2p4_2p4', 'Pendant_Linear_5p4_2p75', 'Plant_FiddleFig_1_11', 'Plant_FiddleFig_1p05_13', 'Plant_FiddleFig_1p05_14', 'Plant_FiddleFig_1p1_31', 'Plant_FiddleFig_1p1_32', 'Plant_FiddleFig_1p45_60', 'Plant_FiddleFig_1p45_61', 'Plant_Olive_1_52', 'Plant_Olive_1_53', 'Plant_Olive_1p15_50', 'Plant_Olive_1p15_51', 'Plant_Snake_1_70_1', 'Plant_Snake_1_71_1', 'Plant_Snake_1_72_1', 'Plant_Snake_1_73_1', 'Plant_Snake_1_74_1', 'Plant_Strelitzia_0p95_21', 'Plant_Strelitzia_0p9_41', 'Plant_Strelitzia_1_12', 'Plant_Strelitzia_1_33', 'Rack', 'Rug_2p8_3p6_Rug_Charcoal', 'Rug_4_3p2_Rug_Oat', 'Rug_4p1_3p6_Rug_Oat', 'Rug_4p8_3p9_Rug_Charcoal', 'Rug_4p8_3p9_Rug_Oat', 'Rug_5_4_Rug_Charcoal', 'Rug_6_4p9_Rug_Charcoal', 'Rug_6p2_4p4_Rug_Charcoal', 'Security_Desk', 'Sheer_Curtains_10', 'Sheer_Curtains_6', 'Sheer_Curtains_8', 'Shelf_0', 'Shelf_1', 'Shelf_2', 'SideTable', 'Sofa_2p7_Fabric_Oat_Fabric_Rust', 'Sofa_2p7_Fabric_Stone_Fabric_Oat', 'Sofa_2p8_Fabric_Stone_Fabric_Oat', 'Sofa_3p4_Fabric_Oat_Fabric_Rust', 'StoneBench_1p8', 'StoneBench_2p8', 'Wardrobe', 'WaterFeature_Monolith']
PLACEMENTS = [
    ('Sheer_Curtains_10', 'GuestRoom1', -3100.0, -3370.0, 0.0, 'solid'),
    ('Sheer_Curtains_6', 'GuestRoom1', -3670.0, -3000.0, 90.0, 'solid'),
    ('Sheer_Curtains_10', 'GuestRoom2', -1900.0, -3370.0, 0.0, 'solid'),
    ('Sheer_Curtains_10', 'GuestRoom3', -700.0, -3370.0, 0.0, 'solid'),
    ('Sheer_Curtains_10', 'GuestRoom4', -4300.0, -2570.0, 0.0, 'solid'),
    ('Sheer_Curtains_8', 'GuestRoom4', -4870.0, -2100.0, 90.0, 'solid'),
    ('Sheer_Curtains_8', 'GuestRoom5', 4470.0, 100.0, -90.0, 'solid'),
    ('Sheer_Curtains_8', 'GuestRoom6', 4470.0, 1100.0, -90.0, 'solid'),
    ('Sheer_Curtains_8', 'GuestRoom7', 4470.0, 2100.0, -90.0, 'solid'),
    ('Bed_Wall_OakSlat', 'GuestRoom1', -2520.0, -2895.0, 90.0, 'solid'),
    ('Rug_4_3p2_Rug_Oat', 'GuestRoom1', -2820.0, -2895.0, 90.0, 'rug'),
    ('Bed_King', 'GuestRoom1', -2520.0, -2895.0, 90.0, 'solid'),
    ('Nightstand_Floating', 'GuestRoom1', -2520.0, -3062.5, 90.0, 'solid'),
    ('Nightstand_Floating', 'GuestRoom1', -2520.0, -2727.5, 90.0, 'solid'),
    ('Bench_Bed', 'GuestRoom1', -2805.0, -2895.0, 90.0, 'solid'),
    ('Wardrobe', 'GuestRoom1', -2750.0, -3305.0, 0.0, 'solid'),
    ('LoungeChair_Fabric_Oat', 'GuestRoom1', -2890.0, -2550.0, -90.0, 'solid'),
    ('Desk_1p6_0p65', 'GuestRoom1', -3350.0, -3295.0, 0.0, 'solid'),
    ('Chair_Leather_Tan', 'GuestRoom1', -3350.0, -3215.0, 180.0, 'solid'),
    ('Plant_Strelitzia_0p9_41', 'GuestRoom1', -3590.0, -2500.0, 90.0, 'solid'),
    ('Bed_Wall_OakSlat', 'GuestRoom2', -2480.0, -2895.0, -90.0, 'solid'),
    ('Rug_4_3p2_Rug_Oat', 'GuestRoom2', -2180.0, -2895.0, -90.0, 'rug'),
    ('Bed_King', 'GuestRoom2', -2480.0, -2895.0, -90.0, 'solid'),
    ('Nightstand_Floating', 'GuestRoom2', -2480.0, -2727.5, -90.0, 'solid'),
    ('Nightstand_Floating', 'GuestRoom2', -2480.0, -3062.5, -90.0, 'solid'),
    ('Bench_Bed', 'GuestRoom2', -2195.0, -2895.0, -90.0, 'solid'),
    ('Wardrobe', 'GuestRoom2', -2250.0, -2485.0, 180.0, 'solid'),
    ('SideTable', 'GuestRoom2', -2205.0, -3240.0, -90.0, 'solid'),
    ('LoungeChair_Fabric_Stone', 'GuestRoom2', -2300.0, -3240.0, -90.0, 'solid'),
    ('LoungeChair_Fabric_Oat', 'GuestRoom2', -2110.0, -3240.0, 90.0, 'solid'),
    ('FloorLamp', 'GuestRoom2', -2380.0, -3315.0, -90.0, 'solid'),
    ('Desk_1p6_0p65', 'GuestRoom2', -1640.0, -2495.0, 180.0, 'solid'),
    ('Chair_Leather_Tan', 'GuestRoom2', -1640.0, -2575.0, 0.0, 'solid'),
    ('Plant_Strelitzia_0p9_41', 'GuestRoom2', -1400.0, -3290.0, -90.0, 'solid'),
    ('Bed_Wall_OakSlat', 'GuestRoom3', -1280.0, -2895.0, -90.0, 'solid'),
    ('Rug_4_3p2_Rug_Oat', 'GuestRoom3', -980.0, -2895.0, -90.0, 'rug'),
    ('Bed_King', 'GuestRoom3', -1280.0, -2895.0, -90.0, 'solid'),
    ('Nightstand_Floating', 'GuestRoom3', -1280.0, -2727.5, -90.0, 'solid'),
    ('Nightstand_Floating', 'GuestRoom3', -1280.0, -3062.5, -90.0, 'solid'),
    ('Bench_Bed', 'GuestRoom3', -995.0, -2895.0, -90.0, 'solid'),
    ('Wardrobe', 'GuestRoom3', -1050.0, -2485.0, 180.0, 'solid'),
    ('SideTable', 'GuestRoom3', -1005.0, -3240.0, -90.0, 'solid'),
    ('LoungeChair_Fabric_Stone', 'GuestRoom3', -1100.0, -3240.0, -90.0, 'solid'),
    ('LoungeChair_Fabric_Oat', 'GuestRoom3', -910.0, -3240.0, 90.0, 'solid'),
    ('FloorLamp', 'GuestRoom3', -1180.0, -3315.0, -90.0, 'solid'),
    ('Desk_1p6_0p65', 'GuestRoom3', -440.0, -2495.0, 180.0, 'solid'),
    ('Chair_Leather_Tan', 'GuestRoom3', -440.0, -2575.0, 0.0, 'solid'),
    ('Plant_Strelitzia_0p9_41', 'GuestRoom3', -200.0, -3290.0, -90.0, 'solid'),
    ('StoneBench_2p8', 'Gallery1', 900.0, -3525.0, 0.0, 'solid'),
    ('StoneBench_2p8', 'Gallery1', 900.0, -3265.0, 180.0, 'solid'),
    ('Plant_Olive_1p15_50', 'Gallery1', 70.0, -4230.0, 0.0, 'solid'),
    ('Plant_Olive_1p15_51', 'Gallery1', 1730.0, -4230.0, 0.0, 'solid'),
    ('Plant_Olive_1p15_51', 'Gallery1', 1730.0, -2560.0, 0.0, 'solid'),
    ('Shelf_0', 'Study2', 1920.0, -2540.0, -90.0, 'solid'),
    ('Shelf_1', 'Study2', 1920.0, -2660.0, -90.0, 'solid'),
    ('Shelf_2', 'Study2', 1920.0, -2780.0, -90.0, 'solid'),
    ('Shelf_0', 'Study2', 1920.0, -2900.0, -90.0, 'solid'),
    ('Shelf_1', 'Study2', 1920.0, -3020.0, -90.0, 'solid'),
    ('Shelf_2', 'Study2', 1920.0, -3140.0, -90.0, 'solid'),
    ('Rug_5_4_Rug_Charcoal', 'Study2', 2270.0, -2895.0, -90.0, 'rug'),
    ('Desk_2_0p8', 'Study2', 2250.0, -2895.0, -90.0, 'solid'),
    ('Chair_Leather_Tan', 'Study2', 2175.0, -2895.0, -90.0, 'solid'),
    ('SideTable', 'Study2', 2590.0, -2565.0, -90.0, 'solid'),
    ('LoungeChair_Leather_Tan', 'Study2', 2500.0, -2565.0, -90.0, 'solid'),
    ('LoungeChair_Fabric_Stone', 'Study2', 2680.0, -2565.0, 90.0, 'solid'),
    ('FloorLamp', 'Study2', 2420.0, -2480.0, -90.0, 'solid'),
    ('Plant_Strelitzia_0p95_21', 'Study2', 3195.0, -3285.0, -90.0, 'solid'),
    ('Pendant_Linear_1p6_2p3', 'Study2', 2250.0, -2895.0, -90.0, 'overhead'),
    ('Bed_Wall_OakSlat', 'GuestRoom4', -4295.0, -2570.0, 0.0, 'solid'),
    ('Rug_4_3p2_Rug_Oat', 'GuestRoom4', -4295.0, -2270.0, 0.0, 'rug'),
    ('Bench_Bed', 'GuestRoom4', -4295.0, -2285.0, 0.0, 'solid'),
    ('Wardrobe', 'GuestRoom4', -4805.0, -2340.0, -90.0, 'solid'),
    ('SideTable', 'GuestRoom4', -3850.0, -2295.0, 0.0, 'solid'),
    ('LoungeChair_Fabric_Stone', 'GuestRoom4', -3850.0, -2390.0, 0.0, 'solid'),
    ('FloorLamp', 'GuestRoom4', -3775.0, -2470.0, 0.0, 'solid'),
    ('Desk_1p6_0p65', 'GuestRoom4', -4795.0, -1950.0, -90.0, 'solid'),
    ('Chair_Leather_Tan', 'GuestRoom4', -4715.0, -1950.0, 90.0, 'solid'),
    ('Plant_Strelitzia_0p9_41', 'GuestRoom4', -3800.0, -1710.0, 0.0, 'solid'),
    ('Shelf_0', 'Study3', -3320.0, -1460.0, 90.0, 'solid'),
    ('Shelf_1', 'Study3', -3320.0, -1340.0, 90.0, 'solid'),
    ('Shelf_2', 'Study3', -3320.0, -1220.0, 90.0, 'solid'),
    ('Shelf_0', 'Study3', -3320.0, -1100.0, 90.0, 'solid'),
    ('Shelf_1', 'Study3', -3320.0, -980.0, 90.0, 'solid'),
    ('Shelf_2', 'Study3', -3320.0, -860.0, 90.0, 'solid'),
    ('Rug_5_4_Rug_Charcoal', 'Study3', -3670.0, -1100.0, 90.0, 'rug'),
    ('Desk_2_0p8', 'Study3', -3650.0, -1100.0, 90.0, 'solid'),
    ('Chair_Leather_Tan', 'Study3', -3575.0, -1100.0, 90.0, 'solid'),
    ('SideTable', 'Study3', -3990.0, -1435.0, 90.0, 'solid'),
    ('LoungeChair_Leather_Tan', 'Study3', -3900.0, -1435.0, 90.0, 'solid'),
    ('LoungeChair_Fabric_Stone', 'Study3', -4080.0, -1435.0, -90.0, 'solid'),
    ('FloorLamp', 'Study3', -3820.0, -1520.0, 90.0, 'solid'),
    ('Plant_Strelitzia_0p95_21', 'Study3', -4585.0, -705.0, 90.0, 'solid'),
    ('Pendant_Linear_1p6_2p3', 'Study3', -3650.0, -1100.0, 90.0, 'overhead'),
    ('Rack', 'Storage1', -3875.0, 340.0, 180.0, 'solid'),
    ('Rack', 'Storage1', -4075.0, 340.0, 180.0, 'solid'),
    ('Rack', 'Storage1', -4275.0, 340.0, 180.0, 'solid'),
    ('Rack', 'Storage1', -4475.0, 340.0, 180.0, 'solid'),
    ('Rack', 'Storage1', -4675.0, 340.0, 180.0, 'solid'),
    ('Rack', 'Storage1', -3875.0, 50.0, 180.0, 'solid'),
    ('Rack', 'Storage1', -4075.0, 50.0, 180.0, 'solid'),
    ('Rack', 'Storage1', -4275.0, 50.0, 180.0, 'solid'),
    ('Rack', 'Storage1', -4475.0, 50.0, 180.0, 'solid'),
    ('Rack', 'Storage1', -4675.0, 50.0, 180.0, 'solid'),
    ('Rack', 'Storage1', -4075.0, -240.0, 180.0, 'solid'),
    ('Rack', 'Storage1', -4275.0, -240.0, 180.0, 'solid'),
    ('Rack', 'Storage1', -4475.0, -240.0, 180.0, 'solid'),
    ('Rack', 'Storage1', -4675.0, -240.0, 180.0, 'solid'),
    ('Rack', 'Storage2', -4715.0, 460.0, 0.0, 'solid'),
    ('Rack', 'Storage2', -4515.0, 460.0, 0.0, 'solid'),
    ('Rack', 'Storage2', -4315.0, 460.0, 0.0, 'solid'),
    ('Rack', 'Storage2', -4115.0, 460.0, 0.0, 'solid'),
    ('Rack', 'Storage2', -3915.0, 460.0, 0.0, 'solid'),
    ('Rack', 'Storage2', -4715.0, 750.0, 0.0, 'solid'),
    ('Rack', 'Storage2', -4515.0, 750.0, 0.0, 'solid'),
    ('Rack', 'Storage2', -4315.0, 750.0, 0.0, 'solid'),
    ('Rack', 'Storage2', -4115.0, 750.0, 0.0, 'solid'),
    ('Rack', 'Storage2', -4715.0, 1040.0, 0.0, 'solid'),
    ('Rack', 'Storage2', -4515.0, 1040.0, 0.0, 'solid'),
    ('Rack', 'Storage2', -4315.0, 1040.0, 0.0, 'solid'),
    ('Rack', 'Storage2', -4115.0, 1040.0, 0.0, 'solid'),
    ('Rack', 'Storage2', -3915.0, 1040.0, 0.0, 'solid'),
    ('Locker', 'SecurityRoom', -4814.0, 1452.0, 0.0, 'solid'),
    ('Locker', 'SecurityRoom', -4759.0, 1452.0, 0.0, 'solid'),
    ('Locker', 'SecurityRoom', -4704.0, 1452.0, 0.0, 'solid'),
    ('Locker', 'SecurityRoom', -4649.0, 1452.0, 0.0, 'solid'),
    ('Locker', 'SecurityRoom', -4594.0, 1452.0, 0.0, 'solid'),
    ('Locker', 'SecurityRoom', -4539.0, 1452.0, 0.0, 'solid'),
    ('Security_Desk', 'SecurityRoom', -4075.0, 1460.0, 0.0, 'solid'),
    ('Chair_Leather_Tan', 'SecurityRoom', -4180.0, 1560.0, 180.0, 'solid'),
    ('Chair_Leather_Tan', 'SecurityRoom', -3970.0, 1560.0, 180.0, 'solid'),
    ('Shelf_0', 'Study1', -2650.0, 3370.0, 180.0, 'solid'),
    ('Shelf_1', 'Study1', -2770.0, 3370.0, 180.0, 'solid'),
    ('Shelf_2', 'Study1', -2890.0, 3370.0, 180.0, 'solid'),
    ('Shelf_0', 'Study1', -3010.0, 3370.0, 180.0, 'solid'),
    ('Shelf_1', 'Study1', -3130.0, 3370.0, 180.0, 'solid'),
    ('Shelf_2', 'Study1', -3250.0, 3370.0, 180.0, 'solid'),
    ('Shelf_0', 'Study1', -3370.0, 3370.0, 180.0, 'solid'),
    ('Shelf_1', 'Study1', -3490.0, 3370.0, 180.0, 'solid'),
    ('Shelf_2', 'Study1', -3610.0, 3370.0, 180.0, 'solid'),
    ('Shelf_0', 'Study1', -3730.0, 3370.0, 180.0, 'solid'),
    ('Rug_5_4_Rug_Charcoal', 'Study1', -3205.0, 3020.0, 180.0, 'rug'),
    ('Desk_2_0p8', 'Study1', -3205.0, 3040.0, 180.0, 'solid'),
    ('Chair_Leather_Tan', 'Study1', -3205.0, 3115.0, 180.0, 'solid'),
    ('SideTable', 'Study1', -2675.0, 2700.0, 180.0, 'solid'),
    ('LoungeChair_Leather_Tan', 'Study1', -2675.0, 2790.0, 180.0, 'solid'),
    ('LoungeChair_Fabric_Stone', 'Study1', -2675.0, 2610.0, 0.0, 'solid'),
    ('FloorLamp', 'Study1', -2590.0, 2870.0, 180.0, 'solid'),
    ('Plant_Strelitzia_0p95_21', 'Study1', -3795.0, 2505.0, 180.0, 'solid'),
    ('Pendant_Linear_1p6_2p3', 'Study1', -3205.0, 3040.0, 180.0, 'overhead'),
    ('Rug_6_4p9_Rug_Charcoal', 'Lounge1', -2700.0, -1645.0, 0.0, 'rug'),
    ('Sofa_3p4_Fabric_Oat_Fabric_Rust', 'Lounge1', -2700.0, -1855.0, 0.0, 'solid'),
    ('CoffeeTable_0p55', 'Lounge1', -2730.0, -1660.0, 0.0, 'solid'),
    ('LoungeChair_Fabric_Stone', 'Lounge1', -2930.0, -1660.0, -90.0, 'solid'),
    ('LoungeChair_Fabric_Stone', 'Lounge1', -2470.0, -1660.0, 90.0, 'solid'),
    ('Sofa_2p8_Fabric_Stone_Fabric_Oat', 'Lounge1', -2700.0, -1460.0, 180.0, 'solid'),
    ('Plant_FiddleFig_1_11', 'Lounge1', -3195.0, -1895.0, 0.0, 'solid'),
    ('Plant_Strelitzia_1_12', 'Lounge1', -2205.0, -1895.0, 0.0, 'solid'),
    ('FloorLamp', 'Lounge1', -2920.0, -1920.0, 0.0, 'solid'),
    ('FloorLamp', 'Lounge1', -2480.0, -1920.0, 0.0, 'solid'),
    ('Pendant_Globes_9_0p7_3_4', 'Lounge1', -2700.0, -1660.0, 0.0, 'overhead'),
    ('Rack', 'Storage4', -3125.0, 60.0, 0.0, 'solid'),
    ('Rack', 'Storage4', -2925.0, 60.0, 0.0, 'solid'),
    ('Rack', 'Storage4', -2725.0, 60.0, 0.0, 'solid'),
    ('Rack', 'Storage4', -2525.0, 60.0, 0.0, 'solid'),
    ('Rack', 'Storage4', -2325.0, 60.0, 0.0, 'solid'),
    ('Rack', 'Storage4', -2925.0, 350.0, 0.0, 'solid'),
    ('Rack', 'Storage4', -2725.0, 350.0, 0.0, 'solid'),
    ('Rack', 'Storage4', -2525.0, 350.0, 0.0, 'solid'),
    ('Rack', 'Storage4', -2325.0, 350.0, 0.0, 'solid'),
    ('Rack', 'Storage4', -3125.0, 640.0, 0.0, 'solid'),
    ('Rack', 'Storage4', -2925.0, 640.0, 0.0, 'solid'),
    ('Rack', 'Storage4', -2725.0, 640.0, 0.0, 'solid'),
    ('Rack', 'Storage4', -2525.0, 640.0, 0.0, 'solid'),
    ('Rack', 'Storage4', -2325.0, 640.0, 0.0, 'solid'),
    ('Rack', 'Storage5', -3125.0, 1060.0, 0.0, 'solid'),
    ('Rack', 'Storage5', -2925.0, 1060.0, 0.0, 'solid'),
    ('Rack', 'Storage5', -2725.0, 1060.0, 0.0, 'solid'),
    ('Rack', 'Storage5', -2525.0, 1060.0, 0.0, 'solid'),
    ('Rack', 'Storage5', -2325.0, 1060.0, 0.0, 'solid'),
    ('Rack', 'Storage5', -2925.0, 1350.0, 0.0, 'solid'),
    ('Rack', 'Storage5', -2725.0, 1350.0, 0.0, 'solid'),
    ('Rack', 'Storage5', -2525.0, 1350.0, 0.0, 'solid'),
    ('Rack', 'Storage5', -2325.0, 1350.0, 0.0, 'solid'),
    ('Rack', 'Storage5', -3125.0, 1640.0, 0.0, 'solid'),
    ('Rack', 'Storage5', -2925.0, 1640.0, 0.0, 'solid'),
    ('Rack', 'Storage5', -2725.0, 1640.0, 0.0, 'solid'),
    ('Rack', 'Storage5', -2525.0, 1640.0, 0.0, 'solid'),
    ('Rack', 'Storage5', -2325.0, 1640.0, 0.0, 'solid'),
    ('Shelf_0', 'Office', -1420.0, -1980.0, 0.0, 'solid'),
    ('Shelf_1', 'Office', -1300.0, -1980.0, 0.0, 'solid'),
    ('Shelf_2', 'Office', -1180.0, -1980.0, 0.0, 'solid'),
    ('Shelf_0', 'Office', -1060.0, -1980.0, 0.0, 'solid'),
    ('Shelf_1', 'Office', -940.0, -1980.0, 0.0, 'solid'),
    ('Shelf_2', 'Office', -820.0, -1980.0, 0.0, 'solid'),
    ('Shelf_0', 'Office', -700.0, -1980.0, 0.0, 'solid'),
    ('Shelf_1', 'Office', -580.0, -1980.0, 0.0, 'solid'),
    ('Shelf_2', 'Office', -460.0, -1980.0, 0.0, 'solid'),
    ('Shelf_0', 'Office', -340.0, -1980.0, 0.0, 'solid'),
    ('Shelf_1', 'Office', -220.0, -1980.0, 0.0, 'solid'),
    ('Shelf_2', 'Office', -100.0, -1980.0, 0.0, 'solid'),
    ('Rug_6p2_4p4_Rug_Charcoal', 'Office', -700.0, -1630.0, 0.0, 'rug'),
    ('Desk_2p4_0p9', 'Office', -700.0, -1660.0, 0.0, 'solid'),
    ('LoungeChair_Leather_Tan', 'Office', -700.0, -1755.0, 0.0, 'solid'),
    ('Chair_Fabric_Oat', 'Office', -770.0, -1540.0, 180.0, 'solid'),
    ('Chair_Fabric_Oat', 'Office', -630.0, -1540.0, 180.0, 'solid'),
    ('Rug_4p1_3p6_Rug_Oat', 'Office', 15.0, -1470.0, 0.0, 'rug'),
    ('CoffeeTable_0p45', 'Office', 20.0, -1480.0, 0.0, 'solid'),
    ('LoungeChair_Fabric_Oat', 'Office', -110.0, -1545.0, -90.0, 'solid'),
    ('LoungeChair_Fabric_Oat', 'Office', -110.0, -1395.0, -90.0, 'solid'),
    ('Plant_FiddleFig_1p1_31', 'Office', -1595.0, -1895.0, 0.0, 'solid'),
    ('Plant_FiddleFig_1p1_32', 'Office', 195.0, -1895.0, 0.0, 'solid'),
    ('Plant_Strelitzia_1_33', 'Office', -1595.0, -1110.0, 0.0, 'solid'),
    ('Pendant_Linear_2_2p4', 'Office', -700.0, -1660.0, 0.0, 'overhead'),
    ('WaterFeature_Monolith', 'CentralHall', -200.0, 500.0, 0.0, 'solid'),
    ('Rug_4p8_3p9_Rug_Charcoal', 'CentralHall', -1000.0, 100.0, 0.0, 'rug'),
    ('Sofa_2p7_Fabric_Oat_Fabric_Rust', 'CentralHall', -1000.0, -35.0, 0.0, 'solid'),
    ('Sofa_2p7_Fabric_Stone_Fabric_Oat', 'CentralHall', -1000.0, 235.0, 180.0, 'solid'),
    ('CoffeeTable_0p45', 'CentralHall', -1025.0, 100.0, 0.0, 'solid'),
    ('LoungeChair_Fabric_Charcoal', 'CentralHall', -1185.0, 100.0, -90.0, 'solid'),
    ('LoungeChair_Fabric_Charcoal', 'CentralHall', -815.0, 100.0, 90.0, 'solid'),
    ('Pendant_Globes_12_0p9_11_8', 'CentralHall', -1000.0, 100.0, 0.0, 'overhead'),
    ('Rug_4p8_3p9_Rug_Oat', 'CentralHall', 600.0, 100.0, 0.0, 'rug'),
    ('Sofa_2p7_Fabric_Oat_Fabric_Rust', 'CentralHall', 600.0, -35.0, 0.0, 'solid'),
    ('Sofa_2p7_Fabric_Stone_Fabric_Oat', 'CentralHall', 600.0, 235.0, 180.0, 'solid'),
    ('CoffeeTable_0p45', 'CentralHall', 575.0, 100.0, 0.0, 'solid'),
    ('LoungeChair_Fabric_Charcoal', 'CentralHall', 415.0, 100.0, -90.0, 'solid'),
    ('LoungeChair_Fabric_Charcoal', 'CentralHall', 785.0, 100.0, 90.0, 'solid'),
    ('Pendant_Globes_12_0p9_12_8', 'CentralHall', 600.0, 100.0, 0.0, 'overhead'),
    ('Rug_4p8_3p9_Rug_Oat', 'CentralHall', -1000.0, 900.0, 0.0, 'rug'),
    ('Sofa_2p7_Fabric_Oat_Fabric_Rust', 'CentralHall', -1000.0, 765.0, 0.0, 'solid'),
    ('Sofa_2p7_Fabric_Stone_Fabric_Oat', 'CentralHall', -1000.0, 1035.0, 180.0, 'solid'),
    ('CoffeeTable_0p45', 'CentralHall', -1025.0, 900.0, 0.0, 'solid'),
    ('LoungeChair_Fabric_Charcoal', 'CentralHall', -1185.0, 900.0, -90.0, 'solid'),
    ('LoungeChair_Fabric_Charcoal', 'CentralHall', -815.0, 900.0, 90.0, 'solid'),
    ('Pendant_Globes_12_0p9_13_8', 'CentralHall', -1000.0, 900.0, 0.0, 'overhead'),
    ('Rug_4p8_3p9_Rug_Charcoal', 'CentralHall', 600.0, 900.0, 0.0, 'rug'),
    ('Sofa_2p7_Fabric_Oat_Fabric_Rust', 'CentralHall', 600.0, 765.0, 0.0, 'solid'),
    ('Sofa_2p7_Fabric_Stone_Fabric_Oat', 'CentralHall', 600.0, 1035.0, 180.0, 'solid'),
    ('CoffeeTable_0p45', 'CentralHall', 575.0, 900.0, 0.0, 'solid'),
    ('LoungeChair_Fabric_Charcoal', 'CentralHall', 415.0, 900.0, -90.0, 'solid'),
    ('LoungeChair_Fabric_Charcoal', 'CentralHall', 785.0, 900.0, 90.0, 'solid'),
    ('Pendant_Globes_12_0p9_14_8', 'CentralHall', 600.0, 900.0, 0.0, 'overhead'),
    ('Plant_FiddleFig_1p45_60', 'CentralHall', -1460.0, -740.0, 0.0, 'solid'),
    ('Plant_FiddleFig_1p45_61', 'CentralHall', 1060.0, -740.0, 0.0, 'solid'),
    ('Plant_FiddleFig_1p45_60', 'CentralHall', -1460.0, 1740.0, 0.0, 'solid'),
    ('Plant_FiddleFig_1p45_61', 'CentralHall', 1060.0, 1740.0, 0.0, 'solid'),
    ('Plant_Snake_1_70_1', 'CentralHall', -580.0, -890.0, 0.0, 'solid'),
    ('Plant_Snake_1_70_1', 'CentralHall', -120.0, -890.0, 0.0, 'solid'),
    ('Plant_Snake_1_71_1', 'CentralHall', -1590.0, 170.0, 0.0, 'solid'),
    ('Plant_Snake_1_71_1', 'CentralHall', -1590.0, 630.0, 0.0, 'solid'),
    ('Plant_Snake_1_71_1', 'CentralHall', 1190.0, 270.0, 0.0, 'solid'),
    ('Plant_Snake_1_71_1', 'CentralHall', 1190.0, 730.0, 0.0, 'solid'),
    ('Plant_Snake_1_70_1', 'CentralHall', -380.0, 1890.0, 0.0, 'solid'),
    ('Plant_Snake_1_70_1', 'CentralHall', 80.0, 1890.0, 0.0, 'solid'),
    ('Pendant_Globes_30_2p6_7_8', 'CentralHall', -200.0, 500.0, 0.0, 'overhead'),
    ('DiningTable_6', 'DiningRoom', 2500.0, -1500.0, 0.0, 'solid'),
    ('Chair_Fabric_Oat', 'DiningRoom', 2250.0, -1595.0, 0.0, 'solid'),
    ('Chair_Fabric_Oat', 'DiningRoom', 2250.0, -1405.0, 180.0, 'solid'),
    ('Chair_Fabric_Oat', 'DiningRoom', 2350.0, -1595.0, 0.0, 'solid'),
    ('Chair_Fabric_Oat', 'DiningRoom', 2350.0, -1405.0, 180.0, 'solid'),
    ('Chair_Fabric_Oat', 'DiningRoom', 2450.0, -1595.0, 0.0, 'solid'),
    ('Chair_Fabric_Oat', 'DiningRoom', 2450.0, -1405.0, 180.0, 'solid'),
    ('Chair_Fabric_Oat', 'DiningRoom', 2550.0, -1595.0, 0.0, 'solid'),
    ('Chair_Fabric_Oat', 'DiningRoom', 2550.0, -1405.0, 180.0, 'solid'),
    ('Chair_Fabric_Oat', 'DiningRoom', 2650.0, -1595.0, 0.0, 'solid'),
    ('Chair_Fabric_Oat', 'DiningRoom', 2650.0, -1405.0, 180.0, 'solid'),
    ('Chair_Fabric_Oat', 'DiningRoom', 2750.0, -1595.0, 0.0, 'solid'),
    ('Chair_Fabric_Oat', 'DiningRoom', 2750.0, -1405.0, 180.0, 'solid'),
    ('Plant_FiddleFig_1p05_13', 'DiningRoom', 1805.0, -1895.0, 0.0, 'solid'),
    ('Plant_FiddleFig_1p05_14', 'DiningRoom', 3195.0, -1895.0, 0.0, 'solid'),
    ('Pendant_Linear_5p4_2p75', 'DiningRoom', 2500.0, -1500.0, 0.0, 'overhead'),
    ('Rug_6_4p9_Rug_Charcoal', 'Lounge2', 2300.0, 355.0, 0.0, 'rug'),
    ('Sofa_3p4_Fabric_Oat_Fabric_Rust', 'Lounge2', 2300.0, 145.0, 0.0, 'solid'),
    ('CoffeeTable_0p55', 'Lounge2', 2270.0, 340.0, 0.0, 'solid'),
    ('LoungeChair_Fabric_Stone', 'Lounge2', 2070.0, 340.0, -90.0, 'solid'),
    ('LoungeChair_Fabric_Stone', 'Lounge2', 2530.0, 340.0, 90.0, 'solid'),
    ('Sofa_2p8_Fabric_Stone_Fabric_Oat', 'Lounge2', 2300.0, 540.0, 180.0, 'solid'),
    ('Plant_FiddleFig_1_11', 'Lounge2', 1805.0, 105.0, 0.0, 'solid'),
    ('Plant_Strelitzia_1_12', 'Lounge2', 2795.0, 105.0, 0.0, 'solid'),
    ('FloorLamp', 'Lounge2', 2080.0, 80.0, 0.0, 'solid'),
    ('FloorLamp', 'Lounge2', 2520.0, 80.0, 0.0, 'solid'),
    ('Pendant_Globes_9_0p7_3_4', 'Lounge2', 2300.0, 340.0, 0.0, 'overhead'),
    ('Rack', 'Storage3', 3875.0, -2330.0, 0.0, 'solid'),
    ('Rack', 'Storage3', 4075.0, -2330.0, 0.0, 'solid'),
    ('Rack', 'Storage3', 4275.0, -2330.0, 0.0, 'solid'),
    ('Rack', 'Storage3', 4475.0, -2330.0, 0.0, 'solid'),
    ('Rack', 'Storage3', 4675.0, -2330.0, 0.0, 'solid'),
    ('Rack', 'Storage3', 3875.0, -2040.0, 0.0, 'solid'),
    ('Rack', 'Storage3', 4075.0, -2040.0, 0.0, 'solid'),
    ('Rack', 'Storage3', 4275.0, -2040.0, 0.0, 'solid'),
    ('Rack', 'Storage3', 4475.0, -2040.0, 0.0, 'solid'),
    ('Rack', 'Storage3', 4675.0, -2040.0, 0.0, 'solid'),
    ('Rack', 'Storage3', 4075.0, -1750.0, 0.0, 'solid'),
    ('Rack', 'Storage3', 4275.0, -1750.0, 0.0, 'solid'),
    ('Rack', 'Storage3', 4475.0, -1750.0, 0.0, 'solid'),
    ('Rack', 'Storage3', 4675.0, -1750.0, 0.0, 'solid'),
    ('Kitchen_TallUnit', 'Kitchen', 3985.0, -1380.0, 0.0, 'solid'),
    ('Kitchen_Island', 'Kitchen', 3730.0, -900.0, 0.0, 'solid'),
    ('BarStool', 'Kitchen', 3650.0, -805.0, 180.0, 'solid'),
    ('BarStool', 'Kitchen', 3730.0, -805.0, 180.0, 'solid'),
    ('BarStool', 'Kitchen', 3810.0, -805.0, 180.0, 'solid'),
    ('Pendant_Linear_2p4_2p4', 'Kitchen', 3730.0, -900.0, 0.0, 'overhead'),
    ('Rack', 'FreezerRoom', 4155.0, -835.0, -90.0, 'solid'),
    ('Rack', 'FreezerRoom', 4155.0, -635.0, -90.0, 'solid'),
    ('Rack', 'FreezerRoom', 4435.0, -1235.0, -90.0, 'solid'),
    ('Rack', 'FreezerRoom', 4435.0, -635.0, -90.0, 'solid'),
    ('Bed_Wall_OakSlat', 'GuestRoom5', 3895.0, -380.0, 0.0, 'solid'),
    ('Rug_4_3p2_Rug_Oat', 'GuestRoom5', 3895.0, -80.0, 0.0, 'rug'),
    ('Bed_King', 'GuestRoom5', 3895.0, -380.0, 0.0, 'solid'),
    ('Nightstand_Floating', 'GuestRoom5', 3727.5, -380.0, 0.0, 'solid'),
    ('Nightstand_Floating', 'GuestRoom5', 4062.5, -380.0, 0.0, 'solid'),
    ('Bench_Bed', 'GuestRoom5', 3895.0, -95.0, 0.0, 'solid'),
    ('Wardrobe', 'GuestRoom5', 3385.0, -150.0, -90.0, 'solid'),
    ('SideTable', 'GuestRoom5', 4340.0, -105.0, 0.0, 'solid'),
    ('LoungeChair_Fabric_Stone', 'GuestRoom5', 4340.0, -200.0, 0.0, 'solid'),
    ('LoungeChair_Fabric_Oat', 'GuestRoom5', 4340.0, -10.0, 180.0, 'solid'),
    ('FloorLamp', 'GuestRoom5', 4415.0, -280.0, 0.0, 'solid'),
    ('Plant_Strelitzia_0p9_41', 'GuestRoom5', 4390.0, 500.0, 0.0, 'solid'),
    ('Bed_Wall_OakSlat', 'GuestRoom6', 3895.0, 620.0, 0.0, 'solid'),
    ('Rug_4_3p2_Rug_Oat', 'GuestRoom6', 3895.0, 920.0, 0.0, 'rug'),
    ('Bed_King', 'GuestRoom6', 3895.0, 620.0, 0.0, 'solid'),
    ('Nightstand_Floating', 'GuestRoom6', 3727.5, 620.0, 0.0, 'solid'),
    ('Nightstand_Floating', 'GuestRoom6', 4062.5, 620.0, 0.0, 'solid'),
    ('Bench_Bed', 'GuestRoom6', 3895.0, 905.0, 0.0, 'solid'),
    ('Wardrobe', 'GuestRoom6', 3385.0, 850.0, -90.0, 'solid'),
    ('SideTable', 'GuestRoom6', 4340.0, 895.0, 0.0, 'solid'),
    ('LoungeChair_Fabric_Stone', 'GuestRoom6', 4340.0, 800.0, 0.0, 'solid'),
    ('LoungeChair_Fabric_Oat', 'GuestRoom6', 4340.0, 990.0, 180.0, 'solid'),
    ('FloorLamp', 'GuestRoom6', 4415.0, 720.0, 0.0, 'solid'),
    ('Chair_Leather_Tan', 'GuestRoom6', 3475.0, 1260.0, 90.0, 'solid'),
    ('Plant_Strelitzia_0p9_41', 'GuestRoom6', 4390.0, 1500.0, 0.0, 'solid'),
    ('Bed_Wall_OakSlat', 'GuestRoom7', 3895.0, 1620.0, 0.0, 'solid'),
    ('Rug_4_3p2_Rug_Oat', 'GuestRoom7', 3895.0, 1920.0, 0.0, 'rug'),
    ('Bed_King', 'GuestRoom7', 3895.0, 1620.0, 0.0, 'solid'),
    ('Nightstand_Floating', 'GuestRoom7', 3727.5, 1620.0, 0.0, 'solid'),
    ('Nightstand_Floating', 'GuestRoom7', 4062.5, 1620.0, 0.0, 'solid'),
    ('Bench_Bed', 'GuestRoom7', 3895.0, 1905.0, 0.0, 'solid'),
    ('Wardrobe', 'GuestRoom7', 3385.0, 1850.0, -90.0, 'solid'),
    ('SideTable', 'GuestRoom7', 4340.0, 1895.0, 0.0, 'solid'),
    ('LoungeChair_Fabric_Stone', 'GuestRoom7', 4340.0, 1800.0, 0.0, 'solid'),
    ('LoungeChair_Fabric_Oat', 'GuestRoom7', 4340.0, 1990.0, 180.0, 'solid'),
    ('FloorLamp', 'GuestRoom7', 4415.0, 1720.0, 0.0, 'solid'),
    ('Plant_Strelitzia_0p9_41', 'GuestRoom7', 4390.0, 2490.0, 0.0, 'solid'),
    ('Rug_2p8_3p6_Rug_Charcoal', 'Entrance', -200.0, 2650.0, 0.0, 'rug'),
    ('Plant_Olive_1_52', 'Entrance', -580.0, 2740.0, 0.0, 'solid'),
    ('Plant_Olive_1_53', 'Entrance', 180.0, 2740.0, 0.0, 'solid'),
    ('Plant_Snake_1_72_1', 'Entrance', -710.0, 2530.0, 0.0, 'solid'),
    ('Plant_Snake_1_72_1', 'Entrance', 310.0, 2530.0, 0.0, 'solid'),
    ('StoneBench_1p8', 'Entrance', -510.0, 2500.0, 0.0, 'solid'),
    ('StoneBench_1p8', 'Entrance', 110.0, 2500.0, 0.0, 'solid'),
    ('Pendant_Globes_8_0p6_21_4', 'Entrance', -200.0, 2650.0, 0.0, 'overhead'),
    ('StoneBench_2p8', 'Gallery2', 2300.0, 3525.0, 180.0, 'solid'),
    ('StoneBench_2p8', 'Gallery2', 2300.0, 3265.0, 0.0, 'solid'),
    ('Plant_Olive_1p15_50', 'Gallery2', 3130.0, 4230.0, 180.0, 'solid'),
    ('Plant_Olive_1p15_51', 'Gallery2', 1470.0, 4230.0, 180.0, 'solid'),
    ('Plant_Olive_1p15_50', 'Gallery2', 3130.0, 2560.0, 180.0, 'solid'),
    ('Plant_Olive_1p15_51', 'Gallery2', 1470.0, 2560.0, 180.0, 'solid'),
    ('Jail_Bench', 'Jail', -4650.0, 2342.0, 0.0, 'solid'),
    ('Plant_Snake_1_73_1', 'Corridor', -3600.0, -2300.0, 0.0, 'solid'),
    ('Plant_Snake_1_73_1', 'Corridor', -3600.0, 2300.0, 0.0, 'solid'),
    ('Plant_Snake_1_74_1', 'Corridor', 3200.0, 2300.0, 0.0, 'solid'),
    ('Plant_Snake_1_73_1', 'Corridor', -2000.0, -1900.0, 0.0, 'solid'),
    ('Plant_Snake_1_74_1', 'Corridor', 1600.0, -1900.0, 0.0, 'solid'),
    ('Plant_Snake_1_73_1', 'Corridor', -2000.0, 1900.0, 0.0, 'solid'),
    ('Plant_Snake_1_74_1', 'Corridor', 1600.0, 1900.0, 0.0, 'solid')
]
LIGHTS = [
    ('Fill_GuestRoom1_00', -3500.0, -3150.0, 360, 75, 750, 3200, False),
    ('Fill_GuestRoom1_01', -3500.0, -2650.0, 360, 75, 750, 3200, False),
    ('Fill_GuestRoom1_10', -3100.0, -3150.0, 360, 75, 750, 3200, False),
    ('Fill_GuestRoom1_11', -3100.0, -2650.0, 360, 75, 750, 3200, False),
    ('Fill_GuestRoom1_20', -2700.0, -3150.0, 360, 75, 750, 3200, False),
    ('Fill_GuestRoom1_21', -2700.0, -2650.0, 360, 75, 750, 3200, False),
    ('Fill_GuestRoom2_00', -2300.0, -3150.0, 360, 75, 750, 3200, False),
    ('Fill_GuestRoom2_01', -2300.0, -2650.0, 360, 75, 750, 3200, False),
    ('Fill_GuestRoom2_10', -1900.0, -3150.0, 360, 75, 750, 3200, False),
    ('Fill_GuestRoom2_11', -1900.0, -2650.0, 360, 75, 750, 3200, False),
    ('Fill_GuestRoom2_20', -1500.0, -3150.0, 360, 75, 750, 3200, False),
    ('Fill_GuestRoom2_21', -1500.0, -2650.0, 360, 75, 750, 3200, False),
    ('Fill_GuestRoom3_00', -1100.0, -3150.0, 360, 75, 750, 3200, False),
    ('Fill_GuestRoom3_01', -1100.0, -2650.0, 360, 75, 750, 3200, False),
    ('Fill_GuestRoom3_10', -700.0, -3150.0, 360, 75, 750, 3200, False),
    ('Fill_GuestRoom3_11', -700.0, -2650.0, 360, 75, 750, 3200, False),
    ('Fill_GuestRoom3_20', -300.0, -3150.0, 360, 75, 750, 3200, False),
    ('Fill_GuestRoom3_21', -300.0, -2650.0, 360, 75, 750, 3200, False),
    ('Fill_Gallery1_00', 150.0, -4150.0, 360, 75, 750, 3200, False),
    ('Fill_Gallery1_01', 150.0, -3650.0, 360, 75, 750, 3200, False),
    ('Fill_Gallery1_02', 150.0, -3150.0, 360, 75, 750, 3200, False),
    ('Fill_Gallery1_03', 150.0, -2650.0, 360, 75, 750, 3200, False),
    ('Fill_Gallery1_10', 650.0, -4150.0, 360, 75, 750, 3200, False),
    ('Fill_Gallery1_11', 650.0, -3650.0, 360, 75, 750, 3200, False),
    ('Fill_Gallery1_12', 650.0, -3150.0, 360, 75, 750, 3200, False),
    ('Fill_Gallery1_13', 650.0, -2650.0, 360, 75, 750, 3200, False),
    ('Fill_Gallery1_20', 1150.0, -4150.0, 360, 75, 750, 3200, False),
    ('Fill_Gallery1_21', 1150.0, -3650.0, 360, 75, 750, 3200, False),
    ('Fill_Gallery1_22', 1150.0, -3150.0, 360, 75, 750, 3200, False),
    ('Fill_Gallery1_23', 1150.0, -2650.0, 360, 75, 750, 3200, False),
    ('Fill_Gallery1_30', 1650.0, -4150.0, 360, 75, 750, 3200, False),
    ('Fill_Gallery1_31', 1650.0, -3650.0, 360, 75, 750, 3200, False),
    ('Fill_Gallery1_32', 1650.0, -3150.0, 360, 75, 750, 3200, False),
    ('Fill_Gallery1_33', 1650.0, -2650.0, 360, 75, 750, 3200, False),
    ('Fill_Study2_00', 2133.3, -3150.0, 360, 75, 750, 3200, False),
    ('Fill_Study2_01', 2133.3, -2650.0, 360, 75, 750, 3200, False),
    ('Fill_Study2_10', 2600.0, -3150.0, 360, 75, 750, 3200, False),
    ('Fill_Study2_11', 2600.0, -2650.0, 360, 75, 750, 3200, False),
    ('Fill_Study2_20', 3066.7, -3150.0, 360, 75, 750, 3200, False),
    ('Fill_Study2_21', 3066.7, -2650.0, 360, 75, 750, 3200, False),
    ('Fill_BigVault_00', 3500.0, -3150.0, 360, 75, 750, 4500, False),
    ('Fill_BigVault_01', 3500.0, -2650.0, 360, 75, 750, 4500, False),
    ('Fill_BigVault_10', 3900.0, -3150.0, 360, 75, 750, 4500, False),
    ('Fill_BigVault_11', 3900.0, -2650.0, 360, 75, 750, 4500, False),
    ('Fill_BigVault_20', 4300.0, -3150.0, 360, 75, 750, 4500, False),
    ('Fill_BigVault_21', 4300.0, -2650.0, 360, 75, 750, 4500, False),
    ('Fill_GuestRoom4_00', -4700.0, -2350.0, 360, 75, 750, 3200, False),
    ('Fill_GuestRoom4_01', -4700.0, -1850.0, 360, 75, 750, 3200, False),
    ('Fill_GuestRoom4_10', -4300.0, -2350.0, 360, 75, 750, 3200, False),
    ('Fill_GuestRoom4_11', -4300.0, -1850.0, 360, 75, 750, 3200, False),
    ('Fill_GuestRoom4_20', -3900.0, -2350.0, 360, 75, 750, 3200, False),
    ('Fill_GuestRoom4_21', -3900.0, -1850.0, 360, 75, 750, 3200, False),
    ('Fill_Study3_00', -4466.7, -1350.0, 360, 75, 750, 3200, False),
    ('Fill_Study3_01', -4466.7, -850.0, 360, 75, 750, 3200, False),
    ('Fill_Study3_10', -4000.0, -1350.0, 360, 75, 750, 3200, False),
    ('Fill_Study3_11', -4000.0, -850.0, 360, 75, 750, 3200, False),
    ('Fill_Study3_20', -3533.3, -1350.0, 360, 75, 750, 3200, False),
    ('Fill_Study3_21', -3533.3, -850.0, 360, 75, 750, 3200, False),
    ('Fill_Storage1_00', -4700.0, -350.0, 360, 75, 750, 4500, False),
    ('Fill_Storage1_01', -4700.0, 150.0, 360, 75, 750, 4500, False),
    ('Fill_Storage1_10', -4300.0, -350.0, 360, 75, 750, 4500, False),
    ('Fill_Storage1_11', -4300.0, 150.0, 360, 75, 750, 4500, False),
    ('Fill_Storage1_20', -3900.0, -350.0, 360, 75, 750, 4500, False),
    ('Fill_Storage1_21', -3900.0, 150.0, 360, 75, 750, 4500, False),
    ('Fill_Storage2_00', -4700.0, 650.0, 360, 75, 750, 4500, False),
    ('Fill_Storage2_01', -4700.0, 1150.0, 360, 75, 750, 4500, False),
    ('Fill_Storage2_10', -4300.0, 650.0, 360, 75, 750, 4500, False),
    ('Fill_Storage2_11', -4300.0, 1150.0, 360, 75, 750, 4500, False),
    ('Fill_Storage2_20', -3900.0, 650.0, 360, 75, 750, 4500, False),
    ('Fill_Storage2_21', -3900.0, 1150.0, 360, 75, 750, 4500, False),
    ('Fill_SecurityRoom_00', -4700.0, 1650.0, 360, 75, 750, 4500, False),
    ('Fill_SecurityRoom_10', -4300.0, 1650.0, 360, 75, 750, 4500, False),
    ('Fill_SecurityRoom_11', -4300.0, 2150.0, 360, 75, 750, 4500, False),
    ('Fill_SecurityRoom_20', -3900.0, 1650.0, 360, 75, 750, 4500, False),
    ('Fill_SecurityRoom_21', -3900.0, 2150.0, 360, 75, 750, 4500, False),
    ('Fill_ValuableRoom2_00', -4650.0, 2650.0, 360, 75, 750, 4500, False),
    ('Fill_ValuableRoom2_01', -4650.0, 3150.0, 360, 75, 750, 4500, False),
    ('Fill_ValuableRoom2_10', -4150.0, 2650.0, 360, 75, 750, 4500, False),
    ('Fill_ValuableRoom2_11', -4150.0, 3150.0, 360, 75, 750, 4500, False),
    ('Fill_Study1_00', -3666.7, 2650.0, 360, 75, 750, 3200, False),
    ('Fill_Study1_01', -3666.7, 3150.0, 360, 75, 750, 3200, False),
    ('Fill_Study1_10', -3200.0, 2650.0, 360, 75, 750, 3200, False),
    ('Fill_Study1_11', -3200.0, 3150.0, 360, 75, 750, 3200, False),
    ('Fill_Study1_20', -2733.3, 2650.0, 360, 75, 750, 3200, False),
    ('Fill_Study1_21', -2733.3, 3150.0, 360, 75, 750, 3200, False),
    ('Fill_Lounge1_00', -3100.0, -1750.0, 360, 75, 750, 3200, False),
    ('Fill_Lounge1_01', -3100.0, -1250.0, 360, 75, 750, 3200, False),
    ('Fill_Lounge1_10', -2700.0, -1750.0, 360, 75, 750, 3200, False),
    ('Fill_Lounge1_11', -2700.0, -1250.0, 360, 75, 750, 3200, False),
    ('Fill_Lounge1_20', -2300.0, -1750.0, 360, 75, 750, 3200, False),
    ('Fill_Lounge1_21', -2300.0, -1250.0, 360, 75, 750, 3200, False),
    ('Fill_EmptySpace1_00', -3100.0, -750.0, 360, 75, 750, 4500, False),
    ('Fill_EmptySpace1_01', -3100.0, -250.0, 360, 75, 750, 4500, False),
    ('Fill_EmptySpace1_10', -2700.0, -750.0, 360, 75, 750, 4500, False),
    ('Fill_EmptySpace1_11', -2700.0, -250.0, 360, 75, 750, 4500, False),
    ('Fill_EmptySpace1_20', -2300.0, -750.0, 360, 75, 750, 4500, False),
    ('Fill_EmptySpace1_21', -2300.0, -250.0, 360, 75, 750, 4500, False),
    ('Fill_Storage4_00', -3100.0, 250.0, 360, 75, 750, 4500, False),
    ('Fill_Storage4_01', -3100.0, 750.0, 360, 75, 750, 4500, False),
    ('Fill_Storage4_10', -2700.0, 250.0, 360, 75, 750, 4500, False),
    ('Fill_Storage4_11', -2700.0, 750.0, 360, 75, 750, 4500, False),
    ('Fill_Storage4_20', -2300.0, 250.0, 360, 75, 750, 4500, False),
    ('Fill_Storage4_21', -2300.0, 750.0, 360, 75, 750, 4500, False),
    ('Fill_Storage5_00', -3100.0, 1250.0, 360, 75, 750, 4500, False),
    ('Fill_Storage5_01', -3100.0, 1750.0, 360, 75, 750, 4500, False),
    ('Fill_Storage5_10', -2700.0, 1250.0, 360, 75, 750, 4500, False),
    ('Fill_Storage5_11', -2700.0, 1750.0, 360, 75, 750, 4500, False),
    ('Fill_Storage5_20', -2300.0, 1250.0, 360, 75, 750, 4500, False),
    ('Fill_Storage5_21', -2300.0, 1750.0, 360, 75, 750, 4500, False),
    ('Fill_Office_00', -1450.0, -1750.0, 360, 75, 750, 3200, False),
    ('Fill_Office_01', -1450.0, -1250.0, 360, 75, 750, 3200, False),
    ('Fill_Office_10', -950.0, -1750.0, 360, 75, 750, 3200, False),
    ('Fill_Office_11', -950.0, -1250.0, 360, 75, 750, 3200, False),
    ('Fill_Office_20', -450.0, -1750.0, 360, 75, 750, 3200, False),
    ('Fill_Office_21', -450.0, -1250.0, 360, 75, 750, 3200, False),
    ('Fill_Office_30', 50.0, -1750.0, 360, 75, 750, 3200, False),
    ('Fill_Office_31', 50.0, -1250.0, 360, 75, 750, 3200, False),
    ('Fill_ValuableRoom1_00', 550.0, -1750.0, 360, 75, 750, 4500, False),
    ('Fill_ValuableRoom1_01', 550.0, -1250.0, 360, 75, 750, 4500, False),
    ('Fill_ValuableRoom1_10', 1050.0, -1750.0, 360, 75, 750, 4500, False),
    ('Fill_ValuableRoom1_11', 1050.0, -1250.0, 360, 75, 750, 4500, False),
    ('Fill_Hall_37_39', -1200.0, -500.0, 700, 260, 2200, 3500, False),
    ('Fill_Hall_37_49', -1200.0, 500.0, 700, 260, 2200, 3500, False),
    ('Fill_Hall_37_59', -1200.0, 1500.0, 700, 260, 2200, 3500, False),
    ('Fill_Hall_47_39', -200.0, -500.0, 700, 260, 2200, 3500, False),
    ('Fill_Hall_47_49', -200.0, 500.0, 700, 260, 2200, 3500, False),
    ('Fill_Hall_47_59', -200.0, 1500.0, 700, 260, 2200, 3500, False),
    ('Fill_Hall_57_39', 800.0, -500.0, 700, 260, 2200, 3500, False),
    ('Fill_Hall_57_49', 800.0, 500.0, 700, 260, 2200, 3500, False),
    ('Fill_Hall_57_59', 800.0, 1500.0, 700, 260, 2200, 3500, False),
    ('Fill_DiningRoom_00', 1900.0, -1750.0, 360, 75, 750, 3200, False),
    ('Fill_DiningRoom_01', 1900.0, -1250.0, 360, 75, 750, 3200, False),
    ('Fill_DiningRoom_10', 2300.0, -1750.0, 360, 75, 750, 3200, False),
    ('Fill_DiningRoom_11', 2300.0, -1250.0, 360, 75, 750, 3200, False),
    ('Fill_DiningRoom_20', 2700.0, -1750.0, 360, 75, 750, 3200, False),
    ('Fill_DiningRoom_21', 2700.0, -1250.0, 360, 75, 750, 3200, False),
    ('Fill_DiningRoom_30', 3100.0, -1750.0, 360, 75, 750, 3200, False),
    ('Fill_DiningRoom_31', 3100.0, -1250.0, 360, 75, 750, 3200, False),
    ('Fill_EmptySpace2_00', 1900.0, -750.0, 360, 75, 750, 4500, False),
    ('Fill_EmptySpace2_01', 1900.0, -250.0, 360, 75, 750, 4500, False),
    ('Fill_EmptySpace2_10', 2300.0, -750.0, 360, 75, 750, 4500, False),
    ('Fill_EmptySpace2_11', 2300.0, -250.0, 360, 75, 750, 4500, False),
    ('Fill_EmptySpace2_20', 2700.0, -750.0, 360, 75, 750, 4500, False),
    ('Fill_EmptySpace2_21', 2700.0, -250.0, 360, 75, 750, 4500, False),
    ('Fill_Lounge2_00', 1900.0, 250.0, 360, 75, 750, 3200, False),
    ('Fill_Lounge2_01', 1900.0, 750.0, 360, 75, 750, 3200, False),
    ('Fill_Lounge2_10', 2300.0, 250.0, 360, 75, 750, 3200, False),
    ('Fill_Lounge2_11', 2300.0, 750.0, 360, 75, 750, 3200, False),
    ('Fill_Lounge2_20', 2700.0, 250.0, 360, 75, 750, 3200, False),
    ('Fill_Lounge2_21', 2700.0, 750.0, 360, 75, 750, 3200, False),
    ('Fill_EmptySpace3_00', 1900.0, 1250.0, 360, 75, 750, 4500, False),
    ('Fill_EmptySpace3_01', 1900.0, 1750.0, 360, 75, 750, 4500, False),
    ('Fill_EmptySpace3_10', 2300.0, 1250.0, 360, 75, 750, 4500, False),
    ('Fill_EmptySpace3_11', 2300.0, 1750.0, 360, 75, 750, 4500, False),
    ('Fill_EmptySpace3_20', 2700.0, 1250.0, 360, 75, 750, 4500, False),
    ('Fill_EmptySpace3_21', 2700.0, 1750.0, 360, 75, 750, 4500, False),
    ('Fill_Storage3_00', 3900.0, -2150.0, 360, 75, 750, 4500, False),
    ('Fill_Storage3_01', 3900.0, -1650.0, 360, 75, 750, 4500, False),
    ('Fill_Storage3_10', 4300.0, -2150.0, 360, 75, 750, 4500, False),
    ('Fill_Storage3_11', 4300.0, -1650.0, 360, 75, 750, 4500, False),
    ('Fill_Storage3_20', 4700.0, -2150.0, 360, 75, 750, 4500, False),
    ('Fill_Storage3_21', 4700.0, -1650.0, 360, 75, 750, 4500, False),
    ('Fill_Kitchen_00', 3500.0, -1150.0, 360, 75, 750, 3200, False),
    ('Fill_Kitchen_01', 3500.0, -650.0, 360, 75, 750, 3200, False),
    ('Fill_Kitchen_10', 3900.0, -1150.0, 360, 75, 750, 3200, False),
    ('Fill_Kitchen_11', 3900.0, -650.0, 360, 75, 750, 3200, False),
    ('Fill_FreezerRoom_00', 4300.0, -1150.0, 360, 75, 750, 4500, False),
    ('Fill_FreezerRoom_01', 4300.0, -650.0, 360, 75, 750, 4500, False),
    ('Fill_GuestRoom5_00', 3500.0, -150.0, 360, 75, 750, 3200, False),
    ('Fill_GuestRoom5_01', 3500.0, 350.0, 360, 75, 750, 3200, False),
    ('Fill_GuestRoom5_10', 3900.0, -150.0, 360, 75, 750, 3200, False),
    ('Fill_GuestRoom5_11', 3900.0, 350.0, 360, 75, 750, 3200, False),
    ('Fill_GuestRoom5_20', 4300.0, -150.0, 360, 75, 750, 3200, False),
    ('Fill_GuestRoom5_21', 4300.0, 350.0, 360, 75, 750, 3200, False),
    ('Fill_GuestRoom6_00', 3500.0, 850.0, 360, 75, 750, 3200, False),
    ('Fill_GuestRoom6_01', 3500.0, 1350.0, 360, 75, 750, 3200, False),
    ('Fill_GuestRoom6_10', 3900.0, 850.0, 360, 75, 750, 3200, False),
    ('Fill_GuestRoom6_11', 3900.0, 1350.0, 360, 75, 750, 3200, False),
    ('Fill_GuestRoom6_20', 4300.0, 850.0, 360, 75, 750, 3200, False),
    ('Fill_GuestRoom6_21', 4300.0, 1350.0, 360, 75, 750, 3200, False),
    ('Fill_GuestRoom7_00', 3500.0, 1850.0, 360, 75, 750, 3200, False),
    ('Fill_GuestRoom7_01', 3500.0, 2350.0, 360, 75, 750, 3200, False),
    ('Fill_GuestRoom7_10', 3900.0, 1850.0, 360, 75, 750, 3200, False),
    ('Fill_GuestRoom7_11', 3900.0, 2350.0, 360, 75, 750, 3200, False),
    ('Fill_GuestRoom7_20', 4300.0, 1850.0, 360, 75, 750, 3200, False),
    ('Fill_GuestRoom7_21', 4300.0, 2350.0, 360, 75, 750, 3200, False),
    ('Fill_ValuableRoom3_00', 3550.0, 2850.0, 360, 75, 750, 4500, False),
    ('Fill_ValuableRoom3_01', 3550.0, 3350.0, 360, 75, 750, 4500, False),
    ('Fill_ValuableRoom3_10', 4050.0, 2850.0, 360, 75, 750, 4500, False),
    ('Fill_ValuableRoom3_11', 4050.0, 3350.0, 360, 75, 750, 4500, False),
    ('Fill_Entrance_00', -600.0, 2650.0, 360, 75, 750, 3200, False),
    ('Fill_Entrance_10', -200.0, 2650.0, 360, 75, 750, 3200, False),
    ('Fill_Entrance_20', 200.0, 2650.0, 360, 75, 750, 3200, False),
    ('Fill_Gallery2_00', 1550.0, 2650.0, 360, 75, 750, 3200, False),
    ('Fill_Gallery2_01', 1550.0, 3150.0, 360, 75, 750, 3200, False),
    ('Fill_Gallery2_02', 1550.0, 3650.0, 360, 75, 750, 3200, False),
    ('Fill_Gallery2_03', 1550.0, 4150.0, 360, 75, 750, 3200, False),
    ('Fill_Gallery2_10', 2050.0, 2650.0, 360, 75, 750, 3200, False),
    ('Fill_Gallery2_11', 2050.0, 3150.0, 360, 75, 750, 3200, False),
    ('Fill_Gallery2_12', 2050.0, 3650.0, 360, 75, 750, 3200, False),
    ('Fill_Gallery2_13', 2050.0, 4150.0, 360, 75, 750, 3200, False),
    ('Fill_Gallery2_20', 2550.0, 2650.0, 360, 75, 750, 3200, False),
    ('Fill_Gallery2_21', 2550.0, 3150.0, 360, 75, 750, 3200, False),
    ('Fill_Gallery2_22', 2550.0, 3650.0, 360, 75, 750, 3200, False),
    ('Fill_Gallery2_23', 2550.0, 4150.0, 360, 75, 750, 3200, False),
    ('Fill_Gallery2_30', 3050.0, 2650.0, 360, 75, 750, 3200, False),
    ('Fill_Gallery2_31', 3050.0, 3150.0, 360, 75, 750, 3200, False),
    ('Fill_Gallery2_32', 3050.0, 3650.0, 360, 75, 750, 3200, False),
    ('Fill_Gallery2_33', 3050.0, 4150.0, 360, 75, 750, 3200, False),
    ('Fill_Jail_00', -4650.0, 2150.0, 360, 75, 750, 4500, False),
    ('Fill_Corridor_0_0', -3482.4, -2200.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_0_1', -3047.1, -2200.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_0_2', -2611.8, -2200.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_0_3', -2176.5, -2200.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_0_4', -1741.2, -2200.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_0_5', -1305.9, -2200.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_0_6', -870.6, -2200.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_0_7', -435.3, -2200.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_0_8', 0.0, -2200.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_0_9', 435.3, -2200.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_0_10', 870.6, -2200.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_0_11', 1305.9, -2200.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_0_12', 1741.2, -2200.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_0_13', 2176.5, -2200.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_0_14', 2611.8, -2200.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_0_15', 3047.1, -2200.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_0_16', 3482.4, -2200.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_1_0', 3500.0, -1850.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_1_1', 3500.0, -1550.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_2_0', -3500.0, -1800.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_3_0', -3500.0, -383.3, 360, 60, 620, 3000, False),
    ('Fill_Corridor_3_1', -3500.0, 50.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_3_2', -3500.0, 483.3, 360, 60, 620, 3000, False),
    ('Fill_Corridor_3_3', -3500.0, 916.7, 360, 60, 620, 3000, False),
    ('Fill_Corridor_3_4', -3500.0, 1350.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_3_5', -3500.0, 1783.3, 360, 60, 620, 3000, False),
    ('Fill_Corridor_4_0', -3481.2, 2200.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_4_1', -3043.8, 2200.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_4_2', -2606.2, 2200.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_4_3', -2168.8, 2200.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_4_4', -1731.2, 2200.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_4_5', -1293.8, 2200.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_4_6', -856.2, 2200.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_4_7', -418.8, 2200.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_4_8', 18.8, 2200.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_4_9', 456.2, 2200.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_4_10', 893.8, 2200.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_4_11', 1331.2, 2200.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_4_12', 1768.8, 2200.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_4_13', 2206.2, 2200.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_4_14', 2643.8, 2200.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_4_15', 3081.2, 2200.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_5_0', -1900.0, -1777.8, 360, 60, 620, 3000, False),
    ('Fill_Corridor_5_1', -1900.0, -1333.3, 360, 60, 620, 3000, False),
    ('Fill_Corridor_5_2', -1900.0, -888.9, 360, 60, 620, 3000, False),
    ('Fill_Corridor_5_3', -1900.0, -444.4, 360, 60, 620, 3000, False),
    ('Fill_Corridor_5_4', -1900.0, 0.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_5_5', -1900.0, 444.4, 360, 60, 620, 3000, False),
    ('Fill_Corridor_5_6', -1900.0, 888.9, 360, 60, 620, 3000, False),
    ('Fill_Corridor_5_7', -1900.0, 1333.3, 360, 60, 620, 3000, False),
    ('Fill_Corridor_5_8', -1900.0, 1777.8, 360, 60, 620, 3000, False),
    ('Fill_Corridor_6_0', 1500.0, -1777.8, 360, 60, 620, 3000, False),
    ('Fill_Corridor_6_1', 1500.0, -1333.3, 360, 60, 620, 3000, False),
    ('Fill_Corridor_6_2', 1500.0, -888.9, 360, 60, 620, 3000, False),
    ('Fill_Corridor_6_3', 1500.0, -444.4, 360, 60, 620, 3000, False),
    ('Fill_Corridor_6_4', 1500.0, 0.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_6_5', 1500.0, 444.4, 360, 60, 620, 3000, False),
    ('Fill_Corridor_6_6', 1500.0, 888.9, 360, 60, 620, 3000, False),
    ('Fill_Corridor_6_7', 1500.0, 1333.3, 360, 60, 620, 3000, False),
    ('Fill_Corridor_6_8', 1500.0, 1777.8, 360, 60, 620, 3000, False),
    ('Fill_Corridor_7_0', 3100.0, -785.7, 360, 60, 620, 3000, False),
    ('Fill_Corridor_7_1', 3100.0, -357.1, 360, 60, 620, 3000, False),
    ('Fill_Corridor_7_2', 3100.0, 71.4, 360, 60, 620, 3000, False),
    ('Fill_Corridor_7_3', 3100.0, 500.0, 360, 60, 620, 3000, False),
    ('Fill_Corridor_7_4', 3100.0, 928.6, 360, 60, 620, 3000, False),
    ('Fill_Corridor_7_5', 3100.0, 1357.1, 360, 60, 620, 3000, False),
    ('Fill_Corridor_7_6', 3100.0, 1785.7, 360, 60, 620, 3000, False),
    ('Pendant_Study2_0', 2250.0, -2895.0, 209.99999999999997, 45, 900, 2700, True),
    ('Pendant_Study3_1', -3650.0, -1100.0, 209.99999999999997, 45, 900, 2700, True),
    ('Pendant_Study1_2', -3205.0, 3040.0, 209.99999999999997, 45, 900, 2700, True),
    ('Pendant_Lounge1_3', -2700.0, -1660.0, 240.0, 45, 900, 2700, True),
    ('Pendant_Office_4', -700.0, -1660.0, 219.99999999999997, 45, 900, 2700, True),
    ('Pool_5', -200.0, 360.0, 50.0, 40, 700, 6500, False),
    ('Pool_6', -200.0, 640.0, 50.0, 40, 700, 6500, False),
    ('Pendant_CentralHall_7', -1000.0, 100.0, 500.0, 45, 900, 2700, True),
    ('Pendant_CentralHall_8', 600.0, 100.0, 500.0, 45, 900, 2700, True),
    ('Pendant_CentralHall_9', -1000.0, 900.0, 500.0, 45, 900, 2700, True),
    ('Pendant_CentralHall_10', 600.0, 900.0, 500.0, 45, 900, 2700, True),
    ('Pendant_CentralHall_11', -200.0, 500.0, 439.99999999999994, 45, 900, 2700, True),
    ('Pendant_DiningRoom_12', 2500.0, -1500.0, 254.99999999999997, 45, 900, 2700, True),
    ('Pendant_Lounge2_13', 2300.0, 340.0, 240.0, 45, 900, 2700, True),
    ('Pendant_Kitchen_14', 3730.0, -900.0, 219.99999999999997, 45, 900, 2700, True),
    ('Pendant_Entrance_15', -200.0, 2650.0, 240.0, 45, 900, 2700, True)
]
START = (-200.0, 3200.0)


# ---------------- 1. 임포트 ----------------
def find_mesh(dest, name):
    for p in eal.list_assets(dest, recursive=True, include_folder=False):
        a = eal.find_asset_data(p)
        if a.asset_class_path.asset_name == "StaticMesh" and str(a.asset_name).lower() in (name.lower(), name.lower().replace("sm_", "")):
            return unreal.load_asset(p)
    return None
def import_obj(path, dest, name):
    if not SKIP_IMPORT:
        t = unreal.AssetImportTask()
        t.filename = path; t.destination_path = dest
        t.automated = True; t.replace_existing = True; t.save = True
        tools.import_asset_tasks([t])
        for p in t.get_editor_property("imported_object_paths"):
            a = unreal.load_asset(p)
            if isinstance(a, unreal.StaticMesh): return a
    return find_mesh(dest, name)
def complex_collision(mesh, enable=True):
    try:
        bs = mesh.get_editor_property("body_setup")
        if bs: bs.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    except Exception as e: unreal.log_warning(f"[Mansion] collision: {e}")

meshes = {}
total = 2 + len(PROPS)
with unreal.ScopedSlowTask(total, "대저택 임포트 중...") as task:
    task.make_dialog(True)
    for name in ("SM_Mansion_Shell", "SM_Mansion_Ceiling"):
        task.enter_progress_frame(1, name)
        meshes[name] = import_obj(os.path.join(HERE, "Building", name + ".obj"), f"{ROOT}/Building", name)
    for key in PROPS:
        if task.should_cancel(): break
        task.enter_progress_frame(1, key)
        meshes[key] = import_obj(os.path.join(HERE, "Props", f"SM_{key}.obj"), f"{ROOT}/Props", f"SM_{key}")
missing = [k for k, v in meshes.items() if v is None]
if missing: unreal.log_warning(f"[Mansion] 메시를 찾지 못함: {missing}")
for k, m in meshes.items():
    if m: complex_collision(m)

# ---------------- 2. 배치 ----------------
for a in actor_sub.get_all_level_actors():
    if str(a.get_folder_path()).startswith("Mansion/"): actor_sub.destroy_actor(a)
# ---- OBJ 축 보정: 크기·방향을 아는 보정 메시를 임포트해 언리얼의 축 변환을 측정 ----
def _mm(A, B): return [[sum(A[i][k] * B[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
def _det(M): return (M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1]) - M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0])
                     + M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0]))
_t = unreal.AssetImportTask(); _t.filename = os.path.join(HERE, "Building", "SM_Mansion_Calibration.obj")
_t.destination_path = f"{ROOT}/Calibration"; _t.automated = True; _t.replace_existing = True; _t.save = False
tools.import_asset_tasks([_t])
_cal = None
for _p in _t.get_editor_property("imported_object_paths"):
    _a = unreal.load_asset(_p)
    if isinstance(_a, unreal.StaticMesh): _cal = _a
CORR_R, CORR_S = [[1, 0, 0], [0, 1, 0], [0, 0, 1]], (1.0, 1.0, 1.0)
if _cal:
    _b = _cal.get_bounding_box(); mn = [_b.min.x, _b.min.y, _b.min.z]; mx = [_b.max.x, _b.max.y, _b.max.z]
    ext = [mx[i] - mn[i] for i in range(3)]; k = max(ext) / 300.0
    A = [[0, 0, 0] for _ in range(3)]; used = set()
    for i in range(3):
        e = ext[i] / k; j = 0 if abs(e - 100) < 20 else (1 if abs(e - 300) < 20 else 2)
        A[i][j] = 1 if mx[i] > -mn[i] else -1; used.add(j)
    if len(used) == 3:
        AP = _mm(A, [[1, 0, 0], [0, 0, 1], [0, 1, 0]])
        R = [[AP[j][i] for j in range(3)] for i in range(3)]
        S = [1.0, 1.0, 1.0]
        if _det(R) < 0: S = [1.0, -1.0, 1.0]; R = _mm(R, [[1, 0, 0], [0, -1, 0], [0, 0, 1]])
        CORR_R, CORR_S = R, (S[0] / k, S[1] / k, S[2] / k)
        if all(abs(mn[i]) > 5 and abs(mx[i]) > 5 for i in range(3)):
            unreal.log_warning("[Mansion] 임포터가 피벗을 메시 중심으로 옮긴 것 같습니다. 가구 높이가 어긋나면 알려주세요.")
        unreal.log(f"[Mansion] OBJ 축 보정 측정: 임포트 행렬={A}, 배율={k:.3f}")
    else:
        unreal.log_warning(f"[Mansion] 축 보정 측정 실패 (bounds={mn},{mx}) - 기본값으로 배치")
    eal.delete_directory(f"{ROOT}/Calibration")
def rot_for(yaw):
    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    T = _mm([[c, -s, 0], [s, c, 0], [0, 0, 1]], CORR_R)
    return unreal.MathLibrary.make_rot_from_xz(unreal.Vector(T[0][0], T[1][0], T[2][0]), unreal.Vector(T[0][2], T[1][2], T[2][2]))
def spawn(mesh, x, y, z, yaw, folder, label):
    a = actor_sub.spawn_actor_from_object(mesh, unreal.Vector(x, y, z), rot_for(yaw))
    a.set_actor_scale3d(unreal.Vector(*CORR_S))
    a.set_folder_path(folder); a.set_actor_label(label); return a
with unreal.ScopedEditorTransaction("Build Mansion"):
    if meshes.get("SM_Mansion_Shell"): spawn(meshes["SM_Mansion_Shell"], 0, 0, 0, 0, "Mansion/Building", "Mansion_Shell")
    if meshes.get("SM_Mansion_Ceiling"):
        c = spawn(meshes["SM_Mansion_Ceiling"], 0, 0, 0, 0, "Mansion/Building", "Mansion_Ceiling")
        c.set_actor_hidden_in_game(True)      # 쿼터뷰: 게임에서는 천장 숨김 (에디터에서 끄려면 눈 아이콘)
    count = {}
    for key, room, x, y, yaw, kind in PLACEMENTS:
        m = meshes.get(key)
        if not m: continue
        count[key] = count.get(key, 0) + 1
        a = spawn(m, x, y, 0, yaw, f"Mansion/Props/{room}", f"{key}_{count[key]:02d}")
        if kind in ("rug", "overhead"):
            for comp in a.get_components_by_class(unreal.StaticMeshComponent):
                comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
unreal.log(f"[Mansion] 프롭 {len(PLACEMENTS)}개 배치")

# ---------------- 3. 물 / 발광 재질 ----------------
FX = f"{ROOT}/FX"
def import_tex(fname, normal=False):
    t = unreal.AssetImportTask(); t.filename = os.path.join(HERE, "FX", fname); t.destination_path = FX
    t.automated = True; t.replace_existing = True; t.save = True; tools.import_asset_tasks([t])
    tex = unreal.load_asset(f"{FX}/{os.path.splitext(fname)[0]}")
    if normal and tex:
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("srgb", False); eal.save_loaded_asset(tex)
    return tex
T_NRM = import_tex("T_FX_WaterNormal.png", True); T_STK = import_tex("T_FX_WaterStreak.png")
def new_mat(name):
    path = f"{FX}/{name}"
    if eal.does_asset_exist(path): eal.delete_asset(path)
    return tools.create_asset(name, FX, unreal.Material, unreal.MaterialFactoryNew())
def E(m, cls, x, y, **props):
    e = mel.create_material_expression(m, cls, x, y)
    for k, v in props.items(): e.set_editor_property(k, v)
    return e
def link(a, ao, b, bi): mel.connect_material_expressions(a, ao, b, bi)
def out(e, o, prop): mel.connect_material_property(e, o, prop)
def c3(m, x, y, r, g, b): return E(m, unreal.MaterialExpressionConstant3Vector, x, y, constant=unreal.LinearColor(r, g, b, 1))
def c1(m, x, y, v): return E(m, unreal.MaterialExpressionConstant, x, y, r=v)
def translucent(m):
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT); m.set_editor_property("two_sided", True)
    try: m.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    except Exception: pass
pool = new_mat("M_FX_WaterPool"); translucent(pool)
tc = E(pool, unreal.MaterialExpressionTextureCoordinate, -900, 0)
pa = E(pool, unreal.MaterialExpressionPanner, -700, -120, speed_x=0.035, speed_y=0.02)
pb = E(pool, unreal.MaterialExpressionPanner, -700, 120, speed_x=-0.025, speed_y=0.03)
link(tc, "", pa, "Coordinate"); link(tc, "", pb, "Coordinate")
na = E(pool, unreal.MaterialExpressionTextureSample, -450, -120, texture=T_NRM, sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
nb = E(pool, unreal.MaterialExpressionTextureSample, -450, 120, texture=T_NRM, sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
link(pa, "", na, "UVs"); link(pb, "", nb, "UVs")
ad = E(pool, unreal.MaterialExpressionAdd, -200, 0); link(na, "RGB", ad, "A"); link(nb, "RGB", ad, "B")
nr = E(pool, unreal.MaterialExpressionNormalize, -50, 0); link(ad, "", nr, "VectorInput")
out(nr, "", unreal.MaterialProperty.MP_NORMAL); out(c3(pool, -200, -300, .02, .11, .14), "", unreal.MaterialProperty.MP_BASE_COLOR)
out(c1(pool, -200, 250, .03), "", unreal.MaterialProperty.MP_ROUGHNESS); out(c1(pool, -200, 330, 1.0), "", unreal.MaterialProperty.MP_SPECULAR)
out(c1(pool, -200, 410, .62), "", unreal.MaterialProperty.MP_OPACITY)
mel.recompile_material(pool); eal.save_loaded_asset(pool)
def flow_mat(name, speed):
    m = new_mat(name); translucent(m)
    tc = E(m, unreal.MaterialExpressionTextureCoordinate, -1100, 0); tm = E(m, unreal.MaterialExpressionTime, -1100, 150)
    sp = E(m, unreal.MaterialExpressionScalarParameter, -1100, 250, parameter_name="FlowSpeed", default_value=speed)
    mu = E(m, unreal.MaterialExpressionMultiply, -900, 200); link(tm, "", mu, "A"); link(sp, "", mu, "B")
    ap = E(m, unreal.MaterialExpressionAppendVector, -750, 120); link(c1(m, -900, 80, 0.0), "", ap, "A"); link(mu, "", ap, "B")
    uv = E(m, unreal.MaterialExpressionAdd, -600, 0); link(tc, "", uv, "A"); link(ap, "", uv, "B")
    tx = E(m, unreal.MaterialExpressionTextureSample, -400, 0, texture=T_STK); link(uv, "", tx, "UVs")
    col = E(m, unreal.MaterialExpressionLinearInterpolate, -150, -150)
    link(c3(m, -400, -300, .05, .2, .25), "", col, "A"); link(c3(m, -400, -200, .85, .95, 1.0), "", col, "B"); link(tx, "R", col, "Alpha")
    out(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
    op = E(m, unreal.MaterialExpressionLinearInterpolate, -150, 150)
    link(c1(m, -400, 200, .3), "", op, "A"); link(c1(m, -400, 280, .9), "", op, "B"); link(tx, "R", op, "Alpha")
    out(op, "", unreal.MaterialProperty.MP_OPACITY)
    em = E(m, unreal.MaterialExpressionMultiply, -150, 320); link(tx, "R", em, "A"); link(c1(m, -400, 360, .4), "", em, "B")
    out(em, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR); out(c1(m, -150, 450, .05), "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(m); eal.save_loaded_asset(m); return m
fall = flow_mat("M_FX_WaterFall", -0.9); jet = flow_mat("M_FX_WaterJet", 1.6)
glow = new_mat("M_FX_Glow"); glow.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
gi = E(glow, unreal.MaterialExpressionScalarParameter, -500, 100, parameter_name="Intensity", default_value=8.0)
gm = E(glow, unreal.MaterialExpressionMultiply, -250, 0); link(c3(glow, -500, -50, 1.0, .85, .66), "", gm, "A"); link(gi, "", gm, "B")
out(gm, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR); mel.recompile_material(glow); eal.save_loaded_asset(glow)
SWAP = [("Water_Fall", fall), ("Water_Jet", jet), ("Water", pool), ("Glow", glow)]
for a in actor_sub.get_all_level_actors():
    if not str(a.get_folder_path()).startswith("Mansion/"): continue
    for comp in a.get_components_by_class(unreal.StaticMeshComponent):
        for slot in comp.get_material_slot_names():
            for key, mat in SWAP:
                if key in str(slot): comp.set_material(comp.get_material_index(slot), mat); break

# ---------------- 4. 조명 ----------------
with unreal.ScopedEditorTransaction("Mansion Lights"):
    for name, x, y, z, cd, radius, temp, shadows in LIGHTS:
        a = actor_sub.spawn_actor_from_class(unreal.PointLight, unreal.Vector(x, y, z))
        a.set_actor_label(name); a.set_folder_path("Mansion/Lights")
        c = a.get_component_by_class(unreal.PointLightComponent)
        c.set_editor_property("intensity_units", unreal.LightUnits.CANDELAS); c.set_intensity(cd)
        c.set_attenuation_radius(radius); c.set_editor_property("source_radius", 20.0)
        c.set_editor_property("use_temperature", True); c.set_editor_property("temperature", float(temp))
        c.set_cast_shadows(bool(shadows))
    # 하늘 + 태양 (통유리로 들어오는 자연광)
    try:
        sky = actor_sub.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0)); sky.set_folder_path("Mansion/Environment")
    except Exception: pass
    sun = actor_sub.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 3000), unreal.Rotator(roll=0.0, pitch=-48.0, yaw=35.0))
    sun.set_actor_label("Sun"); sun.set_folder_path("Mansion/Environment")
    sc = sun.get_component_by_class(unreal.DirectionalLightComponent); sc.set_intensity(6.0)
    try: sc.set_editor_property("atmosphere_sun_light", True)
    except Exception: pass
    sl = actor_sub.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 1500)); sl.set_actor_label("SkyLight"); sl.set_folder_path("Mansion/Environment")
    slc = sl.get_component_by_class(unreal.SkyLightComponent)
    try: slc.set_editor_property("real_time_capture", True)
    except Exception: pass
    slc.set_intensity(1.5)
    # 노출: 실내가 어둡게 보이지 않도록 보정
    pp = actor_sub.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0, 0, 0)); pp.set_actor_label("Mansion_PostProcess"); pp.set_folder_path("Mansion/Environment")
    pp.set_editor_property("unbound", True)
    s = pp.get_editor_property("settings")
    for k, v in (("override_auto_exposure_bias", True), ("auto_exposure_bias", 1.0),
                 ("override_auto_exposure_min_brightness", True), ("auto_exposure_min_brightness", -2.0),
                 ("override_auto_exposure_max_brightness", True), ("auto_exposure_max_brightness", 8.0)):
        try: s.set_editor_property(k, v)
        except Exception as e: unreal.log_warning(f"[Mansion] post process {k}: {e}")
    pp.set_editor_property("settings", s)
    ps = actor_sub.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(START[0], START[1], 100.0))
    ps.set_actor_label("PlayerStart_Entrance"); ps.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=0.0, yaw=-90.0), False)
    ps.set_folder_path("Mansion/Gameplay")
unreal.log(f"[Mansion] 완료: 프롭 {len(PLACEMENTS)}개, 조명 {len(LIGHTS)}개")
