#pragma once

// 하트비트: HEARTBEAT_TIMEOUT_MS(10초) 동안 아무 패킷도 오지 않은 연결을 끊는다.
//
// 클라가 비정상으로 사라지면(전원 차단, 랜선 뽑힘) TCP는 끊김을 알려 주지 않아서 세션이 계속 남는다.
// 클라는 2초마다 CS_PING을 보내므로, 10초 동안 조용하면 사라진 것으로 본다.
// SC_KICK 뒤에 클라가 닫지 않는 경우도 여기서 정리된다(끊는 중에는 받은 패킷을 처리하지 않아 시각이 멈춘다).
//
// 전용 스레드 없이 JobTimer로 1초마다 Tick을 예약한다. Tick은 IOCP Worker 중 하나가 실행한다.

#include "ServerCore/CorePch.h"
#include "ServerCore/JobQueue.h"

class Service;

class Heartbeat final : public JobQueue
{
public:
	explicit Heartbeat(std::weak_ptr<Service> service) : _service(std::move(service)) {}

	void Start();
	void Stop();		// 다음 Tick부터 예약하지 않는다

private:
	void Tick();

	std::weak_ptr<Service> _service;
	std::atomic<bool> _running{ false };
	std::vector<SessionRef> _sessions;	// Tick마다 재사용 (용량 유지)
};
