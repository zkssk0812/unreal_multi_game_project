#pragma once

#include "Session.h"

class PacketSession : public Session
{
    
public:
    static constexpr uint16 HeaderSize = 4;
    static constexpr uint16 DefaultMaxPacketSize = 8192;

    explicit PacketSession(uint16 maxPacketSize = DefaultMaxPacketSize) : _maxPacketSize(maxPacketSize) {}
    
protected:
    virtual void OnRecvPacket(const char* packet, uint16 size, uint16 id) = 0;

    private:
    int32 OnRecv(char* data, int32 len) final;
 
    uint16 _maxPacketSize;


};

