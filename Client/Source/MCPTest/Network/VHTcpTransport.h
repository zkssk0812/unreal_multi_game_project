#pragma once

#include "CoreMinimal.h"
#include "Network/VHTransport.h"

class FSocket;

/**
 * 진짜 서버용 TCP 전송.
 * 논블로킹 FSocket을 게임 스레드 Tick에서 폴링한다(별도 수신 스레드 없음).
 * 연결 → 쓰기 가능 여부로 연결 완료 확인, 수신 → 쌓인 만큼 읽어 그대로 위로 넘김,
 * 송신 → 한 번에 다 못 보내면 남은 바이트를 보관했다가 다음 Tick에 이어서 보낸다.
 */
class FVHTcpTransport : public IVHTransport
{
public:
	explicit FVHTcpTransport(float InConnectTimeoutSeconds);
	virtual ~FVHTcpTransport() override;

	virtual void Connect(const FString& Host, int32 Port) override;
	virtual void Disconnect() override;
	virtual bool Send(const TArray<uint8>& Bytes) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool IsConnected() const override { return State == EState::Connected; }
	virtual FString GetDescription() const override;

private:
	enum class EState : uint8 { Idle, Connecting, Connected };

	void CloseSocket();
	void Fail(const FString& Reason);
	bool FlushSend();

	FSocket* Socket = nullptr;
	EState State = EState::Idle;
	double ConnectStartTime = 0.0;
	float ConnectTimeoutSeconds = 5.f;
	FString RemoteDesc;
	TArray<uint8> PendingSend;
	TArray<uint8> RecvScratch;
};
