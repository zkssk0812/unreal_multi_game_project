#include "../ServerCore/CorePch.h"
#include "../ServerCore/CoreGlobal.h"
#include "../ServerCore/IocpCore.h"
#include "../ServerCore/NetStats.h"
#include "../ServerCore/ObjectPool.h"
#include "../ServerCore/SendBuffer.h"
#include "../ServerCore/Service.h"
#include "../ServerCore/Session.h"
#include "../ServerCore/ThreadManager.h"
#include "../Shared/Protocol/protocol.h"
#include "Content/ContentGlobal.h"
#include "Content/Interface/MessageEncoder.h"
#include "Protocol/MessageEncoder.h"
#include "Net/GameSession.h"
#include "Net/PacketHandler.h"
#include "Net/Heartbeat.h"
#include "Db/SqlRecordStore.h"

#include <filesystem>
#include <iostream>

#include "Content/Game/MapData.h"
#include "Content/Perf.h"

namespace {
constexpr uint32 AcceptCount = 16;		// 미리 걸어 둘 AcceptEx 수
constexpr uint32 ShutdownWaitMs = 5000;

// 실행 파일이 있는 폴더. 작업 폴더(어디서 실행했나)와 상관없이 옆에 복사된 Data/Maps를 찾기 위해
std::filesystem::path ExeDir()
{
	wchar_t buffer[MAX_PATH] = {};
	::GetModuleFileNameW(nullptr, buffer, MAX_PATH);
	return std::filesystem::path(buffer).parent_path();
}
constexpr uint32 StatsIntervalMs = 5000;	// 처리량 지표(NetStats)를 찍는 간격. 오가는 것이 없으면 찍지 않는다

// cond가 참이 될 때까지 최대 timeoutMs 기다린다.
template <class Cond>
bool WaitUntil(Cond cond, uint32 timeoutMs)
{
	for (uint32 waited = 0; !cond(); ++waited)
	{
		if (waited >= timeoutMs)
			return false;
		::Sleep(1);
	}
	return true;
}

// 콘솔 명령. 메인 스레드는 원래 종료 신호만 기다리므로 그 자리에서 명령을 읽는다.
void RunConsole(Service& service)
{
	std::string line;
	while (std::getline(std::cin, line))
	{
		if (line.empty() || line == "quit")
			return;

		if (line.starts_with("notice "))
		{
			// 한 번 인코딩한 버퍼를 모든 세션이 공유한다
			const SendBufferRef buffer = Content::Encoder().Encode(Content::Msg::Notice{ line.substr(7) });
			const uint32 sent = service.Broadcast(buffer);
			LOG_INFO("공지 → {}명 (패킷 1개 {}바이트를 공유)", sent, buffer->WriteSize());
			continue;
		}
		LOG_INFO("명령: notice <메시지> | quit");
	}
}
} // namespace

