#pragma once

#include <chrono>
#include <cstdint>
 

namespace ServerCore {

// 서버 내부 시각(ms). 부팅 후 경과 시간이라 시스템 시간을 바꿔도 뒤로 가지 않는다.
// JobQueue 시간 예산, JobTimer 예약처럼 "지금부터 몇 ms"를 재는 데 쓴다.
inline uint64 NowMs()
{
	using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

}
