# 「털어라! 금고!」 서버 개발 설계서 (PSD)

> **이 문서는 개발의 기준입니다.** Claude는 이 문서만 보고 개발합니다.
>
> | 항목 | 값 |
> |---|---|
> | 원본 명세 | [컨텐츠_개발문서.md](컨텐츠_개발문서.md) |
> | **동기화 기준** | 명세 변경 이력 `2026-11-14` "Q-39~45 답" 항목까지 반영 |
> | 목표 프로토콜 | **v3.0** (지금 v2.1, 호환 안 됨 → 클라 팀과 동시 교체) |
> | 지금 구현 설명 | [GAME_SYSTEM.md](GAME_SYSTEM.md) (v2.1, 협동 PvE) |

---

## 0. 작업 방식

```
① 사용자: 컨텐츠_개발문서.md 수정 (규칙·수치·컨텐츠 추가, Q 답변)
② psd.md 갱신: 바뀐 명세 항목 → 설계(§3~§5)·작업(§6)·가정(§2)에 반영, 동기화 기준 갱신
③ Claude: psd.md §6의 작업을 순서대로 개발 → 시험 → 작업 상태 갱신 → 커밋
```

- **명세가 바뀌면 psd.md부터 고칩니다.** 코드를 명세와 바로 맞추지 않습니다(psd.md가 둘 사이의 약속).
- 명세 항목 번호(`W-13`, `N-15` …)를 이 문서와 커밋 메시지에 그대로 씁니다. 작업 번호는 `T-xx`, 가정은 `A-xx`.
- 명세에 ❓가 있거나 명세가 말하지 않는 구현 세부는 §2의 **가정**으로 개발하고, 답이 정해지면 가정을 고친 뒤 코드를 맞춥니다.
- 수치는 모두 `Content/GameData.h`에, 위치는 맵 JSON에 둡니다. 이 문서 §4의 표와 `GameData.h`가 같아야 합니다.

**작업 상태 표시**: ⬜ 대기 · 🔨 진행 중 · ✅ 완료 · ⏸ 보류(질문 대기)

---

## 1. 바뀌는 범위 한눈에 보기

| 구분 | 내용 |
|---|---|
| 남김 | 접속·로비·방·로딩·틱·결과 흐름, 스테미나·탈진·구르기, 상호작용 틀(한 대상 한 명, 진행도), 반납 지점 5곳 순환, 경비원 시야·순찰·추격·탐색·경로(A*), 맵 로딩 (맵은 2026-11-13 `Mansion_1F_Collision.json`으로 바뀜, T-73) |
| 바꿈 | 협동 → 경쟁(돈·순위가 개인별), 한 대에 포획 → HP·대미지, 한 번 공격 → 꾹 눌러 연사(SP 소비 없음), 상호작용 거리 1.5m → 3m 확인·1m 작업, 금고 5개·30초 → 1개·10초 순환, 보물 10종 → 1종(가치 복리 상승), 경비원 경직 → HP 200·자가 수리, 경비원 근접 포획 → 기관총, 경비원 속도·회전, 타겟 규칙, 캐릭터 4 → 5, 퍽 8 → 7, 게임 시간 5분 |
| 새로 | HP·보호막, 쓰러짐·리스폰, 무기 10종·무기 칸 2개(삼단봉 채움), 근접 부채꼴·히트 박스 판정·레일건·투척·지뢰, 캐릭터 고유 스킬(임시 3종), 퍽 7종 효과, 환금 아이템 품질 4단계, 상자(부수기·드롭 표·재생성), 소비 아이템 4종(E), 필드 아이템(회복약 3종), 상태이상 10종, 개인 상태 패킷, 최고 기록 저장 |
| 지움 | 숨는 곳, 포획·운반·감금·구출(창고 문), 탈출구·탈출, 소리 추격·의심·끄집어내기, 무게, 인벤토리 슬롯 선택·버리기, 팀 목표 금액, 손전등, 무기 없는 기본 공격, 보물 10종 표 |

---

## 2. 가정 (명세 ❓·명세에 없는 구현 세부 → 임시 결정)

답이 정해지면 "답" 열에 적고, 가정과 다르면 관련 작업을 다시 엽니다.
명세에서 답이 나온 질문(Q-01~19, 21~23, 26, 27, 29~37)은 가정이 아니라 §4 수치와 §6 작업에 바로 반영했습니다.

### 2.1 명세 ❓ (기획 답 대기)

| 가정 | 명세 질문 | 임시 결정 (이대로 개발) | 영향 작업 | 답 |
|---|---|---|---|---|
| A-01 | Q-20 환금 아이템 목록 | 품질별로 이름 없는 아이템 1종씩: 일반·고급·희귀. 생길 때 품질 범위 안에서 가치를 무작위로 정함(일반 1~29, 고급 30~99, 희귀 100~500). 맵에 처음부터 놓이는 것은 없음 (상자·쓰러진 플레이어에게서만) | T-20 | |
| A-02 | Q-24 상자 수·배치·재생성 | 맵에 상자 지점이 없으므로 서버가 **방 안 무작위 20개**. 부서진 상자는 **같은 자리에 30~60초 사이 무작위 시간 뒤** 다시 생김. 대미지 20은 **누적** (리스폰·재생성 때 초기화) | T-50 | |
| A-03 | Q-25 소비 아이템을 들고 있을 때 다른 것을 주움 | 무기와 같은 방식으로 **바꿈**: 새 것을 들고, 들고 있던 것은 주운 자리에 놓임 | T-51 | |
| A-04 | Q-28 필드 아이템 생성 위치 | 방 안 무작위 지점 (다른 오브젝트와 1m 이상 떨어짐). 5개로 시작, 20초마다 1개, 최대 20개 | T-52 | |
| A-05 | Q-38 금고·상자 위치 | **확정(2026-11-13): 방 안 무작위.** 금고는 반납 지점을 놓을 수 있는 방(복도·문 없는 방 제외) 중 무작위 방의 빈 칸(다른 오브젝트와 1m 이상). 게임 시작 때는 **의적 셋이 시작하는 방**을 빼고(O-01), 그 뒤로는 **직전 금고가 있던 방**을 빼고(O-07) 고름. 상자도 같은 칸 목록에서 무작위 | T-23, T-73 | 확정 |
| A-06 | Q-18 캐릭터 ↔ 스킬 | 도적1 = 사제 테이저건(S-01), 도적2 = 끈끈이(S-02), 도적3 = 은신(S-03). 캐릭터는 도적1~3 셋으로 확정(2026-11-22, 도적4·5 없앰). 표 한 줄만 바꾸면 되게 만듦 | T-60 | |

