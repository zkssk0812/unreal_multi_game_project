#include "Network/VHNetSubsystem.h"

#include "Network/VHSettings.h"
#include "Network/VHTcpTransport.h"
#include "Network/VHMockTransport.h"
#include "Game/VHGameData.h"
#include "Game/VHLocalSaveGame.h"
#include "Gameplay/VHGameplayConfig.h"
#include "UI/VHCodeWidgets.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogVHNet, Log, All);

// 실서버/가짜 서버 전환: 설정 파일(DefaultGame.ini)을 고치지 않고 콘솔·실행 옵션으로 바꾼다.
//   콘솔: vh.UseServer [host[:port]]  /  vh.UseMock      (다음 로그인부터 적용)
//   실행 옵션: -VHServer  또는 -VHServer=192.168.0.10:9000  /  -VHMock
static TAutoConsoleVariable<int32> CVarVHNetMode(TEXT("vh.Net.Mode"), -1,
	TEXT("-1 = 프로젝트 설정(bUseMockServer) / 0 = 실서버(TCP) / 1 = 가짜 서버(Mock)"), ECVF_Default);
static TAutoConsoleVariable<FString> CVarVHNetHost(TEXT("vh.Net.Host"), TEXT(""),
	TEXT("실서버 주소 (비우면 프로젝트 설정 ServerHost)"), ECVF_Default);
static TAutoConsoleVariable<int32> CVarVHNetPort(TEXT("vh.Net.Port"), 0,
	TEXT("실서버 포트 (0이면 프로젝트 설정 ServerPort)"), ECVF_Default);

// 전환 명령은 모듈이 올라올 때 등록한다 → 에디터에서 ▶ 플레이 전에 입력해도 "명령 없음"이 나오지 않는다.
// (예전에는 게임 인스턴스가 생길 때 등록해서 플레이 전에는 명령이 없었다)
static FAutoConsoleCommand GVHUseServerCommand(TEXT("vh.UseServer"),
	TEXT("실서버(TCP)로 전환. 인자: [host[:port]] (예: vh.UseServer 192.168.0.10:9000). 다음 로그인부터 적용. 플레이 전·중 모두 가능"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		FString HostPort = Args.Num() > 0 ? Args[0] : FString();
		if (Args.Num() > 1 && !HostPort.Contains(TEXT(":")))
		{
			HostPort += TEXT(":") + Args[1];
		}
		UVHNetSubsystem::UseServer(HostPort);
	}));
static FAutoConsoleCommand GVHUseMockCommand(TEXT("vh.UseMock"),
	TEXT("가짜 서버(Mock)로 전환. 다음 로그인부터 적용"),
	FConsoleCommandDelegate::CreateLambda([]() { UVHNetSubsystem::UseMock(); }));

using namespace RobProto;

namespace
{
	constexpr int32 RequestLimitPerSecond = 8;	// 서버 제한(초당 10회)보다 조금 낮게
	constexpr double ReceiveTimeoutSeconds = 15.0;	// 이 시간 동안 아무것도 못 받으면 끊긴 것으로 본다

	EVHRoomState ToRoomState(RoomState S) { return static_cast<EVHRoomState>(FMath::Clamp<uint8>(static_cast<uint8>(S), 0, 3)); }
}

// ───────────────────────── 수명 ─────────────────────────

void UVHNetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	StartSeconds = FPlatformTime::Seconds();

	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UVHNetSubsystem::Tick));
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UVHNetSubsystem::OnPostLoadMap);

	if (!IConsoleManager::Get().FindConsoleObject(TEXT("vh.ShowSampleResult")))
	{
		SampleResultCommand = IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("vh.ShowSampleResult"),
			TEXT("개발용: 서버 없이 샘플 결과창을 띄운다"),
			FConsoleCommandDelegate::CreateUObject(this, &UVHNetSubsystem::ShowSampleResult),
			ECVF_Default);
	}
	ServerModeChangedHandle = OnServerModeChanged().AddUObject(this, &UVHNetSubsystem::HandleServerModeChanged);
	if (!IConsoleManager::Get().FindConsoleObject(TEXT("vh.Bot.Start")))
	{
		BotStartCommand = IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("vh.Bot.Start"),
			TEXT("개발용 자동 진행: vh.Bot.Start <영문 이름> → 로그인·빠른 찾기·캐릭터·퍽·준비를 알아서 한다"),
			FConsoleCommandWithArgsDelegate::CreateUObject(this, &UVHNetSubsystem::CmdBotStart),
			ECVF_Default);
		BotStopCommand = IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("vh.Bot.Stop"),
			TEXT("개발용 자동 진행 멈춤"),
			FConsoleCommandDelegate::CreateLambda([this]() { bBot = false; UE_LOG(LogVHNet, Display, TEXT("[Bot] 멈춤")); }),
			ECVF_Default);
	}
	ApplyCommandLineOverrides();
}

void UVHNetSubsystem::CmdBotStart(const TArray<FString>& Args)
{
	BotName = Args.Num() > 0 ? Args[0] : FString(TEXT("Bot"));
	BotMinPlayers = Args.Num() > 1 ? FMath::Clamp(FCString::Atoi(*Args[1]), 1, MAX_ROOM_PLAYERS) : 1;
	FParse::Value(FCommandLine::Get(), TEXT("-VHBotMin="), BotMinPlayers);
	bBot = true;
	BotNextActionTime = FPlatformTime::Seconds() + 0.5;
	UE_LOG(LogVHNet, Display, TEXT("[Bot] 시작: %s (%s), %d명 모이면 준비"), *BotName, *GetTransportDescription(), BotMinPlayers);
}

void UVHNetSubsystem::TickBot()
{
	const double Now = FPlatformTime::Seconds();
	if (!bBot || Now < BotNextActionTime)
	{
		return;
	}
	BotNextActionTime = Now + 0.8;
	switch (Phase)
	{
	case EVHNetPhase::Disconnected:
		UE_LOG(LogVHNet, Display, TEXT("[Bot] 로그인 %s"), *BotName);
		ConnectAndLogin(BotName);
		BotNextActionTime = Now + 3.0;
		break;
	case EVHNetPhase::Lobby:
		if (!IsRequestInFlight())
		{
			UE_LOG(LogVHNet, Display, TEXT("[Bot] 빠른 찾기"));
			QuickFind();
			BotNextActionTime = Now + 3.0;
		}
		break;
	case EVHNetPhase::Room:
	{
		const FVHRoomMember* Me = GetMyMember();
		if (!Me || RoomState.State != EVHRoomState::Waiting)
		{
			break;
		}
		// 실행 옵션 -VHBotChar=번호: 그 캐릭터를 고른다(스킬 시험: 1 테이저·2 끈끈이·3 은신). 이미 누가 골랐으면 빈 캐릭터
		// 실행 옵션 -VHBotPerks=4,5,6: 고를 퍽 (기본 1,2,3)
		static int32 WantCharacter = -1;
		static TArray<int32> WantPerks;
		if (WantCharacter < 0)
		{
			WantCharacter = 0;
			FParse::Value(FCommandLine::Get(), TEXT("-VHBotChar="), WantCharacter);
			FString PerkList;
			if (FParse::Value(FCommandLine::Get(), TEXT("-VHBotPerks="), PerkList, false))
			{
				TArray<FString> Parts;
				PerkList.ParseIntoArray(Parts, TEXT(","));
				for (const FString& Part : Parts)
				{
					WantPerks.AddUnique(FCString::Atoi(*Part));
				}
			}
			if (WantPerks.Num() == 0)
			{
				WantPerks = { 1, 2, 3 };
			}
		}
		if (Me->CharacterId == 0)
		{
			if (WantCharacter >= 1 && WantCharacter <= 3
				&& !RoomState.Members.ContainsByPredicate([](const FVHRoomMember& M) { return M.CharacterId == WantCharacter; }))
			{
				UE_LOG(LogVHNet, Display, TEXT("[Bot] 캐릭터 %d (지정)"), WantCharacter);
				SelectCharacter(WantCharacter);
				break;
			}
			for (int32 C = 1; C <= 3; ++C)	// 플레이어 캐릭터는 도적1~3 (서버는 1~5를 받음)
			{
				if (!RoomState.Members.ContainsByPredicate([C](const FVHRoomMember& M) { return M.CharacterId == C; }))
				{
					UE_LOG(LogVHNet, Display, TEXT("[Bot] 캐릭터 %d"), C);
					SelectCharacter(C);
					break;
				}
			}
		}
		else if (Me->Perks.Num() < FMath::Min<int32>(WantPerks.Num(), MAX_PERKS))
		{
			UE_LOG(LogVHNet, Display, TEXT("[Bot] 퍽 %s"), *FString::JoinBy(WantPerks, TEXT(","), [](int32 P) { return FString::FromInt(P); }));
			SelectPerks(WantPerks);
		}
		else if (!Me->bReady && RoomState.Members.Num() >= BotMinPlayers)
		{
			UE_LOG(LogVHNet, Display, TEXT("[Bot] 준비"));
			SetReady(true);
		}
		break;
	}
	default:
		break;
	}
}

