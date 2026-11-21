#include "ServerCore/CorePch.h"

#include <chrono>
#include <random>

#include "ServerCore/CoreGlobal.h"
#include "ServerCore/IocpCore.h"
#include "ServerCore/ObjectPool.h"
#include "ServerCore/PacketSession.h"
#include "ServerCore/SendBuffer.h"
#include "ServerCore/Service.h"
#include "ServerCore/ThreadManager.h"
#include "Protocol/protocol.h"
#include "Protocol/ProtocolUtil.h"
#include "PhaseTest.h"
#include "RoomTest.h"

// 시험 모드
//   DummyClient.exe ping [세션 수=100] [세션당 CS_PING 수=20]
//     CS_PING들을 이어 붙인 바이트를 1~7바이트씩 잘라 1ms 간격으로 보낸다.
//     SC_PONG이 보낸 개수만큼, 보낸 순서대로(client_time_ms 0,1,2…) 돌아오면 성공.
//   DummyClient.exe bad
//     잘못된 패킷 4종을 보내고 서버가 4개 연결을 모두 끊으면 성공.
//   DummyClient.exe phase   (PhaseTest.h 참고)
//     연결 단계별 패킷 규칙, 버전 불일치, 형식 위반 처리를 시나리오마다 확인한다.
//   DummyClient.exe room    (RoomTest.h 참고)
//     3명이 방을 만들고 참여·선택·준비·나가기까지 로비·방 흐름을 확인한다.

namespace {

enum class Mode { Ping, Bad, Phase, Room };

std::atomic<uint32> g_connected{ 0 };
std::atomic<uint32> g_disconnected{ 0 };
std::atomic<uint64> g_pongs{ 0 };
std::atomic<uint64> g_orderErrors{ 0 };

class DummySession : public PacketSession
{
public:
	// 보낼 바이트 전체와, 지금까지 보낸 위치. 메인 스레드만 만진다.
	std::vector<char> outgoing;
	size_t sentOffset = 0;
	uint64 expectedPong = 0;	// 다음에 와야 할 client_time_ms (수신 스레드만 만짐)

protected:
	void OnConnected() override { g_connected.fetch_add(1); }
	void OnDisconnected(const char* /*reason*/) override { g_disconnected.fetch_add(1); }

	// 클라이언트도 같은 PacketSession으로 조립한다 → 서버의 패킷도 쪼개져 오면 여기서 합쳐진다.
	void OnRecvPacket(const char* packet, uint16 size, uint16 id) override
	{
		switch (static_cast<PacketId>(id))
		{
		case PacketId::SC_PONG:
		{
			SC_Pong pong;
			if (!Protocol::ReadPacket(packet, size, pong))
				break;
			if (pong.client_time_ms != expectedPong)
				g_orderErrors.fetch_add(1);
			expectedPong = pong.client_time_ms + 1;
			g_pongs.fetch_add(1);
			return;
		}
		case PacketId::SC_KICK:
		{
			// 서버는 SC_KICK을 보낸 뒤 우리가 끊기를 기다린다(그 사이 FIN도 보냄). 받았으면 바로 끊는다.
			SC_Kick kick;
			if (Protocol::ReadPacket(packet, size, kick))
			{
				kick.message[MAX_MESSAGE_LEN] = '\0';
				LOG_INFO("세션 #{}: SC_KICK 받음 (이유 {}, \"{}\")", SessionId(), static_cast<int>(kick.reason), kick.message);
				Disconnect("kicked");
				return;
			}
			break;
		}
		default:
			break;
		}
		LOG_WARN("세션 #{}: 예상 못 한 패킷 id {} size {}", SessionId(), id, size);
	}
};

std::mutex g_sessionsLock;
std::vector<std::shared_ptr<DummySession>> g_sessions;

template <class Cond>
bool WaitUntil(Cond cond, uint32 timeoutMs)
{
	for (uint32 waited = 0; !cond(); ++waited)
	{
		if (waited >= timeoutMs)
			return false;
		::Sleep(1);
	}
	return true;
}

template <class T>
void Append(std::vector<char>& out, const T& packet)
{
	const char* bytes = reinterpret_cast<const char*>(&packet);
	out.insert(out.end(), bytes, bytes + packet.size);
}

// 세션마다 보낼 바이트를 준비한다.
void BuildPingStream(DummySession& session, uint32 pingCount)
{
	for (uint32 i = 0; i < pingCount; ++i)
	{
		auto ping = Protocol::MakePacket<CS_Ping>(PacketId::CS_PING);
		ping.client_time_ms = i;
		Append(session.outgoing, ping);
	}
}

void BuildBadStream(DummySession& session, int kind)
{
	auto assign = [&](const PacketHeader& h) {
		session.outgoing.assign(reinterpret_cast<const char*>(&h), reinterpret_cast<const char*>(&h) + sizeof(h));
	};
	switch (kind)
	{
	case 0: assign({ 2, PacketId::CS_PING }); break;						// size < 4
	case 1: assign({ 9000, PacketId::CS_PING }); break;						// size > 8192
	case 2: assign({ 4, static_cast<PacketId>(1234) }); break;				// 없는 id
	case 3: assign({ 8, PacketId::CS_PING }); session.outgoing.resize(8); break;	// CS_PING인데 크기가 12가 아님
	}
}

} // namespace

