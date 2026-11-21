#include "ServerCore/CorePch.h"
#include "GameServer/Net/Heartbeat.h"

#include "ServerCore/CoreClock.h"
#include "ServerCore/Service.h"
#include "Protocol/protocol.h"
#include "GameServer/Net/GameSession.h"

namespace {
constexpr uint32 CheckIntervalMs = 1000;
}

void Heartbeat::Start()
{
	if (_running.exchange(true))
		return;
	DoTimer(CheckIntervalMs, &Heartbeat::Tick);
}

void Heartbeat::Stop()
{
	_running.store(false);
}

void Heartbeat::Tick()
{
	if (!_running.load())
		return;

	if (auto service = _service.lock())
	{
		// 목록 복사만 Service 락 안에서. Disconnect는 락 밖에서
		// (Disconnect → ReleaseSession이 같은 락을 잡으므로, 락 안에서 부르면 교착).
		service->CopySessions(_sessions);
		const uint64 now = ServerCore::NowMs();

		for (const SessionRef& base : _sessions)
		{
			// Service에는 GameSession만 들어 있다(main의 팩토리)
			GameSession& session = static_cast<GameSession&>(*base);
			const uint64 last = session.LastRecvMs();
			// 접속 처리 중이면 아직 0. 다른 Worker가 방금 갱신했으면 last > now일 수 있다(그때는 건너뜀)
			if (!session.IsConnected() || last == 0 || last >= now)
				continue;
			if (now - last >= static_cast<uint64>(HEARTBEAT_TIMEOUT_MS))
				session.Disconnect("heartbeat timeout");
		}

		// 참조를 바로 놓는다. 들고 있으면 끊긴 세션의 소멸이 다음 Tick까지 늦어진다.
		_sessions.clear();
	}

	if (_running.load())
		DoTimer(CheckIntervalMs, &Heartbeat::Tick);
}
