#pragma once

#include "CoreMinimal.h"
#include "Network/VHProtocol.h"

#include <type_traits>

// 패킷 조립/분해 도우미.
// 패킷 = [size:uint16][type:uint16][body], 리틀 엔디언, #pragma pack(1) 구조체를 그대로 memcpy.
// (클라이언트·서버 모두 x86/x64 리틀 엔디언이라 바이트 순서 변환 없이 구조체를 복사한다.)

namespace VHPacket
{
	constexpr int32 HeaderSize = sizeof(RobProto::PacketHeader);
	constexpr int32 MinPacketSize = HeaderSize;
	constexpr int32 MaxPacketSize = RobProto::MAX_PACKET_SIZE;

	/** 0으로 채운 패킷 구조체를 만들고 size/type을 채운다 */
	template <typename T>
	T Make(RobProto::PacketId Id)
	{
		static_assert(std::is_trivially_copyable_v<T>, "패킷 구조체는 memcpy 가능한 POD여야 한다");
		static_assert(sizeof(T) <= RobProto::MAX_PACKET_SIZE, "패킷이 최대 크기를 넘는다");
		T Packet{};
		Packet.size = static_cast<uint16_t>(sizeof(T));
		Packet.type = Id;
		return Packet;
	}

	/** 패킷 구조체 앞부분 Size 바이트를 바이트 배열로 (가변 길이 패킷은 Size에 *_Size(count)를 넘긴다) */
	template <typename T>
	TArray<uint8> ToBytes(const T& Packet, uint16 Size = static_cast<uint16>(sizeof(T)))
	{
		check(Size >= HeaderSize && Size <= sizeof(T));
		TArray<uint8> Bytes;
		Bytes.Append(reinterpret_cast<const uint8*>(&Packet), Size);
		reinterpret_cast<RobProto::PacketHeader*>(Bytes.GetData())->size = Size;
		return Bytes;
	}

	/** 고정 길이 패킷 읽기: 크기가 sizeof(T)와 정확히 같아야 한다 */
	template <typename T>
	bool ReadFixed(const uint8* Data, int32 Size, T& Out)
	{
		if (Size != static_cast<int32>(sizeof(T)))
		{
			return false;
		}
		FMemory::Memcpy(&Out, Data, sizeof(T));
		return true;
	}

	/**
	 * 가변 길이 패킷 읽기(배열이 마지막 멤버).
	 * 받은 크기가 SizeOfCount(count)와 정확히 같고 count <= MaxCount인지 검사한 뒤,
	 * 나머지는 0으로 채운 전체 구조체에 복사한다.
	 */
	template <typename T, typename CountGetter, typename SizeFunc>
	bool ReadVariable(const uint8* Data, int32 Size, T& Out, int32 MaxCount, CountGetter GetCount, SizeFunc SizeOfCount)
	{
		if (Size < HeaderSize || Size > static_cast<int32>(sizeof(T)))
		{
			return false;
		}
		Out = T{};
		FMemory::Memcpy(&Out, Data, Size);
		const int32 Count = static_cast<int32>(GetCount(Out));
		if (Count > MaxCount)
		{
			return false;
		}
		return Size == static_cast<int32>(SizeOfCount(static_cast<uint8>(Count)));
	}

	/** FString → UTF-8 고정 길이 배열. 널 종료를 지키고, 멀티바이트 문자 중간에서 자르지 않는다 */
	void WriteString(char* Dest, int32 DestSize, const FString& Src);

	/** UTF-8 고정 길이 배열 → FString (널 종료가 없어도 DestSize에서 멈춘다) */
	FString ReadString(const char* Src, int32 SrcSize);

	/** UTF-8로 바꿨을 때의 바이트 수 */
	int32 Utf8Length(const FString& Str);

	/** 디버그 로그용 패킷 이름 */
	const TCHAR* PacketName(uint16 Type);
}

/**
 * TCP 스트림 재조립기.
 * TCP는 "바이트의 흐름"이라 한 번 받은 데이터에 패킷이 반쪽만 있거나 여러 개가 붙어 올 수 있다.
 * 받은 바이트를 계속 쌓아 두고, 헤더의 size만큼 모이면 패킷 하나를 꺼낸다.
 */
class FVHPacketAssembler
{
public:
	enum class EResult : uint8 { NeedMore, Packet, Error };

	void Append(const uint8* Data, int32 Num);

	/** 완성된 패킷 하나를 꺼낸다. size가 4 미만이거나 8192 초과면 Error(연결을 끊어야 함) */
	EResult Pop(TArray<uint8>& OutPacket);

	void Reset();

	int32 BufferedBytes() const { return Buffer.Num() - ReadPos; }

private:
	TArray<uint8> Buffer;
	int32 ReadPos = 0;
};
