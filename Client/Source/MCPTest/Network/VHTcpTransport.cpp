#include "Network/VHTcpTransport.h"

#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "AddressInfoTypes.h"

DEFINE_LOG_CATEGORY_STATIC(LogVHTcp, Log, All);

FVHTcpTransport::FVHTcpTransport(float InConnectTimeoutSeconds)
	: ConnectTimeoutSeconds(InConnectTimeoutSeconds)
{
	RecvScratch.SetNumUninitialized(16 * 1024);
}

FVHTcpTransport::~FVHTcpTransport()
{
	CloseSocket();
}

FString FVHTcpTransport::GetDescription() const
{
	return FString::Printf(TEXT("TCP %s"), *RemoteDesc);
}

void FVHTcpTransport::Connect(const FString& Host, int32 Port)
{
	CloseSocket();
	RemoteDesc = FString::Printf(TEXT("%s:%d"), *Host, Port);

	ISocketSubsystem* Subsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!Subsystem)
	{
		Fail(TEXT("소켓 시스템을 사용할 수 없습니다."));
		return;
	}

	// IP 문자열이면 바로 쓰고, 아니면 호스트 이름을 조회한다(로컬 개발용이라 동기 조회로 충분).
	TSharedPtr<FInternetAddr> Addr = Subsystem->CreateInternetAddr();
	bool bIsValidIp = false;
	Addr->SetIp(*Host, bIsValidIp);
	if (!bIsValidIp)
	{
		const FAddressInfoResult Info = Subsystem->GetAddressInfo(*Host, nullptr, EAddressInfoFlags::Default, NAME_None, ESocketType::SOCKTYPE_Streaming);
		if (Info.ReturnCode != SE_NO_ERROR || Info.Results.Num() == 0)
		{
			Fail(FString::Printf(TEXT("서버 주소(%s)를 찾을 수 없습니다."), *Host));
			return;
		}
		Addr = Info.Results[0].Address;
	}
	Addr->SetPort(Port);

	Socket = Subsystem->CreateSocket(NAME_Stream, TEXT("VaultHeistClient"), Addr->GetProtocolType());
	if (!Socket)
	{
		Fail(TEXT("소켓을 만들 수 없습니다."));
		return;
	}
	Socket->SetNonBlocking(true);
	Socket->SetNoDelay(true);

	if (!Socket->Connect(*Addr))
	{
		const ESocketErrors Err = Subsystem->GetLastErrorCode();
		Fail(FString::Printf(TEXT("서버에 연결할 수 없습니다. (%s)"), Subsystem->GetSocketError(Err)));
		return;
	}

	State = EState::Connecting;
	ConnectStartTime = FPlatformTime::Seconds();
	UE_LOG(LogVHTcp, Log, TEXT("연결 시도: %s"), *RemoteDesc);
}

void FVHTcpTransport::Disconnect()
{
	CloseSocket();
}

bool FVHTcpTransport::Send(const TArray<uint8>& Bytes)
{
	if (State != EState::Connected)
	{
		return false;
	}
	PendingSend.Append(Bytes);
	return FlushSend();
}

bool FVHTcpTransport::FlushSend()
{
	while (PendingSend.Num() > 0 && Socket)
	{
		int32 Sent = 0;
		if (!Socket->Send(PendingSend.GetData(), PendingSend.Num(), Sent))
		{
			const ESocketErrors Err = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLastErrorCode();
			if (Err == SE_EWOULDBLOCK)
			{
				return true;	// 송신 버퍼가 꽉 참 → 다음 Tick에 이어서
			}
			CloseSocket();
			if (OnDisconnected) OnDisconnected(TEXT("데이터를 보내는 중 연결이 끊어졌습니다."));
			return false;
		}
		if (Sent <= 0)
		{
			return true;
		}
		PendingSend.RemoveAt(0, Sent, EAllowShrinking::No);
	}
	return true;
}

void FVHTcpTransport::Tick(float DeltaSeconds)
{
	if (!Socket)
	{
		return;
	}

	if (State == EState::Connecting)
	{
		const ESocketConnectionState ConnState = Socket->GetConnectionState();
		if (ConnState == SCS_Connected)
		{
			State = EState::Connected;
			UE_LOG(LogVHTcp, Log, TEXT("연결됨: %s"), *RemoteDesc);
			if (OnConnected) OnConnected();
		}
		else if (ConnState == SCS_ConnectionError)
		{
			Fail(FString::Printf(TEXT("서버(%s)에 연결할 수 없습니다. 서버가 켜져 있는지 확인해 주세요."), *RemoteDesc));
			return;
		}
		else if (FPlatformTime::Seconds() - ConnectStartTime > ConnectTimeoutSeconds)
		{
			Fail(FString::Printf(TEXT("서버(%s) 연결 시간이 초과되었습니다."), *RemoteDesc));
			return;
		}
		if (State != EState::Connected)
		{
			return;
		}
	}

	if (!FlushSend() || !Socket)
	{
		return;
	}

	// 받을 수 있는 만큼 모두 읽는다. 패킷 경계는 신경 쓰지 않는다(위에서 재조립).
	for (;;)
	{
		int32 Read = 0;
		const bool bOk = Socket->Recv(RecvScratch.GetData(), RecvScratch.Num(), Read);
		if (!bOk)
		{
			// 스트림 소켓에서 false = 상대가 연결을 닫았거나 오류
			CloseSocket();
			if (OnDisconnected) OnDisconnected(TEXT("서버와의 연결이 끊어졌습니다."));
			return;
		}
		if (Read <= 0)
		{
			break;	// 지금은 더 받을 데이터 없음
		}
		if (OnBytesReceived) OnBytesReceived(RecvScratch.GetData(), Read);
		if (!Socket)
		{
			return;	// 콜백 안에서 끊었을 수 있다
		}
	}
}

void FVHTcpTransport::Fail(const FString& Reason)
{
	UE_LOG(LogVHTcp, Warning, TEXT("연결 실패: %s"), *Reason);
	CloseSocket();
	if (OnConnectFailed) OnConnectFailed(Reason);
}

void FVHTcpTransport::CloseSocket()
{
	if (Socket)
	{
		Socket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
		Socket = nullptr;
	}
	State = EState::Idle;
	PendingSend.Reset();
}
