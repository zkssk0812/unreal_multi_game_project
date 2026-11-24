# 졸업작품 「털어라! 금고!」 프로젝트 안내 (Claude Code용)

## 프로젝트 개요
- 팀 졸업작품 게임 **「털어라! 금고!」**(코드명 VaultHeist). 이 폴더(`C:\UnrealProjects\MCPTest`)가 **실제 졸업작품 클라이언트 프로젝트**다. (프로젝트 이름 변경은 큰 구조 변경이므로 사용자 확인 전에는 하지 않는다.)
- 장르: **경쟁 PvPvE(최대 3인)**. 플레이어는 **모두 도둑(의적)** 이고 서로 경쟁한다. 경비원(로봇)은 서버 AI다. 5분 안에 **반납 금액이 가장 많은 사람이 승리**. (2026-11-06 PvE 협동 → PvP 전환, 2026-11-13부터 **게임 규칙은 서버 v3.0 기준**)
- 엔진: **Unreal Engine 5.8.3**, **C++ 프로젝트** (Third Person 템플릿 기반)
- 팀 구성: 사용자 = **클라이언트**, 별도 팀원 = **서버**(C++ IOCP 서버). 서버와는 아래 "서버 프로토콜"로 통신한다.
- 규칙·기획 자료(우선순위 순): ① 서버 규칙 `Docs/ServerRef/Project_Rob-master/docs/psd.md`·`Content/GameData.h`·`Shared/Protocol/protocol.h` ② 최신 기획 `Docs/Design/기획_2026-11/`(상호작용·아이템 명세, 조작키, UI 수정본, 팀 Q&A) ③ 진행 상황·결정 기록 `Docs/작업보고_YYYY-MM-DD.md`. 세계관: 동물 의인화 캐주얼(주토피아풍), 악당 카피바라 사업가 '바바라'의 저택.
- **`Docs/Archive_PvE/`는 옛 PvE 규칙 문서라 참고하지 않는다.** `Docs/PvP_규칙_v1.md`는 11-06 결정 기록이며 서버 규칙과 다르면 서버가 우선이다.
- 기획과 서버가 다르면 **일단 서버 기준으로 구현**하고, 차이는 작업보고와 `Docs/ServerHandoff`에 적는다. 임의로 규칙을 확정하지 말고 사용자에게 묻거나 "가정"으로 기록한다.

## 사용자와 역할 분담
- 사용자는 게임공학과 학생, 언리얼/C++ 초보자다. 게임 방향·요구사항을 정하고 결과를 확인한다.
- Claude는 개발 파트너로서 기획 → 구현 → 오류 해결 → 테스트 → 시연용 빌드까지 **직접 실행하고 검증**한다. 설명으로 끝내지 말고 실제로 구현하고 확인한다.
- 사용자가 직접 해야 하는 작업은 **클릭 위치와 입력할 명령까지** 따라 할 수 있게 설명한다.

## 개발 환경
- Windows 11 / 엔진 `C:\Users\FORYOUCOM\Desktop\UE_5.8` / Visual Studio 2026
- **언리얼 에디터·Visual Studio 모두 한국어 UI**. 안내는 한국어 메뉴 이름으로 한다. (편집 → 플러그인, 편집 → 에디터 개인설정, 창 → 출력 로그, 툴 → Visual Studio 프로젝트 새로고침)
- 에디터는 **에픽게임즈 런처 → 라이브러리 → 내 프로젝트**에서 연다. (.uproject 더블클릭 연결이 안 되어 있음)

## Unreal MCP 연결
- 엔진 내장 **Unreal MCP(ModelContextProtocol)** + **All Toolsets**. 서버 `http://127.0.0.1:8000/mcp`, 이름 `unreal-mcp`
- `Config/DefaultEditorPerProjectUserSettings.ini`의 `bAutoStartServer=True`로 자동 시작
- **에디터가 켜져 있어야** MCP 도구를 쓸 수 있다. 연결 실패 시 출력 로그의 `LogModelContextProtocol` 줄부터 확인한다.

