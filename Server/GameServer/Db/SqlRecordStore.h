#pragma once

// 최고 기록 저장소의 SQL Server 구현 (psd.md A-21, T-72).
//
// - DB 일은 느리고(네트워크 왕복) 막히므로 IOCP Worker나 방의 Job에서 하지 않는다.
//   전용 스레드 하나가 일 목록(_jobs)을 차례로 처리하고, 결과는 콘텐츠가 준 콜백(done)으로 돌려준다.
// - 연결이 끊기면 다음 일에서 다시 붙는다(최대 10초에 한 번 시도). 그동안은 실패로 알리고 게임은 계속.
// - 접속 문자열은 실행 파일 옆 server_config.json에서 읽는다(저장소에 커밋하지 않음).

#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "Content/Interface/RecordStore.h"
#include "GameServer/Db/DbConnection.h"

class SqlRecordStore final : public Content::IRecordStore
{
public:
	// server_config.json의 { "db": { "connection_string": "..." } }. 파일이나 값이 없으면 nullptr (DB 없이 실행)
	static std::unique_ptr<SqlRecordStore> FromConfig(const std::filesystem::path& configPath);

	explicit SqlRecordStore(std::wstring connectionString);
	~SqlRecordStore() override;

	void Start();		// DB 스레드 시작 (첫 연결 시도 포함)
	void Stop();		// 남은 일을 마치고 스레드 종료

	void LoadBest(std::vector<std::string> nicknames, std::function<void(bool ok, BestMap best)> done) override;
	void SaveScore(std::string nickname, uint16_t characterId, uint32_t score) override;

private:
	void Push(std::function<void()> job);
	void Run();
	bool EnsureConnected();

	const std::wstring _connectionString;
	DbConnection _db;					// DB 스레드만 만진다
	uint64_t _nextConnectTryMs = 0;

	std::mutex _lock;
	std::condition_variable _wake;
	std::deque<std::function<void()>> _jobs;
	bool _stopping = false;
	std::thread _thread;
};