void UVHNetSubsystem::ApplyCommandLineOverrides()
{
	FString Value;
	if (FParse::Value(FCommandLine::Get(), TEXT("-VHServer="), Value))
	{
		UseServer(Value);
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("VHServer")))
	{
		UseServer();
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("VHMock")))
	{
		UseMock();
	}
	FString Bot;
	if (FParse::Value(FCommandLine::Get(), TEXT("-VHBot="), Bot))
	{
		CmdBotStart({ Bot });
	}
}

FSimpleMulticastDelegate& UVHNetSubsystem::OnServerModeChanged()
{
	static FSimpleMulticastDelegate Delegate;
	return Delegate;
}

bool UVHNetSubsystem::ParseHostPort(const FString& In, FString& OutHost, int32& OutPort)
{
	FString Text = In.TrimStartAndEnd();
	OutPort = 0;
	FString PortStr;
	if (Text.Split(TEXT(":"), &OutHost, &PortStr))
	{
		OutHost.TrimStartAndEndInline();
		PortStr.TrimStartAndEndInline();
		if (!PortStr.IsNumeric())
		{
			return false;
		}
		OutPort = FCString::Atoi(*PortStr);
		if (OutPort <= 0 || OutPort > 65535)
		{
			return false;
		}
	}
	else
	{
		OutHost = Text;
	}
	// IP 또는 호스트 이름: 영문·숫자·점·하이픈만 (예: 127.0.0.1, 192.168.0.10, my-pc.local)
	if (OutHost.IsEmpty() || OutHost.Len() > 253)
	{
		return false;
	}
	for (const TCHAR C : OutHost)
	{
		if (!(FChar::IsAlnum(C) && C < 128) && C != TEXT('.') && C != TEXT('-'))
		{
			return false;
		}
	}
	return true;
}

void UVHNetSubsystem::UseServer(const FString& HostPort)
{
	if (!HostPort.IsEmpty())
	{
		FString Host;
		int32 Port = 0;
		if (ParseHostPort(HostPort, Host, Port))
		{
			CVarVHNetHost->Set(*Host, ECVF_SetByConsole);
			if (Port > 0)
			{
				CVarVHNetPort->Set(Port, ECVF_SetByConsole);
			}
		}
		else
		{
			// 잘못된 주소면 전환하지 않는다 (예전 모드·주소 그대로)
			UE_LOG(LogVHNet, Warning, TEXT("서버 주소 형식이 틀림: '%s' (예: vh.UseServer 127.0.0.1:9000) → 전환하지 않음"), *HostPort);
			return;
		}
	}
	CVarVHNetMode->Set(0, ECVF_SetByConsole);
	const FString Host = CVarVHNetHost.GetValueOnGameThread();
	const int32 Port = CVarVHNetPort.GetValueOnGameThread();
	UE_LOG(LogVHNet, Display, TEXT("서버 모드: 실서버 %s:%d (다음 로그인부터)"),
		Host.IsEmpty() ? *UVHSettings::Get().ServerHost : *Host, Port > 0 ? Port : UVHSettings::Get().ServerPort);
	OnServerModeChanged().Broadcast();
}

void UVHNetSubsystem::UseMock()
{
	CVarVHNetMode->Set(1, ECVF_SetByConsole);
	UE_LOG(LogVHNet, Display, TEXT("서버 모드: 가짜 서버(Mock) (다음 로그인부터)"));
	OnServerModeChanged().Broadcast();
}

void UVHNetSubsystem::HandleServerModeChanged()
{
	if (WillUseMock())
	{
		ShowToast(FText::FromString(TEXT("가짜 서버(Mock) 모드 (다음 로그인부터 적용)")));
	}
	else
	{
		ShowToast(FText::FromString(FString::Printf(TEXT("실서버 모드: %s:%d (다음 로그인부터 적용)"), *GetServerHost(), GetServerPort())));
	}
}

FString UVHNetSubsystem::GetProtocolVersionText()
{
	const uint32 V = PROTOCOL_VERSION;
	return FString::Printf(TEXT("v%u.%u (0x%08X)"), V >> 16, V & 0xFFFF, V);
}

FString UVHNetSubsystem::LoadLastServerAddress() const
{
	if (const UVHLocalSaveGame* Save = Cast<UVHLocalSaveGame>(UGameplayStatics::LoadGameFromSlot(UVHLocalSaveGame::SlotName, 0)))
	{
		return Save->LastServerAddress;
	}
	return FString();
}

void UVHNetSubsystem::SaveLastServerAddress(const FString& Address) const
{
	UVHLocalSaveGame* Save = Cast<UVHLocalSaveGame>(UGameplayStatics::LoadGameFromSlot(UVHLocalSaveGame::SlotName, 0));
	if (!Save)
	{
		Save = Cast<UVHLocalSaveGame>(UGameplayStatics::CreateSaveGameObject(UVHLocalSaveGame::StaticClass()));
	}
	if (Save)
	{
		Save->LastServerAddress = Address;
		UGameplayStatics::SaveGameToSlot(Save, UVHLocalSaveGame::SlotName, 0);
	}
}

bool UVHNetSubsystem::WillUseMock() const
{
	const int32 Mode = CVarVHNetMode.GetValueOnGameThread();
	return Mode < 0 ? UVHSettings::Get().bUseMockServer : Mode != 0;
}

FString UVHNetSubsystem::GetServerHost() const
{
	const FString Host = CVarVHNetHost.GetValueOnGameThread();
	return Host.IsEmpty() ? UVHSettings::Get().ServerHost : Host;
}

int32 UVHNetSubsystem::GetServerPort() const
{
	const int32 Port = CVarVHNetPort.GetValueOnGameThread();
	return Port > 0 ? Port : UVHSettings::Get().ServerPort;
}

void UVHNetSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
	if (SampleResultCommand)
	{
		IConsoleManager::Get().UnregisterConsoleObject(SampleResultCommand);
		SampleResultCommand = nullptr;
	}
	OnServerModeChanged().Remove(ServerModeChangedHandle);
	if (BotStartCommand)
	{
		IConsoleManager::Get().UnregisterConsoleObject(BotStartCommand);
		IConsoleManager::Get().UnregisterConsoleObject(BotStopCommand);
		BotStartCommand = nullptr;
		BotStopCommand = nullptr;
	}
	if (Transport)
	{
		Transport->Disconnect();
	}
	Transport.Reset();
	TransportToDestroy.Reset();
	Super::Deinitialize();
}

UVHNetSubsystem* UVHNetSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UVHNetSubsystem>() : nullptr;
}

uint64 UVHNetSubsystem::ClientTimeMs() const
{
	return static_cast<uint64>((FPlatformTime::Seconds() - StartSeconds) * 1000.0);
}