## 빌드
```
"C:\Users\FORYOUCOM\Desktop\UE_5.8\Engine\Build\BatchFiles\Build.bat" MCPTestEditor Win64 Development -Project="C:\UnrealProjects\MCPTest\MCPTest.uproject" -WaitMutex
```
- 에디터가 켜져 있으면 Live Coding 때문에 명령줄 빌드가 막힐 수 있다. `.cpp`만 바꿨으면 Live Coding, 헤더·UCLASS·UPROPERTY 구조를 바꿨으면 에디터를 닫고 빌드 후 다시 열어야 한다. **에디터를 닫거나 다시 열어야 할 때는 먼저 사용자에게 알리고**, 다시 연 뒤 MCP 재연결을 확인한다.

## 작업 원칙
1. 시작 시 프로젝트 상태, MCP 연결, 사용 가능한 도구부터 확인한다. 접근 못 하면 연결된 것처럼 말하지 않는다.
2. 작은 플레이 가능한 단위부터 만들고, 기능 하나마다 실행 결과를 확인한다.
3. 기존 작업 보존. **Git이 없으면 작업 전에 Git + Git LFS를 세팅하고 첫 커밋.** 기능 하나가 끝날 때마다 커밋. `git reset --hard`, 강제 삭제 금지.
   - **올리는 방법(2026-11-22부터)**: 작업 커밋(MCPTest 로컬) → `powershell -ExecutionPolicy Bypass -File Tools\push_to_team_repo.ps1` 실행으로 **팀 통합 저장소** `zkssk0812/unreal_multi_game_project`의 `main`, `Client/` 폴더에 push. 작업은 계속 `C:\UnrealProjects\MCPTest`에서 하고, 팀 저장소 clone은 `C:\UnrealProjects\unreal_multi_game_project`.
   - **원래 저장소(`LeeJuneWu/project_Unreal_Engine5.8`, `client/menu-ui`)에는 push 하지 않는다**(MCPTest 의 `git push`는 막아 둠, 올라가 있는 것은 그대로 둔다). MCPTest 커밋은 기록용.
   - 스크립트가 하는 일: 팀 저장소 `main` pull → MCPTest의 Git 추적 파일 + 새 파일(.gitignore 제외)을 `Client/`로 복사(지운 파일은 `Client/` 안에서만 반영) → `Client/`만 커밋(메시지 = MCPTest 마지막 커밋 앞에 `[client] `) → push. 빼는 것: Binaries·Intermediate·Saved·DerivedDataCache·Build·.vs·*.sln(x)·`Config/DefaultEditor.ini`·`SourceArt/**/*.zip`·`Docs/ServerRef/`·MCPTest의 .gitignore/.gitattributes. 100MB 넘는 파일이나 LFS로 안 잡힌 에셋이 있으면 멈춘다. MCPTest에 커밋 안 한 변경이 있으면 멈춘다(`-DryRun`, `-NoPush`, `-AllowDirty`).
   - 팀 저장소의 `Server/`·`Common/`·`Docs/`와 공용 `.gitignore`·`.gitattributes`·`README.md`는 건드리지 않는다. PR은 만들지 않는다. `Config/DefaultEditor.ini`는 올리지 않는다.
4. **`.uasset`, `.umap`은 절대 텍스트로 수정하지 않는다.** 에셋은 MCP 도구나 에디터 기능으로만 다룬다.
5. **삭제, 대규모 구조 변경, 유료 서비스는 사용자 확인 후.** 합의된 범위의 구현·오류 수정은 바로 진행한다.
6. 핵심 로직은 **C++**, 조립·수치 조정은 C++을 상속한 **블루프린트**에서 하도록 설계한다.
7. **필요하면 사용자에게 물어보면서 진행한다.** 기획 방향, 화면 구성, 우선순위처럼 사용자가 결정할 일은 짧게 선택지를 제시해 묻는다. 사소한 구현 세부사항은 합리적으로 정해 진행하고 보고서 "확인 필요"에 기록한다. 한 번에 질문을 몰아서 하기보다 작업 흐름에 맞춰 필요한 것만 묻는다.
8. **항상 한국어로 답변한다.** 사용자에게 하는 모든 답변·질문·진행 안내·보고서는 한국어로 쓴다. (코드 식별자, 명령어, 로그 원문은 그대로 둔다)

