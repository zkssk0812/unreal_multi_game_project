#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>
 
#include "Protocol/protocol.h"		// 저장소 루트 Common/ (클라와 공유)

namespace Protocol {


template <class T>
bool ReadPacket(const char* packet, uint16_t size, T& out)
{
    static_assert(std::is_trivially_copyable_v<T>, "패킷 구조체는 memcpy 가능한 POD여야 한다");
    if (size != sizeof(T))
        return false;
    std::memcpy(&out, packet, sizeof(T));
    return true;
}


template <class T>
T MakePacket(PacketId id)
{
    static_assert(sizeof(T) <= MAX_PACKET_SIZE, "패킷이 최대 크기를 넘는다");
    T packet{};
    packet.size = static_cast<uint16_t>(sizeof(T));
    packet.type = id;
    return packet;
}


};