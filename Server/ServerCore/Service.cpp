#include "Service.h"
#include "Session.h"
#include "Listener.h"

// ───────────────────────── Service ─────────────────────────

Service::Service(IocpCore& iocp, const SOCKADDR_IN& address, SessionFactory factory)
	: _iocp(iocp), _address(address), _factory(std::move(factory))
{
}

void Service::Stop()
{
	_stopped.store(true);
}

SessionRef Service::CreateSession()
{
	if (_stopped.load() || !_factory)
		return nullptr;

	SessionRef session = _factory();
	if (!session || session->GetSocket() == INVALID_SOCKET)
	{
		LOG_ERROR("Service: 세션 소켓 생성 실패 {}", ::WSAGetLastError());
		return nullptr;
	}

	// AcceptEx/ConnectEx를 걸기 전에 포트에 묶어야 완료 통지를 받는다.
	if (!_iocp.Register(session->GetSocket()))
	{
		LOG_ERROR("Service: 세션 IOCP 등록 실패 {}", ::GetLastError());
		return nullptr;
	}

	session->SetService(shared_from_this());
	return session;
}

void Service::AddSession(SessionRef session)
{
	std::lock_guard lock(_lock);
	_sessions.insert(std::move(session));
}

void Service::ReleaseSession(SessionRef session)
{
	std::lock_guard lock(_lock);
	_sessions.erase(session);
}

void Service::DisconnectAll(const char* reason)
{
	// Disconnect가 ReleaseSession으로 다시 _lock을 잡으므로 복사본을 락 밖에서 돈다.
	std::vector<SessionRef> sessions;
	{
		std::lock_guard lock(_lock);
		sessions.assign(_sessions.begin(), _sessions.end());
	}
	for (const SessionRef& session : sessions)
		session->Disconnect(reason);
}

uint32 Service::Broadcast(const SendBufferRef& buffer)
{
	std::vector<SessionRef> sessions;
	{
		std::lock_guard lock(_lock);
		sessions.assign(_sessions.begin(), _sessions.end());
	}
	// Send는 세션의 송신 락을 잡고 WSASend까지 할 수 있으므로 목록 락 밖에서 부른다.
	// 각 세션의 송신 큐에는 같은 SendBufferRef가 들어간다(참조 수만 늘어남, 바이트 복사 없음).
	for (auto& session : sessions)
		session->Send(buffer);
	return static_cast<uint32>(sessions.size());
}

void Service::CopySessions(std::vector<SessionRef>& out)
{
	out.clear();
	std::lock_guard lock(_lock);
	out.assign(_sessions.begin(), _sessions.end());
}

uint32 Service::SessionCount()
{
	std::lock_guard lock(_lock);
	return static_cast<uint32>(_sessions.size());
}

// ───────────────────────── ServerService ─────────────────────────

namespace {

SOCKADDR_IN MakeAnyAddress(uint16 port)
{
	SOCKADDR_IN address{};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = ::htonl(INADDR_ANY);
	address.sin_port = ::htons(port);
	return address;
}

} // namespace

ServerService::ServerService(IocpCore& iocp, uint16 port, uint32 acceptCount, SessionFactory factory)
	: Service(iocp, MakeAnyAddress(port), std::move(factory)), _acceptCount(acceptCount)
{
}

bool ServerService::Start()
{
	if (_listener)
		return false;

	auto self = std::static_pointer_cast<ServerService>(shared_from_this());
	_listener = std::make_shared<Listener>(self);
	if (!_listener->StartAccept(::ntohs(_address.sin_port), _acceptCount))
	{
		_listener->Close();
		_listener.reset();
		return false;
	}
	return true;
}

void ServerService::Stop()
{
	Service::Stop();
	// Listener는 걸린 AcceptEx가 모두 취소되어 owner가 풀릴 때 소멸한다.
	// PendingAccepts로 그 수를 볼 수 있도록 참조는 남겨 둔다.
	if (_listener)
		_listener->Close();
}

uint32 ServerService::PendingAccepts() const
{
	return _listener ? _listener->PendingAccepts() : 0;
}

// ───────────────────────── ClientService ─────────────────────────

ClientService::ClientService(IocpCore& iocp, const SOCKADDR_IN& target, uint32 connectCount, SessionFactory factory)
	: Service(iocp, target, std::move(factory)), _connectCount(connectCount)
{
}

bool ClientService::Start()
{
	for (uint32 i = 0; i < _connectCount; ++i)
	{
		SessionRef session = CreateSession();
		if (!session)
			return false;
		// 실패하면 session이 범위를 벗어나며 소멸하고 소켓이 닫힌다.
		if (!session->Connect())
			return false;
	}
	LOG_INFO("ClientService: {} 접속, 연결 {}개 시도", SocketUtils::ToString(_address), _connectCount);
	return true;
}