## 서버 프로토콜 v3.0 (서버 권위)
- 참고 원본: `Docs/ServerRef/Project_Rob-master/` (서버 팀원 저장소 사본, 브랜치 `feature/pvpve-v3`, **참고 전용·Git 제외**. 읽기만 하고 수정하지 않는다)
- 핵심 정의: `Shared/Protocol/protocol.h` (**v3.0**, v2.1과 호환 안 됨). 수치는 `Content/GameData.h`, 설계·가정(A-01~A-25)은 `docs/psd.md`.
- 클라이언트에서는 이 파일을 **내용 그대로** `Source/MCPTest/Network/Protocol/protocol.h`로 복사해 사용한다. 고치지 않는다. 다른 파일은 래퍼 `Network/VHProtocol.h`(`namespace RobProto`)만 include한다.
- 규칙 요약
  - TCP, 서버 포트 **9000**, 패킷 = `[size:uint16][type:uint16][body]`(리틀 엔디언, size는 헤더 포함 전체). `#pragma pack(1)` 고정 크기 구조체를 그대로 memcpy.
  - size < 4 또는 > 8192, 모르는 패킷 ID, 크기 불일치, NaN 좌표, 단계 위반이면 서버가 끊는다. 가변 길이 패킷은 `*_Size(count)`로 크기 계산·검증.
  - 문자열은 UTF-8, 널 종료, 고정 길이 배열(이름 48바이트·12자, 방 이름 60바이트·20자).
  - 접속 직후 `CS_LOGIN`에 `PROTOCOL_VERSION`(3.0)을 정확히 넣는다. 다르면 VersionMismatch 후 끊김.
  - **모든 단계에서 2초마다 `CS_PING`** (10초 응답 없으면 서버가 끊음).
  - 단계별 허용 패킷(Anonymous → Lobby → Room → Game)만 보낸다. 로비·방 버튼 연타 방지는 유지한다(서버 강제 제한은 지금 없음).
  - 방 안의 변경 요청(캐릭터 1~3·퍽 1~7 최대 3개·준비)은 성공 = 방 전원 `SC_ROOM_STATE`, 실패 = 요청자에게 `SC_ROOM_ERROR`. 캐릭터를 골라야 준비 가능, 전원 준비 시 자동 시작(1명도 시작됨). 스킬은 캐릭터 고정(도적1 테이저·2 끈끈이·3 은신. 2026-11-22 서버 캐릭터 5 → 3).
  - `SC_KICK`, `SC_NOTICE`는 어느 화면에서든 팝업/토스트로 보여준다.
- 게임 단계 (서버가 전부 판정, 클라는 입력 전송 + 결과 표시)
  - **이동은 클라가 한다.** `CS_MOVE`(최대 20Hz, 멈춰도 1초에 한 번)로 발 위치·`yaw`(커서 방향 = 공격·스킬 방향)·달리기 플래그를 보내면 서버가 그대로 쓴다(검증·보정 없음).
  - `CS_ACTION`: AttackStart/AttackStop(꾹 누르기), Dodge, InteractBegin/Cancel, SwitchWeapon, UseSkill, UseConsumable. `aim` = 커서가 가리키는 바닥 지점(투척 조준). 거절은 `SC_ACTION_REJECT`.
  - 서버가 보내는 것: `SC_SNAPSHOT`(20Hz: 플레이어·경비원 매 틱, 오브젝트는 바뀐 틱만), `SC_ENTITY_SPAWN/DESPAWN`, 이벤트 4001~4015(`SC_FIRE`, `SC_DAMAGE`, `SC_DOWNED`, `SC_RESPAWNED`, `SC_MONEY`, `SC_PLAYER_STATUS`(본인만), `SC_PICKUP`, `SC_TREASURE_TAKEN` 등).
  - **클라가 스스로 적용해야 하는 것**: 경직 시 정지, 끈끈이 −50%·은신 −80%·운동화 +20% 속도, 레일건 잠금, 제트팩 이동, `SC_KNOCKBACK` 밀려남, 구르기 이동, `SC_RESPAWNED` 위치로 이동, 은신·연막 안 대상과 퍽 없는 미니맵 정보 숨기기.
  - 판정에 쓰는 수치는 서버 값을 따른다. 클라 설정값(DA)에는 표시용·클라 적용 값만 둔다.
  - 게임 끝: 5분 → `SC_GAME_END`(순위·반납 금액·최고 기록) → 15초 뒤 또는 `CS_RETURN`으로 **로비**(대기실 복귀 없음).
