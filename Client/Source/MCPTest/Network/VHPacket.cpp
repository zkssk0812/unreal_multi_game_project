#include "Network/VHPacket.h"

using namespace RobProto;

namespace VHPacket
{
	void WriteString(char* Dest, int32 DestSize, const FString& Src)
	{
		check(Dest && DestSize > 0);
		FMemory::Memzero(Dest, DestSize);

		const FTCHARToUTF8 Utf8(*Src);
		const ANSICHAR* Bytes = Utf8.Get();
		const int32 SrcLen = Utf8.Length();

		// 마지막 1바이트는 널 종료용으로 남긴다.
		int32 Len = FMath::Min(SrcLen, DestSize - 1);
		// 잘린 위치가 UTF-8 연속 바이트(10xxxxxx)면 문자 경계까지 뒤로 물린다.
		while (Len > 0 && Len < SrcLen && (static_cast<uint8>(Bytes[Len]) & 0xC0) == 0x80)
		{
			--Len;
		}
		FMemory::Memcpy(Dest, Bytes, Len);
	}

	FString ReadString(const char* Src, int32 SrcSize)
	{
		int32 Len = 0;
		while (Len < SrcSize && Src[Len] != '\0')
		{
			++Len;
		}
		const FUTF8ToTCHAR Conv(reinterpret_cast<const ANSICHAR*>(Src), Len);
		return FString::ConstructFromPtrSize(Conv.Get(), Conv.Length());
	}

	int32 Utf8Length(const FString& Str)
	{
		return FTCHARToUTF8(*Str).Length();
	}

	const TCHAR* PacketName(uint16 Type)
	{
		switch (static_cast<PacketId>(Type))
		{
		case PacketId::CS_LOGIN: return TEXT("CS_LOGIN");
		case PacketId::SC_LOGIN: return TEXT("SC_LOGIN");
		case PacketId::CS_ROOM_LIST: return TEXT("CS_ROOM_LIST");
		case PacketId::SC_ROOM_LIST: return TEXT("SC_ROOM_LIST");
		case PacketId::CS_CREATE_ROOM: return TEXT("CS_CREATE_ROOM");
		case PacketId::SC_CREATE_ROOM: return TEXT("SC_CREATE_ROOM");
		case PacketId::CS_JOIN_ROOM: return TEXT("CS_JOIN_ROOM");
		case PacketId::SC_JOIN_ROOM: return TEXT("SC_JOIN_ROOM");
		case PacketId::CS_LEAVE_ROOM: return TEXT("CS_LEAVE_ROOM");
		case PacketId::SC_LEAVE_ROOM: return TEXT("SC_LEAVE_ROOM");
		case PacketId::SC_ROOM_STATE: return TEXT("SC_ROOM_STATE");
		case PacketId::CS_SELECT_CHARACTER: return TEXT("CS_SELECT_CHARACTER");
		case PacketId::CS_SELECT_PERKS: return TEXT("CS_SELECT_PERKS");
		case PacketId::CS_READY: return TEXT("CS_READY");
		case PacketId::SC_ROOM_ERROR: return TEXT("SC_ROOM_ERROR");
		case PacketId::SC_GAME_LOAD: return TEXT("SC_GAME_LOAD");
		case PacketId::CS_LOAD_COMPLETE: return TEXT("CS_LOAD_COMPLETE");
		case PacketId::SC_GAME_START: return TEXT("SC_GAME_START");
		case PacketId::CS_MOVE: return TEXT("CS_MOVE");
		case PacketId::SC_SNAPSHOT: return TEXT("SC_SNAPSHOT");
		case PacketId::CS_ACTION: return TEXT("CS_ACTION");
		case PacketId::SC_ACTION_REJECT: return TEXT("SC_ACTION_REJECT");
		case PacketId::SC_ENTITY_SPAWN: return TEXT("SC_ENTITY_SPAWN");
		case PacketId::SC_ENTITY_DESPAWN: return TEXT("SC_ENTITY_DESPAWN");
		case PacketId::SC_GAME_END: return TEXT("SC_GAME_END");
		case PacketId::CS_RETURN: return TEXT("CS_RETURN");
		case PacketId::SC_FIRE: return TEXT("SC_FIRE");
		case PacketId::SC_DAMAGE: return TEXT("SC_DAMAGE");
		case PacketId::SC_DOWNED: return TEXT("SC_DOWNED");
		case PacketId::SC_RESPAWNED: return TEXT("SC_RESPAWNED");
		case PacketId::SC_PROJECTILE: return TEXT("SC_PROJECTILE");
		case PacketId::SC_INTERACT: return TEXT("SC_INTERACT");
		case PacketId::SC_SAFE_OPENED: return TEXT("SC_SAFE_OPENED");
		case PacketId::SC_MONEY: return TEXT("SC_MONEY");
		case PacketId::SC_EXPLOSION: return TEXT("SC_EXPLOSION");
		case PacketId::SC_TREASURE_TAKEN: return TEXT("SC_TREASURE_TAKEN");
		case PacketId::SC_GUARD_ALERT: return TEXT("SC_GUARD_ALERT");
		case PacketId::SC_KNOCKBACK: return TEXT("SC_KNOCKBACK");
		case PacketId::SC_PLAYER_STATUS: return TEXT("SC_PLAYER_STATUS");
	case PacketId::SC_SKILL: return TEXT("SC_SKILL");
	case PacketId::SC_PICKUP: return TEXT("SC_PICKUP");
	case PacketId::CS_PING: return TEXT("CS_PING");
		case PacketId::SC_PONG: return TEXT("SC_PONG");
		case PacketId::SC_KICK: return TEXT("SC_KICK");
		case PacketId::SC_NOTICE: return TEXT("SC_NOTICE");
		default: break;
		}
		return TEXT("(기타)");
	}
}

void FVHPacketAssembler::Append(const uint8* Data, int32 Num)
{
	if (Num > 0)
	{
		Buffer.Append(Data, Num);
	}
}

FVHPacketAssembler::EResult FVHPacketAssembler::Pop(TArray<uint8>& OutPacket)
{
	const int32 Available = Buffer.Num() - ReadPos;
	if (Available < VHPacket::HeaderSize)
	{
		return EResult::NeedMore;
	}

	const uint8* Head = Buffer.GetData() + ReadPos;
	const int32 Size = static_cast<int32>(Head[0]) | (static_cast<int32>(Head[1]) << 8);	// 리틀 엔디언 uint16
	if (Size < VHPacket::MinPacketSize || Size > VHPacket::MaxPacketSize)
	{
		return EResult::Error;
	}
	if (Available < Size)
	{
		return EResult::NeedMore;	// 패킷 일부만 도착 → 나머지를 기다린다
	}

	OutPacket.Reset(Size);
	OutPacket.Append(Head, Size);
	ReadPos += Size;

	// 읽은 앞부분을 가끔 정리해 버퍼가 끝없이 커지지 않게 한다.
	if (ReadPos == Buffer.Num())
	{
		Buffer.Reset();
		ReadPos = 0;
	}
	else if (ReadPos > 16 * 1024)
	{
		Buffer.RemoveAt(0, ReadPos, EAllowShrinking::No);
		ReadPos = 0;
	}
	return EResult::Packet;
}

void FVHPacketAssembler::Reset()
{
	Buffer.Reset();
	ReadPos = 0;
}
