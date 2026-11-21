#include "JobQueue.h"
#include "CoreClock.h"
#include "CoreGlobal.h"
#include "CoreTLS.h"
#include "JobTimer.h"

// ───────────────────────── JobQueue ─────────────────────────

void JobQueue::Push(Job&& job, bool pushOnly)
{
	const int32 prevCount = _jobCount.fetch_add(1);
	{
		std::lock_guard lock(_lock);
		_jobs.push_back(std::move(job));
	}

	if (prevCount != 0)
		return;

	// 내가 실행 담당이 됐다.
	if (tls_currentJobQueue == nullptr && !pushOnly)
	{
		// 이 스레드가 다른 JobQueue를 실행 중이 아니면 바로 실행한다(지연 최소).
		// 마감 시각은 "지금부터 예산만큼". Worker가 아닌 스레드(예산 0)는 Job 1개만 하고 GlobalQueue로 넘긴다.
		tls_endTickMs = ServerCore::NowMs() + tls_jobTimeBudgetMs;
		Execute();
	}
	else
	{
		// 다른 JobQueue의 Job 안에서 Push했다(예: Room이 Lobby에 알림).
		// 여기서 바로 실행하면 호출이 중첩되어 스택이 깊어지고, 지금 Job의 실행이 길어진다.
		// → GlobalQueue에 맡겨 다른 Worker(또는 나중의 나)가 실행하게 한다.
		GGlobalQueue->Push(shared_from_this());
	}
}

void JobQueue::Execute()
{
	const JobQueueRef self = shared_from_this();//jobqueue.clear()시 충돌방지
	tls_currentJobQueue = this;

	while (true)
	{
		{
			std::lock_guard lock(_lock);
			_executing.swap(_jobs);	// 쌓인 것을 통째로 가져간다. 실행 중 새로 들어오는 Job은 다음 바퀴에.
		}

        std::vector<Job>& jobs = _executing;
		// Job 하나마다 시간 예산을 본다. 묶음을 다 실행한 뒤에만 보면, 100개가 쌓였을 때 예산이 무의미해진다.
		size_t executed = 0;
		bool outOfTime = false;
		while (executed < jobs.size())
		{
			jobs[executed]();
			++executed;
			if (executed < jobs.size() && ServerCore::NowMs() >= tls_endTickMs)
			{
				outOfTime = true;
				break;
			}
		}

		if (outOfTime)
		{
			// 못 한 Job을 큐 "맨 앞"에 되돌린다. 그사이 새로 들어온 Job보다 먼저 실행되어야 순서가 지켜진다.
			std::lock_guard lock(_lock);
            _jobs.insert(_jobs.begin(), std::make_move_iterator(jobs.begin() + executed),
                std::make_move_iterator(jobs.end()));
		}
        jobs.clear();

		// 실행한 만큼 개수를 내린다. 0이 됐다면 그사이 들어온 것이 없다 → 실행 담당 끝.
		const int32 done = static_cast<int32>(executed);
		if (_jobCount.fetch_sub(done) == done)
		{
			tls_currentJobQueue = nullptr;
			return;
		}

		// 아직 남았다. 시간 예산을 넘겼으면 다른 Worker에게 넘긴다(한 방이 Worker를 독점하지 않게).
		if (outOfTime || ServerCore::NowMs() >= tls_endTickMs)
		{
			tls_currentJobQueue = nullptr;
			GGlobalQueue->Push(self);
			return;
		}
	}
}

void JobQueue::DoTimer(uint64 afterMs, Job job)
{
	// 타이머는 이 큐를 weak_ptr로 든다: 예약이 걸려 있다는 이유로 방이 사라지지 못하면 안 된다.
	GJobTimer->Reserve(afterMs, weak_from_this(), std::move(job));
}

void JobQueue::ClearJobs()
{
	std::vector<Job> jobs;
	{
		std::lock_guard lock(_lock);
		jobs.swap(_jobs);
	}
	_jobCount.fetch_sub(static_cast<int32>(jobs.size()));
	// jobs가 여기서 소멸하며 Job이 들고 있던 shared_ptr들이 풀린다(락 밖에서)
}

// ───────────────────────── GlobalQueue ─────────────────────────

void GlobalQueue::Push(JobQueueRef jobQueue)
{
	std::lock_guard lock(_lock);
	_queues.push_back(std::move(jobQueue));
}

void GlobalQueue::Execute(uint64 endTickMs)
{
	tls_endTickMs = endTickMs;	// 꺼낸 JobQueue들이 이 마감을 함께 쓴다
	while (ServerCore::NowMs() < endTickMs)
	{
		JobQueueRef jobQueue;
		{
			std::lock_guard lock(_lock);
			if (_head == _queues.size())
				return;
			jobQueue = std::move(_queues[_head++]);
			if (_head == _queues.size())
			{
				_queues.clear();	// 다 꺼냈으면 처음으로(용량 유지)
				_head = 0;
			}
			else if (_head >= 1024 && _head * 2 >= _queues.size())
			{
				// 계속 밀려서 비지 않는 경우: 앞쪽의 꺼낸 자리를 정리해 vector가 끝없이 커지지 않게
				_queues.erase(_queues.begin(), _queues.begin() + static_cast<ptrdiff_t>(_head));
				_head = 0;
			}
		}
		jobQueue->Execute();	// 락 밖에서
	}
}

uint32 GlobalQueue::Size()
{
	std::lock_guard lock(_lock);
	return static_cast<uint32>(_queues.size() - _head);
}