- 네트워크 구조
  - GameInstance 서브시스템(`UVHNetSubsystem`)이 연결·단계·로그인·방 상태를 소유, 받은 SC_ 패킷을 게임 스레드에서 처리해 델리게이트로 알림. 게임 단계 패킷은 `UVHServerGameSubsystem`이 화면에 반영.
  - 전송 계층 2종: **TCP**(실서버) / **Mock**(인프로세스 가짜 서버). 설정 `bUseMockServer`, `ServerHost`, `ServerPort`.
  - **Mock은 최소 기능만**(2026-11-13 결정): 메뉴·로비·방 → 샘플 엔티티로 게임 시작 → 샘플 결과. 규칙 판정은 하지 않는다. **규칙 시험은 로컬 실서버**로 한다.
  - 실서버 전환: 메인화면 주소칸 + [실서버로 전환] / [가짜 서버로], 콘솔 `vh.UseServer [host[:port]]` / `vh.UseMock`, 실행 옵션 `-VHServer[=host:port]` / `-VHMock`, 개발용 봇 `-VHBot`.
- **게임 맵(2026-11-15부터)**: `L_Mansion_Shell`(모델러 패키지 `SourceArt/Mansion_1F_Modular`로 만든 새 맵, Shell 원점 좌표 X ±5000·Y ±4500). 서버 커밋 `1b53272`의 맵(`Mansion_1F_Collision.json` 기준, 방 35개)과 같다. **맵 버전 10**(2026-11-22부터, 팀 저장소 `Server/` 기준. 같은 맵의 예전 V3b 사본은 버전 2 → `-VHMapVersion=2`). 설정은 `Config/DefaultGame.ini`의 `GameLevel`·`GameLevelMapVersion`. 옛 블록아웃 레벨 `L_Mansion_1F`(맵 버전 3)는 지우지 않고 둔다(`-VHLevel=… -VHMapVersion=3`으로 열 수 있다).
  - 레벨을 다시 만들거나 고칠 때: `Tools/MansionBuild/`의 `build_shell_level.py`(패키지 스크립트 실행) → `setup_shell_level.py base walls lights rooms`(게임 모드·카메라 설정·닫힌 바깥 문·벽 투명·채움광 반사 끔·방 영역 35개). 에디터 아래쪽 Cmd 칸에 `py "경로" 인자`로 실행한다. 패키지 원본 스크립트는 고치지 않는다.
  - 금고·상자·반납지점·시작 위치는 레벨에 놓지 않는다. 서버가 보낸 위치에 만든다(금고는 `SpawnServerSafe`).
  - 사용자 결정(2026-11-16): 탑뷰 **화면 위 = +X**(`TopViewYaw = 0`, 조작키 문서의 W = +X와 맞춤), 방 이름은 `Tools/MansionBuild/room_names.json`(객실·창고·서재·빈 방·닫힌 방 등), 펜던트 조명 14개는 **기구 모양만 게임에서 숨기고 빛은 유지**, 조명은 패키지의 낮 조명 그대로(채움광 277개 유지, 바닥 반사만 끔).
- **시험용 서버(2026-11-22부터) = 팀 저장소 서버** `C:\UnrealProjects\unreal_multi_game_project\Server`(정호님 최신 코드). **코드는 절대 고치지 않고 빌드만** 한다: 그 폴더에서 `"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Project_Rob.sln -p:Configuration=Release -p:Platform=x64 -m`(툴셋은 프로젝트의 `$(DefaultPlatformToolset)`, 지정 불필요). 결과물 `Server\Binaries\x64\Release\GameServer.exe`(+ `Data/Maps` 자동 복사, 공용 .gitignore로 무시됨). 팀 저장소를 pull 하면 다시 빌드한다. 서버 시험 `ContentTests.exe`(2026-11-22: 168개 통과).
  - 켜기: 바탕화면 **`VaultHeist_팀서버켜기.bat`**(맵 v10 확인 후 실행, 정상 시작 로그 `protocol 0x00030000`, `mansion_1f v10: 방 35(금고·아이템을 놓는 방 31)`, `Listener: 포트 9000`). 자동 시험용(입력 닫힘 방지 파이프·로그 파일)은 `Saved/V3Test/team_server.bat` → `Saved/V3Test/team_server.log`.
  - 클라 `protocol.h` 사본 원본은 이제 팀 저장소 **`Common/Protocol/protocol.h`**(서버 `Server/Shared/Protocol/protocol.h`에도 같은 내용). 바뀌면 그대로 복사한다.
  - 예비: 예전 사본 `C:\UnrealProjects\VaultHeistServerV3b\Project_Rob`(커밋 `1b53272`, 맵 버전 2, 바탕화면 `VaultHeist_서버켜기.bat`)은 지우지 않고 둔다 — 레일건 충전·서버 테이저·반납 경고 55초가 **없는 옛 서버**라 지금 클라와 동작이 다르다. 기존 `VaultHeistServerV3`(옛 맵용)·`VaultHeistServerTest`·`VaultHeistServerLatest`도 그대로 둔다.
  - 옛 레벨 `L_Mansion_1F`로 시험할 때만: `VaultHeistServerV3` 사본 + 실행 파일 옆 `Data/Maps/mansion_1f_server.json`을 `Docs/ServerHandoff/mansion_1f_server.json`으로 교체.
  - `server_config.json`이 없으면 DB 없이 실행된다(최고 기록 저장 없음). 서버 콘솔 입력이 닫히면(빈 줄·EOF) 서버가 종료된다.
