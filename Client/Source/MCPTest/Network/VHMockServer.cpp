#include "Network/VHMockServer.h"
#include "Network/VHPacket.h"
#include "Network/VHSettings.h"
#include "Gameplay/VHGameplayConfig.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogVHMock, Log, All);

using namespace RobProto;

namespace
{
	constexpr uint16 NumCharacters = 3;		// 플레이어 캐릭터 도적1~3 (서버 v3.0 은 1~5를 받음)
	constexpr uint16 NumPerks = 7;			// 서버 v3.0: 퍽 1~7
	constexpr uint32 MockMapId = 1;

	const TCHAR* FakeNames[] = { TEXT("Bella"), TEXT("Coco") };

	// 개발용: 가짜 서버 게임 시작 때 든 무기 (0 = 삼단봉). 화염방사기 연타 같은 화면 시험용 — 판정은 하지 않는다
	TAutoConsoleVariable<int32> CVarMockWeapon(TEXT("vh.MockWeapon"), 0,
		TEXT("개발용: 가짜 서버에서 게임 시작 때 들 무기 번호 (0 = 삼단봉, 5 = 화염방사기). 다음 판부터"));
}

// ───────────────────────── FRoom ─────────────────────────

FVHMockServer::FMember* FVHMockServer::FRoom::FindMember(uint64 AccountId)
{
	return Members.FindByPredicate([AccountId](const FMember& M) { return M.AccountId == AccountId; });
}

FString FVHMockServer::FRoom::HostName() const
{
	for (const FMember& M : Members)
	{
		if (M.AccountId == HostAccountId)
		{
			return M.Name;
		}
	}
	return Members.Num() > 0 ? Members[0].Name : FString();
}

// ───────────────────────── 기본 ─────────────────────────

FVHMockServer::FVHMockServer(const FVHMockConfig& InConfig)
	: Config(InConfig)
{
	StartTime = FPlatformTime::Seconds();
	Now = StartTime;

	// 로비에 보일 가짜 방들. 입장 실패(RoomFull/RoomNotWaiting) 테스트용 방도 포함.
	uint64 FakeId = 900000;
	auto AddRoom = [this, &FakeId](uint32 Id, const TCHAR* Name, RoomState State, std::initializer_list<const TCHAR*> MemberNames)
	{
		FRoom Room;
		Room.RoomId = Id;
		Room.Name = Name;
		Room.State = State;
		uint8 Slot = 0;
		for (const TCHAR* MemberName : MemberNames)
		{
			FMember M;
			M.Slot = Slot++;
			M.AccountId = ++FakeId;
			M.Name = MemberName;
			M.bFake = true;
			Room.Members.Add(M);
		}
		Room.HostAccountId = Room.Members[0].AccountId;
		Rooms.Add(MoveTemp(Room));
	};

	AddRoom(1, TEXT("초보 환영 금고털이"), RoomState::Waiting, { TEXT("Alice") });
	AddRoom(2, TEXT("바바라 저택 습격"), RoomState::Waiting, { TEXT("Bob"), TEXT("Chris") });
	AddRoom(3, TEXT("꽉 찬 방"), RoomState::Waiting, { TEXT("Dan"), TEXT("Dora"), TEXT("Duke") });
	AddRoom(4, TEXT("이미 시작한 방"), RoomState::Playing, { TEXT("Eve"), TEXT("Evan") });
	AddRoom(5, TEXT("도전자 모집"), RoomState::Waiting, { TEXT("Finn") });
}

uint64 FVHMockServer::ServerTimeMs() const
{
	return static_cast<uint64>((FPlatformTime::Seconds() - StartTime) * 1000.0);
}

template <typename T>
void FVHMockServer::Send(const T& Packet, uint16 Size)
{
	if (bKicked || !SendToClient)
	{
		return;
	}
	UE_LOG(LogVHMock, Verbose, TEXT("Mock → %s (%d bytes)"), VHPacket::PacketName(static_cast<uint16>(Packet.type)), Size);
	SendToClient(VHPacket::ToBytes(Packet, Size));
}

void FVHMockServer::After(float Seconds, TFunction<void()> Fn)
{
	FTimer T;
	T.FireAt = Now + Seconds;
	T.Epoch = Epoch;
	T.Fn = MoveTemp(Fn);
	Timers.Add(MoveTemp(T));
}

void FVHMockServer::Tick(double NowSeconds)
{
	Now = NowSeconds;
	if (bKicked)
	{
		return;
	}

	// 실행 중에 새 타이머가 추가될 수 있으므로 꺼낸 뒤 실행한다.
	TArray<FTimer> Due;
	for (int32 i = Timers.Num() - 1; i >= 0; --i)
	{
		if (Timers[i].FireAt <= Now)
		{
			Due.Add(MoveTemp(Timers[i]));
			Timers.RemoveAtSwap(i);
		}
	}
	Due.Sort([](const FTimer& A, const FTimer& B) { return A.FireAt < B.FireAt; });
	for (FTimer& T : Due)
	{
		if (T.Epoch == Epoch && !bKicked)
		{
			T.Fn();
		}
	}
}