### 2.2 명세에 없는 구현 세부 (서버가 정함 — 기획과 다르면 알려 주세요)

> **2026-11-11 사용자 확인**: A-10~A-25 모두 이대로 진행. A-18은 이후 기획 변경으로 **움직이면서 사격**(명세 §12.2). A-21은 **SQL Server**로 바뀜.

| 가정 | 주제 | 임시 결정 | 영향 작업 |
|---|---|---|---|
| A-10 | 히트 박스 | 플레이어·경비원 모두 반지름 40cm, 높이 180cm 원기둥. 상자 반지름 50cm | T-15 |
| A-11 | 벽과 판정 | 레일건만 벽을 무시. 나머지(근접·권총·테이저·화염방사기·기관총·폭발)는 **벽에 막힘** (중심에서 대상까지 선이 벽에 걸리면 안 맞음) | T-15, T-41~46 |
| A-12 | 무제한 사거리 | 맵 바닥 범위의 대각선 + 10m를 서버 시작 때 계산 | T-15 |
| A-13 | 화염방사기 범위 모양 | 시선 방향 **좌우 15° 부채꼴**, 반지름 6m, 안의 대상 모두 | T-43 |
| A-14 | 투척(수류탄·섬광탄) 조준 | 클라가 보낸 조준 지점(커서) 쪽으로, 거리는 **최대 6m**까지. 포물선은 1초 동안 날아감. 서버는 평면에서 날아가는 선이 벽·플레이어·경비원에 닿는지 매 틱 확인 → 닿으면 그 자리, 아니면 도착 지점에서 터짐 | T-45, T-51 |
| A-15 | 수류탄 대미지 계산 | 거리 d(m): d ≤ 2 → 120, 그 뒤 `120 − 20 × ceil(d − 2)`, 6m 넘으면 0. 지뢰도 같은 방식(폭발 80 − 30·ceil(d−1), 감전 20 − 10·ceil(d−1) 최소 1) | T-45, T-46 |
| A-16 | 연출용 수치가 없는 공격 간격 | 초당 공격 횟수 → 간격: 2회 500ms, 3회 333ms, 4회 250ms, 30회 33ms. 1회 무기는 쏘면 바로 탄 0 | T-11, T-4x |
| A-17 | 쓰러뜨린 원인 | 다른 플레이어의 무기·지뢰·**자기 지뢰**로 쓰러지면 "플레이어에게 쓰러짐". 경비원 기관총으로만 "경비원에게 쓰러짐" | T-12, T-22 |
| A-18 | 경비원 사격 중 이동 | ~~멈춰 서서 쏨~~ → 명세 §12.2로 확정: 타겟이 공격 범위(60°) 안에 보이면 **추격 속도로 쫓으면서** 쏨. 범위 밖이면 돌면서 추격(사격 없음). 기관총 탄은 다른 경비원에게 맞지 않음, 상자에는 맞지 않음(N-09, Q-43) | T-32 |
| A-19 | 제트팩 충돌 | 이동은 클라가 하므로, 서버가 제트팩 중인 플레이어와 다른 대상의 거리가 히트 박스 합(80cm) 안이면 충돌로 봄. 한 번 쓸 때 대상마다 한 번. 밀려나는 플레이어에게 `SC_KNOCKBACK`(방향·3m)을 보내고 그 클라가 이동 | T-51 |
| A-20 | 순위 동점 | **확정(Q-45)**: 반납 금액이 같으면 같은 순위(높은 쪽). 다음 순위는 사람 수만큼 건너뜀 — 100·100·50 → 1·1·3, 100·50·50 → 1·2·2 | T-70 |
| A-21 | 최고 기록 DB | **Microsoft SQL Server** (관리 도구 SSMS 22). 콘텐츠는 `IRecordStore` 인터페이스만 알고, 브리지가 **ODBC**(`ODBC Driver 18 for SQL Server`, Windows 기본 `odbc32.lib`)로 구현. 접속 문자열은 저장소에 넣지 않는 설정 파일(`server_config.json`, `.gitignore`)에서 읽음. DB 작업은 별도 DB 스레드(JobQueue)에서 하고 결과만 방으로 돌려줌. DB에 연결할 수 없으면 로그만 남기고 게임은 계속(결과의 `new_record`는 false) | T-71 |
| A-22 | 떨어진 무기의 탄 | 바뀌어 땅에 놓인 무기는 남은 탄을 그대로 가짐 | T-40 |
| A-23 | 엔티티 상한 | 64 → **128** (상자 20·필드 아이템 20·떨어진 아이템·지뢰 등) | T-02 |
| A-24 | 보물이 맵에 남은 채 게임 끝 | 아무 일 없음 (반납하지 않은 가치는 점수가 아님) | T-24 |
| A-25 | 경비원 시야와 연막 | 연막 원 안의 플레이어는 감지 대상에서 빠짐. 원 밖에서 보는 선이 연막을 지나는 것은 막지 않음 | T-31 |
| A-26 | 의적 시작·리스폰, 경비원 스폰 | **의적: 확정(Q-39)** — 시작은 방 안 무작위 칸(반납 지점 칸과 같은 목록, 다른 오브젝트와 1m 이상), **셋은 서로 다른 방**(안 되면 아무 방), 바라보는 방향도 무작위. 리스폰도 방 안 무작위 칸(쓰러진 방도 될 수 있음). 맵의 `spawns.thief`는 이제 **기준 지점**(첫 지점에서 걸어서 닿는 칸만 씀, 방이 없는 맵에서만 시작 위치)으로만 씀. **경비원: 임시** — 경비실(−4300, 1750)·식당(2500, −1200)·중앙 홀(−300, −700) | T-12, T-73, T-74 |
| A-27 | 새 맵의 방·순찰 (충돌 파일에 없음) | 방 = 문을 닫았다고 보고 벽·유리·기둥으로 막힌 공간, 이름은 안의 소품 `room` 값(없으면 Area_n). **복도는 반납 지점·상자·금고를 두지 않음.** 문 없는 방(Area_2·4·5)은 갈 수 없어 빠짐. 순찰 지점 = 방마다 가운데 + 큰 방은 7m 간격 + 안쪽 문마다 하나. 바깥 문 2곳(현관·주방 옆)은 막음(건물 밖은 게임 공간이 아님). 문 위·창 위 벽은 이동을 막지 않고, 유리는 이동만 막고 시야는 통과, 높이 100cm 미만 소품은 시야를 막지 않음 | T-73 |

---

## 3. 프로토콜 v3.0 설계

`Common/Protocol/protocol.h`(저장소 루트)를 v3.0으로 바꿉니다. **호환되지 않는 변경**이므로 enum 번호도 새로 매기고, 클라 팀에 이 절을 그대로 전달합니다. `MessageEncoder.cpp`의 `VH_SAME_ENUM` 목록도 함께 맞춥니다.

