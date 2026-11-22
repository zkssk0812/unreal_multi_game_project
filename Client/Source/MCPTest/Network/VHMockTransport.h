#pragma once

#include "CoreMinimal.h"
#include "Network/VHTransport.h"
#include "Network/VHPacket.h"
#include "Network/VHMockServer.h"

/**
 * Mock 서버 전송. 실제 소켓 대신 같은 프로세스 안의 FVHMockServer와 바이트를 주고받는다.
 * 클라이언트 → 서버 방향도 패킷 재조립을 거치고, 서버 → 클라이언트 바이트는
 * bSplitPackets이면 일부러 작은 조각으로 쪼개 보낸다(TCP처럼 끊겨 오는 상황에서 재조립 테스트).
 */
class FVHMockTransport : public IVHTransport
{
public:
	FVHMockTransport(const FVHMockConfig& InConfig, bool bInSplitPackets);

	virtual void Connect(const FString& Host, int32 Port) override;
	virtual void Disconnect() override;
	virtual bool Send(const TArray<uint8>& Bytes) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool IsConnected() const override { return bConnected; }
	virtual FString GetDescription() const override { return TEXT("Mock 서버(가짜 서버)"); }

	/** 개발용: 같은 프로세스 안의 가짜 서버 (게임 결과 보고 등 프로토콜 밖 통로) */
	FVHMockServer* GetServer() const { return Server.Get(); }

private:
	void ResetServer();

	FVHMockConfig Config;
	bool bSplitPackets = true;
	TUniquePtr<FVHMockServer> Server;
	FVHPacketAssembler ToServer;
	TArray<uint8> ToClient;
	bool bConnecting = false;
	bool bConnected = false;
	double ConnectAt = 0.0;
	bool bServerClosed = false;
	FString ServerCloseReason;
};
