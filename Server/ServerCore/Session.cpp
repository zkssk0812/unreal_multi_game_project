#include "Session.h"
#include "Service.h"
#include "NetStats.h"
#include "CoreClock.h"

Session::Session()
    : _sessionId(s_nextSessionId.fetch_add(1))
{
    _socket = SocketUtils::CreateSocket();
    s_liveCount.fetch_add(1);
}

Session::~Session()
{
    SocketUtils::Close(_socket);
    s_liveCount.fetch_sub(1);
}


void Session::Dispatch(IocpEvent* event, DWORD bytes, DWORD error)
{
    //bytes : 네트워크 카드를 통해 성공적으로 전송한 데이터의 총 크기
    switch (event->type)
    {
    case IOType::Connect: ProcessConnectEx(error); break;
    case IOType::Recv:    ProcessRecv(bytes, error); break;
    case IOType::Send:    ProcessSend(bytes, error); break;
    default:
        LOG_ERROR("Session {}: 예상 못 한 이벤트 {}", _sessionId, static_cast<int>(event->type));
        break;
    }
}

bool Session::Connect()
{
    auto service = GetService();
    if (!service || IsConnected())
        return false;
 
    // ConnectEx는 bind된 소켓이 필요하다(포트 0 = 임의 포트).
    if (!SocketUtils::BindAnyAddress(_socket, 0))
    {
        LOG_ERROR("Session {}: bind 실패 {}", _sessionId, ::WSAGetLastError());
        return false;
    }
 
    const SOCKADDR_IN& target = service->Address();
    _connectEvent.Reset();
    _connectEvent.owner = shared_from_this();
 
    DWORD bytes = 0;
    if (!SocketUtils::ConnectEx(_socket, reinterpret_cast<const SOCKADDR*>(&target), sizeof(target),
        nullptr, 0, &bytes, &_connectEvent))
    {
        const int err = ::WSAGetLastError();
        if (err != WSA_IO_PENDING)
        {
            _connectEvent.owner.reset();
            LOG_ERROR("Session {}: ConnectEx 실패 {}", _sessionId, err);
            return false;
        }
    }
    // 연결 전이라 skip 모드가 꺼져 있으므로, 즉시 성공해도 완료는 포트로 온다.
    return true;
}

void Session::ProcessConnectEx(DWORD error)
{
    if (error != 0)
    {
        LOG_WARN("Session {}: 연결 실패 {}", _sessionId, error);
        return;    // owner가 풀리며 세션 소멸
    }
    // ConnectEx로 연결한 소켓도 이 옵션 전까지는 getpeername, shutdown 등이 안 된다.
    ::setsockopt(_socket, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, nullptr, 0);
    if (auto service = GetService())
        SetRemoteAddress(service->Address());
    ProcessConnect();
}

void Session::ProcessConnect()
{
    auto service = GetService();
    if (!service)
        return;
 
    // 프로토콜 명세 1장: TCP_NODELAY 켬. 작은 패킷(이동 20Hz)을 Nagle로 묶어 지연시키지 않는다.
    SocketUtils::SetTcpNoDelay(_socket, true);

     if (service->UseSkipCompletionPort() && SocketUtils::CanSkipCompletionPort)
    {
        _skipCompletionPort = ::SetFileCompletionNotificationModes(GetHandle(),
            FILE_SKIP_COMPLETION_PORT_ON_SUCCESS | FILE_SKIP_SET_EVENT_ON_HANDLE) != FALSE;
    }

    _connected.store(true);
    NetStats::Add(NetStats::Connects);
    service->AddSession(SharedThis());
 
    OnConnected();
    RegisterRecv();

}

void Session::Disconnect(const char* reason)
{
    if (!_connected.exchange(false))
        return;
    ::CancelIoEx(GetHandle(), nullptr);
    NetStats::Add(NetStats::Disconnects);
 
    OnDisconnected(reason);
 
    if (auto service = GetService())
        service->ReleaseSession(SharedThis());
}

void Session::HandleError(DWORD error, const char* where)
{
    switch (error)
    {
    case ERROR_OPERATION_ABORTED:    // 우리가 Disconnect에서 취소한 것
        break;
    case WSAECONNRESET:
    case WSAECONNABORTED:
    case ERROR_NETNAME_DELETED:        // 상대가 RST로 끊음
        Disconnect("peer reset");
        break;
    default:
        LOG_WARN("Session {}: {} 오류 {}", _sessionId, where, error);
        Disconnect(where);
        break;
    }
}

// ───────────────────────── 수신 ─────────────────────────