bool FVHMockServer::IsAllowed(PacketId Id) const
{
	if (Id == PacketId::CS_PING)
	{
		return true;
	}
	switch (Phase)
	{
	case ClientPhase::Anonymous:
		return Id == PacketId::CS_LOGIN;
	case ClientPhase::Authenticating:
		return false;
	case ClientPhase::Lobby:
		return Id == PacketId::CS_ROOM_LIST || Id == PacketId::CS_CREATE_ROOM || Id == PacketId::CS_JOIN_ROOM;
	case ClientPhase::Room:
		return Id == PacketId::CS_LEAVE_ROOM || Id == PacketId::CS_SELECT_CHARACTER
			|| Id == PacketId::CS_SELECT_PERKS || Id == PacketId::CS_READY;
	case ClientPhase::Game:
		return Id == PacketId::CS_LOAD_COMPLETE || Id == PacketId::CS_MOVE || Id == PacketId::CS_ACTION || Id == PacketId::CS_RETURN;
	}
	return false;
}

void FVHMockServer::Kick(KickReason Reason, const FString& Message)
{
	SC_Kick Kick = VHPacket::Make<SC_Kick>(PacketId::SC_KICK);
	Kick.reason = Reason;
	VHPacket::WriteString(Kick.message, sizeof(Kick.message), Message);
	Send(Kick);
	bKicked = true;
	Timers.Reset();
	UE_LOG(LogVHMock, Warning, TEXT("Mock 서버가 연결을 끊음: %s"), *Message);
	if (DisconnectClient)
	{
		DisconnectClient(Message);
	}
}

void FVHMockServer::ReceivePacket(const uint8* Data, int32 Size)
{
	if (bKicked || Size < VHPacket::HeaderSize)
	{
		return;
	}
	const PacketHeader* Header = reinterpret_cast<const PacketHeader*>(Data);
	const PacketId Id = Header->type;
	UE_LOG(LogVHMock, Verbose, TEXT("Mock ← %s (%d bytes)"), VHPacket::PacketName(static_cast<uint16>(Id)), Size);

	if (!IsAllowed(Id))
	{
		// 진짜 서버도 단계에 맞지 않는 패킷이면 연결을 끊는다(명세).
		Kick(KickReason::ProtocolViolation, FString::Printf(TEXT("[Mock] 현재 단계(%d)에서 허용되지 않은 패킷 %s"),
			static_cast<int32>(Phase), VHPacket::PacketName(static_cast<uint16>(Id))));
		return;
	}

	switch (Id)
	{
	case PacketId::CS_PING:
	{
		CS_Ping Ping;
		if (!VHPacket::ReadFixed(Data, Size, Ping)) { Kick(KickReason::ProtocolViolation, TEXT("[Mock] CS_PING 크기 오류")); return; }
		SC_Pong Pong = VHPacket::Make<SC_Pong>(PacketId::SC_PONG);
		Pong.client_time_ms = Ping.client_time_ms;
		Pong.server_time_ms = ServerTimeMs();
		Send(Pong);
		break;
	}
	case PacketId::CS_LOGIN:			HandleLogin(Data, Size); break;
	case PacketId::CS_ROOM_LIST:		HandleRoomList(Data, Size); break;
	case PacketId::CS_CREATE_ROOM:		HandleCreateRoom(Data, Size); break;
	case PacketId::CS_JOIN_ROOM:		HandleJoinRoom(Data, Size); break;
	case PacketId::CS_LEAVE_ROOM:		HandleLeaveRoom(); break;
	case PacketId::CS_SELECT_CHARACTER:	HandleSelectCharacter(Data, Size); break;
	case PacketId::CS_SELECT_PERKS:		HandleSelectPerks(Data, Size); break;
	case PacketId::CS_READY:			HandleReady(Data, Size); break;
	case PacketId::CS_LOAD_COMPLETE:	HandleLoadComplete(Data, Size); break;
	case PacketId::CS_RETURN:			HandleReturn(Data, Size); break;
	case PacketId::CS_MOVE:
	{
		// 이동은 클라가 한다. 서버는 받은 위치를 그대로 쓴다(스냅샷으로 되돌려 준다)
		CS_Move Move;
		if (!VHPacket::ReadFixed(Data, Size, Move)) { Kick(KickReason::ProtocolViolation, TEXT("[Mock] CS_MOVE 크기 오류")); return; }
		SelfPos = Move.pos;
		SelfYaw = Move.yaw;
		break;
	}
	case PacketId::CS_ACTION:			HandleAction(Data, Size); break;
	default:
		Kick(KickReason::ProtocolViolation, TEXT("[Mock] 처리하지 않는 패킷"));
		break;
	}
}

// ───────────────────────── 계정 ─────────────────────────