FString UVHNetSubsystem::GetTransportDescription() const
{
	if (Transport && bUsingMock)
	{
		return Transport->GetDescription();
	}
	return (Transport ? bUsingMock : WillUseMock()) ? TEXT("Mock 서버(가짜 서버)") : FString::Printf(TEXT("실서버 TCP %s:%d"), *GetServerHost(), GetServerPort());
}

bool UVHNetSubsystem::IsHost() const
{
	return RoomState.bValid && MyAccountId != 0 && RoomState.HostAccountId == static_cast<int64>(MyAccountId);
}

bool UVHNetSubsystem::IsMenuWorld(const UWorld* World)
{
	if (!World)
	{
		return false;
	}
	const FString MenuPackage = UVHSettings::Get().MenuLevel.GetLongPackageName();
	const FString WorldPackage = UWorld::RemovePIEPrefix(World->GetOutermost()->GetName());
	return !MenuPackage.IsEmpty() && WorldPackage == MenuPackage;
}

// ───────────────────────── Tick ─────────────────────────

bool UVHNetSubsystem::Tick(float DeltaSeconds)
{
	TransportToDestroy.Reset();

	if (Transport)
	{
		Transport->Tick(DeltaSeconds);
	}
	TickBot();

	if (IsConnected())
	{
		const double Now = FPlatformTime::Seconds();
		if (Now >= NextPingTime)
		{
			SendPing();
		}
		if (Now - LastReceiveTime > ReceiveTimeoutSeconds)
		{
			UE_LOG(LogVHNet, Warning, TEXT("%.0f초 동안 서버 응답 없음 → 연결 끊김 처리"), ReceiveTimeoutSeconds);
			if (Transport)
			{
				Transport->Disconnect();
			}
			HandleTransportClosed(TEXT("서버 응답이 없어 연결을 끊었습니다."));
		}
	}
	return true;
}

void UVHNetSubsystem::SetPhase(EVHNetPhase NewPhase)
{
	if (Phase == NewPhase)
	{
		return;
	}
	const EVHNetPhase Old = Phase;
	Phase = NewPhase;
	UE_LOG(LogVHNet, Log, TEXT("단계: %s → %s"),
		*StaticEnum<EVHNetPhase>()->GetNameStringByValue(static_cast<int64>(Old)),
		*StaticEnum<EVHNetPhase>()->GetNameStringByValue(static_cast<int64>(NewPhase)));
	OnPhaseChanged.Broadcast(NewPhase, Old);
}

void UVHNetSubsystem::ResetSessionState()
{
	Assembler.Reset();
	MyName.Reset();
	MyAccountId = 0;
	MyStats = FVHPlayerStats();
	RoomList.Reset();
	RoomListTotal = 0;
	RoomState = FVHRoomState();
	bRequestInFlight = false;
	bLeaveRequested = false;
	bReturnRequested = false;
	bAwaitingLoadComplete = false;
	GameLoadInfo = FVHGameLoadInfo();
	GameStartInfo = FVHGameStartInfo();
	QuickFindStep = EQuickFindStep::None;
	QuickFindCandidates.Reset();
	RttMs = -1;
	RecentRequestTimes.Reset();
	if (!LastGameEnd.bIsSample)
	{
		bResultPending = false;
	}
}

void UVHNetSubsystem::DestroyTransport()
{
	if (Transport)
	{
		Transport->OnConnected = nullptr;
		Transport->OnConnectFailed = nullptr;
		Transport->OnDisconnected = nullptr;
		Transport->OnBytesReceived = nullptr;
		Transport->Disconnect();
		TransportToDestroy = MoveTemp(Transport);
	}
}

// ───────────────────────── 연결·로그인 ─────────────────────────

