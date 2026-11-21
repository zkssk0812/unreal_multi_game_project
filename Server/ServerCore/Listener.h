#pragma once

#include "IocpCore.h"

struct AcceptEvent;
class ServerService;

class Listener : public IocpObject
{
public:
	explicit Listener(std::weak_ptr<ServerService> service);
	~Listener() override;

	bool StartAccept(uint16 port, uint32 acceptCount);

	void Close();
	uint32 PendingAccepts() const { return _pending.load(); }

	// IocpObject
	HANDLE GetHandle() override { return reinterpret_cast<HANDLE>(_listenSocket); }
	void Dispatch(IocpEvent* event, DWORD bytes, DWORD error) override;

private:
	void PostAccept(AcceptEvent& event);
	void ProcessAccept(AcceptEvent& event, DWORD error);

	std::weak_ptr<ServerService> _service;
	SOCKET _listenSocket = INVALID_SOCKET;

	std::vector<std::unique_ptr<AcceptEvent>> _acceptEvents;

	std::atomic<bool> _closed{ false };
	std::atomic<uint32> _pending{ 0 };	// 커널에 걸려 있거나 처리 중인 AcceptEx 수
};