- 서버에 묻거나 요청할 것, 기획과 서버의 차이는 `Docs/ServerHandoff/`에 정리한다(정호님께 보내는 글은 존댓말, 호칭 "정호님").

## 화면 흐름과 조작 (2026-11-13 기준)
화면 흐름: **메인화면 → 로그인 → 로비(방 목록) → 대기실 → 게임 → 결과창 → 로비**

- **메인화면**: 타이틀, [시작하기], [설정](자리만), [종료]. **프로그램 종료는 메인화면에서만.**
- **로그인**: 이름만 입력. 클라이언트 규칙 **영문자만 2~12자**(사용자 요구). 실패 코드별 한국어 안내, 마지막 이름은 SaveGame으로 기억.
- **로비**: 방 목록(대기 중인 방 최대 20개), [새로고침](연타 방지), [방 만들기](방 이름만), [입장], 검색, [빠른 찾기], [뒤로](로그아웃). 방 비밀번호·맵·난이도 선택은 프로토콜에 없다.
- **대기실**: `SC_ROOM_STATE` 기준. 캐릭터 3명(중복 불가, 캐릭터별 고유 스킬 표시), 퍽 7종 중 최대 3개, [준비], [나가기]. 방장 시작 버튼 없이 "전원 준비 시 자동 시작".
- **게임 조작**(사용자 결정 2026-11-13): WASD **화면(월드) 축 고정 이동**(설정으로 커서 기준 이동 전환 가능), 커서 = 시선·공격 방향, Shift 달리기, Space 구르기, 좌클릭 꾹 누르기 공격, 마우스 휠 무기 교체, **F 상호작용**(우클릭·G 없음), Q 스킬, E 소비 아이템.
  - 상호작용: 3m 안이면 노란 테두리, 커서를 올리면 정보창(품질별 색: 일반 흰색·고급 초록·희귀 파랑·보물 보라). 아이템은 F로 즉시 줍기, 금고·상자는 F 한 번 → 다가가서 작업(이동·공격·구르기·F 다시 누르기로 취소), 필드 아이템은 닿으면 획득. F 대상은 가장 가까운 것, 커서를 올린 것이 3m 안이면 그것 먼저.
  - **반납 = F로 시작하는 4초 시간 작업**(2026-11-23 팀 서버 `3109190`, 2026-11-24 클라): 반납 구역 3곳·30초(경고 = 사라지기 5초 전), 한 구역에 한 명, 금고·상자처럼 진행 게이지. 끊김(구역 밖·경직·쓰러짐·맞음·내 공격·구르기·F)은 처음부터, 실패(TargetBusy = 다른 사람이 반납 중, NotAvailable = 2초 이하 남음, 도중에 구역 닫힘)는 이유별 안내 문구. 개수·수명·작업 시간은 서버 값(`timer_ms`·`duration_ms`)을 따르고 클라에 고정하지 않는다. `DA_VHInteractionConfig`의 `Auto Return`은 끔(켜면 범위에 들어갈 때 작업 요청). 시험 봇 `-VHBotReturnTest`(+ 짝 봇 `-VHBotReturnPartner`, 화면 `-VHReturnShots`).
  - **손전등과 "빛 안에서만 상호작용"은 끔, 맵은 일반 밝기.** (코드 삭제는 기획 확인 뒤)
  - **천장 가림(방 단위 시야, 2026-11-21)**: 내 구역(방 볼륨 35개, 복도는 한 구역)만 보이고 나머지는 회색 천장(`M_VHCeiling`, 언릿)으로 덮는다. 덮인 구역의 플레이어·경비원·물품·연출은 `SetHiddenInGame` 으로 숨김(보이는 것만, 판정은 서버). `UVHCeilingSubsystem`, 설정 `DA_VHGameplayConfig` Camera|Ceiling, 콘솔 `vh.Ceiling 0/1`·`vh.CeilingInfo`. 퍽 표시는 미니맵이라 그대로. 천장에 원 구멍은 없고, 가려진 내 캐릭터는 실루엣(`M_VHSilhouette`, 커스텀 뎁스 스텐실 0 — 스텐실 1~3은 상호작용 테두리 몫)으로 보인다. 반납지점은 화면에선 숨겨도 미니맵엔 항상, 경고 빨강은 **서버 Warning 비트 그대로**(2026-11-23 팀 서버: 등장 25초 = 남은 5초).
    - **앞 가림 지우기(2026-11-21 (4))**: 구역 지도 텍스처(R 넓은 구역·G 좁은 구역)로 "카메라 → 픽셀 선을 바닥까지 늘린 점이 내 구역"인 다른 구역 천장·카메라 쪽 벽·문 픽셀을 지운다(`ZoneCut` 노드, `M_VHCeiling`·`M_VHWallFadeTex`, `make_ceiling_material.py`). 덮인 구역 가구는 숨김(그림자·간접광 유지). 콘솔 `vh.CeilingCut 0/1`·`vh.CeilingCutZ`.
  - **방 입구 문(2026-11-21 (4))**: `AVHDoor` 39개(서버 `above_door` 자리, `door_plan.py` → `setup_shell_level.py doors`). 미닫이, Kenney 문 소리(덮인 구역끼리 문은 무음), 바깥 문 2곳은 잠긴 문. 열린 문 너머는 문간 가림막(세로 천장 조각). 카메라 쪽 벽의 문은 벽과 함께 지운다(사용자 결정 2026-11-21). 콘솔 `vh.Doors 0/1`·`vh.DoorInfo`.
    - **F로 열고 닫기(2026-11-24, 기획 변경)**: 자동 열림 없음, 처음엔 모두 닫힘. F 대상 물품(상자·아이템·금고·반납)이 없거나 지금 할 수 없을 때만 앞의 문(벽 면에서 150cm·옆 40cm 안)을 연다/닫는다. 잠긴 문·문간에 서서 닫기는 거절 안내. **문 상태는 `UVHDoorSubsystem::ApplyDoorState` 한 곳에서만 바뀐다** — 출처 `IVHDoorStateSource`(지금 `로컬`, 서버에 문 상태가 생기면 서버용 출처를 끼우고 SC_ 메시지에서 `ApplyDoorState(ServerId, 열림)`). 설정 `DA_VHGameplayConfig` Door `DoorStateSource`. **다른 클라와 문 상태가 맞춰지지 않는다**(서버 상태 전까지).
    - **닫힌 문 통과 막기**: 문의 `Blocker` 상자(WorldDynamic, Pawn 채널만 막음 = 내 캐릭터만, 총알·시야·조준·카메라·배치 점검과 무관). 열림 0.75 이상이면 통과. 서버 판정은 그대로.
    - **열린 문 너머 엿보기(부채꼴 시야)**: `UVHCeilingSubsystem::UpdatePeek` — 내 구역 열린 문마다 문 로컬 좌표에서 문틈 양 끝(벽 두 면 모두)을 지나는 각도 범위를 구해 광선 64개를 구역 지도 위로 쏘고 너머 구역을 벗어나면(벽) 멈춤 → 다각형을 엿보기 마스크 렌더 타깃(구역 지도 ×3 해상도)에 그림 → 천장·벽 재질 ZoneCut 이 `PeekTex`로 지움. 같은 다각형으로 그 안의 사람·물품·가구를 보이게(`IsPointPeeked`). 바라보는 방향과 무관(설정 `DoorPeekMode`로 "문틈 + 바라보는 방향" 전환 가능). 콘솔 `vh.DoorPeek 0/1`·`vh.DoorPeekInfo`, 개발 `vh.PressF`(F와 같은 길)·`vh.DoorSet open|close|toggle all|near|<id>`. 시험 스크립트 `Tools/MansionBuild/doors/e1_*`.
    - **문 위치 표시(2026-11-21 (5))**: 내 구역 문 = 바닥 문턱 띠 + 바깥쪽 화살표(카메라 쪽 문에도), 덮인 구역 문·카메라 쪽이 아닌 내 문 = 벽 위 밝은 틈, 미니맵 = 문 선분. 잠긴 바깥 문은 모두 빨강(바닥은 X). 2026-11-24부터 열린 문 = 하늘색(밝게), 닫힌 문 = 흰 회색(어둡게)(`DoorMarkClosedColor`·`MinimapDoorClosedColor`). 재질 `M_VHDoorMark`, 값은 `DA_VHGameplayConfig` Camera|Ceiling `DoorMark*`, 콘솔 `vh.DoorMarks 0/1`.
  - **사제 테이저건(도적1, 2026-11-22 서버 판정)**: Q = `UseSkill` → 서버가 들기(`SC_SKILL` Active, 상태 비트 `ES_TASER_READY`), 든 채 Q 다시 = 집어넣기(End, 쿨타임 없음), 든 채 공격 키 = `AttackStart` → 서버가 무기 대신 테이저 발사(Cast, 쿨타임 30초 시작). **든 동안 휠(무기 교체)·E(소비 아이템)는 서버가 Locked**(2026-11-23) → 클라는 보내지 않고 "Q로 먼저 집어넣으세요" 안내만. 경직·쓰러짐으로 서버가 집어넣으면 쿨타임 30초 시작(쓰러짐은 리스폰 때 초기화). 손 모양·HUD는 서버 상태를 따르고, 다른 플레이어 손에도 `ES_TASER_READY`로 보인다. 쿨타임 중 Q는 보내지 않고 알림만. 개발 명령 `vh.Press skill|attack|wheel`, 시험 봇 `-VHBotTaserTest`(+ `-VHTaserStunWait`, 상대 `-VHBotTaserStun` = 테이저건 무기로 경직시킴).
  - **레일건(2026-11-22 서버)**: 클릭 즉시 `AttackStart`(클릭 한 번 = 한 발, 내 `SC_FIRE`를 받으면 `AttackStop`. 2026-11-23 서버도 쏘면 손 뗀 것으로 처리 = 꾹 눌러도 한 발) → 서버가 누른 순간 `SC_FIRE`·탄약 → 1초 충전(판정 없음) → 200ms마다 6번 판정, 이동·회전 잠금 2초. 클라는 `SC_FIRE`부터 1초 충전 연출(소리·빛) 뒤 빔 — 다른 플레이어 레일건도 같게 보이고, 충전 중 쓰러지면 연출을 끊는다(서버도 안 쏨). 빔이 끝날 때까지 손에 레일건을 붙잡아 둔다(마지막 탄이면 서버 무기는 이미 삼단봉). 시험 봇 `-VHBotRailTest`.
  - **서버가 먼저 바뀌어야 하는 기획**(반납 시간, 반납 구역 개수·유지 시간, 버저비터, 상자 무기 확률)은 새 서버 파일을 받은 뒤 작업한다.
