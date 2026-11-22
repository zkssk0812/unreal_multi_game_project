// 네트워크 계층 자동화 테스트 (에디터: 창 → 테스트 자동화 → VaultHeist.Net)
#include "Misc/AutomationTest.h"
#include "Network/VHPacket.h"
#include "Network/VHMockServer.h"
#include "Network/VHSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace RobProto;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVHPacketAssemblerTest, "VaultHeist.Net.PacketAssembler",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVHPacketAssemblerTest::RunTest(const FString& Parameters)
{
	// 패킷 두 개를 이어 붙인 뒤 1바이트씩 넣어도, 한 번에 넣어도 똑같이 두 개가 나와야 한다.
	CS_Ping A = VHPacket::Make<CS_Ping>(PacketId::CS_PING);
	A.client_time_ms = 1234;
	SC_Notice B = VHPacket::Make<SC_Notice>(PacketId::SC_NOTICE);
	VHPacket::WriteString(B.message, sizeof(B.message), TEXT("공지 테스트"));
	TArray<uint8> Stream = VHPacket::ToBytes(A);
	Stream.Append(VHPacket::ToBytes(B));

	for (int32 Mode = 0; Mode < 2; ++Mode)
	{
		FVHPacketAssembler Asm;
		TArray<TArray<uint8>> Out;
		TArray<uint8> Packet;
		auto Drain = [&]()
		{
			while (Asm.Pop(Packet) == FVHPacketAssembler::EResult::Packet)
			{
				Out.Add(Packet);
			}
		};
		if (Mode == 0)
		{
			for (uint8 Byte : Stream) { Asm.Append(&Byte, 1); Drain(); }
		}
		else
		{
			Asm.Append(Stream.GetData(), Stream.Num());
			Drain();
		}
		TestEqual(TEXT("패킷 개수"), Out.Num(), 2);
		if (Out.Num() == 2)
		{
			CS_Ping A2;
			TestTrue(TEXT("CS_PING 크기"), VHPacket::ReadFixed(Out[0].GetData(), Out[0].Num(), A2));
			TestTrue(TEXT("client_time_ms"), A2.client_time_ms == 1234);
			SC_Notice B2;
			TestTrue(TEXT("SC_NOTICE 크기"), VHPacket::ReadFixed(Out[1].GetData(), Out[1].Num(), B2));
			TestEqual(TEXT("UTF-8 문자열 왕복"), VHPacket::ReadString(B2.message, sizeof(B2.message)), FString(TEXT("공지 테스트")));
		}
		TestEqual(TEXT("남은 바이트 없음"), Asm.BufferedBytes(), 0);
	}

	// 잘못된 size(< 4)는 오류
	FVHPacketAssembler Bad;
	const uint8 BadBytes[] = { 2, 0, 0, 0 };
	Bad.Append(BadBytes, 4);
	TArray<uint8> P;
	TestTrue(TEXT("size<4는 오류"), Bad.Pop(P) == FVHPacketAssembler::EResult::Error);

	// 멀티바이트 문자 경계에서 자르기: "가"(3바이트) × 3 = 9바이트를 8바이트 칸(널 포함)에 → "가가"
	char Small[8];
	VHPacket::WriteString(Small, sizeof(Small), TEXT("가가가"));
	TestEqual(TEXT("UTF-8 경계 자르기"), VHPacket::ReadString(Small, sizeof(Small)), FString(TEXT("가가")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVHMockServerFlowTest, "VaultHeist.Net.MockServerFlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVHMockServerFlowTest::RunTest(const FString& Parameters)
{
	FVHMockConfig Config;
	Config.GameSeconds = 4.f;	// 가짜 게임 길이 (끝나면 샘플 결과)
	Config.ResultScreenMs = 3000;
	Config.FakePlayersInNewRoom = 1;
	Config.FakeActionDelaySeconds = 0.1f;
	FVHMockServer Server(Config);

	TArray<TArray<uint8>> Received;
	bool bDisconnected = false;
	Server.SendToClient = [&Received](const TArray<uint8>& Bytes) { Received.Add(Bytes); };
	Server.DisconnectClient = [&bDisconnected](const FString&) { bDisconnected = true; };

	auto SendToServer = [&Server](const auto& Packet) { const TArray<uint8> B = VHPacket::ToBytes(Packet); Server.ReceivePacket(B.GetData(), B.Num()); };
	auto TypeOf = [](const TArray<uint8>& B) { return reinterpret_cast<const PacketHeader*>(B.GetData())->type; };
	auto Find = [&Received, &TypeOf](PacketId Id) -> const TArray<uint8>*
	{
		for (int32 i = Received.Num() - 1; i >= 0; --i) { if (TypeOf(Received[i]) == Id) return &Received[i]; }
		return nullptr;
	};
	double Now = FPlatformTime::Seconds();
	auto Advance = [&Server, &Now](double Seconds) { for (double t = 0; t < Seconds; t += 0.05) { Now += 0.05; Server.Tick(Now); } };

	// 1) 로그인 전 로비 패킷 → 연결 끊김 (단계별 제한)
	{
		FVHMockServer Strict(Config);
		bool bKicked = false;
		Strict.DisconnectClient = [&bKicked](const FString&) { bKicked = true; };
		const TArray<uint8> B = VHPacket::ToBytes(VHPacket::Make<CS_RoomList>(PacketId::CS_ROOM_LIST));
		Strict.ReceivePacket(B.GetData(), B.Num());
		TestTrue(TEXT("Anonymous에서 CS_ROOM_LIST → 끊김"), bKicked);
	}

	// 2) 버전 불일치
	{
		CS_Login Login = VHPacket::Make<CS_Login>(PacketId::CS_LOGIN);
		Login.protocol_version = PROTOCOL_VERSION + 1;
		VHPacket::WriteString(Login.name, sizeof(Login.name), TEXT("Tester"));
		SendToServer(Login);
		SC_Login Res;
		const TArray<uint8>* B = Find(PacketId::SC_LOGIN);
		TestTrue(TEXT("SC_LOGIN 수신"), B && VHPacket::ReadFixed(B->GetData(), B->Num(), Res));
		TestEqual(TEXT("VersionMismatch"), static_cast<int32>(Res.result), static_cast<int32>(ResultCode::VersionMismatch));
	}

	// 3) 정상 로그인
	uint64 MyId = 0;
	CS_Login Login = VHPacket::Make<CS_Login>(PacketId::CS_LOGIN);
	Login.protocol_version = PROTOCOL_VERSION;
	VHPacket::WriteString(Login.name, sizeof(Login.name), TEXT("Tester"));
	SendToServer(Login);
	{
		SC_Login Res;
		const TArray<uint8>* B = Find(PacketId::SC_LOGIN);
		TestTrue(TEXT("SC_LOGIN 수신"), B && VHPacket::ReadFixed(B->GetData(), B->Num(), Res));
		TestEqual(TEXT("로그인 Ok"), static_cast<int32>(Res.result), static_cast<int32>(ResultCode::Ok));
		MyId = Res.player_id;
	}

	// 4) 방 목록 (가변 길이 패킷 크기 검증)
	SendToServer(VHPacket::Make<CS_RoomList>(PacketId::CS_ROOM_LIST));
	{
		const TArray<uint8>* B = Find(PacketId::SC_ROOM_LIST);
		SC_RoomList List;
		const bool bOk = B && VHPacket::ReadVariable(B->GetData(), B->Num(), List, MAX_ROOM_LIST,
			[](const SC_RoomList& X) { return X.count; }, [](uint8 C) { return SC_RoomList_Size(C); });
		TestTrue(TEXT("SC_ROOM_LIST 가변 길이 검증"), bOk);
		TestTrue(TEXT("가짜 방이 있다"), bOk && List.count > 0);
		bool bOnlyWaiting = bOk;
		for (int32 i = 0; bOk && i < List.count; ++i)
		{
			bOnlyWaiting &= List.rooms[i].state == RoomState::Waiting;
		}
		TestTrue(TEXT("v2.1: 대기 중인 방만 보인다"), bOnlyWaiting);
	}

	// 5) 방 만들기 → 가짜 플레이어 입장·준비 → 내 캐릭터 선택·준비 → 게임 로드
	CS_CreateRoom Create = VHPacket::Make<CS_CreateRoom>(PacketId::CS_CREATE_ROOM);
	VHPacket::WriteString(Create.name, sizeof(Create.name), TEXT("테스트 방"));
	SendToServer(Create);
	TestNotNull(TEXT("SC_CREATE_ROOM"), Find(PacketId::SC_CREATE_ROOM));
	TestNotNull(TEXT("생성 후 SC_ROOM_STATE"), Find(PacketId::SC_ROOM_STATE));

	CS_Ready NotYet = VHPacket::Make<CS_Ready>(PacketId::CS_READY);
	NotYet.ready = true;
	SendToServer(NotYet);
	{
		const TArray<uint8>* B = Find(PacketId::SC_ROOM_ERROR);
		SC_RoomError E;
		TestTrue(TEXT("캐릭터 없이 준비 → SC_ROOM_ERROR"), B && VHPacket::ReadFixed(B->GetData(), B->Num(), E));
		TestEqual(TEXT("NotReadyable"), static_cast<int32>(E.result), static_cast<int32>(ResultCode::NotReadyable));
	}

	Advance(3.0);	// 가짜 플레이어 입장 + 캐릭터/퍽/준비
	CS_SelectCharacter Pick = VHPacket::Make<CS_SelectCharacter>(PacketId::CS_SELECT_CHARACTER);
	Pick.character_id = 1;
	SendToServer(Pick);

	// v2.1 서버 규칙: 캐릭터만 고르면 준비 가능 (퍽 3개 조건은 클라 UI가 따로 지킨다)
	CS_SelectPerks Perks = VHPacket::Make<CS_SelectPerks>(PacketId::CS_SELECT_PERKS);
	Perks.perk_count = 2;
	Perks.perks[0] = 1;
	Perks.perks[1] = 1;
	Received.Reset();
	SendToServer(Perks);
	{
		const TArray<uint8>* B = Find(PacketId::SC_ROOM_ERROR);
		SC_RoomError E;
		TestTrue(TEXT("같은 퍽 두 번 → SC_ROOM_ERROR"), B && VHPacket::ReadFixed(B->GetData(), B->Num(), E));
		TestEqual(TEXT("PerkDuplicate"), static_cast<int32>(E.result), static_cast<int32>(ResultCode::PerkDuplicate));
	}
	Received.Reset();
	SendToServer(NotYet);	// 캐릭터만 있으면 준비
	TestNull(TEXT("캐릭터만 골라도 준비 성공(오류 없음)"), Find(PacketId::SC_ROOM_ERROR));
	Advance(3.0);
	TestNotNull(TEXT("전원 준비 → SC_GAME_LOAD"), Find(PacketId::SC_GAME_LOAD));

	// 6) 로드 완료 → 게임 시작(샘플 엔티티) → 스냅샷 → 시간 종료 → 샘플 결과 → 로비
	{
		const TArray<uint8>* B = Find(PacketId::SC_GAME_LOAD);
		SC_GameLoad Load;
		TestTrue(TEXT("SC_GAME_LOAD 크기"), B && VHPacket::ReadFixed(B->GetData(), B->Num(), Load));
		// 가짜 서버는 게임 레벨이 맞춰진 서버 맵 버전(설정 GameLevelMapVersion)을 그대로 보낸다
		TestEqual(TEXT("맵 버전 = 설정의 게임 레벨 맵 버전"), static_cast<int32>(Load.map_version), static_cast<int32>(UVHSettings::Get().GetGameLevelMapVersion()));
	}
	Received.Reset();
	SendToServer(VHPacket::Make<CS_LoadComplete>(PacketId::CS_LOAD_COMPLETE));
	uint16 SelfId = 0;
	uint16 SafeId = 0;
	{
		const TArray<uint8>* B = Find(PacketId::SC_GAME_START);
		SC_GameStart Start;
		const bool bOk = B && VHPacket::ReadVariable(B->GetData(), B->Num(), Start, MAX_ENTITIES,
			[](const SC_GameStart& X) { return X.entity_count; }, SC_GameStart_Size);
		TestTrue(TEXT("SC_GAME_START 가변 길이"), bOk);
		if (bOk)
		{
			int32 CountByKind[16] = {};
			bool bSelfIsMine = false;
			for (int32 i = 0; i < Start.entity_count; ++i)
			{
				const EntitySpawn& E = Start.entities[i];
				++CountByKind[FMath::Clamp<int32>(static_cast<int32>(E.kind), 0, 15)];
				if (E.entity_id == Start.self_entity_id)
				{
					bSelfIsMine = E.kind == EntityKind::Thief && E.owner_player_id == MyId;
				}
				if (E.kind == EntityKind::Safe)
				{
					SafeId = E.entity_id;
				}
			}
			SelfId = Start.self_entity_id;
			TestEqual(TEXT("제한 시간 = 설정값"), static_cast<int32>(Start.duration_ms), 4000);
			TestTrue(TEXT("내 엔티티 = 내 계정의 의적"), bSelfIsMine);
			TestEqual(TEXT("의적 = 방 인원 수"), CountByKind[static_cast<int32>(EntityKind::Thief)], 2);
			TestTrue(TEXT("샘플: 경비원·금고·반납지점·상자가 있다"), CountByKind[static_cast<int32>(EntityKind::Guard)] >= 1
				&& CountByKind[static_cast<int32>(EntityKind::Safe)] == 1 && CountByKind[static_cast<int32>(EntityKind::ReturnPoint)] >= 1
				&& CountByKind[static_cast<int32>(EntityKind::Box)] >= 1);
			TestTrue(TEXT("샘플: 줍는 아이템 종류별로 있다"), CountByKind[static_cast<int32>(EntityKind::Valuable)] >= 1
				&& CountByKind[static_cast<int32>(EntityKind::WeaponPickup)] >= 1 && CountByKind[static_cast<int32>(EntityKind::ConsumablePickup)] >= 1
				&& CountByKind[static_cast<int32>(EntityKind::FieldItem)] >= 1);
		}
		const TArray<uint8>* S = Find(PacketId::SC_PLAYER_STATUS);
		SC_PlayerStatus Status;
		TestTrue(TEXT("SC_PLAYER_STATUS: HP 100·삼단봉"), S && VHPacket::ReadFixed(S->GetData(), S->Num(), Status)
			&& Status.hp == 100 && Status.weapons[0].id == WeaponId::Baton);
		TestNotNull(TEXT("첫 SC_SNAPSHOT"), Find(PacketId::SC_SNAPSHOT));
	}
	{
		// 이동은 클라가 한다: 보낸 위치가 다음 스냅샷의 내 위치로 돌아온다
		CS_Move Move = VHPacket::Make<CS_Move>(PacketId::CS_MOVE);
		Move.seq = 1;
		Move.pos = { 1234.f, -567.f, 0.f };
		Move.yaw = 45.f;
		SendToServer(Move);
		Received.Reset();
		Advance(0.5);
		const TArray<uint8>* B = Find(PacketId::SC_SNAPSHOT);
		SC_Snapshot Snap;
		const bool bOk = B && VHPacket::ReadVariable(B->GetData(), B->Num(), Snap, MAX_ENTITIES,
			[](const SC_Snapshot& X) { return X.entity_count; }, SC_Snapshot_Size);
		bool bMoved = false;
		for (int32 i = 0; bOk && i < Snap.entity_count; ++i)
		{
			if (Snap.entities[i].entity_id == SelfId)
			{
				bMoved = FMath::IsNearlyEqual(Snap.entities[i].pos.x, 1234.f) && FMath::IsNearlyEqual(Snap.entities[i].pos.y, -567.f)
					&& FMath::IsNearlyEqual(Snap.entities[i].yaw, 45.f);
			}
		}
		TestTrue(TEXT("스냅샷: 남은 시간 + 내 위치 = 보낸 위치"), bOk && bMoved && Snap.remaining_ms > 0 && Snap.remaining_ms < 4000);
	}
	{
		// 규칙은 판정하지 않는다: 상호작용 요청은 거절로 답하고, 공격은 답 없이 무시한다(끊지 않는다)
		CS_Action Act = VHPacket::Make<CS_Action>(PacketId::CS_ACTION);
		Act.seq = 7;
		Act.action = ActionType::InteractBegin;
		Act.target_entity_id = SafeId;
		Received.Reset();
		SendToServer(Act);
		const TArray<uint8>* B = Find(PacketId::SC_ACTION_REJECT);
		SC_ActionReject Reject;
		TestTrue(TEXT("상호작용 요청 → 거절(같은 seq)"), B && VHPacket::ReadFixed(B->GetData(), B->Num(), Reject)
			&& Reject.seq == 7 && Reject.action == ActionType::InteractBegin);
		Act.seq = 8;
		Act.action = ActionType::AttackStart;
		Received.Reset();
		SendToServer(Act);
		TestNull(TEXT("공격 시작 → 답 없음"), Find(PacketId::SC_ACTION_REJECT));
		TestNull(TEXT("공격 결과(SC_FIRE)를 만들지 않는다"), Find(PacketId::SC_FIRE));
	}
	TestNull(TEXT("시간이 남았으면 SC_GAME_END 없음"), Find(PacketId::SC_GAME_END));
	Advance(5.0);
	{
		const TArray<uint8>* B = Find(PacketId::SC_GAME_END);
		SC_GameEnd End;
		const bool bOk = B && VHPacket::ReadFixed(B->GetData(), B->Num(), End);
		TestTrue(TEXT("제한 시간이 끝나면 SC_GAME_END"), bOk);
		if (bOk)
		{
			TestEqual(TEXT("결과 인원"), static_cast<int32>(End.player_count), 2);
			TestTrue(TEXT("종료 사유 = TimeUp"), End.reason == EndReason::TimeUp);
			TestEqual(TEXT("플레이 시간 = 제한 시간"), static_cast<int32>(End.duration_ms), 4000);
			bool bMeOk = false;
			bool bFakeOk = false;
			for (int32 i = 0; i < End.player_count; ++i)
			{
				const PlayerResult& P = End.players[i];
				if (P.player_id == MyId)
				{
					// 샘플 결과: 규칙을 판정하지 않으므로 나는 0$
					bMeOk = P.banked_value == 0 && P.rank == 2 && !P.new_record && VHPacket::ReadString(P.name, sizeof(P.name)) == TEXT("Tester");
				}
				else
				{
					bFakeOk = P.banked_value == 3100 && P.rank == 1 && P.best_record == 3100 && !P.new_record
						&& VHPacket::ReadString(P.name, sizeof(P.name)) == TEXT("Bella");
				}
			}
			TestTrue(TEXT("내 샘플 결과 = 0$, 2위"), bMeOk);
			TestTrue(TEXT("가짜 플레이어 샘플 결과 = 3,100$, 1위"), bFakeOk);
		}
	}
	Received.Reset();
	SendToServer(VHPacket::Make<CS_Return>(PacketId::CS_RETURN));
	{
		const TArray<uint8>* B = Find(PacketId::SC_LEAVE_ROOM);
		SC_LeaveRoom L;
		TestTrue(TEXT("CS_RETURN → SC_LEAVE_ROOM"), B && VHPacket::ReadFixed(B->GetData(), B->Num(), L));
		TestTrue(TEXT("이유 GameEnded"), L.reason == LeaveReason::GameEnded);
	}
	// 로비 단계로 돌아왔으므로 방 목록 요청이 허용되어야 한다
	SendToServer(VHPacket::Make<CS_RoomList>(PacketId::CS_ROOM_LIST));
	TestFalse(TEXT("정상 흐름에서 끊기지 않음"), bDisconnected);
	return true;
}

#endif