void FVHMockServer::HandleLogin(const uint8* Data, int32 Size)
{
	CS_Login Req;
	if (!VHPacket::ReadFixed(Data, Size, Req)) { Kick(KickReason::ProtocolViolation, TEXT("[Mock] CS_LOGIN 크기 오류")); return; }

	const FString Name = VHPacket::ReadString(Req.name, sizeof(Req.name)).TrimStartAndEnd();
	SC_Login Res = VHPacket::Make<SC_Login>(PacketId::SC_LOGIN);
	Res.server_time_ms = ServerTimeMs();

	if (Req.protocol_version != PROTOCOL_VERSION)
	{
		Res.result = ResultCode::VersionMismatch;
	}
	else if (Name.IsEmpty() || VHPacket::Utf8Length(Name) > MAX_NAME_LEN)
	{
		Res.result = ResultCode::NameInvalid;
	}
	else if (Name.Equals(TEXT("Taken"), ESearchCase::IgnoreCase))
	{
		Res.result = ResultCode::NameInUse;
	}
	else if (Name.Equals(TEXT("Busy"), ESearchCase::IgnoreCase))
	{
		Res.result = ResultCode::ServerBusy;
	}
	else
	{
		Res.result = ResultCode::Ok;
		MyName = Name;
		MyAccountId = 10000ull + FCrc::StrCrc32(*Name.ToLower());
		Res.player_id = MyAccountId;
		VHPacket::WriteString(Res.name, sizeof(Res.name), MyName);
		Phase = ClientPhase::Lobby;
	}
	Send(Res);

	if (Res.result == ResultCode::Ok)
	{
		After(0.8f, [this]()
		{
			SC_Notice Notice = VHPacket::Make<SC_Notice>(PacketId::SC_NOTICE);
			VHPacket::WriteString(Notice.message, sizeof(Notice.message), TEXT("[공지] Mock 서버에 접속했습니다. (테스트 모드)"));
			Send(Notice);
		});
	}
}

// ───────────────────────── 로비 ─────────────────────────

void FVHMockServer::HandleRoomList(const uint8* Data, int32 Size)
{
	CS_RoomList Req;
	if (!VHPacket::ReadFixed(Data, Size, Req)) { Kick(KickReason::ProtocolViolation, TEXT("[Mock] CS_ROOM_LIST 크기 오류")); return; }

	// v2.1: 페이지 없음. 서버처럼 대기 중인 방만 최대 20개
	SC_RoomList Res = VHPacket::Make<SC_RoomList>(PacketId::SC_ROOM_LIST);
	for (const FRoom& Room : Rooms)
	{
		if (Room.State != RoomState::Waiting || Res.count >= MAX_ROOM_LIST)
		{
			continue;
		}
		RoomSummary& S = Res.rooms[Res.count++];
		S.room_id = Room.RoomId;
		VHPacket::WriteString(S.name, sizeof(S.name), Room.Name);
		VHPacket::WriteString(S.host_name, sizeof(S.host_name), Room.HostName());
		S.player_count = static_cast<uint8>(Room.Members.Num());
		S.state = Room.State;
	}
	Send(Res, SC_RoomList_Size(Res.count));
}

void FVHMockServer::HandleCreateRoom(const uint8* Data, int32 Size)
{
	CS_CreateRoom Req;
	if (!VHPacket::ReadFixed(Data, Size, Req)) { Kick(KickReason::ProtocolViolation, TEXT("[Mock] CS_CREATE_ROOM 크기 오류")); return; }

	SC_CreateRoom Res = VHPacket::Make<SC_CreateRoom>(PacketId::SC_CREATE_ROOM);
	const FString Name = VHPacket::ReadString(Req.name, sizeof(Req.name)).TrimStartAndEnd();
	if (Name.IsEmpty())
	{
		Res.result = ResultCode::RoomNameInvalid;
		Send(Res);
		return;
	}

	FRoom Room;
	Room.RoomId = NextRoomId++;
	Room.Name = Name;
	Room.State = RoomState::Waiting;
	Room.bCreatedByClient = true;
	FMember Me;
	Me.Slot = 0;
	Me.AccountId = MyAccountId;
	Me.Name = MyName;
	Me.bFake = false;
	Room.Members.Add(Me);
	Room.HostAccountId = MyAccountId;
	Rooms.Add(Room);

	Res.result = ResultCode::Ok;
	Res.room_id = Room.RoomId;
	Send(Res);

	CurrentRoomId = Room.RoomId;
	Phase = ClientPhase::Room;
	InvalidateTimers();
	SendRoomState(*CurrentRoom());	// 가정: 생성 직후 SC_ROOM_STATE를 보낸다

	for (int32 i = 0; i < FMath::Clamp(Config.FakePlayersInNewRoom, 0, 2); ++i)
	{
		ScheduleFakeJoin(i, 2.0f + i * 2.5f);
	}
}

void FVHMockServer::HandleJoinRoom(const uint8* Data, int32 Size)
{
	CS_JoinRoom Req;
	if (!VHPacket::ReadFixed(Data, Size, Req)) { Kick(KickReason::ProtocolViolation, TEXT("[Mock] CS_JOIN_ROOM 크기 오류")); return; }

	SC_JoinRoom Res = VHPacket::Make<SC_JoinRoom>(PacketId::SC_JOIN_ROOM);
	Res.room_id = Req.room_id;
	FRoom* Room = FindRoom(Req.room_id);
	if (!Room)
	{
		Res.result = ResultCode::RoomNotFound;
	}
	else if (Room->State != RoomState::Waiting)
	{
		Res.result = ResultCode::RoomNotWaiting;
	}
	else if (Room->Members.Num() >= MAX_ROOM_PLAYERS)
	{
		Res.result = ResultCode::RoomFull;
	}
	else
	{
		Res.result = ResultCode::Ok;
	}
	Send(Res);
	if (Res.result != ResultCode::Ok)
	{
		return;
	}

	// 비어 있는 가장 작은 슬롯 번호
	uint8 FreeSlot = 0;
	while (Room->Members.ContainsByPredicate([FreeSlot](const FMember& M) { return M.Slot == FreeSlot; }))
	{
		++FreeSlot;
	}
	FMember Me;
	Me.Slot = FreeSlot;
	Me.AccountId = MyAccountId;
	Me.Name = MyName;
	Me.bFake = false;
	Room->Members.Add(Me);

	CurrentRoomId = Room->RoomId;
	Phase = ClientPhase::Room;
	InvalidateTimers();
	SendRoomState(*Room);
	ScheduleFakePreparation(Config.FakeActionDelaySeconds);
}

