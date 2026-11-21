#include "ServerCore/CorePch.h"
#include "Content/ContentGlobal.h"
#include "Content/Interface/MessageEncoder.h"
#include "Content/Interface/RecordStore.h"
#include "Content/Lobby/GameLobby.h"

namespace Content {

namespace {
IMessageEncoder* g_encoder = nullptr;
IRecordStore* g_records = nullptr;
NullRecordStore g_nullRecords;			// 저장소를 주입하지 않았을 때
std::shared_ptr<GameLobby> g_lobby;		// 서버 수명 동안 존재
}

void Initialize(IMessageEncoder& encoder, IRecordStore* records)
{
	g_encoder = &encoder;
	g_records = records != nullptr ? records : &g_nullRecords;
	g_lobby = std::make_shared<GameLobby>();	// JobQueue는 shared_from_this를 쓰므로 shared_ptr로 만든다
	LOG_INFO("[Content] {} 초기화 (인코더 주입, 기록 저장소 {})", Version(), records != nullptr ? "있음" : "없음");
}

void Shutdown()
{
	// Worker가 모두 멈춘 뒤에 불린다. 남은 Job이 로비를 붙잡고 있을 수 있으므로 비우고 놓는다.
	if (g_lobby)
		g_lobby->Shutdown();	// 방들의 남은 Job까지 비운다
	g_lobby.reset();
	g_encoder = nullptr;
	g_records = nullptr;
}

IMessageEncoder& Encoder()
{
	// 주입 전에 쓰면 즉시 알 수 있게 (조용히 nullptr 역참조로 크래시하는 대신)
	if (g_encoder == nullptr)
	{
		LOG_ERROR("[Content] Encoder()가 Initialize 전에 호출됨");
		std::abort();
	}
	return *g_encoder;
}

IRecordStore& Records()
{
	return g_records != nullptr ? *g_records : g_nullRecords;
}

ILobbyApi& Lobby()
{
	if (!g_lobby)
	{
		LOG_ERROR("[Content] Lobby()가 Initialize 전(또는 Shutdown 후)에 호출됨");
		std::abort();
	}
	return *g_lobby;
}

const char* Version()
{
	return "VaultHeistContent v3 (PvPvE)";
}

} // namespace Content
