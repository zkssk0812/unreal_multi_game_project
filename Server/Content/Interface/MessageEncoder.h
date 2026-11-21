#pragma once

// 콘텐츠의 Msg를 보낼 수 있는 형태(OutMessage)로 바꾸는 입구. 구현은 브리지(GameServer/Protocol/MessageEncoder).
// 콘텐츠는 결과물(OutMessage)의 내용을 보지 않고 IClientChannel::Send에 넘기기만 한다.
// 방 전원에게 같은 것을 보낼 때는 한 번만 Encode하고 같은 OutMessage를 여러 채널에 넘긴다.

#include "Content/Interface/ClientChannel.h"
#include "Content/Messages/Messages.h"

// 보내는 메시지 목록. 여기 한 줄을 넣으면 인터페이스·브리지 선언·시험용 가짜 인코더가 함께 늘어난다
// (브리지 MessageEncoder.cpp에는 Encode 본문을 직접 써야 한다 — 빠뜨리면 링크 오류로 알 수 있다).
#define VH_MESSAGES(X) \
	X(LoginResult) X(Notice) \
	X(RoomList) X(CreateRoomResult) X(JoinRoomResult) X(LeftRoom) X(RoomInfo) X(RoomError) \
	X(GameLoad) X(GameStart) X(Snapshot) X(ActionReject) X(EntitySpawn) X(EntityDespawn) X(GameEnd) \
	X(Fire) X(Damage) X(Downed) X(Respawned) X(Projectile) X(Interact) X(SafeOpened) X(Money) \
	X(Explosion) X(TreasureTaken) X(GuardAlert) X(Knockback) X(PlayerStatus) X(Skill) X(Pickup)

namespace Content {

class IMessageEncoder
{
public:
	virtual ~IMessageEncoder() = default;

#define VH_DECLARE_ENCODE(Type) virtual OutMessage Encode(const Msg::Type& message) = 0;
	VH_MESSAGES(VH_DECLARE_ENCODE)
#undef VH_DECLARE_ENCODE
};

} // namespace Content
