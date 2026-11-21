#include "ServerCore/CorePch.h"
#include "RoomTest.h"

#include <chrono>

#include "TestConn.h"

using namespace TestKit;

namespace {

Scoreboard g_board;

uint64 NowMs()
{
	return static_cast<uint64>(std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count());
}

// 받은 패킷을 T로 읽는다. 가변 길이 패킷(SC_ROOM_LIST 등)은 구조체보다 짧게 오므로
// "크기가 정확히 같아야 함" 대신 "구조체 크기 이하"면 받아들이고 나머지는 0으로 둔다.
template <class T>
bool ReadUpTo(const Received& r, PacketId id, T& out)
{
	if (r.kind != Received::Kind::Packet || r.id != static_cast<uint16>(id) || r.bytes.size() > sizeof(T))
		return false;
	out = T{};
	std::memcpy(&out, r.bytes.data(), r.bytes.size());
	return true;
}

// id 패킷 중 pred를 만족하는 것이 올 때까지 기다린다. 그 사이 다른 패킷은 건너뛴다.
// (방 상태는 바뀔 때마다 오므로, "2명이 된 상태"처럼 원하는 것만 골라 본다)
template <class T, class Pred>
bool WaitFor(TestConn& conn, PacketId id, T& out, Pred pred, uint32 timeoutMs = 2000)
{
	const uint64 deadline = NowMs() + timeoutMs;
	while (NowMs() < deadline)
	{
		const Received r = conn.Recv(static_cast<uint32>(deadline - NowMs()) + 1);
		if (r.kind != Received::Kind::Packet)
			return false;
		if (ReadUpTo(r, id, out) && pred(out))
			return true;
	}
	return false;
}

template <class T>
bool WaitFor(TestConn& conn, PacketId id, T& out, uint32 timeoutMs = 2000)
{
	return WaitFor(conn, id, out, [](const T&) { return true; }, timeoutMs);
}

CS_JoinRoom MakeJoin(uint32 roomId)
{
	auto join = Protocol::MakePacket<CS_JoinRoom>(PacketId::CS_JOIN_ROOM);
	join.room_id = roomId;
	return join;
}

CS_SelectCharacter MakeSelect(uint16 characterId)
{
	auto select = Protocol::MakePacket<CS_SelectCharacter>(PacketId::CS_SELECT_CHARACTER);
	select.character_id = characterId;
	return select;
}

CS_Ready MakeReady(bool ready)
{
	auto packet = Protocol::MakePacket<CS_Ready>(PacketId::CS_READY);
	packet.ready = ready;
	return packet;
}

const RoomMemberInfo* MemberOf(const SC_RoomState& state, uint64 playerId)
{
	for (uint8 i = 0; i < state.member_count && i < MAX_ROOM_PLAYERS; ++i)
		if (state.members[i].player_id == playerId)
			return &state.members[i];
	return nullptr;
}

} // namespace

