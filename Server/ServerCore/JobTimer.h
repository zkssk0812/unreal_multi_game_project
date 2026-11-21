#pragma once

#include "JobQueue.h"

// DoTimer로 예약한 Job을 들고 있다가 시간이 되면 해당 JobQueue에 넣는다.
// Worker 루프가 주기적으로 Distribute(NowMs())를 불러 준다.
class JobTimer
{
public:
	void Reserve(uint64 afterMs, std::weak_ptr<JobQueue> owner, Job job);

	// 시간이 된 예약을 꺼내 owner에게 넣는다. 여러 Worker가 동시에 불러도 한 명만 분배한다.
	void Distribute(uint64 nowMs);

	void Clear();
	uint32 Size();

	// 가장 이른 예약의 실행 시각(없으면 UINT64_MAX). Worker가 IOCP를 얼마나 기다릴지 정할 때 쓴다.
	uint64 NextDueMs();

private:
	struct Item
	{
		uint64 executeTickMs;
		uint64 sequence;					// 같은 시각이면 먼저 예약한 것부터
		std::weak_ptr<JobQueue> owner;
		Job job;
	};
	// std::*_heap은 최대 힙이므로 "더 늦은 것이 작다"로 비교해 가장 이른 것이 앞에 오게 한다.
	static bool Later(const Item& a, const Item& b)
	{
		return a.executeTickMs != b.executeTickMs ? a.executeTickMs > b.executeTickMs : a.sequence > b.sequence;
	}

	std::mutex _lock;
	std::vector<Item> _heap;
	uint64 _nextSequence = 0;
	std::atomic<bool> _distributing{ false };
};
