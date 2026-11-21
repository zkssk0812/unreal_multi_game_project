#include "PacketSession.h"
#include "NetStats.h"

int32 PacketSession::OnRecv(char* data, int32 len)
{
    int32 processed = 0;
 
    while (true)
    {
        const int32 remain = len - processed;

        if (remain < HeaderSize)
        {
            if (remain > 0)
                NetStats::Add(NetStats::PartialHeader);
            break;
        }

        uint16 size = 0;
        uint16 id = 0;
        std::memcpy(&size, data + processed, sizeof(size));
        std::memcpy(&id, data + processed + sizeof(size), sizeof(id));

        if (size < HeaderSize || size > _maxPacketSize)
        {
            LOG_WARN("Session {}: 잘못된 패킷 크기 {} (id {})", SessionId(), size, id);
            Disconnect("invalid packet size");
            return -1;
        }

        if (remain < size)
        {
            NetStats::Add(NetStats::PartialBody);
            break;
        }

        OnRecvPacket(data + processed, size, id);
        processed += size;
        NetStats::Add(NetStats::Packets);

        // 핸들러가 연결을 끊었거나 끊기 시작했으면(SC_KICK) 뒤에 남은 패킷은 처리하지 않는다.
        if (!IsConnected() || IsClosing())
            break;
    }

    return processed;
    
}