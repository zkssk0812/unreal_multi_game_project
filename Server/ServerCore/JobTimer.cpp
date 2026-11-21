#include "JobTimer.h"
#include "CoreClock.h"

#include <algorithm>

void JobTimer::Reserve(uint64 afterMs, std::weak_ptr<JobQueue> owner, Job job)
{
	const uint64 executeTick = ServerCore::NowMs() + afterMs;
	std::lock_guard lock(_lock);
	_heap.push_back(Item{ executeTick, _nextSequence++, std::move(owner), std::move(job) });
	std::push_heap(_heap.begin(), _heap.end(), Later);
}

void JobTimer::Distribute(uint64 nowMs)
{
	// 이미 다른 Worker가 분배 중이면 그냥 돌아간다. 모든 Worker가 같은 락에 줄 서지 않게.
	if (_distributing.exchange(true))
		return;

	std::vector<Item> due;
	{
		std::lock_guard lock(_lock);
		while (!_heap.empty() && _heap.front().executeTickMs <= nowMs)
		{
			std::pop_heap(_heap.begin(), _heap.end(), Later);	// 가장 이른 것을 맨 뒤로
			due.push_back(std::move(_heap.back()));
			_heap.pop_back();
		}
	}

	// 꺼내는 일은 끝났으니 분배 표시를 먼저 푼다. 아래 DoAsync가 그 자리에서 Job을 실행하면
	// 몇 ms가 걸릴 수 있는데, 그동안 다른 Worker가 다음 타이머를 분배하지 못하면 안 된다.
	_distributing.store(false);

	// JobQueue에 넣는 것은 락 밖에서: Push가 그 자리에서 Job을 실행할 수 있고,
	// 그 Job이 또 DoTimer(Reserve → 같은 락)를 부를 수 있다.
	for (Item& item : due)
	{
		if (JobQueueRef owner = item.owner.lock())
			owner->DoAsync(std::move(item.job));
		// owner가 이미 사라졌으면(방이 닫힘) 예약은 조용히 버려진다
	}
}

void JobTimer::Clear()
{
	std::vector<Item> items;
	{
		std::lock_guard lock(_lock);
		items.swap(_heap);
	}
}

uint64 JobTimer::NextDueMs()
{
	std::lock_guard lock(_lock);
	return _heap.empty() ? UINT64_MAX : _heap.front().executeTickMs;
}

uint32 JobTimer::Size()
{
	std::lock_guard lock(_lock);
	return static_cast<uint32>(_heap.size());
}