// ───────────────────────── 방 ─────────────────────────

FVHMockServer::FRoom* FVHMockServer::FindRoom(uint32 RoomId)
{
	return Rooms.FindByPredicate([RoomId](const FRoom& R) { return R.RoomId == RoomId; });
}

FVHMockServer::FRoom* FVHMockServer::CurrentRoom()
{
	return CurrentRoomId != 0 ? FindRoom(CurrentRoomId) : nullptr;
}

void FVHMockServer::SendRoomState(const FRoom& Room)
{
	SC_RoomState S = VHPacket::Make<SC_RoomState>(PacketId::SC_ROOM_STATE);
	S.room_id = Room.RoomId;
	VHPacket::WriteString(S.name, sizeof(S.name), Room.Name);
	S.host_player_id = Room.HostAccountId;
	S.state = Room.State;
	S.member_count = static_cast<uint8>(FMath::Min(Room.Members.Num(), MAX_ROOM_PLAYERS));
	for (int32 i = 0; i < S.member_count; ++i)
	{
		const FMember& M = Room.Members[i];
		RoomMemberInfo& Info = S.members[i];
		Info.player_id = M.AccountId;
		VHPacket::WriteString(Info.name, sizeof(Info.name), M.Name);
		Info.character_id = M.CharacterId;
		Info.perk_count = static_cast<uint8>(FMath::Min(M.Perks.Num(), MAX_PERKS));
		for (int32 p = 0; p < Info.perk_count; ++p)
		{
			Info.perks[p] = M.Perks[p];
		}
		Info.ready = M.bReady;
	}
	Send(S);
}

void FVHMockServer::SendRoomError(PacketId Request, ResultCode Code)
{
	SC_RoomError E = VHPacket::Make<SC_RoomError>(PacketId::SC_ROOM_ERROR);
	E.request_id = Request;
	E.result = Code;
	Send(E);
}

void FVHMockServer::LeaveCurrentRoom()
{
	FRoom* Room = CurrentRoom();
	if (Room)
	{
		Room->Members.RemoveAll([this](const FMember& M) { return M.AccountId == MyAccountId; });
		if (Room->bCreatedByClient || Room->Members.Num() == 0)
		{
			const uint32 Id = Room->RoomId;
			Rooms.RemoveAll([Id](const FRoom& R) { return R.RoomId == Id; });
		}
		else
		{
			Room->State = RoomState::Waiting;
			for (FMember& M : Room->Members)
			{
				M.bReady = false;
			}
		}
	}
	CurrentRoomId = 0;
	InvalidateTimers();
}

void FVHMockServer::HandleLeaveRoom()
{
	LeaveCurrentRoom();
	Phase = ClientPhase::Lobby;
	SC_LeaveRoom Res = VHPacket::Make<SC_LeaveRoom>(PacketId::SC_LEAVE_ROOM);
	Res.reason = LeaveReason::Self;
	Send(Res);
}

void FVHMockServer::HandleSelectCharacter(const uint8* Data, int32 Size)
{
	CS_SelectCharacter Req;
	if (!VHPacket::ReadFixed(Data, Size, Req)) { Kick(KickReason::ProtocolViolation, TEXT("[Mock] CS_SELECT_CHARACTER 크기 오류")); return; }
	FRoom* Room = CurrentRoom();
	if (!Room) return;
	FMember* Me = Room->FindMember(MyAccountId);
	if (!Me) return;

	if (Req.character_id < 1 || Req.character_id > NumCharacters) { SendRoomError(PacketId::CS_SELECT_CHARACTER, ResultCode::CharacterInvalid); return; }
	if (Me->bReady) { SendRoomError(PacketId::CS_SELECT_CHARACTER, ResultCode::NotAllowedNow); return; }
	const bool bTaken = Room->Members.ContainsByPredicate([this, &Req](const FMember& M)
	{
		return M.AccountId != MyAccountId && M.CharacterId == Req.character_id;
	});
	if (bTaken) { SendRoomError(PacketId::CS_SELECT_CHARACTER, ResultCode::CharacterTaken); return; }

	Me->CharacterId = Req.character_id;
	SendRoomState(*Room);
}

void FVHMockServer::HandleSelectPerks(const uint8* Data, int32 Size)
{
	CS_SelectPerks Req;
	if (!VHPacket::ReadFixed(Data, Size, Req)) { Kick(KickReason::ProtocolViolation, TEXT("[Mock] CS_SELECT_PERKS 크기 오류")); return; }
	FRoom* Room = CurrentRoom();
	if (!Room) return;
	FMember* Me = Room->FindMember(MyAccountId);
	if (!Me) return;

	if (Req.perk_count > MAX_PERKS) { SendRoomError(PacketId::CS_SELECT_PERKS, ResultCode::PerkInvalid); return; }
	if (Me->bReady) { SendRoomError(PacketId::CS_SELECT_PERKS, ResultCode::NotAllowedNow); return; }
	TArray<uint16> NewPerks;
	for (int32 i = 0; i < Req.perk_count; ++i)
	{
		const uint16 P = Req.perks[i];
		if (P < 1 || P > NumPerks) { SendRoomError(PacketId::CS_SELECT_PERKS, ResultCode::PerkInvalid); return; }
		if (NewPerks.Contains(P)) { SendRoomError(PacketId::CS_SELECT_PERKS, ResultCode::PerkDuplicate); return; }
		NewPerks.Add(P);
	}
	Me->Perks = NewPerks;
	SendRoomState(*Room);
}

