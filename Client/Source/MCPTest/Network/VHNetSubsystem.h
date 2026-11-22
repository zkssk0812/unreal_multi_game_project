#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "Network/VHNetTypes.h"
#include "Network/VHPacket.h"
#include "Network/VHTransport.h"
#include "VHNetSubsystem.generated.h"

class UVHToastWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FVHOnPhaseChanged, EVHNetPhase, NewPhase, EVHNetPhase, OldPhase);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FVHOnRequestResult, bool, bSuccess, const FText&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVHOnMessage, const FText&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FVHOnSimpleEvent);
/** 게임 중 받은 서버 패킷(SC_GAME_START, 3010~, 4000~) 그대로. 게임 레벨의 UVHServerGameSubsystem 이 해석한다 */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FVHOnGamePacket, uint16 /*Type*/, const uint8* /*Data*/, int32 /*Size*/);

/** SC_GAME_LOAD 내용 (게임 레벨이 서버 판정 모드로 시작할 때 쓴다) */
struct FVHGameLoadInfo
{
	bool bValid = false;
	uint64 MatchId = 0;
	uint32 MapId = 0;
	uint32 MapVersion = 0;
	TMap<uint64, int32> SpawnIndexByPlayer;
};

/** SC_GAME_START 내용 */
struct FVHGameStartInfo
{
	bool bValid = false;
	uint64 StartServerTimeMs = 0;
	uint32 DurationMs = 0;
	uint8 TickHz = 20;
	uint16 SelfEntityId = 0;
	TArray<RobProto::EntitySpawn> Entities;
	double ReceivedSeconds = 0.0;		// FPlatformTime::Seconds
};

/**
 * 서버 연결의 주인. GameInstance 서브시스템이라 레벨이 바뀌어도 연결과 상태가 유지된다.
 *
 * - 연결/로그인 정보/방 목록/방 상태/게임 결과를 보관하고, UI는 이 값만 보고 화면을 그린다.
 * - 받은 S_ 패킷은 게임 스레드 Tick에서 해석하고 델리게이트로 UI에 알린다(디스패처).
 * - 현재 단계(Phase)를 추적해 그 단계에서 허용된 C_ 패킷만 보낸다(서버는 위반 시 연결을 끊음).
 * - 모든 연결 단계에서 2초마다 CS_PING (하트비트 + RTT 측정).
 */
