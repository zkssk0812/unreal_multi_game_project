#include "ThreadManager.h"
#include "CoreTLS.h"
#include "SendBuffer.h"


void ThreadManager::Launch(std::string name, std::function<void()> fn)
{
    std::lock_guard lock(_lock);
    _threads.emplace_back([name = std::move(name), fn = std::move(fn)] {
        InitTLS(name);
        fn();
        DestroyTLS();
    });
}

void ThreadManager::JoinAll()
{
    std::vector<std::thread> threads;
    {
        std::lock_guard lock(_lock);
        threads.swap(_threads);
    }
    for (std::thread& t : threads)
    {
        if (t.joinable())
            t.join();
    }
}

uint32 ThreadManager::Count()
{
    std::lock_guard lock(_lock);
    return static_cast<uint32>(_threads.size());
}

void ThreadManager::InitTLS(std::string_view name)
{
    tls_threadId = s_nextThreadId.fetch_add(1);
 
    const size_t len = name.size() < ThreadNameSize - 1 ? name.size() : ThreadNameSize - 1;
    std::memcpy(tls_threadName, name.data(), len);
    tls_threadName[len] = '\0';

    wchar_t wide[ThreadNameSize] = {};
    ::MultiByteToWideChar(CP_UTF8, 0, tls_threadName, -1, wide, ThreadNameSize);
    ::SetThreadDescription(::GetCurrentThread(), wide);
}

void ThreadManager::DestroyTLS()
{
    SendBufferManager::ReleaseThreadChunk();
}