void FVHMockServer::HandleReady(const uint8* Data, int32 Size)
{
	CS_Ready Req;
	if (!VHPacket::ReadFixed(Data, Size, Req)) { Kick(KickReason::ProtocolViolation, TEXT("[Mock] CS_READY 크기 오류")); return; }
	FRoom* Room = CurrentRoom();
	if (!Room) return;
	FMember* Me = Room->FindMember(MyAccountId);
	if (!Me) return;

	// 준비 조건: 서버 v2.1과 같이 캐릭터만 고르면 준비 가능 (퍽 3개 조건은 클라 UI가 따로 지킨다: 사용자 결정 2026-10-16)
	if (Req.ready && Me->CharacterId == 0) { SendRoomError(PacketId::CS_READY, ResultCode::NotReadyable); return; }
	Me->bReady = Req.ready;
	SendRoomState(*Room);
	CheckAllReady();
}

void FVHMockServer::ScheduleFakeJoin(int32 Index, float Delay)
{
	After(Delay, [this, Index]()
	{
		FRoom* Room = CurrentRoom();
		if (!Room || Room->State != RoomState::Waiting || Room->Members.Num() >= MAX_ROOM_PLAYERS)
		{
			return;
		}
		uint8 FreeSlot = 0;
		while (Room->Members.ContainsByPredicate([FreeSlot](const FMember& M) { return M.Slot == FreeSlot; }))
		{
			++FreeSlot;
		}
		FMember Fake;
		Fake.Slot = FreeSlot;
		Fake.AccountId = 950000 + Index;
		Fake.Name = FakeNames[Index % UE_ARRAY_COUNT(FakeNames)];
		Fake.bFake = true;
		Room->Members.Add(Fake);
		SendRoomState(*Room);
		ScheduleFakePreparation(Config.FakeActionDelaySeconds);
	});
}

void FVHMockServer::ScheduleFakePreparation(float Delay)
{
	// 가짜 플레이어 한 명씩: 캐릭터 고르기 → 퍽 고르기 → 준비. 한 번에 한 단계만 하고 다시 예약한다.
	After(Delay, [this]()
	{
		FRoom* Room = CurrentRoom();
		if (!Room || Room->State != RoomState::Waiting)
		{
			return;
		}
		for (FMember& M : Room->Members)
		{
			if (!M.bFake || M.bReady)
			{
				continue;
			}
			if (M.CharacterId == 0)
			{
				// 높은 번호부터 비어 있는 캐릭터를 고른다(사용자는 보통 1번부터 고르므로 겹침이 적다).
				for (uint16 C = NumCharacters; C >= 1; --C)
				{
					if (!Room->Members.ContainsByPredicate([C](const FMember& O) { return O.CharacterId == C; }))
					{
						M.CharacterId = C;
						break;
					}
				}
			}
			else if (M.Perks.Num() == 0)
			{
				const uint16 First = static_cast<uint16>(FMath::RandRange(1, NumPerks));
				const uint16 Second = static_cast<uint16>(First % NumPerks + 1);
				const uint16 Third = static_cast<uint16>(Second % NumPerks + 1);
				M.Perks = { First, Second, Third };	// 준비 조건(퍽 3개)에 맞춘다
			}
			else
			{
				M.bReady = true;
			}
			SendRoomState(*Room);
			CheckAllReady();
			ScheduleFakePreparation(Config.FakeActionDelaySeconds);
			return;
		}
	});
}

void FVHMockServer::CheckAllReady()
{
	FRoom* Room = CurrentRoom();
	if (!Room || Room->State != RoomState::Waiting || Room->Members.Num() == 0)
	{
		return;
	}
	for (const FMember& M : Room->Members)
	{
		if (!M.bReady)
		{
			return;
		}
	}
	// 가정: 전원 준비 → 1초 뒤 게임 로드 (명세상 시작 요청 패킷이 없음)
	const uint32 RoomId = Room->RoomId;
	After(1.0f, [this, RoomId]()
	{
		FRoom* R = CurrentRoom();
		if (!R || R->RoomId != RoomId || R->State != RoomState::Waiting)
		{
			return;
		}
		for (const FMember& M : R->Members)
		{
			if (!M.bReady) return;	// 그 사이 누군가 준비를 풀었음
		}
		StartGameLoad();
	});
}

// ───────────────────────── 게임 ─────────────────────────

