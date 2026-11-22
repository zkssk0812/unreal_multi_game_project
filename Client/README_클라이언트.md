# 털어라! 금고! — 클라이언트 처음 받기

언리얼 프로젝트 `Client/MCPTest.uproject`(코드명 VaultHeist, 프로젝트 이름은 아직 MCPTest)를 처음 받아 실행하는 순서입니다.
2026-11-22에 새 폴더로 clone → 빌드 → 실서버 접속 한 판까지 확인했습니다.

## 준비물
| 항목 | 내용 |
|---|---|
| OS | Windows 10/11 64비트 |
| 언리얼 엔진 | **5.8** (에픽게임즈 런처 → 언리얼 엔진 → 라이브러리에서 설치). `.uproject`의 엔진 버전이 5.8로 되어 있습니다 |
| Visual Studio | 2026 또는 2022 (Community로 충분). 설치 관리자에서 **"C++를 사용한 게임 개발"** 워크로드와 그 안의 **Unreal Engine 설치 관리자/IDE 지원**, **MSVC v143 빌드 도구(14.44)**, **Windows 11 SDK(10.0.22621 이상)** 를 켭니다. (확인한 PC: VS 2026 + MSVC 14.44 + SDK 10.0.26100) |
| Git + Git LFS | Git for Windows(LFS 포함) 또는 GitHub Desktop. 에셋·이미지·소리는 LFS(약 630MB)라 **LFS가 없으면 에셋이 깨집니다** |
| 디스크 | 받은 것 약 1.3GB + 빌드 산출물 약 3GB |

## 순서
1. **받기** (GitHub Desktop이면 Clone 후 LFS 안내가 뜨면 허용)
   ```
   git lfs install
   git clone https://github.com/zkssk0812/unreal_multi_game_project
   ```
2. **LFS 확인**: `git lfs fsck` → `Git LFS fsck OK`. `Client/Content` 안의 `.uasset`이 1KB 정도의 글자 파일이면 LFS를 못 받은 것 → `git lfs pull`.
3. **프로젝트 파일 만들기**: `Client/MCPTest.uproject` 우클릭 → **Generate Visual Studio project files** (Windows 11은 "추가 옵션 표시" 안에 있음).
4. **빌드**: 만들어진 `Client/MCPTest.sln`을 Visual Studio로 열고 구성 **Development Editor / Win64** → 빌드(Ctrl+Shift+B). 1분 안팎.
   - 빌드 없이 `.uproject`를 열면 "MCPTest 모듈이 없습니다. 다시 빌드할까요?" 창이 뜹니다 → **예**를 눌러도 됩니다.
5. **에디터 열기**: `Client/MCPTest.uproject` 더블클릭(또는 런처 → 내 프로젝트). 처음 한 번은 셰이더 컴파일로 오래 걸릴 수 있습니다(오른쪽 아래 진행률). 시작 맵은 `L_MainMenu`.
   - 출력 로그의 `LogAutomationTest: Error: Condition failed` 15줄, `LogPython: Warning ... VHRoomState`, 플레이 중 `Attempting to focus Non-Focusable widget`은 원래 나오는 것이라 무시해도 됩니다.

## 서버에 붙여 한 판 해 보기
1. 서버 켜기: 팀 저장소 `Server/`의 `Project_Rob.sln`을 Release/x64로 빌드해 `GameServer.exe` 실행(포트 9000, 자세한 것은 `Server/README.md`). 콘솔에 아무것도 안 쓰고 Enter만 누르면 서버가 꺼집니다.
2. 에디터에서 `L_MainMenu`를 열고 ▶ 플레이.
3. 아래쪽 주소칸 `127.0.0.1:9000` 확인 → **[실서버로 전환]** (기본은 서버 없이 도는 "가짜 서버")
4. [시작하기] → 이름(**영문자만 2~12자**) → 로비 [방 만들기] → 캐릭터 1명·퍽 최대 3개 → [준비]. 방 안 전원이 준비하면 자동 시작(혼자도 됨).
5. 조작: WASD 이동(화면 위 = 앞), 마우스 커서 = 바라보는 방향, 좌클릭 꾹 = 공격, Shift 달리기, Space 구르기, F 상호작용(상자·금고·아이템), 휠 무기 교체, Q 스킬, E 소비 아이템.
6. 5분 뒤 결과창 → 로비.
- 참고: 팀 저장소 서버의 맵 버전은 10, 클라 설정(`Config/DefaultGame.ini`의 `GameLevelMapVersion`)은 아직 2라서 게임 시작 때 "서버 맵 버전이 다릅니다" 안내가 뜰 수 있습니다. 방 배치는 같아서 게임에는 지장 없습니다(맞출 예정).

## 주의
- **`Client/`는 클라이언트 담당이 작업 폴더에서 동기화 스크립트로 통째로 올립니다.** `Client/` 파일을 직접 고쳐 커밋하면 다음 업로드 때 덮어써질 수 있으니, 고칠 것이 있으면 클라이언트 담당에게 알려 주세요.
- `.uasset`/`.umap`은 합칠 수 없는 파일이라 같은 에셋을 동시에 고치지 않습니다.
- `Binaries/`, `Intermediate/`, `Saved/`, `DerivedDataCache/`, `*.sln`은 만들어지는 파일이라 올리지 않습니다.
- 프로젝트 안내·규칙은 `Client/CLAUDE.md`, 작업 기록은 `Client/Docs/작업보고_*.md`.
