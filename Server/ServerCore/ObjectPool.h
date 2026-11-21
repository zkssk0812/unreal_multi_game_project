#pragma once

#include "CorePch.h"


namespace StompAllocator {
void* Alloc(size_t size);
void Free(void* ptr);
bool Enabled();
}

namespace PoolDetail {

struct FreeNode { FreeNode* next; };

struct PoolStats
{
	std::atomic<uint64> heapAllocs{ 0 };	// 목록이 비어 힙에서 새로 받은 횟수
	std::atomic<uint64> heapFrees{ 0 };		// 목록이 꽉 차 힙에 돌려준 횟수
};

template <size_t BlockSize>
struct TlsFreeList
{
	static constexpr size_t kMaxCached = BlockSize >= 0x4000 ? 64 : 4096;	// 큰 블록은 적게 쌓는다

	FreeNode* head = nullptr;
	size_t count = 0;

	~TlsFreeList()
	{
		while (head != nullptr)
		{
			FreeNode* node = head;
			head = node->next;
			::operator delete(node);
		}
	}

	static TlsFreeList& Mine()
	{
		thread_local TlsFreeList list;
		return list;
	}
};

template <size_t BlockSize>
inline PoolStats& StatsFor()
{
	static PoolStats stats;
	return stats;
}

template <size_t BlockSize>
void* AllocBlock()
{
	if (StompAllocator::Enabled())
		return StompAllocator::Alloc(BlockSize);

	auto& list = TlsFreeList<BlockSize>::Mine();
	if (list.head != nullptr)
	{
		FreeNode* node = list.head;
		list.head = node->next;
		--list.count;
		return node;
	}
	StatsFor<BlockSize>().heapAllocs.fetch_add(1, std::memory_order_relaxed);
	return ::operator new(BlockSize);
}

template <size_t BlockSize>
void FreeBlock(void* ptr) noexcept
{
	if (StompAllocator::Enabled())
	{
		StompAllocator::Free(ptr);
		return;
	}

	auto& list = TlsFreeList<BlockSize>::Mine();
	if (list.count < TlsFreeList<BlockSize>::kMaxCached)
	{
		auto* node = static_cast<FreeNode*>(ptr);
		node->next = list.head;
		list.head = node;
		++list.count;
		return;
	}
	StatsFor<BlockSize>().heapFrees.fetch_add(1, std::memory_order_relaxed);
	::operator delete(ptr);
}

} // namespace PoolDetail

// 크기만 정해진 원시 블록 풀 (예: 세션 수신 버퍼 64KB)
template <size_t BlockSize>
class BlockPool
{
	static_assert(BlockSize >= sizeof(PoolDetail::FreeNode));

public:
	static void* Alloc() { return PoolDetail::AllocBlock<BlockSize>(); }
	static void Free(void* ptr) noexcept { PoolDetail::FreeBlock<BlockSize>(ptr); }
	static PoolDetail::PoolStats& Stats() { return PoolDetail::StatsFor<BlockSize>(); }
};

// 표준 할당기 요건을 맞춘 풀 할당기. allocate_shared가 "객체 + 참조 카운트 블록"을 한 번에 이것으로 받는다.
template <class T>
struct PoolAllocator
{
	using value_type = T;
	static constexpr size_t kBlockSize = sizeof(T) > sizeof(PoolDetail::FreeNode) ? sizeof(T) : sizeof(PoolDetail::FreeNode);

	PoolAllocator() = default;
	template <class U> PoolAllocator(const PoolAllocator<U>&) noexcept {}

	T* allocate(size_t n)
	{
		if (n == 1)
			return static_cast<T*>(PoolDetail::AllocBlock<kBlockSize>());
		return static_cast<T*>(::operator new(n * sizeof(T)));
	}

	void deallocate(T* p, size_t n) noexcept
	{
		if (n == 1)
			PoolDetail::FreeBlock<kBlockSize>(p);
		else
			::operator delete(p);
	}

	template <class U> bool operator==(const PoolAllocator<U>&) const noexcept { return true; }
};

template <class T>
class ObjectPool
{
public:
	// make_shared와 같은 쓰임. 객체와 참조 카운트 블록이 풀의 블록 하나에 함께 들어간다.
	template <class... Args>
	static std::shared_ptr<T> MakeShared(Args&&... args)
	{
		return std::allocate_shared<T>(PoolAllocator<T>{}, std::forward<Args>(args)...);
	}
};
