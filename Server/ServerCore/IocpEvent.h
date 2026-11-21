#pragma once

#include "CorePch.h"
#include "SocketUtils.h"

enum class IOType : uint8 { Accept, Connect, Recv, Send };

struct IocpEvent : OVERLAPPED
{
	explicit IocpEvent(IOType eventType) : type(eventType) { Reset(); }

	IocpEvent(const IocpEvent&) = delete;
	IocpEvent& operator=(const IocpEvent&) = delete;

	void Reset() { static_cast<OVERLAPPED&>(*this) = OVERLAPPED{}; }

	IOType type;

	IocpObjectRef owner;
};

struct AcceptEvent : IocpEvent
{
	AcceptEvent() : IocpEvent(IOType::Accept) {}

	// AcceptEx에 넘긴 소켓을 가진 세션. 완료되면 Listener가 꺼내서 연결 처리한다.
	SessionRef session;

	// AcceptEx가 로컬·원격 주소를 여기에 쓴다(첫 데이터는 받지 않음).
	std::array<char, SocketUtils::AcceptAddressLength * 2> addressBuffer{};
};

struct SendEvent : IocpEvent
{
	SendEvent() : IocpEvent(IOType::Send) {}

	// WSASend가 끝날 때까지 버퍼를 살려 둔다.
	std::vector<SendBufferRef> buffers;
};
