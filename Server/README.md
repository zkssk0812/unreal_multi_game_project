# Project_Rob — 「털어라! 금고!」 게임 서버

도둑 셋이 5분 동안 경쟁하며 로봇 경비가 지키는 저택을 터는 PvPvE 게임의 C++ IOCP 서버입니다. 시간이 끝났을 때 가장 많은 금액을 반납한 플레이어가 이깁니다.

- 게임 규칙·컨텐츠 명세: [docs/컨텐츠_개발문서.md](docs/컨텐츠_개발문서.md)
- 서버 개발 설계서(명세 → 작업 목록, 개발 기준): [docs/psd.md](docs/psd.md)
- **학습 사이트** (서버 없이 브라우저로 열기): [docs/learn/index.html](docs/learn/index.html)
- 설계: [docs/DESIGN.md](docs/DESIGN.md)
- 진행 상황과 일정: [ROADMAP.md](ROADMAP.md)
- 클라이언트와 공유하는 프로토콜 v3.0: [Shared/Protocol/protocol.h](Shared/Protocol/protocol.h)
- 최고 기록 DB(SQL Server): [tools/db/README.md](tools/db/README.md)

## 구성

| 프로젝트 | 종류 | 역할 |
|---|---|---|
| ServerCore | lib | IOCP 네트워크, SendBuffer, 메모리 풀, JobQueue/JobTimer, Logger |
| VaultHeistContent (`Content/`) | lib | 게임 규칙(로비·방·게임). 소켓·패킷·DB를 모른다 |
| GameServer | exe | 브리지(패킷 ↔ 콘텐츠 통역, 패킷 표, 하트비트), 최고 기록 DB(ODBC), main |
| DummyClient | exe | 서버 시험용 클라이언트 (`phase`, `room`, `bad`, `ping`) |
| ContentTests (`Tests/ContentTests`) | exe | 콘텐츠를 서버 없이 시험 |

## 빌드와 실행

- Visual Studio 2022 (v143, C++ latest), x64. `Project_Rob.sln`을 열어 빌드합니다.
  VS 2026만 있으면 "솔루션 대상 변경"으로 v145를 쓰거나 `MSBuild Project_Rob.sln /p:PlatformToolset=v145`.
- 결과물: `Binaries/x64/<Debug|Release>/`
- 파일을 추가·삭제했다면 `tools/gen_vcxproj.py`의 목록을 고치고 `python tools/gen_vcxproj.py`로 프로젝트를 다시 만듭니다.
- 맵은 `Data/Maps/mansion_1f_server.json`을 서버 시작 때 읽습니다. 원본은 클라 팀의 충돌 정보 `Data/Maps/Mansion_1F_Collision.json`(벽·유리·기둥·소품)이고, `python tools/collision_to_map.py`가 방·순찰 지점·스폰을 더해 만듭니다. 금고·상자는 맵에 자리가 없고 서버가 게임마다 방 안의 빈 칸에서 무작위로 고릅니다. 스폰 위치는 임시 값이라 클라 팀 값을 받으면 스크립트 위쪽 `THIEF_SPAWNS`·`GUARD_SPAWNS`를 고칩니다. 빌드하면 실행 파일 옆으로 복사됩니다.
- 최고 기록 DB: 실행 파일 옆에 `server_config.json`이 있으면 SQL Server에 연결합니다 ([tools/db/README.md](tools/db/README.md)). 없으면 기록 없이 실행합니다.
- 외부 라이브러리: `ThirdParty/nlohmann/json.hpp` (JSON 파서, MIT, v3.11.3)

```
GameServer.exe
  콘솔 명령: notice <메시지> | quit

DummyClient.exe phase              연결 단계 규칙 시험
DummyClient.exe room               로비·방·게임 흐름 시험 (3명)
DummyClient.exe bad                잘못된 패킷 처리 시험
DummyClient.exe ping [세션] [핑]   처리량 시험

ContentTests.exe [이름 일부]       콘텐츠 시험 (152개)
```