UCLASS()
class UVHNetSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	static UVHNetSubsystem* Get(const UObject* WorldContext);

	// ───────── 요청 (UI에서 호출) ─────────

	/** 서버에 연결하고 CS_LOGIN. 결과는 OnLoginResult */
	UFUNCTION(BlueprintCallable, Category = "VaultHeist|Net")
	void ConnectAndLogin(const FString& Name);

	/** 연결 종료(로그아웃). OnConnectionLost는 호출되지 않는다 */
	UFUNCTION(BlueprintCallable, Category = "VaultHeist|Net")
	void Disconnect();

	UFUNCTION(BlueprintCallable, Category = "VaultHeist|Net")
	bool RequestRoomList(int32 Page = 0);

	/** 방 이름만 보낸다 (맵·난이도 선택은 프로토콜에 없음) */
	UFUNCTION(BlueprintCallable, Category = "VaultHeist|Net")
	bool CreateRoom(const FString& RoomName);

	UFUNCTION(BlueprintCallable, Category = "VaultHeist|Net")
	bool JoinRoom(int64 RoomId);

	/** 빠른 찾기: 방 목록 → 들어갈 수 있는 방에 차례로 입장 시도 → 없으면 기본 설정으로 방 만들기 */
	UFUNCTION(BlueprintCallable, Category = "VaultHeist|Net")
	bool QuickFind();

	UFUNCTION(BlueprintCallable, Category = "VaultHeist|Net")
	bool LeaveRoom();

	UFUNCTION(BlueprintCallable, Category = "VaultHeist|Net")
	bool SelectCharacter(int32 CharacterId);

	UFUNCTION(BlueprintCallable, Category = "VaultHeist|Net")
	bool SelectPerks(const TArray<int32>& Perks);

	UFUNCTION(BlueprintCallable, Category = "VaultHeist|Net")
	bool SetReady(bool bReady);

	/** 결과창에서 로비로 (CS_RETURN에는 대기실/로비 선택이 없다 → 서버가 SC_LEAVE_ROOM(GameEnded), 방은 사라진다) */
	UFUNCTION(BlueprintCallable, Category = "VaultHeist|Net")
	bool ReturnFromResult();

	// ───────── 게임 중 (서버 판정) ─────────

	/** CS_MOVE: Pos = 발 위치(cm), Yaw = 몸 방향(도, 0 = +X), Flags = MoveFlag. 보낸 seq 를 돌려준다(실패 0) */
	uint16 SendMove(const FVector& Pos, float Yaw, uint8 Flags);
	/** CS_ACTION. Yaw = 시선(커서) 방향, Aim = 커서가 가리키는 바닥 지점(투척 조준, 없으면 0). 보낸 seq 를 돌려준다(실패 0) */
	uint16 SendAction(RobProto::ActionType Action, float Yaw, uint16 TargetEntityId, const FVector& Aim = FVector::ZeroVector);
	/** 서버(실서버 또는 가짜 서버)와 게임 중인가 → 게임 레벨은 서버 패킷으로 화면을 그린다. 서버 없이 레벨을 직접 ▶ 하면 false */
	bool IsServerAuthoritativeGame() const { return Phase == EVHNetPhase::Game && Transport.IsValid(); }
	const FVHGameLoadInfo& GetGameLoadInfo() const { return GameLoadInfo; }
	const FVHGameStartInfo& GetGameStartInfo() const { return GameStartInfo; }
	/** 서버 시각(ms) 추정 = 클라 시각 + PONG으로 잰 차이 */
	int64 EstimateServerTimeMs() const { return static_cast<int64>(ClientTimeMs()) + ServerTimeOffsetMs; }
	FVHOnGamePacket OnGamePacket;

	/**
	 * 실서버/가짜 서버 전환 — 다음 로그인부터 적용. 설정 파일(DefaultGame.ini)은 바꾸지 않는다.
	 *   메인화면 [실서버로 전환] / [가짜 서버로], 콘솔 vh.UseServer [host[:port]] / vh.UseMock,
	 *   실행 옵션 -VHServer[=host[:port]] / -VHMock.
	 * 콘솔 명령은 모듈이 올라올 때 등록되므로 에디터에서 ▶ 플레이 전에도 쓸 수 있다.
	 */
	static void UseServer(const FString& HostPort = FString());
	static void UseMock();
	/** "host", "host:port" 해석. 형식이 틀리면 false */
	static bool ParseHostPort(const FString& In, FString& OutHost, int32& OutPort);
	/** 서버 모드가 바뀌었을 때 (메인화면 문구 갱신·알림용) */
	static FSimpleMulticastDelegate& OnServerModeChanged();
	bool WillUseMock() const;
	FString GetServerHost() const;
	int32 GetServerPort() const;
	/** 클라 프로토콜 버전 문구, 예: "v3.0 (0x00030000)" — 서버 시작 로그의 protocol 0x... 와 비교 */
	static FString GetProtocolVersionText();
	/** 이 PC에 저장된 마지막 실서버 주소 (메인화면 입력칸 기본값) */
	FString LoadLastServerAddress() const;
	void SaveLastServerAddress(const FString& Address) const;

	/** 개발용: 서버 없이 샘플 결과창을 띄운다 (콘솔: vh.ShowSampleResult) */
	UFUNCTION(BlueprintCallable, Category = "VaultHeist|Dev")
	void ShowSampleResult();

	/** 화면 위쪽에 잠깐 떴다 사라지는 알림 */
	UFUNCTION(BlueprintCallable, Category = "VaultHeist|UI")
	void ShowToast(const FText& Message, bool bIsError = false);

	// ───────── 상태 조회 ─────────

	UFUNCTION(BlueprintPure, Category = "VaultHeist|Net") EVHNetPhase GetPhase() const { return Phase; }
	UFUNCTION(BlueprintPure, Category = "VaultHeist|Net") bool IsConnected() const { return Phase >= EVHNetPhase::Anonymous; }
	UFUNCTION(BlueprintPure, Category = "VaultHeist|Net") bool IsUsingMock() const { return bUsingMock; }
	UFUNCTION(BlueprintPure, Category = "VaultHeist|Net") FString GetMyName() const { return MyName; }
	UFUNCTION(BlueprintPure, Category = "VaultHeist|Net") int64 GetMyAccountId() const { return static_cast<int64>(MyAccountId); }
	UFUNCTION(BlueprintPure, Category = "VaultHeist|Net") FVHPlayerStats GetMyStats() const { return MyStats; }
	UFUNCTION(BlueprintPure, Category = "VaultHeist|Net") const TArray<FVHRoomSummary>& GetRoomList() const { return RoomList; }
	UFUNCTION(BlueprintPure, Category = "VaultHeist|Net") int32 GetRoomListTotal() const { return RoomListTotal; }
	UFUNCTION(BlueprintPure, Category = "VaultHeist|Net") const FVHRoomState& GetRoomState() const { return RoomState; }
	UFUNCTION(BlueprintPure, Category = "VaultHeist|Net") const FVHGameEndInfo& GetLastGameEnd() const { return LastGameEnd; }
	UFUNCTION(BlueprintPure, Category = "VaultHeist|Net") bool HasPendingResult() const { return bResultPending && LastGameEnd.bValid; }
	UFUNCTION(BlueprintPure, Category = "VaultHeist|Net") int32 GetRttMs() const { return RttMs; }
	UFUNCTION(BlueprintPure, Category = "VaultHeist|Net") bool IsHost() const;
	UFUNCTION(BlueprintPure, Category = "VaultHeist|Net") bool IsRequestInFlight() const { return bRequestInFlight || QuickFindStep != EQuickFindStep::None; }
	UFUNCTION(BlueprintPure, Category = "VaultHeist|Net") FString GetTransportDescription() const;

	const FVHRoomMember* GetMyMember() const { return RoomState.FindMember(static_cast<int64>(MyAccountId)); }

	/** 결과창을 닫을 때(로컬 샘플 등) */
	void ClearPendingResult() { bResultPending = false; }

	/** 로그인 이름 규칙: 영문자만 2~12자. 위반이면 OutReason에 안내 문구 */
	static bool ValidateLoginName(const FString& Name, FText& OutReason);
	static bool IsMenuWorld(const UWorld* World);

	FString LoadLastLoginName() const;

	// ───────── 이벤트 ─────────

	UPROPERTY(BlueprintAssignable) FVHOnPhaseChanged OnPhaseChanged;
	/** 로그인 성공/실패 (연결 실패 포함) */
	UPROPERTY(BlueprintAssignable) FVHOnRequestResult OnLoginResult;
	/** 로그인 후 예기치 않게 끊김(SC_KICK, 서버 종료, 네트워크 오류) */
	UPROPERTY(BlueprintAssignable) FVHOnMessage OnConnectionLost;
	UPROPERTY(BlueprintAssignable) FVHOnSimpleEvent OnRoomListUpdated;
	/** 방 만들기/입장/빠른 찾기 결과 */
	UPROPERTY(BlueprintAssignable) FVHOnRequestResult OnRoomRequestResult;
	/** 진행 중 안내(빠른 찾기 단계 등) */
	UPROPERTY(BlueprintAssignable) FVHOnMessage OnStatusMessage;
	UPROPERTY(BlueprintAssignable) FVHOnSimpleEvent OnRoomStateChanged;
	UPROPERTY(BlueprintAssignable) FVHOnMessage OnRoomError;
	UPROPERTY(BlueprintAssignable) FVHOnMessage OnLeftRoom;
	UPROPERTY(BlueprintAssignable) FVHOnSimpleEvent OnGameEnd;