- `PROTOCOL_VERSION = (3 << 16) | 0`, `MAX_ENTITIES = 128` (A-23), `MAX_PACKET_SIZE` 8192 유지 (시작 패킷이 넘지 않는지 `static_assert`).
- 서버는 **모든 정보를 전원에게 보냅니다.** 퍽·은신·연막으로 숨길 것은 클라가 화면에서 숨깁니다 (Q-15). 서버는 판단에 필요한 상태 비트와 퍽 목록만 함께 보냅니다.

### 3.1 enum

```cpp
enum class EntityKind : uint8_t {
    Thief, Guard, Safe, ReturnPoint,
    Valuable,        // type_id = ValuableQuality, param = 가치
    WeaponPickup,    // type_id = WeaponId, param = 남은 탄
    ConsumablePickup,// type_id = ConsumableId
    FieldItem,       // type_id = FieldItemId (닿으면 효과)
    Box,
    StickyTrap,      // owner = 설치자, timer_ms = 남은 시간
    Mine,            // type_id = WeaponId(BlastMine/ShockMine), owner = 설치자
    Smoke,           // timer_ms = 남은 시간, 반지름은 고정값
};

enum class ActionType : uint8_t {
    None, AttackStart, AttackStop, Dodge,
    InteractBegin, InteractCancel,   // target = 엔티티 ID
    SwitchWeapon,                    // 무기1 ↔ 무기2
    UseSkill,                        // 은신 중 다시 누르면 해제
    UseConsumable,
};

enum class RejectReason : uint8_t {
    InvalidState, Cooldown, NoStamina, OutOfRange, TargetBusy, NotAvailable,
    Downed, Stunned, Locked /*레일건·제트팩 중*/, HpFull, OnlyOneWeapon,
};

enum class WeaponId : uint8_t {
    None, Baton, Sword, BeamSword, Pistol, Flamethrower, Railgun, Taser,
    Grenade, BlastMine, ShockMine,
};
enum class ConsumableId   : uint8_t { None, HealKit, Smoke, Flashbang, Jetpack };
enum class FieldItemId    : uint8_t { None, PotionS, PotionM, PotionL };
enum class ValuableQuality: uint8_t { Common, Advanced, Rare, Unique /*보물*/ };
enum class SkillId        : uint8_t { None, Taser, Sticky, Cloak };
enum class DownCause      : uint8_t { Player, Guard };
enum class AlertLevel     : uint8_t { Patrol, Chase, Attack, Search, Repairing, Stunned };
enum class EndReason      : uint8_t { TimeUp, Aborted };
enum class ExplosionKind  : uint8_t { Grenade, BlastMine, ShockMine, Flashbang };
```

삭제: `HitOutcome`, `ReleaseReason`, `HideState`, `TooHeavy`, `TreasureFull`, `StorageDoor`, `HideSpot`, `ExitGate`, `DroppedTreasure`, `FlashlightOn/Off`, 보물 ID 표.

### 3.2 상태 비트 (`uint32_t`로 넓힘)

| 비트 | 이름 | 대상 |
|---|---|---|
| 0 | Running | 플레이어·경비원 |
| 1 | Dodging | 플레이어 (구르기 무적) |
| 2 | Interacting | 플레이어 |
| 3 | Exhausted | 플레이어 |
| 4 | Attacking | 플레이어·경비원 (공격 키 누르는 중 / 사격 중) |
| 5 | Downed | 플레이어 (리스폰 대기) |
| 6 | Invulnerable | 플레이어 (리스폰 5초) · 경비원 (자가 수리) |
| 7 | Stunned | 플레이어·경비원 (E-01) |
| 8 | Slowed | 플레이어·경비원 (끈끈이 위, E-02) |
| 9 | Cloaked | 플레이어 (E-03) |
| 10 | InSmoke | 플레이어·경비원 (E-08) |
| 11 | Flashed | 플레이어 (E-07, 3초) |
| 12 | RailgunLock | 플레이어 (E-10, 1초) |
| 13 | Jetpack | 플레이어 (3초) |
| 14 | Repairing | 경비원 |
| 15 | LeftGame | 플레이어 |
| 16 | Opened | 금고 |
| 17 | Warning | 반납 지점 (55초 경과 = 마지막 5초) |
| 18 | Armed | 지뢰 (설치 3초 뒤 작동) |
| 19 | Triggered | 지뢰 (감지, 0.5초 경고) |
| 20 | CarryingTreasure | 플레이어 |
| 21 | TaserReady | 플레이어: 사제 테이저건을 든 상태 (Q로 들기, 공격 키로 발사. 2026-11-22 추가) |
| 22~31 | 예비 | |

### 3.3 구조체

```cpp
struct EntitySpawn {            // SC_GAME_START, SC_ENTITY_SPAWN
    uint16_t entity_id; EntityKind kind; uint16_t type_id; uint64_t owner_player_id;
    Vec3 pos; float yaw; uint32_t state;
    uint32_t timer_ms;          // 남은 수명 (반납 지점·끈끈이·연막), 없으면 0   (O-25)
    uint32_t param;             // 환금 아이템 가치 / 떨어진 무기 탄 (U-09, A-22)
};

struct EntitySnapshot {         // SC_SNAPSHOT
    uint16_t entity_id; uint32_t state; Vec3 pos; float yaw;
    uint16_t hp;                // 플레이어·경비원·상자. 그 밖은 0
    ActionType action;
    WeaponId weapon;            // 들고 있는 무기 (애니메이션)
};
```

- **스냅샷에 넣는 엔티티**: 플레이어·경비원은 매 틱, 그 밖의 오브젝트는 **상태가 바뀐 틱에만**. 오브젝트의 처음 상태는 `SC_GAME_START` / `SC_ENTITY_SPAWN`으로 받습니다.
- `CS_ACTION`에 `Vec3 aim`(커서가 가리키는 바닥 지점)을 추가합니다. 투척·섬광탄 조준(A-14)에 씁니다. 공격·스킬 방향은 이동 패킷의 `yaw`(시선, CT-09)를 씁니다.

### 3.4 패킷 목록