bool UVHNetSubsystem::ValidateLoginName(const FString& Name, FText& OutReason)
{
	// 사용자 요구: 영문자(A–Z, a–z)만, 2~12자. (서버는 48바이트·한글까지 허용)
	for (const TCHAR C : Name)
	{
		const bool bAlpha = (C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z');
		if (!bAlpha)
		{
			if (C == ' ')
			{
				OutReason = FText::FromString(TEXT("공백은 쓸 수 없습니다. 영문자만 입력해 주세요."));
			}
			else if (C >= '0' && C <= '9')
			{
				OutReason = FText::FromString(TEXT("숫자는 쓸 수 없습니다. 영문자만 입력해 주세요."));
			}
			else
			{
				OutReason = FText::FromString(TEXT("영문자(A~Z, a~z)만 쓸 수 있습니다. 한글·특수문자는 안 됩니다."));
			}
			return false;
		}
	}
	if (Name.Len() < 2)
	{
		OutReason = FText::FromString(FString::Printf(TEXT("이름은 2~12자로 입력해 주세요. (현재 %d자)"), Name.Len()));
		return false;
	}
	if (Name.Len() > 12)
	{
		OutReason = FText::FromString(FString::Printf(TEXT("이름은 12자까지 입력할 수 있습니다. (현재 %d자)"), Name.Len()));
		return false;
	}
	OutReason = FText::FromString(FString::Printf(TEXT("사용할 수 있는 이름입니다. (%d/12자)"), Name.Len()));
	return true;
}

void UVHNetSubsystem::ConnectAndLogin(const FString& Name)
{
	FText Reason;
	if (!ValidateLoginName(Name, Reason))
	{
		OnLoginResult.Broadcast(false, Reason);
		return;
	}
	if (Phase != EVHNetPhase::Disconnected)
	{
		UE_LOG(LogVHNet, Warning, TEXT("이미 연결 중이라 로그인 요청을 무시합니다."));
		return;
	}

	DestroyTransport();
	ResetSessionState();
	PendingLoginName = Name;
	KickMessage.Reset();
	SaveLastLoginName(Name);

	const UVHSettings& S = UVHSettings::Get();
	bUsingMock = WillUseMock();
	if (bUsingMock)
	{
		FVHMockConfig Config;
		// 가짜 게임 길이 = 클라 설정의 제한 시간 (끝나면 Mock 서버가 샘플 결과를 보낸다)
		Config.GameSeconds = S.GetGameplayConfig().TimeLimitSeconds;
		Config.ResultScreenMs = S.MockResultScreenMs;
		Config.FakePlayersInNewRoom = S.MockFakePlayers;
		Config.FakeActionDelaySeconds = S.MockFakeActionDelaySeconds;
		Transport = MakeUnique<FVHMockTransport>(Config, S.bMockSplitPackets);
	}
	else
	{
		Transport = MakeUnique<FVHTcpTransport>(S.ConnectTimeoutSeconds);
	}

	Transport->OnConnected = [this]()
	{
		const double Now = FPlatformTime::Seconds();
		LastReceiveTime = Now;
		NextPingTime = Now;		// 접속 직후 바로 한 번 PING
		SetPhase(EVHNetPhase::Anonymous);

		CS_Login Login = VHPacket::Make<CS_Login>(PacketId::CS_LOGIN);
		Login.protocol_version = PROTOCOL_VERSION;
		VHPacket::WriteString(Login.name, sizeof(Login.name), PendingLoginName);
		if (SendPacket(Login))
		{
			SetPhase(EVHNetPhase::Authenticating);
		}
	};
	Transport->OnConnectFailed = [this](const FString& Why)
	{
		DestroyTransport();
		SetPhase(EVHNetPhase::Disconnected);
		OnLoginResult.Broadcast(false, FText::FromString(Why));
	};
	Transport->OnDisconnected = [this](const FString& Why)
	{
		HandleTransportClosed(Why);
	};
	Transport->OnBytesReceived = [this](const uint8* Data, int32 Num)
	{
		OnBytes(Data, Num);
	};

	UE_LOG(LogVHNet, Log, TEXT("연결 시작: %s, 이름 %s, 클라 프로토콜 %s"), *GetTransportDescription(), *Name, *GetProtocolVersionText());
	SetPhase(EVHNetPhase::Connecting);
	Transport->Connect(GetServerHost(), GetServerPort());
}

void UVHNetSubsystem::Disconnect()
{
	if (Phase == EVHNetPhase::Disconnected && !Transport)
	{
		return;
	}
	UE_LOG(LogVHNet, Log, TEXT("연결 종료(사용자 요청)"));
	DestroyTransport();
	ResetSessionState();
	SetPhase(EVHNetPhase::Disconnected);
}

void UVHNetSubsystem::HandleTransportClosed(const FString& Reason)
{
	const EVHNetPhase OldPhase = Phase;
	const FString Message = KickMessage.IsEmpty() ? Reason : KickMessage;
	UE_LOG(LogVHNet, Warning, TEXT("연결 끊김 (단계 %d): %s"), static_cast<int32>(OldPhase), *Message);

	DestroyTransport();
	ResetSessionState();
	SetPhase(EVHNetPhase::Disconnected);

	if (OldPhase <= EVHNetPhase::Authenticating)
	{
		// 로그인 도중 끊김 → 로그인 화면에서 안내
		OnLoginResult.Broadcast(false, FText::FromString(Message));
	}
	else
	{
		ShowToast(FText::FromString(Message), true);
		OnConnectionLost.Broadcast(FText::FromString(Message));
		if (!IsMenuWorld(GetWorld()))
		{
			OpenMenuLevel();
		}
	}
}

// ───────────────────────── 보내기 ─────────────────────────

bool UVHNetSubsystem::CanSend(PacketId Id) const
{
	if (Id == PacketId::CS_PING)
	{
		return Phase >= EVHNetPhase::Anonymous;
	}
	// 서버 protocol.h v2.1 "연결 단계별 허용 CS_ 패킷"과 같은 표 (그 외는 서버가 연결을 끊음)
	switch (Phase)
	{
	case EVHNetPhase::Anonymous:
		return Id == PacketId::CS_LOGIN;
	case EVHNetPhase::Lobby:
		return Id == PacketId::CS_ROOM_LIST || Id == PacketId::CS_CREATE_ROOM || Id == PacketId::CS_JOIN_ROOM;
	case EVHNetPhase::Room:
		return Id == PacketId::CS_LEAVE_ROOM || Id == PacketId::CS_SELECT_CHARACTER
			|| Id == PacketId::CS_SELECT_PERKS || Id == PacketId::CS_READY;
	case EVHNetPhase::Game:
		return Id == PacketId::CS_LOAD_COMPLETE || Id == PacketId::CS_MOVE || Id == PacketId::CS_ACTION || Id == PacketId::CS_RETURN;
	default:
		return false;
	}
}

bool UVHNetSubsystem::PassRateLimit()
{
	const double Now = FPlatformTime::Seconds();
	RecentRequestTimes.RemoveAll([Now](double T) { return Now - T > 1.0; });
	if (RecentRequestTimes.Num() >= RequestLimitPerSecond)
	{
		OnStatusMessage.Broadcast(FText::FromString(TEXT("요청이 너무 잦습니다. 잠시 후 다시 시도해 주세요.")));
		return false;
	}
	RecentRequestTimes.Add(Now);
	return true;
}

bool UVHNetSubsystem::SendBytes(PacketId Id, const TArray<uint8>& Bytes)
{
	if (!Transport || !Transport->IsConnected())
	{
		return false;
	}
	if (!CanSend(Id))
	{
		// 보내면 서버가 연결을 끊으므로 클라이언트에서 막는다(버그 신호이므로 경고 로그).
		UE_LOG(LogVHNet, Warning, TEXT("단계 %d에서 허용되지 않은 패킷 %s 전송을 막았습니다."),
			static_cast<int32>(Phase), VHPacket::PacketName(static_cast<uint16>(Id)));
		return false;
	}
	const bool bLobbyOrRoom = static_cast<uint16>(Id) >= 1000 && static_cast<uint16>(Id) < 3000 && Id != PacketId::CS_LOGIN;
	if (bLobbyOrRoom && !PassRateLimit())
	{
		return false;
	}
	if (Id != PacketId::CS_PING && Id != PacketId::CS_MOVE)
	{
		UE_LOG(LogVHNet, Log, TEXT("→ %s (%d bytes)"), VHPacket::PacketName(static_cast<uint16>(Id)), Bytes.Num());
	}
	return Transport->Send(Bytes);
}

void UVHNetSubsystem::SendPing()
{
	NextPingTime = FPlatformTime::Seconds() + PING_INTERVAL_MS / 1000.0;
	CS_Ping Ping = VHPacket::Make<CS_Ping>(PacketId::CS_PING);
	Ping.client_time_ms = ClientTimeMs();
	SendPacket(Ping);
}

bool UVHNetSubsystem::RequestRoomList(int32 Page)
{
	if (Phase != EVHNetPhase::Lobby)
	{
		return false;
	}
	// v2.1: 페이지 없음, 대기 중인 방 최대 20개
	return SendPacket(VHPacket::Make<CS_RoomList>(PacketId::CS_ROOM_LIST));
}

bool UVHNetSubsystem::CreateRoom(const FString& RoomName)
{
	if (Phase != EVHNetPhase::Lobby || bRequestInFlight)
	{
		return false;
	}
	const FString Trimmed = RoomName.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		OnRoomRequestResult.Broadcast(false, FText::FromString(TEXT("방 이름을 입력해 주세요.")));
		return false;
	}
	CS_CreateRoom P = VHPacket::Make<CS_CreateRoom>(PacketId::CS_CREATE_ROOM);
	VHPacket::WriteString(P.name, sizeof(P.name), Trimmed);
	if (!SendPacket(P))
	{
		return false;
	}
	bRequestInFlight = true;
	return true;
}

bool UVHNetSubsystem::JoinRoom(int64 RoomId)
{
	if (Phase != EVHNetPhase::Lobby || bRequestInFlight)
	{
		return false;
	}
	CS_JoinRoom P = VHPacket::Make<CS_JoinRoom>(PacketId::CS_JOIN_ROOM);
	P.room_id = static_cast<uint32>(RoomId);
	if (!SendPacket(P))
	{
		return false;
	}
	bRequestInFlight = true;
	return true;
}

bool UVHNetSubsystem::LeaveRoom()
{
	if (Phase != EVHNetPhase::Room || bLeaveRequested)
	{
		return false;
	}
	if (!SendPacket(VHPacket::Make<CS_LeaveRoom>(PacketId::CS_LEAVE_ROOM)))
	{
		return false;
	}
	bLeaveRequested = true;
	return true;
}

bool UVHNetSubsystem::SelectCharacter(int32 CharacterId)
{
	if (Phase != EVHNetPhase::Room || bLeaveRequested)
	{
		return false;
	}
	CS_SelectCharacter P = VHPacket::Make<CS_SelectCharacter>(PacketId::CS_SELECT_CHARACTER);
	P.character_id = static_cast<uint16>(CharacterId);
	return SendPacket(P);
}

bool UVHNetSubsystem::SelectPerks(const TArray<int32>& Perks)
{
	if (Phase != EVHNetPhase::Room || bLeaveRequested)
	{
		return false;
	}
	if (Perks.Num() > MAX_PERKS)
	{
		OnRoomError.Broadcast(UVHNetText::ResultCodeText(static_cast<int32>(ResultCode::PerkInvalid)));
		return false;
	}
	CS_SelectPerks P = VHPacket::Make<CS_SelectPerks>(PacketId::CS_SELECT_PERKS);
	P.perk_count = static_cast<uint8>(Perks.Num());
	for (int32 i = 0; i < Perks.Num(); ++i)
	{
		P.perks[i] = static_cast<uint16>(Perks[i]);
	}
	return SendPacket(P);
}

bool UVHNetSubsystem::SetReady(bool bReady)
{
	if (Phase != EVHNetPhase::Room || bLeaveRequested)
	{
		return false;
	}
	CS_Ready P = VHPacket::Make<CS_Ready>(PacketId::CS_READY);
	P.ready = bReady;
	return SendPacket(P);
}

