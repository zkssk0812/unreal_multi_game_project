#pragma once

//Core 기반 인프라
// windows.h의 min/max 매크로가 std::max, numeric_limits::max()를 깨뜨리지 않도록 막는다.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <mswsock.h>
#include <ws2tcpip.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "mswsock.lib")

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Type.h"
#include "Logger.h"