| ID | 패킷 | 방향 | 내용 | 변경 |
|---|---|---|---|---|
| 2009 | `SC_ROOM_STATE` | S→C | 캐릭터 1~3, 퍽 1~7 | 범위만 바뀜 |
| 3001 | `SC_GAME_LOAD` | S→C | 금고 자리 목록 삭제 (금고는 엔티티로). `spawn_index`는 자리 순서일 뿐 — **실제 시작 위치는 `SC_GAME_START`의 엔티티 pos** (A-26) | 바뀜 |
| 3011 | `SC_SNAPSHOT` | S→C | §3.3 | 바뀜 |
| 3020 | `CS_ACTION` | C→S | type, target(엔티티), **aim** | 바뀜 |
| 3090 | `SC_GAME_END` | S→C | §3.5 | 바뀜 |
| 4001 | `SC_FIRE` | S→C | shooter, weapon, origin, dir, hit_entity — 한 번의 공격 판정(연출·소리용) | `SC_ATTACK` 대체 |
| 4002 | `SC_DAMAGE` | S→C | attacker, target, weapon, amount, hp_after, shield_after | `SC_HIT` 대체 |
| 4003 | `SC_DOWNED` | S→C | victim, attacker, cause, lost_banked, respawn_ms | `SC_CAPTURED` 대체 |
| 4004 | `SC_RESPAWNED` | S→C | thief, pos, invuln_ms | `SC_RELEASED` 대체 |
| 4005 | `SC_PROJECTILE` | S→C | thrower, kind(수류탄·섬광탄), from, to, flight_ms | 새로 |
| 4006 | `SC_INTERACT` | S→C | 그대로 (duration은 작업 속도 반영, 진행도 포함) | 유지 |
| 4007 | `SC_SAFE_OPENED` | S→C | safe, opener | 유지 |
| 4008 | `SC_MONEY` | S→C | 플레이어별 {thief, carried_value, banked_value} | `SC_TREASURE` 대체 |
| 4009 | `SC_EXPLOSION` | S→C | kind, pos, radius | 새로 |
| 4010 | `SC_TREASURE_TAKEN` | S→C | thief, pos, value — 전원 메시지(I-23), 보물탐색 방향(K-05) | `SC_HIDE` 대체 |
| 4011 | `SC_GUARD_ALERT` | S→C | guard, level, target | 유지 (enum만 바뀜) |
| 4012 | `SC_KNOCKBACK` | S→C | target, dir, distance — 제트팩 밀려남 (A-19) | 새로 |
| 4013 | `SC_PLAYER_STATUS` | S→C (본인) | hp, shield, sp, weapons[2]{id, ammo}, active_slot, consumable, skill_id, skill_cooldown_ms, skill_active_ms, death_save_left | 새로 (U-01, U-03) |
| 4014 | `SC_SKILL` | S→C | caster, skill_id, state(Cast/Active/End), pos. 사제 테이저건: 들기 = Active, 발사 = Cast, 쏘지 않고 집어넣음 = End | 새로 |
| 4015 | `SC_PICKUP` | S→C | thief, kind, type_id, param (연출용) | 새로 |

삭제: `SC_JAILED`, `SC_ESCAPED`, `SC_EXIT_OPENED`, `CS_SELECT_SKILL`(스킬은 캐릭터 고정).

- `SC_PLAYER_STATUS`는 값이 바뀐 틱에만 본인에게 보냅니다 (달리는 동안은 SP가 매 틱 바뀜).

### 3.5 결과 (`PlayerResult`)

`player_id, nickname, character_id, left_early, banked_value(= 점수), rank, best_record, new_record(bool), kills, downs, treasures_delivered`
— 순위 = 반납 금액 내림차순, 같으면 같은 순위 (G-07, A-20).

---

## 4. 컨텐츠 데이터 설계 (`Content/GameData.h`)

### 4.1 표 데이터

```cpp
enum class WeaponHit : uint8_t { MeleeCone, Ray, Cone, Beam, Thrown, Mine };
struct WeaponType {
    WeaponId id; WeaponHit hit;
    uint16_t damage; uint32_t intervalMs;
    uint16_t ammo;            // 0 = 무한
    float rangeCm;            // 0 = 무제한 (A-12)
    float halfAngleDeg;       // 근접·화염방사기
    uint32_t stunMs;
};
struct ConsumableType { ConsumableId id; /* 수치는 아래 상수 */ };
struct FieldItemType  { FieldItemId id; uint16_t heal; uint8_t weightPct; };
struct QualityType    { ValuableQuality q; uint32_t minValue, maxValue; };
struct SkillType      { SkillId id; uint32_t cooldownMs, castDelayMs, durationMs; bool cooldownOnEnd; };
struct CharacterType  { uint16_t id; SkillId skill; };      // A-06
struct BoxDrop        { enum Kind { Weapon, FieldItem, Nothing, Consumable, Common, Advanced, Rare } kind; uint8_t pct; };
```

### 4.2 수치 (명세 + 가정)

| 분류 | 값 | 출처 |
|---|---|---|
| 게임 시간 | 300,000ms | G-04 |
| 캐릭터 / 퍽 | 캐릭터 1~3 (중복 불가, 2026-11-22 5→3), 퍽 1~7 최대 3개 (중복 불가) | CH-01~05, K-01~07 |
| 플레이어 HP | 100, 자연 회복 없음 | P-01 |
| SP | 100, 달리기 −10/s, 회복 +10/s(탈진 +5/s), 구르기 −20·0.5초 무적, **공격 소비 없음**. 무게 배율 삭제 | P-02~03, Q-08 |
| 이동 | 걷기 300cm/s, 달리기 600cm/s (이동은 클라, 서버는 상태만) | P-04 |
| 작업 속도 | 1.0, 유용한 도구 1.1 | P-05, K-03 |
| 상호작용 | 대상 확인 300cm, 금고·상자 작업 100cm (여유 +30cm) | IN-02, Q-33 |
| 무기 | 아래 표 | §8.2 |
| 리스폰 | 플레이어에게 5초·반납 −10%, 경비원에게 10초·반납 −40%, 무적 5초, 방 안 무작위 칸 | D-04~07 |
| 금고 | 1개, 방 안 무작위 칸(시작 때 의적 시작 방, 그 뒤 직전 방 제외), 10,000ms × 작업 속도, 진행도 유지 | O-01~07, A-05 |
| 보물 | 첫 2,000$, 반납마다 `ceil(직전 × 1.2 / 10) × 10` | I-22, Q-23 |
| 반납 지점 | 5곳, 55초 경고(마지막 5초, 2026-11-22 50→55초), 60초 교체, 범위 100cm) | O-21~25 |
| 상자 | HP 20(누적), 상호작용 3,000ms × 작업 속도(멈추면 초기화), 20개, 재생성 30~60초 | O-11~14, A-02 |
| 상자 드롭 | 무기 60 · 필드 10 · 없음 5 · 소비 15 · 일반 6 · 고급 3 · 희귀 1 (%). 무기 안: 권총·테이저건 20 · 도검 15 · 수류탄 12 · 폭발·감전 지뢰 8 · 빔 소드 7 · 화염방사기·레일건 5 (2026-11-22 클라 요청) | O-12 |
| 필드 아이템 | 소 +10(60%) · 중 +30(30%) · 대 +50(10%), 시작 5개, 20초마다 1개, 최대 20, HP 100이면 안 먹음 | I-11~16 |
| 소비 아이템 | 회복 키트 +30 · 연막 반지름 500cm·10초 · 섬광탄 최대 600cm·반지름 300cm·플레이어 3초·경비원 경직 2초·사용자 제외 · 제트팩 3초·1,000cm/s·밀어냄 300cm·경비원 경직 1초. 1개만 | I-31~34 |
| 경비원 | 3명, HP 200, 자가 수리 5초(무적·정지), 탐색 210cm/s, 추격 420cm/s, 회전 90°/s, 탐지 1,500cm, 시야 좌우 45° | N-02~06, N-11, Q-17 |
| 기관총 | 10 대미지, 100ms 간격, 공격 범위 좌우 30°·1,500cm, 난사(타겟 방향 ±30° 무작위, 발마다 단일 히트 박스), 쏘면서 추격 속도로 이동 | 12.2 |
| 스킬 | 테이저 Q로 들기 → 공격 키로 발사·30s 쿨(발사 때, 경직·쓰러짐으로 취소될 때. Q로 집어넣으면 없음)·든 동안 무기 교체·소비 아이템 불가·300cm 단일·경직 2초 · 끈끈이 30s 쿨·1초 뒤·뒤 100cm·반경 300cm·10초·감속 50%(설치자 포함) · 은신 30s 쿨(해제 시)·20초·감속 80% | S-01~03 |
| 퍽 | 방탄조끼 보호막 20 · 가시회생 1회 · 유용한 도구 ×1.1 · 운동화 달리기 ×1.2 (리스폰 때 다시 충전) | K-01~04 |
| 히트 박스 | 반지름 40cm, 높이 180cm. 상자 50cm | A-10 |