void Session::RegisterRecv()
{
    // skip 모드에서 즉시 성공하면 여기서 바로 처리하고 다음 WSARecv를 건다.
    // 재귀 대신 반복문: 데이터가 계속 들어오는 동안 스택이 깊어지지 않게.
    while (true)
    {
        if (!IsConnected())
            return;
 
        _recvEvent.Reset();
        _recvEvent.owner = shared_from_this();
 
        WSABUF buf;
        buf.buf = _recvBuffer.WritePos();
        buf.len = static_cast<ULONG>(_recvBuffer.FreeSize());
        DWORD bytes = 0;
        DWORD flags = 0;
        if (::WSARecv(_socket, &buf, 1, &bytes, &flags, &_recvEvent, nullptr) == SOCKET_ERROR)
        {
            const int err = ::WSAGetLastError();
            if (err == WSA_IO_PENDING)
            {
                // Disconnect의 CancelIoEx가 이 WSARecv보다 먼저 실행됐을 수 있다. 그러면 직접 취소.
                if (!IsConnected())
                    ::CancelIoEx(GetHandle(), &_recvEvent);
                return;
            }
            _recvEvent.owner.reset();
            HandleError(static_cast<DWORD>(err), "WSARecv");
            return;

            
        }
        if (!_skipCompletionPort)
            return;    // skip 모드가 아니면 완료가 포트로 온다
 
        IocpObjectRef self = std::move(_recvEvent.owner);    // 처리 동안 수명 유지
        if (!HandleRecvBytes(bytes))
            return;
    }
}

void Session::ProcessRecv(DWORD bytes, DWORD error)
{
    if (error != 0)
    {
        HandleError(error, "Recv");
        return;
    }
    if (HandleRecvBytes(bytes))
        RegisterRecv();
}

bool Session::HandleRecvBytes(DWORD bytes)
{
    if (bytes == 0)
    {
        // 상대가 정상 종료(FIN). 우리가 SendAndDisconnect로 끊는 중이었다면 기다리던 바로 그 신호다.
        Disconnect(IsClosing() ? _closeReason : "peer closed");
        return false;
    }

    NetStats::Add(NetStats::RecvBytes, bytes);
    NetStats::Add(NetStats::RecvCalls);

    if (!_recvBuffer.OnWrite(bytes))
    {
        Disconnect("recv buffer overflow");
        return false;
    }

    // 끊는 중: 받은 것은 해석하지 않고 버린다. 그래도 수신은 계속 걸어 둔다(ShutdownSend 참고).
    if (IsClosing())
    {
        _recvBuffer.OnRead(_recvBuffer.DataSize());
        _recvBuffer.Clean();
        return IsConnected();
    }

    const int32 dataSize = static_cast<int32>(_recvBuffer.DataSize());
    const int32 processed = OnRecv(_recvBuffer.ReadPos(), dataSize);//전송받은 데이터 처리
    if (processed < 0 || processed > dataSize || !_recvBuffer.OnRead(static_cast<uint32>(processed)))
    {
        Disconnect("invalid data");
        return false;
    }

    // 이번 묶음을 처리하다가 끊기 시작했다면(핸들러가 SC_KICK) 뒤에 남은 바이트도 버린다.
    if (IsClosing())
        _recvBuffer.OnRead(_recvBuffer.DataSize());

    _recvBuffer.Clean();
 
    if (_recvBuffer.FreeSize() == 0)
    {
        Disconnect("recv buffer full");    // 버퍼를 다 채우고도 하나도 처리 못 함
        return false;
    }
    
    return IsConnected();
}
 
// ───────────────────────── 송신 ─────────────────────────


void Session::Send(SendBufferRef buffer) // sendqueue의 데이터 쌓기
{
    if (!buffer || buffer->WriteSize() == 0 || !IsConnected())
        return;
 
    bool registerSend = false;
    {
        std::lock_guard lock(_sendLock);
        if (_closing.load())
            return;        // 끊는 중: 마지막 패킷 뒤에 아무것도 붙이지 않는다
        _sendQueue.push_back(std::move(buffer));
        if (!_sendRegistered)
        {
            _sendRegistered = true;
            registerSend = true;
        }
    }
    // 락 밖에서 WSASend (락을 잡은 채 외부 호출 금지)
    if (registerSend)
        RegisterSend();
}

void Session::SendAndDisconnect(SendBufferRef last, const char* reason)
{
    if (!IsConnected())
        return;

    bool registerSend = false;
    {
        // Send와 같은 락 안에서 _closing을 켠다. 그래서 "검사는 통과했는데 그 사이 끊기 시작"하는
        // Send가 생기지 않고, 마지막 패킷이 항상 큐의 맨 끝이 된다.
        std::lock_guard lock(_sendLock);
        if (_closing.load())
            return;        // 이미 끊는 중 (먼저 부른 쪽의 이유를 유지)
        _closeReason = reason;
        _closingSinceMs.store(ServerCore::NowMs());    // _closing보다 먼저: 끊는 중을 본 감시자는 시각도 본다
        _closing.store(true);
        if (last && last->WriteSize() > 0)
            _sendQueue.push_back(std::move(last));
        if (!_sendRegistered)
        {
            _sendRegistered = true;
            registerSend = true;
        }
    }
    // 이미 WSASend가 걸려 있으면 그 완료(ProcessSend → RegisterSend)가 큐를 비운 뒤 ShutdownSend로 간다.
    if (registerSend)
        RegisterSend();
}

