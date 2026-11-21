#pragma once
#include <cstdint>
#include <memory>


using int8 = std::int8_t;
using int16 = std::int16_t;
using int32 = std::int32_t;
using int64 = std::int64_t;
using uint8 = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;

//Ref 참조 카운팅(Reference Counting)으로 관리되는 객체
using IocpObjectRef = std::shared_ptr<class IocpObject>;
using ListenerRef = std::shared_ptr<class Listener>;
using SessionRef = std::shared_ptr<class Session>;
using ServiceRef = std::shared_ptr<class Service>;
using ServerServiceRef = std::shared_ptr<class ServerService>;
using ClientServiceRef = std::shared_ptr<class ClientService>;
using SendBufferRef = std::shared_ptr<class SendBuffer>;