**무기 표**

| 무기 | 판정 | 대미지 | 간격 | 탄 | 사거리 | 각도 | 경직 |
|---|---|---|---|---|---|---|---|
| 삼단봉 | 근접 부채꼴 | 10 | 500ms | 무한 | 200cm | 좌우 60° | |
| 도검 | 근접 부채꼴 | 25 | 500ms | 무한 | 300cm | 좌우 60° | |
| 빔 소드 | 근접 부채꼴 | 40 | 333ms | 6 | 400cm | 좌우 60° | |
| 권총 | 직선 단일 | 10 | 250ms | 12 | 무제한 | | |
| 화염방사기 | 부채꼴 | 2 | 33ms | 90 | 600cm | 좌우 15° (A-13) | |
| 레일건 | 빔 폭 50cm, 벽 무시 | 20 × 6 (충전 1초 뒤 0·200·…·1,000ms) | 1발 2초 | 2 | 무제한 | | 충전 1초 + 발사 1초, 2초 이동·회전 잠금 |
| 테이저건 | 직선 단일 | 1 | — | 1 | 300cm | | 2,000ms |
| 수류탄 | 투척, 반지름 600cm, 던진 사람 제외 | 120~40 (A-15) | — | 1 | 600cm | | |
| 폭발 지뢰 | 설치, 3초 뒤 작동, 감지 100cm, 0.5초 뒤 반지름 300cm, 설치자 포함 | 80~20 | — | 1 | 발밑 | | |
| 감전 지뢰 | 폭발 지뢰와 같음 | 20~1 | — | 1 | 발밑 | | 2,000ms |

### 4.3 엔티티 데이터 (`Entity`에 추가)

```
공통(플레이어·경비원): hp, maxHp, stunUntilMs, slowed(이번 틱), inSmoke(이번 틱), attackHeld, nextFireMs
플레이어: shield, deathSaveLeft, perks(비트), characterId, skillReadyMs, skillActiveUntilMs, skillCastAtMs,
        weapons[2]{id, ammo}, activeSlot, consumable, carriedValue, carriesTreasure, banked,
        downedUntilMs, downCause, invulnUntilMs, flashedUntilMs, railgunUntilMs, railgunTicks, railgunDir,
        jetpackUntilMs, jetpackHit(대상 목록), spawnIndex, 결과용 카운터(kills, downs, treasures)
경비원: repairUntilMs, brain.lockedByAttack (N-16), bodyYaw(느린 회전)
금고: slotIndex, progressMs
상자: hp, respawnAtMs, workProgressMs
아이템: value / ammo (param)
끈끈이·연막: expireMs, ownerEntity
지뢰: armAtMs, triggerAtMs, ownerEntity
투척물(엔티티 아님, 월드 목록): from, to, startMs, flightMs, kind, owner
```

삭제: `hideSpot`, `carriedBy`, `carrying`, `nextNoiseMs`, 무게 계산, 인벤토리 슬롯, `_jailCells`, `_jailZone`, `_noises`, 손전등, 탈출구.

---

## 5. 코드 구조 설계

`GameWorld.cpp`가 커지지 않게 같은 클래스를 기능별 파일로 나눕니다 (`GuardAI.cpp`와 같은 방식). 새 파일은 `tools/gen_vcxproj.py`에 등록합니다.

| 파일 | 내용 |
|---|---|
| `Game/GameWorld.cpp` | Setup, Update 순서, 입력(Move/Action 분기), 조회, 스냅샷·결과 |
| `Game/WorldCombat.cpp` 🆕 | `ApplyDamage`(무적 → 보호막 → HP → 가시회생 → 쓰러짐), 무기 발사(근접·직선·부채꼴·빔), 쓰러짐·리스폰, 경비원 HP·수리 |
| `Game/WorldExplosives.cpp` 🆕 | 투척물 비행·충돌, 폭발 대미지 감쇠, 지뢰 작동, 섬광 |
| `Game/WorldItems.cpp` 🆕 | 환금 아이템·반납·떨어뜨리기, 금고·보물 순환, 상자, 무기·소비 아이템 줍기, 필드 아이템 생성 |
| `Game/WorldSkills.cpp` 🆕 | 스킬 3종, 소비 아이템 사용, 상태이상(경직·감속·은신·연막·섬광·제트팩), 퍽 효과 |
| `Game/GuardAI.cpp` | 경비원 상태 머신 (소리·끄집어내기·운반 제거, 사격·느린 회전·타겟 규칙·경직 추가) |
| `Game/Sight.cpp` | 선분 ↔ 충돌체 **처음 맞은 거리**, 선분·부채꼴 ↔ 원기둥 판정 |
| `Interface/IRecordStore.h` 🆕 | 최고 기록 읽기·쓰기, 비동기 콜백 (A-21) |
| `GameServer/Db/DbConnection.cpp` 🆕 | ODBC 연결·재연결, 매개변수 바인딩 쿼리 |
| `GameServer/Db/SqlRecordStore.cpp` 🆕 | `IRecordStore`의 SQL Server 구현. DB 전용 JobQueue에서 실행 |
| `tools/db/create_records.sql` 🆕 | 데이터베이스·테이블 만들기 스크립트 (SSMS에서 실행) |