bool UVHNetSubsystem::ReturnFromResult()
{
	if (Phase != EVHNetPhase::Game || bReturnRequested)
	{
		return false;
	}
	if (!SendPacket(VHPacket::Make<CS_Return>(PacketId::CS_RETURN)))
	{
		return false;
	}
	bReturnRequested = true;
	return true;
}

uint16 UVHNetSubsystem::SendMove(const FVector& Pos, float Yaw, uint8 Flags)
{
	if (Phase != EVHNetPhase::Game)
	{
		return 0;
	}
	CS_Move P = VHPacket::Make<CS_Move>(PacketId::CS_MOVE);
	P.seq = ++InputSeq;
	P.pos = { static_cast<float>(Pos.X), static_cast<float>(Pos.Y), static_cast<float>(Pos.Z) };
	P.yaw = Yaw;
	P.flags = Flags;
	return SendPacket(P) ? P.seq : 0;
}

uint16 UVHNetSubsystem::SendAction(ActionType Action, float Yaw, uint16 TargetEntityId, const FVector& Aim)
{
	if (Phase != EVHNetPhase::Game)
	{
		return 0;
	}
	CS_Action P = VHPacket::Make<CS_Action>(PacketId::CS_ACTION);
	P.seq = ++InputSeq;
	P.action = Action;
	P.yaw = Yaw;
	P.target_entity_id = TargetEntityId;
	P.aim = { static_cast<float>(Aim.X), static_cast<float>(Aim.Y), static_cast<float>(Aim.Z) };
	return SendPacket(P) ? P.seq : 0;
}

// ───────────────────────── 빠른 찾기 ─────────────────────────

bool UVHNetSubsystem::QuickFind()
{
	if (Phase != EVHNetPhase::Lobby || IsRequestInFlight())
	{
		return false;
	}
	if (!RequestRoomList(0))
	{
		return false;
	}
	QuickFindStep = EQuickFindStep::WaitingList;
	OnStatusMessage.Broadcast(FText::FromString(TEXT("빠른 찾기: 들어갈 수 있는 방을 찾는 중...")));
	return true;
}

void UVHNetSubsystem::QuickFindOnList()
{
	// 대기중이고 빈자리가 있는 방, 사람이 많은 방부터(빨리 시작할 수 있게)
	TArray<FVHRoomSummary> Candidates = RoomList.FilterByPredicate([](const FVHRoomSummary& R) { return R.CanJoin(); });
	Candidates.Sort([](const FVHRoomSummary& A, const FVHRoomSummary& B) { return A.PlayerCount > B.PlayerCount; });
	QuickFindCandidates.Reset();
	for (const FVHRoomSummary& R : Candidates)
	{
		QuickFindCandidates.Add(R.RoomId);
	}
	QuickFindStep = EQuickFindStep::Joining;
	QuickFindTryNext();
}

void UVHNetSubsystem::QuickFindTryNext()
{
	if (QuickFindCandidates.Num() == 0)
	{
		QuickFindCreate();
		return;
	}
	const int64 RoomId = QuickFindCandidates[0];
	QuickFindCandidates.RemoveAt(0);
	OnStatusMessage.Broadcast(FText::FromString(FString::Printf(TEXT("빠른 찾기: 방 #%lld 입장 시도 중..."), RoomId)));
	if (!JoinRoom(RoomId))
	{
		QuickFindFinish(false, FText::FromString(TEXT("빠른 찾기 중 요청을 보내지 못했습니다.")));
	}
}

void UVHNetSubsystem::QuickFindCreate()
{
	QuickFindStep = EQuickFindStep::Creating;
	OnStatusMessage.Broadcast(FText::FromString(TEXT("빠른 찾기: 들어갈 방이 없어 새 방을 만드는 중...")));
	if (!CreateRoom(FString::Printf(TEXT("%s의 방"), *MyName)))
	{
		QuickFindFinish(false, FText::FromString(TEXT("빠른 찾기 중 방을 만들지 못했습니다.")));
	}
}

void UVHNetSubsystem::QuickFindFinish(bool bSuccess, const FText& Message)
{
	QuickFindStep = EQuickFindStep::None;
	QuickFindCandidates.Reset();
	OnRoomRequestResult.Broadcast(bSuccess, Message);
}

// ───────────────────────── 받기 (디스패처) ─────────────────────────

void UVHNetSubsystem::OnBytes(const uint8* Data, int32 Num)
{
	LastReceiveTime = FPlatformTime::Seconds();
	Assembler.Append(Data, Num);

	TArray<uint8> Packet;
	for (;;)
	{
		const FVHPacketAssembler::EResult R = Assembler.Pop(Packet);
		if (R == FVHPacketAssembler::EResult::NeedMore)
		{
			return;
		}
		if (R == FVHPacketAssembler::EResult::Error)
		{
			UE_LOG(LogVHNet, Error, TEXT("잘못된 패킷 크기를 받아 연결을 끊습니다."));
			HandleTransportClosed(TEXT("서버에서 잘못된 데이터를 받아 연결을 끊었습니다."));
			return;
		}
		HandlePacket(Packet.GetData(), Packet.Num());
		if (!Transport)
		{
			return;	// 처리 중 연결이 끊겼다
		}
	}
}

void UVHNetSubsystem::HandlePacket(const uint8* Data, int32 Size)
{
	const PacketId Id = reinterpret_cast<const PacketHeader*>(Data)->type;
	if (Id != PacketId::SC_PONG && Id != PacketId::SC_SNAPSHOT)
	{
		UE_LOG(LogVHNet, Log, TEXT("← %s (%d bytes)"), VHPacket::PacketName(static_cast<uint16>(Id)), Size);
	}

	// 크기가 명세와 다르면 해석하지 않고 버린다(로그만 남김).
	auto Bad = [Id, Size]()
	{
		UE_LOG(LogVHNet, Error, TEXT("%s 크기 오류(%d bytes) → 무시"), VHPacket::PacketName(static_cast<uint16>(Id)), Size);
	};

	switch (Id)
	{
	case PacketId::SC_LOGIN:		{ SC_Login P;		if (VHPacket::ReadFixed(Data, Size, P)) HandleLogin(P); else Bad(); break; }
	case PacketId::SC_ROOM_LIST:
	{
		SC_RoomList P;
		const bool bOk = VHPacket::ReadVariable(Data, Size, P, MAX_ROOM_LIST,
			[](const SC_RoomList& X) { return X.count; }, [](uint8 C) { return SC_RoomList_Size(C); });
		if (bOk) HandleRoomList(P); else Bad();
		break;
	}
	case PacketId::SC_CREATE_ROOM:	{ SC_CreateRoom P;	if (VHPacket::ReadFixed(Data, Size, P)) HandleCreateRoom(P); else Bad(); break; }
	case PacketId::SC_JOIN_ROOM:	{ SC_JoinRoom P;	if (VHPacket::ReadFixed(Data, Size, P)) HandleJoinRoom(P); else Bad(); break; }
	case PacketId::SC_LEAVE_ROOM:	{ SC_LeaveRoom P;	if (VHPacket::ReadFixed(Data, Size, P)) HandleLeaveRoom(P); else Bad(); break; }
	case PacketId::SC_ROOM_STATE:	{ SC_RoomState P;	if (VHPacket::ReadFixed(Data, Size, P)) HandleRoomState(P); else Bad(); break; }
	case PacketId::SC_ROOM_ERROR:	{ SC_RoomError P;	if (VHPacket::ReadFixed(Data, Size, P)) HandleRoomError(P); else Bad(); break; }
	case PacketId::SC_GAME_LOAD:	{ SC_GameLoad P;	if (VHPacket::ReadFixed(Data, Size, P)) HandleGameLoad(P); else Bad(); break; }
	case PacketId::SC_GAME_START:
	{
		SC_GameStart P;
		const bool bOk = VHPacket::ReadVariable(Data, Size, P, MAX_ENTITIES,
			[](const SC_GameStart& X) { return X.entity_count; }, [](uint8 C) { return SC_GameStart_Size(C); });
		if (bOk) HandleGameStart(P); else Bad();
		if (bOk && Phase == EVHNetPhase::Game) OnGamePacket.Broadcast(static_cast<uint16>(Id), Data, Size);
		break;
	}
	case PacketId::SC_GAME_END:		{ SC_GameEnd P;		if (VHPacket::ReadFixed(Data, Size, P)) HandleGameEnd(P); else Bad(); break; }
	case PacketId::SC_PONG:			{ SC_Pong P;		if (VHPacket::ReadFixed(Data, Size, P)) HandlePong(P); else Bad(); break; }
	case PacketId::SC_KICK:			{ SC_Kick P;		if (VHPacket::ReadFixed(Data, Size, P)) HandleKick(P); else Bad(); break; }
	case PacketId::SC_NOTICE:		{ SC_Notice P;		if (VHPacket::ReadFixed(Data, Size, P)) HandleNotice(P); else Bad(); break; }
	default:
		// 게임 중 패킷(스냅샷·엔티티·게임 이벤트)은 게임 레벨(UVHServerGameSubsystem)에 그대로 넘긴다
		if (Phase == EVHNetPhase::Game && static_cast<uint16>(Id) >= 3000 && static_cast<uint16>(Id) < 5000)
		{
			OnGamePacket.Broadcast(static_cast<uint16>(Id), Data, Size);
		}
		else
		{
			UE_LOG(LogVHNet, Verbose, TEXT("처리하지 않는 패킷 %d 무시"), static_cast<int32>(Id));
		}
		break;
	}
}

