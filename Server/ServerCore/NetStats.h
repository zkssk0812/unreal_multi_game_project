#pragma once

#include "CorePch.h"

#include <condition_variable>

class ThreadManager;

// 네트워크 지표. 수신·송신이 일어나는 자리에서 NetStats::Add(...)로 센다.
//
// 카운터는 스레드마다 따로 있다(스레드 전용 블록). 모든 Worker가 같은 전역 원자 변수를 올리면
// 그 캐시 줄이 코어 사이를 계속 오가서(캐시 줄 핑퐁) 카운트 하나가 수십~수백 ns가 된다.
// 스레드 전용이면 자기 캐시에서만 고쳐 쓰고, 합계는 읽는 쪽(1초에 한 번)이 모든 블록을 더한다.
class NetStats
{
public:
	enum Counter : uint32
	{
		RecvBytes, RecvCalls,			// WSARecv 완료 바이트 / 횟수
		SendBytes, SendCalls,			// WSASend 완료 바이트 / 횟수
		Packets,						// PacketSession이 조립한 패킷
		PartialHeader, PartialBody,		// 덜 온 패킷 때문에 다음 수신을 기다린 횟수
		Connects, Disconnects,
		kCount
	};

	static void Add(Counter counter, uint64 value = 1);
	static std::array<uint64, kCount> Snapshot();	// 전체 스레드 합계(누적)
};

// 1초마다 [Stats] 한 줄을 로그로 남기는 스레드.
class StatsReporter
{
public:
	// extra: 매번 덧붙일 문자열(예: 현재 접속 수). 보고 스레드에서 호출된다.
	void Start(ThreadManager& threads, uint32 intervalMs, std::function<std::string()> extra = {});
	void Stop();

private:
	void Run(uint32 intervalMs);

	std::function<std::string()> _extra;
	std::mutex _lock;
	std::condition_variable _cv;
	bool _stop = false;
};
