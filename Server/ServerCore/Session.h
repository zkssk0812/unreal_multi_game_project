#pragma once

#include "IocpCore.h"
#include "IocpEvent.h"
#include "SendBuffer.h"
#include "RecvBuffer.h"

class Service;

class Session : public IocpObject
 {
    friend class Listener;
    friend class Service;

public:
    Session();
    ~Session() override;

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    void Send(SendBufferRef buffer);
    void Disconnect(const char* reason);

    // 마지막 패킷(SC_KICK, 버전 불일치 응답 등)이 상대에게 간 뒤에 끊는다(B2).
    // Disconnect는 걸린 WSASend까지 취소하므로 "알려 주고 끊기"에 쓸 수 없다.
    // 부른 뒤로는 Send를 받지 않고, 받은 데이터는 해석하지 않고 버린다.
    // reason은 문자열 상수여야 한다(나중에 OnDisconnected로 전달될 때까지 보관).
    void SendAndDisconnect(SendBufferRef last, const char* reason);

    bool Connect();

    bool IsConnected() const {return _connected.load();};
    bool IsClosing() const { return _closing.load(); }    // SendAndDisconnect 이후
    uint64 ClosingSinceMs() const { return _closingSinceMs.load(); }    // 끊기 시작한 시각(NowMs), 0 = 아님
    SOCKET GetSocket() const {return _socket;};
    const SOCKADDR_IN& RemoteAddress() const { return _remoteAddress; }
    uint64 SessionId() const { return _sessionId; }
    std::shared_ptr<Service> GetService() const { return _service.lock(); }

    static uint32 LiveCount() { return s_liveCount.load(); }

    HANDLE GetHandle() override { return reinterpret_cast<HANDLE>(_socket); }
    void Dispatch(IocpEvent* event, DWORD bytes, DWORD error) override;

protected:
    // 콘텐츠(상속 클래스)가 구현하는 부분
    virtual void OnConnected() {}
    virtual int32 OnRecv(char* /*data*/, int32 len) { return len; }
    virtual void OnSend(int32 /*len*/) {}
    virtual void OnDisconnected(const char* /*reason*/) {}

private:
    void SetService(const std::shared_ptr<Service>& service) { _service = service; }
    void SetRemoteAddress(const SOCKADDR_IN& address) { _remoteAddress = address; }
    SessionRef SharedThis() { return std::static_pointer_cast<Session>(shared_from_this()); }

    void ProcessConnect();
    void ProcessConnectEx(DWORD error);


    void RegisterRecv();
    void ProcessRecv(DWORD bytes, DWORD error);
    bool HandleRecvBytes(DWORD bytes);    // false = 연결이 끊김
 

    void RegisterSend();
    void ProcessSend(DWORD bytes, DWORD error);
    bool HandleSendBytes(DWORD bytes);    // false = 연결이 끊김
    void ShutdownSend();                  // 끊는 중이고 송신 큐가 비었다: FIN을 보낸다

    void HandleError(DWORD error, const char* where);

private:
	static constexpr uint32 RecvBufferSize = 0x10000;	// 64KB. C3에서 RecvBuffer로 분리
    static constexpr uint32 RecvCompactThreshold = 0x4000;
	static inline std::atomic<uint32> s_liveCount{ 0 };
	static inline std::atomic<uint64> s_nextSessionId{ 1 };

	SOCKET _socket = INVALID_SOCKET;
	const uint64 _sessionId;
	SOCKADDR_IN _remoteAddress{};
	std::weak_ptr<Service> _service;
	std::atomic<bool> _connected{ false };
	std::atomic<bool> _closing{ false };	// SendAndDisconnect 이후 (_sendLock 안에서 켬)
	const char* _closeReason = "";			// _closing을 켜기 전에 기록
	std::atomic<uint64> _closingSinceMs{ 0 };	// 끊기 시작한 시각 (지금은 쓰는 곳 없음, 진단용)
	bool _skipCompletionPort = false;

	IocpEvent _connectEvent{ IOType::Connect };
	IocpEvent _recvEvent{ IOType::Recv };

    RecvBuffer _recvBuffer{ RecvBufferSize, RecvCompactThreshold };
	
	std::mutex _sendLock;
	std::vector<SendBufferRef> _sendQueue;
	bool _sendRegistered = false;
	SendEvent _sendEvent;
	std::vector<WSABUF> _wsaBufs;
 };