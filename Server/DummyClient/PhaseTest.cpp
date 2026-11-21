#include "ServerCore/CorePch.h"
#include "PhaseTest.h"

#include "Protocol/protocol.h"
#include "Protocol/ProtocolUtil.h"

#include "TestConn.h"

using namespace TestKit;

namespace {

TestKit::Scoreboard g_board;

void Report(bool ok, const std::string& name, const std::string& detail)
{
	g_board.Report(ok, name, detail);
}

// ───────────────── 시나리오 ─────────────────

void KickScenario(const char* name, KickReason reason, const std::function<bool(TestConn&)>& act)
{
	TestConn conn;
	if (!conn.Connect() || !act(conn))
	{
		Report(false, name, "연결·전송 실패");
		return;
	}
	const std::string error = ExpectKick(conn, reason);
	Report(error.empty(), name, error.empty() ? "SC_KICK → FIN" : error);
}

} // namespace

int RunPhaseTest()
{
	std::printf("[phase] 연결 단계별 패킷 규칙 시험 (서버 127.0.0.1:%d)\n", PORT);

	// 1) Anonymous에서 로비 패킷
	KickScenario("Anonymous에서 CS_ROOM_LIST → 추방", KickReason::ProtocolViolation, [](TestConn& c) {
		return c.Send(Protocol::MakePacket<CS_RoomList>(PacketId::CS_ROOM_LIST));
	});

	// 2) 등록되지 않은 id: SC_KICK 없이 바로 끊는다
	{
		TestConn conn;
		PacketHeader header{ 4, static_cast<PacketId>(1234) };
		const bool sent = conn.Connect() && conn.SendRaw(&header, sizeof(header));
		const Received r = conn.Recv();
		const bool ok = sent && (r.kind == Received::Kind::Closed || r.kind == Received::Kind::Reset);
		Report(ok, "등록 안 된 id 1234 → 응답 없이 끊음", KindName(r.kind));
	}

	// 3) 버전 불일치: SC_KICK이 아니라 SC_LOGIN(VersionMismatch) 후 FIN
	{
		TestConn conn;
		const bool sent = conn.Connect() && conn.Send(MakeLogin("old", PROTOCOL_VERSION + 1));
		SC_Login reply{};
		const bool gotReply = conn.Recv().As(PacketId::SC_LOGIN, reply) && reply.result == ResultCode::VersionMismatch;
		const Received after = conn.Recv();
		Report(sent && gotReply && after.kind == Received::Kind::Closed, "CS_LOGIN 버전 불일치 → SC_LOGIN(VersionMismatch) 후 끊음",
			std::format("응답 {}, 이후 {}", gotReply ? "VersionMismatch" : "없음", KindName(after.kind)));
	}

	// 4) 형식 위반: 이름 49바이트 안에 널이 없음
	KickScenario("CS_LOGIN 이름에 널 없음(형식 위반) → 추방", KickReason::ProtocolViolation, [](TestConn& c) {
		auto login = MakeLogin("");
		std::memset(login.name, 'A', sizeof(login.name));
		return c.Send(login);
	});

	// 5) 크기가 틀린 CS_PING (등록은 됐지만 형식 위반)
	KickScenario("CS_PING 크기 오류(형식 위반) → 추방", KickReason::ProtocolViolation, [](TestConn& c) {
		char bytes[8] = {};
		PacketHeader header{ 8, PacketId::CS_PING };
		std::memcpy(bytes, &header, sizeof(header));
		return c.SendRaw(bytes, sizeof(bytes));
	});

	// 6) 로그인 실패 → Anonymous로 돌아가 다시 시도 가능
	{
		TestConn conn;
		SC_Login fail{};
		SC_Login ok{};
		const bool first = conn.Connect() && conn.Send(MakeLogin("")) && conn.Recv().As(PacketId::SC_LOGIN, fail)
			&& fail.result == ResultCode::NameInvalid;
		const bool second = first && LoginOk(conn, "재시도", ok);
		Report(first && second, "빈 이름 → NameInvalid, 같은 연결로 다시 로그인 성공",
			std::format("플레이어 {}", second ? ok.player_id : 0));
	}

	// 7) 로그인 후 Lobby: 로비 패킷은 처리되고(끊기지 않음) 핑도 된다
	{
		TestConn conn;
		SC_Login reply{};
		const bool login = conn.Connect() && LoginOk(conn, "로비손님", reply);
		// SC_ROOM_LIST는 가변 길이라 크기를 보지 않고 id만 확인한다
		const bool lobby = login && conn.Send(Protocol::MakePacket<CS_RoomList>(PacketId::CS_ROOM_LIST))
			&& conn.Recv().id == static_cast<uint16>(PacketId::SC_ROOM_LIST);
		const bool alive = lobby && ExpectAlive(conn, 7001);
		Report(alive, "Lobby에서 CS_ROOM_LIST → SC_ROOM_LIST, 연결 유지",
			std::format("플레이어 {}, 핑 {}", reply.player_id, alive ? "응답" : "없음"));
	}

	// 8) Lobby에서 방 안 패킷 / 로그인 재전송
	KickScenario("Lobby에서 CS_SELECT_CHARACTER → 추방", KickReason::ProtocolViolation, [](TestConn& c) {
		SC_Login reply{};
		auto select = Protocol::MakePacket<CS_SelectCharacter>(PacketId::CS_SELECT_CHARACTER);
		select.character_id = 1;
		return LoginOk(c, "성급한손님", reply) && c.Send(select);
	});
	KickScenario("Lobby에서 CS_LOGIN 다시 → 추방", KickReason::ProtocolViolation, [](TestConn& c) {
		SC_Login reply{};
		return LoginOk(c, "두번로그인", reply) && c.Send(MakeLogin("두번로그인"));
	});

	// 9) 단계 전환 1초 유예: 로그인(Authenticating → Lobby) 직후의 CS_MOVE는 버리고, 1초가 지나면 추방
	{
		TestConn conn;
		SC_Login reply{};
		auto move = Protocol::MakePacket<CS_Move>(PacketId::CS_MOVE);
		const bool login = conn.Connect() && LoginOk(conn, "늦은입력", reply);
		const bool dropped = login && conn.Send(move) && ExpectAlive(conn, 9001);
		Report(dropped, "전환 1초 이내 CS_MOVE → 조용히 버림(연결 유지)", dropped ? "핑 응답" : "끊김");

		::Sleep(PHASE_CHANGE_GRACE_MS + 200);
		const bool sent = dropped && conn.Send(move);
		const std::string error = sent ? ExpectKick(conn, KickReason::ProtocolViolation) : "전송 실패";
		Report(error.empty(), "전환 1.2초 뒤 CS_MOVE → 추방", error.empty() ? "SC_KICK → FIN" : error);
	}

	// 10) 추방 뒤에도 클라가 계속 보낸다: 서버는 받은 것을 버리면서 SC_KICK을 끝까지 전하고 FIN
	//     (SC_KICK 직후 소켓을 닫으면, 안 읽은 데이터 때문에 RST가 나가 클라가 SC_KICK을 못 받을 수 있다)
	KickScenario("추방 직후 쓰레기 256KB를 계속 보내도 SC_KICK → FIN", KickReason::ProtocolViolation, [](TestConn& c) {
		std::vector<char> stream;
		const auto bad = Protocol::MakePacket<CS_RoomList>(PacketId::CS_ROOM_LIST);	// Anonymous에서 위반
		stream.insert(stream.end(), reinterpret_cast<const char*>(&bad), reinterpret_cast<const char*>(&bad) + bad.size);
		stream.resize(stream.size() + 256 * 1024, '\x7F');	// 패킷으로 읽으면 size 0x7F7F(범위 밖)
		return c.SendRaw(stream.data(), stream.size());
	});

	std::printf("[DummyClient phase] 통과 %d / 실패 %d → %s\n", g_board.passed, g_board.failed, g_board.failed == 0 ? "성공" : "실패");
	return g_board.failed == 0 ? 0 : 1;
}
