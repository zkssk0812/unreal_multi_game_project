#include "Network/VHMockTransport.h"

FVHMockTransport::FVHMockTransport(const FVHMockConfig& InConfig, bool bInSplitPackets)
	: Config(InConfig)
	, bSplitPackets(bInSplitPackets)
{
}

void FVHMockTransport::ResetServer()
{
	Server = MakeUnique<FVHMockServer>(Config);
	Server->SendToClient = [this](const TArray<uint8>& Bytes) { ToClient.Append(Bytes); };
	Server->DisconnectClient = [this](const FString& Reason)
	{
		// 이미 쌓인 바이트(SC_KICK 등)를 먼저 전달한 뒤 끊는다.
		bServerClosed = true;
		ServerCloseReason = Reason;
	};
	ToServer.Reset();
	ToClient.Reset();
	bServerClosed = false;
}

void FVHMockTransport::Connect(const FString& Host, int32 Port)
{
	ResetServer();
	bConnected = false;
	bConnecting = true;
	ConnectAt = FPlatformTime::Seconds() + 0.3;	// 접속에 약간 시간이 걸리는 척
}

void FVHMockTransport::Disconnect()
{
	bConnecting = false;
	bConnected = false;
	Server.Reset();
	ToServer.Reset();
	ToClient.Reset();
}

bool FVHMockTransport::Send(const TArray<uint8>& Bytes)
{
	if (!bConnected || !Server || bServerClosed)
	{
		return false;
	}
	ToServer.Append(Bytes.GetData(), Bytes.Num());
	TArray<uint8> Packet;
	for (;;)
	{
		const FVHPacketAssembler::EResult R = ToServer.Pop(Packet);
		if (R == FVHPacketAssembler::EResult::NeedMore)
		{
			break;
		}
		if (R == FVHPacketAssembler::EResult::Error)
		{
			bServerClosed = true;
			ServerCloseReason = TEXT("[Mock] 잘못된 패킷 크기");
			break;
		}
		Server->ReceivePacket(Packet.GetData(), Packet.Num());
	}
	return true;
}

void FVHMockTransport::Tick(float DeltaSeconds)
{
	const double Now = FPlatformTime::Seconds();
	if (bConnecting)
	{
		if (Now >= ConnectAt)
		{
			bConnecting = false;
			bConnected = true;
			if (OnConnected) OnConnected();
		}
		return;
	}
	if (!bConnected || !Server)
	{
		return;
	}

	Server->Tick(Now);

	// 서버가 보낸 바이트 전달. 조각내기 모드면 1~(남은 길이) 사이 무작위 크기로 여러 번에 나눠 보낸다.
	while (ToClient.Num() > 0 && bConnected)
	{
		const int32 Chunk = bSplitPackets ? FMath::RandRange(1, FMath::Min(ToClient.Num(), 97)) : ToClient.Num();
		TArray<uint8> Piece(ToClient.GetData(), Chunk);
		ToClient.RemoveAt(0, Chunk, EAllowShrinking::No);
		if (OnBytesReceived) OnBytesReceived(Piece.GetData(), Piece.Num());
	}

	if (bServerClosed && bConnected)
	{
		const FString Reason = ServerCloseReason;
		Disconnect();
		if (OnDisconnected) OnDisconnected(Reason);
	}
}
