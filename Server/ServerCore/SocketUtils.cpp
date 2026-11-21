#include "SocketUtils.h"

namespace SocketUtils {

//함수포인터를 쓰는 이유 -> 함수를 호출 할때마다 커널에게 함수 주소를 받아와야 하기에 미리 주소를 받아와 커널을 호출하지 않기 위해
LPFN_ACCEPTEX AcceptEx = nullptr;
LPFN_GETACCEPTEXSOCKADDRS GetAcceptExSockaddrs = nullptr;
LPFN_CONNECTEX ConnectEx = nullptr;
LPFN_DISCONNECTEX DisconnectEx = nullptr;
bool CanSkipCompletionPort = false;

namespace {

bool AllTcpProvidersAreIfs()
//이 컴퓨터에 껴든 이상한 백신이나 방화벽(LSP)이 있는지 검사하는 함수
{
	DWORD size = 0;
	::WSAEnumProtocolsW(nullptr, nullptr, &size);
	if (size == 0)
		return false;

	std::vector<char> buffer(size);
	auto* protocols = reinterpret_cast<WSAPROTOCOL_INFOW*>(buffer.data());
	const int count = ::WSAEnumProtocolsW(nullptr, protocols, &size);
	if (count == SOCKET_ERROR)
		return false;

	for (int i = 0; i < count; ++i)
	{
		const WSAPROTOCOL_INFOW& p = protocols[i];
		if (p.iAddressFamily == AF_INET && p.iProtocol == IPPROTO_TCP && (p.dwServiceFlags1 & XP1_IFS_HANDLES) == 0)
			return false;
	}
	return true;
}


template <class Fn>
bool LoadExtension(SOCKET socket, GUID guid, Fn* fn)
//운영체제에게 특수 비급 함수(AcceptEx 등)의 메모리 주소를 달라고 조르는 함수
{
	DWORD bytes = 0;
    //WSAIoctl은 소켓을 제어하는 만능 리모컨 같은 윈도우 함수
	return ::WSAIoctl(socket, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&guid, sizeof(guid), fn, sizeof(*fn), &bytes, nullptr, nullptr) != SOCKET_ERROR;
}

template <class T>
bool SetOption(SOCKET socket, int level, int name, const T& value)
//소켓의 설정(옵션) 포장 함수
{
	return ::setsockopt(socket, level, name, reinterpret_cast<const char*>(&value), sizeof(T)) != SOCKET_ERROR;
}

}//이름이 없는 namespace { ... } 안에 들어있는 함수들은 이 파일 안에서만 쓰려고 만든 내부용 도우미(Helper) 함수


bool Init()
{
	//Windows 전용 고성능 API의 함수 포인터를 가져오고, OS 단의 통지 최적화 옵션을 켤 수 있는지 검사하는 초기화 작업
    SOCKET dummy = CreateSocket();
    if(dummy == INVALID_SOCKET) return false;

    const bool ok = 
        LoadExtension(dummy, WSAID_ACCEPTEX, &AcceptEx) &&
        LoadExtension(dummy, WSAID_GETACCEPTEXSOCKADDRS, &GetAcceptExSockaddrs) &&
        LoadExtension(dummy, WSAID_CONNECTEX, &ConnectEx) &&
        LoadExtension(dummy, WSAID_DISCONNECTEX, &DisconnectEx);

    Close(dummy);

    CanSkipCompletionPort = AllTcpProvidersAreIfs();
    if(!CanSkipCompletionPort)
        LOG_WARN("비-IFS TCP 공급자 발견: FILE_SKIP_COMPLETION_PORT_ON_SUCCESS를 쓰지 않음");

    return ok;
}

void Close(SOCKET& socket)
{
	if (socket != INVALID_SOCKET)
		::closesocket(socket);
	socket = INVALID_SOCKET;
}

void Clear()
{
	AcceptEx = nullptr;
	GetAcceptExSockaddrs = nullptr;
	ConnectEx = nullptr;
	DisconnectEx = nullptr;
	CanSkipCompletionPort = false;
}

SOCKET CreateSocket()
{
	return ::WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
}

bool SetLinger(SOCKET socket, bool onOff, uint16 seconds)
{
	LINGER option{};
	option.l_onoff = onOff ? 1 : 0;
	option.l_linger = seconds;
	return SetOption(socket, SOL_SOCKET, SO_LINGER, option);
}

bool SetTcpNoDelay(SOCKET socket, bool enable)
{
	const BOOL value = enable ? TRUE : FALSE;
	return SetOption(socket, IPPROTO_TCP, TCP_NODELAY, value);
}

bool SetUpdateAcceptContext(SOCKET acceptSocket, SOCKET listenSocket)
{
	// AcceptEx로 받은 소켓은 이 옵션 전까지 listen 소켓의 속성을 모른다
	// (getpeername, shutdown 등이 실패). 완료 직후 반드시 호출.
	return SetOption(acceptSocket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, listenSocket);
}

bool BindAnyAddress(SOCKET socket, uint16 port)
{
	SOCKADDR_IN address{};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = ::htonl(INADDR_ANY);
	address.sin_port = ::htons(port);
	return ::bind(socket, reinterpret_cast<const SOCKADDR*>(&address), sizeof(address)) != SOCKET_ERROR;
}

bool Listen(SOCKET socket, int backlog)
{
	return ::listen(socket, backlog) != SOCKET_ERROR;
}

std::string ToString(const SOCKADDR_IN& address)
{
	char ip[INET_ADDRSTRLEN] = {};
	::inet_ntop(AF_INET, &address.sin_addr, ip, sizeof(ip));
	return std::format("{}:{}", ip, ::ntohs(address.sin_port));
}

//::가 붙어있는 것만으로도 이 함수가 우리 프로젝트 내부 로직인지, 
//아니면 외부 운영체제의 기능인지 마우스를 올려보지 않고도 1초 만에 구별
}