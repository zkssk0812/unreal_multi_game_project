#pragma once

// 받는 명령(Cmd::*): 브리지가 패킷을 해석해 만들어 콘텐츠에 넘기는 순수 구조체.

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "Content/Messages/Types.h"

namespace Content::Cmd {

// ── 입장 ──
struct Login { std::string name; };		// 닉네임(빈 이름·길이 규칙은 로비가 검사)

// ── 로비 ──
struct RoomList {};
struct CreateRoom { std::string name; };
struct JoinRoom { uint32_t roomId = 0; };

// ── 방 (대기) ──
struct LeaveRoom {};
struct SelectCharacter { uint16_t characterId = 0; };
struct SelectPerks { std::vector<uint16_t> perks; };	// 0~3개(개수 상한은 브리지, 중복 검사는 콘텐츠)
struct Ready { bool ready = false; };

// ── 게임 진행 (제어) ──
struct LoadComplete {};
struct Return {};						// 결과 화면 → 메인

} // namespace Content::Cmd

namespace Content {

// 방에 Post로 들어가는 명령들
using RoomCmd = std::variant<Cmd::LeaveRoom, Cmd::SelectCharacter, Cmd::SelectPerks, Cmd::Ready,
	Cmd::LoadComplete, Cmd::Return>;

// 게임 중 입력(이동·행동). 방의 수신함에 쌓였다가 다음 틱에서 처리된다.
struct InputCmd
{
	enum class Kind : uint8_t { Move, Action } kind = Kind::Move;
	uint16_t seq = 0;
	Vec3 pos{};								// Move: 클라가 정한 위치(서버가 그대로 씀)
	float yaw = 0.0f;
	uint8_t flags = 0;						// Move: MoveFlag
	ActionType action = ActionType::None;	// Action
	uint16_t targetEntityId = 0;			// Action: 상호작용 대상
	Vec3 aim{};								// Action: 커서가 가리키는 바닥 지점 (투척 조준, A-14)
};

} // namespace Content