void FVHMockServer::StartGameLoad()
{
	FRoom* Room = CurrentRoom();
	if (!Room) return;

	Room->State = RoomState::Loading;
	Room->MatchId = NextMatchId++;
	SendRoomState(*Room);

	SC_GameLoad Load = VHPacket::Make<SC_GameLoad>(PacketId::SC_GAME_LOAD);
	Load.match_id = Room->MatchId;
	Load.map_id = MockMapId;
	Load.map_version = UVHSettings::Get().GetGameLevelMapVersion();	// 게임 레벨이 맞춰진 서버 맵 버전을 그대로 흉내 낸다
	Load.spawn_count = static_cast<uint8>(FMath::Min(Room->Members.Num(), MAX_ROOM_PLAYERS));
	for (int32 i = 0; i < Load.spawn_count; ++i)
	{
		Load.spawns[i].player_id = Room->Members[i].AccountId;
		Load.spawns[i].spawn_index = static_cast<uint8>(i);
	}
	Load.load_timeout_ms = LOAD_TIMEOUT_MS;
	Send(Load);
	Phase = ClientPhase::Game;
}

void FVHMockServer::HandleLoadComplete(const uint8* Data, int32 Size)
{
	CS_LoadComplete Req;
	if (!VHPacket::ReadFixed(Data, Size, Req)) { Kick(KickReason::ProtocolViolation, TEXT("[Mock] CS_LOAD_COMPLETE 크기 오류")); return; }
	FRoom* Room = CurrentRoom();
	if (!Room || Room->State != RoomState::Loading) return;

	Room->State = RoomState::Playing;
	GameStartNow = Now;
	SnapshotTick = 0;
	GameEntities.Reset();
	SelfEntityId = 0;

	// 샘플 엔티티. 위치는 서버용 배치(Docs/ServerHandoff/mansion_1f_server.json v3)에서 고른 값이라 레벨과 맞는다
	auto Add = [this](EntityKind Kind, uint16 TypeId, float X, float Y, float Yaw, uint32 Param = 0, uint32 TimerMs = 0, uint64 Owner = 0) -> EntitySpawn&
	{
		EntitySpawn& E = GameEntities.AddZeroed_GetRef();
		E.entity_id = static_cast<uint16>(GameEntities.Num());
		E.kind = Kind;
		E.type_id = TypeId;
		E.owner_player_id = Owner;
		E.pos = { X, Y, 0.f };
		E.yaw = Yaw;
		E.timer_ms = TimerMs;
		E.param = Param;
		return E;
	};
	// 샘플 위치는 게임 레벨에 따라 고른다: 새 맵(L_Mansion_Shell, Shell 원점 좌표) / 옛 블록아웃 레벨(L_Mansion_1F)
	// 새 맵 좌표는 서버 맵(커밋 1b53272)의 충돌체와 겹치지 않는 자리로 골랐다(현관·중앙 홀·사무실)
	const bool bShell = UVHSettings::Get().IsShellMap();
	struct FSpot { float X; float Y; float Yaw; };
	const FSpot ThiefSpots[MAX_ROOM_PLAYERS] = {
		bShell ? FSpot{ -200.f, 2700.f, -90.f } : FSpot{ 4900.f, -2950.f, 90.f },
		bShell ? FSpot{ -400.f, 2700.f, -90.f } : FSpot{ 5100.f, -2950.f, 90.f },
		bShell ? FSpot{ 0.f, 2700.f, -90.f } : FSpot{ 4700.f, -2950.f, 90.f } };
	// 의적: 방 인원 전원 (시작 위치: 옛 레벨은 큰 홀, 새 맵은 현관). 가짜 플레이어는 제자리에 서 있다
	const int32 MemberCount = FMath::Min(Room->Members.Num(), MAX_ROOM_PLAYERS);
	for (int32 i = 0; i < MemberCount; ++i)
	{
		const FMember& M = Room->Members[i];
		const EntitySpawn& E = Add(EntityKind::Thief, M.CharacterId, ThiefSpots[i].X, ThiefSpots[i].Y, ThiefSpots[i].Yaw, 0, 0, M.AccountId);
		if (!M.bFake)
		{
			SelfEntityId = E.entity_id;
			SelfPos = E.pos;
			SelfYaw = E.yaw;
		}
	}
	if (bShell)
	{
		Add(EntityKind::Guard, 0, -300.f, -700.f, 90.f);											// 중앙 홀 (서버 guard_spawn_3)
		Add(EntityKind::Safe, 0, -900.f, 1500.f, 0.f);												// 중앙 홀
		Add(EntityKind::ReturnPoint, 0, 300.f, 1600.f, 0.f, 0, 60000);								// 중앙 홀
		Add(EntityKind::ReturnPoint, 0, -1000.f, -1500.f, 0.f, 0, 60000);							// 사무실
		Add(EntityKind::Box, 0, 800.f, 1500.f, 0.f);												// 중앙 홀
		Add(EntityKind::Box, 0, 900.f, 300.f, 0.f);
		Add(EntityKind::Box, 0, -1200.f, -300.f, 0.f);
		// 시작 위치 바로 앞(현관)과 홀 입구: 줍는 아이템 종류별로 하나씩 (정보창·테두리 확인용)
		Add(EntityKind::Valuable, static_cast<uint16>(ValuableQuality::Common), -300.f, 2600.f, 0.f, 12);
		Add(EntityKind::Valuable, static_cast<uint16>(ValuableQuality::Unique), -100.f, 1800.f, 0.f, 2000);
		Add(EntityKind::WeaponPickup, static_cast<uint16>(WeaponId::Pistol), -100.f, 2600.f, 0.f, 12);
		Add(EntityKind::ConsumablePickup, static_cast<uint16>(ConsumableId::HealKit), -300.f, 1800.f, 0.f);
		Add(EntityKind::FieldItem, static_cast<uint16>(FieldItemId::PotionS), -200.f, 2500.f, 0.f);
	}
	else
	{
		Add(EntityKind::Guard, 0, 5331.f, -6152.f, 0.f);											// guard_spawn_3
		Add(EntityKind::Safe, 0, 2895.f, -2920.f, 180.f);											// vault_slot_06 (Safe_06)
		Add(EntityKind::ReturnPoint, 0, 5600.f, -3600.f, 0.f, 0, 60000);							// 큰 홀
		Add(EntityKind::ReturnPoint, 0, 4410.f, -5014.f, 0.f, 0, 60000);							// 방 17
		Add(EntityKind::Box, 0, 6173.f, -4622.f, 0.f);												// box_slot_09
		Add(EntityKind::Box, 0, 6913.f, -4069.f, 0.f);												// box_slot_07
		Add(EntityKind::Box, 0, 2865.f, -1461.f, 0.f);												// box_slot_10
		// 시작 위치 바로 앞: 줍는 아이템 종류별로 하나씩 (정보창·테두리 확인용)
		Add(EntityKind::Valuable, static_cast<uint16>(ValuableQuality::Common), 4700.f, -2650.f, 0.f, 12);
		Add(EntityKind::Valuable, static_cast<uint16>(ValuableQuality::Unique), 4700.f, -2400.f, 0.f, 2000);
		Add(EntityKind::WeaponPickup, static_cast<uint16>(WeaponId::Pistol), 4900.f, -2650.f, 0.f, 12);
		Add(EntityKind::ConsumablePickup, static_cast<uint16>(ConsumableId::HealKit), 5100.f, -2650.f, 0.f);
		Add(EntityKind::FieldItem, static_cast<uint16>(FieldItemId::PotionS), 4900.f, -2400.f, 0.f);
	}

	SC_GameStart Start = VHPacket::Make<SC_GameStart>(PacketId::SC_GAME_START);
	Start.start_server_time_ms = ServerTimeMs();
	Start.duration_ms = static_cast<uint32>(FMath::Max(1.f, Config.GameSeconds) * 1000.f);
	Start.tick_hz = TICK_HZ;
	Start.self_entity_id = SelfEntityId;
	Start.entity_count = static_cast<uint8>(GameEntities.Num());
	for (int32 i = 0; i < GameEntities.Num(); ++i)
	{
		Start.entities[i] = GameEntities[i];
	}
	Send(Start, SC_GameStart_Size(Start.entity_count));

	// 내 상태(샘플): HP·SP 가득, 삼단봉, 스킬은 캐릭터 고정(도적1 테이저·2 끈끈이·3 은신)
	SC_PlayerStatus Status = VHPacket::Make<SC_PlayerStatus>(PacketId::SC_PLAYER_STATUS);
	Status.hp = 100;
	Status.stamina = 100;
	Status.weapons[0].id = WeaponId::Baton;
	Status.active_slot = 0;
	if (const int32 W = CVarMockWeapon.GetValueOnGameThread(); W > 0 && W <= 10)
	{
		Status.weapons[1].id = static_cast<WeaponId>(W);
		Status.weapons[1].ammo = 100;
		Status.active_slot = 1;
	}
	if (const FMember* Me = Room->FindMember(MyAccountId))
	{
		Status.skill = Me->CharacterId >= 1 && Me->CharacterId <= 3 ? static_cast<SkillId>(Me->CharacterId) : SkillId::None;
	}
	Send(Status);

	SC_Notice Notice = VHPacket::Make<SC_Notice>(PacketId::SC_NOTICE);
	VHPacket::WriteString(Notice.message, sizeof(Notice.message),
		TEXT("가짜 서버: 화면 확인용 샘플입니다. 금고·전투·반납은 판정하지 않습니다 (규칙 시험은 실서버로)"));
	Send(Notice);

	SendSnapshot();
	After(FMath::Max(1.f, Config.GameSeconds), [this]() { EndGame(); });
}