- **결과창**: `SC_GAME_END` 기준 순위표(반납 금액, 같은 금액은 같은 순위), 최고 기록·갱신 표시, [로비로], 남은 시간 카운트다운.
- UI 문구는 한국어. 스타일 값은 `DA_VHUIStyle` 한곳에 모은다. 기획 화면비 16:9. HUD 배치는 `Docs/Design/기획_2026-11/UI_수정본.png`를 따른다.
- 위젯 블루프린트는 MCP의 UMG 도구로 만든다. 어려운 부분은 C++ 위젯 클래스에서 위젯 트리를 직접 구성해 동작을 우선 확보하고 기록한다.
- 기능 삭제, 파일 삭제, 큰 구조 변경은 계획을 먼저 보여 주고 사용자 확인을 받은 뒤에 한다.

## 작업 후 보고
작업 단위가 끝날 때마다, 그리고 세션 종료 전에 `Docs/작업보고_YYYY-MM-DD.md`에 정리한다.
- **변경 내용**(파일/에셋) / **테스트 결과**(실제 확인한 것, 못 한 것은 명시) / **남은 문제** / **확인 필요**(가정 목록) / **서버 팀원 질문** / **다음 단계**
- **발표 대비 메모**: 기능별 핵심 원리(예: TCP 스트림 재조립, 단계별 패킷 제한, Mock 서버로 서버 없이 개발한 이유)를 쉬운 말로, 사용자가 결정한 요구사항과 함께 정리
