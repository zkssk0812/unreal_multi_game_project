#pragma once
 
#include "CorePch.h"

//   [ 처리 끝난 영역 | 아직 처리 안 한 데이터 | 비어 있는 영역 ]
//   0            readPos                 writePos           capacity

class RecvBuffer
{
public:
    RecvBuffer(uint32 capacity, uint32 compactThreshold);
    void Clean();
    bool OnRead(uint32 numOfBytes);
    bool OnWrite(uint32 numOfBytes);

    char* ReadPos() { return _buffer + _readPos; }
    char* WritePos() { return _buffer + _writePos; }
    uint32 DataSize() const { return _writePos - _readPos; }
    uint32 FreeSize() const { return _capacity - _writePos; }
    uint32 Capacity() const { return _capacity; }

    uint64 CompactCount() const { return _compactCount; }

    ~RecvBuffer();
	RecvBuffer(const RecvBuffer&) = delete;
	RecvBuffer& operator=(const RecvBuffer&) = delete;

    static constexpr uint32 PooledCapacity = 0x10000;

private:
    uint32 _capacity;
    uint32 _compactThreshold;
    uint32 _readPos = 0;
    uint32 _writePos = 0;
    uint64 _compactCount = 0;

    char* _buffer = nullptr;
};