void UVHNetSubsystem::HandleLogin(const SC_Login& P)
{
	if (Phase != EVHNetPhase::Authenticating)
	{
		return;
	}
	if (P.result != ResultCode::Ok)
	{
		const FText Message = UVHNetText::ResultCodeText(static_cast<int32>(P.result));
		// 가정: 로그인 실패 시 클라이언트가 연결을 끊고, 다시 시도하면 새로 연결한다.
		Disconnect();
		OnLoginResult.Broadcast(false, Message);
		return;
	}

	MyAccountId = P.player_id;
	MyName = VHPacket::ReadString(P.name, sizeof(P.name));
	MyStats = FVHPlayerStats();	// SC_LOGIN에는 전적이 없다 (최고 기록은 결과창에서만 온다)
	ServerTimeOffsetMs = static_cast<int64>(P.server_time_ms) - static_cast<int64>(ClientTimeMs());

	UE_LOG(LogVHNet, Log, TEXT("로그인 성공: %s (account %llu)"), *MyName, MyAccountId);
	SetPhase(EVHNetPhase::Lobby);
	OnLoginResult.Broadcast(true, FText::FromString(FString::Printf(TEXT("%s 님, 환영합니다!"), *MyName)));
}

void UVHNetSubsystem::HandleRoomList(const SC_RoomList& P)
{
	RoomList.Reset();
	RoomListTotal = P.count;	// 전체 수 없음(대기 중인 방 최대 20개)
	for (int32 i = 0; i < P.count; ++i)
	{
		const RoomSummary& S = P.rooms[i];
		FVHRoomSummary& R = RoomList.AddDefaulted_GetRef();
		R.RoomId = S.room_id;
		R.Name = VHPacket::ReadString(S.name, sizeof(S.name));
		R.HostName = VHPacket::ReadString(S.host_name, sizeof(S.host_name));
		R.PlayerCount = S.player_count;
		R.MaxPlayers = MAX_ROOM_PLAYERS;
		R.State = ToRoomState(S.state);
	}
	OnRoomListUpdated.Broadcast();

	if (QuickFindStep == EQuickFindStep::WaitingList)
	{
		QuickFindOnList();
	}
}

void UVHNetSubsystem::HandleCreateRoom(const SC_CreateRoom& P)
{
	bRequestInFlight = false;
	const bool bOk = P.result == ResultCode::Ok;
	if (bOk)
	{
		RoomState = FVHRoomState();
		RoomState.RoomId = P.room_id;
		SetPhase(EVHNetPhase::Room);
	}
	const FText Message = bOk ? FText::FromString(TEXT("방을 만들었습니다."))
		: FText::FromString(FString::Printf(TEXT("방을 만들지 못했습니다: %s"), *UVHNetText::ResultCodeText(static_cast<int32>(P.result)).ToString()));
	if (QuickFindStep != EQuickFindStep::None)
	{
		QuickFindFinish(bOk, Message);
	}
	else
	{
		OnRoomRequestResult.Broadcast(bOk, Message);
	}
}

void UVHNetSubsystem::HandleJoinRoom(const SC_JoinRoom& P)
{
	bRequestInFlight = false;
	const bool bOk = P.result == ResultCode::Ok;
	if (bOk)
	{
		RoomState = FVHRoomState();
		RoomState.RoomId = P.room_id;
		SetPhase(EVHNetPhase::Room);
		if (QuickFindStep != EQuickFindStep::None)
		{
			QuickFindFinish(true, FText::FromString(TEXT("빠른 찾기: 방에 들어갔습니다.")));
		}
		else
		{
			OnRoomRequestResult.Broadcast(true, FText::FromString(TEXT("방에 들어갔습니다.")));
		}
		return;
	}

	const FText Reason = UVHNetText::ResultCodeText(static_cast<int32>(P.result));
	if (QuickFindStep == EQuickFindStep::Joining)
	{
		// 그 사이 방이 찼거나 시작됨 → 다음 후보
		UE_LOG(LogVHNet, Log, TEXT("빠른 찾기: 방 %u 입장 실패(%s) → 다음 후보"), P.room_id, *Reason.ToString());
		QuickFindTryNext();
		return;
	}
	OnRoomRequestResult.Broadcast(false, FText::FromString(FString::Printf(TEXT("입장하지 못했습니다: %s"), *Reason.ToString())));
}

void UVHNetSubsystem::HandleLeaveRoom(const SC_LeaveRoom& P)
{
	if (Phase != EVHNetPhase::Room && Phase != EVHNetPhase::Game)
	{
		return;
	}
	const bool bFromGame = Phase == EVHNetPhase::Game;
	bLeaveRequested = false;
	bReturnRequested = false;
	bResultPending = false;
	RoomState = FVHRoomState();
	SetPhase(EVHNetPhase::Lobby);

	const FText Message = UVHNetText::LeaveReasonText(static_cast<int32>(P.reason));
	if (P.reason != LeaveReason::Self)
	{
		ShowToast(Message, true);
	}
	OnLeftRoom.Broadcast(Message);

	if (bFromGame && !IsMenuWorld(GetWorld()))
	{
		OpenMenuLevel();
	}
}

void UVHNetSubsystem::HandleRoomState(const SC_RoomState& P)
{
	if (Phase != EVHNetPhase::Room && Phase != EVHNetPhase::Game)
	{
		return;
	}
	FVHRoomState S;
	S.bValid = true;
	S.RoomId = P.room_id;
	S.Name = VHPacket::ReadString(P.name, sizeof(P.name));
	S.HostAccountId = static_cast<int64>(P.host_player_id);
	S.State = ToRoomState(P.state);
	const int32 Count = FMath::Min<int32>(P.member_count, MAX_ROOM_PLAYERS);
	for (int32 i = 0; i < Count; ++i)
	{
		const RoomMemberInfo& M = P.members[i];
		FVHRoomMember& Out = S.Members.AddDefaulted_GetRef();
		Out.Slot = i;	// slot 없음 → 배열 순서 (0 = 방장 = 시작 위치 0)
		Out.AccountId = static_cast<int64>(M.player_id);
		Out.Name = VHPacket::ReadString(M.name, sizeof(M.name));
		Out.CharacterId = M.character_id;
		for (int32 p = 0; p < FMath::Min<int32>(M.perk_count, MAX_PERKS); ++p)
		{
			Out.Perks.Add(M.perks[p]);
		}
		Out.bReady = M.ready;
	}
	S.Members.Sort([](const FVHRoomMember& A, const FVHRoomMember& B) { return A.Slot < B.Slot; });
	RoomState = MoveTemp(S);

	// 게임(결과창) 중에 대기중 상태를 받으면 대기실로 돌아온 것 (가정: CS_RETURN Room / 자동 복귀 응답)
	if (Phase == EVHNetPhase::Game && RoomState.State == EVHRoomState::Waiting)
	{
		bReturnRequested = false;
		bResultPending = false;
		bAwaitingLoadComplete = false;
		SetPhase(EVHNetPhase::Room);
		if (!IsMenuWorld(GetWorld()))
		{
			OpenMenuLevel();
		}
	}
	OnRoomStateChanged.Broadcast();
}

