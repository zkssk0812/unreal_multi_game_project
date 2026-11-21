#include "RecvBuffer.h"
#include "ObjectPool.h"

RecvBuffer::RecvBuffer(uint32 capacity, uint32 compactThreshold)
	: _capacity(capacity), _compactThreshold(compactThreshold < capacity ? compactThreshold : capacity)
{
	_buffer = capacity == PooledCapacity ? static_cast<char*>(BlockPool<PooledCapacity>::Alloc()) : new char[capacity];
}

RecvBuffer::~RecvBuffer()
{
	if (_capacity == PooledCapacity)
		BlockPool<PooledCapacity>::Free(_buffer);
	else
		delete[] _buffer;
}

void RecvBuffer::Clean()
{
    const uint32 dataSize = DataSize();
    if (dataSize == 0)
    {
        // 가장 흔한 경우: 받은 것을 전부 처리했다. 복사 없이 커서만 되돌린다.
        _readPos = 0;
        _writePos = 0;
        return;
    }

    if (FreeSize() < _compactThreshold)
	{
		std::memmove(_buffer, _buffer + _readPos, dataSize);
		_readPos = 0;
		_writePos = dataSize;
		++_compactCount;
	}
}

bool RecvBuffer::OnRead(uint32 numOfBytes)
{
    if (numOfBytes > DataSize())
        return false;
    _readPos += numOfBytes;
    return true;
}

bool RecvBuffer::OnWrite(uint32 numOfBytes)
{
    if (numOfBytes > FreeSize())
        return false;
    _writePos += numOfBytes;
    return true;
}