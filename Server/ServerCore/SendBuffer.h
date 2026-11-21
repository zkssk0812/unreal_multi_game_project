#pragma once

#include "CorePch.h"

//Open과 Close는 같은 스레드에서, 그리고 Close 전에 다시 Open하면 안 됩니다. 덩어리가 스레드 전용이라 락이 없는 대신 생긴 약속

class SendBufferChunk;
using SendBufferChunkRef = std::shared_ptr<SendBufferChunk>;


class SendBuffer
{
public:
	SendBuffer(SendBufferChunkRef owner, char* buffer, uint32 allocSize);

    char* Buffer() { return _buffer; }
    const char* Buffer() const { return _buffer; }
    uint32 Capacity() const { return _allocSize; }
    uint32 WriteSize() const { return _writeSize; }

	void Close(uint32 writeSize);	// 쓴 크기를 확정하고 덩어리의 사용량에 반영



private:
	SendBufferChunkRef _owner;// 이 조각이 살아 있는 동안 덩어리를 붙잡는다
	char* _buffer;//_buffer는 덩어리 안의 위치를 가리키는 포인터
	uint32 _allocSize;
    uint32 _writeSize = 0;
};


class SendBufferChunk : public std::enable_shared_from_this<SendBufferChunk>
//enable_shared_from_this: Open이 자기 자신의 shared_ptr를 SendBuffer에 넘겨야 하기 때문.
{
public:
    static constexpr uint32 ChunkSize = 0x10000;    // 64KB
 
    void Reset();
    SendBufferRef Open(uint32 allocSize);
    void Close(uint32 writeSize);
 
    bool IsOpen() const { return _open; }
    uint32 FreeSize() const { return ChunkSize - _usedSize; }
 
private:
    std::array<char, ChunkSize> _buffer{};
    bool _open = false;
    uint32 _usedSize = 0;
};

class SendBufferManager//스레드 주머니(TLS, Thread Local Storage)에 64KB짜리 덩어리 넣어주는 역할
{
public:
    static SendBufferRef Open(uint32 allocSize);

	static void ReleaseThreadChunk();
	
    struct Stats { uint64 chunksCreated; uint64 chunksInPool; };
    static Stats GetStats();
 
    ~SendBufferManager();
	
private:
    static SendBufferManager& Instance();
 
    SendBufferChunkRef Pop();
    void Push(SendBufferChunk* chunk);
 
    std::mutex _lock;
    std::vector<SendBufferChunk*> _pool;
    std::atomic<uint64> _chunksCreated{ 0 };


};

inline SendBufferRef MakeSendBuffer(const void* data, uint32 len)
{
    SendBufferRef buffer = SendBufferManager::Open(len);
    if (!buffer)
        return nullptr;
    std::memcpy(buffer->Buffer(), data, len);
    buffer->Close(len);
    return buffer;
}

