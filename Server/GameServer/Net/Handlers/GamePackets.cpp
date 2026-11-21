#include "ServerCore/CorePch.h"
#include "GameServer/Net/Handlers/HandlerUtil.h"

// 공통 인게임 3000~ : 모두 BindRoom으로 받은 방 입구로 바로 간다(로비를 거치지 않음)
//  - 드문 제어(로딩 완료·복귀): Post → GameRoom JobQueue
//  - 잦은 입력(이동·행동): PushInput → 수신함 → 다음 틱

void GamePackets::Register(PacketTable& table)
{
	using Phase = Content::ClientPhase;	// protocol.h의 전역 ClientPhase와 구분
	constexpr PhaseMask game = Mask(Phase::Game);

	table.Add(PacketId::CS_LOAD_COMPLETE, { .fn = PostToRoom<CommandDecoder::DecodeLoadComplete>, .allowed = game, .name = "CS_LOAD_COMPLETE" });
	table.Add(PacketId::CS_RETURN, { .fn = PostToRoom<CommandDecoder::DecodeReturn>, .allowed = game, .name = "CS_RETURN" });

	// 경기가 끝나 단계가 바뀐 직후에도 클라가 그 전에 보낸 입력이 도착할 수 있다 → 1초 동안은 끊지 않고 버림
	table.Add(PacketId::CS_MOVE, { .fn = PushToRoom<CommandDecoder::DecodeMove>, .allowed = game,
		.graceAfterPhaseChange = true, .name = "CS_MOVE" });
	table.Add(PacketId::CS_ACTION, { .fn = PushToRoom<CommandDecoder::DecodeAction>, .allowed = game,
		.graceAfterPhaseChange = true, .name = "CS_ACTION" });
}
