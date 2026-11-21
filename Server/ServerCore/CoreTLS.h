#pragma once

#include <cstdint>

class JobQueue;

inline constexpr int ThreadNameSize = 24;

extern thread_local uint32_t tls_threadId;
extern thread_local char tls_threadName[ThreadNameSize];

// JobQueue 실행 상태
extern thread_local JobQueue* tls_currentJobQueue;	// 이 스레드가 지금 실행 중인 JobQueue (없으면 nullptr)
extern thread_local uint64_t tls_endTickMs;			// 이번 실행의 마감 시각(NowMs 기준)
extern thread_local uint32_t tls_jobTimeBudgetMs;	// Push로 실행 담당이 됐을 때 쓸 시간 예산. 0이면 Job 1개만 실행
