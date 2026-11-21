#pragma once

#include "Logger.h"

class GlobalQueue;
class JobTimer;
class IocpCore;		// IocpCore는 전역 네임스페이스에 있다(ServerCore 안이 아님)

// Initialize에서 만들고 Shutdown에서 지운다.
extern GlobalQueue* GGlobalQueue;	// 실행이 밀린 JobQueue들의 줄
extern JobTimer* GJobTimer;			// DoTimer 예약

namespace ServerCore {

inline constexpr uint32_t DefaultJobTimeBudgetMs = 8; 
inline constexpr uint32_t WorkerDispatchTimeoutMs = 10; // IOCP 대기 시간 (타이머·GlobalQueue 확인 주기)

bool Initialize(const char* appName, const LogOptions& logOptions = {});
void Shutdown();


// IOCP Worker 한 개의 일. ThreadManager::Launch로 Worker 수만큼 띄운다.
//   반복: IOCP 완료 처리 → 시간이 된 타이머 분배 → GlobalQueue의 JobQueue 실행
//   한 바퀴의 Job 실행은 timeBudgetMs 안에서만(넘기면 다음 바퀴/다른 Worker로)
// iocp.PostShutdown을 받으면 돌아온다.
void RunIocpWorker(IocpCore& iocp, uint32_t timeBudgetMs = DefaultJobTimeBudgetMs);

const char* Version();

}