void FVHMockServer::SendSnapshot()
{
	FRoom* Room = CurrentRoom();
	if (!Room || Room->State != RoomState::Playing)
	{
		return;
	}
	SC_Snapshot Snap = VHPacket::Make<SC_Snapshot>(PacketId::SC_SNAPSHOT);
	Snap.server_tick = ++SnapshotTick;
	Snap.remaining_ms = static_cast<uint32>(FMath::Max(0.0, Config.GameSeconds - (Now - GameStartNow)) * 1000.0);
	for (const EntitySpawn& E : GameEntities)
	{
		// 플레이어·경비원만 매번 보낸다(그 밖의 오브젝트는 상태가 바뀔 때만 — 가짜 서버에서는 바뀌지 않는다)
		if (E.kind != EntityKind::Thief && E.kind != EntityKind::Guard)
		{
			continue;
		}
		EntitySnapshot& S = Snap.entities[Snap.entity_count++];
		const bool bSelf = E.entity_id == SelfEntityId;
		S.entity_id = E.entity_id;
		S.pos = bSelf ? SelfPos : E.pos;
		S.yaw = bSelf ? SelfYaw : E.yaw;
		S.hp = E.kind == EntityKind::Guard ? 200 : 100;
		S.weapon = E.kind == EntityKind::Thief ? WeaponId::Baton : WeaponId::None;
	}
	Send(Snap, SC_Snapshot_Size(Snap.entity_count));
	After(0.25f, [this]() { SendSnapshot(); });
}

