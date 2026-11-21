#include "ServerCore/CorePch.h"
#include "GameServer/Db/SqlRecordStore.h"

#include <fstream>

#include "ServerCore/CoreClock.h"
#include "ThirdParty/nlohmann/json.hpp"

namespace {
constexpr uint64_t ReconnectEveryMs = 10000;
}

std::unique_ptr<SqlRecordStore> SqlRecordStore::FromConfig(const std::filesystem::path& configPath)
{
	std::ifstream file(configPath);
	if (!file)
	{
		LOG_WARN("[DB] {} 없음 → 최고 기록을 저장하지 않고 실행 (tools/db/README 참고)", configPath.string());
		return nullptr;
	}
	try
	{
		const nlohmann::json config = nlohmann::json::parse(file);
		const std::string cs = config.at("db").at("connection_string").get<std::string>();
		if (cs.empty())
			return nullptr;
		return std::make_unique<SqlRecordStore>(Utf8ToWide(cs));
	}
	catch (const std::exception& e)
	{
		LOG_WARN("[DB] {}를 읽지 못함 ({}) → 최고 기록 없이 실행", configPath.string(), e.what());
		return nullptr;
	}
}

SqlRecordStore::SqlRecordStore(std::wstring connectionString)
	: _connectionString(std::move(connectionString))
{
}

SqlRecordStore::~SqlRecordStore()
{
	Stop();
}

void SqlRecordStore::Start()
{
	_thread = std::thread([this] { Run(); });
	Push([this] {
		if (EnsureConnected())
			LOG_INFO("[DB] SQL Server 연결됨 (최고 기록 저장)");
	});
}

void SqlRecordStore::Stop()
{
	{
		std::lock_guard lock(_lock);
		if (_stopping)
			return;
		_stopping = true;
	}
	_wake.notify_all();
	if (_thread.joinable())
		_thread.join();
	_db.Disconnect();
}

void SqlRecordStore::Push(std::function<void()> job)
{
	{
		std::lock_guard lock(_lock);
		if (_stopping)
			return;
		_jobs.push_back(std::move(job));
	}
	_wake.notify_one();
}

void SqlRecordStore::Run()
{
	for (;;)
	{
		std::function<void()> job;
		{
			std::unique_lock lock(_lock);
			_wake.wait(lock, [this] { return _stopping || !_jobs.empty(); });
			if (_jobs.empty())
				return;			// 멈추라고 했고 남은 일이 없음
			job = std::move(_jobs.front());
			_jobs.pop_front();
		}
		job();
	}
}

bool SqlRecordStore::EnsureConnected()
{
	if (_db.Connected())
		return true;
	const uint64_t now = ServerCore::NowMs();
	if (now < _nextConnectTryMs)
		return false;			// 방금 실패했다: 매 요청마다 5초씩 기다리지 않게
	_nextConnectTryMs = now + ReconnectEveryMs;
	std::string error;
	if (_db.Connect(_connectionString, error))
		return true;
	LOG_WARN("[DB] SQL Server에 연결하지 못함: {}", error);
	return false;
}

void SqlRecordStore::LoadBest(std::vector<std::string> nicknames, std::function<void(bool, BestMap)> done)
{
	Push([this, nicknames = std::move(nicknames), done = std::move(done)] {
		BestMap best;
		if (!EnsureConnected())
			return done(false, {});
		for (const std::string& name : nicknames)
		{
			std::optional<uint32_t> value;
			std::string error;
			if (!_db.QueryBest(Utf8ToWide(name), value, error))
			{
				LOG_WARN("[DB] 최고 기록 읽기 실패: {}", error);
				_db.Disconnect();		// 다음 일에서 다시 연결
				return done(false, {});
			}
			if (value)
				best[name] = *value;
		}
		done(true, std::move(best));
	});
}

void SqlRecordStore::SaveScore(std::string nickname, uint16_t characterId, uint32_t score)
{
	Push([this, nickname = std::move(nickname), characterId, score] {
		if (!EnsureConnected())
			return;
		bool updated = false;
		std::string error;
		if (!_db.SaveScore(Utf8ToWide(nickname), characterId, score, updated, error))
		{
			LOG_WARN("[DB] 기록 저장 실패 ({}): {}", nickname, error);
			_db.Disconnect();
			return;
		}
		if (updated)
			LOG_INFO("[DB] 최고 기록 갱신: {} → {}$", nickname, score);
	});
}
