#pragma once

#include "IocpCore.h"

#include <unordered_set>

class Session;

// Session 생성과 연결된 Session 목록을 관리한다.
// ServerService = Listener로 받는 쪽, ClientService = ConnectEx로 거는 쪽(DummyClient).
class Service : public std::enable_shared_from_this<Service>
{
public:
	// 어떤 Session 파생 클래스를 만들지 정한다. 예) std::make_shared<GameSession>
	using SessionFactory = std::function<SessionRef()>;

	Service(IocpCore& iocp, const SOCKADDR_IN& address, SessionFactory factory);
	virtual ~Service() = default;

	virtual bool Start() = 0;
	virtual void Stop();		// 새 연결을 더 만들지 않는다

	SessionRef CreateSession();	// 생성 + IOCP 등록 + Service 연결
	void AddSession(SessionRef session);		// 연결 완료 시 Session이 호출
	void ReleaseSession(SessionRef session);	// 연결 종료 시 Session이 호출

	// 목록을 복사한 뒤 락 밖에서 Disconnect (락을 잡은 채 외부 호출 금지)
	void DisconnectAll(const char* reason);

	// 연결된 모든 세션에 같은 버퍼를 보낸다. 직렬화는 호출자가 한 번만 하고, 버퍼는 공유된다.
	// 돌려주는 값: 보낸 세션 수
	uint32 Broadcast(const SendBufferRef& buffer);

	// 연결된 세션 목록을 out에 복사한다(out은 비우고 채움). 감시처럼 모든 세션을 훑을 때,
	// 락은 복사하는 동안만 잡고 검사·Disconnect는 락 밖에서 한다. out을 재사용하면 할당이 매번 생기지 않는다.
	void CopySessions(std::vector<SessionRef>& out);

	uint32 SessionCount();
	IocpCore& Iocp() { return _iocp; }
	const SOCKADDR_IN& Address() const { return _address; }	// 서버: 받을 포트, 클라: 접속 대상

	bool UseSkipCompletionPort() const { return _skipCompletionPort; }
	void SetSkipCompletionPort(bool enable) { _skipCompletionPort = enable; }

protected:
	IocpCore& _iocp;
	SOCKADDR_IN _address{};
	SessionFactory _factory;
	bool _skipCompletionPort = true;	// 설정 파일 skipCompletionPortOnSuccess (12장)
	std::atomic<bool> _stopped{ false };	// Stop 이후 CreateSession은 nullptr

	std::mutex _lock;
	std::unordered_set<SessionRef> _sessions;
};

class ServerService : public Service
{
public:
	ServerService(IocpCore& iocp, uint16 port, uint32 acceptCount, SessionFactory factory);

	bool Start() override;
	void Stop() override;

	uint32 PendingAccepts() const;

private:
	uint32 _acceptCount;
	ListenerRef _listener;
};

class ClientService : public Service
{
public:
	ClientService(IocpCore& iocp, const SOCKADDR_IN& target, uint32 connectCount, SessionFactory factory);

	bool Start() override;	// connectCount개 세션을 만들어 ConnectEx

private:
	uint32 _connectCount;
};
