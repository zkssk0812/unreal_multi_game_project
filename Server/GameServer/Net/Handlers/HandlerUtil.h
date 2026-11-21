#pragma once

// 핸들러 모듈들이 함께 쓰는 틀. 대부분의 핸들러는 "디코드 → 콘텐츠 입구 호출" 한 줄이라
// 디코더 함수만 바꿔 끼우는 템플릿으로 만든다.

#include "GameServer/Net/GameSession.h"
#include "GameServer/Net/PacketHandler.h"
#include "GameServer/Protocol/CommandDecoder.h"

// 게임 방에 제어 명령(선택·준비·로딩 완료 등): 방의 JobQueue로
template <auto Decode>
bool PostToRoom(GameSession& session, const char* packet, uint16 size)
{
	auto command = Decode(packet, size);
	if (!command)
		return false;					// 형식 위반 → SC_KICK
	// 방이 없으면(퇴장 직후 등) 버린다. 형식은 맞으므로 끊지 않는다.
	// 방이 있어도 오래된 방일 수 있다: GameRoom이 playerId가 자기 멤버인지 확인한다.
	if (auto room = session.Room())
		room->Post(session.PlayerId(), std::move(*command));
	return true;
}

// 게임 방에 이동·행동 입력: Job을 만들지 않고 수신함으로
template <auto Decode>
bool PushToRoom(GameSession& session, const char* packet, uint16 size)
{
	auto input = Decode(packet, size);
	if (!input)
		return false;
	if (auto room = session.Room())
		room->PushInput(session.PlayerId(), *input);
	return true;
}