void UVHNetSubsystem::HandleRoomError(const SC_RoomError& P)
{
	const FText Message = UVHNetText::ResultCodeText(static_cast<int32>(P.result));
	UE_LOG(LogVHNet, Log, TEXT("방 요청 실패: %s → %s"), VHPacket::PacketName(static_cast<uint16>(P.request_id)), *Message.ToString());
	OnRoomError.Broadcast(Message);
}

void UVHNetSubsystem::HandleGameLoad(const SC_GameLoad& P)
{
	if (Phase != EVHNetPhase::Room)
	{
		return;
	}
	LastGameRoomState = RoomState;
	GameLoadInfo = FVHGameLoadInfo();
	GameLoadInfo.bValid = true;
	GameLoadInfo.MatchId = P.match_id;
	GameLoadInfo.MapId = P.map_id;
	GameLoadInfo.MapVersion = P.map_version;
	for (int32 i = 0; i < FMath::Min<int32>(P.spawn_count, MAX_ROOM_PLAYERS); ++i)
	{
		GameLoadInfo.SpawnIndexByPlayer.Add(P.spawns[i].player_id, P.spawns[i].spawn_index);
	}
	GameStartInfo = FVHGameStartInfo();
	UE_LOG(LogVHNet, Log, TEXT("게임 로드: match %llu, 맵 %u v%u, 플레이어 %d명, 로딩 제한 %ums"),
		P.match_id, P.map_id, P.map_version, P.spawn_count, P.load_timeout_ms);
	// 게임 레벨이 맞춰진 서버 맵 버전 (설정 GameLevelMapVersion: 옛 레벨 3, 새 맵 L_Mansion_Shell 10 — 예전 V3b 서버는 2)
	const uint32 ExpectedServerMapVersion = UVHSettings::Get().GetGameLevelMapVersion();
	if (!bUsingMock && P.map_version != ExpectedServerMapVersion)
	{
		UE_LOG(LogVHNet, Warning, TEXT("서버 맵 버전 %u ≠ 클라 배치 버전 %u → 금고·시작 위치가 어긋날 수 있음"), P.map_version, ExpectedServerMapVersion);
		ShowToast(FText::FromString(FString::Printf(TEXT("서버 맵 버전(%u)이 클라 배치(%u)와 다릅니다. 위치가 어긋날 수 있습니다."), P.map_version, ExpectedServerMapVersion)), true);
	}
	bAwaitingLoadComplete = true;
	bResultPending = false;
	LastGameEnd = FVHGameEndInfo();
	InputSeq = 0;
	SetPhase(EVHNetPhase::Game);
	OpenGameLevel();
}

void UVHNetSubsystem::HandleGameStart(const SC_GameStart& P)
{
	if (Phase != EVHNetPhase::Game)
	{
		return;
	}
	GameStartInfo = FVHGameStartInfo();
	GameStartInfo.bValid = true;
	GameStartInfo.StartServerTimeMs = P.start_server_time_ms;
	GameStartInfo.DurationMs = P.duration_ms;
	GameStartInfo.TickHz = P.tick_hz;
	GameStartInfo.SelfEntityId = P.self_entity_id;
	GameStartInfo.ReceivedSeconds = FPlatformTime::Seconds();
	for (int32 i = 0; i < P.entity_count; ++i)
	{
		GameStartInfo.Entities.Add(P.entities[i]);
	}
	UE_LOG(LogVHNet, Log, TEXT("게임 시작: 내 의적 엔티티 %u, 엔티티 %d개, 제한 %ums, 틱 %dHz"),
		P.self_entity_id, P.entity_count, P.duration_ms, P.tick_hz);
}

void UVHNetSubsystem::HandleGameEnd(const SC_GameEnd& P)
{
	if (Phase != EVHNetPhase::Game)
	{
		return;
	}
	FVHGameEndInfo Info;
	Info.bValid = true;
	Info.MatchId = static_cast<int64>(P.match_id);
	Info.Reason = P.reason == EndReason::Aborted ? EVHEndReason::Aborted : EVHEndReason::TimeUp;
	Info.bServerResult = !bUsingMock;
	Info.DurationMs = static_cast<int32>(P.duration_ms);
	Info.ResultScreenMs = static_cast<int32>(P.result_screen_ms);
	Info.ReceivedTimeSeconds = FPlatformTime::Seconds();
	for (int32 i = 0; i < FMath::Min<int32>(P.player_count, MAX_ROOM_PLAYERS); ++i)
	{
		const PlayerResult& R = P.players[i];
		FVHPlayerResult& Out = Info.Players.AddDefaulted_GetRef();
		Out.AccountId = static_cast<int64>(R.player_id);
		// v3.0: 결과에 이름이 들어 있다. 비어 있으면 직전 방 상태에서 찾는다
		Out.Name = VHPacket::ReadString(R.name, sizeof(R.name));
		if (Out.Name.IsEmpty())
		{
			const FVHRoomMember* Member = LastGameRoomState.FindMember(Out.AccountId);
			Out.Name = Member ? Member->Name : FString::Printf(TEXT("플레이어 #%llu"), R.player_id);
		}
		Out.CharacterId = R.character_id;
		Out.bLeftEarly = R.left_early;
		Out.TreasureEscaped = R.treasures_delivered;
		Out.ValueDelivered = static_cast<int32>(R.banked_value);	// 반납 금액 = 점수
		Out.Rank = R.rank;										// 서버가 매긴 순위 (같은 금액은 같은 순위)
		Out.BestRecord = static_cast<int32>(R.best_record);
		Out.bNewRecord = R.new_record;
		Out.Kills = R.kills;
		Out.Deaths = R.downs;
		Out.bIsMe = MyAccountId != 0 && Out.AccountId == static_cast<int64>(MyAccountId);
	}
	Info.bHasCombatStats = true;	// v3.0 결과에는 쓰러뜨림·쓰러짐 횟수가 들어 있다
	// 순위는 서버가 매긴 값을 쓴다(순위 순으로 정렬만). 순위가 비어 있을 때만 반납 금액으로 직접 매긴다
	const bool bServerRanks = Info.Players.Num() > 0
		&& !Info.Players.ContainsByPredicate([](const FVHPlayerResult& X) { return X.Rank <= 0; });
	if (bServerRanks)
	{
		Info.Players.StableSort([](const FVHPlayerResult& A, const FVHPlayerResult& B) { return A.Rank < B.Rank; });
	}
	else
	{
		VHRankResults(Info.Players);
	}
	Info.Result = Info.Reason == EVHEndReason::Aborted ? EVHMatchResult::Aborted : EVHMatchResult::Finished;
	UE_LOG(LogVHNet, Log, TEXT("게임 끝: 판 %lld, 사유 %d, %dms, %d명, 결과 화면 %dms"),
		Info.MatchId, static_cast<int32>(Info.Reason), Info.DurationMs, Info.Players.Num(), Info.ResultScreenMs);
	for (const FVHPlayerResult& R : Info.Players)
	{
		UE_LOG(LogVHNet, Log, TEXT("[결과] %d위 %s%s 캐릭터 %d: 반납 %d$, 최고 기록 %d$%s, 쓰러뜨림 %d, 쓰러짐 %d, 보물 %d개%s"),
			R.Rank, *R.Name, R.bIsMe ? TEXT("(나)") : TEXT(""), R.CharacterId, R.ValueDelivered, R.BestRecord,
			R.bNewRecord ? TEXT(" (갱신)") : TEXT(""), R.Kills, R.Deaths, R.TreasureEscaped, R.bLeftEarly ? TEXT(", 중도 이탈") : TEXT(""));
	}
	LastGameEnd = MoveTemp(Info);
	bResultPending = true;
	bAwaitingLoadComplete = false;
	OnGameEnd.Broadcast();

	if (!IsMenuWorld(GetWorld()))
	{
		OpenMenuLevel();	// 메뉴 레벨이 열리면 결과창을 띄운다
	}
}

