#pragma once

#include <new>
#include <type_traits>
 
#include "ServerCore/SendBuffer.h"
#include "protocol.h"


template <class T>
class PacketBuilder
{
    static_assert(std::is_trivially_copyable_v<T>, "패킷 구조체는 POD여야 한다");
    static_assert(sizeof(T) <= MAX_PACKET_SIZE, "패킷이 최대 크기를 넘는다");

    public:

    explicit PacketBuilder(PacketId id)
        : _buffer(SendBufferManager::Open(static_cast<uint32>(sizeof(T))))
    {
        // 덩어리 메모리 위에 구조체를 바로 만든다(placement new). T{}라서 모든 필드가 0으로 시작한다.
        // #pragma pack(1) 구조체라 정렬 요구가 1이므로 덩어리 안 어느 위치에 놓여도 괜찮다.
        _packet = new (_buffer->Buffer()) T{};
        _packet->size = static_cast<uint16_t>(sizeof(T));
        _packet->type = id;
    }

     // Finish를 부르지 않고 사라지면 크기 0으로 닫아서, 이 스레드의 덩어리가 열린 채 남지 않게 한다.
    ~PacketBuilder()
    {
        if (_buffer)
            _buffer->Close(0);
    }
 
    PacketBuilder(const PacketBuilder&) = delete;
    PacketBuilder& operator=(const PacketBuilder&) = delete;

    T* operator->() { return _packet; }
    T& Get() { return *_packet; }

    SendBufferRef Finish(uint16_t size = static_cast<uint16_t>(sizeof(T)))
    {
        _packet->size = size;
        _buffer->Close(size);
        return std::move(_buffer);
    }

    private:
    SendBufferRef _buffer;
    T* _packet = nullptr;
};