**DB 설계 (SQL Server)**

```sql
CREATE TABLE dbo.PlayerRecord (
    nickname    NVARCHAR(16) NOT NULL PRIMARY KEY,   -- 플레이어 구분 (F-08)
    best_score  BIGINT       NOT NULL,                -- 최고 반납 금액
    character_id SMALLINT    NOT NULL,                -- 기록을 세운 캐릭터
    updated_at  DATETIME2    NOT NULL DEFAULT SYSUTCDATETIME()
);
```

- 갱신은 한 문장으로: 행이 없으면 넣고, 있으면 `best_score < @score`일 때만 바꿈 (`MERGE` 또는 `UPDATE … ; IF @@ROWCOUNT = 0 INSERT`). 갱신됐는지를 결과로 돌려줌.
- 게임 시작 때 참가자의 지난 최고 기록을 읽어 두고, 끝날 때 점수를 저장해 `best_record`·`new_record`를 채움.
- 쿼리는 모두 매개변수 바인딩 (닉네임을 SQL 문자열에 붙이지 않음).

**Update 순서** (한 틱, 50ms)
```
① 만료: 상태이상, 무적, 끈끈이·연막, 레일건·제트팩
② 경비원 AI (회전·이동·사격 판정 예약)
③ 플레이어: SP·탈진 → 상호작용 진행 → 무기 발사(누르고 있으면 간격마다) → 레일건 틱 → 제트팩 충돌 → 스킬 진행
④ 투척물 비행·폭발, 지뢰 감지·폭발, 필드 아이템 닿기
⑤ 대미지 반영 → 쓰러짐 처리 (떨어질 아이템은 모아 둠)
⑥ 리스폰, 반납 지점·금고·상자·필드 아이템 생성
⑦ 모아 둔 엔티티 생성·삭제, 바뀐 상태 표시(스냅샷용 dirty)
```

---

## 6. 작업 목록

순서대로 진행합니다. 각 단계가 끝나면 빌드·`ContentTests` 통과 후 커밋합니다 (머리말 `[규칙]`/`[AI]`/`[프로토콜]`, 본문에 작업·명세 번호).

### M0. 정리와 프로토콜 v3 뼈대

| 작업 | 내용 | 명세 | 상태 |
|---|---|---|---|
| T-01 | 제거: 숨는 곳, 포획·운반·감금·구출, 탈출구·탈출, 소리 추격·의심·끄집어내기, 무게, 인벤토리 슬롯, 팀 목표 금액, 손전등, 무기 없는 기본 공격, 보물 10종 (코드·시험). (새 맵에서는 맵 파일에서도 없어짐, T-73) | §16, O-32, O-41~43, N-17, N-18, P-06, M-04, C-03 | ✅ |
| T-02 | 프로토콜 v3.0: §3 enum·상태 비트 32비트·구조체·패킷 ID·`MAX_ENTITIES` 128. 콘텐츠 `Types.h`/`Messages.h`, 브리지 인코더·디코더, `FakeEncoder`, DummyClient를 함께 맞춤. 게임 시간 5분 | §3, G-04 | ✅ |
| T-03 | 스냅샷: 움직이는 엔티티 + 상태 바뀐 오브젝트만 | §3.3 | ✅ |
| T-04 | 캐릭터 3종(도적1~3)·퍽 7종 ID 범위, 캐릭터 → 스킬 표 | F-02, CH-01~05, A-06 | ✅ |
| T-05 | 시험 도우미(`MakeWorld`/`Run`/`Teleport`/`Act`)를 공용 헤더로 | §7 | ✅ |

### M1. HP·전투 기반

| 작업 | 내용 | 명세 | 상태 |
|---|---|---|---|
| T-10 | 플레이어·경비원 HP, `ApplyDamage`, `SC_DAMAGE`, 보호막·가시회생 | P-01, C-04~06, K-01, K-02, N-05 | ✅ |
| T-11 | 공격 꾹 누르기: `AttackStart/Stop`, 무기 간격마다 발사, SP 소비 없음, PvP, 삼단봉 근접 120° | C-01~03, W-11, Q-08, Q-34 | ✅ |
| T-12 | 쓰러짐·리스폰 대기(5/10초)·방 안 무작위 리스폰·5초 무적·리스폰 상태 초기화, `SC_DOWNED`/`SC_RESPAWNED` | D-01, D-05~07, A-17 | ✅ |
| T-13 | 경비원 HP 0 → 자가 수리 5초(무적·정지) → HP 200 | N-06 | ✅ |
| T-14 | `SC_PLAYER_STATUS` (바뀔 때만) | U-01, U-03 | ✅ |
| T-15 | 판정 도구: 히트 박스, 직선 처음 맞은 대상, 부채꼴, 벽 막힘, 무제한 사거리 계산 | C-07, A-10~12 | ✅ |

### M2. 돈·금고·보물

| 작업 | 내용 | 명세 | 상태 |
|---|---|---|---|
| T-20 | 환금 아이템 품질 4단계·가치 무작위, 들고 있는 가치, 개인 반납 금액, `SC_MONEY`, F로 즉시 줍기(3m) | M-01, M-02, M-05, I-01~03, A-01 | ✅ |
| T-21 | 반납 지점: 개인 금액으로 반납, `timer_ms`로 남은 수명 | O-21~25 | ✅ |
| T-22 | 쓰러짐 손실: 플레이어에게 → 환금 아이템(보물 포함) 떨어뜨림·−10%, 경비원에게 → 환금 아이템 소멸·보물만 떨어뜨림·−40%. 삼단봉 외 무기·소비 아이템 소멸 | D-02~04, D-08 | ✅ |
| T-23 | 금고 1개(방 안 무작위, 직전 방 제외), 10초 × 작업 속도, 진행도 유지, 한 명씩, 1m 작업, `SC_SAFE_OPENED` | O-01~04, O-07, IN-02, A-05 | ✅ |
| T-24 | 보물: 열린 금고에서 줍기 → 금고 사라짐 + `SC_TREASURE_TAKEN` 전원. 반납 말고는 사라지지 않음. 반납되면 직전 자리 빼고 금고 생성, 가치 복리 상승 | O-05~07, I-21~24, A-24 | ✅ |

### M3. 경비원 개편

