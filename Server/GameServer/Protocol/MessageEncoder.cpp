#include "ServerCore/CorePch.h"
#include "MessageEncoder.h"

#include <cmath>

#include "Protocol/PacketBuilder.h"
#include "Protocol/protocol.h"
#include "GameServer/Net/GameSession.h"	// ServerTimeMs

// ───────────────── 콘텐츠 타입과 프로토콜 타입이 같은 값인지 컴파일할 때 확인 ─────────────────
// 콘텐츠는 protocol.h를 모르므로 자기 enum을 따로 갖는다. 둘이 어긋나면 여기서 빌드가 실패한다.

#define VH_SAME_ENUM(contentEnum, protocolEnum, name) \
	static_assert(static_cast<int>(contentEnum::name) == static_cast<int>(protocolEnum::name), #name " 값이 다름")

VH_SAME_ENUM(Content::ClientPhase, ClientPhase, Anonymous);
VH_SAME_ENUM(Content::ClientPhase, ClientPhase, Authenticating);
VH_SAME_ENUM(Content::ClientPhase, ClientPhase, Lobby);
VH_SAME_ENUM(Content::ClientPhase, ClientPhase, Room);
VH_SAME_ENUM(Content::ClientPhase, ClientPhase, Game);

VH_SAME_ENUM(Content::Result, ResultCode, Ok);
VH_SAME_ENUM(Content::Result, ResultCode, InvalidFormat);
VH_SAME_ENUM(Content::Result, ResultCode, ServerBusy);
VH_SAME_ENUM(Content::Result, ResultCode, InternalError);
VH_SAME_ENUM(Content::Result, ResultCode, NotAllowedNow);
VH_SAME_ENUM(Content::Result, ResultCode, NameInUse);
VH_SAME_ENUM(Content::Result, ResultCode, NameInvalid);
VH_SAME_ENUM(Content::Result, ResultCode, RoomNotFound);
VH_SAME_ENUM(Content::Result, ResultCode, RoomFull);
VH_SAME_ENUM(Content::Result, ResultCode, RoomNotWaiting);
VH_SAME_ENUM(Content::Result, ResultCode, AlreadyInRoom);
VH_SAME_ENUM(Content::Result, ResultCode, RoomNameInvalid);
VH_SAME_ENUM(Content::Result, ResultCode, CharacterTaken);
VH_SAME_ENUM(Content::Result, ResultCode, CharacterInvalid);
VH_SAME_ENUM(Content::Result, ResultCode, PerkInvalid);
VH_SAME_ENUM(Content::Result, ResultCode, PerkDuplicate);
VH_SAME_ENUM(Content::Result, ResultCode, NotReadyable);

VH_SAME_ENUM(Content::RoomState, RoomState, Waiting);
VH_SAME_ENUM(Content::RoomState, RoomState, Loading);
VH_SAME_ENUM(Content::RoomState, RoomState, Playing);
VH_SAME_ENUM(Content::RoomState, RoomState, Result);

VH_SAME_ENUM(Content::LeaveReason, LeaveReason, Self);
VH_SAME_ENUM(Content::LeaveReason, LeaveReason, Disconnected);
VH_SAME_ENUM(Content::LeaveReason, LeaveReason, GameEnded);

VH_SAME_ENUM(Content::EntityKind, EntityKind, Thief);
VH_SAME_ENUM(Content::EntityKind, EntityKind, Guard);
VH_SAME_ENUM(Content::EntityKind, EntityKind, Safe);
VH_SAME_ENUM(Content::EntityKind, EntityKind, ReturnPoint);
VH_SAME_ENUM(Content::EntityKind, EntityKind, Valuable);
VH_SAME_ENUM(Content::EntityKind, EntityKind, WeaponPickup);
VH_SAME_ENUM(Content::EntityKind, EntityKind, ConsumablePickup);
VH_SAME_ENUM(Content::EntityKind, EntityKind, FieldItem);
VH_SAME_ENUM(Content::EntityKind, EntityKind, Box);
VH_SAME_ENUM(Content::EntityKind, EntityKind, StickyTrap);
VH_SAME_ENUM(Content::EntityKind, EntityKind, Mine);
VH_SAME_ENUM(Content::EntityKind, EntityKind, Smoke);
VH_SAME_ENUM(Content::EntityKind, EntityKind, Door);

VH_SAME_ENUM(Content::ActionType, ActionType, None);
VH_SAME_ENUM(Content::ActionType, ActionType, AttackStart);
VH_SAME_ENUM(Content::ActionType, ActionType, AttackStop);
VH_SAME_ENUM(Content::ActionType, ActionType, Dodge);
VH_SAME_ENUM(Content::ActionType, ActionType, InteractBegin);
VH_SAME_ENUM(Content::ActionType, ActionType, InteractCancel);
VH_SAME_ENUM(Content::ActionType, ActionType, SwitchWeapon);
VH_SAME_ENUM(Content::ActionType, ActionType, UseSkill);
VH_SAME_ENUM(Content::ActionType, ActionType, UseConsumable);

VH_SAME_ENUM(Content::RejectReason, RejectReason, InvalidState);
VH_SAME_ENUM(Content::RejectReason, RejectReason, Cooldown);
VH_SAME_ENUM(Content::RejectReason, RejectReason, NoStamina);
VH_SAME_ENUM(Content::RejectReason, RejectReason, OutOfRange);
VH_SAME_ENUM(Content::RejectReason, RejectReason, TargetBusy);
VH_SAME_ENUM(Content::RejectReason, RejectReason, NotAvailable);
VH_SAME_ENUM(Content::RejectReason, RejectReason, Downed);
VH_SAME_ENUM(Content::RejectReason, RejectReason, Stunned);
VH_SAME_ENUM(Content::RejectReason, RejectReason, Locked);
VH_SAME_ENUM(Content::RejectReason, RejectReason, HpFull);
VH_SAME_ENUM(Content::RejectReason, RejectReason, OnlyOneWeapon);

VH_SAME_ENUM(Content::EndReason, EndReason, TimeUp);
VH_SAME_ENUM(Content::EndReason, EndReason, Aborted);
VH_SAME_ENUM(Content::InteractState, InteractState, Begin);
VH_SAME_ENUM(Content::InteractState, InteractState, Cancel);
VH_SAME_ENUM(Content::InteractState, InteractState, Complete);
VH_SAME_ENUM(Content::AlertLevel, AlertLevel, Patrol);
VH_SAME_ENUM(Content::AlertLevel, AlertLevel, Chase);
VH_SAME_ENUM(Content::AlertLevel, AlertLevel, Attack);
VH_SAME_ENUM(Content::AlertLevel, AlertLevel, Search);
VH_SAME_ENUM(Content::AlertLevel, AlertLevel, Repairing);
VH_SAME_ENUM(Content::AlertLevel, AlertLevel, Stunned);

VH_SAME_ENUM(Content::WeaponId, WeaponId, None);
VH_SAME_ENUM(Content::WeaponId, WeaponId, Baton);
VH_SAME_ENUM(Content::WeaponId, WeaponId, Sword);
VH_SAME_ENUM(Content::WeaponId, WeaponId, BeamSword);
VH_SAME_ENUM(Content::WeaponId, WeaponId, Pistol);
VH_SAME_ENUM(Content::WeaponId, WeaponId, Flamethrower);
VH_SAME_ENUM(Content::WeaponId, WeaponId, Railgun);
VH_SAME_ENUM(Content::WeaponId, WeaponId, Taser);
VH_SAME_ENUM(Content::WeaponId, WeaponId, Grenade);
VH_SAME_ENUM(Content::WeaponId, WeaponId, BlastMine);
VH_SAME_ENUM(Content::WeaponId, WeaponId, ShockMine);
VH_SAME_ENUM(Content::ConsumableId, ConsumableId, None);
VH_SAME_ENUM(Content::ConsumableId, ConsumableId, HealKit);
VH_SAME_ENUM(Content::ConsumableId, ConsumableId, Smoke);
VH_SAME_ENUM(Content::ConsumableId, ConsumableId, Flashbang);
VH_SAME_ENUM(Content::ConsumableId, ConsumableId, Jetpack);
VH_SAME_ENUM(Content::BoxType, BoxType, Weapon);
VH_SAME_ENUM(Content::BoxType, BoxType, Valuable);
VH_SAME_ENUM(Content::FieldItemId, FieldItemId, PotionS);
VH_SAME_ENUM(Content::FieldItemId, FieldItemId, PotionM);
VH_SAME_ENUM(Content::FieldItemId, FieldItemId, PotionL);
VH_SAME_ENUM(Content::ValuableQuality, ValuableQuality, Common);
VH_SAME_ENUM(Content::ValuableQuality, ValuableQuality, Advanced);
VH_SAME_ENUM(Content::ValuableQuality, ValuableQuality, Rare);
VH_SAME_ENUM(Content::ValuableQuality, ValuableQuality, Unique);
VH_SAME_ENUM(Content::SkillId, SkillId, None);
VH_SAME_ENUM(Content::SkillId, SkillId, Taser);
VH_SAME_ENUM(Content::SkillId, SkillId, Sticky);
VH_SAME_ENUM(Content::SkillId, SkillId, Cloak);
VH_SAME_ENUM(Content::SkillPhase, SkillPhase, Cast);
VH_SAME_ENUM(Content::SkillPhase, SkillPhase, Active);
VH_SAME_ENUM(Content::SkillPhase, SkillPhase, End);
VH_SAME_ENUM(Content::DownCause, DownCause, Player);
VH_SAME_ENUM(Content::DownCause, DownCause, Guard);
VH_SAME_ENUM(Content::ExplosionKind, ExplosionKind, Grenade);
VH_SAME_ENUM(Content::ExplosionKind, ExplosionKind, BlastMine);
VH_SAME_ENUM(Content::ExplosionKind, ExplosionKind, ShockMine);
VH_SAME_ENUM(Content::ExplosionKind, ExplosionKind, Flashbang);

#undef VH_SAME_ENUM

// 상태 비트와 이동 플래그도 같은 값이어야 그대로 복사할 수 있다
static_assert(Content::StateBit::Running == ES_RUNNING && Content::StateBit::Dodging == ES_DODGING
	&& Content::StateBit::Interacting == ES_INTERACTING && Content::StateBit::Exhausted == ES_EXHAUSTED
	&& Content::StateBit::Attacking == ES_ATTACKING && Content::StateBit::Downed == ES_DOWNED
	&& Content::StateBit::Invulnerable == ES_INVULNERABLE && Content::StateBit::Stunned == ES_STUNNED
	&& Content::StateBit::Slowed == ES_SLOWED && Content::StateBit::Cloaked == ES_CLOAKED
	&& Content::StateBit::InSmoke == ES_IN_SMOKE && Content::StateBit::Flashed == ES_FLASHED
	&& Content::StateBit::RailgunLock == ES_RAILGUN_LOCK && Content::StateBit::Jetpack == ES_JETPACK
	&& Content::StateBit::Repairing == ES_REPAIRING && Content::StateBit::LeftGame == ES_LEFT_GAME
	&& Content::StateBit::Opened == ES_OPENED && Content::StateBit::Warning == ES_WARNING
	&& Content::StateBit::Armed == ES_ARMED && Content::StateBit::Triggered == ES_TRIGGERED
	&& Content::StateBit::CarryingTreasure == ES_CARRYING_TREASURE
	&& Content::StateBit::TaserReady == ES_TASER_READY, "상태 비트 값이 다름");
static_assert(Content::MoveFlag::Moving == MF_MOVING && Content::MoveFlag::Running == MF_RUNNING, "이동 플래그 값이 다름");

// ───────────────── 도우미 ─────────────────

bool CopyUtf8(char* dest, size_t capacity, const std::string& source)
{
	if (capacity == 0)
		return source.empty();

	size_t len = source.size();
	const bool fits = len < capacity;	// 널 문자 자리 1바이트
	if (!fits)
	{
		len = capacity - 1;
		// 자르는 자리가 멀티바이트 글자의 중간(10xxxxxx 연속 바이트)이면, 그 글자의 시작 앞까지 물러난다.
		// 한글은 3바이트라 그냥 자르면 깨진 글자(잘린 앞 1~2바이트)가 클라이언트로 간다.
		while (len > 0 && (static_cast<unsigned char>(source[len]) & 0xC0) == 0x80)
			--len;
	}
	std::memcpy(dest, source.data(), len);
	dest[len] = '\0';
	return fits;
}

namespace {

// 배열 필드에 넣을 개수. 패킷 배열보다 많으면 잘라서 보내고 경고를 남긴다(콘텐츠가 상한을 지켰어야 함).
uint8_t CountFor(size_t count, int capacity, const char* what)
{
	if (count > static_cast<size_t>(capacity))
	{
		LOG_WARN("{}: {}개 중 {}개만 보냄 (패킷 상한)", what, count, capacity);
		return static_cast<uint8_t>(capacity);
	}
	return static_cast<uint8_t>(count);
}

::Vec3 ToVec3(const Content::Vec3& v) { return { v.x, v.y, v.z }; }

void Fill(EntitySpawn& out, const Content::Msg::EntityInfo& in)
{
	out.entity_id = in.entityId;
	out.kind = static_cast<EntityKind>(in.kind);
	out.type_id = in.typeId;
	out.owner_player_id = in.ownerPlayerId;
	out.pos = ToVec3(in.pos);
	out.yaw = in.yaw;
	out.state = in.state;
	out.timer_ms = in.timerMs;
	out.param = in.param;
}

PacketId RequestPacket(Content::RoomRequest request)
{
	switch (request)
	{
	case Content::RoomRequest::CreateRoom:      return PacketId::CS_CREATE_ROOM;
	case Content::RoomRequest::JoinRoom:        return PacketId::CS_JOIN_ROOM;
	case Content::RoomRequest::LeaveRoom:       return PacketId::CS_LEAVE_ROOM;
	case Content::RoomRequest::SelectCharacter: return PacketId::CS_SELECT_CHARACTER;
	case Content::RoomRequest::SelectPerks:     return PacketId::CS_SELECT_PERKS;
	case Content::RoomRequest::Ready:           return PacketId::CS_READY;
	}
	return PacketId::CS_CREATE_ROOM;
}

} // namespace

// ───────────────── 입장·시스템 ─────────────────

Content::OutMessage MessageEncoder::Encode(const Content::Msg::LoginResult& message)
{
	PacketBuilder<SC_Login> packet(PacketId::SC_LOGIN);
	packet->result = static_cast<ResultCode>(message.result);
	packet->player_id = message.playerId;
	if (!CopyUtf8(packet->name, sizeof(packet->name), message.name))
		LOG_WARN("SC_LOGIN 이름이 {}바이트를 넘어 잘림 (로비의 이름 규칙이 먼저 막았어야 함)", MAX_NAME_LEN);
	// 서버 시각은 보내는 순간의 값. 콘텐츠가 아니라 브리지가 채운다(시간 동기화는 프로토콜의 일).
	packet->server_time_ms = ServerTimeMs();
	return packet.Finish();
}

Content::OutMessage MessageEncoder::Encode(const Content::Msg::Notice& message)
{
	PacketBuilder<SC_Notice> packet(PacketId::SC_NOTICE);
	CopyUtf8(packet->message, sizeof(packet->message), message.message);
	return packet.Finish();
}

// ───────────────── 로비·방 ─────────────────

Content::OutMessage MessageEncoder::Encode(const Content::Msg::RoomList& message)
{
	PacketBuilder<SC_RoomList> packet(PacketId::SC_ROOM_LIST);
	packet->count = CountFor(message.rooms.size(), MAX_ROOM_LIST, "SC_ROOM_LIST");
	for (uint8_t i = 0; i < packet->count; ++i)
	{
		const Content::Msg::RoomSummary& in = message.rooms[i];
		RoomSummary& out = packet->rooms[i];
		out.room_id = in.roomId;
		CopyUtf8(out.name, sizeof(out.name), in.name);
		CopyUtf8(out.host_name, sizeof(out.host_name), in.hostName);
		out.player_count = in.playerCount;
		out.state = static_cast<RoomState>(in.state);
	}
	return packet.Finish(SC_RoomList_Size(packet->count));	// 쓴 방 수만큼만 보낸다
}

Content::OutMessage MessageEncoder::Encode(const Content::Msg::CreateRoomResult& message)
{
	PacketBuilder<SC_CreateRoom> packet(PacketId::SC_CREATE_ROOM);
	packet->result = static_cast<ResultCode>(message.result);
	packet->room_id = message.roomId;
	return packet.Finish();
}

Content::OutMessage MessageEncoder::Encode(const Content::Msg::JoinRoomResult& message)
{
	PacketBuilder<SC_JoinRoom> packet(PacketId::SC_JOIN_ROOM);
	packet->result = static_cast<ResultCode>(message.result);
	packet->room_id = message.roomId;
	return packet.Finish();
}

Content::OutMessage MessageEncoder::Encode(const Content::Msg::LeftRoom& message)
{
	PacketBuilder<SC_LeaveRoom> packet(PacketId::SC_LEAVE_ROOM);
	packet->reason = static_cast<LeaveReason>(message.reason);
	return packet.Finish();
}

Content::OutMessage MessageEncoder::Encode(const Content::Msg::RoomInfo& message)
{
	PacketBuilder<SC_RoomState> packet(PacketId::SC_ROOM_STATE);
	packet->room_id = message.roomId;
	CopyUtf8(packet->name, sizeof(packet->name), message.name);
	packet->host_player_id = message.hostPlayerId;
	packet->state = static_cast<RoomState>(message.state);
	packet->member_count = CountFor(message.members.size(), MAX_ROOM_PLAYERS, "SC_ROOM_STATE");
	for (uint8_t i = 0; i < packet->member_count; ++i)
	{
		const Content::Msg::RoomMember& in = message.members[i];
		RoomMemberInfo& out = packet->members[i];
		out.player_id = in.playerId;
		CopyUtf8(out.name, sizeof(out.name), in.name);
		out.character_id = in.characterId;
		out.perk_count = CountFor(in.perks.size(), MAX_PERKS, "SC_ROOM_STATE perks");
		for (uint8_t p = 0; p < out.perk_count; ++p)
			out.perks[p] = in.perks[p];
		out.ready = in.ready;
	}
	return packet.Finish();		// 고정 길이 (멤버 3칸 모두 보냄, 빈 칸은 0)
}

Content::OutMessage MessageEncoder::Encode(const Content::Msg::RoomError& message)
{
	PacketBuilder<SC_RoomError> packet(PacketId::SC_ROOM_ERROR);
	packet->request_id = RequestPacket(message.request);
	packet->result = static_cast<ResultCode>(message.result);
	return packet.Finish();
}


// ───────────────── 게임 진행 ─────────────────

Content::OutMessage MessageEncoder::Encode(const Content::Msg::GameLoad& message)
{
	PacketBuilder<SC_GameLoad> packet(PacketId::SC_GAME_LOAD);
	packet->match_id = message.matchId;
	packet->map_id = message.mapId;
	packet->map_version = message.mapVersion;
	packet->spawn_count = CountFor(message.spawns.size(), MAX_ROOM_PLAYERS, "SC_GAME_LOAD spawns");
	for (uint8_t i = 0; i < packet->spawn_count; ++i)
	{
		packet->spawns[i].player_id = message.spawns[i].playerId;
		packet->spawns[i].spawn_index = message.spawns[i].spawnIndex;
	}
	packet->load_timeout_ms = message.loadTimeoutMs;
	return packet.Finish();
}

Content::OutMessage MessageEncoder::Encode(const Content::Msg::GameStart& message)
{
	PacketBuilder<SC_GameStart> packet(PacketId::SC_GAME_START);
	packet->start_server_time_ms = ServerTimeMs();	// "지금 시작" — 클라는 이 값과 SC_PONG으로 남은 시간을 맞춘다
	packet->duration_ms = message.durationMs;
	packet->tick_hz = message.tickHz;
	packet->self_entity_id = message.selfEntityId;
	packet->entity_count = CountFor(message.entities.size(), MAX_ENTITIES, "SC_GAME_START");
	for (uint8_t i = 0; i < packet->entity_count; ++i)
		Fill(packet->entities[i], message.entities[i]);
	return packet.Finish(SC_GameStart_Size(packet->entity_count));
}

Content::OutMessage MessageEncoder::Encode(const Content::Msg::Snapshot& message)
{
	PacketBuilder<SC_Snapshot> packet(PacketId::SC_SNAPSHOT);
	packet->server_tick = message.tick;
	packet->remaining_ms = message.remainingMs;
	packet->entity_count = CountFor(message.entities.size(), MAX_ENTITIES, "SC_SNAPSHOT");
	for (uint8_t i = 0; i < packet->entity_count; ++i)
	{
		const Content::Msg::EntityState& in = message.entities[i];
		EntitySnapshot& out = packet->entities[i];
		out.entity_id = in.entityId;
		out.state = in.state;
		out.pos = ToVec3(in.pos);
		out.yaw = in.yaw;
		out.hp = in.hp;
		out.action = static_cast<ActionType>(in.action);
		out.weapon = static_cast<WeaponId>(in.weapon);
	}
	return packet.Finish(SC_Snapshot_Size(packet->entity_count));
}

Content::OutMessage MessageEncoder::Encode(const Content::Msg::ActionReject& message)
{
	PacketBuilder<SC_ActionReject> packet(PacketId::SC_ACTION_REJECT);
	packet->seq = message.seq;
	packet->action = static_cast<ActionType>(message.action);
	packet->reason = static_cast<RejectReason>(message.reason);
	return packet.Finish();
}

Content::OutMessage MessageEncoder::Encode(const Content::Msg::EntitySpawn& message)
{
	PacketBuilder<SC_EntitySpawn> packet(PacketId::SC_ENTITY_SPAWN);
	packet->count = CountFor(message.entities.size(), MAX_ENTITIES, "SC_ENTITY_SPAWN");
	for (uint8_t i = 0; i < packet->count; ++i)
		Fill(packet->entities[i], message.entities[i]);
	return packet.Finish(SC_EntitySpawn_Size(packet->count));
}

Content::OutMessage MessageEncoder::Encode(const Content::Msg::EntityDespawn& message)
{
	PacketBuilder<SC_EntityDespawn> packet(PacketId::SC_ENTITY_DESPAWN);
	packet->count = CountFor(message.entityIds.size(), MAX_ENTITIES, "SC_ENTITY_DESPAWN");
	for (uint8_t i = 0; i < packet->count; ++i)
		packet->entity_ids[i] = message.entityIds[i];
	return packet.Finish(SC_EntityDespawn_Size(packet->count));
}

Content::OutMessage MessageEncoder::Encode(const Content::Msg::GameEnd& message)
{
	PacketBuilder<SC_GameEnd> packet(PacketId::SC_GAME_END);
	packet->match_id = message.matchId;
	packet->reason = static_cast<EndReason>(message.reason);
	packet->duration_ms = message.durationMs;
	packet->player_count = CountFor(message.players.size(), MAX_ROOM_PLAYERS, "SC_GAME_END");
	for (uint8_t i = 0; i < packet->player_count; ++i)
	{
		const Content::Msg::PlayerResult& in = message.players[i];
		PlayerResult& out = packet->players[i];
		out.player_id = in.playerId;
		CopyUtf8(out.name, sizeof(out.name), in.name);
		out.character_id = in.characterId;
		out.left_early = in.leftEarly;
		out.banked_value = in.banked;
		out.rank = in.rank;
		out.best_record = in.bestRecord;
		out.new_record = in.newRecord;
		out.kills = in.kills;
		out.downs = in.downs;
		out.treasures_delivered = in.treasuresDelivered;
	}
	packet->result_screen_ms = message.resultScreenMs;
	return packet.Finish();
}

// ───────────────── 게임 이벤트 ─────────────────

Content::OutMessage MessageEncoder::Encode(const Content::Msg::Fire& message)
{
	PacketBuilder<SC_Fire> packet(PacketId::SC_FIRE);
	packet->shooter = message.shooter;
	packet->weapon = static_cast<WeaponId>(message.weapon);
	packet->origin = ToVec3(message.origin);
	packet->yaw = message.yaw;
	packet->hit_entity = message.hitEntity;
	return packet.Finish();
}

Content::OutMessage MessageEncoder::Encode(const Content::Msg::Damage& message)
{
	PacketBuilder<SC_Damage> packet(PacketId::SC_DAMAGE);
	packet->attacker = message.attacker;
	packet->target = message.target;
	packet->weapon = static_cast<WeaponId>(message.weapon);
	packet->amount = message.amount;
	packet->hp_after = message.hpAfter;
	packet->shield_after = message.shieldAfter;
	return packet.Finish();
}

Content::OutMessage MessageEncoder::Encode(const Content::Msg::Downed& message)
{
	PacketBuilder<SC_Downed> packet(PacketId::SC_DOWNED);
	packet->victim = message.victim;
	packet->attacker = message.attacker;
	packet->cause = static_cast<DownCause>(message.cause);
	packet->lost_banked = message.lostBanked;
	packet->respawn_ms = message.respawnMs;
	return packet.Finish();
}

Content::OutMessage MessageEncoder::Encode(const Content::Msg::Respawned& message)
{
	PacketBuilder<SC_Respawned> packet(PacketId::SC_RESPAWNED);
	packet->thief = message.thief;
	packet->pos = ToVec3(message.pos);
	packet->invuln_ms = message.invulnMs;
	return packet.Finish();
}

Content::OutMessage MessageEncoder::Encode(const Content::Msg::Projectile& message)
{
	PacketBuilder<SC_Projectile> packet(PacketId::SC_PROJECTILE);
	packet->thrower = message.thrower;
	packet->kind = static_cast<ExplosionKind>(message.kind);
	packet->from = ToVec3(message.from);
	packet->to = ToVec3(message.to);
	packet->flight_ms = message.flightMs;
	return packet.Finish();
}

Content::OutMessage MessageEncoder::Encode(const Content::Msg::Interact& message)
{
	PacketBuilder<SC_Interact> packet(PacketId::SC_INTERACT);
	packet->actor = message.actor;
	packet->target = message.target;
	packet->state = static_cast<InteractState>(message.state);
	packet->progress_ms = message.progressMs;
	packet->duration_ms = message.durationMs;
	return packet.Finish();
}

Content::OutMessage MessageEncoder::Encode(const Content::Msg::SafeOpened& message)
{
	PacketBuilder<SC_SafeOpened> packet(PacketId::SC_SAFE_OPENED);
	packet->safe = message.safe;
	packet->opener = message.opener;
	return packet.Finish();
}

Content::OutMessage MessageEncoder::Encode(const Content::Msg::Money& message)
{
	PacketBuilder<SC_Money> packet(PacketId::SC_MONEY);
	packet->thief_count = CountFor(message.thieves.size(), MAX_ROOM_PLAYERS, "SC_MONEY");
	for (uint8_t i = 0; i < packet->thief_count; ++i)
	{
		packet->thieves[i].thief = message.thieves[i].thief;
		packet->thieves[i].carried_value = message.thieves[i].carried;
		packet->thieves[i].banked_value = message.thieves[i].banked;
	}
	return packet.Finish();
}

Content::OutMessage MessageEncoder::Encode(const Content::Msg::Explosion& message)
{
	PacketBuilder<SC_Explosion> packet(PacketId::SC_EXPLOSION);
	packet->kind = static_cast<ExplosionKind>(message.kind);
	packet->pos = ToVec3(message.pos);
	packet->radius = message.radius;
	return packet.Finish();
}

Content::OutMessage MessageEncoder::Encode(const Content::Msg::TreasureTaken& message)
{
	PacketBuilder<SC_TreasureTaken> packet(PacketId::SC_TREASURE_TAKEN);
	packet->thief = message.thief;
	packet->pos = ToVec3(message.pos);
	packet->value = message.value;
	return packet.Finish();
}

Content::OutMessage MessageEncoder::Encode(const Content::Msg::GuardAlert& message)
{
	PacketBuilder<SC_GuardAlert> packet(PacketId::SC_GUARD_ALERT);
	packet->guard = message.guard;
	packet->level = static_cast<AlertLevel>(message.level);
	packet->target = message.target;
	return packet.Finish();
}

Content::OutMessage MessageEncoder::Encode(const Content::Msg::Knockback& message)
{
	PacketBuilder<SC_Knockback> packet(PacketId::SC_KNOCKBACK);
	packet->target = message.target;
	packet->yaw = message.yaw;
	packet->distance = message.distance;
	return packet.Finish();
}

Content::OutMessage MessageEncoder::Encode(const Content::Msg::PlayerStatus& message)
{
	PacketBuilder<SC_PlayerStatus> packet(PacketId::SC_PLAYER_STATUS);
	packet->hp = message.hp;
	packet->shield = message.shield;
	packet->stamina = message.stamina;
	for (int i = 0; i < 2; ++i)
	{
		packet->weapons[i].id = static_cast<WeaponId>(message.weapons[i].id);
		packet->weapons[i].ammo = message.weapons[i].ammo;
	}
	packet->active_slot = message.activeSlot;
	packet->consumable = static_cast<ConsumableId>(message.consumable);
	packet->skill = static_cast<SkillId>(message.skill);
	packet->skill_cooldown_ms = message.skillCooldownMs;
	packet->skill_active_ms = message.skillActiveMs;
	packet->death_save_left = message.deathSaveLeft;
	return packet.Finish();
}

Content::OutMessage MessageEncoder::Encode(const Content::Msg::Skill& message)
{
	PacketBuilder<SC_Skill> packet(PacketId::SC_SKILL);
	packet->caster = message.caster;
	packet->skill = static_cast<SkillId>(message.skill);
	packet->phase = static_cast<SkillPhase>(message.phase);
	packet->pos = ToVec3(message.pos);
	return packet.Finish();
}

Content::OutMessage MessageEncoder::Encode(const Content::Msg::Pickup& message)
{
	PacketBuilder<SC_Pickup> packet(PacketId::SC_PICKUP);
	packet->thief = message.thief;
	packet->kind = static_cast<EntityKind>(message.kind);
	packet->type_id = message.typeId;
	packet->param = message.param;
	return packet.Finish();
}