void UVHNetSubsystem::HandlePong(const SC_Pong& P)
{
	const int64 Now = static_cast<int64>(ClientTimeMs());
	RttMs = static_cast<int32>(FMath::Max<int64>(0, Now - static_cast<int64>(P.client_time_ms)));
	// 서버 시각 ≈ 받은 server_time + RTT/2
	ServerTimeOffsetMs = static_cast<int64>(P.server_time_ms) + RttMs / 2 - Now;
}

void UVHNetSubsystem::HandleKick(const SC_Kick& P)
{
	const FString Detail = VHPacket::ReadString(P.message, sizeof(P.message));
	KickMessage = UVHNetText::KickReasonText(static_cast<int32>(P.reason)).ToString();
	if (!Detail.IsEmpty())
	{
		KickMessage += FString::Printf(TEXT("\n(%s)"), *Detail);
	}
	UE_LOG(LogVHNet, Warning, TEXT("SC_KICK: %s"), *KickMessage);
	// 곧 서버가 연결을 끊는다 → HandleTransportClosed에서 이 메시지로 안내
}

void UVHNetSubsystem::HandleNotice(const SC_Notice& P)
{
	ShowToast(FText::FromString(VHPacket::ReadString(P.message, sizeof(P.message))), false);
}

// ───────────────────────── 레벨 이동 ─────────────────────────

void UVHNetSubsystem::OpenMenuLevel()
{
	const FSoftObjectPath& Path = UVHSettings::Get().MenuLevel;
	if (Path.IsValid())
	{
		UGameplayStatics::OpenLevelBySoftObjectPtr(GetGameInstance(), TSoftObjectPtr<UWorld>(Path));
	}
}

void UVHNetSubsystem::OpenGameLevel()
{
	const FSoftObjectPath Path = UVHSettings::Get().GetGameLevel();
	if (Path.IsValid())
	{
		UE_LOG(LogVHNet, Log, TEXT("게임 레벨 열기: %s (맞춘 서버 맵 v%u)"), *Path.ToString(), UVHSettings::Get().GetGameLevelMapVersion());
		UGameplayStatics::OpenLevelBySoftObjectPtr(GetGameInstance(), TSoftObjectPtr<UWorld>(Path));
	}
}

void UVHNetSubsystem::OnPostLoadMap(UWorld* World)
{
	if (!World || World->GetGameInstance() != GetGameInstance())
	{
		return;
	}
	if (bAwaitingLoadComplete && Phase == EVHNetPhase::Game && !IsMenuWorld(World))
	{
		// 맵 로드가 끝났음을 서버에 알린다. 가정: 실제 맵 해시 계산 전까지 서버가 준 값을 그대로 돌려보낸다.
		bAwaitingLoadComplete = false;
		SendPacket(VHPacket::Make<CS_LoadComplete>(PacketId::CS_LOAD_COMPLETE));
		// 입력 모드·HUD·제한시간은 게임 레벨의 플레이어 컨트롤러(AVHGamePlayerController)가 맡는다.
		// (2026-10-25 이전의 "[임시 게임 화면]" 안내와 자동 종료는 삭제)
	}
}

// ───────────────────────── 기타 ─────────────────────────

void UVHNetSubsystem::ShowToast(const FText& Message, bool bIsError)
{
	UE_LOG(LogVHNet, Log, TEXT("알림: %s"), *Message.ToString());
	UVHToastWidget* Widget = Toast.Get();
	if (!Widget || !Widget->IsInViewport())
	{
		APlayerController* PC = GetGameInstance() ? GetGameInstance()->GetFirstLocalPlayerController() : nullptr;
		if (!PC)
		{
			return;
		}
		Widget = CreateWidget<UVHToastWidget>(PC, UVHToastWidget::StaticClass());
		if (!Widget)
		{
			return;
		}
		Widget->AddToViewport(1000);
		Toast = Widget;
	}
	Widget->AddMessage(Message, bIsError);
}

void UVHNetSubsystem::ShowSampleResult()
{
	// 개발용 샘플 결과 (PvP): 내 이름(없으면 Tester)과 가짜 상대 둘. 공동 1위 표시를 볼 수 있게 나와 Bella를 같은 금액으로 둔다
	const FString Me = MyName.IsEmpty() ? FString(TEXT("Tester")) : MyName;
	FVHGameEndInfo Info;
	Info.bValid = true;
	Info.bIsSample = true;
	Info.bHasCombatStats = true;
	Info.MatchId = 1;
	Info.Result = EVHMatchResult::Finished;
	Info.Reason = EVHEndReason::TimeUp;
	Info.DurationMs = FMath::RoundToInt(UVHSettings::Get().GetGameplayConfig().TimeLimitSeconds * 1000.f);
	Info.ResultScreenMs = RESULT_SCREEN_MS;
	Info.ReceivedTimeSeconds = FPlatformTime::Seconds();
	auto Add = [&Info](const FString& Name, bool bMe, int32 Character, int32 Value, int32 Treasure, int32 Kills, int32 Deaths)
	{
		FVHPlayerResult& P = Info.Players.AddDefaulted_GetRef();
		P.AccountId = Info.Players.Num();
		P.bIsMe = bMe;
		P.Name = Name;
		P.CharacterId = Character;
		P.ValueDelivered = Value;
		P.TreasureEscaped = Treasure;
		P.Kills = Kills;
		P.Deaths = Deaths;
	};
	// 최고 기록 표시 3가지를 한 화면에서 볼 수 있게: 나 = 이번 판으로 갱신, Bella = 예전 기록이 더 높음, Coco = 기록 없음(0$ 반납)
	Add(TEXT("Coco"), false, 3, 0, 0, 0, 3);
	Add(Me, true, 1, 5200, 3, 2, 1);
	Add(TEXT("Bella"), false, 2, 5200, 2, 1, 0);
	Info.Players[1].BestRecord = 5200;
	Info.Players[1].bNewRecord = true;
	Info.Players[2].BestRecord = 7400;
	VHRankResults(Info.Players);

	LastGameEnd = MoveTemp(Info);
	bResultPending = true;
	OnGameEnd.Broadcast();
	if (!IsMenuWorld(GetWorld()))
	{
		OpenMenuLevel();
	}
}

FString UVHNetSubsystem::LoadLastLoginName() const
{
	if (const UVHLocalSaveGame* Save = Cast<UVHLocalSaveGame>(UGameplayStatics::LoadGameFromSlot(UVHLocalSaveGame::SlotName, 0)))
	{
		return Save->LastLoginName;
	}
	return FString();
}

void UVHNetSubsystem::SaveLastLoginName(const FString& Name) const
{
	UVHLocalSaveGame* Save = Cast<UVHLocalSaveGame>(UGameplayStatics::CreateSaveGameObject(UVHLocalSaveGame::StaticClass()));
	if (Save)
	{
		Save->LastLoginName = Name;
		UGameplayStatics::SaveGameToSlot(Save, UVHLocalSaveGame::SlotName, 0);
	}
}
