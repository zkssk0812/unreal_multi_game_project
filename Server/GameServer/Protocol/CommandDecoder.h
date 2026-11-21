#pragma once

// 브리지: 패킷 바이트 → 콘텐츠의 Cmd::* (docs/DESIGN.md 2장).
// 여기서는 "형식"만 검사한다: 크기, 널 종료, 개수 상한, 숫자가 NaN·무한대가 아닌지.
// 이름 규칙, 캐릭터 중복 같은 "게임 규칙"은 콘텐츠가 검사한다.
// 형식이 틀리면 nullopt → 핸들러는 SC_KICK(ProtocolViolation) 후 끊는다.
//
// enum 값의 범위(캐릭터 99번 등)는 검사하지 않는다. 값이 틀린 요청은 연결을 끊지 않고 그 요청만
// 실패 응답(SC_ROOM_ERROR 등)을 보내야 하고, 어떤 응답을 보낼지는 콘텐츠가 정하기 때문이다.

#include <optional>

#include "Content/Messages/Commands.h"

namespace CommandDecoder {

// CS_LOGIN은 프로토콜 버전도 함께 온다. 버전 검사는 브리지의 일이라 Cmd::Login과 따로 돌려준다.
struct LoginRequest
{
	uint32_t protocolVersion = 0;
	Content::Cmd::Login command;
};

// 입장
std::optional<LoginRequest> DecodeLogin(const char* packet, uint16_t size);

// 로비·방
std::optional<Content::Cmd::RoomList> DecodeRoomList(const char* packet, uint16_t size);
std::optional<Content::Cmd::CreateRoom> DecodeCreateRoom(const char* packet, uint16_t size);
std::optional<Content::Cmd::JoinRoom> DecodeJoinRoom(const char* packet, uint16_t size);
std::optional<Content::Cmd::LeaveRoom> DecodeLeaveRoom(const char* packet, uint16_t size);
std::optional<Content::Cmd::SelectCharacter> DecodeSelectCharacter(const char* packet, uint16_t size);
std::optional<Content::Cmd::SelectPerks> DecodeSelectPerks(const char* packet, uint16_t size);
std::optional<Content::Cmd::Ready> DecodeReady(const char* packet, uint16_t size);

// 게임 진행
std::optional<Content::Cmd::LoadComplete> DecodeLoadComplete(const char* packet, uint16_t size);
std::optional<Content::Cmd::Return> DecodeReturn(const char* packet, uint16_t size);
std::optional<Content::InputCmd> DecodeMove(const char* packet, uint16_t size);
std::optional<Content::InputCmd> DecodeAction(const char* packet, uint16_t size);

} // namespace CommandDecoder
