# 소비 아이템 구상안 v1 (2026-11-17, Claude 시안 · 사용자 "그대로 진행")

게임 안 소비 아이템 4종의 바닥 모델·HUD 아이콘·정보창 아이콘 기준 자료다. 무기 구상안 v2.2와 같은 재질 톤.

## 파일
- `consumable_concepts_v1.html`: 그림 원본(코드로 그린 SVG). 브라우저로 열면 전체 시트. `?mode=icon&c=1`(아이콘), `?mode=top&c=1`(탑뷰).
- `consumables_v1_sheet.png`: 전체 시트 1600×1000.

## 디자인 규칙
- **실루엣 = 종류**(탑뷰 1080p에서 50px 남짓이라 윤곽으로 구분), **발광 띠 색 = 종류**(무기 분류 4색 레드·시안·앰버·라임과 겹치지 않게).
- 공통 재질: 무광 블랙 · 브러시드 스틸 · 황동. 각인 `VH-C1` ~ `VH-C4`.

| 번호 | 아이템 | 서버 `ConsumableId` | 모양 | 띠 색 |
|---|---|---|---|---|
| VH-C1 | 회복 키트 | HealKit (1) | 네모 상자 + 민트 십자 | 민트 `#2BF5B0` |
| VH-C2 | 연막탄 | Smoke (2) | 세운 깡통 + 옆 레버·고리 | 라벤더 `#B07CFF` |
| VH-C3 | 섬광탄 | Flashbang (3) | 눕힌 원통 + 띠 2줄 | 플래시 옐로 `#FFE45C` |
| VH-C4 | 제트팩 | Jetpack (4) | 쌍둥이 탱크 | 코발트 `#4C7DFF` |

게임 에셋은 `Tools/WeaponBuild/render_consumables.ps1` → `build_consumables.py`로 만든다(`Docs/캐릭터_교체_가이드.md` 8-3절).