int main()
{
	// 로그는 Info부터: 접속·해제·로그인 같은 연결마다 찍히는 Debug 로그는 걸러진다(문자열도 만들지 않음)
	ServerCore::LogOptions logOptions;
	logOptions.minLevel = ServerCore::LogLevel::Info;
	if (!ServerCore::Initialize("GameServer", logOptions))
		return 1;

	LOG_INFO("[GameServer] {}, protocol 0x{:08X}", ServerCore::Version(), PROTOCOL_VERSION);
	ServerTimeMs();	// 서버 시각의 기준점(0ms)을 지금으로 고정

	// 맵: 실행 파일 옆 Data/Maps/mansion_1f_server.json을 한 번 읽어 메모리에 둔다(모든 방이 읽기 전용으로 공유).
	// 맵이 없거나 규칙에 안 맞으면 시작하지 않는다 — 게임 중에 이상한 일이 생기기 전에 여기서 드러나게.
	const std::filesystem::path mapPath = ExeDir() / "Data" / "Maps" / "mansion_1f_server.json";
	std::string mapError;
	if (!Content::LoadDefaultMap(mapPath, mapError))
	{
		LOG_ERROR("[Map] 맵을 읽지 못해 서버를 시작하지 않습니다: {}", mapError);
		ServerCore::Shutdown();
		return 1;
	}
	const Content::MapData& map = Content::DefaultMap();
	const auto itemRooms = std::count_if(map.rooms.begin(), map.rooms.end(), [](const Content::MapRoom& r) { return !r.returnCells.empty(); });
	LOG_INFO("[Map] {} v{}: 방 {}(금고·아이템을 놓는 방 {}), 순찰 지점 {}, 충돌체 {}, 격자 {}x{}", map.key, map.version,
		map.rooms.size(), itemRooms, map.patrolNodes.size(), map.colliders.size(), map.nav.rows, map.nav.cols);
	for (const std::string& warning : map.warnings)
		LOG_WARN("[Map] {}", warning);		// 게임은 돌지만 맵 팀에 알려야 할 것 (문 없는 방 등)

	// 최고 기록 DB (SQL Server). 실행 파일 옆 server_config.json에 접속 문자열이 있으면 쓰고, 없으면 기록 없이 실행
	std::unique_ptr<SqlRecordStore> records = SqlRecordStore::FromConfig(ExeDir() / "server_config.json");
	if (records)
		records->Start();

	// 계층 조립: ServerCore 시작 → 콘텐츠 생성(인코더·기록 저장소 주입) → 패킷 표 → 접속 받기
	// 인코더·저장소는 브리지의 것. 콘텐츠는 IMessageEncoder·IRecordStore 인터페이스로만 안다.
	MessageEncoder encoder;
	Content::Initialize(encoder, records.get());

	// 핸들러 표는 접속을 받기 전에 다 채운다. 이후로는 읽기만 하므로 Worker들이 락 없이 본다.
	PacketHandler::Initialize();

	IocpCore iocp;
	if (!iocp.IsValid())
		return 1;

	auto service = std::make_shared<ServerService>(iocp, static_cast<uint16>(PORT), AcceptCount,
		[] { return ObjectPool<GameSession>::MakeShared(); });
	if (!service->Start())//listen
		return 1;

	// 모든 스레드는 ThreadManager로: 번호·이름이 붙고(로그·디버거), 끝날 때 송신 덩어리를 반납한다.
	ThreadManager threads;
	const uint32 workerCount = std::max(1u, std::thread::hardware_concurrency());
	for (uint32 i = 0; i < workerCount; ++i)
		threads.Launch(std::format("IOCP Worker {}", i + 1), [&iocp] { ServerCore::RunIocpWorker(iocp); });

	// 하트비트 검사. Worker가 돌기 시작한 뒤에 시작해야 JobTimer 예약이 실행된다.
	auto heartbeat = std::make_shared<Heartbeat>(service);	// JobQueue라 shared_ptr로
	heartbeat->Start();

	StatsReporter stats;
	// 처리량 줄 끝에 붙는 내용: 접속 수 + 게임 틱 지표(게임 중인 방, 틱 평균·최대 µs) + 경비원 AI 비용
	stats.Start(threads, StatsIntervalMs, [&service] {
		const Content::Perf::TickReport tick = Content::Perf::TakeTickReport();
		return std::format("접속 {} | 게임 방 {}, 틱 {}회 평균 {}µs 최대 {}µs | AI 평균 {}µs, 경로 {}회 평균 {}µs 최대 {}µs 칸 {}, 시야 검사 {}",
			service->SessionCount(), tick.playingRooms, tick.ticks, tick.avgUs, tick.maxUs,
			tick.aiAvgUs, tick.paths, tick.pathAvgUs, tick.pathMaxUs, tick.pathAvgExpanded, tick.sightTests);
	});

	LOG_INFO("IOCP Worker {}개 실행. 명령: notice <메시지> | quit (빈 줄도 종료)", workerCount);
	RunConsole(*service);

	// 종료 순서
	// 1) 새 접속 막기: 걸린 AcceptEx가 모두 돌아올 때까지 Worker는 계속 돈다
	heartbeat->Stop();	// 종료 중에는 검사하지 않는다(DisconnectAll과 겹치지 않게)
	service->Stop();
	if (!WaitUntil([&] { return service->PendingAccepts() == 0; }, ShutdownWaitMs))
		LOG_WARN("AcceptEx {}개가 돌아오지 않음", service->PendingAccepts());

	// 2) 모든 세션 끊기 → 걸린 I/O가 취소되어 돌아오고 세션이 소멸할 때까지 대기
	service->DisconnectAll("server shutdown");
	if (!WaitUntil([] { return Session::LiveCount() == 0; }, ShutdownWaitMs))
		LOG_WARN("세션 {}개가 소멸하지 않음", Session::LiveCount());

	// 3) 스레드 종료: 지표 보고를 멈추고, Worker에 종료 신호를 보낸 뒤 모두 기다린다
	stats.Stop();
	iocp.PostShutdown(workerCount);
	threads.JoinAll();
	service.reset();

	// 4) 마무리 통계. 메인 스레드의 덩어리도 놓아서, 만든 덩어리가 모두 풀에 돌아왔는지 본다.
	SendBufferManager::ReleaseThreadChunk();
	const auto total = NetStats::Snapshot();
	const auto sendStats = SendBufferManager::GetStats();
	const auto logStats = ServerCore::Log::GetStats();
	LOG_INFO("종료. 접속 {}회, 패킷 {}개 조립(헤더 대기 {}회, 본문 대기 {}회), 수신 {}B, 송신 {}B (WSASend {}회)",
		total[NetStats::Connects], total[NetStats::Packets], total[NetStats::PartialHeader], total[NetStats::PartialBody],
		total[NetStats::RecvBytes], total[NetStats::SendBytes], total[NetStats::SendCalls]);
	LOG_INFO("송신 덩어리: 만든 수 {}, 풀에 돌아온 수 {} | 로그: 쓴 줄 {}, 버린 줄 {}, 큐 최대 {}",
		sendStats.chunksCreated, sendStats.chunksInPool, logStats.written, logStats.dropped, logStats.maxQueueDepth);
	heartbeat.reset();

	Content::Shutdown();	// 인코더(지역 변수)·저장소보다 먼저
	if (records)
		records->Stop();	// 남은 기록 저장을 마치고 DB 스레드 종료
	ServerCore::Shutdown();
	return 0;
}
