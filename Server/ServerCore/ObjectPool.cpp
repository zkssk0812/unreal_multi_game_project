#include "ObjectPool.h"

#include <cstdlib>

namespace StompAllocator {

namespace {
constexpr size_t PageSize = 0x1000;
}

bool Enabled()
{
	// 처음 한 번만 읽고 고정: 도중에 바뀌면 풀에서 받은 블록을 STOMP로 해제하는 사고가 난다.
	static const bool enabled = [] {
		char value[8] = {};
		size_t len = 0;
		return ::getenv_s(&len, value, sizeof(value), "VH_STOMP") == 0 && len > 0 && value[0] == '1';
	}();
	return enabled;
}

void* Alloc(size_t size)
{
	const size_t pageCount = (size + PageSize - 1) / PageSize;
	const size_t totalSize = pageCount * PageSize;
	char* base = static_cast<char*>(::VirtualAlloc(nullptr, totalSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
	if (base == nullptr)
		throw std::bad_alloc();
	const size_t offset = (totalSize - size) & ~static_cast<size_t>(15);	// 16바이트 정렬은 지킨다
	return base + offset;
}

void Free(void* ptr)
{
	// 객체가 들어 있던 페이지 전체를 OS에 돌려준다. 이후 이 주소를 읽거나 쓰면 접근 위반(크래시).
	const auto address = reinterpret_cast<uintptr_t>(ptr);
	void* base = reinterpret_cast<void*>(address & ~(PageSize - 1));
	::VirtualFree(base, 0, MEM_RELEASE);
}

} // namespace StompAllocator
