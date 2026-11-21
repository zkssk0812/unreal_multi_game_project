#include "IocpCore.h"
#include "CoreGlobal.h"
#include "SocketUtils.h"
#include "ThreadManager.h"
#include "JobQueue.h"
#include "JobTimer.h"
#include "CoreTLS.h"
#include "CoreClock.h"

#include <timeapi.h>
#pragma comment(lib, "winmm.lib")

GlobalQueue* GGlobalQueue = nullptr;
JobTimer* GJobTimer = nullptr;

namespace ServerCore {

bool Initialize(const char* appName, const LogOptions& logOptions)
{
	::SetConsoleOutputCP(CP_UTF8);
	::SetConsoleCP(CP_UTF8);
	// 시스템 타이머 해상도를 1ms로. 기본값(약 15.6ms)이면 IOCP를 "3ms만 기다려"라고 해도
	// 15.6ms 단위로 깨어나서 JobTimer와 Sleep·수신 대기 시간이 최대 15ms씩 늦는다. 프로세스가 도는 동안만 적용.
	::timeBeginPeriod(1);

	// 메인 스레드도 다른 스레드와 같은 준비를 거친다(번호 1, 이름 Main). 로그에 [Main]으로 보인다.
	ThreadManager::InitTLS("Main");

	// 로거를 가장 먼저 켠다: 이후 초기화 단계의 실패도 파일에 남는다.
	Log::Start(appName, logOptions);

	GGlobalQueue = new GlobalQueue();
	GJobTimer = new JobTimer();

	WSADATA wsaData;
	if (::WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		LOG_ERROR("WSAStartup 실패");
		Log::Stop();
		return false;
	}

	if (!SocketUtils::Init())
	{
		LOG_ERROR("Winsock 확장 함수 로드 실패: {}", ::WSAGetLastError());
		::WSACleanup();
		Log::Stop();
		return false;
	}

	return true;
}

void Shutdown()
{

	SocketUtils::Clear();
	::WSACleanup();

	if (GJobTimer)
		GJobTimer->Clear();

	delete GJobTimer;
	GJobTimer = nullptr;
	delete GGlobalQueue;
	GGlobalQueue = nullptr;

	// 로거는 가장 마지막에 끈다: 남은 로그를 모두 쓰고 Logger 스레드를 join한다.
	// 끄지 않으면 프로그램 종료 시 Logger 스레드가 살아 있는 채로 전역 상태가 파괴되어 비정상 종료된다.
	ThreadManager::DestroyTLS();
	Log::Stop();
	::timeEndPeriod(1);
}

void RunIocpWorker(IocpCore& iocp, uint32_t timeBudgetMs)
{
	// IOCP 완료 처리 중 Push로 바로 실행되는 Job도 이 예산을 쓴다(JobQueue::Push가 읽음)
	tls_jobTimeBudgetMs = timeBudgetMs;

	while (true)
	{
		// 1) 얼마나 기다릴지: 밀린 JobQueue가 있으면 기다리지 않고, 곧 실행할 타이머가 있으면 그때까지만.
		//    (처음 버전은 항상 10ms를 기다려서, 네트워크가 조용하면 밀린 Job이 거의 실행되지 않았다)
		uint32_t timeoutMs = WorkerDispatchTimeoutMs;
		if (GGlobalQueue->Size() > 0)
		{
			timeoutMs = 0;
		}
		else
		{
			const uint64_t now = NowMs();
			const uint64_t next = GJobTimer->NextDueMs();
			if (next <= now)
				timeoutMs = 0;
			else if (next - now < timeoutMs)
				timeoutMs = static_cast<uint32_t>(next - now);
		}

		// 2) 네트워크 완료
		if (!iocp.Dispatch(timeoutMs))
			return;		// 종료 신호

		// 3) 시간이 된 예약을 각 JobQueue에 넣는다(한 Worker만 분배)
		GJobTimer->Distribute(NowMs());

		// 4) 밀린 JobQueue를 실행. 예산 시계는 "기다림이 끝난 지금"부터 잰다.
		GGlobalQueue->Execute(NowMs() + timeBudgetMs);
	}
}

const char* Version()
{
	return "ServerCore C6";
}

}