private:
	enum class EQuickFindStep : uint8 { None, WaitingList, Joining, Creating };

	bool Tick(float DeltaSeconds);
	void SetPhase(EVHNetPhase NewPhase);
	void ResetSessionState();
	void DestroyTransport();
	void HandleTransportClosed(const FString& Reason);
	void OnPostLoadMap(UWorld* World);
	void OpenMenuLevel();
	void OpenGameLevel();

	// 보내기
	bool CanSend(RobProto::PacketId Id) const;
	bool PassRateLimit();
	bool SendBytes(RobProto::PacketId Id, const TArray<uint8>& Bytes);
	template <typename T>
	bool SendPacket(const T& Packet) { return SendBytes(Packet.type, VHPacket::ToBytes(Packet)); }
	void SendPing();

	// 받기 (디스패처)
	void OnBytes(const uint8* Data, int32 Num);
	void HandlePacket(const uint8* Data, int32 Size);
	void HandleLogin(const RobProto::SC_Login& P);
	void HandleRoomList(const RobProto::SC_RoomList& P);
	void HandleCreateRoom(const RobProto::SC_CreateRoom& P);
	void HandleJoinRoom(const RobProto::SC_JoinRoom& P);
	void HandleLeaveRoom(const RobProto::SC_LeaveRoom& P);
	void HandleRoomState(const RobProto::SC_RoomState& P);
	void HandleRoomError(const RobProto::SC_RoomError& P);
	void HandleGameLoad(const RobProto::SC_GameLoad& P);
	void HandleGameStart(const RobProto::SC_GameStart& P);
	void HandleGameEnd(const RobProto::SC_GameEnd& P);
	void HandlePong(const RobProto::SC_Pong& P);
	void HandleKick(const RobProto::SC_Kick& P);
	void HandleNotice(const RobProto::SC_Notice& P);

	// 빠른 찾기
	void QuickFindOnList();
	void QuickFindTryNext();
	void QuickFindCreate();
	void QuickFindFinish(bool bSuccess, const FText& Message);

	void SaveLastLoginName(const FString& Name) const;
	uint64 ClientTimeMs() const;

	TUniquePtr<IVHTransport> Transport;
	TUniquePtr<IVHTransport> TransportToDestroy;	// 콜백 도중 파괴하지 않도록 다음 Tick에 정리
	FVHPacketAssembler Assembler;
	FTSTicker::FDelegateHandle TickHandle;
	FDelegateHandle PostLoadMapHandle;
	IConsoleObject* SampleResultCommand = nullptr;
	FDelegateHandle ServerModeChangedHandle;
	void ApplyCommandLineOverrides();
	void HandleServerModeChanged();

	// 개발용 자동 진행(봇): 로그인 → 빠른 찾기 → 캐릭터·퍽 → 준비. 게임이 끝나 로비로 오면 다시 빠른 찾기.
	// 콘솔 vh.Bot.Start <이름> / vh.Bot.Stop, 실행 옵션 -VHBot=<이름>
	IConsoleObject* BotStartCommand = nullptr;
	IConsoleObject* BotStopCommand = nullptr;
	void CmdBotStart(const TArray<FString>& Args);
	void TickBot();
	bool bBot = false;
	FString BotName;
	int32 BotMinPlayers = 1;		// 방 인원이 이만큼 모여야 준비 (vh.Bot.Start 이름 인원, -VHBotMin=2)
	double BotNextActionTime = 0.0;
