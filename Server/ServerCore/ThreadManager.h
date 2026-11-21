#pragma once
 
#include "CorePch.h"
 
#include <string_view>

class ThreadManager
{
public:
    ThreadManager() = default;
    ~ThreadManager() { JoinAll(); }
 
    ThreadManager(const ThreadManager&) = delete;
    ThreadManager& operator=(const ThreadManager&) = delete;


    void Launch(std::string name, std::function<void()> fn);

    void JoinAll();
 
    uint32 Count();

    static void InitTLS(std::string_view name);
    static void DestroyTLS();

    private:
    std::mutex _lock;
    std::vector<std::thread> _threads;
 
    static inline std::atomic<uint32> s_nextThreadId{ 1 };
};