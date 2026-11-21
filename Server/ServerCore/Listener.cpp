#include "Listener.h"
#include "Service.h"
#include "Session.h"

namespace {
// AcceptEx가 즉시 실패하는 경우(예: WSAECONNRESET — 받기 전에 상대가 포기)는 일시적이라 몇 번 재시도한다.
constexpr int kMaxImmediateRetries = 3;
} // namespace

Listener::Listener(std::weak_ptr<ServerService> service)
	: _service(std::move(service))
{
}

Listener::~Listener()
{
	SocketUtils::Close(_listenSocket);
}

bool Listener::StartAccept(uint16 port, uint32 acceptCount)
{
	auto service = _service.lock();
	if (!service)
		return false;

	_listenSocket = SocketUtils::CreateSocket();
	if (_listenSocket == INVALID_SOCKET)
	{
		LOG_ERROR("Listener: 소켓 생성 실패 {}", ::WSAGetLastError());
		return false;
	}

	if (!service->Iocp().Register(_listenSocket))
	{
		LOG_ERROR("Listener: IOCP 등록 실패 {}", ::GetLastError());
		return false;
	}

	if (!SocketUtils::BindAnyAddress(_listenSocket, port))
	{
		LOG_ERROR("Listener: bind({}) 실패 {}", port, ::WSAGetLastError());
		return false;
	}
	if (!SocketUtils::Listen(_listenSocket))
	{
		LOG_ERROR("Listener: listen 실패 {}", ::WSAGetLastError());
		return false;
	}

	_acceptEvents.reserve(acceptCount);
	for (uint32 i = 0; i < acceptCount; ++i)
		_acceptEvents.push_back(std::make_unique<AcceptEvent>());
	for (auto& event : _acceptEvents)
		PostAccept(*event);

	LOG_INFO("Listener: 포트 {}, AcceptEx {}개 대기", port, acceptCount);
	return true;
}

void Listener::Close()
{
	if (_closed.exchange(true))
		return;

	// closesocket 대신 취소: 다른 Worker가 _closed 검사와 AcceptEx 호출 사이에 있을 수 있고,
	// 닫힌 핸들 값은 다른 소켓에 재사용될 수 있다. 핸들은 소멸자에서 닫는다.
	// 취소된 AcceptEx는 ERROR_OPERATION_ABORTED로 완료되고, ProcessAccept가 다시 걸지 않는다.
	::CancelIoEx(reinterpret_cast<HANDLE>(_listenSocket), nullptr);
}

void Listener::Dispatch(IocpEvent* event, DWORD /*bytes*/, DWORD error)
{
	if (event->type != IOType::Accept)
	{
		LOG_ERROR("Listener: 예상 못 한 이벤트 {}", static_cast<int>(event->type));
		return;
	}
	ProcessAccept(*static_cast<AcceptEvent*>(event), error);
}

void Listener::PostAccept(AcceptEvent& event)
{
	//운영체제의 일시적인 변덕 AcceptEx의 즉시 실패로 서버의 입구가 영원히 닫혀버리는 대참사를 막기 위한 방어루프
	for (int attempt = 0; attempt <= kMaxImmediateRetries; ++attempt)
	{
		if (_closed.load())
			return;
		auto service = _service.lock();
		if (!service)
			return;

		// AcceptEx는 받을 소켓을 미리 만들어 넘겨야 한다 → 세션을 먼저 만든다.
		SessionRef session = service->CreateSession();
		if (!session)
			return;

		event.Reset();
		event.session = session;
		event.owner = shared_from_this();
		_pending.fetch_add(1);

		DWORD bytes = 0;
		// 받을 데이터 길이 0: 연결만 되면 바로 완료된다.
		// 0이 아니면 접속만 하고 아무것도 안 보내는 클라이언트가 AcceptEx를 계속 붙잡는다.
		const BOOL ok = SocketUtils::AcceptEx(_listenSocket, session->GetSocket(), event.addressBuffer.data(), 0,
			SocketUtils::AcceptAddressLength, SocketUtils::AcceptAddressLength, &bytes, &event);

		if (ok || ::WSAGetLastError() == WSA_IO_PENDING)
		{
			// Close()의 CancelIoEx가 방금 건 이 AcceptEx보다 먼저 실행됐을 수 있다.
			// 그러면 이 I/O는 누구도 취소하지 않아 영원히 걸려 있으므로 직접 취소한다.
			if (_closed.load())
				::CancelIoEx(reinterpret_cast<HANDLE>(_listenSocket), &event);
			return;
		}

		// 즉시 실패: 완료 통지가 오지 않으므로 여기서 모두 되돌린다(세션은 소멸하며 소켓을 닫음).
		const int err = ::WSAGetLastError();
		event.owner.reset();
		event.session.reset();
		_pending.fetch_sub(1);
		LOG_WARN("Listener: AcceptEx 즉시 실패 {} (시도 {})", err, attempt + 1);
	}

	LOG_ERROR("Listener: AcceptEx 재시도 초과, 대기 슬롯 1개 감소");
}

void Listener::ProcessAccept(AcceptEvent& event, DWORD error)
{
	SessionRef session = std::move(event.session);

	if (error != 0 || !SocketUtils::SetUpdateAcceptContext(session->GetSocket(), _listenSocket))
	{
		if (error != ERROR_OPERATION_ABORTED)
			LOG_WARN("Listener: accept 실패 {}", error != 0 ? error : static_cast<DWORD>(::WSAGetLastError()));
		session.reset();	// 소멸하며 소켓을 닫는다
	}
	else
	{
		SOCKADDR* local = nullptr;
		SOCKADDR* remote = nullptr;
		int localLength = 0;
		int remoteLength = 0;
		SocketUtils::GetAcceptExSockaddrs(event.addressBuffer.data(), 0,
			SocketUtils::AcceptAddressLength, SocketUtils::AcceptAddressLength,
			&local, &localLength, &remote, &remoteLength);

		SOCKADDR_IN remoteAddress{};
		if (remote != nullptr && remoteLength >= static_cast<int>(sizeof(SOCKADDR_IN)))
			std::memcpy(&remoteAddress, remote, sizeof(remoteAddress));

		session->SetRemoteAddress(remoteAddress);
		session->ProcessConnect();
	}

	// 다시 건 뒤에 감소시킨다. 먼저 감소시키면 종료 쪽이 0을 보고
	// Worker를 멈춘 직후에 새 AcceptEx가 걸릴 수 있다.
	PostAccept(event);
	_pending.fetch_sub(1);
}