public:
	bool IsBot() const { return bBot; }
private:

	EVHNetPhase Phase = EVHNetPhase::Disconnected;
	bool bUsingMock = true;
	FString PendingLoginName;
	FString KickMessage;

	FString MyName;
	uint64 MyAccountId = 0;
	FVHPlayerStats MyStats;

	TArray<FVHRoomSummary> RoomList;
	int32 RoomListTotal = 0;
	FVHRoomState RoomState;
	FVHRoomState LastGameRoomState;		// 결과창 이름 매칭용 (게임 직전 방 상태)
	FVHGameEndInfo LastGameEnd;
	bool bResultPending = false;
	bool bRequestInFlight = false;		// 방 만들기/입장 응답 대기
	bool bLeaveRequested = false;
	bool bReturnRequested = false;
	bool bAwaitingLoadComplete = false;
	FVHGameLoadInfo GameLoadInfo;
	FVHGameStartInfo GameStartInfo;
	uint16 InputSeq = 0;				// CS_MOVE·CS_ACTION 공용 순번

	EQuickFindStep QuickFindStep = EQuickFindStep::None;
	TArray<int64> QuickFindCandidates;

	double StartSeconds = 0.0;
	double NextPingTime = 0.0;
	double LastReceiveTime = 0.0;
	int32 RttMs = -1;
	int64 ServerTimeOffsetMs = 0;
	TArray<double> RecentRequestTimes;	// 로비·방 요청 초당 제한용

	TWeakObjectPtr<UVHToastWidget> Toast;
};
