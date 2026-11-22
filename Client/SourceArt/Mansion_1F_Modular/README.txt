대저택 1층 (컨템포러리 호텔) — 모듈식 패키지

[폴더]
  Building/   SM_Mansion_Shell.obj   바닥·벽·기둥·통유리 (메시 1개)
              SM_Mansion_Ceiling.obj 천장·천창 (메시 1개, 게임에서는 자동 숨김)
  Props/      SM_*.obj               가구·식물·조명기구 78종 (피벗 = 바닥 중앙)
  FX/         흐르는 물 텍스처
  Mansion_1F_Build.py                언리얼 원클릭 빌드 스크립트

[사용법]
  1. 이 폴더 구조 그대로 압축을 풉니다.
  2. 언리얼: Edit > Plugins > Python Editor Script Plugin 활성화 후 재시작
  3. 새 빈 레벨(Empty Level)을 엽니다.
  4. Tools > Execute Python Script... > Mansion_1F_Build.py 선택
     → 임포트(몇 분 소요) → 건물 배치 → 가구 365개 개별 배치 → 물/조명 설정까지 자동

[수정]
  - 가구·식물은 아웃라이너 Mansion/Props/<방 이름> 폴더에 하나씩 들어 있어 개별 삭제/이동 가능
  - 같은 종류는 같은 메시를 공유 (메시 하나를 고치면 전부 반영)
  - 스크립트를 다시 실행하면 Mansion/ 폴더 액터를 지우고 처음 상태로 재배치합니다
    (재임포트 없이 배치만 다시: 스크립트 상단 SKIP_IMPORT = True)
