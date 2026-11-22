털어라! 금고! — 서버 전달 파일 (2026-10-28, 클라 client/menu-ui 브랜치)

1) mansion_1f_server.json  (맵, 우리 배치 v2 + 문 밖 바닥 floor_exit 포함)
   → 서버 Data/Maps/mansion_1f_server.json 에 덮어쓰기.
     이미 빌드했다면 Binaries/x64/Release/Data/Maps/mansion_1f_server.json 에도 덮어쓰거나 다시 빌드(빌드 때 복사됨).
   → 서버 재시작 후 로그 확인:
     [Map] mansion_1f v2: 금고 후보 10, 숨는 곳 4, 방 33, 순찰 지점 47, 충돌체 115, 격자 205x139

2) treasure_types.json  (보물 10종, 서버 TreasureType 필드 그대로: id 1~10, name, weightKg, value)
   * 지금 서버는 이 JSON을 읽지 않고 Content/GameData.h 의 TreasureTypes 배열을 쓴다.
   → GameData_TreasureTypes.txt 의 배열로 GameData.h 의 TreasureTypes 를 바꾸고 다시 빌드.
     (JSON은 나중에 파일에서 읽게 바꿀 때 쓰는 원본. 값은 둘 다 같음)
   * 값은 클라 데이터 에셋(DA_VHGameplayConfig.SafeTreasures) 기준 [임시값, 기획 확정 시 바뀔 수 있음].
     가장 싼 5개 합 11,000$ >= 목표 10,000$.

3) 클라 PC에서 시험한 결과 (정호님 서버 최신본의 사본에서 시험했고, 서버 저장소는 수정하지 않았습니다)
   - 1)+2) 적용 → 빌드·실행 OK, 맵 v2 로그 정상
   - 실제 게임: 로그인 → 방 → 3명(봇 2) → 금고 → 줍기(값 = 클라 값) → 반납 11,600$ → 탈출구 열림 → 탈출 → 결과 → 로비,
     포획·감금, 창고 구출, 공격 → 경비원 경직, 숨기 모두 OK
   - 아직: 경비원 때려서 떨어진 보물 다시 줍기 (사람 2명이 필요해서 내일 함께 확인 부탁드립니다)

4) ContentTests: 옛 맵·옛 보물 값을 숫자로 고정해 둔 시험이 실패함 (게임 동작 문제 아님)
   · 맵 v2 때문: Map_Floors_OutsideBuildingIsBlocked(바닥 5→6), World_Setup_CreatesAllEntities(25→21),
     Play_AllLoaded_StartsGame_EachSeesOwnThief(26→22)  <- 숨는 곳 8→4, floor_exit 추가
   · 보물 값 때문: Rules_Exit_OpensAutomaticallyAtValue_ThenEscapeZone, Rules_Results_ScoreByDeliveredValue
     <- "1~4번 반납 = 10,000$ 넘음" 전제(옛 값 11,800$, 새 값 8,600$). 1~5번(11,000$)으로 바꾸면 됨
   · Rules_Treasure_CarryLimit_CountAndWeight 는 통과하지만 주석의 "3번 = 0.5kg" 전제는 이제 5kg

5) 부탁드리는 것 (가능하시면)
   · 경비원 시작 2번 (2343.5, -3876) 이 문 없는 방(L2) 안 → 그 방에서 잡히면 창고로 못 데려가서 판 끝까지 묶임.
     시작 위치를 방 밖으로 옮기거나 문 추가.
   · 숨기 완료 바로 다음 틱에 온 InteractCancel 을 "나오기"로 처리함. 클라는 고쳤지만 서버도 숨은 직후 짧은 Cancel 은 무시하면 더 안전.

6) 빌드 후 에디터·프로그램이 "잘못된 이미지 ... 0xc0e90002" 로 안 열리면 Windows 스마트 앱 컨트롤이 새로 빌드한 DLL/EXE를 막은 것.