| 작업 | 내용 | 명세 | 상태 |
|---|---|---|---|
| T-30 | 속도: 탐색 210·추격 420cm/s. 몸 회전 90°/s | N-03, N-04 | ✅ |
| T-31 | 감지·타겟: 시야 15m·좌우 45°·벽, 처음 발견한 플레이어 유지, 동시면 가까운 쪽, 공격받으면 공격자로 바꾸고 시야에서 놓칠 때까지 고정. 은신·연막 안·쓰러짐·리스폰 무적은 제외 | N-11~16, A-25 | ✅ |
| T-32 | 기관총: 타겟이 공격 범위(좌우 30°·15m) 안에 보이면 추격 속도로 쫓으면서 100ms마다 타겟 방향 ±30° 안 무작위 방향으로 10 대미지 직선 판정 (난사, 처음 맞은 플레이어(상자·경비원은 지나감), 벽에 막힘) | 12.2, A-18, Q-07 | ✅ |
| T-33 | 상태: Patrol·Chase·Attack·Search·Repairing·Stunned, `AlertLevel` 정리 | N-19 | ✅ |
| T-34 | 경직: 섬광 2초·제트팩 1초·테이저·감전 2초 동안 정지 | N-08 | ✅ |

### M4. 무기

| 작업 | 내용 | 명세 | 상태 |
|---|---|---|---|
| T-40 | 무기 칸 2개: 리스폰 [삼단봉, 빈 칸], 빈 칸에 줍기, 꽉 차면 무기1과 교체(옛 무기는 그 자리, 삼단봉은 사라짐), 탄 소진 시 소멸 → 삼단봉으로 채움(최대 1개), `SwitchWeapon` | W-01~10, A-22 | ✅ |
| T-41 | 근접 3종 (삼단봉·도검·빔 소드 6회) | W-11, W-15, W-16 | ✅ |
| T-42 | 권총·테이저건 (직선 단일, 벽 막힘) | W-12, W-13 | ✅ |
| T-43 | 화염방사기 (부채꼴 6m) | W-14, A-13 | ✅ |
| T-44 | 레일건 (빔 폭 50cm·벽 무시·1초 충전 뒤 6회 판정·2초 잠금, 2026-11-22 충전 추가) | W-17, E-10 | ✅ |
| T-45 | 수류탄 (투척물·충돌·폭발 감쇠·던진 사람 제외), `SC_PROJECTILE`/`SC_EXPLOSION` | W-18, A-14, A-15 | ✅ |
| T-46 | 폭발·감전 지뢰 (설치·3초 작동·감지·경고·폭발, 설치자·경비원 포함) | W-19, W-20, Q-26 | ✅ |

### M5. 상자·소비·필드 아이템

| 작업 | 내용 | 명세 | 상태 |
|---|---|---|---|
| T-50 | 상자: 대미지 20 누적 또는 3초 상호작용(멈추면 초기화), 드롭 표, 재생성 30~60초 | O-11~14, A-02 | ✅ |
| T-51 | 소비 아이템 1칸(교체 A-03)·E 사용: 회복 키트, 연막탄, 섬광탄(사용자 제외), 제트팩(`SC_KNOCKBACK`) | I-31~34, E-07~09, A-19 | ✅ |
| T-52 | 필드 아이템: 시작 5개·20초마다·최대 20, 닿으면 회복, HP 100이면 안 먹음 | I-11~16, A-04 | ✅ |

### M6. 스킬·퍽·상태이상

| 작업 | 내용 | 명세 | 상태 |
|---|---|---|---|
| T-60 | 캐릭터 고유 스킬, 쿨타임(발동 직후 / 은신은 해제 때)·리스폰 초기화, `SC_SKILL` | F-02, §9, A-06 | ✅ |
| T-61 | 사제 테이저건(Q로 들기 → 공격 키로 발사, 3m 단일, 2초 경직) · 끈끈이(1초 뒤 뒤 1m, 반경 3m, 10초, 설치자 포함 감속 50%) | S-01, S-02, E-01, E-02 | ✅ |
| T-62 | 은신: 20초, 다시 누르면 해제, 이동 80% 감속, 특수 동작 불가, 공격받음·공격·상호작용 시 해제, 경비원 감지 제외 | S-03, E-03, Q-30 | ✅ |
| T-63 | 퍽 7종: 방탄조끼·가시회생(리스폰 때 다시 충전)·유용한 도구·운동화. 정보 퍽 3종은 클라 표시(서버는 퍽 목록과 위치를 모두 보냄) | K-01~07, U-05, Q-15 | ✅ |
| T-64 | 이동 속도 효과: 플레이어 이동은 클라가 하므로 감속·은신·운동화는 **클라가 적용**, 서버는 상태 비트만. 경비원 감속(끈끈이)은 서버가 적용 | E-02, E-03, K-04 | ✅ |

### M7. 종료·결과·최고 기록

| 작업 | 내용 | 명세 | 상태 |
|---|---|---|---|
| T-70 | 시간 종료로 끝, 반납 금액 순위(동점 같은 순위), `PlayerResult` §3.5 | G-07, F-05, F-06, U-08, A-20 | ✅ |
| T-71 | 최고 기록: `IRecordStore`, 닉네임별 최고 점수 비교·갱신, 결과에 갱신 여부 | F-08, A-21 | ✅ |
| T-72 | SQL Server 구현: ODBC 연결, DB 스레드, `create_records.sql`, 설정 파일, 연결 실패 시 게임 계속 | A-21 | ✅ |
| T-73 | 새 맵: `Mansion_1F_Collision.json` → `tools/collision_to_map.py` → `mansion_1f_server.json`(방은 직사각형 여러 개). 맵에서 옛 항목(금고 후보·숨는 곳·감금·탈출구) 제거, 금고 방 안 무작위 | Q-38, A-05, A-26, A-27 | ✅ |
| T-74 | Q-39~45 반영: 의적 시작(서로 다른 방)·리스폰 방 안 무작위, 첫 금고는 의적 시작 방 제외, 공격받으면 상호작용 중단, 경비원 탄은 상자 통과. 은신 중 상호작용→은신 해제·동점 순위는 이미 그대로 | F-09, D-06, O-01, IN-05, N-09, S-03, G-07, A-26 | ✅ |

---

## 6.1 개발 결과 (2026-11-11)

- M0~M7 전부 구현. `ContentTests` 152개 통과 (Debug·Release), Level 4 경고 없음.
- DummyClient `room`(12/12)·`phase`(12/12)로 실제 서버와 패킷 v3.0 왕복 확인.
- SQL Server Express(로컬)에 `tools/db/create_records.sql`로 DB를 만들고 서버 연결·기록 조회·"더 높을 때만 갱신" 문장을 확인.
- 빌드: 프로젝트는 v143(VS 2022) 그대로. VS 2026만 있으면 `/p:PlatformToolset=v145`로 빌드하거나 솔루션 대상 변경.
- 학습 자료: [docs/learn/index.html](learn/index.html) (서버 없이 브라우저로 여는 동작·코드 설명).

