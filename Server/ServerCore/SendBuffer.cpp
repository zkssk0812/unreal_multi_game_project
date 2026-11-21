#include "SendBuffer.h"

#include <cassert>

#include "ObjectPool.h"

SendBuffer::SendBuffer(SendBufferChunkRef owner, char* buffer, uint32 allocSize)
	: _owner(std::move(owner)), _buffer(buffer), _allocSize(allocSize)
{
}

void SendBuffer::Close(uint32 writeSize)
{
	assert(writeSize <= _allocSize);
	_writeSize = writeSize <= _allocSize ? writeSize : _allocSize;
	_owner->Close(_writeSize);
}

// ───────────────────────── SendBufferChunk ─────────────────────────

void SendBufferChunk::Reset()
{
	_open = false;
	_usedSize = 0;
}

SendBufferRef SendBufferChunk::Open(uint32 allocSize)
{
	assert(!_open && allocSize <= FreeSize());
	_open = true;
	// 바이트는 덩어리 안에서, SendBuffer 객체(+참조 카운트 블록)는 풀에서(범용 ObjectPool로 옮김).
	return ObjectPool<SendBuffer>::MakeShared(shared_from_this(), _buffer.data() + _usedSize, allocSize);
}

void SendBufferChunk::Close(uint32 writeSize)
{
	assert(_open);
	_open = false;
	_usedSize += writeSize;		// Open 때 요청한 크기가 아니라 실제로 쓴 만큼만 전진
}

// ───────────────────────── SendBufferManager ─────────────────────────

namespace {
// 스레드마다 지금 잘라 쓰고 있는 덩어리. 스레드가 끝나면 shared_ptr가 풀리며 풀로 돌아간다.
thread_local SendBufferChunkRef tls_sendChunk;
} // namespace

SendBufferManager& SendBufferManager::Instance()
{
	static SendBufferManager instance;
	return instance;
}

SendBufferManager::~SendBufferManager()
{
	for (SendBufferChunk* chunk : _pool)
		delete chunk;
}

SendBufferRef SendBufferManager::Open(uint32 allocSize)
{
	if (allocSize == 0 || allocSize > SendBufferChunk::ChunkSize)
	{
		LOG_ERROR("SendBufferManager: 잘못된 크기 {}", allocSize);
		return nullptr;
	}

	if (!tls_sendChunk)
		tls_sendChunk = Instance().Pop();

	assert(!tls_sendChunk->IsOpen() && "SendBuffer를 Close하기 전에 다시 Open했다");

	// 남은 공간이 모자라면 새 덩어리로 갈아탄다. 이전 덩어리는 그 안의 SendBuffer들이
	// 아직 전송 중이면 그것들이 붙잡고 있다가, 모두 해제되면 풀로 돌아간다.
	if (tls_sendChunk->FreeSize() < allocSize)
		tls_sendChunk = Instance().Pop();

	return tls_sendChunk->Open(allocSize);
}

void SendBufferManager::ReleaseThreadChunk()
{
	assert(!tls_sendChunk || !tls_sendChunk->IsOpen());
	tls_sendChunk.reset();
}

SendBufferChunkRef SendBufferManager::Pop()
{
	SendBufferChunk* chunk = nullptr;
	{
		std::lock_guard lock(_lock);
		if (!_pool.empty())
		{
			chunk = _pool.back();
			_pool.pop_back();
		}
	}
	if (chunk == nullptr)
	{
		chunk = new SendBufferChunk();
		_chunksCreated.fetch_add(1, std::memory_order_relaxed);
	}
	chunk->Reset();

	return SendBufferChunkRef(chunk, [](SendBufferChunk* c) { Instance().Push(c); });
}

void SendBufferManager::Push(SendBufferChunk* chunk)
{
	std::lock_guard lock(_lock);
	_pool.push_back(chunk);
}

SendBufferManager::Stats SendBufferManager::GetStats()
{
	SendBufferManager& self = Instance();
	std::lock_guard lock(self._lock);
	return { self._chunksCreated.load(), static_cast<uint64>(self._pool.size()) };
}