int RunRoomTest()
{
	std::printf("[room] 로비·방 흐름 시험 (서버 127.0.0.1:%d)\n", PORT);

	TestConn a, b, c;
	SC_Login la{}, lb{}, lc{};
	const bool logged = a.Connect() && b.Connect() && c.Connect()
		&& LoginOk(a, "방장A", la) && LoginOk(b, "손님B", lb) && LoginOk(c, "늦은C", lc);
	g_board.Report(logged, "3명 로그인", logged ? std::format("플레이어 {}, {}, {}", la.player_id, lb.player_id, lc.player_id) : "실패");
	if (!logged)
		return 1;

	// 1) 방 만들기: SC_CREATE_ROOM(Ok) → SC_ROOM_STATE(1명, 방장 = A)
	SC_CreateRoom created{};
	SC_RoomState state{};
	bool ok = a.Send(MakeCreateRoom("  통합시험방 ")) && WaitFor(a, PacketId::SC_CREATE_ROOM, created)
		&& created.result == ResultCode::Ok && WaitFor(a, PacketId::SC_ROOM_STATE, state)
		&& state.member_count == 1 && state.host_player_id == la.player_id;
	const uint32 roomId = created.room_id;
	g_board.Report(ok, "방 만들기 → SC_CREATE_ROOM, SC_ROOM_STATE", std::format("방 {} \"{}\"", roomId, state.name));

	// 2) 방 목록: 정리된 이름과 방장 이름이 보인다
	SC_RoomList list{};
	ok = b.Send(Protocol::MakePacket<CS_RoomList>(PacketId::CS_ROOM_LIST)) && WaitFor(b, PacketId::SC_ROOM_LIST, list)
		&& list.count >= 1;
	const RoomSummary* found = nullptr;
	for (uint8 i = 0; ok && i < list.count; ++i)
		if (list.rooms[i].room_id == roomId)
			found = &list.rooms[i];
	ok = found && std::string(found->name) == "통합시험방" && std::string(found->host_name) == "방장A";
	g_board.Report(ok, "방 목록 → 앞뒤 공백이 정리된 이름, 방장 이름", std::format("{}개", list.count));

	// 3) 참여: B는 SC_JOIN_ROOM(Ok), A·B 모두 2명이 된 방 상태를 받는다
	SC_JoinRoom joined{};
	SC_RoomState stateA{}, stateB{};
	auto twoMembers = [](const SC_RoomState& s) { return s.member_count == 2; };
	ok = b.Send(MakeJoin(roomId)) && WaitFor(b, PacketId::SC_JOIN_ROOM, joined) && joined.result == ResultCode::Ok
		&& WaitFor(b, PacketId::SC_ROOM_STATE, stateB, twoMembers) && WaitFor(a, PacketId::SC_ROOM_STATE, stateA, twoMembers);
	g_board.Report(ok, "참여 → 두 사람 모두 2명 상태를 받음", ok ? "통과" : "실패");

	// 4) 캐릭터: A가 1번, B도 1번 → B만 SC_ROOM_ERROR(CharacterTaken)
	SC_RoomError error{};
	ok = a.Send(MakeSelect(1)) && WaitFor(b, PacketId::SC_ROOM_STATE, stateB,
			[&](const SC_RoomState& s) { const auto* m = MemberOf(s, la.player_id); return m && m->character_id == 1; })
		&& b.Send(MakeSelect(1)) && WaitFor(b, PacketId::SC_ROOM_ERROR, error)
		&& error.result == ResultCode::CharacterTaken && error.request_id == PacketId::CS_SELECT_CHARACTER;
	g_board.Report(ok, "같은 캐릭터 선택 → 나중 사람만 CharacterTaken", ok ? "통과" : "실패");

	// 5) C가 들어와 3명 → C가 나감 → A는 2명 상태, C는 SC_LEAVE_ROOM
	SC_LeaveRoom left{};
	SC_RoomState stateC{};
	ok = c.Send(MakeJoin(roomId)) && WaitFor(c, PacketId::SC_ROOM_STATE, stateC, [](const SC_RoomState& s) { return s.member_count == 3; })
		&& c.Send(Protocol::MakePacket<CS_LeaveRoom>(PacketId::CS_LEAVE_ROOM)) && WaitFor(c, PacketId::SC_LEAVE_ROOM, left)
		&& left.reason == LeaveReason::Self && WaitFor(a, PacketId::SC_ROOM_STATE, stateA, twoMembers);
	g_board.Report(ok, "세 번째 사람 입장 후 나가기 → 남은 방은 2명", ok ? "통과" : "실패");

	// 6) B는 2번 + 퍽 2개, 둘 다 준비 → Loading, 두 사람 모두 SC_GAME_LOAD
	auto perks = Protocol::MakePacket<CS_SelectPerks>(PacketId::CS_SELECT_PERKS);
	perks.perk_count = 2;
	perks.perks[0] = 1;
	perks.perks[1] = 2;
	auto loading = [](const SC_RoomState& s) { return s.state == RoomState::Loading; };
	SC_GameLoad loadA{}, loadB{};
	ok = b.Send(MakeSelect(2)) && b.Send(perks) && a.Send(MakeReady(true)) && b.Send(MakeReady(true))
		&& WaitFor(b, PacketId::SC_ROOM_STATE, stateB, loading)
		&& WaitFor(a, PacketId::SC_GAME_LOAD, loadA) && WaitFor(b, PacketId::SC_GAME_LOAD, loadB);
	const auto* memberB = ok ? MemberOf(stateB, lb.player_id) : nullptr;
	ok = ok && memberB && memberB->character_id == 2 && memberB->perk_count == 2 && loadA.spawn_count == 2
		&& loadA.match_id == loadB.match_id;
	g_board.Report(ok, "전원 준비 → Loading, SC_GAME_LOAD", std::format("판 {}, 맵 {} v{}", loadA.match_id,
		loadA.map_id, loadA.map_version));

	// 7) 시작한 방에는 들어갈 수 없다
	ok = c.Send(MakeJoin(roomId)) && WaitFor(c, PacketId::SC_JOIN_ROOM, joined) && joined.result == ResultCode::RoomNotWaiting;
	g_board.Report(ok, "게임이 시작된 방에 입장 → RoomNotWaiting", ok ? "통과" : "실패");

	// 8) 둘 다 로딩 완료 → SC_GAME_START, 각자 자기 의적 번호
	SC_GameStart startA{}, startB{};
	ok = a.Send(Protocol::MakePacket<CS_LoadComplete>(PacketId::CS_LOAD_COMPLETE))
		&& b.Send(Protocol::MakePacket<CS_LoadComplete>(PacketId::CS_LOAD_COMPLETE))
		&& WaitFor(a, PacketId::SC_GAME_START, startA) && WaitFor(b, PacketId::SC_GAME_START, startB)
		&& startA.self_entity_id != startB.self_entity_id && startA.tick_hz == TICK_HZ && startA.duration_ms == GAME_DURATION_MS;
	g_board.Report(ok, "로딩 완료 → SC_GAME_START", std::format("의적 {}·{}, 엔티티 {}개", startA.self_entity_id,
		startB.self_entity_id, startA.entity_count));

	// 9) A가 움직이면 B의 스냅샷에 A의 위치가 보인다 (20Hz)
	auto move = Protocol::MakePacket<CS_Move>(PacketId::CS_MOVE);
	move.pos = { 555.0f, 666.0f, 0.0f };
	move.flags = MF_MOVING;
	SC_Snapshot snapshot{};
	auto seesA = [&](const SC_Snapshot& s) {
		for (uint8 i = 0; i < s.entity_count && i < MAX_ENTITIES; ++i)
			if (s.entities[i].entity_id == startA.self_entity_id)
				return s.entities[i].pos.x == 555.0f && s.entities[i].pos.y == 666.0f;
		return false;
	};
	ok = a.Send(move) && WaitFor(b, PacketId::SC_SNAPSHOT, snapshot, seesA);
	g_board.Report(ok, "이동 → 상대 스냅샷에 위치 반영", std::format("틱 {}, 남은 시간 {}ms", snapshot.server_tick, snapshot.remaining_ms));

	// 10) 소비 아이템이 없는데 E → SC_ACTION_REJECT(NotAvailable). 공격 꾹 누르기 → 상대에게 SC_FIRE(삼단봉)와
	//     스냅샷의 Attacking 비트. 시작하자마자 자기에게 SC_PLAYER_STATUS(HP 100, 삼단봉)
	auto useItem = Protocol::MakePacket<CS_Action>(PacketId::CS_ACTION);
	useItem.seq = 7;
	useItem.action = ActionType::UseConsumable;
	auto attack = Protocol::MakePacket<CS_Action>(PacketId::CS_ACTION);
	attack.seq = 8;
	attack.action = ActionType::AttackStart;
	attack.yaw = 90.0f;
	SC_Fire fireSeen{};
	SC_ActionReject reject{};
	SC_PlayerStatus status{};
	auto attacking = [&](const SC_Snapshot& s) {
		for (uint8 i = 0; i < s.entity_count && i < MAX_ENTITIES; ++i)
			if (s.entities[i].entity_id == startA.self_entity_id)
				return (s.entities[i].state & ES_ATTACKING) != 0 && s.entities[i].weapon == WeaponId::Baton;
		return false;
	};
	ok = WaitFor(a, PacketId::SC_PLAYER_STATUS, status) && status.hp == 100 && status.weapons[0].id == WeaponId::Baton
		&& a.Send(useItem) && WaitFor(a, PacketId::SC_ACTION_REJECT, reject) && reject.seq == 7
		&& reject.reason == RejectReason::NotAvailable
		&& a.Send(attack) && WaitFor(b, PacketId::SC_FIRE, fireSeen) && fireSeen.shooter == startA.self_entity_id
		&& fireSeen.weapon == WeaponId::Baton && WaitFor(b, PacketId::SC_SNAPSHOT, snapshot, attacking);
	g_board.Report(ok, "개인 상태 → SC_PLAYER_STATUS, 빈 소비 아이템 → 거절, 공격 → 상대에게 SC_FIRE", ok ? "통과" : "실패");

	// 11) 게임 중 두 사람이 모두 끊기면 판이 끝나고 방이 사라진다.
	//     게임 중인 방은 목록에 안 보이므로, 참여를 시도해 RoomNotFound(삭제됨)인지 RoomNotWaiting(남아 있음)인지로 본다.
	a.Close();
	b.Close();
	bool gone = false;
	for (int attempt = 0; attempt < 20 && !gone; ++attempt)
	{
		::Sleep(50);
		gone = c.Send(MakeJoin(roomId)) && WaitFor(c, PacketId::SC_JOIN_ROOM, joined) && joined.result == ResultCode::RoomNotFound;
	}
	g_board.Report(gone, "게임 중 모두 끊김 → 방 삭제", gone ? "RoomNotFound" : "아직 남아 있음");

	std::printf("[DummyClient room] 통과 %d / 실패 %d → %s\n", g_board.passed, g_board.failed, g_board.failed == 0 ? "성공" : "실패");
	return g_board.failed == 0 ? 0 : 1;
}