## 7. 시험 계획 (`Tests/ContentTests`)

| 단계 | 시험 파일 | 확인할 것 |
|---|---|---|
| M0 | 기존 시험 정리 | 지운 기능의 시험 삭제, 남은 시험 통과 |
| M1 | `CombatTests.cpp` 🆕 | 대미지 순서(무적 → 보호막 → HP → 가시회생), 꾹 누르기 간격, SP 안 줄어듦, 쓰러짐·리스폰 시간과 위치, 리스폰 무적, 경비원 수리 |
| M2 | `MoneyTests.cpp` 🆕 | 개인 반납, 손실 10%/40%, 떨어뜨림/소멸(보물은 항상 남음), 금고 직전 자리 제외, 보물 가치 2,000 → 2,400 → 2,880 → 3,460 → 4,160 |
| M3 | `GuardAITests.cpp` | 회전 90°/s, 공격 범위 30°/시야 45° 차이, 타겟 유지·공격자 전환·고정, 기관총 벽 막힘, 은신·연막 무시, 경직 |
| M4 | `WeaponTests.cpp` 🆕 | 무기 칸 규칙(W-03·W-06·W-09·W-10), 근접 120°, 직선 첫 대상, 레일건 벽 관통·6회, 수류탄·지뢰 감쇠, 설치자 피해 |
| M5 | `ItemTests.cpp` 🆕 | 상자 파괴(대미지·상호작용 초기화)·드롭·재생성, 소비 아이템 1개·교체, 섬광 사용자 제외, 제트팩 충돌 1회, 회복약 HP 100 안 먹음 |
| M6 | `SkillTests.cpp` 🆕 | 쿨타임 시작 시점, 끈끈이 설치 지연·설치자 감속, 은신 해제 조건, 퍽 효과 |
| M7 | `ResultTests.cpp` 🆕 | 순위·동점, 최고 기록 갱신/유지 (가짜 `IRecordStore`) |

---

## 8. 클라 팀에 전달할 것

- 프로토콜 v3.0 (§3 전체). v2.1 클라는 접속되지 않습니다.
- 숨길 정보(퍽 없는 사람의 경비원·플레이어 위치, 은신·연막 안의 대상)는 서버가 모두 보내므로 클라가 숨깁니다.
- 이동 속도 효과(감속·은신·운동화·레일건 잠금·제트팩 이동·밀려남)는 클라가 적용합니다.
- `CS_ACTION.aim`에 커서가 가리키는 바닥 지점을 넣어 주세요 (투척 조준).
- 맵: `Mansion_1F_Collision.json`을 `tools/collision_to_map.py`로 서버 맵으로 바꿈. **의적은 방 안 무작위에서 시작·리스폰**(A-26)하므로 클라는 `SC_GAME_START`·`SC_RESPAWNED`의 pos로 캐릭터를 옮겨야 함. **경비원 스폰은 서버 임시 값**이라 클라 팀 값을 받으면 스크립트 위쪽 `GUARD_SPAWNS`만 고치면 됨. 서버 맵 버전(`SC_GAME_LOAD.map_version`)은 **10** (옛 블록아웃 v2·v3과 구분, 스크립트의 `MAP_VERSION`) — 클라 `GameLevelMapVersion`과 같아야 함. 바깥 문 2곳은 서버에서 닫혀 있음(A-27).

## 9. 서버 실행 환경 (DB)

- SQL Server(Express 이상)와 SSMS 22 설치 → SSMS에서 `tools/db/create_records.sql` 실행.
- `ODBC Driver 18 for SQL Server` 설치.
- 실행 파일 옆 `server_config.json`에 접속 문자열 작성 (예: `Driver={ODBC Driver 18 for SQL Server};Server=localhost;Database=VaultHeist;Trusted_Connection=yes;TrustServerCertificate=yes;`). 이 파일은 커밋하지 않고 `server_config.example.json`만 커밋.

---

## 변경 이력

| 날짜 | 명세 기준 | 내용 |
|---|---|---|
| 2026-11-07 | 2026-11-07 | 처음 작성: 명세(기획 변경점) 반영, 가정 A-03~A-23, 작업 M0~M7 |
| 2026-11-11 | 2026-11-11 (Q-34) | 명세 전체 다시 반영: 조작·상호작용 3m/1m, 무기 10종·칸 규칙(삼단봉 채움), 상자·소비·필드 아이템, 보물 1종 복리, 경비원 수치·범위 그림, 캐릭터 고유 스킬, 승리 조건·최고 기록, 손전등·탈출 삭제. 가정을 A-01~A-06(명세 ❓)·A-10~A-25(구현 세부)로 다시 번호, 작업 M0~M7 다시 구성 (보류 없음) |
| 2026-11-11 | 2026-11-11 (Q-34) | 사용자 확인: A-10~A-25 그대로, A-18 확정. 최고 기록 DB를 SQL Server(SSMS 22)로 — A-21, §5 DB 설계, T-72, §9 추가 |
| 2026-11-11 | 2026-11-11 (Q-34) | 개발 완료: T-01~T-72 ✅, 6.1 개발 결과 추가 |
| 2026-11-11 | 2026-11-11 (경비원 이동 사격) | 기획 변경 반영: 경비원은 움직이면서 사격 — A-18·T-32·§4 기관총 수정, 서버 수정 완료 |
| 2026-11-13 | 2026-11-13 (기관총 난사) | 기관총 난사: 발마다 타겟 방향 ±30° 무작위 — T-32·§4 수정, 서버 수정 완료 |
| 2026-11-13 | 2026-11-13 (새 맵) | 새 맵(`Mansion_1F_Collision.json`) 사용, 금고·상자 방 안 무작위 — A-05 확정, A-26·A-27·T-73 추가, §4 금고, §8 |
| 2026-11-14 | 2026-11-14 (Q-39~45 답) | 의적 시작·리스폰 방 안 무작위, 공격받으면 상호작용 중단, 경비원 탄 상자 통과, 동점 순위 확정 — A-05·A-18·A-20·A-26, §3 `SC_GAME_LOAD`, §4, T-12·T-32 수정, T-74 추가, 서버 수정 완료 |
| 2026-11-16 | 2026-11-14 (변경 없음) | 클라 팀 요청: 새 맵 버전 2 → 10 (옛 블록아웃 맵과 번호가 겹치지 않게), §8 클라 안내 |
