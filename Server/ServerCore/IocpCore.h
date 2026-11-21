#pragma once

#include "CorePch.h"

struct IocpEvent;

// 완료 포트에 등록되는 모든 것(Listener, Session)의 공통 인터페이스.
class IocpObject : public std::enable_shared_from_this<IocpObject>
{
public:
	virtual ~IocpObject() = default;

	virtual HANDLE GetHandle() = 0;

	virtual void Dispatch(IocpEvent* event, DWORD bytes, DWORD error) = 0;
};

class IocpCore
{
public:
	IocpCore();
	~IocpCore();

	IocpCore(const IocpCore&) = delete;
	IocpCore& operator=(const IocpCore&) = delete;

	bool IsValid() const { return _iocp != nullptr; }

	bool Register(HANDLE handle);
	bool Register(SOCKET socket) { return Register(reinterpret_cast<HANDLE>(socket)); }

	// 완료를 최대 Batch개씩 꺼내 처리한다.
	// 종료 신호를 꺼냈으면 false, 그 외(타임아웃 포함)는 true.
	bool Dispatch(uint32 timeoutMs = INFINITE);

	// Worker마다 종료 신호 1개씩
	void PostShutdown(uint32 workerCount);

private:
	HANDLE _iocp = nullptr;

	static constexpr ULONG_PTR kShutdownKey = 0xDEAD;
	static constexpr ULONG kBatch = 64;
};
