#pragma once

#include "CorePch.h"

#include <queue>

using Job = std::function<void()>;
using JobQueueRef = std::shared_ptr<class JobQueue>;

class JobQueue : public std::enable_shared_from_this<JobQueue>
{
public:
	virtual ~JobQueue() = default;

	void DoAsync(Job job) { Push(std::move(job)); }

	// 멤버 함수 + 인자를 Job으로. 객체의 shared_ptr를 Job이 들고 있어 실행 전에 사라지지 않는다.
	template <class T, class Ret, class... Args, class... Params>
	void DoAsync(Ret (T::*memberFn)(Args...), Params&&... params)
	{
		Push(Bind<T>(memberFn, std::forward<Params>(params)...));
	}

	// afterMs 뒤에 실행. 예약은 JobTimer가 들고 있다가 시간이 되면 이 큐에 넣는다.
	void DoTimer(uint64 afterMs, Job job);

	template <class T, class Ret, class... Args, class... Params>
	void DoTimer(uint64 afterMs, Ret (T::*memberFn)(Args...), Params&&... params)
	{
		DoTimer(afterMs, Bind<T>(memberFn, std::forward<Params>(params)...));
	}

	// GlobalQueue에서 꺼낸 Worker가 호출. 쌓인 Job을 실행한다.
	void Execute();

	// 남은 Job을 실행하지 않고 버린다(종료 시 Job이 객체를 붙잡은 순환 참조를 끊을 때).
	void ClearJobs();

	uint32 PendingJobs() const { return static_cast<uint32>(_jobCount.load()); }

protected:
	// pushOnly: 큐가 비어 있었어도 이 스레드에서 실행하지 않고 GlobalQueue로 넘긴다.
	// (DB 스레드처럼 게임 로직을 돌리면 안 되는 스레드가 결과를 넣을 때)
	void Push(Job&& job, bool pushOnly = false);

private:
	template <class T, class Ret, class... Args, class... Params>
	Job Bind(Ret (T::*memberFn)(Args...), Params&&... params)
	{
		std::shared_ptr<T> owner = std::static_pointer_cast<T>(shared_from_this());
		return [owner = std::move(owner), memberFn, ... args = std::forward<Params>(params)]() mutable {
			(owner.get()->*memberFn)(std::move(args)...);
		};
	}

	std::mutex _lock;
	std::vector<Job> _jobs;
	std::atomic<int32> _jobCount{ 0 };	// 넣었지만 아직 실행이 끝나지 않은 Job 수

    // 실행 담당만 만지는 vector.
    std::vector<Job> _executing;
};

// 실행이 밀린 JobQueue들의 줄. Worker들이 꺼내서 실행한다.
class GlobalQueue
{
public:
	void Push(JobQueueRef jobQueue);

	// endTickMs까지 큐를 꺼내 실행한다(Worker 루프에서 호출).
	void Execute(uint64 endTickMs);

	uint32 Size();

private:
	std::mutex _lock;
	std::vector<JobQueueRef> _queues;
    size_t _head = 0;
};