int main(int argc, char* argv[])
{
	const std::string_view modeArg = argc > 1 ? argv[1] : "ping";
	const Mode mode = modeArg == "bad" ? Mode::Bad : modeArg == "phase" ? Mode::Phase
		: modeArg == "room" ? Mode::Room : Mode::Ping;
	auto argOr = [&](int index, int fallback) { return argc > index ? std::max(1, std::atoi(argv[index])) : fallback; };

	if (!ServerCore::Initialize("DummyClient"))
		return 1;

	if (mode == Mode::Room)
	{
		const int result = RunRoomTest();
		ServerCore::Shutdown();
		return result;
	}

	if (mode == Mode::Phase)
	{
		const int result = RunPhaseTest();
		ServerCore::Shutdown();
		return result;
	}

	const uint32 sessionCount = mode == Mode::Bad ? 4 : static_cast<uint32>(argOr(2, 100));
	const uint32 pingCount = static_cast<uint32>(argOr(3, 20));

	IocpCore iocp;
	SOCKADDR_IN target{};
	target.sin_family = AF_INET;
	target.sin_port = ::htons(static_cast<uint16>(PORT));
	::inet_pton(AF_INET, "127.0.0.1", &target.sin_addr);

	auto service = std::make_shared<ClientService>(iocp, target, sessionCount, [] {
		auto session = ObjectPool<DummySession>::MakeShared();
		std::lock_guard lock(g_sessionsLock);
		g_sessions.push_back(session);
		return session;
	});

	ThreadManager threads;
	const uint32 workerCount = std::max(1u, std::thread::hardware_concurrency());
	for (uint32 i = 0; i < workerCount; ++i)
		threads.Launch(std::format("IOCP Worker {}", i + 1), [&iocp] { ServerCore::RunIocpWorker(iocp); });

	const auto begin = std::chrono::steady_clock::now();
	service->Start();
	WaitUntil([&] { return g_connected.load() == sessionCount; }, 10000);

	bool ok = false;
	{
		for (uint32 i = 0; i < g_sessions.size(); ++i)
		{
			if (mode == Mode::Ping)
				BuildPingStream(*g_sessions[i], pingCount);
			else
				BuildBadStream(*g_sessions[i], static_cast<int>(i));
		}

		// 1~7바이트씩 잘라서 1ms 간격으로 보낸다. TCP_NODELAY라 조각마다 따로 나가서
		// 서버는 헤더 반쪽, 본문 일부만 받은 상태를 실제로 겪는다.
		std::mt19937 rng(12345);
		std::uniform_int_distribution<size_t> chunk(1, 7);
		uint64 sendCalls = 0;
		for (bool pending = true; pending;)
		{
			pending = false;
			for (auto& session : g_sessions)
			{
				if (session->sentOffset >= session->outgoing.size())
					continue;
				const size_t n = std::min(chunk(rng), session->outgoing.size() - session->sentOffset);
				session->Send(MakeSendBuffer(session->outgoing.data() + session->sentOffset, static_cast<uint32>(n)));
				session->sentOffset += n;
				++sendCalls;
				pending = true;
			}
			::Sleep(1);
		}

		if (mode == Mode::Ping)
		{
			const uint64 expected = static_cast<uint64>(sessionCount) * pingCount;
			WaitUntil([&] { return g_pongs.load() == expected; }, 10000);
			const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count();
			ok = g_connected.load() == sessionCount && g_pongs.load() == expected && g_orderErrors.load() == 0 && g_disconnected.load() == 0;
			LOG_INFO("[DummyClient ping] 접속 {}/{}, 조각 송신 {}회, SC_PONG {}/{}, 순서 오류 {}, 끊김 {}, {}ms → {}",
				g_connected.load(), sessionCount, sendCalls, g_pongs.load(), expected, g_orderErrors.load(),
				g_disconnected.load(), elapsedMs, ok ? "성공" : "실패");
		}
		else
		{
			// 서버가 4개 모두 끊어야 성공
			ok = WaitUntil([&] { return g_disconnected.load() == sessionCount; }, 5000);
			LOG_INFO("[DummyClient bad] 잘못된 패킷 {}종 → 서버가 끊은 연결 {}/{} → {}",
				sessionCount, g_disconnected.load(), sessionCount, ok ? "성공" : "실패");
		}
	}

	service->DisconnectAll("client done");
	g_sessions.clear();
	WaitUntil([] { return Session::LiveCount() == 0; }, 5000);
	iocp.PostShutdown(workerCount);
	threads.JoinAll();
	service.reset();

	ServerCore::Shutdown();
	return ok ? 0 : 1;
}
