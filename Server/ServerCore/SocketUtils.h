#pragma once

#include "CorePch.h"

//깔끔하게 포장(Wrapping)하기 위해 만든 유틸리티(도우미) 파일
//LPFN은 Long Pointer to FunctioN의 약자로 함수 포인터
namespace SocketUtils{

inline constexpr DWORD AcceptAddressLength = sizeof(SOCKADDR_IN) + 16;

extern LPFN_ACCEPTEX AcceptEx;
extern LPFN_GETACCEPTEXSOCKADDRS GetAcceptExSockaddrs;// 서버가 읽기 편하게 예쁘게 해독(파싱)해 주는 전용 번역기
extern LPFN_CONNECTEX ConnectEx;
extern LPFN_DISCONNECTEX DisconnectEx;

extern bool CanSkipCompletionPort;

bool Init();
void Clear();

SOCKET CreateSocket();	// Overlapped TCP 소켓
void Close(SOCKET& socket);

bool SetLinger(SOCKET socket, bool onOff, uint16 seconds);
bool SetTcpNoDelay(SOCKET socket, bool enable);
bool SetUpdateAcceptContext(SOCKET acceptSocket, SOCKET listenSocket);

bool BindAnyAddress(SOCKET socket, uint16 port);
bool Listen(SOCKET socket, int backlog = SOMAXCONN);

std::string ToString(const SOCKADDR_IN& address);


}