void FVHMockServer::HandleAction(const uint8* Data, int32 Size)
{
	CS_Action Req;
	if (!VHPacket::ReadFixed(Data, Size, Req)) { Kick(KickReason::ProtocolViolation, TEXT("[Mock] CS_ACTION 크기 오류")); return; }
	RejectReason Reason;
	switch (Req.action)
	{
	case ActionType::InteractBegin:	Reason = RejectReason::NotAvailable; break;		// 금고·상자·줍기·반납: 판정하지 않는다
	case ActionType::UseConsumable:	Reason = RejectReason::NotAvailable; break;		// 소비 아이템 없음
	case ActionType::UseSkill:		Reason = RejectReason::InvalidState; break;
	case ActionType::SwitchWeapon:	Reason = RejectReason::OnlyOneWeapon; break;	// 삼단봉 하나뿐
	default:
		return;	// 공격 시작·끝, 구르기, 취소: 답이 필요 없다(공격 결과 SC_FIRE·SC_DAMAGE 는 보내지 않는다)
	}
	SC_ActionReject Reject = VHPacket::Make<SC_ActionReject>(PacketId::SC_ACTION_REJECT);
	Reject.seq = Req.seq;
	Reject.action = Req.action;
	Reject.reason = Reason;
	Send(Reject);
}

void FVHMockServer::EndGame()
{
	FRoom* Room = CurrentRoom();
	if (!Room || Room->State != RoomState::Playing) return;

	Room->State = RoomState::Result;

	SC_GameEnd End = VHPacket::Make<SC_GameEnd>(PacketId::SC_GAME_END);
	End.match_id = Room->MatchId;
	End.reason = EndReason::TimeUp;
	End.duration_ms = static_cast<uint32>(FMath::Max(1.f, Config.GameSeconds) * 1000.f);
	End.player_count = static_cast<uint8>(FMath::Min(Room->Members.Num(), MAX_ROOM_PLAYERS));
	// 샘플 결과: 규칙을 판정하지 않으므로 점수는 정해 둔 값이다 (가짜 플레이어 3,100$·1,800$, 나는 0$)
	const uint32 FakeBanked[2] = { 3100, 1800 };
	int32 FakeIndex = 0;
	for (int32 i = 0; i < End.player_count; ++i)
	{
		const FMember& M = Room->Members[i];
		PlayerResult& P = End.players[i];
		P.player_id = M.AccountId;
		VHPacket::WriteString(P.name, sizeof(P.name), M.Name);
		P.character_id = M.CharacterId;
		P.left_early = false;
		if (M.bFake)
		{
			P.banked_value = FakeBanked[FakeIndex++ % 2];
			P.treasures_delivered = 1;
		}
		// 최고 기록 DB가 없는 서버처럼: 이번 판 금액 그대로, 갱신 표시 없음
		P.best_record = P.banked_value;
		P.new_record = false;
	}
	// 순위: 반납 금액이 같으면 같은 순위 (서버 v3.0 규칙과 같음)
	for (int32 i = 0; i < End.player_count; ++i)
	{
		uint8 Rank = 1;
		for (int32 j = 0; j < End.player_count; ++j)
		{
			if (End.players[j].banked_value > End.players[i].banked_value) ++Rank;
		}
		End.players[i].rank = Rank;
	}
	End.result_screen_ms = static_cast<uint32>(Config.ResultScreenMs);
	Send(End);

	// result_screen_ms 뒤 자동으로 로비(메인)로. 방은 없어진다
	const uint32 RoomId = Room->RoomId;
	After(Config.ResultScreenMs / 1000.f, [this, RoomId]()
	{
		FRoom* R = CurrentRoom();
		if (!R || R->RoomId != RoomId || Phase != ClientPhase::Game) return;
		ReturnToLobbyAfterGame();
	});
}

void FVHMockServer::ReturnToLobbyAfterGame()
{
	if (FRoom* Room = CurrentRoom())
	{
		const uint32 Id = Room->RoomId;
		Rooms.RemoveAll([Id](const FRoom& R) { return R.RoomId == Id; });
	}
	CurrentRoomId = 0;
	InvalidateTimers();
	Phase = ClientPhase::Lobby;
	SC_LeaveRoom Res = VHPacket::Make<SC_LeaveRoom>(PacketId::SC_LEAVE_ROOM);
	Res.reason = LeaveReason::GameEnded;
	Send(Res);
}

void FVHMockServer::HandleReturn(const uint8* Data, int32 Size)
{
	CS_Return Req;
	if (!VHPacket::ReadFixed(Data, Size, Req)) { Kick(KickReason::ProtocolViolation, TEXT("[Mock] CS_RETURN 크기 오류")); return; }
	FRoom* Room = CurrentRoom();
	if (!Room || Room->State != RoomState::Result)
	{
		return;	// 결과 화면이 아닐 때의 복귀 요청은 무시
	}
	// 대기실/로비 선택 없이 로비(메인)로
	ReturnToLobbyAfterGame();
}