void Session::ShutdownSend()
{
    // 마지막 패킷까지 커널에 넘겼다. 여기서 바로 Disconnect(→ 소멸자에서 closesocket)하지 않는 이유:
    // 우리 수신 버퍼에 아직 안 읽은 데이터가 있는 채로 소켓을 닫으면 Windows는 FIN 대신 RST를 보내고,
    // RST를 받은 상대는 아직 읽지 않은 SC_KICK까지 버릴 수 있다(상대에게는 "연결이 재설정됨"만 보임).
    // 그래서 FIN만 보내고(상대는 SC_KICK을 다 읽은 뒤 recv 0을 봄) 수신은 계속하면서 버리다가,
    // 상대가 닫으면(recv 0) HandleRecvBytes가 _closeReason으로 Disconnect한다.
    // 상대가 끝내 닫지 않으면 GameServer의 Heartbeat가 하트비트 시간 뒤 강제로 끊는다.
    ::shutdown(_socket, SD_SEND);
}

void Session::RegisterSend()//queue에 쌓인 데이터 한번에 wsasend
{
    while (true)
    {
        bool drained = false;
        bool closing = false;
        {
            std::lock_guard lock(_sendLock);
            if (!IsConnected())
                _sendQueue.clear();
            if (_sendQueue.empty())
            {
                _sendRegistered = false;
                drained = true;
                closing = _closing.load();    // 끊는 중에는 Send가 막혀 있으므로 이 큐는 다시 차지 않는다
            }
            else
            {
                // 쌓인 버퍼를 한 번에 가져가 WSASend 1회로 보낸다(Scatter-Gather).
                _sendEvent.buffers.swap(_sendQueue);
            }
        }
        if (drained)
        {
            if (closing && IsConnected())
                ShutdownSend();
            return;
        }

        _wsaBufs.clear();
        for (const SendBufferRef& buffer : _sendEvent.buffers)
        {
            WSABUF buf;
            buf.buf = buffer->Buffer();
            buf.len = buffer->WriteSize();
            _wsaBufs.push_back(buf);
        }
 
        _sendEvent.Reset();
        _sendEvent.owner = shared_from_this();
        DWORD bytes = 0;

            if (::WSASend(_socket, _wsaBufs.data(), static_cast<DWORD>(_wsaBufs.size()), &bytes, 0,
            &_sendEvent, nullptr) == SOCKET_ERROR)
        {
            const int err = ::WSAGetLastError();
            if (err == WSA_IO_PENDING)
            {
                if (!IsConnected())
                    ::CancelIoEx(GetHandle(), &_sendEvent);
                return;    // 완료되면 ProcessSend
            }
            _sendEvent.owner.reset();
            _sendEvent.buffers.clear();
            {
                std::lock_guard lock(_sendLock);
                _sendQueue.clear();
                _sendRegistered = false;
            }
            HandleError(static_cast<DWORD>(err), "WSASend");
            return;
        }

            // 즉시 성공
        if (!_skipCompletionPort)
            return;
 
        IocpObjectRef self = std::move(_sendEvent.owner);
        if (!HandleSendBytes(bytes))
            return;
        // 그 사이 쌓인 것이 있으면 이어서 보낸다
    }
}

void Session::ProcessSend(DWORD bytes, DWORD error)// error 검증
{
    if (error != 0)//네트워크가 끊겼거나 클라이언트가 강제 종료
    {
        _sendEvent.buffers.clear();
        {
            std::lock_guard lock(_sendLock);
            _sendQueue.clear();
            _sendRegistered = false;
        }
        HandleError(error, "Send");
        return;
    }

    if (HandleSendBytes(bytes))
        RegisterSend();
}

bool Session::HandleSendBytes(DWORD bytes)//byte검증과 후속 전송 이어가기
{
    uint64 expected = 0;
    for (const SendBufferRef& buffer : _sendEvent.buffers)
        expected += buffer->WriteSize();
    _sendEvent.buffers.clear();

    // Overlapped WSASend는 오류가 없으면 전부 보낸다. 일부만 갔다면 비정상이므로 끊는다.
    if (bytes != expected)
    {
        {
            std::lock_guard lock(_sendLock);
            _sendQueue.clear();
            _sendRegistered = false;
        }
        Disconnect("partial send");
        return false;
    }

    NetStats::Add(NetStats::SendBytes, bytes);
    NetStats::Add(NetStats::SendCalls);
    OnSend(static_cast<int32>(bytes));//통계 및 콜백
    return true;
}