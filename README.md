# unreal_multi_game_project

언리얼 클라이언트 + IOCP 게임 서버 + 개발 문서를 하나로 관리하는 저장소입니다.

## 폴더 구조

| 폴더 | 내용 |
|---|---|
| `Client/` | 언리얼 프로젝트 (`.uproject`가 이 폴더 바로 아래에 위치) |
| `Server/` | Visual Studio C++ 서버 솔루션 (ServerCore, GameServer) |
| `Common/` | 클라이언트·서버 공용 파일 (패킷 정의, `.proto` 등) |
| `Docs/` | 기획서, 설계 문서 |

## 처음 받을 때

1. GitHub Desktop으로 Clone (Git LFS 초기화 안내가 뜨면 허용)
2. `Client/*.uproject` 우클릭 → **Generate Visual Studio project files**
3. 서버는 `Server/` 안의 `.sln`을 바로 열기

## 규칙

- 대용량 파일(에셋, 이미지, 사운드, 문서)은 `.gitattributes`에 따라 Git LFS로 관리됩니다.
- `.umap` / `.uasset`은 동시에 수정하면 병합이 안 되므로, 작업 전에 팀원과 공유하거나 잠금(lock)을 거세요.
